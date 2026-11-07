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

/* Turkish writes the percent sign before the number ("%38"), English after ("38%"). */

int
an_verdict_abs(const session_summary_t *s, const config_t *c, int lang, char *why, size_t n) {
  why[0] = '\0';
  if(!s->ss_valid) {
    snprintf(why, n, LNGX(lang, "Not enough steady-state data: at least %u min of continuous play is needed.",
                                "Kararlı durum verisi yetersiz: en az %u dk kesintisiz oyun gerekli."),
             (c->warmup_s + c->min_ss_s + 59) / 60);
    return VERDICT_NA;
  }
  int v = VERDICT_OK;
  char t[AN_REASON_LEN];
  double soc = s->ss_soc, fan = s->ss_fan;

  if(s->danger_s >= 60) {
    v = VERDICT_SERVICE;
    snprintf(t, sizeof(t), LNGX(lang, "SoC spent %u s at or above %.0f°C.",
                                      "SoC %u sn boyunca %.0f°C ve üzerinde kaldı."),
             (unsigned)s->danger_s, (double)c->hot_soc_c);
    add_reason(why, n, t);
  }
  if(s->ss_fan < 5.0f && s->ss_soc >= 70.0f) {
    v = VERDICT_SERVICE;
    snprintf(t, sizeof(t), LNGX(lang, "SoC %.0f°C with the fan at %.0f%%: the fan may not be spinning.",
                                      "SoC %.0f°C iken fan %%%.0f: fan dönmüyor olabilir."), soc, fan);
    add_reason(why, n, t);
  }
  if(s->ss_fan >= c->abs_service_fan && s->ss_soc >= c->abs_service_soc) {
    v = maxv(v, VERDICT_SERVICE);
    snprintf(t, sizeof(t), LNGX(lang, "Fan at %.0f%% and SoC still %.0f°C: cooling is insufficient.",
                                      "Fan %%%.0f'de çalışırken SoC %.0f°C: soğutma yetersiz."), fan, soc);
    add_reason(why, n, t);
  } else if(s->ss_fan >= c->abs_clean_fan && s->ss_soc >= c->abs_clean_soc) {
    v = maxv(v, VERDICT_CLEAN);
    snprintf(t, sizeof(t), LNGX(lang, "Fan %.0f%% and SoC %.0f°C: the fan struggles to hold the temperature at high speed.",
                                      "Fan %%%.0f ve SoC %.0f°C: fan yüksek devirde sıcaklığı zor tutuyor."), fan, soc);
    add_reason(why, n, t);
  } else if(s->ss_fan >= c->abs_watch_fan && s->ss_soc >= c->abs_watch_soc) {
    v = maxv(v, VERDICT_WATCH);
    snprintf(t, sizeof(t), LNGX(lang, "Fan %.0f%% and SoC %.0f°C: slightly above normal.",
                                      "Fan %%%.0f ve SoC %.0f°C: normalin biraz üstünde."), fan, soc);
    add_reason(why, n, t);
  }
  if(s->hot_share >= 0.5f && v < VERDICT_CLEAN) {
    v = VERDICT_CLEAN;
    snprintf(t, sizeof(t), LNGX(lang, "Fan >= 80%% and SoC >= 80°C for %.0f%% of the session.",
                                      "Oturumun %%%.0f'inde fan >= %%80 ve SoC >= 80°C."),
             (double)(s->hot_share * 100.0f));
    add_reason(why, n, t);
  }
  if(v == VERDICT_OK) {
    if(s->power_valid)
      snprintf(why, n, LNGX(lang, "SoC %.0f°C, fan %.0f%%, %.0f W: values are in the normal range.",
                                  "SoC %.0f°C, fan %%%.0f, %.0f W: değerler normal aralıkta."),
               soc, fan, (double)s->ss_power);
    else
      snprintf(why, n, LNGX(lang, "SoC %.0f°C, fan %.0f%%: values are in the normal range.",
                                  "SoC %.0f°C, fan %%%.0f: değerler normal aralıkta."), soc, fan);
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
    snprintf(why, n, LNGX(lang, "Building the baseline: %u/%u sessions. Play a few more games.",
                                "Referans oluşuyor: %u/%u oturum. Birkaç oyun daha oynayın."), b->n, b->need);
    return VERDICT_NA;
  }
  if(recent_n == 0) {
    snprintf(why, n, "%s", LNGX(lang, "Baseline ready; no newer session to compare yet.",
                                      "Referans hazır, karşılaştırılacak yeni oturum yok."));
    return VERDICT_NA;
  }
  float d = recent_cdi - b->cdi_mean;
  *drift = d;
  int v = d >= c->service_drift ? VERDICT_SERVICE
        : d >= c->clean_drift   ? VERDICT_CLEAN
        : d >= c->watch_drift   ? VERDICT_WATCH
        : VERDICT_OK;
  snprintf(why, n, LNGX(lang, "Cooling index %+.1f°C vs. baseline (baseline %.1f ± %.1f, last %u sessions %.1f).",
                              "Soğutma indeksi referansa göre %+.1f°C (referans %.1f ± %.1f, son %u oturum %.1f)."),
           (double)d, (double)b->cdi_mean, (double)b->cdi_sd, recent_n, (double)recent_cdi);
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
      snprintf(o->reason_abs[l], AN_REASON_LEN,
               LNGX(l, "No recorded session yet. Start a game and play for at least %u min.",
                       "Henüz kayıtlı oturum yok. Bir oyun açın ve en az %u dk oynayın."), need_min);
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
      snprintf(o->trend_text[l], AN_REASON_LEN,
               LNGX(l, "Trend over %u sessions: %+.1f°C per 30 days.%s",
                       "Eğilim: %u oturum üzerinden %+.1f°C / 30 gün.%s"),
               tn, (double)o->trend_per_30d,
               worse ? LNGX(l, " Cooling is getting worse over time.", " Soğutma zamanla kötüleşiyor.") : "");
    else
      snprintf(o->trend_text[l], AN_REASON_LEN, "%s",
               LNGX(l, "Not enough data for a trend (needs at least 6 sessions over 14 days).",
                       "Eğilim için yeterli veri yok (en az 6 oturum ve 14 gün)."));
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
    case VERDICT_OK:      return "OK";
    case VERDICT_WATCH:   return LNGX(lang, "WATCH", "İZLE");
    case VERDICT_CLEAN:   return LNGX(lang, "CLEAN", "TEMİZLİK");
    case VERDICT_SERVICE: return LNGX(lang, "SERVICE", "SERVİS");
    default:              return LNGX(lang, "NO DATA", "VERİ YOK");
  }
}

