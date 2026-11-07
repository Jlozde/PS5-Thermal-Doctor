/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_SAMPLER_H
#define PS5TD_SAMPLER_H

#include "analysis.h"
#include "ps5td.h"

typedef struct {
  sample_t   now;                 /* latest sample */
  int        session_active;
  char       title[PS5TD_TITLE_LEN];
  uint32_t   session_s;
  int        live_valid;          /* interim summary while a game runs */
  session_summary_t live;
  int        live_verdict;
  char       live_reason[LANG_COUNT][AN_REASON_LEN];
  overall_t  overall;
  uint32_t   epoch;
  uint64_t   epoch_since_ms;
  unsigned   history_n;
  uint32_t   samples_total;
} status_t;

int      sampler_start(void);
void     sampler_get_status(status_t *out);
void     sampler_get_config(config_t *out);
int      sampler_set_config_text(const char *ini_text);   /* applies + saves; returns bad line count */
unsigned sampler_get_history(session_summary_t *out, unsigned max);
unsigned sampler_get_recent(sample_t *out, unsigned max, unsigned step);   /* oldest first */
void     sampler_reset_baseline(void);

#endif
