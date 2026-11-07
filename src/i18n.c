/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "i18n.h"

#include <string.h>

static volatile int g_lang = LANG_EN;

void
i18n_set(int lang) {
  g_lang = (lang == LANG_TR) ? LANG_TR : LANG_EN;
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
  if(ieq(s, "en") || ieq(s, "english")) return LANG_EN;
  if(ieq(s, "tr") || ieq(s, "turkish") || ieq(s, "türkçe") || ieq(s, "turkce")) return LANG_TR;
  return -2;
}

const char *
i18n_code(int lang) {
  switch(lang) {
    case LANG_EN: return "en";
    case LANG_TR: return "tr";
    default:      return "auto";
  }
}
