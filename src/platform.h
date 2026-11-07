/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything PS5-specific sits behind this interface: sensors, fan, power,
 * the /dev/dce flip counter, game detection, notifications, privileges.
 */
#ifndef PS5TD_PLATFORM_H
#define PS5TD_PLATFORM_H

#include "ps5td.h"

int  plat_init(const config_t *c);

/* Resolves config "lang" (auto follows the PS5 system language) and makes it
   the active console language. */
void plat_apply_lang(int cfg_lang);

/* Fast, lock-free reads: temperature, fan, power, FPS (when a game is up). */
void plat_read(sample_t *out, int game_in_front, int fps_enabled);

/* Game detection. Talks to Sony services and may block, so it is only called
   from the probe thread. 1 = a game is in front; title is filled ("-" if none). */
int  plat_game_probe(char *title, size_t n);

/* Pass LNG("english", "türkçe") as the format to follow the console language. */
void plat_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

uint32_t plat_fw(void);
void plat_fw_text(char *out, size_t n);
int  plat_local_ip(char *out, size_t n);

/* Diagnostic text in the given language: available symbols, dce state. */
const char *plat_symbols_text(int lang);
const char *plat_dce_text(int lang);

#endif
