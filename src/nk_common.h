/* nk_common.h - shared Nuklear configuration.
 * Included by every TU. The implementation lives in nk_impl.c only. */
#ifndef CURIE_NK_COMMON_H
#define CURIE_NK_COMMON_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
/* Both are required by nuklear_sdl3_renderer.h, which #errors without them. */
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA
/* Font baking replaces the GDI font path: stb_truetype rasterises into an
 * atlas, which is portable and is how the CC0 TTF will be loaded (M3). */
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

#include "../third_party/nuklear/nuklear.h"

#endif /* CURIE_NK_COMMON_H */