const char *
an_verdict_label(int v, int lang) {
  switch(v) {
    case VERDICT_OK:      return LNGX(lang, "Cooling is normal", "Soğutma normal");
    case VERDICT_WATCH:   return LNGX(lang, "Slight deviation, keep monitoring", "Hafif sapma, izlemeye devam");
    case VERDICT_CLEAN:   return LNGX(lang, "Dust cleaning recommended", "Toz temizliği önerilir");
    case VERDICT_SERVICE: return LNGX(lang, "Cooling insufficient: maintenance needed", "Soğutma yetersiz: bakım gerekli");
    default:              return LNGX(lang, "Not enough data yet", "Henüz yeterli veri yok");
  }
}

const char *
an_verdict_advice(int v, int lang) {
  switch(v) {
    case VERDICT_OK:
      return LNGX(lang,
        "No maintenance needed. Keep measuring; the comparison gets more precise as the baseline builds up.",
        "Bakım gerekmiyor. Ölçüme devam edin; referans oluştukça karşılaştırma hassaslaşır.");
    case VERDICT_WATCH:
      return LNGX(lang,
        "Make sure the console's vents are not blocked and the room is not unusually hot. "
        "Watch a few more sessions; plan a cleaning if the deviation grows.",
        "Cihazın havalandırma deliklerinin kapalı olmadığından ve ortamın aşırı sıcak olmadığından emin olun. "
        "Birkaç oturum daha izleyin; sapma büyürse temizlik planlayın.");
    case VERDICT_CLEAN:
      return LNGX(lang,
        "Dust has probably built up on the fan and heatsink fins. Clean them with compressed air while the "
        "console is off. After cleaning, reset the comparison with the 'New baseline' button.",
        "Fan ve soğutucu kanatçıklarında toz birikmiş olabilir. Cihaz kapalıyken basınçlı hava ile "
        "temizlik önerilir. Temizlikten sonra 'Yeni referans' düğmesiyle karşılaştırmayı sıfırlayın.");
    case VERDICT_SERVICE:
      return LNGX(lang,
        "The temperature does not come down even with the fan at full speed. Start with a thorough dust "
        "cleaning; if the problem persists, have the liquid metal / thermal interface checked by an "
        "authorised service or an experienced technician. Re-applying liquid metal is risky; do not try it yourself.",
        "Fan tam güçte çalışmasına rağmen sıcaklık düşmüyor. Önce kapsamlı toz temizliği yapın; "
        "sorun sürerse sıvı metal / termal arayüz kontrolü için yetkili servis veya deneyimli bir "
        "teknisyene başvurun. Sıvı metal yenilemesi riskli bir işlemdir, kendiniz denemeyin.");
    default:
      return LNGX(lang,
        "Start a game and play for at least 15 minutes; the first evaluation is made when the game closes.",
        "Bir oyun açın ve en az 15 dakika oynayın; ilk değerlendirme oyun kapanınca yapılır.");
  }
}
