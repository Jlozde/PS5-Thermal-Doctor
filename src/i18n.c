/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "i18n.h"

#include <string.h>

static volatile int g_lang = LANG_EN;

static const char *const g_codes[LANG_COUNT] = { "en", "tr", "de", "fr", "es", "ru", "ko", "zh" };

void
i18n_set(int lang) {
  g_lang = (lang >= 0 && lang < LANG_COUNT) ? lang : LANG_EN;
}

int
i18n_get(void) {
  return g_lang;
}

static int
ieq(const char *a, const char *b) {
  for(; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if(x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if(y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if(x != y) return 0;
  }
  return *a == *b;
}

int
i18n_parse(const char *s) {
  if(!s) return -2;
  if(ieq(s, "auto")) return LANG_AUTO;
  for(int l = 0; l < LANG_COUNT; l++)
    if(ieq(s, g_codes[l])) return l;
  static const struct { const char *name; int lang; } names[] = {
    { "english", LANG_EN }, { "turkish", LANG_TR }, { "türkçe", LANG_TR }, { "turkce", LANG_TR },
    { "german", LANG_DE }, { "deutsch", LANG_DE }, { "french", LANG_FR }, { "francais", LANG_FR },
    { "français", LANG_FR }, { "spanish", LANG_ES }, { "espanol", LANG_ES }, { "español", LANG_ES },
    { "russian", LANG_RU }, { "korean", LANG_KO }, { "chinese", LANG_ZH }, { "zh-cn", LANG_ZH },
    { "zh-hans", LANG_ZH },
  };
  for(unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if(ieq(s, names[i].name)) return names[i].lang;
  return -2;
}

const char *
i18n_code(int lang) {
  return (lang >= 0 && lang < LANG_COUNT) ? g_codes[lang] : "auto";
}

/* sceSystemServiceParamGetInt(SCE_SYSTEM_SERVICE_PARAM_ID_LANG) values */
int
i18n_from_sce(int sce_lang) {
  switch(sce_lang) {
    case 19: return LANG_TR;               /* Turkish */
    case 4:  return LANG_DE;               /* German */
    case 2:  case 22: return LANG_FR;      /* French, French (Canada) */
    case 3:  case 20: return LANG_ES;      /* Spanish, Spanish (Latin America) */
    case 8:  return LANG_RU;               /* Russian */
    case 9:  return LANG_KO;               /* Korean */
    case 10: case 11: return LANG_ZH;      /* Chinese (Traditional, Simplified) */
    default: return LANG_EN;
  }
}

int
i18n_parse_unit(const char *s) {
  if(!s) return -1;
  if(ieq(s, "c") || ieq(s, "celsius")) return TEMP_C;
  if(ieq(s, "f") || ieq(s, "fahrenheit")) return TEMP_F;
  return -1;
}

const char *
i18n_unit_code(int unit) {
  return unit == TEMP_F ? "f" : "c";
}

const char *
i18n_unit_sym(int unit) {
  return unit == TEMP_F ? "°F" : "°C";
}

double
i18n_temp(double c, int unit) {
  return unit == TEMP_F ? c * 1.8 + 32.0 : c;
}

double
i18n_tdelta(double c, int unit) {
  return unit == TEMP_F ? c * 1.8 : c;
}
