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

/* Nuklear's default fires a button on press, which needs a frame
 * while the mouse is held. A browser delivers press and release in
 * one task, so that frame never happens and the button never fires. */
#define NK_BUTTON_TRIGGER_ON_RELEASE

#include "../../third_party/nuklear/nuklear.h"

#endif
