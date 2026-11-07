/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Home-screen tile.
 *
 * When the payload starts and "PS5 Thermal Doctor" is not on the PS5 home
 * screen, it is added there. The tile holds no program: its param.json has a
 * deeplinkUri, so selecting it opens this payload's web UI in the PS5 browser.
 * The payload still has to be sent after every console restart; the tile only
 * opens the page.
 *
 * Other homebrew does it the same way (facts only; the code is our own):
 *  - websrv (John Törnblom) and the ps5-payload-sdk install_app sample: write
 *    sce_sys/param.json + icon0.png to /user/app/<id>/, then
 *    sceAppInstUtilInitialize() and sceAppInstUtilAppInstallTitleDir(id,
 *    "/user/app/", NULL); NID Wudg3Xe3heE when the name is not exported;
 *    auth id 0x4801000000000013 (the one escalate() sets).
 *  - ShadowMountPlus: 0x80990002 means "already registered", which is fine;
 *    the per-title call is gone on firmware 12.00 and later.
 *  - ps5-cooling-system-center-pro: sceAppInstUtilInitialize() can block when
 *    the service is not up; a staging note keeps a failed try from passing
 *    for a finished tile; sceAppInstUtilAppInstallAll() registers every title
 *    waiting under /user/app, so it can pull in someone else's half-installed
 *    game.
 *
 * Choices made here:
 *  - Only the per-title call. sceAppInstUtilAppInstallAll() is never used; a
 *    firmware without the per-title call simply gets no tile.
 *  - Everything runs on a detached thread. Nothing waits for it, so a stuck
 *    system call costs one idle thread and nothing else.
 *  - "Already there": no staging note is left, and either /user/appmeta/<id>
 *    exists or our param.json is in place and the system's app database
 *    names the id. On firmware 11.60 a registered deeplink tile gets no
 *    /user/appmeta folder (measured on the test console), so the folder alone
 *    would register it again at every start. A tile the user deleted is
 *    added again at the next start unless home_tile = 0.
 *  - The tile sits in the media row ("applicationCategoryType": 65536, as
 *    in ps5-cooling-system-center-pro's media tile), next to the other apps,
 *    not among the games.
 *  - When the param.json on disk differs from the one this run would write
 *    (an older version, another http_port), the files are written again and
 *    the tile is registered again, without a notification.
 */
#include "tile.h"
#include "i18n.h"
#include "log.h"
#include "platform.h"
#include "ps5td.h"
#include "texts.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define TILE_ID        "FAKE08088"
#define TILE_APP_ROOT  "/user/app/"
#define TILE_DIR       TILE_APP_ROOT TILE_ID
#define TILE_SCE_SYS   TILE_DIR "/sce_sys"
#define TILE_PARAM     TILE_SCE_SYS "/param.json"
#define TILE_META      "/user/appmeta/" TILE_ID
#define TILE_META_SYS  "/system_data/priv/appmeta/" TILE_ID
#define TILE_STAGING   PS5TD_DATA_DIR "/tile-staging"
#define APP_DB         "/system_data/priv/mms/app.db"
#define NID_TITLE_DIR  "Wudg3Xe3heE"     /* sceAppInstUtilAppInstallTitleDir */
#define INST_ALREADY   0x80990002u

/* libSceAppInstUtil. Weak, like the sensor calls in platform.c: a firmware
   that does not export one must not stop the payload before main(). */
int sceAppInstUtilInitialize(void)                                          __attribute__((weak));
int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *dir,
                                     void *reserved)                        __attribute__((weak));
typedef int (*title_dir_fn)(const char *, const char *, void *);

/* assets/icon0.png, embedded by tools/gen_assets.py (gen/icon0.c). */
extern const unsigned char icon0_png[];
extern const unsigned int  icon0_png_len;

static volatile int      g_state = TILE_OFF;
static volatile unsigned g_code;
static volatile unsigned g_tile_port;
static volatile unsigned g_want_port;

const char *
tile_state_name(int state) {
  switch(state) {
  case TILE_WORKING:     return "working";
  case TILE_PRESENT:     return "present";
  case TILE_CREATED:     return "created";
  case TILE_FAILED:      return "failed";
  case TILE_UNAVAILABLE: return "unavailable";
  default:               return "off";
  }
}

