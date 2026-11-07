/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PS5 Thermal Doctor: shared types and constants.
 *
 * Goal: on a jailbroken PS5, record temperature, fan, power and FPS while a
 * game runs and answer "does this console need maintenance?".
 */
#ifndef PS5TD_H
#define PS5TD_H

#include <stddef.h>
#include <stdint.h>

#define PS5TD_NAME          "PS5 Thermal Doctor"
#define PS5TD_VERSION       "0.2.0"
#define PS5TD_PROCESS_NAME  "ps5td.elf"

#define PS5TD_DATA_DIR      "/data/ps5td"
#define PS5TD_SESSION_DIR   PS5TD_DATA_DIR "/sessions"
#define PS5TD_CONFIG_PATH   PS5TD_DATA_DIR "/config.ini"
#define PS5TD_STATE_PATH    PS5TD_DATA_DIR "/state.ini"
#define PS5TD_HISTORY_PATH  PS5TD_DATA_DIR "/history.csv"
#define PS5TD_LOG_PATH      PS5TD_DATA_DIR "/ps5td.log"

#define PS5TD_SOC_CHANNELS  8
#define PS5TD_TITLE_LEN     16
#define PS5TD_RING_SECONDS  (2 * 3600)   /* live data kept in memory: 2 hours */
#define PS5TD_HISTORY_MAX   400          /* session summaries kept */

/* One measurement (once per second). -1 = value could not be read this time. */
typedef struct {
  uint64_t wall_ms;                    /* wall clock, ms (UTC)              */
  uint32_t mono_s;                     /* monotonic clock, s                */
  int16_t  cpu_c;                      /* CPU sensor, °C                    */
  int16_t  soc_c;                      /* hottest of SoC channels 1..7, °C  */
  int16_t  gpu_c;                      /* SoC channel 7 (closest to GPU)    */
  int16_t  soc_raw[PS5TD_SOC_CHANNELS];
  int16_t  fan_raw;                    /* 0..1024 (10-bit PWM)              */
  int16_t  fan_pct10;                  /* percent x 10                      */
  int16_t  power_dw;                   /* total power, W x 10               */
  int16_t  gpu_w10, cpu_w10, mem_w10;  /* rail groups, W x 10               */
  int16_t  fps10;                      /* display flip rate x 10            */
  uint8_t  game;                       /* 1 = a game is in the foreground   */
  uint8_t  cpu_live;                   /* 1 = CPU rail is a live reading    */
} sample_t;

/* Verdict levels */
enum {
  VERDICT_NA      = -1,   /* not enough data                                 */
  VERDICT_OK      = 0,    /* normal                                          */
  VERDICT_WATCH   = 1,    /* slight deviation, keep watching                 */
  VERDICT_CLEAN   = 2,    /* dust cleaning recommended                       */
  VERDICT_SERVICE = 3     /* cooling insufficient: cleaning + thermal service */
};

/* Settings (config.ini) */
typedef struct {
  int      lang;              /* LANG_AUTO / LANG_EN / LANG_TR (i18n.h)       */
  unsigned http_port;
  unsigned sample_ms;
  unsigned csv_every_s;
  int      fps_enabled;
  int      escalate;
  int      notify_startup;
  int      notify_session;
  int      notify_live_warn;
  unsigned warmup_s;          /* warm-up skipped at session start            */
  unsigned min_ss_s;          /* minimum steady-state time for a verdict     */
  unsigned min_session_s;     /* shorter sessions are not recorded           */
  float    k_fan;             /* CDI: °C equivalent per fan percent          */
  float    k_pow;             /* CDI: °C equivalent per watt                 */
  float    watch_drift, clean_drift, service_drift;      /* °C vs. baseline */
  float    abs_watch_fan,   abs_watch_soc;
  float    abs_clean_fan,   abs_clean_soc;
  float    abs_service_fan, abs_service_soc;
  float    hot_soc_c;         /* SoC temperature that is dangerous by itself */
  unsigned baseline_sessions; /* sessions that form the baseline             */
  unsigned recent_sessions;   /* sessions averaged for "now"                 */
  unsigned keep_sessions;     /* session CSV files kept on disk              */
} config_t;

/* Summary of one game session (a history.csv line) */
typedef struct {
  uint64_t start_ms;
  uint32_t epoch;             /* baseline counter: increases after cleaning  */
  char     title[PS5TD_TITLE_LEN];
  uint32_t dur_s;
  int      ss_valid;          /* a steady-state measurement exists           */
  uint32_t ss_samples;
  float    ss_soc, ss_cpu, ss_fan, ss_power, ss_fps;
  int      power_valid, fps_valid;
  float    max_soc, max_cpu, max_fan;
  float    warm_rate;         /* warm-up rate over the first 5 min, °C/min (-1000 = none) */
  float    hot_share;         /* share of time with fan >= 80% and SoC >= 80°C, 0..1 */
  uint32_t danger_s;          /* seconds spent at SoC >= hot_soc_c           */
  float    cdi;               /* cooling demand index, °C equivalent (-1000 = none) */
  int      verdict;
} session_summary_t;

#endif
