/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PC tests of the diagnosis logic. Generates synthetic sessions and checks
 * the verdicts, the bilingual texts and the persistence formats.
 *   gcc -std=gnu11 -pthread -Isrc src/analysis.c src/history.c src/config.c src/util.c \
 *       src/i18n.c src/log.c test/test_analysis.c -lm
 */
#include "analysis.h"
#include "config.h"
#include "history.h"
#include "i18n.h"
#include "log.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail, g_pass;

#define CHECK(cond, ...) do { if(cond) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

/* Simple deterministic "noise" */
static int
jit(uint32_t i, int amp) {
  return amp ? (int)((i * 2654435761u >> 24) % (unsigned)(2 * amp + 1)) - amp : 0;
}

/* A session of 'secs' seconds: soc/fan approach their targets along a warm-up curve. */
static session_summary_t
run_session(const config_t *c, uint64_t wall0, unsigned secs, int soc_ss, int fan_ss, int power_w, int fps,
            uint32_t epoch, const char *title) {
  session_acc_t a;
  an_begin(&a, wall0, 1000, title, epoch);
  for(unsigned t = 0; t < secs; t++) {
    double k = 1.0 - exp(-(double)t / 240.0);
    sample_t s;
    memset(&s, 0, sizeof(s));
    s.wall_ms = wall0 + (uint64_t)t * 1000ULL;
    s.mono_s = 1000 + t;
    s.soc_c = (int16_t)(45 + (soc_ss - 45) * k + jit(t, 1));
    s.cpu_c = (int16_t)(s.soc_c + 6);
    s.gpu_c = (int16_t)(s.soc_c - 2);
    int fan = (int)(22 + (fan_ss - 22) * k + jit(t + 7, 2));
    if(fan < 0) fan = 0;
    s.fan_pct10 = (int16_t)(fan * 10);
    s.fan_raw = (int16_t)(fan * 1024 / 100);
    s.power_dw = power_w > 0 ? (int16_t)(power_w * 10 + jit(t + 3, 30)) : -1;
    s.fps10 = fps > 0 ? (int16_t)(fps * 10 - jit(t, 5)) : -1;
    s.game = 1;
    an_add(&a, &s, c);
  }
  session_summary_t sum;
  an_finish(&a, c, &sum);
  char why[AN_REASON_LEN];
  sum.verdict = an_verdict_abs(&sum, c, LANG_EN, why, sizeof(why));
  return sum;
}