int
tile_status(unsigned *code, unsigned *port, unsigned *want) {
  if(code) *code = g_code;
  if(port) *port = g_tile_port;
  if(want) *want = g_want_port;
  return g_state;
}

static int
is_dir(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* The port in our own param.json, 0 if it cannot be read. */
static unsigned
port_in_param(void) {
  char buf[1024];
  int fd = open(TILE_PARAM, O_RDONLY);
  if(fd < 0) return 0;
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0) return 0;
  buf[n] = '\0';
  const char *p = strstr(buf, "127.0.0.1:");
  return p ? (unsigned)strtoul(p + 10, NULL, 10) : 0;
}

/* Does the system's app database name our id? A plain byte search, read
   only: enough to tell "registered" from "never registered" without SQLite.
   1 = yes, 0 = no, -1 = the database cannot be read. */
static int
in_app_db(void) {
  int fd = open(APP_DB, O_RDONLY);
  if(fd < 0) return -1;
  static char blk[65536 + 16];
  const size_t idlen = sizeof(TILE_ID) - 1;
  size_t keep = 0;
  int found = 0;
  ssize_t n;
  while(!found && (n = read(fd, blk + keep, 65536)) > 0) {
    size_t len = keep + (size_t)n;
    for(size_t i = 0; i + idlen <= len; i++)
      if(blk[i] == 'F' && !memcmp(blk + i, TILE_ID, idlen)) { found = 1; break; }
    keep = len < idlen - 1 ? len : idlen - 1;
    memmove(blk, blk + len - keep, keep);
  }
  close(fd);
  return found;
}

static int
registered(void) {
  if(exists(TILE_STAGING)) return 0;
  if(is_dir(TILE_META) || is_dir(TILE_META_SYS)) return 1;
  return exists(TILE_PARAM) && in_app_db() == 1;
}

/* Writes through a temporary name, so a cut-off write never looks finished. */
static int
write_file(const char *path, const void *data, size_t len) {
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s.part", path);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return -1;
  const unsigned char *p = data;
  size_t off = 0;
  while(off < len) {
    ssize_t w = write(fd, p + off, len - off);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) { close(fd); unlink(tmp); return -1; }
    off += (size_t)w;
  }
  if(close(fd) != 0 || rename(tmp, path) != 0) { unlink(tmp); return -1; }
  return 0;
}

/* The param.json this run wants: media row, deeplink to the web UI port. */
static int
param_text(char *buf, size_t n, unsigned port) {
  return snprintf(buf, n,
                  "{\n"
                  "  \"titleId\": \"" TILE_ID "\",\n"
                  "  \"applicationCategoryType\": 65536,\n"
                  "  \"deeplinkUri\": \"http://127.0.0.1:%u\",\n"
                  "  \"localizedParameters\": {\n"
                  "    \"defaultLanguage\": \"en-US\",\n"
                  "    \"en-US\": { \"titleName\": \"" PS5TD_NAME "\" }\n"
                  "  }\n"
                  "}\n", port);
}

/* Is the param.json on disk the one this run would write? */
static int
param_current(unsigned port) {
  char want[512], have[600];
  int wn = param_text(want, sizeof(want), port);
  int fd = open(TILE_PARAM, O_RDONLY);
  if(fd < 0) return 0;
  ssize_t hn = read(fd, have, sizeof(have));
  close(fd);
  return hn == wn && memcmp(want, have, (size_t)wn) == 0;
}

