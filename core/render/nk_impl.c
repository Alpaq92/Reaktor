#include <SDL3/SDL.h>

#define NK_IMPLEMENTATION
#include "nk_common.h"

#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nk_sdl3_renderer.h"

static size_t
be32(const unsigned char *p)
{
    return (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
}

/* stb_truetype trusts every offset, so each table must lie inside the file. */
int
reaktor_font_valid(const void *ttf, size_t size)
{
    const unsigned char *p = (const unsigned char *)ttf;
    stbtt_fontinfo info;
    size_t at, n, i;
    int off;

    if (!p || size < 16 || size > (size_t)SDL_MAX_SINT32) return 0;
    off = stbtt_GetFontOffsetForIndex(p, 0);
    if (off < 0 || (size_t)off > size - 12) return 0;
    at = (size_t)off;
    n  = (size_t)p[at + 4] << 8 | p[at + 5];
    if (!n || n > (size - at - 12) / 16) return 0;
    for (i = 0; i < n; i++) {
        const unsigned char *r = p + at + 12 + 16 * i;
        size_t start = be32(r + 8), len = be32(r + 12);

        if (start > size || len > size - start) return 0;
    }
    return stbtt_InitFont(&info, p, off);
}

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
