/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sampler: measures once per second, manages sessions, updates the diagnosis.
 *
 *  - Memory ring: the last 2 hours (for the web charts)
 *  - Session: while a game is in front; when it closes, summary -> history.csv + notification
 *  - Session CSV: one line every csv_every_s, under /data/ps5td/sessions/
 */
#include "sampler.h"
#include "config.h"
#include "history.h"
#include "i18n.h"
#include "texts.h"
#include "log.h"
#include "platform.h"
#include "probe.h"
#include "util.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GAME_GONE_S     15     /* the session ends when the game is gone this long */
#define LIVE_EVERY_S    30

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static sample_t   g_ring[PS5TD_RING_SECONDS];
static unsigned   g_ring_head, g_ring_count;

static config_t   g_cfg;
static session_summary_t g_hist[PS5TD_HISTORY_MAX];
static unsigned   g_hist_n;
static uint32_t   g_epoch;
static uint64_t   g_epoch_since;

static status_t   g_status;

/* session state (touched only by the sampler thread) */
static int            g_in_session;
static session_acc_t  g_acc;
static uint32_t       g_gone_s;
static FILE          *g_csv;
static uint32_t       g_csv_last_s;
static uint32_t       g_live_last_s;
static int            g_live_warned;

/* ------------------------------------------------------------ helpers */

static void
prune_sessions(unsigned keep) {
  DIR *d = opendir(PS5TD_SESSION_DIR);
  if(!d) return;
  char names[512][64];
  unsigned n = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL && n < 512) {
    if(e->d_name[0] == '.') continue;
    snprintf(names[n++], 64, "%s", e->d_name);
  }
  closedir(d);
  if(n <= keep) return;
  /* names start with a timestamp: lexical order = time order */
  for(unsigned i = 0; i + 1 < n; i++)
    for(unsigned j = i + 1; j < n; j++)
      if(strcmp(names[i], names[j]) > 0) { char t[64]; memcpy(t, names[i], 64); memcpy(names[i], names[j], 64); memcpy(names[j], t, 64); }
  for(unsigned i = 0; i < n - keep; i++) {
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", PS5TD_SESSION_DIR, names[i]);
    unlink(path);
  }
}

static void
csv_open(uint64_t wall_ms, const char *title) {
  char stamp[32], path[320];
  util_fmt_utc(wall_ms, stamp, sizeof(stamp), 1);
  snprintf(path, sizeof(path), "%s/%s_%s.csv", PS5TD_SESSION_DIR, stamp, util_safe_name(title) ? title : "game");
  g_csv = fopen(path, "w");
  if(!g_csv) { LOG("Cannot open the session CSV: %s", "Oturum CSV'si açılamadı: %s", path); return; }
  fprintf(g_csv, "t_s,wall_ms,cpu_c,soc_c,gpu_c,fan_pct10,power_dw,gpu_w10,cpu_w10,mem_w10,fps10,ch0,ch1,ch2,ch3,ch4,ch5,ch6,ch7\n");
  g_csv_last_s = 0;
}

static void
csv_write(const sample_t *s, uint32_t t_s) {
  if(!g_csv) return;
  fprintf(g_csv, "%u,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
          t_s, (unsigned long long)s->wall_ms, s->cpu_c, s->soc_c, s->gpu_c, s->fan_pct10,
          s->power_dw, s->gpu_w10, s->cpu_w10, s->mem_w10, s->fps10,
          s->soc_raw[0], s->soc_raw[1], s->soc_raw[2], s->soc_raw[3],
          s->soc_raw[4], s->soc_raw[5], s->soc_raw[6], s->soc_raw[7]);
}

static void
refresh_overall_locked(void) {
  an_overall(g_hist, g_hist_n, g_epoch, &g_cfg, &g_status.overall);
  g_status.history_n = g_hist_n;
  g_status.epoch = g_epoch;
  g_status.epoch_since_ms = g_epoch_since;
}

/* ------------------------------------------------------------ sessions */

static void
session_begin(const sample_t *s, const char *title) {
  an_begin(&g_acc, s->wall_ms, s->mono_s, title, g_epoch);
  g_in_session = 1;
  g_gone_s = 0;
  g_live_last_s = s->mono_s;
  g_live_warned = 0;
  csv_open(s->wall_ms, title);
  LOG("Session started: %s", "Oturum başladı: %s", title);
}