static void
test_absolute(const config_t *c) {
  printf("[1] Absolute rules\n");
  char why[AN_REASON_LEN];

  session_summary_t h = run_session(c, 1700000000000ULL, 1800, 72, 38, 170, 60, 0, "PPSA00001");
  CHECK(h.ss_valid, "healthy session must be steady (n_ss=%u)", h.ss_samples);
  CHECK(fabs(h.ss_soc - 72) < 2.0, "ss_soc ~ 72 (%.1f)", (double)h.ss_soc);
  CHECK(fabs(h.ss_fan - 38) < 3.0, "ss_fan ~ 38 (%.1f)", (double)h.ss_fan);
  CHECK(h.power_valid && fabs(h.ss_power - 170) < 3.0, "power ~ 170 (%.1f)", (double)h.ss_power);
  CHECK(h.fps_valid && h.ss_fps > 55 && h.ss_fps <= 60.5, "fps ~ 60 (%.1f)", (double)h.ss_fps);
  CHECK(h.verdict == VERDICT_OK, "healthy -> OK (%s)", an_verdict_name(h.verdict));
  CHECK(h.warm_rate > 2.0f && h.warm_rate < 8.0f, "plausible warm-up rate (%.2f °C/min)", (double)h.warm_rate);
  printf("    healthy: SoC %.1f fan %.1f power %.1f cdi %.1f -> %s\n", (double)h.ss_soc, (double)h.ss_fan, (double)h.ss_power, (double)h.cdi, an_verdict_name(h.verdict));

  session_summary_t d = run_session(c, 1700000000000ULL, 1800, 88, 92, 170, 60, 0, "PPSA00001");
  CHECK(d.verdict == VERDICT_SERVICE, "dusty (88°C, 92%%) -> SERVICE (%s)", an_verdict_name(d.verdict));
  an_verdict_abs(&d, c, LANG_EN, why, sizeof(why));
  printf("    dusty: %s -> %s\n", why, an_verdict_name(d.verdict));

  session_summary_t m = run_session(c, 1700000000000ULL, 1800, 82, 74, 170, 60, 0, "PPSA00001");
  CHECK(m.verdict == VERDICT_CLEAN, "medium (82°C, 74%%) -> CLEAN (%s)", an_verdict_name(m.verdict));

  session_summary_t w = run_session(c, 1700000000000ULL, 1800, 78, 64, 170, 60, 0, "PPSA00001");
  CHECK(w.verdict == VERDICT_WATCH, "slight (78°C, 64%%) -> WATCH (%s)", an_verdict_name(w.verdict));

  session_summary_t hot_low_fan = run_session(c, 1700000000000ULL, 1800, 80, 35, 170, 60, 0, "PPSA00001");
  CHECK(hot_low_fan.verdict == VERDICT_OK, "hot but low fan (firmware has not ramped up) -> OK (%s)", an_verdict_name(hot_low_fan.verdict));

  session_summary_t nofan = run_session(c, 1700000000000ULL, 1800, 78, 0, 170, 60, 0, "PPSA00001");
  CHECK(nofan.verdict == VERDICT_SERVICE, "fan 0 + hot -> SERVICE (%s)", an_verdict_name(nofan.verdict));

  session_summary_t sh = run_session(c, 1700000000000ULL, 300, 72, 38, 170, 60, 0, "PPSA00001");
  CHECK(!sh.ss_valid && sh.verdict == VERDICT_NA, "5 min session -> no data (%s)", an_verdict_name(sh.verdict));
  CHECK(sh.max_soc > 60, "a short session still records the peak (%.0f)", (double)sh.max_soc);

  session_summary_t np = run_session(c, 1700000000000ULL, 1800, 72, 38, 0, 0, 0, "PPSA00001");
  CHECK(np.ss_valid && !np.power_valid && !np.fps_valid && np.verdict == VERDICT_OK, "a verdict is given without power/FPS too");
  CHECK(fabs(np.cdi - (72 + 0.3 * (38 - 40))) < 2.5, "CDI without power = T + k_fan*(fan-40) (%.1f)", (double)np.cdi);
}

static void
test_relative(const config_t *c) {
  printf("[2] Comparison with the baseline\n");
  session_summary_t hist[32];
  unsigned n = 0;
  uint64_t t0 = 1700000000000ULL;
  for(unsigned i = 0; i < 5; i++) hist[n++] = run_session(c, t0 + i * 2ULL * 86400000ULL, 2400, 70, 38, 170, 60, 0, "PPSA00001");
  overall_t o;
  an_overall(hist, n, 0, c, &o);
  CHECK(o.baseline.valid && o.baseline.n == 5, "the baseline forms with 5 sessions (n=%u valid=%d)", o.baseline.n, o.baseline.valid);
  CHECK(o.verdict == VERDICT_OK, "baseline sessions -> OK (%s)", an_verdict_name(o.verdict));
  CHECK(fabs(o.drift) < 1.5, "drift ~ 0 inside the baseline (%.2f)", (double)o.drift);

  /* Hotter at the same power with a faster fan: dust has built up */
  for(unsigned i = 0; i < 3; i++) hist[n++] = run_session(c, t0 + (10 + i) * 2ULL * 86400000ULL, 2400, 76, 52, 170, 60, 0, "PPSA00002");
  an_overall(hist, n, 0, c, &o);
  CHECK(o.drift > 8.0f && o.drift < 14.0f, "drift ~ 10 (%.2f)", (double)o.drift);
  CHECK(o.verdict_abs == VERDICT_OK, "absolute rule alone is OK (%s)", an_verdict_name(o.verdict_abs));
  CHECK(o.verdict_rel == VERDICT_CLEAN, "relative -> CLEAN (%s)", an_verdict_name(o.verdict_rel));
  CHECK(o.verdict == VERDICT_CLEAN, "final -> CLEAN (%s)", an_verdict_name(o.verdict));
  printf("    %s\n    %s\n", o.reason_abs[LANG_EN], o.reason_rel[LANG_EN]);
  printf("    %s\n    %s\n", o.reason_abs[LANG_TR], o.reason_rel[LANG_TR]);

  /* Heavy game (more watts) on the same console: power normalisation keeps the drift small */
  session_summary_t heavy = run_session(c, t0 + 40ULL * 86400000ULL, 2400, 75, 42, 190, 60, 0, "PPSA00003");
  CHECK(fabs(heavy.cdi - hist[0].cdi) < 4.0, "heavy game CDI close to baseline (%.1f vs %.1f)", (double)heavy.cdi, (double)hist[0].cdi);

  /* Cleaning -> new counter: older sessions are left out */
  an_overall(hist, n, 1, c, &o);
  CHECK(!o.baseline.valid && o.baseline.n == 0 && o.verdict == VERDICT_NA, "history resets with a new counter");
}

