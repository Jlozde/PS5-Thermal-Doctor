/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_LOG_H
#define PS5TD_LOG_H

#include <stddef.h>

/* With path == NULL the log goes to stdout and the in-memory ring only. */
void log_init(const char *path);

/* Every line is formatted in both languages. stdout and the log file get the
   active console language; the web UI can ask for either (log_tail). */
void log_printf2(const char *fmt_en, const char *fmt_tr, ...)
  __attribute__((format(printf, 1, 3), format(printf, 2, 3)));

/* Copies the latest lines (oldest first) in the given language into out;
   returns the number of bytes written. */
size_t log_tail(int lang, char *out, size_t n);

/* LOG("English %s", "Türkçe %s", arg) */
#define LOG(en, tr, ...) log_printf2(en, tr, ##__VA_ARGS__)

#endif
