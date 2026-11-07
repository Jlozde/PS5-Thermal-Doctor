/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Language support.
 *
 * Texts a player sees (verdicts, reasons, advice, PS5 notifications, the web UI)
 * exist in eight languages: English, Turkish, German, French, Spanish, Russian,
 * Korean and Simplified Chinese. They live in tools/i18n_texts.py and reach the
 * C code as TX_<KEY>(lang) macros in the generated src/texts.h.
 *
 * Technical texts (the log, config.ini comments, diagnostics) stay in English
 * and Turkish, written next to each other in the source:
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
 * Any other language falls back to English for these.
 *
 * The console-wide language (notifications, stdout, log file) comes from
 * config.ini "lang" (auto or a language code); "auto" follows the PS5 system
 * language. The web API returns text in the language the browser asks for (?lang=).
 */
#ifndef PS5TD_I18N_H
#define PS5TD_I18N_H

enum {
  LANG_AUTO  = -1,   /* config value only: follow the PS5 system language */
  LANG_EN    = 0,
  LANG_TR    = 1,
  LANG_DE    = 2,
  LANG_FR    = 3,
  LANG_ES    = 4,
  LANG_RU    = 5,
  LANG_KO    = 6,
  LANG_ZH    = 7,   /* Simplified Chinese */
  LANG_COUNT = 8
};
/* The log and other technical texts exist in English and Turkish only. */
#define LANG_TECH_COUNT 2
#define LANG_TECH(lang) ((lang) == LANG_TR ? LANG_TR : LANG_EN)

void        i18n_set(int lang);            /* a LANG_* language; anything else -> LANG_EN */
int         i18n_get(void);                /* active console language */
int         i18n_parse(const char *s);     /* "en", "de", ... or "auto" -> LANG_*, -2 if unknown */
const char *i18n_code(int lang);           /* LANG_* -> "en", "de", ... or "auto" */
int         i18n_from_sce(int sce_lang);   /* PS5 system language code -> LANG_* (English if unsupported) */

/* English / Turkish technical text for the console language or an explicit one. */
#define LNG(en, tr)        (i18n_get() == LANG_TR ? (tr) : (en))
#define LNGX(lang, en, tr) ((lang) == LANG_TR ? (tr) : (en))

/* Player-facing text in every language (used by the generated src/texts.h). A chain
   of conditionals keeps the literals visible to the compiler's format checks. */
#define T8(l, en, tr, de, fr, es, ru, ko, zh) \
  ((l) == LANG_TR ? (tr) : (l) == LANG_DE ? (de) : (l) == LANG_FR ? (fr) : (l) == LANG_ES ? (es) : \
   (l) == LANG_RU ? (ru) : (l) == LANG_KO ? (ko) : (l) == LANG_ZH ? (zh) : (en))

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