static void
test_trend(const config_t *c) {
  printf("[3] Trend\n");
  session_summary_t hist[32];
  unsigned n = 0;
  uint64_t t0 = 1700000000000ULL;
  for(unsigned i = 0; i < 10; i++)
    hist[n++] = run_session(c, t0 + i * 6ULL * 86400000ULL, 2400, 70 + (int)(i / 2), 38 + (int)i, 170, 60, 0, "PPSA00001");
  float per30 = 0; unsigned used = 0;
  int rc = an_trend(hist, n, 0, &per30, &used);
  CHECK(rc == 0 && used == 10, "10 sessions / 54 days -> trend computed (rc=%d used=%u)", rc, used);
  CHECK(per30 > 2.0f, "trend is positive and clear (%.2f °C/30d)", (double)per30);
  overall_t o;
  an_overall(hist, n, 0, c, &o);
  CHECK(o.verdict >= VERDICT_WATCH, "a worsening trend gives at least WATCH (%s)", an_verdict_name(o.verdict));
  CHECK(strstr(o.trend_text[LANG_EN], "getting worse") != NULL, "EN trend text says it is getting worse (%s)", o.trend_text[LANG_EN]);
  CHECK(strstr(o.trend_text[LANG_TR], "kötüleşiyor") != NULL, "TR trend text says kötüleşiyor (%s)", o.trend_text[LANG_TR]);
  printf("    %s\n    %s\n", o.trend_text[LANG_EN], o.trend_text[LANG_TR]);
}

