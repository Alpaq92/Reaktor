/* nk_impl.c - the single translation unit holding Nuklear + backend code.
 *
 * The backend is Nuklear's SDL3 renderer, which draws through SDL_Renderer.
 * SDL picks D3D11/12 on Windows, Metal on macOS, OpenGL/Vulkan on Linux and
 * the BSDs, and WebGL under Emscripten - so no graphics API is ever ours. */
#include <SDL3/SDL.h>

/* Must precede nk_common.h: that header includes nuklear.h, and nuklear.h
 * only emits its implementation if NK_IMPLEMENTATION is already defined when
 * the include guard is first opened. */
#define NK_IMPLEMENTATION
#include "nk_common.h"

/* A vendored copy, not the submodule's: the atlas is baked 8-bit indexed
 * rather than RGBA32, which is a change inside the backend. See the header of
 * that file, and docs/NOTICE.md. */
#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nk_sdl3_renderer.h"