/* Writes /user/app/<id>/sce_sys/{param.json,icon0.png}. 0 or errno. */
static int
stage(unsigned port) {
  char param[512];
  int n = param_text(param, sizeof(param), port);
  int fd = open(TILE_STAGING, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return errno;
  close(fd);
  if(util_mkdir(TILE_DIR) != 0 || util_mkdir(TILE_SCE_SYS) != 0) return errno ? errno : EIO;
  if(write_file(TILE_PARAM, param, (size_t)n) != 0) return errno;
  if(write_file(TILE_SCE_SYS "/icon0.png", icon0_png, icon0_png_len) != 0) return errno;
  return 0;
}

static title_dir_fn
find_title_dir(void) {
  if(sceAppInstUtilAppInstallTitleDir) return sceAppInstUtilAppInstallTitleDir;
  uint32_t handle = 0;
  if(kernel_dynlib_handle(getpid(), "libSceAppInstUtil.sprx", &handle) == 0) {
    intptr_t p = kernel_dynlib_resolve(getpid(), handle, NID_TITLE_DIR);
    if(p) return (title_dir_fn)p;
  }
  return NULL;
}

static void *
tile_thread(void *arg) {
  unsigned port = (unsigned)(uintptr_t)arg;

  int renew = 0;
  if(registered()) {
    g_tile_port = port_in_param();
    if(param_current(port)) {
      g_state = TILE_PRESENT;
      LOG("Home-screen tile is there (%s).", "Ana ekran kutucuğu mevcut (%s).", TILE_ID);
      return NULL;
    }
    renew = 1;
    LOG("Home-screen tile is out of date (older version or another port); renewing it.",
        "Ana ekran kutucuğu güncel değil (eski sürüm ya da başka port); yenileniyor.");
  }

  title_dir_fn install = find_title_dir();
  if(!sceAppInstUtilInitialize || !install) {
    g_state = TILE_UNAVAILABLE;
    LOG("Home-screen tile: this firmware does not offer the install call; skipped.",
        "Ana ekran kutucuğu: bu firmware kurulum çağrısını sunmuyor; atlandı.");
    return NULL;
  }

  int rc = sceAppInstUtilInitialize();
  if(rc != 0) {
    g_code = (unsigned)rc;
    g_state = TILE_FAILED;
    LOG("Home-screen tile: sceAppInstUtilInitialize 0x%08X.", "Ana ekran kutucuğu: sceAppInstUtilInitialize 0x%08X.",
        (unsigned)rc);
    return NULL;
  }

  int err = stage(port);
  if(err != 0) {
    g_code = (unsigned)err;
    g_state = TILE_FAILED;
    LOG("Home-screen tile: files could not be written to %s (%s).",
        "Ana ekran kutucuğu: dosyalar %s altına yazılamadı (%s).", TILE_SCE_SYS, strerror(err));
    return NULL;
  }

  rc = install(TILE_ID, TILE_APP_ROOT, NULL);
  g_code = (unsigned)rc;
  if(renew && (rc == 0 || (unsigned)rc == INST_ALREADY)) {
    unlink(TILE_STAGING);
    g_tile_port = port;
    g_state = TILE_PRESENT;
    LOG("Home-screen tile renewed (%s, 0x%08X); it opens http://127.0.0.1:%u.",
        "Ana ekran kutucuğu yenilendi (%s, 0x%08X); http://127.0.0.1:%u adresini açar.", TILE_ID, (unsigned)rc, port);
  } else if((unsigned)rc == INST_ALREADY) {
    /* Registered, just not where we looked: no notification. */
    unlink(TILE_STAGING);
    g_tile_port = port;
    g_state = TILE_PRESENT;
    LOG("Home-screen tile was already registered (%s).", "Ana ekran kutucuğu zaten kayıtlıydı (%s).", TILE_ID);
  } else if(rc == 0) {
    unlink(TILE_STAGING);
    g_tile_port = port;
    g_state = TILE_CREATED;
    LOG("Home-screen tile added (%s); it opens http://127.0.0.1:%u.",
        "Ana ekran kutucuğu eklendi (%s); http://127.0.0.1:%u adresini açar.", TILE_ID, port);
    plat_notify(TX_NOTIFY_TILE(i18n_get()), PS5TD_NAME);
  } else {
    g_state = TILE_FAILED;
    LOG("Home-screen tile: the system refused it (0x%08X). It is tried again at the next start.",
        "Ana ekran kutucuğu: sistem kabul etmedi (0x%08X). Bir sonraki açılışta yeniden denenir.", (unsigned)rc);
  }
  return NULL;
}

void
tile_start(unsigned port) {
  g_want_port = port;
  g_state = TILE_WORKING;
  pthread_t t;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&t, &attr, tile_thread, (void *)(uintptr_t)port) != 0) {
    g_state = TILE_FAILED;
    LOG("Home-screen tile: no thread.", "Ana ekran kutucuğu: iş parçacığı yok.");
  }
  pthread_attr_destroy(&attr);
}