static void
test_i18n(const config_t *c) {
  printf("[4] Languages\n");
  CHECK(i18n_parse("en") == LANG_EN && i18n_parse("TR") == LANG_TR && i18n_parse("auto") == LANG_AUTO, "language codes parse");
  CHECK(i18n_parse("xx") == -2 && i18n_parse(NULL) == -2, "unknown language codes are rejected");
  CHECK(strcmp(i18n_code(LANG_TR), "tr") == 0 && strcmp(i18n_code(LANG_AUTO), "auto") == 0, "language codes print");
  int round_trip = 1;
  for(int l = 0; l < LANG_COUNT; l++) round_trip &= i18n_parse(i18n_code(l)) == l;
  CHECK(round_trip && i18n_parse("Deutsch") == LANG_DE && i18n_parse("zh-CN") == LANG_ZH, "all %d language codes round-trip", LANG_COUNT);
  CHECK(i18n_from_sce(19) == LANG_TR && i18n_from_sce(4) == LANG_DE && i18n_from_sce(2) == LANG_FR &&
        i18n_from_sce(22) == LANG_FR && i18n_from_sce(3) == LANG_ES && i18n_from_sce(20) == LANG_ES &&
        i18n_from_sce(8) == LANG_RU && i18n_from_sce(9) == LANG_KO && i18n_from_sce(11) == LANG_ZH &&
        i18n_from_sce(1) == LANG_EN && i18n_from_sce(0) == LANG_EN && i18n_from_sce(-1) == LANG_EN,
        "PS5 system language codes map to app languages");

  for(int v = VERDICT_NA; v <= VERDICT_SERVICE; v++) {
    int distinct = 1;
    for(int l = 0; l < LANG_COUNT; l++) {
      const char *lb = an_verdict_label(v, l), *ad = an_verdict_advice(v, l), *bd = an_verdict_badge(v, l);
      distinct &= lb[0] && ad[0] && bd[0];
      if(l != LANG_EN) distinct &= strcmp(lb, an_verdict_label(v, LANG_EN)) != 0 && strcmp(ad, an_verdict_advice(v, LANG_EN)) != 0;
    }
    CHECK(distinct, "verdict %d has its own label and advice in all %d languages", v, LANG_COUNT);
  }
  CHECK(strcmp(an_verdict_badge(VERDICT_CLEAN, LANG_EN), "CLEAN") == 0 &&
        strcmp(an_verdict_badge(VERDICT_CLEAN, LANG_TR), "TEMİZLİK") == 0 &&
        strcmp(an_verdict_badge(VERDICT_CLEAN, LANG_DE), "REINIGEN") == 0, "badges per language");

  /* Percent placement: English "38%", Turkish "%38" */
  session_summary_t h = run_session(c, 1700000000000ULL, 1800, 72, 38, 170, 60, 0, "PPSA00001");
  char en[AN_REASON_LEN], tr[AN_REASON_LEN];
  an_verdict_abs(&h, c, LANG_EN, en, sizeof(en));
  an_verdict_abs(&h, c, LANG_TR, tr, sizeof(tr));
  CHECK(strstr(en, "%,") != NULL && strstr(en, "normal range") != NULL, "EN reason: number then %% (%s)", en);
  CHECK(strstr(tr, "fan %") != NULL && strstr(tr, "normal aralıkta") != NULL, "TR reason: %% then number (%s)", tr);
  char de[AN_REASON_LEN], zh[AN_REASON_LEN];
  an_verdict_abs(&h, c, LANG_DE, de, sizeof(de));
  an_verdict_abs(&h, c, LANG_ZH, zh, sizeof(zh));
  CHECK(strstr(de, " %,") != NULL && strstr(de, "Normalbereich") != NULL, "DE reason: number, space, %% (%s)", de);
  CHECK(strstr(zh, "正常范围") != NULL, "ZH reason (%s)", zh);

  /* The longest reasons (two at once) fit in every language */
  session_summary_t worst = run_session(c, 1700000000000ULL, 1800, 98, 92, 200, 60, 0, "PPSA00003");
  int fits = 1, two = 1;
  for(int l = 0; l < LANG_COUNT; l++) {
    char w[AN_REASON_LEN];
    int vw = an_verdict_abs(&worst, c, l, w, sizeof(w));
    fits &= vw == VERDICT_SERVICE && strlen(w) + 1 < sizeof(w);
    two &= strstr(w, "%") != NULL;   /* the fan reason with its percent is there after the danger reason */
    if(l == LANG_RU || l == LANG_KO) printf("    %s\n", w);
  }
  CHECK(fits && two, "two reasons fit in AN_REASON_LEN in all languages");

  /* Temperature unit: values stay in °C, texts follow temp_unit */
  CHECK(i18n_parse_unit("c") == TEMP_C && i18n_parse_unit("F") == TEMP_F && i18n_parse_unit("Fahrenheit") == TEMP_F &&
        i18n_parse_unit("k") == -1 && i18n_parse_unit(NULL) == -1, "temperature unit codes parse");
  CHECK(fabs(i18n_temp(100.0, TEMP_F) - 212.0) < 1e-9 && fabs(i18n_temp(0.0, TEMP_F) - 32.0) < 1e-9 &&
        fabs(i18n_temp(72.0, TEMP_C) - 72.0) < 1e-9, "absolute conversion C -> F");
  CHECK(fabs(i18n_tdelta(10.0, TEMP_F) - 18.0) < 1e-9 && fabs(i18n_tdelta(10.0, TEMP_C) - 10.0) < 1e-9,
        "difference conversion C -> F (no +32)");
  CHECK(strcmp(i18n_unit_sym(TEMP_F), "°F") == 0 && strcmp(i18n_unit_code(TEMP_F), "f") == 0 &&
        strcmp(i18n_unit_sym(TEMP_C), "°C") == 0, "unit symbols and codes");
  config_t cf = *c;
  cf.temp_unit = TEMP_F;
  char fen[AN_REASON_LEN], ftr[AN_REASON_LEN];
  int vf = an_verdict_abs(&h, &cf, LANG_EN, fen, sizeof(fen));
  an_verdict_abs(&h, &cf, LANG_TR, ftr, sizeof(ftr));
  CHECK(vf == VERDICT_OK && strstr(fen, "°F") != NULL && strstr(fen, "°C") == NULL && strstr(ftr, "°F") != NULL,
        "reason texts use °F when asked (%s)", fen);
  char want[32];
  snprintf(want, sizeof(want), "SoC %.0f°F", i18n_temp(h.ss_soc, TEMP_F));
  CHECK(strstr(fen, want) != NULL, "SoC %.1f °C is shown as \"%s\" (%s)", (double)h.ss_soc, want, fen);
  session_summary_t hot = run_session(&cf, 1700000000000ULL, 1800, 88, 92, 170, 60, 0, "PPSA00002");
  CHECK(hot.verdict == VERDICT_SERVICE, "the verdict does not depend on the display unit (%s)", an_verdict_name(hot.verdict));

  /* The active console language switches LNG() */
  i18n_set(LANG_TR);
  CHECK(strcmp(LNG("yes", "evet"), "evet") == 0, "LNG follows the console language (tr)");
  i18n_set(LANG_EN);
  CHECK(strcmp(LNG("yes", "evet"), "yes") == 0, "LNG follows the console language (en)");

  /* Log lines are kept in both languages */
  log_init(NULL);
  LOG("test line %d", "deneme satırı %d", 7);
  char buf[2048];
  log_tail(LANG_EN, buf, sizeof(buf));
  CHECK(strstr(buf, "test line 7") != NULL, "log tail in English");
  log_tail(LANG_TR, buf, sizeof(buf));
  CHECK(strstr(buf, "deneme satırı 7") != NULL, "log tail in Turkish");
  log_tail(LANG_KO, buf, sizeof(buf));
  CHECK(strstr(buf, "test line 7") != NULL, "the log falls back to English for other languages");
  i18n_set(LANG_RU);
  CHECK(i18n_get() == LANG_RU && strcmp(LNG("yes", "evet"), "yes") == 0, "technical texts fall back to English (ru)");
  i18n_set(LANG_COUNT + 3);
  CHECK(i18n_get() == LANG_EN, "an invalid language becomes English");
  i18n_set(LANG_EN);
}

