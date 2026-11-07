/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Diagnosis logic: "does the console need maintenance?"
 *
 * Physics: chip temperature = ambient + power x thermal resistance(fan).
 * Thermal resistance rises with dust and with dried-out / migrated thermal
 * interface material. We cannot measure ambient temperature, but we do know
 * fan speed and power, so two methods are combined:
 *
 *  1. Absolute rules (work from day one): fan high AND temperature high ->
 *     cooling is insufficient. A high temperature with a low fan is normal
 *     (the firmware raises the fan later); both high means the cooling
 *     system has run out of headroom.
 *
 *  2. Comparison with a baseline (after a few sessions): every session gets
 *     a "cooling demand index":
 *         CDI = T_soc + k_fan * (fan% - 40) - k_pow * (P - 150 W)
 *     A console that needs a faster fan and/or runs hotter for the same work
 *     gets a higher CDI; differences in power (heavy vs. light game) are
 *     normalised. The first N sessions form the baseline, later ones are
 *     compared with it. After cleaning, "New baseline" increments the epoch.
 */
#include "analysis.h"
#include "texts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NONE (-1000.0f)

void
an_begin(session_acc_t *a, uint64_t wall_ms, uint32_t mono_s, const char *title, uint32_t epoch) {
  memset(a, 0, sizeof(*a));
  a->s.start_ms = wall_ms;
  a->s.epoch = epoch;
  snprintf(a->s.title, sizeof(a->s.title), "%s", (title && *title) ? title : "-");
  a->first_mono_s = a->last_mono_s = mono_s;
  a->s.max_soc = a->s.max_cpu = a->s.max_fan = -1.0f;
  a->s.warm_rate = NONE;
  a->s.cdi = NONE;
  a->s.verdict = VERDICT_NA;
  a->t0_soc = -1;
  a->t300_soc = -1;
}

void
an_add(session_acc_t *a, const sample_t *s, const config_t *c) {
  a->n++;
  a->last_mono_s = s->mono_s;
  uint32_t el = s->mono_s - a->first_mono_s;
  float fan = s->fan_pct10 >= 0 ? (float)s->fan_pct10 / 10.0f : -1.0f;

  if(s->soc_c > 0) {
    if(a->t0_soc < 0) { a->t0_soc = s->soc_c; a->t0_s = el; }
    if(!a->have_t300 && el >= a->t0_s + 300) { a->t300_soc = s->soc_c; a->have_t300 = 1; }
    if((float)s->soc_c > a->s.max_soc) a->s.max_soc = (float)s->soc_c;
    if((float)s->soc_c >= c->hot_soc_c) a->n_danger++;
    if(fan >= 80.0f && s->soc_c >= 80) a->n_hot++;
  }
  if(s->cpu_c > 0 && (float)s->cpu_c > a->s.max_cpu) a->s.max_cpu = (float)s->cpu_c;
  if(fan >= 0.0f && fan > a->s.max_fan) a->s.max_fan = fan;

  /* Steady state: samples after the warm-up period */
  if(el >= c->warmup_s && s->soc_c > 0 && fan >= 0.0f) {
    a->n_ss++;
    a->sum_soc += s->soc_c;
    a->sum_fan += fan;
    if(s->cpu_c > 0)     { a->sum_cpu += s->cpu_c;              a->n_cpu++; }
    if(s->power_dw >= 0) { a->sum_pow += s->power_dw / 10.0;    a->n_pow++; }
    if(s->fps10 >= 0)    { a->sum_fps += s->fps10 / 10.0;       a->n_fps++; }
  }
}

float
an_cdi(const session_summary_t *s, const config_t *c) {
  float cdi = s->ss_soc + c->k_fan * (s->ss_fan - 40.0f);
  if(s->power_valid && s->ss_power > 20.0f) cdi -= c->k_pow * (s->ss_power - 150.0f);
  return cdi;
}

