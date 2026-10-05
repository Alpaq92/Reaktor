#ifndef REAKTOR_LOCALE_H
#define REAKTOR_LOCALE_H

#include "reaktor/locale.h"

/* The caller SDL_frees them. */
unsigned *reaktor_locale_glyphs(const unsigned char *ttf);

#endif
