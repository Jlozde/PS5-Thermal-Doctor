/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Small HTTP server: one thread per connection, standalone JSON writer.
 * The web UI (web/index.html) is embedded into the ELF at build time.
 *
 *  GET  /                      UI
 *  GET  /api/status?lang=en    live values + overall verdict
 *  GET  /api/live?minutes=30&step=5
 *  GET  /api/sessions          session summaries + file list
 *  GET  /api/session?file=X    a session CSV
 *  GET  /api/config            config.ini text
 *  POST /api/config            body = new config.ini
 *  POST /api/baseline/reset    new baseline after cleaning
 *  POST /api/notify/test       try a PS5 notification
 *  GET  /api/log?lang=en       latest log lines
 *
 * "lang" (en / tr) picks the language of human-readable text; without it the
 * console language is used.
 */
#include "http.h"
#include "analysis.h"
#include "config.h"
#include "i18n.h"
#include "log.h"
#include "platform.h"
#include "ps5td.h"
#include "sampler.h"
#include "util.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern const unsigned char web_index_html[];
extern const unsigned int  web_index_html_len;

#define MAX_CONN  8
#define HDR_MAX   8192
#define BODY_MAX  32768
#define NONE_F    (-999.0)

static pthread_mutex_t g_conn_lock = PTHREAD_MUTEX_INITIALIZER;
static int      g_conns;
static uint64_t g_start_ms;

/* ------------------------------------------------------------ JSON buffer */

typedef struct { char *buf; size_t len, cap; int oom; } jb_t;

static int
jb_grow(jb_t *j, size_t extra) {
  if(j->len + extra + 1 <= j->cap) return 0;
  size_t cap = j->cap ? j->cap * 2 : 4096;
  while(cap < j->len + extra + 1) cap *= 2;
  char *nb = realloc(j->buf, cap);
  if(!nb) { j->oom = 1; return -1; }
  j->buf = nb; j->cap = cap;
  return 0;
}

static void
jb_puts(jb_t *j, const char *s) {
  size_t l = strlen(s);
  if(jb_grow(j, l)) return;
  memcpy(j->buf + j->len, s, l);
  j->len += l;
  j->buf[j->len] = '\0';
}

