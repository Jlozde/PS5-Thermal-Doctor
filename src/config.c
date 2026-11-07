/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * config.ini read/write. The key table lives in one place; both parsing and
 * the commented output are generated from it. Comments are bilingual.
 */
#include "config.h"
#include "i18n.h"
#include "util.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { CT_UINT, CT_INT, CT_FLOAT, CT_LANG, CT_UNIT } ctype_t;

typedef struct {
  const char *key;
  ctype_t     type;
  size_t      off;
  const char *help_en;
  const char *help_tr;
} citem_t;

#define CI(k, t, f, en, tr) { k, t, offsetof(config_t, f), en, tr }

static const citem_t g_items[] = {
  CI("lang",              CT_LANG,  lang,              "Language: auto (PS5 system language), en, tr, de, fr, es, ru, ko, zh",
                                                       "Dil: auto (PS5 sistem dili), en, tr, de, fr, es, ru, ko, zh"),
  CI("temp_unit",         CT_UNIT,  temp_unit,         "Temperature unit: c (Celsius) or f (Fahrenheit); thresholds below stay in °C",
                                                       "Sıcaklık birimi: c (Celsius) veya f (Fahrenheit); aşağıdaki eşikler °C kalır"),
  CI("http_port",         CT_UINT,  http_port,         "Web UI port", "Web arayüzü portu"),
  CI("home_tile",         CT_INT,   home_tile,         "1 = add a PS5 home-screen tile that opens the web UI, if it is missing",
                                                       "1 = web arayüzünü açan PS5 ana ekran kutucuğunu ekle (yoksa)"),
  CI("sample_ms",         CT_UINT,  sample_ms,         "Sampling interval, ms (1000 recommended)",
                                                       "Örnekleme aralığı, ms (1000 önerilir)"),
  CI("csv_every_s",       CT_UINT,  csv_every_s,       "Write a session CSV line every N seconds",
                                                       "Oturum CSV'sine kaç saniyede bir satır yazılsın"),
  CI("fps_enabled",       CT_INT,   fps_enabled,       "1 = read FPS from /dev/dce", "1 = /dev/dce üzerinden FPS oku"),
  CI("escalate",          CT_INT,   escalate,          "1 = escalate privileges at start (needed for /dev/dce)",
                                                       "1 = başlangıçta yetki yükselt (/dev/dce erişimi için)"),
  CI("notify_startup",    CT_INT,   notify_startup,    "1 = show a notification at start", "1 = açılışta bildirim göster"),
  CI("notify_session",    CT_INT,   notify_session,    "1 = notify a session summary when the game closes",
                                                       "1 = oyun kapanınca oturum özeti bildir"),
  CI("notify_live_warn",  CT_INT,   notify_live_warn,  "1 = warn during play at SERVICE level",
                                                       "1 = oyun sırasında SERVİS seviyesinde uyarı ver"),
  CI("warmup_s",          CT_UINT,  warmup_s,          "Warm-up skipped at session start, s",
                                                       "Oturum başında atlanan ısınma süresi, s"),
  CI("min_ss_s",          CT_UINT,  min_ss_s,          "Minimum steady-state time for a verdict, s",
                                                       "Kararlı durum için gereken en az süre, s"),
  CI("min_session_s",     CT_UINT,  min_session_s,     "Shorter sessions are not recorded, s",
                                                       "Bundan kısa oturumlar kaydedilmez, s"),
  CI("k_fan",             CT_FLOAT, k_fan,             "CDI: °C equivalent per fan percent", "CDI: fan yüzdesi başına °C eşdeğeri"),
  CI("k_pow",             CT_FLOAT, k_pow,             "CDI: °C equivalent per watt", "CDI: watt başına °C eşdeğeri"),
  CI("watch_drift",       CT_FLOAT, watch_drift,       "WATCH threshold vs. baseline, °C", "Referansa göre İZLE eşiği, °C"),
  CI("clean_drift",       CT_FLOAT, clean_drift,       "CLEAN threshold vs. baseline, °C", "Referansa göre TEMİZLİK eşiği, °C"),
  CI("service_drift",     CT_FLOAT, service_drift,     "SERVICE threshold vs. baseline, °C", "Referansa göre SERVİS eşiği, °C"),
  CI("abs_watch_fan",     CT_FLOAT, abs_watch_fan,     "Absolute WATCH: fan % and", "Mutlak İZLE: fan % ve"),
  CI("abs_watch_soc",     CT_FLOAT, abs_watch_soc,     "Absolute WATCH: SoC °C", "Mutlak İZLE: SoC °C"),
  CI("abs_clean_fan",     CT_FLOAT, abs_clean_fan,     "Absolute CLEAN: fan % and", "Mutlak TEMİZLİK: fan % ve"),
  CI("abs_clean_soc",     CT_FLOAT, abs_clean_soc,     "Absolute CLEAN: SoC °C", "Mutlak TEMİZLİK: SoC °C"),
  CI("abs_service_fan",   CT_FLOAT, abs_service_fan,   "Absolute SERVICE: fan % and", "Mutlak SERVİS: fan % ve"),
  CI("abs_service_soc",   CT_FLOAT, abs_service_soc,   "Absolute SERVICE: SoC °C", "Mutlak SERVİS: SoC °C"),
  CI("hot_soc_c",         CT_FLOAT, hot_soc_c,         "SoC temperature that is dangerous by itself, °C",
                                                       "Tek başına tehlikeli SoC sıcaklığı, °C"),
  CI("baseline_sessions", CT_UINT,  baseline_sessions, "Sessions that form the baseline", "Referans için kullanılan oturum sayısı"),
  CI("recent_sessions",   CT_UINT,  recent_sessions,   "Latest sessions averaged for 'now'",
                                                       "'Şimdi' için ortalaması alınan son oturum sayısı"),
  CI("keep_sessions",     CT_UINT,  keep_sessions,     "Session CSV files kept on disk", "Diskte tutulan oturum CSV sayısı"),
};

