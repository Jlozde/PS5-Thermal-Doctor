/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#endif

uint64_t
util_wall_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

uint64_t
util_mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

uint32_t
util_mono_s(void) {
  return (uint32_t)(util_mono_ms() / 1000ULL);
}

/* Day count to calendar date (Howard Hinnant's algorithm); gives the same
   result on every platform without depending on libc's gmtime_r. */
static void
civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = (int64_t)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned dd = doy - (153 * mp + 2) / 5 + 1;
  unsigned mm = mp < 10 ? mp + 3 : mp - 9;
  *y = (int)(yy + (mm <= 2));
  *m = mm;
  *d = dd;
}

void
util_fmt_utc(uint64_t wall_ms, char *out, size_t n, int file_safe) {
  int64_t secs = (int64_t)(wall_ms / 1000ULL);
  int64_t days = secs / 86400;
  int64_t rem  = secs % 86400;
  int y; unsigned m, d;
  civil_from_days(days, &y, &m, &d);
  unsigned hh = (unsigned)(rem / 3600), mi = (unsigned)((rem % 3600) / 60), ss = (unsigned)(rem % 60);
  snprintf(out, n, file_safe ? "%04d%02u%02u-%02u%02u%02u" : "%04d-%02u-%02u %02u:%02u:%02u",
           y, m, d, hh, mi, ss);
}

void
util_trim(char *s) {
  char *p = s;
  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if(p != s) memmove(s, p, strlen(p) + 1);
  size_t len = strlen(s);
  while(len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
    s[--len] = '\0';
}

int
util_mkdir(const char *path) {
#ifdef _WIN32
  int rc = _mkdir(path);
#else
  int rc = mkdir(path, 0755);
#endif
  if(rc != 0 && errno == EEXIST) return 0;
  return rc;
}

int
util_safe_name(const char *s) {
  if(!s || !*s) return 0;
  for(; *s; s++) {
    char c = *s;
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
       c == '.' || c == '_' || c == '-')
      continue;
    return 0;
  }
  return 1;
}