static void
session_end(const char *end_en, const char *end_tr) {
  session_summary_t sum;
  an_finish(&g_acc, &g_cfg, &sum);
  if(g_csv) { fclose(g_csv); g_csv = NULL; }
  g_in_session = 0;

  if(sum.dur_s < g_cfg.min_session_s) {
    LOG("Session ended (%s%.0s), %u s: too short, not recorded.",
        "Oturum bitti (%.0s%s), %u sn: çok kısa, kaydedilmedi.", end_en, end_tr, sum.dur_s);
    pthread_mutex_lock(&g_lock);
    g_status.session_active = 0;
    g_status.live_valid = 0;
    pthread_mutex_unlock(&g_lock);
    return;
  }

  char why[LANG_COUNT][AN_REASON_LEN];
  for(int l = 0; l < LANG_COUNT; l++) sum.verdict = an_verdict_abs(&sum, &g_cfg, l, why[l], AN_REASON_LEN);

  pthread_mutex_lock(&g_lock);
  if(g_hist_n < PS5TD_HISTORY_MAX) g_hist[g_hist_n++] = sum;
  else { memmove(g_hist, g_hist + 1, sizeof(g_hist[0]) * (PS5TD_HISTORY_MAX - 1)); g_hist[PS5TD_HISTORY_MAX - 1] = sum; }
  history_append(PS5TD_HISTORY_PATH, &sum);
  refresh_overall_locked();
  overall_t ov = g_status.overall;
  g_status.session_active = 0;
  g_status.live_valid = 0;
  pthread_mutex_unlock(&g_lock);

  prune_sessions(g_cfg.keep_sessions);

  LOG("Session ended (%s%.0s): %s, %u min, SoC %.0f°C, fan %.0f%%, power %.0f W, FPS %.0f -> %s%.0s. %s%.0s",
      "Oturum bitti (%.0s%s): %s, %u dk, SoC %.0f°C, fan %%%.0f, güç %.0f W, FPS %.0f -> %.0s%s. %.0s%s",
      end_en, end_tr, sum.title, sum.dur_s / 60, (double)sum.ss_soc, (double)sum.ss_fan,
      (double)sum.ss_power, (double)sum.ss_fps,
      an_verdict_badge(sum.verdict, LANG_EN), an_verdict_badge(sum.verdict, LANG_TR),
      why[LANG_EN], why[LANG_TR]);

  if(g_cfg.notify_session) {
    if(sum.ss_valid) {
      int final_v = ov.verdict != VERDICT_NA ? ov.verdict : sum.verdict;
      char pw[24] = "";
      if(sum.power_valid) snprintf(pw, sizeof(pw), " · %.0f W", (double)sum.ss_power);
      plat_notify(TX_NOTIFY_SUMMARY(i18n_get()),
                  PS5TD_NAME, sum.title, sum.dur_s / 60, i18n_temp(sum.ss_soc, g_cfg.temp_unit),
                  i18n_unit_sym(g_cfg.temp_unit), (double)sum.ss_fan, pw, an_verdict_label(final_v, i18n_get()));
    } else {
      plat_notify(TX_NOTIFY_SHORT(i18n_get()),
                  PS5TD_NAME, sum.title, sum.dur_s / 60, i18n_temp(sum.max_soc, g_cfg.temp_unit),
                  i18n_unit_sym(g_cfg.temp_unit));
    }
  }
}

static void
session_live_update(const sample_t *s) {
  if(s->mono_s - g_live_last_s < LIVE_EVERY_S) return;
  g_live_last_s = s->mono_s;
  session_summary_t sum;
  an_finish(&g_acc, &g_cfg, &sum);
  char why[LANG_COUNT][AN_REASON_LEN];
  int v = VERDICT_NA;
  for(int l = 0; l < LANG_COUNT; l++) v = an_verdict_abs(&sum, &g_cfg, l, why[l], AN_REASON_LEN);

  pthread_mutex_lock(&g_lock);
  g_status.live = sum;
  g_status.live_valid = 1;
  g_status.live_verdict = v;
  memcpy(g_status.live_reason, why, sizeof(why));
  pthread_mutex_unlock(&g_lock);

  if(v == VERDICT_SERVICE && g_cfg.notify_live_warn && !g_live_warned) {
    g_live_warned = 1;
    plat_notify(TX_NOTIFY_WARN(i18n_get()),
                PS5TD_NAME, i18n_temp(sum.ss_soc, g_cfg.temp_unit), i18n_unit_sym(g_cfg.temp_unit),
                (double)sum.ss_fan);
  }
}

/* ------------------------------------------------------------ main loop */

static void *
sampler_thread(void *arg) {
  (void)arg;
  LOG("Sampler started (%u ms).", "Örnekleyici başladı (%u ms).", g_cfg.sample_ms);
  for(;;) {
    config_t cfg;
    pthread_mutex_lock(&g_lock);
    cfg = g_cfg;
    pthread_mutex_unlock(&g_lock);

    char title[PS5TD_TITLE_LEN];
    int game = probe_get(title, sizeof(title));

    sample_t s;
    plat_read(&s, game, cfg.fps_enabled);

    /* ring */
    pthread_mutex_lock(&g_lock);
    g_ring[g_ring_head] = s;
    g_ring_head = (g_ring_head + 1) % PS5TD_RING_SECONDS;
    if(g_ring_count < PS5TD_RING_SECONDS) g_ring_count++;
    g_status.now = s;
    g_status.samples_total++;
    pthread_mutex_unlock(&g_lock);

    /* session management */
    if(game) {
      if(g_in_session && strcmp(g_acc.s.title, title) != 0 && strcmp(title, "-") != 0)
        session_end("game changed", "oyun değişti");
      if(!g_in_session) session_begin(&s, title);
      g_gone_s = 0;
    } else if(g_in_session) {
      g_gone_s += cfg.sample_ms / 1000 ? cfg.sample_ms / 1000 : 1;
      if(g_gone_s >= GAME_GONE_S) session_end("game closed", "oyun kapandı");
    }

    if(g_in_session) {
      an_add(&g_acc, &s, &cfg);
      uint32_t t_s = s.mono_s - g_acc.first_mono_s;
      if(t_s == 0 || t_s - g_csv_last_s >= cfg.csv_every_s) { csv_write(&s, t_s); g_csv_last_s = t_s; if(g_csv) fflush(g_csv); }
      pthread_mutex_lock(&g_lock);
      g_status.session_active = 1;
      snprintf(g_status.title, sizeof(g_status.title), "%s", g_acc.s.title);
      g_status.session_s = t_s;
      pthread_mutex_unlock(&g_lock);
      session_live_update(&s);
    }

    usleep(cfg.sample_ms * 1000u);
  }
  return NULL;
}

