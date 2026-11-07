/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_HISTORY_H
#define PS5TD_HISTORY_H

#include "ps5td.h"
#include <stdio.h>

/* history.csv: one line per session. */
unsigned history_load(const char *path, session_summary_t *out, unsigned max);
int      history_append(const char *path, const session_summary_t *s);
int      history_write_line(FILE *f, const session_summary_t *s);
int      history_parse_line(const char *line, session_summary_t *s);

/* state.ini: baseline counter (epoch) and when it started. */
void     state_load(const char *path, uint32_t *epoch, uint64_t *since_ms);
int      state_save(const char *path, uint32_t epoch, uint64_t since_ms);

#endif
