/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_UTIL_H
#define PS5TD_UTIL_H

#include <stddef.h>
#include <stdint.h>

uint64_t util_wall_ms(void);
uint64_t util_mono_ms(void);
uint32_t util_mono_s(void);

/* UTC timestamp. file_safe=1 -> "20261107-211503", 0 -> "2026-11-07 21:15:03" */
void util_fmt_utc(uint64_t wall_ms, char *out, size_t n, int file_safe);

/* Strips leading/trailing whitespace in place. */
void util_trim(char *s);

/* Creates a directory; returns 0 if it already exists. */
int util_mkdir(const char *path);

/* Is the string safe as a file name (only [A-Za-z0-9._-])? */
int util_safe_name(const char *s);

#endif
