#ifndef REAKTOR_LOCALE_H
#define REAKTOR_LOCALE_H

#include "reaktor/locale.h"

unsigned *reaktor_locale_glyphs(const unsigned char *ttf);
void      reaktor_locale_start(const char *lang);

#endif
