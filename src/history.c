/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Session history (history.csv) and the baseline counter (state.ini).
 * Plain text so that people can read the CSV too; one line per session.
 */
#include "history.h"

#include <stdlib.h>
#include <string.h>

#define HDR "# ps5td history v1\n# start_ms,epoch,title,dur_s,ss_valid,ss_samples,ss_soc,ss_cpu,ss_fan,ss_power,power_valid,ss_fps,fps_valid,max_soc,max_cpu,max_fan,warm_rate,hot_share,danger_s,cdi,verdict\n"

int
history_write_line(FILE *f, const session_summary_t *s) {
  return fprintf(f, "%llu,%u,%s,%u,%d,%u,%.1f,%.1f,%.1f,%.1f,%d,%.1f,%d,%.1f,%.1f,%.1f,%.2f,%.3f,%u,%.2f,%d\n",
                 (unsigned long long)s->start_ms, s->epoch, s->title[0] ? s->title : "-", s->dur_s,
                 s->ss_valid, s->ss_samples, (double)s->ss_soc, (double)s->ss_cpu, (double)s->ss_fan,
                 (double)s->ss_power, s->power_valid, (double)s->ss_fps, s->fps_valid,
                 (double)s->max_soc, (double)s->max_cpu, (double)s->max_fan, (double)s->warm_rate,
                 (double)s->hot_share, s->danger_s, (double)s->cdi, s->verdict) > 0 ? 0 : -1;
}

int
history_parse_line(const char *line, session_summary_t *s) {
  unsigned long long start = 0;
  char title[PS5TD_TITLE_LEN + 1] = {0};
  memset(s, 0, sizeof(*s));
  int got = sscanf(line, "%llu,%u,%16[^,],%u,%d,%u,%f,%f,%f,%f,%d,%f,%d,%f,%f,%f,%f,%f,%u,%f,%d",
                   &start, &s->epoch, title, &s->dur_s, &s->ss_valid, &s->ss_samples,
                   &s->ss_soc, &s->ss_cpu, &s->ss_fan, &s->ss_power, &s->power_valid,
                   &s->ss_fps, &s->fps_valid, &s->max_soc, &s->max_cpu, &s->max_fan,
                   &s->warm_rate, &s->hot_share, &s->danger_s, &s->cdi, &s->verdict);
  if(got != 21) return -1;
  s->start_ms = start;
  snprintf(s->title, sizeof(s->title), "%.15s", title);
  return 0;
}

unsigned
history_load(const char *path, session_summary_t *out, unsigned max) {
  FILE *f = fopen(path, "r");
  if(!f) return 0;
  unsigned n = 0;
  char line[512];
  while(fgets(line, sizeof(line), f)) {
    if(line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    session_summary_t s;
    if(history_parse_line(line, &s) != 0) continue;
    if(n < max) out[n++] = s;
    else { memmove(out, out + 1, sizeof(*out) * (max - 1)); out[max - 1] = s; }
  }
  fclose(f);
  return n;
}

int
history_append(const char *path, const session_summary_t *s) {
  FILE *f = fopen(path, "r");
  int fresh = (f == NULL);
  if(f) fclose(f);
  f = fopen(path, "a");
  if(!f) return -1;
  if(fresh) fputs(HDR, f);
  int rc = history_write_line(f, s);
  if(fclose(f) != 0) rc = -1;
  return rc;
}

void
state_load(const char *path, uint32_t *epoch, uint64_t *since_ms) {
  *epoch = 0; *since_ms = 0;
  FILE *f = fopen(path, "r");
  if(!f) return;
  char line[128];
  while(fgets(line, sizeof(line), f)) {
    unsigned e = 0; unsigned long long t = 0;
    if(sscanf(line, "epoch=%u", &e) == 1) *epoch = e;
    else if(sscanf(line, "epoch_since_ms=%llu", &t) == 1) *since_ms = t;
  }
  fclose(f);
}

int
state_save(const char *path, uint32_t epoch, uint64_t since_ms) {
  char tmp[300];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if(!f) return -1;
  fprintf(f, "epoch=%u\nepoch_since_ms=%llu\n", epoch, (unsigned long long)since_ms);
  if(fclose(f) != 0) { remove(tmp); return -1; }
  remove(path);
  return rename(tmp, path) == 0 ? 0 : -1;
}
