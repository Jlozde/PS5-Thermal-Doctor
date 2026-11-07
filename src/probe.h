/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_PROBE_H
#define PS5TD_PROBE_H

#include <stddef.h>

/* Runs game detection on its own thread: if a Sony service hangs, the sampler
   and the web server keep working and only this information goes stale. */
int  probe_start(void);
int  probe_get(char *title, size_t n);   /* 1 = a game is in the foreground */

#endif
