#ifndef REAKTOR_FONT_H
#define REAKTOR_FONT_H

#include <ft2build.h>
#include FT_FREETYPE_H

#include "reaktor/nuklear.h"

#define REAKTOR_FT_LOAD (FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)

struct reaktor_glyph {
    nk_rune codepoint;
    float   xadvance;
    float   x0, y0, x1, y1;
    float   u0, v0, u1, v1;
};

struct reaktor_font {
    struct nk_user_font         handle;
    float                       height, ascent, baseline;
    struct reaktor_glyph       *glyphs;
    int                         glyph_count;
    const struct reaktor_glyph *fallback;
};

struct reaktor_face_src {
    const void *ttf;
    size_t      size;
    float       px;
};

struct reaktor_fontset {
    struct reaktor_font  *fonts;
    int                   count;
    struct reaktor_glyph *glyphs;
    unsigned char        *pixels;
    int                   w, h;
};

int        reaktor_font_valid(const void *ttf, size_t size);
FT_Library reaktor_ft(void);
FT_Face    reaktor_ft_open(const void *ttf, size_t size);
void       reaktor_ft_size(FT_Face face, float px);

int  reaktor_fontset_bake(struct reaktor_fontset *set, const struct reaktor_face_src *src,
                          int n, const nk_rune *ranges);
int  reaktor_fontset_copy(struct reaktor_fontset *to, const struct reaktor_fontset *from);
void reaktor_fontset_bind(struct reaktor_fontset *set, nk_handle texture);
void reaktor_fontset_free(struct reaktor_fontset *set);

const struct reaktor_glyph *reaktor_font_glyph(const struct reaktor_font *f, nk_rune cp);

#endif