/* ------------------------------------------------------------ public interface */

int
sampler_start(void) {
  pthread_mutex_lock(&g_lock);
  config_defaults(&g_cfg);
  int have_cfg = config_load(&g_cfg, PS5TD_CONFIG_PATH) == 0;
  if(!have_cfg) config_save(&g_cfg, PS5TD_CONFIG_PATH);
  state_load(PS5TD_STATE_PATH, &g_epoch, &g_epoch_since);
  g_hist_n = history_load(PS5TD_HISTORY_PATH, g_hist, PS5TD_HISTORY_MAX);
  memset(&g_status, 0, sizeof(g_status));
  snprintf(g_status.title, sizeof(g_status.title), "-");
  refresh_overall_locked();
  unsigned hist_n = g_hist_n, epoch = g_epoch;
  pthread_mutex_unlock(&g_lock);
  if(!have_cfg) LOG("No config.ini, writing defaults.", "config.ini yok, varsayılanlar yazılıyor.");
  LOG("History: %u sessions, baseline counter %u.", "Geçmiş: %u oturum, referans sayacı %u.", hist_n, epoch);

  pthread_t th;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 256 * 1024);   /* texts for every language live on the stack */
  int rc = pthread_create(&th, &attr, sampler_thread, NULL);
  pthread_attr_destroy(&attr);
  if(rc != 0) LOG("Cannot start the sampler thread (%d).", "Örnekleyici iş parçacığı başlatılamadı (%d).", rc);
  return rc;
}

void
sampler_get_status(status_t *out) {
  pthread_mutex_lock(&g_lock);
  *out = g_status;
  pthread_mutex_unlock(&g_lock);
}

void
sampler_get_config(config_t *out) {
  pthread_mutex_lock(&g_lock);
  *out = g_cfg;
  pthread_mutex_unlock(&g_lock);
}

int
sampler_set_config_text(const char *ini_text) {
  config_t c;
  pthread_mutex_lock(&g_lock);
  c = g_cfg;
  pthread_mutex_unlock(&g_lock);
  int bad = config_apply_text(&c, ini_text);
  pthread_mutex_lock(&g_lock);
  g_cfg = c;
  config_save(&g_cfg, PS5TD_CONFIG_PATH);
  refresh_overall_locked();
  pthread_mutex_unlock(&g_lock);
  plat_apply_lang(c.lang);
  LOG("Settings updated (%d bad lines).", "Ayarlar güncellendi (%d hatalı satır).", bad);
  return bad;
}

unsigned
sampler_get_history(session_summary_t *out, unsigned max) {
  pthread_mutex_lock(&g_lock);
  unsigned n = g_hist_n < max ? g_hist_n : max;
  memcpy(out, g_hist + (g_hist_n - n), sizeof(*out) * n);
  pthread_mutex_unlock(&g_lock);
  return n;
}

unsigned
sampler_get_recent(sample_t *out, unsigned max, unsigned step) {
  if(step == 0) step = 1;
  pthread_mutex_lock(&g_lock);
  unsigned avail = g_ring_count;
  unsigned want = max * step;
  if(want > avail) want = avail;
  unsigned n = 0;
  /* start at the oldest sample, take one every 'step' */
  unsigned start = (g_ring_head + PS5TD_RING_SECONDS - want) % PS5TD_RING_SECONDS;
  for(unsigned i = 0; i < want && n < max; i += step)
    out[n++] = g_ring[(start + i) % PS5TD_RING_SECONDS];
  pthread_mutex_unlock(&g_lock);
  return n;
}

void
sampler_reset_baseline(void) {
  pthread_mutex_lock(&g_lock);
  g_epoch++;
  g_epoch_since = util_wall_ms();
  state_save(PS5TD_STATE_PATH, g_epoch, g_epoch_since);
  refresh_overall_locked();
  uint32_t epoch = g_epoch;
  pthread_mutex_unlock(&g_lock);
  LOG("New baseline started (counter %u). Older sessions are excluded from the comparison.",
      "Yeni referans başlatıldı (sayaç %u). Eski oturumlar karşılaştırma dışı.", epoch);
}
