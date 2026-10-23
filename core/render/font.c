#include <SDL3/SDL.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_MODULE_H

#include "font.h"

typedef struct baked {
    int    w, h, left, top, at;
    int    x, y;
} baked;

static void *
ft_alloc(FT_Memory m, long size)
{
    (void)m;
    return SDL_malloc((size_t)size);
}

static void
ft_free(FT_Memory m, void *p)
{
    (void)m;
    SDL_free(p);
}

static void *
ft_realloc(FT_Memory m, long cur, long size, void *p)
{
    (void)m; (void)cur;
    return SDL_realloc(p, (size_t)size);
}

FT_Library
reaktor_ft(void)
{
    static struct FT_MemoryRec_ memory = { NULL, ft_alloc, ft_free, ft_realloc };
    static FT_Library lib;

    if (!lib) {
        if (FT_New_Library(&memory, &lib)) return lib = NULL;
        FT_Add_Default_Modules(lib);
    }
    return lib;
}

FT_Face
reaktor_ft_open(const void *ttf, size_t size)
{
    FT_Library lib = reaktor_ft();
    FT_Face    face;

    if (!lib || !ttf || size > (size_t)SDL_MAX_SINT32 ||
        FT_New_Memory_Face(lib, (const FT_Byte *)ttf, (FT_Long)size, 0, &face))
        return NULL;
    if (!FT_IS_SCALABLE(face) || face->ascender <= face->descender) {
        FT_Done_Face(face);
        return NULL;
    }
    return face;
}

