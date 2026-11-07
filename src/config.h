/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TD_CONFIG_H
#define PS5TD_CONFIG_H

#include "ps5td.h"

void config_defaults(config_t *c);

/* Applies INI text to c (unknown keys are skipped). Returns the number of bad lines. */
int  config_apply_text(config_t *c, const char *text);

/* Loads from a file; if the file does not exist the defaults stay and -1 is returned. */
int  config_load(config_t *c, const char *path);

/* Produces commented (bilingual) INI text. */
size_t config_to_text(const config_t *c, char *out, size_t n);

int  config_save(const config_t *c, const char *path);

/* Pulls unreasonable values into a safe range. */
void config_sanitize(config_t *c);

#endif
