#ifndef REAKTOR_NK_COMMON_H
#define REAKTOR_NK_COMMON_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

/* A browser delivers press and release in one task. */
#define NK_BUTTON_TRIGGER_ON_RELEASE

#include "../../external/nuklear/nuklear.h"

/* The caller SDL_frees the ranges. */
nk_rune *reaktor_font_ranges(const unsigned char *ttf, const unsigned *cp,
                             int n);

#endif