#define N_ITEMS (sizeof(g_items) / sizeof(g_items[0]))

void
config_defaults(config_t *c) {
  memset(c, 0, sizeof(*c));
  c->lang              = LANG_AUTO;
  c->temp_unit         = TEMP_C;
  c->http_port         = 8088;
  c->sample_ms         = 1000;
  c->csv_every_s       = 5;
  c->fps_enabled       = 1;
  c->escalate          = 1;
  c->notify_startup    = 1;
  c->notify_session    = 1;
  c->notify_live_warn  = 1;
  c->home_tile         = 1;
  c->warmup_s          = 600;
  c->min_ss_s          = 300;
  c->min_session_s     = 180;
  c->k_fan             = 0.30f;
  c->k_pow             = 0.25f;
  c->watch_drift       = 4.0f;
  c->clean_drift       = 8.0f;
  c->service_drift     = 14.0f;
  c->abs_watch_fan     = 60.0f;
  c->abs_watch_soc     = 76.0f;
  c->abs_clean_fan     = 70.0f;
  c->abs_clean_soc     = 80.0f;
  c->abs_service_fan   = 85.0f;
  c->abs_service_soc   = 86.0f;
  c->hot_soc_c         = 95.0f;
  c->baseline_sessions = 5;
  c->recent_sessions   = 3;
  c->keep_sessions     = 60;
}

static void
clamp_u(unsigned *v, unsigned lo, unsigned hi) { if(*v < lo) *v = lo; if(*v > hi) *v = hi; }
static void
clamp_f(float *v, float lo, float hi) { if(*v < lo) *v = lo; if(*v > hi) *v = hi; }

void
config_sanitize(config_t *c) {
  if(c->lang < 0 || c->lang >= LANG_COUNT) c->lang = LANG_AUTO;
  if(c->temp_unit != TEMP_F) c->temp_unit = TEMP_C;
  clamp_u(&c->http_port, 1024, 65535);
  clamp_u(&c->sample_ms, 250, 10000);
  clamp_u(&c->csv_every_s, 1, 600);
  clamp_u(&c->warmup_s, 60, 3600);
  clamp_u(&c->min_ss_s, 60, 3600);
  clamp_u(&c->min_session_s, 30, 3600);
  clamp_f(&c->k_fan, 0.0f, 2.0f);
  clamp_f(&c->k_pow, 0.0f, 2.0f);
  clamp_f(&c->watch_drift, 0.5f, 50.0f);
  clamp_f(&c->clean_drift, c->watch_drift, 60.0f);
  clamp_f(&c->service_drift, c->clean_drift, 80.0f);
  clamp_f(&c->abs_watch_fan, 0.0f, 100.0f);
  clamp_f(&c->abs_clean_fan, 0.0f, 100.0f);
  clamp_f(&c->abs_service_fan, 0.0f, 100.0f);
  clamp_f(&c->abs_watch_soc, 30.0f, 130.0f);
  clamp_f(&c->abs_clean_soc, 30.0f, 130.0f);
  clamp_f(&c->abs_service_soc, 30.0f, 130.0f);
  clamp_f(&c->hot_soc_c, 60.0f, 130.0f);
  clamp_u(&c->baseline_sessions, 1, 50);
  clamp_u(&c->recent_sessions, 1, 20);
  clamp_u(&c->keep_sessions, 5, 500);
  c->fps_enabled      = !!c->fps_enabled;
  c->escalate         = !!c->escalate;
  c->notify_startup   = !!c->notify_startup;
  c->notify_session   = !!c->notify_session;
  c->notify_live_warn = !!c->notify_live_warn;
  c->home_tile        = !!c->home_tile;
}

