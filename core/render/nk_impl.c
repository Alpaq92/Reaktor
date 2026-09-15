#include <SDL3/SDL.h>

#define NK_IMPLEMENTATION
#include "nk_common.h"

#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nk_sdl3_renderer.h"

nk_rune *
reaktor_font_ranges(const unsigned char *ttf, const unsigned *cp, int n)
{
    stbtt_fontinfo info;
    nk_rune       *out;
    int            i, at = 0, run = 0;

    if (!ttf || n < 0 ||
        !stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)))
        return NULL;
    out = SDL_malloc(((size_t)n * 2 + 3) * sizeof *out);
    if (!out) return NULL;

    out[at++] = 0x0020;
    out[at++] = 0x00FF;
    for (i = 0; i < n; i++) {
        if (!stbtt_FindGlyphIndex(&info, (int)cp[i])) {
            run = 0;
        } else if (run && cp[i] == out[at - 1] + 1) {
            out[at - 1] = cp[i];
        } else {
            out[at++] = cp[i];
            out[at++] = cp[i];
            run = 1;
        }
    }
    out[at] = 0;
    return out;
}
