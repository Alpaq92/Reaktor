#ifndef REAKTOR_NK_COMMON_H
#define REAKTOR_NK_COMMON_H

#include "reaktor/nuklear.h"

/* The caller SDL_frees the ranges. */
int      reaktor_font_valid(const void *ttf, size_t size);
nk_rune *reaktor_font_ranges(const unsigned char *ttf, const unsigned *cp,
                             int n);

#endif