void
an_finish(const session_acc_t *a, const config_t *c, session_summary_t *o) {
  *o = a->s;
  o->dur_s = a->last_mono_s - a->first_mono_s;
  o->ss_samples = a->n_ss;
  double per_sample_s = c->sample_ms / 1000.0;
  o->ss_valid = (a->n_ss * per_sample_s) >= (double)c->min_ss_s;
  o->ss_soc = o->ss_cpu = o->ss_fan = o->ss_power = o->ss_fps = -1.0f;
  o->power_valid = o->fps_valid = 0;
  if(o->ss_valid) {
    o->ss_soc = (float)(a->sum_soc / a->n_ss);
    o->ss_fan = (float)(a->sum_fan / a->n_ss);
    if(a->n_cpu) o->ss_cpu = (float)(a->sum_cpu / a->n_cpu);
    /* Power and FPS count only if read for at least half of the steady window */
    if(a->n_pow >= a->n_ss / 2 && a->n_pow > 0) { o->ss_power = (float)(a->sum_pow / a->n_pow); o->power_valid = 1; }
    if(a->n_fps >= a->n_ss / 2 && a->n_fps > 0) { o->ss_fps = (float)(a->sum_fps / a->n_fps); o->fps_valid = 1; }
    o->cdi = an_cdi(o, c);
  }
  o->warm_rate = a->have_t300 ? (float)(a->t300_soc - a->t0_soc) / 5.0f : NONE;
  o->hot_share = a->n ? (float)a->n_hot / (float)a->n : 0.0f;
  o->danger_s = (uint32_t)(a->n_danger * per_sample_s);
}

static int
maxv(int a, int b) { return a > b ? a : b; }

static void
add_reason(char *why, size_t n, const char *txt) {
  size_t used = strlen(why);
  if(used && used + 2 < n) { why[used++] = ' '; why[used] = '\0'; }
  snprintf(why + used, n - used, "%s", txt);
}

/* Texts come from src/texts.h (generated from tools/i18n_texts.py); each language
   writes the percent sign its own way ("38%", "%38", "38 %").
   Temperatures in texts follow config "temp_unit"; the comparisons stay in °C. */
static double tc(const config_t *c, double v) { return i18n_temp(v, c->temp_unit); }
static double td(const config_t *c, double v) { return i18n_tdelta(v, c->temp_unit); }
static const char *tsym(const config_t *c) { return i18n_unit_sym(c->temp_unit); }

int
an_verdict_abs(const session_summary_t *s, const config_t *c, int lang, char *why, size_t n) {
  why[0] = '\0';
  if(!s->ss_valid) {
    snprintf(why, n, TX_NOT_STEADY(lang), (c->warmup_s + c->min_ss_s + 59) / 60);
    return VERDICT_NA;
  }
  int v = VERDICT_OK;
  char t[AN_REASON_LEN];
  double soc = s->ss_soc, fan = s->ss_fan;

  if(s->danger_s >= 60) {
    v = VERDICT_SERVICE;
    snprintf(t, sizeof(t), TX_DANGER(lang), (unsigned)s->danger_s, tc(c, c->hot_soc_c), tsym(c));
    add_reason(why, n, t);
  }
  if(s->ss_fan < 5.0f && s->ss_soc >= 70.0f) {
    v = VERDICT_SERVICE;
    snprintf(t, sizeof(t), TX_FAN_STOPPED(lang), tc(c, soc), tsym(c), fan);
    add_reason(why, n, t);
  }
  if(s->ss_fan >= c->abs_service_fan && s->ss_soc >= c->abs_service_soc) {
    v = maxv(v, VERDICT_SERVICE);
    snprintf(t, sizeof(t), TX_ABS_SERVICE(lang), fan, tc(c, soc), tsym(c));
    add_reason(why, n, t);
  } else if(s->ss_fan >= c->abs_clean_fan && s->ss_soc >= c->abs_clean_soc) {
    v = maxv(v, VERDICT_CLEAN);
    snprintf(t, sizeof(t), TX_ABS_CLEAN(lang), fan, tc(c, soc), tsym(c));
    add_reason(why, n, t);
  } else if(s->ss_fan >= c->abs_watch_fan && s->ss_soc >= c->abs_watch_soc) {
    v = maxv(v, VERDICT_WATCH);
    snprintf(t, sizeof(t), TX_ABS_WATCH(lang), fan, tc(c, soc), tsym(c));
    add_reason(why, n, t);
  }
  if(s->hot_share >= 0.5f && v < VERDICT_CLEAN) {
    v = VERDICT_CLEAN;
    snprintf(t, sizeof(t), TX_HOT_SHARE(lang), tc(c, 80.0), tsym(c), (double)(s->hot_share * 100.0f));
    add_reason(why, n, t);
  }
  if(v == VERDICT_OK) {
    if(s->power_valid)
      snprintf(why, n, TX_OK_POWER(lang), tc(c, soc), tsym(c), fan, (double)s->ss_power);
    else
      snprintf(why, n, TX_OK_NOPOWER(lang), tc(c, soc), tsym(c), fan);
  }
  return v;
}