static void
test_persistence(const config_t *c) {
  printf("[5] Persistence (history.csv, config.ini, state.ini)\n");
  session_summary_t s = run_session(c, 1700000000000ULL, 1800, 72, 38, 170, 60, 3, "PPSA00009");
  const char *path = "build/test_history.csv";
  remove(path);
  CHECK(history_append(path, &s) == 0, "history_append");
  CHECK(history_append(path, &s) == 0, "history_append 2");
  session_summary_t back[4];
  unsigned n = history_load(path, back, 4);
  CHECK(n == 2, "2 lines read back (%u)", n);
  CHECK(back[0].start_ms == s.start_ms && back[0].epoch == 3 && strcmp(back[0].title, "PPSA00009") == 0, "fields kept");
  CHECK(fabs(back[0].cdi - s.cdi) < 0.01 && back[0].verdict == s.verdict && back[0].ss_valid == s.ss_valid, "cdi/verdict kept");
  CHECK(fabs(back[0].ss_power - s.ss_power) < 0.1 && back[0].power_valid == 1, "power kept");

  config_t a, b;
  config_defaults(&a);
  CHECK(a.lang == LANG_AUTO, "default language is auto");
  CHECK(a.temp_unit == TEMP_C, "default temperature unit is Celsius");
  a.http_port = 9090; a.k_fan = 0.45f; a.baseline_sessions = 7; a.lang = LANG_TR; a.temp_unit = TEMP_F;
  char txt[8192];
  size_t len = config_to_text(&a, txt, sizeof(txt));
  CHECK(len < sizeof(txt) - 1, "config text fits (%u bytes)", (unsigned)len);
  CHECK(strstr(txt, "lang = tr") != NULL && strstr(txt, "Dil:") != NULL && strstr(txt, "Language:") != NULL,
        "config text is bilingual and has the language");
  config_defaults(&b);
  int bad = config_apply_text(&b, txt);
  CHECK(bad == 0, "the text applies without errors (%d)", bad);
  CHECK(b.http_port == 9090 && fabs(b.k_fan - 0.45f) < 0.001 && b.baseline_sessions == 7 && b.lang == LANG_TR, "config round trip");
  CHECK(strstr(txt, "temp_unit = f") != NULL && b.temp_unit == TEMP_F, "temp_unit round trip");
  bad = config_apply_text(&b, "http_port = 99999\nunknown = 1\nk_pow = abc\nlang = xx\ntemp_unit = kelvin\n");
  CHECK(bad == 4 && b.http_port == 65535 && b.lang == LANG_TR && b.temp_unit == TEMP_F,
        "bad lines are counted, values clamped (bad=%d port=%u)", bad, b.http_port);
  bad = config_apply_text(&b, "temp_unit = C\n");
  CHECK(bad == 0 && b.temp_unit == TEMP_C, "temp_unit = C");
  CHECK(b.home_tile == 1, "home_tile is on by default");
  bad = config_apply_text(&b, "home_tile = 0\n");
  CHECK(bad == 0 && b.home_tile == 0, "home_tile = 0");
  config_to_text(&b, txt, sizeof(txt));
  CHECK(strstr(txt, "home_tile = 0") != NULL, "home_tile = 0 is written back");
  bad = config_apply_text(&b, "lang = ko\n");
  CHECK(bad == 0 && b.lang == LANG_KO, "lang = ko");
  config_to_text(&b, txt, sizeof(txt));
  CHECK(strstr(txt, "lang = ko") != NULL, "lang = ko is written back");
  bad = config_apply_text(&b, "lang = auto\n");
  CHECK(bad == 0 && b.lang == LANG_AUTO, "lang = auto");

  uint32_t ep = 0; uint64_t since = 0;
  const char *sp = "build/test_state.ini";
  CHECK(state_save(sp, 4, 1700000000000ULL) == 0, "state_save");
  state_load(sp, &ep, &since);
  CHECK(ep == 4 && since == 1700000000000ULL, "state round trip (%u, %llu)", ep, (unsigned long long)since);

  char ts[32];
  util_fmt_utc(1700000000000ULL, ts, sizeof(ts), 0);
  CHECK(strcmp(ts, "2023-11-14 22:13:20") == 0, "UTC format (%s)", ts);
}

int
main(void) {
  config_t c;
  config_defaults(&c);
  printf("%s %s - diagnosis tests\n", PS5TD_NAME, PS5TD_VERSION);
  test_absolute(&c);
  test_relative(&c);
  test_trend(&c);
  test_i18n(&c);
  test_persistence(&c);
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
