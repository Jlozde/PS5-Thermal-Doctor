/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PS5 hardware access.
 *
 * Sources (facts only; the code was written from scratch):
 *  - etaHEN / OnionHEN: sceKernelGetCpuTemperature, sceKernelGetSocSensorTemperature,
 *    sceKernelGetCurrentFanDuty, /dev/dce ioctl 0x80308217 (PHU Games Tools).
 *  - ps5-cooling-system-center-pro: behaviour of the SoC channels (channel 0 is
 *    damped, the hottest of 1..7 is the chip), the 0..1024 fan scale, the power
 *    call writing 0x70 bytes and needing a 256-byte buffer, privilege
 *    escalation steps, weak symbols.
 *  - ps5-fan-control: notification struct layout (45 + 3075 bytes).
 */
#include "platform.h"
#include "i18n.h"
#include "log.h"
#include "util.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ps5/kernel.h>

/* libkernel_sys / SceSystemService exports. Weak, so that a symbol missing on
   some firmware does not kill the payload before main(); only that reading is
   lost. */
int sceKernelGetCpuTemperature(int *out_celsius)                    __attribute__((weak));
int sceKernelGetSocSensorTemperature(int channel, int *out_c)       __attribute__((weak));
int sceKernelGetCurrentFanDuty(uint16_t *out_duty, void *scratch)   __attribute__((weak));
int sceKernelGetSocPowerConsumption(void *out)                      __attribute__((weak));
int sceSystemServiceGetAppIdOfRunningBigApp(void)                   __attribute__((weak));
int sceSystemServiceParamGetInt(int param_id, int *value)           __attribute__((weak));

#define SCE_PARAM_ID_LANG   1    /* SCE_SYSTEM_SERVICE_PARAM_ID_LANG */
#define SCE_LANG_TURKISH    19   /* SCE_SYSTEM_PARAM_LANG_TURKISH */