static int
usable(const session_summary_t *s, uint32_t epoch) {
  return s->epoch == epoch && s->ss_valid && s->cdi > NONE + 1.0f;
}

void
an_baseline(const session_summary_t *hist, unsigned n, uint32_t epoch, const config_t *c, baseline_t *b) {
  memset(b, 0, sizeof(*b));
  b->need = c->baseline_sessions;
  double sum = 0.0, sum2 = 0.0;
  for(unsigned i = 0; i < n && b->n < c->baseline_sessions; i++) {
    if(!usable(&hist[i], epoch)) continue;
    if(b->n == 0) b->first_ms = hist[i].start_ms;
    b->last_ms = hist[i].start_ms;
    sum  += hist[i].cdi;
    sum2 += (double)hist[i].cdi * hist[i].cdi;
    b->n++;
  }
  if(b->n == c->baseline_sessions) {
    b->valid = 1;
    b->cdi_mean = (float)(sum / b->n);
    double var = sum2 / b->n - (sum / b->n) * (sum / b->n);
    b->cdi_sd = var > 0 ? (float)sqrt(var) : 0.0f;
  }
}

/* "Now": mean of the last recent_sessions sessions that are not part of the
   baseline. If there are none yet, the last baseline session is used. */
int
an_recent(const session_summary_t *hist, unsigned n, uint32_t epoch, const config_t *c,
          const baseline_t *b, float *cdi_mean, unsigned *used) {
  *cdi_mean = 0.0f; *used = 0;
  unsigned skip = b->valid ? b->n : 0;   /* the first 'skip' usable sessions are the baseline */
  unsigned seen = 0;
  float buf[64]; unsigned nb = 0;
  for(unsigned i = 0; i < n; i++) {
    if(!usable(&hist[i], epoch)) continue;
    seen++;
    if(seen <= skip) continue;
    if(nb < 64) buf[nb++] = hist[i].cdi;
    else { memmove(buf, buf + 1, sizeof(float) * 63); buf[63] = hist[i].cdi; }
  }
  if(nb == 0) {
    /* no session after the baseline yet: use the last baseline session */
    float last = 0.0f; int have = 0;
    for(unsigned i = 0; i < n; i++) if(usable(&hist[i], epoch)) { last = hist[i].cdi; have = 1; }
    if(!have) return -1;
    *cdi_mean = last; *used = 1;
    return 0;
  }
  unsigned k = c->recent_sessions < nb ? c->recent_sessions : nb;
  double sum = 0.0;
  for(unsigned i = nb - k; i < nb; i++) sum += buf[i];
  *cdi_mean = (float)(sum / k);
  *used = k;
  return 0;
}

int
an_verdict_rel(float recent_cdi, unsigned recent_n, const baseline_t *b, const config_t *c,
               int lang, float *drift, char *why, size_t n) {
  *drift = 0.0f;
  if(!b->valid) {
    snprintf(why, n, TX_BUILDING(lang), b->n, b->need);
    return VERDICT_NA;
  }
  if(recent_n == 0) {
    snprintf(why, n, "%s", TX_BASE_READY(lang));
    return VERDICT_NA;
  }
  float d = recent_cdi - b->cdi_mean;
  *drift = d;
  int v = d >= c->service_drift ? VERDICT_SERVICE
        : d >= c->clean_drift   ? VERDICT_CLEAN
        : d >= c->watch_drift   ? VERDICT_WATCH
        : VERDICT_OK;
  snprintf(why, n, TX_REL(lang), td(c, d), tsym(c), tc(c, b->cdi_mean), td(c, b->cdi_sd), recent_n, tc(c, recent_cdi));
  return v;
}

/* Least squares: slope of CDI over time, °C per 30 days. */
int
an_trend(const session_summary_t *hist, unsigned n, uint32_t epoch, float *per_30d, unsigned *used) {
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  unsigned k = 0;
  uint64_t t0 = 0, tmax = 0;
  for(unsigned i = 0; i < n; i++) {
    if(!usable(&hist[i], epoch)) continue;
    if(k == 0) t0 = hist[i].start_ms;
    double x = (double)(hist[i].start_ms - t0) / 86400000.0;   /* days */
    double y = hist[i].cdi;
    sx += x; sy += y; sxx += x * x; sxy += x * y;
    tmax = hist[i].start_ms;
    k++;
  }
  *used = k;
  *per_30d = 0.0f;
  if(k < 6) return -1;
  double span_days = (double)(tmax - t0) / 86400000.0;
  if(span_days < 14.0) return -1;
  double den = k * sxx - sx * sx;
  if(den <= 0.0) return -1;
  double slope = (k * sxy - sx * sy) / den;   /* °C/day */
  *per_30d = (float)(slope * 30.0);
  return 0;
}

