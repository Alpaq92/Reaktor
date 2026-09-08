/* nk_common.h - shared Nuklear configuration.
 * Included by every TU. The implementation lives in nk_impl.c only. */
#ifndef REAKTOR_NK_COMMON_H
#define REAKTOR_NK_COMMON_H

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

/* Fire a button when it is released, not when it is pressed.
 *
 * Nuklear's default ends in nk_input_is_mouse_pressed - (down && clicked) -
 * so it needs a frame drawn while the button is still held. On a desktop it
 * always gets one. A browser delivers mousedown and mouseup in the same task,
 * so the single frame drawn for a click sees down=0 and nothing fires: every
 * button, tab and link on the wasm build was inert.
 *
 * It is also what a button should do. Press, change your mind, drag off the
 * button, let go - and nothing happens. */
#define NK_BUTTON_TRIGGER_ON_RELEASE

#include "../third_party/nuklear/nuklear.h"

#endif /* REAKTOR_NK_COMMON_H */
