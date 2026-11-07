/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Diagnosis logic. Fully portable C with nothing PS5-specific, so it is
 * compiled and tested on a PC (test/test_analysis.c).
 */
#ifndef PS5TD_ANALYSIS_H
#define PS5TD_ANALYSIS_H

#include "i18n.h"
#include "ps5td.h"

/* Accumulator filled during a session */
typedef struct {
  session_summary_t s;
  uint32_t first_mono_s, last_mono_s;
  uint32_t n;
  double   sum_soc, sum_cpu, sum_fan, sum_pow, sum_fps;
  uint32_t n_ss, n_cpu, n_pow, n_fps;
  int16_t  t0_soc;  uint32_t t0_s;
  int16_t  t300_soc; int have_t300;
  uint32_t n_hot, n_danger;
} session_acc_t;

typedef struct {
  int      valid;
  unsigned n;             /* sessions in the baseline */
  unsigned need;          /* sessions required */
  float    cdi_mean, cdi_sd;
  uint64_t first_ms, last_ms;
} baseline_t;

#define AN_REASON_LEN 512   /* room for several reasons in Cyrillic / CJK UTF-8 */

typedef struct {
  int      verdict;             /* final verdict */
  int      verdict_abs, verdict_rel;
  char     reason_abs[LANG_COUNT][AN_REASON_LEN];
  char     reason_rel[LANG_COUNT][AN_REASON_LEN];
  char     trend_text[LANG_COUNT][AN_REASON_LEN];
  baseline_t baseline;
  float    recent_cdi; unsigned recent_n;
  float    drift;
  float    trend_per_30d; int trend_valid;
  int      has_last; session_summary_t last;
} overall_t;

void  an_begin(session_acc_t *a, uint64_t wall_ms, uint32_t mono_s, const char *title, uint32_t epoch);
void  an_add(session_acc_t *a, const sample_t *s, const config_t *c);
void  an_finish(const session_acc_t *a, const config_t *c, session_summary_t *out);

float an_cdi(const session_summary_t *s, const config_t *c);
/* Verdict from absolute rules; "why" is written in the given language. */
int   an_verdict_abs(const session_summary_t *s, const config_t *c, int lang, char *why, size_t n);
void  an_baseline(const session_summary_t *hist, unsigned n, uint32_t epoch, const config_t *c, baseline_t *b);
int   an_recent(const session_summary_t *hist, unsigned n, uint32_t epoch, const config_t *c,
                const baseline_t *b, float *cdi_mean, unsigned *used);
int   an_verdict_rel(float recent_cdi, unsigned recent_n, const baseline_t *b, const config_t *c,
                     int lang, float *drift, char *why, size_t n);
int   an_trend(const session_summary_t *hist, unsigned n, uint32_t epoch, float *per_30d, unsigned *used);

/* Overall verdict from the whole history; texts are filled in every language. */
void  an_overall(const session_summary_t *hist, unsigned n, uint32_t epoch, const config_t *c, overall_t *o);

const char *an_verdict_name(int v);              /* machine code: "OK", "WATCH", "CLEAN", "SERVICE", "NO_DATA" */
const char *an_verdict_badge(int v, int lang);   /* short display text: "WATCH" / "İZLE" */
const char *an_verdict_label(int v, int lang);   /* one-line headline */
const char *an_verdict_advice(int v, int lang);  /* what to do */

#endif