static int
apply_line(config_t *c, char *line) {
  util_trim(line);
  if(line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[') return 0;
  char *eq = strchr(line, '=');
  if(!eq) return -1;
  *eq = '\0';
  char *key = line, *val = eq + 1;
  util_trim(key);
  util_trim(val);
  for(size_t i = 0; i < N_ITEMS; i++) {
    if(strcmp(g_items[i].key, key) != 0) continue;
    char *end = NULL;
    void *p = (char *)c + g_items[i].off;
    switch(g_items[i].type) {
      case CT_UINT: { long v = strtol(val, &end, 10); if(end == val || v < 0) return -1; *(unsigned *)p = (unsigned)v; break; }
      case CT_INT:  { long v = strtol(val, &end, 10); if(end == val) return -1; *(int *)p = (int)v; break; }
      case CT_FLOAT:{ float v = strtof(val, &end); if(end == val) return -1; *(float *)p = v; break; }
      case CT_LANG: { int v = i18n_parse(val); if(v < LANG_AUTO) return -1; *(int *)p = v; break; }
      case CT_UNIT: { int v = i18n_parse_unit(val); if(v < 0) return -1; *(int *)p = v; break; }
    }
    return 0;
  }
  return -1;   /* unknown key */
}

int
config_apply_text(config_t *c, const char *text) {
  int bad = 0;
  const char *p = text;
  while(p && *p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    char line[256];
    if(len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';
    if(apply_line(c, line) != 0) bad++;
    p = nl ? nl + 1 : NULL;
  }
  config_sanitize(c);
  return bad;
}

int
config_load(config_t *c, const char *path) {
  FILE *f = fopen(path, "r");
  if(!f) return -1;
  char line[256];
  while(fgets(line, sizeof(line), f)) apply_line(c, line);
  fclose(f);
  config_sanitize(c);
  return 0;
}

size_t
config_to_text(const config_t *c, char *out, size_t n) {
  size_t used = 0;
  int w = snprintf(out, n,
                   "# %s %s settings / ayarları\n"
                   "# Changes saved from the web UI apply immediately.\n"
                   "# Web arayüzünden kaydedilen değişiklikler anında uygulanır.\n\n",
                   PS5TD_NAME, PS5TD_VERSION);
  if(w > 0) used = (size_t)w < n ? (size_t)w : n - 1;
  for(size_t i = 0; i < N_ITEMS && used < n; i++) {
    const void *p = (const char *)c + g_items[i].off;
    char val[32];
    switch(g_items[i].type) {
      case CT_UINT:  snprintf(val, sizeof(val), "%u", *(const unsigned *)p); break;
      case CT_INT:   snprintf(val, sizeof(val), "%d", *(const int *)p); break;
      case CT_FLOAT: snprintf(val, sizeof(val), "%.2f", (double)*(const float *)p); break;
      case CT_LANG:  snprintf(val, sizeof(val), "%s", i18n_code(*(const int *)p)); break;
      case CT_UNIT:  snprintf(val, sizeof(val), "%s", i18n_unit_code(*(const int *)p)); break;
    }
    w = snprintf(out + used, n - used, "# %s\n# %s\n%s = %s\n\n",
                 g_items[i].help_en, g_items[i].help_tr, g_items[i].key, val);
    if(w < 0) break;
    used += (size_t)w < n - used ? (size_t)w : n - used - 1;
  }
  return used;
}

int
config_save(const config_t *c, const char *path) {
  char buf[8192];
  size_t len = config_to_text(c, buf, sizeof(buf));
  char tmp[300];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if(!f) return -1;
  int ok = fwrite(buf, 1, len, f) == len;
  ok = (fclose(f) == 0) && ok;
  if(!ok) { remove(tmp); return -1; }
  remove(path);
  return rename(tmp, path) == 0 ? 0 : -1;
}