void
an_overall(const session_summary_t *hist, unsigned n, uint32_t epoch, const config_t *c, overall_t *o) {
  memset(o, 0, sizeof(*o));
  o->verdict = o->verdict_abs = o->verdict_rel = VERDICT_NA;

  /* Latest steady session */
  for(unsigned i = n; i > 0; i--) {
    if(hist[i - 1].epoch != epoch) continue;
    o->last = hist[i - 1];
    o->has_last = 1;
    if(hist[i - 1].ss_valid) break;
  }
  unsigned need_min = (c->warmup_s + c->min_ss_s + 59) / 60;
  for(int l = 0; l < LANG_COUNT; l++) {
    if(o->has_last)
      o->verdict_abs = an_verdict_abs(&o->last, c, l, o->reason_abs[l], AN_REASON_LEN);
    else
      snprintf(o->reason_abs[l], AN_REASON_LEN, TX_NO_SESSION(l), need_min);
  }

  an_baseline(hist, n, epoch, c, &o->baseline);
  if(an_recent(hist, n, epoch, c, &o->baseline, &o->recent_cdi, &o->recent_n) != 0) o->recent_n = 0;
  for(int l = 0; l < LANG_COUNT; l++)
    o->verdict_rel = an_verdict_rel(o->recent_cdi, o->recent_n, &o->baseline, c, l, &o->drift,
                                    o->reason_rel[l], AN_REASON_LEN);

  unsigned tn = 0;
  o->trend_valid = an_trend(hist, n, epoch, &o->trend_per_30d, &tn) == 0;
  int worse = o->trend_valid && o->trend_per_30d >= 2.0f;
  for(int l = 0; l < LANG_COUNT; l++) {
    if(o->trend_valid)
      snprintf(o->trend_text[l], AN_REASON_LEN, TX_TREND(l),
               tn, td(c, o->trend_per_30d), tsym(c), worse ? TX_TREND_WORSE(l) : "");
    else
      snprintf(o->trend_text[l], AN_REASON_LEN, "%s", TX_TREND_NONE(l));
  }

  int v = VERDICT_NA;
  if(o->verdict_abs != VERDICT_NA) v = o->verdict_abs;
  if(o->verdict_rel != VERDICT_NA) v = maxv(v, o->verdict_rel);
  if(worse && v < VERDICT_WATCH) v = VERDICT_WATCH;
  o->verdict = v;
}

const char *
an_verdict_name(int v) {
  switch(v) {
    case VERDICT_OK:      return "OK";
    case VERDICT_WATCH:   return "WATCH";
    case VERDICT_CLEAN:   return "CLEAN";
    case VERDICT_SERVICE: return "SERVICE";
    default:              return "NO_DATA";
  }
}

const char *
an_verdict_badge(int v, int lang) {
  switch(v) {
    case VERDICT_OK:      return TX_BADGE_OK(lang);
    case VERDICT_WATCH:   return TX_BADGE_WATCH(lang);
    case VERDICT_CLEAN:   return TX_BADGE_CLEAN(lang);
    case VERDICT_SERVICE: return TX_BADGE_SERVICE(lang);
    default:              return TX_BADGE_NA(lang);
  }
}

const char *
an_verdict_label(int v, int lang) {
  switch(v) {
    case VERDICT_OK:      return TX_LABEL_OK(lang);
    case VERDICT_WATCH:   return TX_LABEL_WATCH(lang);
    case VERDICT_CLEAN:   return TX_LABEL_CLEAN(lang);
    case VERDICT_SERVICE: return TX_LABEL_SERVICE(lang);
    default:              return TX_LABEL_NA(lang);
  }
}

const char *
an_verdict_advice(int v, int lang) {
  switch(v) {
    case VERDICT_OK:      return TX_ADVICE_OK(lang);
    case VERDICT_WATCH:   return TX_ADVICE_WATCH(lang);
    case VERDICT_CLEAN:   return TX_ADVICE_CLEAN(lang);
    case VERDICT_SERVICE: return TX_ADVICE_SERVICE(lang);
    default:              return TX_ADVICE_NA(lang);
  }
}
