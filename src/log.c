/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Log: stdout (the socket the payload was sent from), a file, and the last
 * 128 lines in memory for the web UI (kept in both languages). The file starts
 * over once it grows past 512 KB.
 */
#include "log.h"
#include "i18n.h"
#include "util.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define LOG_LINES 128
#define LOG_LINE  240
#define LOG_MAX_FILE (512 * 1024)

static char            g_lines[LOG_LINES][LANG_COUNT][LOG_LINE];
static unsigned        g_head, g_count;
static char            g_path[256];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void
log_init(const char *path) {
  pthread_mutex_lock(&g_lock);
  if(path) snprintf(g_path, sizeof(g_path), "%s", path);
  else g_path[0] = '\0';
  pthread_mutex_unlock(&g_lock);
}

void
log_printf2(const char *fmt_en, const char *fmt_tr, ...) {
  char msg[LANG_COUNT][LOG_LINE];
  char stamp[32];
  va_list ap, ap2;

  va_start(ap, fmt_tr);
  va_copy(ap2, ap);
  vsnprintf(msg[LANG_EN], LOG_LINE, fmt_en, ap);
  vsnprintf(msg[LANG_TR], LOG_LINE, fmt_tr, ap2);
  va_end(ap2);
  va_end(ap);
  util_fmt_utc(util_wall_ms(), stamp, sizeof(stamp), 0);
  const char *cur = msg[i18n_get() == LANG_TR ? LANG_TR : LANG_EN];

  pthread_mutex_lock(&g_lock);
  for(int l = 0; l < LANG_COUNT; l++)
    snprintf(g_lines[g_head][l], LOG_LINE, "%s %s", stamp, msg[l]);
  g_head = (g_head + 1) % LOG_LINES;
  if(g_count < LOG_LINES) g_count++;

  printf("[ps5td] %s\n", cur);
  fflush(stdout);

  if(g_path[0]) {
    struct stat st;
    const char *mode = "a";
    if(stat(g_path, &st) == 0 && st.st_size > LOG_MAX_FILE) mode = "w";
    FILE *f = fopen(g_path, mode);
    if(f) {
      fprintf(f, "%s %s\n", stamp, cur);
      fclose(f);
    }
  }
  pthread_mutex_unlock(&g_lock);
}

size_t
log_tail(int lang, char *out, size_t n) {
  size_t used = 0;
  if(!out || n == 0) return 0;
  int l = lang == LANG_TR ? LANG_TR : LANG_EN;
  out[0] = '\0';
  pthread_mutex_lock(&g_lock);
  unsigned first = (g_head + LOG_LINES - g_count) % LOG_LINES;
  for(unsigned i = 0; i < g_count; i++) {
    const char *line = g_lines[(first + i) % LOG_LINES][l];
    size_t len = strlen(line);
    if(used + len + 2 > n) break;
    memcpy(out + used, line, len);
    used += len;
    out[used++] = '\n';
    out[used] = '\0';
  }
  pthread_mutex_unlock(&g_lock);
  return used;
}