typedef struct {
  char useless1[45];
  char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

/* Auth id that may open /dev/icc_fan; measured by ps5-cooling-system-center-pro. */
#define AUTHID_FAN 0x4801000000000013ULL

static char g_symbols[LANG_COUNT][256];
static char g_dce_text[LANG_COUNT][128] = { "not tried yet", "henüz denenmedi" };

static void set_dce_text(const char *en, const char *tr, ...)
  __attribute__((format(printf, 1, 3), format(printf, 2, 3)));
static void
set_dce_text(const char *en, const char *tr, ...) {
  va_list ap, ap2;
  va_start(ap, tr);
  va_copy(ap2, ap);
  vsnprintf(g_dce_text[LANG_EN], sizeof(g_dce_text[LANG_EN]), en, ap);
  vsnprintf(g_dce_text[LANG_TR], sizeof(g_dce_text[LANG_TR]), tr, ap2);
  va_end(ap2);
  va_end(ap);
}

/* ---------------------------------------------------------------- privileges */

static int
escalate(void) {
  pid_t pid = getpid();
  if(!kernel_get_proc(pid)) return -1;
  int rc = 0;
  if(kernel_set_ucred_uid(pid, 0) != 0)   rc = -1;
  if(kernel_set_ucred_ruid(pid, 0) != 0)  rc = -1;
  if(kernel_set_ucred_svuid(pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_rgid(pid, 0) != 0)  rc = -1;
  if(kernel_set_ucred_svgid(pid, 0) != 0) rc = -1;
  intptr_t rootvnode = kernel_get_root_vnode();
  if(rootvnode) {
    if(kernel_set_proc_rootdir(pid, rootvnode) != 0) rc = -1;
    if(kernel_set_proc_jaildir(pid, rootvnode) != 0) rc = -1;
  }
  if(kernel_set_ucred_authid(pid, AUTHID_FAN) != 0) rc = -1;
  uint8_t caps[16];
  memset(caps, 0xff, sizeof(caps));
  if(kernel_set_ucred_caps(pid, caps) != 0) rc = -1;
  uint8_t attrs[32];
  memset(attrs, 0, sizeof(attrs));
  attrs[0] = 0x80;
  if(kernel_set_ucred_attrs(pid, attrs) != 0) rc = -1;
  return rc;
}

/* ---------------------------------------------------------------- language */

static int g_sys_lang_raw = -2;   /* -2 = not asked yet, -1 = unavailable */

void
plat_apply_lang(int cfg_lang) {
  int lang = cfg_lang;
  if(lang != LANG_EN && lang != LANG_TR) {
    if(g_sys_lang_raw == -2) {
      int v = -1;
      if(!sceSystemServiceParamGetInt || sceSystemServiceParamGetInt(SCE_PARAM_ID_LANG, &v) != 0) v = -1;
      g_sys_lang_raw = v;
    }
    lang = g_sys_lang_raw == SCE_LANG_TURKISH ? LANG_TR : LANG_EN;
  }
  i18n_set(lang);
}

/* ---------------------------------------------------------------- start-up */

int
plat_init(const config_t *c) {
  const char *yes[LANG_COUNT] = { "ok", "var" }, *no[LANG_COUNT] = { "MISSING", "YOK" };
  for(int l = 0; l < LANG_COUNT; l++)
    snprintf(g_symbols[l], sizeof(g_symbols[l]),
             "CpuTemp:%s SocSensor:%s FanDuty:%s SocPower:%s BigApp:%s SysLang:%s",
             sceKernelGetCpuTemperature ? yes[l] : no[l],
             sceKernelGetSocSensorTemperature ? yes[l] : no[l],
             sceKernelGetCurrentFanDuty ? yes[l] : no[l],
             sceKernelGetSocPowerConsumption ? yes[l] : no[l],
             sceSystemServiceGetAppIdOfRunningBigApp ? yes[l] : no[l],
             sceSystemServiceParamGetInt ? yes[l] : no[l]);
  LOG("Symbols: %s%.0s", "Semboller: %.0s%s", g_symbols[LANG_EN], g_symbols[LANG_TR]);
  LOG("Language: %s (config %s, PS5 system language code %d).",
      "Dil: %s (ayar %s, PS5 sistem dili kodu %d).",
      i18n_code(i18n_get()), i18n_code(c->lang), g_sys_lang_raw);

  if(c->escalate) {
    if(escalate() == 0)
      LOG("Privilege escalation done (pid %d).", "Yetki yükseltme tamam (pid %d).", (int)getpid());
    else
      LOG("Privilege escalation partly failed; /dev/dce may be unavailable.",
          "Yetki yükseltme kısmen başarısız; /dev/dce erişimi olmayabilir.");
  }
  return 0;
}

/* ---------------------------------------------------------------- sensors */

static void
read_temps(sample_t *s) {
  int v = -1;
  s->cpu_c = -1;
  if(sceKernelGetCpuTemperature && sceKernelGetCpuTemperature(&v) == 0 && v > 0 && v <= 130)
    s->cpu_c = (int16_t)v;

  int hottest = -1;
  for(int ch = 0; ch < PS5TD_SOC_CHANNELS; ch++) {
    int t = -1;
    s->soc_raw[ch] = -1;
    if(sceKernelGetSocSensorTemperature &&
       sceKernelGetSocSensorTemperature(ch, &t) == 0 && t > 0 && t <= 130) {
      s->soc_raw[ch] = (int16_t)t;
      /* Channel 0 is left out: it gave damped, lagging readings. */
      if(ch >= 1 && t > hottest) hottest = t;
    }
  }
  s->soc_c = (int16_t)hottest;
  s->gpu_c = s->soc_raw[7];
  /* If channels 1..7 never answered, channel 0 is better than nothing. */
  if(s->soc_c < 0 && s->soc_raw[0] > 0) s->soc_c = s->soc_raw[0];
}

static void
read_fan(sample_t *s) {
  s->fan_raw = -1;
  s->fan_pct10 = -1;
  if(!sceKernelGetCurrentFanDuty) return;
  uint16_t duty = 0;
  uint8_t scratch[64] = {0};
  if(sceKernelGetCurrentFanDuty(&duty, scratch) != 0) return;
  if(duty > 1024) return;                 /* 0xFFFF right after rest mode */
  s->fan_raw = (int16_t)duty;
  int pct10 = ((int)duty * 1000 + 512) / 1024;
  if(pct10 > 1000) pct10 = 1000;
  s->fan_pct10 = (int16_t)pct10;
}

/* The CPU rail may not be a live reading. Two forms have been seen:
   - a fixed budget value;
   - a peak-hold counter (FW 11.60, 2026-11-07 session): it resets when a game
     starts, then only rises (23 increases, 0 decreases in 28 min) and stays at
     123.7 W even after the game has closed.
   "It changes" is not enough; to count as live it must have risen at least
   twice and fallen at least twice within the last 60 s. A single reset drop
   does not qualify. */
static uint32_t g_cpu_prev_mw;
static int      g_cpu_seen;
static uint64_t g_cpu_up_ms[2], g_cpu_dn_ms[2];

static int
cpu_rails_live(uint32_t cpu_mw, uint64_t now) {
  if(!g_cpu_seen) { g_cpu_seen = 1; g_cpu_prev_mw = cpu_mw; return 0; }
  if(cpu_mw > g_cpu_prev_mw) { g_cpu_up_ms[1] = g_cpu_up_ms[0]; g_cpu_up_ms[0] = now; }
  else if(cpu_mw < g_cpu_prev_mw) { g_cpu_dn_ms[1] = g_cpu_dn_ms[0]; g_cpu_dn_ms[0] = now; }
  g_cpu_prev_mw = cpu_mw;
  return g_cpu_up_ms[1] && g_cpu_dn_ms[1] &&
         now - g_cpu_up_ms[1] < 60000ULL && now - g_cpu_dn_ms[1] < 60000ULL;
}

static void
read_power(sample_t *s) {
  s->power_dw = s->gpu_w10 = s->cpu_w10 = s->mem_w10 = -1;
  s->cpu_live = 0;
  if(!sceKernelGetSocPowerConsumption) return;
  uint32_t buf[64];
  memset(buf, 0, sizeof(buf));           /* the call writes 0x70 bytes; keep the buffer roomy */
  if(sceKernelGetSocPowerConsumption(buf) != 0) return;

  uint32_t mw[8];
  uint64_t total = 0; int any = 0;
  for(int r = 0; r < 8; r++) {
    uint32_t w = buf[r * 3], mv = buf[r * 3 + 1], ma = buf[r * 3 + 2];
    if(w > 400000u || mv > 2500u || ma > 400000u) return;   /* unexpected layout */
    mw[r] = w; total += w; any |= (w | mv | ma) != 0;
  }
  if(!any || total == 0 || total > 500000u) return;

  uint32_t gpu = mw[0] + mw[1], cpu = mw[2] + mw[3], mem = mw[4] + mw[5] + mw[6] + mw[7];
  s->gpu_w10 = (int16_t)(gpu / 100);
  s->cpu_w10 = (int16_t)(cpu / 100);
  s->mem_w10 = (int16_t)(mem / 100);
  s->cpu_live = (uint8_t)cpu_rails_live(cpu, util_mono_ms());
  uint32_t live = gpu + mem + (s->cpu_live ? cpu : 0);
  s->power_dw = (int16_t)(live / 100);
}

/* ---------------------------------------------------------------- /dev/dce */

typedef struct {
  uint64_t selector;
  uint64_t mask;
  uint64_t output;
  uint64_t reserved[3];
} dce_arg_t;
_Static_assert(sizeof(dce_arg_t) == 0x30, "the dce ioctl expects 0x30 bytes");

#define DCE_IOCTL     0x0000000080308217UL
#define DCE_IOCTL_SX  0xFFFFFFFF80308217UL

static int      g_dce_fd = -1;
static int      g_dce_refused;
static int      g_dce_sx;
static int      g_dce_ok_logged;
static uint64_t g_flips_prev, g_flips_prev_ms;
static int      g_flips_have;
static float    g_fps = -1.0f;

static void
dce_close(void) {
  if(g_dce_fd >= 0) close(g_dce_fd);
  g_dce_fd = -1;
}

static int
dce_read(uint64_t *flips) {
  if(g_dce_refused) return -1;
  if(g_dce_fd < 0) {
    g_dce_fd = open("/dev/dce", O_RDWR);
    if(g_dce_fd < 0) {
      int e = errno;
      set_dce_text("cannot open /dev/dce (errno %d: %s)", "/dev/dce açılamadı (errno %d: %s)", e, strerror(e));
      LOG("%s%.0s", "%.0s%s", g_dce_text[LANG_EN], g_dce_text[LANG_TR]);
      if(e == EPERM || e == EACCES || e == ENOENT) g_dce_refused = 1;
      return -1;
    }
  }
  uint8_t out[0x60];
  for(int attempt = 0; attempt < 2; attempt++) {
    dce_arg_t arg;
    memset(out, 0, sizeof(out));
    memset(&arg, 0, sizeof(arg));
    arg.selector = 0x10000000AULL;
    arg.mask     = 0x8000000000ULL;
    arg.output   = (uint64_t)(uintptr_t)out;
    if(ioctl(g_dce_fd, g_dce_sx ? DCE_IOCTL_SX : DCE_IOCTL, &arg) == 0) {
      memcpy(flips, out + 8, sizeof(*flips));
      if(!g_dce_ok_logged) {
        g_dce_ok_logged = 1;
        if(g_dce_sx) set_dce_text("reading the flip counter (sign-extended request)",
                                  "flip sayacı okunuyor (işaret genişletilmiş istek)");
        else         set_dce_text("reading the flip counter", "flip sayacı okunuyor");
        LOG("/dev/dce: %s%.0s", "/dev/dce: %.0s%s", g_dce_text[LANG_EN], g_dce_text[LANG_TR]);
      }
      return 0;
    }
    int e = errno;
    if(e == EINVAL && !g_dce_sx) { g_dce_sx = 1; continue; }
    set_dce_text("ioctl rejected (errno %d: %s)", "ioctl reddedildi (errno %d: %s)", e, strerror(e));
    g_dce_ok_logged = 0;
    if(e == EBADF) dce_close();
    return -1;
  }
  return -1;
}

static void
read_fps(sample_t *s, int game_in_front, int enabled) {
  s->fps10 = -1;
  if(!enabled || !game_in_front) {
    /* Without a game the driver returns ENOENT and floods the kernel log; do not ask. */
    if(!game_in_front) { g_flips_have = 0; g_fps = -1.0f; }
    return;
  }
  uint64_t flips = 0;
  uint64_t now = util_mono_ms();
  if(dce_read(&flips) != 0) { g_flips_have = 0; return; }
  if(!g_flips_have || flips < g_flips_prev || now <= g_flips_prev_ms) {
    g_flips_prev = flips; g_flips_prev_ms = now; g_flips_have = 1;
    return;
  }
  if(now - g_flips_prev_ms >= 500) {
    double fps = (double)(flips - g_flips_prev) * 1000.0 / (double)(now - g_flips_prev_ms);
    g_fps = (fps >= 0.0 && fps <= 1000.0) ? (float)fps : -1.0f;
    g_flips_prev = flips; g_flips_prev_ms = now;
  }
  if(g_fps >= 0.0f) s->fps10 = (int16_t)(g_fps * 10.0f + 0.5f);
}

void
plat_read(sample_t *s, int game_in_front, int fps_enabled) {
  memset(s, 0, sizeof(*s));
  s->wall_ms = util_wall_ms();
  s->mono_s  = util_mono_s();
  s->game    = (uint8_t)(game_in_front ? 1 : 0);
  read_temps(s);
  read_fan(s);
  read_power(s);
  read_fps(s, game_in_front, fps_enabled);
}

/* ---------------------------------------------------------------- game */

typedef int (*lnc_title_fn)(uint32_t appid, char *title);
static lnc_title_fn g_lnc_title;
static int g_lnc_resolved;

/* Fallback: the running game's PFS image stays mounted at
   /mnt/sandbox/pfsmnt/<TITLE>-app0, found with a plain opendir, no Sony service. */
static int
title_from_pfsmnt(char *out, size_t n) {
  DIR *d = opendir("/mnt/sandbox/pfsmnt");
  if(!d) return 0;
  int found = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    size_t len = strlen(e->d_name);
    if(len > 5 && strcmp(e->d_name + len - 5, "-app0") == 0 && len - 5 < n) {
      memcpy(out, e->d_name, len - 5);
      out[len - 5] = '\0';
      found = 1;
      break;
    }
  }
  closedir(d);
  return found;
}

int
plat_game_probe(char *title, size_t n) {
  snprintf(title, n, "-");
  int appid = -1;
  if(sceSystemServiceGetAppIdOfRunningBigApp) appid = sceSystemServiceGetAppIdOfRunningBigApp();

  if(appid > 0) {
    if(!g_lnc_resolved) {
      g_lnc_resolved = 1;
      g_lnc_title = (lnc_title_fn)dlsym(RTLD_DEFAULT, "sceLncUtilGetAppTitleId");
      if(g_lnc_title) LOG("sceLncUtilGetAppTitleId: found", "sceLncUtilGetAppTitleId: bulundu");
      else LOG("sceLncUtilGetAppTitleId: not found, using the pfsmnt scan",
               "sceLncUtilGetAppTitleId: yok, pfsmnt taraması kullanılacak");
    }
    char buf[32] = {0};
    if(g_lnc_title && g_lnc_title((uint32_t)appid, buf) == 0 && buf[0] && util_safe_name(buf)) {
      snprintf(title, n, "%.15s", buf);
      return 1;
    }
    if(!title_from_pfsmnt(title, n)) snprintf(title, n, "APP%d", appid);
    return 1;
  }
  if(appid == 0) return 0;

  /* No service or an error: if a game is mounted under pfsmnt, assume a game. */
  return title_from_pfsmnt(title, n) ? 1 : 0;
}

/* ---------------------------------------------------------------- notifications */

void
plat_notify(const char *fmt, ...) {
  notify_request_t req;
  memset(&req, 0, sizeof(req));
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ---------------------------------------------------------------- misc */

uint32_t
plat_fw(void) {
  return (uint32_t)kernel_get_fw_version();
}

void
plat_fw_text(char *out, size_t n) {
  uint32_t fw = plat_fw();
  unsigned major = (fw >> 24) & 0xff, minor = (fw >> 16) & 0xff;
  major = ((major >> 4) & 0xf) * 10 + (major & 0xf);
  minor = ((minor >> 4) & 0xf) * 10 + (minor & 0xf);
  if(!major && !minor) snprintf(out, n, "?");
  else snprintf(out, n, "%u.%02u", major, minor);
}

/* The classic way to learn the local address without sending a packet:
   "connect" a UDP socket to an outside address and read the source address
   the kernel picked. */
int
plat_local_ip(char *out, size_t n) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0) return -1;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  a.sin_addr.s_addr = inet_addr("8.8.8.8");
  int rc = -1;
  if(connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    if(getsockname(fd, (struct sockaddr *)&local, &len) == 0 &&
       inet_ntop(AF_INET, &local.sin_addr, out, (socklen_t)n))
      rc = 0;
  }
  close(fd);
  return rc;
}

const char *plat_symbols_text(int lang) { return g_symbols[lang == LANG_TR ? LANG_TR : LANG_EN]; }
const char *plat_dce_text(int lang)     { return g_dce_text[lang == LANG_TR ? LANG_TR : LANG_EN]; }