static void jb_printf(jb_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void
jb_printf(jb_t *j, const char *fmt, ...) {
  char tmp[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if(n < 0) return;
  if((size_t)n >= sizeof(tmp)) n = (int)sizeof(tmp) - 1;
  if(jb_grow(j, (size_t)n)) return;
  memcpy(j->buf + j->len, tmp, (size_t)n);
  j->len += (size_t)n;
  j->buf[j->len] = '\0';
}

static void
jb_str(jb_t *j, const char *s) {
  jb_puts(j, "\"");
  for(; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    if(c == '"' || c == '\\') { char e[3] = { '\\', (char)c, 0 }; jb_puts(j, e); }
    else if(c == '\n') jb_puts(j, "\\n");
    else if(c < 0x20) jb_printf(j, "\\u%04x", c);
    else { char e[2] = { (char)c, 0 }; jb_puts(j, e); }
  }
  jb_puts(j, "\"");
}

/* Invalid values (sentinels) become JSON null. */
static void
jb_num(jb_t *j, double v, double invalid_below) {
  if(v < invalid_below) jb_puts(j, "null");
  else jb_printf(j, "%.1f", v);
}

static void
jb_sample(jb_t *j, const sample_t *s) {
  jb_printf(j, "{\"wall_ms\":%llu,\"game\":%s,\"cpu_live\":%s", (unsigned long long)s->wall_ms,
            s->game ? "true" : "false", s->cpu_live ? "true" : "false");
  jb_puts(j, ",\"cpu_c\":");  jb_num(j, s->cpu_c, 0);
  jb_puts(j, ",\"soc_c\":");  jb_num(j, s->soc_c, 0);
  jb_puts(j, ",\"gpu_c\":");  jb_num(j, s->gpu_c, 0);
  jb_puts(j, ",\"fan_pct\":"); jb_num(j, s->fan_pct10 < 0 ? -1 : s->fan_pct10 / 10.0, 0);
  jb_puts(j, ",\"fan_raw\":"); jb_num(j, s->fan_raw, 0);
  jb_puts(j, ",\"power_w\":"); jb_num(j, s->power_dw < 0 ? -1 : s->power_dw / 10.0, 0);
  jb_puts(j, ",\"gpu_w\":");  jb_num(j, s->gpu_w10 < 0 ? -1 : s->gpu_w10 / 10.0, 0);
  jb_puts(j, ",\"cpu_w\":");  jb_num(j, s->cpu_w10 < 0 ? -1 : s->cpu_w10 / 10.0, 0);
  jb_puts(j, ",\"mem_w\":");  jb_num(j, s->mem_w10 < 0 ? -1 : s->mem_w10 / 10.0, 0);
  jb_puts(j, ",\"fps\":");    jb_num(j, s->fps10 < 0 ? -1 : s->fps10 / 10.0, 0);
  jb_puts(j, ",\"soc_raw\":[");
  for(int i = 0; i < PS5TD_SOC_CHANNELS; i++) { if(i) jb_puts(j, ","); jb_num(j, s->soc_raw[i], 0); }
  jb_puts(j, "]}");
}

static void
jb_summary(jb_t *j, const session_summary_t *s) {
  char when[32];
  util_fmt_utc(s->start_ms, when, sizeof(when), 0);
  jb_printf(j, "{\"start_ms\":%llu,\"start\":", (unsigned long long)s->start_ms);
  jb_str(j, when);
  jb_printf(j, ",\"epoch\":%u,\"title\":", s->epoch);
  jb_str(j, s->title);
  jb_printf(j, ",\"dur_s\":%u,\"ss_valid\":%s,\"ss_samples\":%u", s->dur_s, s->ss_valid ? "true" : "false", s->ss_samples);
  jb_puts(j, ",\"ss_soc\":");   jb_num(j, s->ss_soc, 0);
  jb_puts(j, ",\"ss_cpu\":");   jb_num(j, s->ss_cpu, 0);
  jb_puts(j, ",\"ss_fan\":");   jb_num(j, s->ss_fan, 0);
  jb_puts(j, ",\"ss_power\":"); jb_num(j, s->power_valid ? s->ss_power : -1, 0);
  jb_puts(j, ",\"ss_fps\":");   jb_num(j, s->fps_valid ? s->ss_fps : -1, 0);
  jb_puts(j, ",\"max_soc\":");  jb_num(j, s->max_soc, 0);
  jb_puts(j, ",\"max_cpu\":");  jb_num(j, s->max_cpu, 0);
  jb_puts(j, ",\"max_fan\":");  jb_num(j, s->max_fan, 0);
  jb_puts(j, ",\"warm_rate\":"); jb_num(j, s->warm_rate, NONE_F);
  jb_printf(j, ",\"hot_share\":%.3f,\"danger_s\":%u", (double)s->hot_share, s->danger_s);
  jb_puts(j, ",\"cdi\":");      jb_num(j, s->cdi, NONE_F);
  jb_printf(j, ",\"verdict\":%d,\"verdict_name\":", s->verdict);
  jb_str(j, an_verdict_name(s->verdict));
  jb_puts(j, "}");
}

static void
jb_overall(jb_t *j, const overall_t *o, int lang) {
  jb_printf(j, "{\"verdict\":%d,\"name\":", o->verdict);
  jb_str(j, an_verdict_name(o->verdict));
  jb_puts(j, ",\"badge\":");  jb_str(j, an_verdict_badge(o->verdict, lang));
  jb_puts(j, ",\"label\":");  jb_str(j, an_verdict_label(o->verdict, lang));
  jb_puts(j, ",\"advice\":"); jb_str(j, an_verdict_advice(o->verdict, lang));
  jb_printf(j, ",\"verdict_abs\":%d,\"reason_abs\":", o->verdict_abs); jb_str(j, o->reason_abs[lang]);
  jb_printf(j, ",\"verdict_rel\":%d,\"reason_rel\":", o->verdict_rel); jb_str(j, o->reason_rel[lang]);
  jb_puts(j, ",\"trend_text\":"); jb_str(j, o->trend_text[lang]);
  jb_printf(j, ",\"trend_valid\":%s,\"trend_per_30d\":%.2f,\"drift\":%.2f,\"recent_cdi\":%.2f,\"recent_n\":%u",
            o->trend_valid ? "true" : "false", (double)o->trend_per_30d, (double)o->drift,
            (double)o->recent_cdi, o->recent_n);
  jb_printf(j, ",\"baseline\":{\"valid\":%s,\"n\":%u,\"need\":%u,\"cdi_mean\":%.2f,\"cdi_sd\":%.2f,\"first_ms\":%llu,\"last_ms\":%llu}",
            o->baseline.valid ? "true" : "false", o->baseline.n, o->baseline.need,
            (double)o->baseline.cdi_mean, (double)o->baseline.cdi_sd,
            (unsigned long long)o->baseline.first_ms, (unsigned long long)o->baseline.last_ms);
  jb_puts(j, ",\"last\":");
  if(o->has_last) jb_summary(j, &o->last); else jb_puts(j, "null");
  jb_puts(j, "}");
}

/* ------------------------------------------------------------ request/response */

typedef struct {
  char   method[8];
  char   path[256];
  char   query[512];
  char  *body;
  size_t body_len;
} req_t;

static int
ci_prefix(const char *s, const char *prefix) {
  for(; *prefix; s++, prefix++) {
    char a = *s, b = *prefix;
    if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if(a != b) return 0;
  }
  return 1;
}

static size_t
header_content_length(const char *hdr) {
  const char *p = hdr;
  while((p = strstr(p, "\r\n")) != NULL) {
    p += 2;
    if(ci_prefix(p, "content-length:")) return (size_t)strtoul(p + 15, NULL, 10);
  }
  return 0;
}

static void
send_all(int fd, const void *buf, size_t len) {
  const char *p = (const char *)buf;
  while(len > 0) {
    ssize_t n = write(fd, p, len);
    if(n <= 0) return;
    p += n; len -= (size_t)n;
  }
}

static void
respond(int fd, int code, const char *ctype, const void *body, size_t len) {
  const char *txt = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 404 ? "Not Found"
                  : code == 405 ? "Method Not Allowed" : code == 503 ? "Service Unavailable" : "Error";
  char h[320];
  int n = snprintf(h, sizeof(h),
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                   "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
                   code, txt, ctype, (unsigned long)len);
  send_all(fd, h, (size_t)n);
  if(len) send_all(fd, body, len);
}

static void
respond_json(int fd, jb_t *j) {
  if(j->oom || !j->buf) respond(fd, 503, "text/plain", "oom", 3);
  else respond(fd, 200, "application/json; charset=utf-8", j->buf, j->len);
  free(j->buf);
}

static void
respond_text(int fd, int code, const char *s) {
  respond(fd, code, "text/plain; charset=utf-8", s, strlen(s));
}

static int
read_request(int fd, req_t *r, char *hdr, size_t cap) {
  size_t len = 0;
  char *end = NULL;
  memset(r, 0, sizeof(*r));
  while(len < cap - 1) {
    ssize_t n = read(fd, hdr + len, cap - 1 - len);
    if(n <= 0) return -1;
    len += (size_t)n;
    hdr[len] = '\0';
    end = strstr(hdr, "\r\n\r\n");
    if(end) break;
  }
  if(!end) return -1;

  char target[800];
  if(sscanf(hdr, "%7s %799s", r->method, target) != 2) return -1;
  char *q = strchr(target, '?');
  if(q) { *q = '\0'; snprintf(r->query, sizeof(r->query), "%s", q + 1); }
  snprintf(r->path, sizeof(r->path), "%s", target);

  size_t clen = header_content_length(hdr);
  if(clen > BODY_MAX) return -1;
  r->body = malloc(clen + 1);
  if(!r->body) return -1;
  size_t have = len - (size_t)(end + 4 - hdr);
  if(have > clen) have = clen;
  memcpy(r->body, end + 4, have);
  size_t got = have;
  while(got < clen) {
    ssize_t n = read(fd, r->body + got, clen - got);
    if(n <= 0) break;
    got += (size_t)n;
  }
  r->body[got] = '\0';
  r->body_len = got;
  return 0;
}

/* Takes b out of ?a=1&b=2 */
static int
query_get(const char *query, const char *key, char *out, size_t n) {
  size_t kl = strlen(key);
  const char *p = query;
  while(p && *p) {
    const char *amp = strchr(p, '&');
    size_t seg = amp ? (size_t)(amp - p) : strlen(p);
    if(seg > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
      size_t vl = seg - kl - 1;
      if(vl >= n) vl = n - 1;
      memcpy(out, p + kl + 1, vl);
      out[vl] = '\0';
      return 1;
    }
    p = amp ? amp + 1 : NULL;
  }
  return 0;
}

/* ?lang=en|tr, otherwise the console language */
static int
query_lang(const char *query) {
  char v[16];
  if(query_get(query, "lang", v, sizeof(v))) {
    int l = i18n_parse(v);
    if(l == LANG_EN || l == LANG_TR) return l;
  }
  return i18n_get();
}

/* ------------------------------------------------------------ handlers */

static void
h_status(int fd, const char *query) {
  int lang = query_lang(query);
  status_t st;
  sampler_get_status(&st);
  config_t cfg;
  sampler_get_config(&cfg);
  char fw[16], ip[48] = "?";
  plat_fw_text(fw, sizeof(fw));
  plat_local_ip(ip, sizeof(ip));

  jb_t j = {0};
  jb_puts(&j, "{\"app\":\"ps5td\",\"version\":\"" PS5TD_VERSION "\"");
  jb_printf(&j, ",\"pid\":%d,\"fw\":", (int)getpid()); jb_str(&j, fw);
  jb_puts(&j, ",\"ip\":"); jb_str(&j, ip);
  jb_printf(&j, ",\"lang\":\"%s\",\"console_lang\":\"%s\",\"config_lang\":\"%s\",\"temp_unit\":\"%s\"",
            i18n_code(lang), i18n_code(i18n_get()), i18n_code(cfg.lang), i18n_unit_code(cfg.temp_unit));
  jb_printf(&j, ",\"uptime_s\":%llu", (unsigned long long)((util_mono_ms() - g_start_ms) / 1000ULL));
  jb_puts(&j, ",\"symbols\":"); jb_str(&j, plat_symbols_text(lang));
  jb_puts(&j, ",\"dce\":"); jb_str(&j, plat_dce_text(lang));
  jb_printf(&j, ",\"samples_total\":%u,\"history_n\":%u,\"epoch\":%u,\"epoch_since_ms\":%llu",
            st.samples_total, st.history_n, st.epoch, (unsigned long long)st.epoch_since_ms);
  jb_puts(&j, ",\"now\":"); jb_sample(&j, &st.now);
  jb_printf(&j, ",\"session\":{\"active\":%s,\"title\":", st.session_active ? "true" : "false");
  jb_str(&j, st.title);
  jb_printf(&j, ",\"dur_s\":%u,\"live\":", st.session_s);
  if(st.live_valid) jb_summary(&j, &st.live); else jb_puts(&j, "null");
  jb_printf(&j, ",\"live_verdict\":%d,\"live_reason\":", st.live_valid ? st.live_verdict : VERDICT_NA);
  jb_str(&j, st.live_valid ? st.live_reason[lang] : "");
  jb_puts(&j, "},\"overall\":"); jb_overall(&j, &st.overall, lang);
  jb_puts(&j, "}");
  respond_json(fd, &j);
}

static void
h_live(int fd, const char *query) {
  char v[16];
  unsigned minutes = 30, step = 5;
  if(query_get(query, "minutes", v, sizeof(v))) minutes = (unsigned)atoi(v);
  if(query_get(query, "step", v, sizeof(v))) step = (unsigned)atoi(v);
  if(minutes < 1) minutes = 1;
  if(minutes > 120) minutes = 120;
  if(step < 1) step = 1;
  unsigned max = minutes * 60 / step;
  if(max > 1500) max = 1500;
  sample_t *buf = malloc(sizeof(sample_t) * max);
  if(!buf) { respond_text(fd, 503, "oom"); return; }
  unsigned n = sampler_get_recent(buf, max, step);

  jb_t j = {0};
  jb_printf(&j, "{\"step\":%u,\"n\":%u", step, n);
  const char *fields[] = { "t", "cpu", "soc", "gpu", "fan", "pw", "fps", "game" };
  for(int f = 0; f < 8; f++) {
    jb_printf(&j, ",\"%s\":[", fields[f]);
    for(unsigned i = 0; i < n; i++) {
      const sample_t *s = &buf[i];
      if(i) jb_puts(&j, ",");
      switch(f) {
        case 0: jb_printf(&j, "%llu", (unsigned long long)s->wall_ms); break;
        case 1: jb_num(&j, s->cpu_c, 0); break;
        case 2: jb_num(&j, s->soc_c, 0); break;
        case 3: jb_num(&j, s->gpu_c, 0); break;
        case 4: jb_num(&j, s->fan_pct10 < 0 ? -1 : s->fan_pct10 / 10.0, 0); break;
        case 5: jb_num(&j, s->power_dw < 0 ? -1 : s->power_dw / 10.0, 0); break;
        case 6: jb_num(&j, s->fps10 < 0 ? -1 : s->fps10 / 10.0, 0); break;
        default: jb_puts(&j, s->game ? "1" : "0"); break;
      }
    }
    jb_puts(&j, "]");
  }
  jb_puts(&j, "}");
  free(buf);
  respond_json(fd, &j);
}

static void
h_sessions(int fd) {
  static session_summary_t hist[PS5TD_HISTORY_MAX];
  static pthread_mutex_t hl = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&hl);
  unsigned n = sampler_get_history(hist, PS5TD_HISTORY_MAX);
  jb_t j = {0};
  jb_puts(&j, "{\"history\":[");
  for(unsigned i = 0; i < n; i++) { if(i) jb_puts(&j, ","); jb_summary(&j, &hist[i]); }
  pthread_mutex_unlock(&hl);
  jb_puts(&j, "],\"files\":[");
  DIR *d = opendir(PS5TD_SESSION_DIR);
  if(d) {
    struct dirent *e; int first = 1;
    while((e = readdir(d)) != NULL) {
      if(e->d_name[0] == '.') continue;
      if(!first) jb_puts(&j, ",");
      first = 0;
      jb_str(&j, e->d_name);
    }
    closedir(d);
  }
  jb_puts(&j, "]}");
  respond_json(fd, &j);
}

static void
h_session_file(int fd, const char *query) {
  char name[128];
  if(!query_get(query, "file", name, sizeof(name)) || !util_safe_name(name)) { respond_text(fd, 400, "file?"); return; }
  char path[320];
  snprintf(path, sizeof(path), "%s/%s", PS5TD_SESSION_DIR, name);
  FILE *f = fopen(path, "rb");
  if(!f) { respond_text(fd, 404, "not found"); return; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(sz < 0) sz = 0;
  char h[256];
  int n = snprintf(h, sizeof(h), "HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\nContent-Length: %ld\r\n"
                   "Content-Disposition: attachment; filename=\"%s\"\r\nConnection: close\r\n\r\n", sz, name);
  send_all(fd, h, (size_t)n);
  char buf[4096];
  size_t r;
  while((r = fread(buf, 1, sizeof(buf), f)) > 0) send_all(fd, buf, r);
  fclose(f);
}

static void
h_config_get(int fd) {
  config_t c;
  sampler_get_config(&c);
  char buf[8192];
  size_t len = config_to_text(&c, buf, sizeof(buf));
  respond(fd, 200, "text/plain; charset=utf-8", buf, len);
}

static void
h_config_post(int fd, const req_t *r) {
  int bad = sampler_set_config_text(r->body ? r->body : "");
  jb_t j = {0};
  jb_printf(&j, "{\"ok\":true,\"bad_lines\":%d}", bad);
  respond_json(fd, &j);
}

static void
h_log(int fd, const char *query) {
  static char buf[128 * 240 + 16];
  static pthread_mutex_t ll = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&ll);
  size_t len = log_tail(query_lang(query), buf, sizeof(buf));
  respond(fd, 200, "text/plain; charset=utf-8", buf, len);
  pthread_mutex_unlock(&ll);
}

static void
handle(int fd) {
  char hdr[HDR_MAX];
  req_t r;
  if(read_request(fd, &r, hdr, sizeof(hdr)) != 0) { respond_text(fd, 400, "bad request"); free(r.body); return; }

  int get = strcmp(r.method, "GET") == 0, post = strcmp(r.method, "POST") == 0;
  if(strcmp(r.path, "/") == 0 || strcmp(r.path, "/index.html") == 0) {
    respond(fd, 200, "text/html; charset=utf-8", web_index_html, web_index_html_len);
  } else if(strcmp(r.path, "/api/status") == 0 && get) {
    h_status(fd, r.query);
  } else if(strcmp(r.path, "/api/live") == 0 && get) {
    h_live(fd, r.query);
  } else if(strcmp(r.path, "/api/sessions") == 0 && get) {
    h_sessions(fd);
  } else if(strcmp(r.path, "/api/session") == 0 && get) {
    h_session_file(fd, r.query);
  } else if(strcmp(r.path, "/api/config") == 0 && get) {
    h_config_get(fd);
  } else if(strcmp(r.path, "/api/config") == 0 && post) {
    h_config_post(fd, &r);
  } else if(strcmp(r.path, "/api/baseline/reset") == 0 && post) {
    sampler_reset_baseline();
    respond_text(fd, 200, "{\"ok\":true}");
  } else if(strcmp(r.path, "/api/notify/test") == 0 && post) {
    plat_notify(LNG("%s\nNotification test: the web UI works.", "%s\nBildirim testi: web arayüzü çalışıyor."), PS5TD_NAME);
    respond_text(fd, 200, "{\"ok\":true}");
  } else if(strcmp(r.path, "/api/log") == 0 && get) {
    h_log(fd, r.query);
  } else if(strncmp(r.path, "/api/", 5) == 0) {
    respond_text(fd, get || post ? 404 : 405, "{\"error\":\"not found\"}");
  } else {
    respond_text(fd, 404, "404");
  }
  free(r.body);
}

/* ------------------------------------------------------------ server */

typedef struct { int fd; } conn_t;

static void *
conn_thread(void *arg) {
  int fd = ((conn_t *)arg)->fd;
  free(arg);
  handle(fd);
  close(fd);
  pthread_mutex_lock(&g_conn_lock);
  g_conns--;
  pthread_mutex_unlock(&g_conn_lock);
  return NULL;
}

int
http_bind(unsigned port) {
  if(!g_start_ms) g_start_ms = util_mono_ms();
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
    int e = errno;
    close(fd);
    return e == EADDRINUSE ? -2 : -1;
  }
  if(listen(fd, 16) != 0) { close(fd); return -1; }
  return fd;
}

void
http_run(int srv) {
  for(;;) {
    int fd = accept(srv, NULL, NULL);
    if(fd < 0) {
      if(errno == EINTR) continue;
      usleep(100 * 1000);
      continue;
    }
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = 15;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    pthread_mutex_lock(&g_conn_lock);
    int busy = g_conns >= MAX_CONN;
    if(!busy) g_conns++;
    pthread_mutex_unlock(&g_conn_lock);
    if(busy) { respond_text(fd, 503, "busy"); close(fd); continue; }

    conn_t *c = malloc(sizeof(*c));
    if(!c) { handle(fd); close(fd); pthread_mutex_lock(&g_conn_lock); g_conns--; pthread_mutex_unlock(&g_conn_lock); continue; }
    c->fd = fd;
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    if(pthread_create(&th, &attr, conn_thread, c) != 0) conn_thread(c);
    pthread_attr_destroy(&attr);
  }
}
