/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PS5 Thermal Doctor: entry point.
 *
 * Lessons learned (from ps5-cooling-system-center-pro):
 *  - SIGPIPE/SIGHUP are ignored first thing: writing to stdout after the
 *    socket that sent the payload has closed must not kill the process.
 *  - No fork()/setsid(): they panic the kernel in a borrowed process context.
 *  - Calls into Sony services (game detection) run on a separate thread.
 */
#include "config.h"
#include "http.h"
#include "i18n.h"
#include "texts.h"
#include "log.h"
#include "platform.h"
#include "probe.h"
#include "ps5td.h"
#include "sampler.h"
#include "util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

/* Returns the pid of an older ps5td on the same port (0 = something else, -1 = nobody). */
static int
sibling_pid(unsigned port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  struct timeval tv = { 1, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int pid = -1;
  if(connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
    const char *req = "GET /api/status HTTP/1.0\r\n\r\n";
    pid = 0;
    if(write(fd, req, strlen(req)) > 0) {
      static char buf[16384];
      size_t len = 0;
      ssize_t n;
      while(len < sizeof(buf) - 1 && (n = read(fd, buf + len, sizeof(buf) - 1 - len)) > 0) len += (size_t)n;
      buf[len] = '\0';
      if(strstr(buf, "\"app\":\"ps5td\"")) {
        const char *p = strstr(buf, "\"pid\":");
        if(p) pid = atoi(p + 6);
      }
    }
  }
  close(fd);
  return pid;
}

static void
takeover(unsigned port) {
  int pid = sibling_pid(port);
  if(pid > 1 && pid != (int)getpid()) {
    if(kill(pid, SIGKILL) == 0) {
      LOG("Stopped the older ps5td instance (pid %d); this one takes over.",
          "Eski ps5td örneği (pid %d) durduruldu; bu örnek devralıyor.", pid);
      sleep(2);
    } else {
      LOG("Could not stop the older ps5td instance (pid %d).", "Eski ps5td örneği (pid %d) durdurulamadı.", pid);
    }
  }
}

int
main(int argc, char **argv) {
  (void)argc; (void)argv;

#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif
#ifdef SIGHUP
  signal(SIGHUP, SIG_IGN);
#endif
#ifdef SIGCHLD
  signal(SIGCHLD, SIG_IGN);
#endif
  setvbuf(stdout, NULL, _IOLBF, 0);

  printf("\n  %s %s\n  Cooling diagnostics: temperature · fan · power · FPS\n"
         "  Soğutma teşhisi: sıcaklık · fan · güç · FPS\n\n", PS5TD_NAME, PS5TD_VERSION);
#ifdef SYS_thr_set_name
  /* Sets the process name on PS5 (".elf" suffix for payload manager lists). */
  syscall(SYS_thr_set_name, -1, PS5TD_PROCESS_NAME);
#endif

  util_mkdir(PS5TD_DATA_DIR);
  util_mkdir(PS5TD_SESSION_DIR);
  log_init(PS5TD_LOG_PATH);

  /* Read the language first so that every following line uses it. */
  config_t cfg;
  config_defaults(&cfg);
  config_load(&cfg, PS5TD_CONFIG_PATH);
  plat_apply_lang(cfg.lang);

  LOG("%s %s starting (pid %d).", "%s %s başlıyor (pid %d).", PS5TD_NAME, PS5TD_VERSION, (int)getpid());
  plat_init(&cfg);

  char fw[16];
  plat_fw_text(fw, sizeof(fw));
  LOG("Firmware %s.", "Firmware %s.", fw);

  takeover(cfg.http_port);

  if(sampler_start() != 0) LOG("WARNING: the sampler could not start.", "UYARI: örnekleyici başlatılamadı.");
  probe_start();

  char ip[48] = "?";
  plat_local_ip(ip, sizeof(ip));

  int announced = 0;
  for(;;) {
    sampler_get_config(&cfg);
    int srv = http_bind(cfg.http_port);
    if(srv == -2) {
      LOG("Port %u is busy; looking for an older instance.", "Port %u dolu; eski örnek aranıyor.", cfg.http_port);
      takeover(cfg.http_port);
      sleep(3);
      continue;
    }
    if(srv < 0) {
      LOG("Cannot open port %u; retrying in 5 s.", "Port %u açılamadı; 5 sn sonra yeniden.", cfg.http_port);
      sleep(5);
      continue;
    }
    if(!announced) {
      announced = 1;
      LOG("Web UI: http://%s:%u", "Web arayüzü: http://%s:%u", ip, cfg.http_port);
      if(cfg.notify_startup)
        plat_notify(TX_NOTIFY_START(i18n_get()),
                    PS5TD_NAME, PS5TD_VERSION, ip, cfg.http_port);
    }
    http_run(srv);   /* does not return */
  }
  return 0;
}
