/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two-language support (English / Turkish).
 *
 * Every user-facing string exists in both languages next to each other in the
 * source, so a missing translation is impossible by construction:
 *
 *     LOG("Session started: %s", "Oturum başladı: %s", title);
 *     snprintf(buf, n, LNGX(lang, "Fan %.0f%%", "Fan %%%.0f"), fan);
 *
 * Both format strings of a pair must take the same arguments in the same
 * order; the compiler checks both branches. To pass text that itself exists
 * in two languages, give both and let each format skip the other one:
 *
 *     LOG("Ended (%s%.0s)", "Bitti (%.0s%s)", why_en, why_tr);
 *
 * The console-wide language (notifications, stdout, log file) comes from
 * config.ini "lang" (auto / en / tr); "auto" follows the PS5 system language.
 * The web API returns text in the language the browser asks for (?lang=).
 */
#ifndef PS5TD_I18N_H
#define PS5TD_I18N_H

enum {
  LANG_AUTO  = -1,   /* config value only: follow the PS5 system language */
  LANG_EN    = 0,
  LANG_TR    = 1,
  LANG_COUNT = 2
};

void        i18n_set(int lang);            /* LANG_EN or LANG_TR; anything else -> LANG_EN */
int         i18n_get(void);                /* active console language */
int         i18n_parse(const char *s);     /* "en" / "tr" / "auto" -> LANG_*, -2 if unknown */
const char *i18n_code(int lang);           /* LANG_* -> "en" / "tr" / "auto" */

/* Pick a string for the active console language / for an explicit language. */
#define LNG(en, tr)        (i18n_get() == LANG_TR ? (tr) : (en))
#define LNGX(lang, en, tr) ((lang) == LANG_TR ? (tr) : (en))

/* Temperature display unit (config.ini "temp_unit"). Values are always measured,
   stored and compared in °C; only text shown to the user is converted. The log
   stays in °C. */
enum { TEMP_C = 0, TEMP_F = 1 };

int         i18n_parse_unit(const char *s);   /* "c" / "f" (also celsius / fahrenheit) -> TEMP_*, -1 if unknown */
const char *i18n_unit_code(int unit);         /* "c" / "f" */
const char *i18n_unit_sym(int unit);          /* "°C" / "°F" */
double      i18n_temp(double c, int unit);    /* an absolute temperature */
double      i18n_tdelta(double c, int unit);  /* a temperature difference or rate */

#endif
