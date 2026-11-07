/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe.h"
#include "log.h"
#include "platform.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PROBE_INTERVAL_S 3

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int  g_running;
static char g_title[PS5TD_TITLE_LEN] = "-";

static void *
probe_thread(void *arg) {
  (void)arg;
  int last = -1;
  for(;;) {
    char t[PS5TD_TITLE_LEN];
    int r = plat_game_probe(t, sizeof(t));
    pthread_mutex_lock(&g_lock);
    g_running = r;
    snprintf(g_title, sizeof(g_title), "%s", t);
    pthread_mutex_unlock(&g_lock);
    if(r != last) {
      if(r) LOG("Game state: game in front - %s", "Oyun durumu: önde oyun var - %s", t);
      else  LOG("Game state: no game", "Oyun durumu: oyun yok");
      last = r;
    }
    sleep(PROBE_INTERVAL_S);
  }
  return NULL;
}

int
probe_start(void) {
  pthread_t th;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&th, &attr, probe_thread, NULL);
  pthread_attr_destroy(&attr);
  if(rc != 0) LOG("Cannot start the game detection thread (%d).", "Oyun tespit iş parçacığı başlatılamadı (%d).", rc);
  return rc;
}

int
probe_get(char *title, size_t n) {
  pthread_mutex_lock(&g_lock);
  int r = g_running;
  if(title) snprintf(title, n, "%s", g_title);
  pthread_mutex_unlock(&g_lock);
  return r;
}