static size_t
be32(const unsigned char *p)
{
    return (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
}

int
reaktor_font_valid(const void *ttf, size_t size)
{
    const unsigned char *p = (const unsigned char *)ttf;
    FT_Face              face;
    size_t               at = 0, n, i;

    if (!p || size < 16) return 0;
    if (SDL_memcmp(p, "ttcf", 4) == 0) {
        at = be32(p + 12);
        if (at > size - 12) return 0;
    }
    n = (size_t)p[at + 4] << 8 | p[at + 5];
    if (!n || n > (size - at - 12) / 16) return 0;
    for (i = 0; i < n; i++) {
        const unsigned char *r = p + at + 12 + 16 * i;
        size_t start = be32(r + 8), len = be32(r + 12);

        if (start > size || len > size - start) return 0;
    }
    if (!(face = reaktor_ft_open(ttf, size))) return 0;
    FT_Done_Face(face);
    return 1;
}

static float
unit_scale(FT_Face face, float px)
{
    return px / (float)(face->ascender - face->descender);
}

void
reaktor_ft_size(FT_Face face, float px)
{
    FT_Set_Char_Size(face, 0, (FT_F26Dot6)(face->units_per_EM * unit_scale(face, px) * 64.0f + 0.5f),
                     72, 72);
}

const struct reaktor_glyph *
reaktor_font_glyph(const struct reaktor_font *f, nk_rune cp)
{
    int lo = 0, hi = f->glyph_count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        if (f->glyphs[mid].codepoint == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].codepoint < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

static const struct reaktor_glyph *
glyph_or_fallback(const struct reaktor_font *f, nk_rune cp)
{
    const struct reaktor_glyph *g = reaktor_font_glyph(f, cp);

    return g ? g : f->fallback;
}

static float
text_width(nk_handle handle, float height, const char *text, int len)
{
    const struct reaktor_font *font = (const struct reaktor_font *)handle.ptr;
    float                      scale, width = 0.0f;
    nk_rune                    cp;
    int                        at, n;

    if (!font || !text || len <= 0 || !font->fallback) return 0.0f;
    scale = height / font->height;
    for (at = 0; at < len && (n = nk_utf_decode(text + at, &cp, len - at)) > 0; at += n) {
        if (cp == NK_UTF_INVALID) break;
        width += glyph_or_fallback(font, cp)->xadvance * scale;
    }
    return width;
}

static void
query(nk_handle handle, float height, struct nk_user_font_glyph *out, nk_rune cp,
      nk_rune next)
{
    const struct reaktor_font  *font = (const struct reaktor_font *)handle.ptr;
    const struct reaktor_glyph *g;
    float                       scale;

    (void)next;
    if (!font || !out || !font->fallback) return;
    scale         = height / font->height;
    g             = glyph_or_fallback(font, cp);
    out->width    = (g->x1 - g->x0) * scale;
    out->height   = (g->y1 - g->y0) * scale;
    out->offset   = nk_vec2(g->x0 * scale, g->y0 * scale);
    out->xadvance = g->xadvance * scale;
    out->uv[0]    = nk_vec2(g->u0, g->v0);
    out->uv[1]    = nk_vec2(g->u1, g->v1);
}

static int SDLCALL
by_rune(const void *a, const void *b)
{
    nk_rune x = *(const nk_rune *)a, y = *(const nk_rune *)b;

    return x < y ? -1 : x > y;
}

static nk_rune *
codepoints(const nk_rune *ranges, int *count)
{
    nk_rune *cp;
    int      n = 0, i, k;

    for (i = 0; ranges[i] && ranges[i + 1]; i += 2)
        if (ranges[i + 1] >= ranges[i]) n += (int)(ranges[i + 1] - ranges[i]) + 1;
    if (!(cp = SDL_malloc((size_t)(n ? n : 1) * sizeof *cp))) return NULL;
    for (i = 0, n = 0; ranges[i] && ranges[i + 1]; i += 2)
        for (k = (int)ranges[i]; k <= (int)ranges[i + 1]; k++) cp[n++] = (nk_rune)k;
    SDL_qsort(cp, (size_t)n, sizeof *cp, by_rune);
    for (i = k = 0; i < n; i++)
        if (!k || cp[i] != cp[k - 1]) cp[k++] = cp[i];
    *count = k;
    return cp;
}

static int SDLCALL
by_height(void *all, const void *a, const void *b)
{
    const baked *x = (const baked *)all + *(const int *)a;
    const baked *y = (const baked *)all + *(const int *)b;

    if (x->h != y->h) return y->h - x->h;
    if (x->w != y->w) return y->w - x->w;
    return *(const int *)a - *(const int *)b;
}

static int
pack(baked *b, int n, int *w, int *h)
{
    long  area = 0;
    int  *order, i, x = 1, y = 1, row = 0;

    for (i = 0; i < n; i++) area += (long)(b[i].w + 1) * (b[i].h + 1);
    for (*w = 256; *w < 4096 && (long)*w * *w < area + area / 3; *w *= 2) {}
    if (!(order = SDL_malloc((size_t)(n ? n : 1) * sizeof *order))) return 0;
    for (i = 0; i < n; i++) order[i] = i;
    SDL_qsort_r(order, (size_t)n, sizeof *order, by_height, b);
    for (i = 0; i < n; i++) {
        baked *g = &b[order[i]];

        if (!g->w || !g->h) continue;
        if (g->w + 2 > *w) {
            SDL_free(order);
            return 0;
        }
        if (x + g->w + 1 > *w) {
            x = 1;
            y += row;
            row = 0;
        }
        g->x = x;
        g->y = y;
        x += g->w + 1;
        if (g->h + 1 > row) row = g->h + 1;
    }
    SDL_free(order);
    *h = y + row + 1;
    return 1;
}

static int
grow(void **p, int *cap, int need, size_t size)
{
    int   n = *cap > 0 ? *cap : 256;
    void *q;

    if (need <= *cap) return 1;
    while (n < need) n *= 2;
    if (!(q = SDL_realloc(*p, (size_t)n * size))) return 0;
    *p   = q;
    *cap = n;
    return 1;
}

int
reaktor_fontset_bake(struct reaktor_fontset *set, const struct reaktor_face_src *src,
                     int n, const nk_rune *ranges)
{
    nk_rune       *cp;
    baked         *b = NULL;
    unsigned char *bits = NULL;
    int            ncp = 0, nb = 0, cap_b = 0, cap_bits = 0, used = 0, i, k, ok = 0;

    SDL_zerop(set);
    if (!(cp = codepoints(ranges, &ncp))) return 0;
    if (!(set->fonts = SDL_calloc((size_t)(n ? n : 1), sizeof *set->fonts)) ||
        !(set->glyphs = SDL_calloc((size_t)(n * ncp ? n * ncp : 1), sizeof *set->glyphs)))
        goto done;
    set->count = n;
    for (i = 0; i < n; i++) {
        struct reaktor_font *f = &set->fonts[i];
        FT_Face              face = reaktor_ft_open(src[i].ttf, src[i].size);
        float                scale;

        f->glyphs = set->glyphs + nb;
        f->height = src[i].px;
        if (!face) continue;
        scale       = unit_scale(face, src[i].px);
        f->ascent   = (float)face->ascender * scale;
        f->baseline = f->ascent + 0.5f;
        reaktor_ft_size(face, src[i].px);
        for (k = 0; k < ncp; k++) {
            FT_UInt               id = FT_Get_Char_Index(face, cp[k]);
            FT_Fixed              adv = 0;
            FT_Bitmap            *bm;
            struct reaktor_glyph *g;
            baked                *d;
            int                   y;

            if (!id || FT_Load_Glyph(face, id, REAKTOR_FT_LOAD) ||
                FT_Get_Advance(face, id, FT_LOAD_NO_SCALE, &adv))
                continue;
            bm = &face->glyph->bitmap;
            if (!grow((void **)&b, &cap_b, nb + 1, sizeof *b) ||
                !grow((void **)&bits, &cap_bits, used + (int)(bm->width * bm->rows), 1)) {
                FT_Done_Face(face);
                goto done;
            }
            d       = &b[nb];
            d->w    = (int)bm->width;
            d->h    = (int)bm->rows;
            d->left = face->glyph->bitmap_left;
            d->top  = face->glyph->bitmap_top;
            d->at   = used;
            d->x = d->y = 0;
            for (y = 0; y < d->h; y++)
                SDL_memcpy(bits + used + y * d->w, bm->buffer + y * bm->pitch, (size_t)d->w);
            used += d->w * d->h;

            g            = &set->glyphs[nb++];
            g->codepoint = cp[k];
            g->xadvance  = (float)(int)((float)adv * scale + 0.5f);
            g->x0        = (float)d->left;
            g->x1        = (float)(d->left + d->w);
            g->y0        = (float)-d->top + f->ascent + 0.5f;
            g->y1        = g->y0 + (float)d->h;
            f->glyph_count++;
        }
        FT_Done_Face(face);
        f->fallback = reaktor_font_glyph(f, '?');
        if (!f->fallback && f->glyph_count) f->fallback = &f->glyphs[0];
    }

    if (!pack(b, nb, &set->w, &set->h) ||
        !(set->pixels = SDL_calloc((size_t)set->w * (size_t)set->h, 1)))
        goto done;
    for (i = 0; i < nb; i++) {
        struct reaktor_glyph *g = &set->glyphs[i];
        int                   y;

        for (y = 0; y < b[i].h; y++)
            SDL_memcpy(set->pixels + (size_t)(b[i].y + y) * set->w + b[i].x,
                       bits + b[i].at + y * b[i].w, (size_t)b[i].w);
        g->u0 = (float)b[i].x / (float)set->w;
        g->v0 = (float)b[i].y / (float)set->h;
        g->u1 = (float)(b[i].x + b[i].w) / (float)set->w;
        g->v1 = (float)(b[i].y + b[i].h) / (float)set->h;
    }
    ok = 1;
done:
    SDL_free(cp);
    SDL_free(b);
    SDL_free(bits);
    if (!ok) reaktor_fontset_free(set);
    return ok;
}

void
reaktor_fontset_bind(struct reaktor_fontset *set, nk_handle texture)
{
    int i;

    for (i = 0; i < set->count; i++) {
        struct reaktor_font *f = &set->fonts[i];

        f->handle.userdata.ptr = f;
        f->handle.height       = f->height;
        f->handle.width        = text_width;
        f->handle.query        = query;
        f->handle.texture      = texture;
    }
}

void
reaktor_fontset_free(struct reaktor_fontset *set)
{
    SDL_free(set->fonts);
    SDL_free(set->glyphs);
    SDL_free(set->pixels);
    SDL_zerop(set);
}
