#include <stddef.h>

#include <SDL3/SDL.h>

#include "nk_common.h"
#include "nk_sdl3_renderer.h"
#include "text.h"

#include "../../external/nuklear/src/stb_truetype.h"
#include "kb_text_shape.h"
#include "mojibake.h"

#define FONTS_MAX   16
#define FACES_MAX   32
#define CONFIGS_MAX 64
#define BUCKETS     4096
#define SHAPED_MAX  (1 << 15)
#define SLOTS_MAX   (1 << 14)
#define PAGE        512
#define PAGES_MAX   8

typedef char text_command_fits[
    offsetof(struct nk_command_text, string) >= sizeof(struct nk_command_custom)
        ? 1 : -1];

typedef struct text_font {
    char           path[1024];
    char           name[64];
    unsigned char *data;
    int            state;
    stbtt_fontinfo info;
    kbts_font      kb;
    float          unit;
    Uint8         *drawn;
    int            drawn_n;
} text_font;

typedef struct text_face {
    struct nk_font *font;
    int             file;
    int             baseline_known;
    float           baseline;
    nk_text_width_f atlas_width;
} text_face;

typedef struct shaped {
    float  x, y;
    Uint16 id;
    Uint8  font;
} shaped;

typedef struct shape_entry {
    Uint32 hash;
    int    file, len, text;
    int    glyph, glyphs, next;
    float  width;
} shape_entry;

typedef struct text_char {
    int         cp, font;
    kbts_script script;
    Uint8       level, start;
} text_char;

typedef struct text_run {
    int         from, to, font;
    Uint8       level;
    kbts_script script;
} text_run;

typedef struct kb_config {
    int                    font;
    kbts_script            script;
    kbts_shape_config     *shape;
    kbts_glyph_config     *glyph;
    kbts_shape_scratchpad *scratch;
} kb_config;

typedef struct glyph_slot {
    Uint32 key;
    Sint16 left, top;
    Uint16 x, y, w, h;
    Uint8  page;
} glyph_slot;

typedef struct glyph_page {
    SDL_Texture *tex;
    int          argb;
    int          x, y, row;
} glyph_page;

typedef struct text_draw {
    int             face, len;
    float           height;
    struct nk_color fg;
    const char     *text;
} text_draw;

static text_font g_font[FONTS_MAX];
static int       g_fonts;
static int       g_fallback[FONTS_MAX];
static int       g_fallbacks;
static struct nk_allocator g_raster;

static text_face g_face[FACES_MAX];
static int       g_faces;

static shape_entry *g_entry;
static shaped      *g_glyph;
static char        *g_bytes;
static int          g_entries, g_entry_cap, g_glyphs, g_glyph_cap;
static int          g_byte_n, g_byte_cap;
static int          g_bucket[BUCKETS];

static text_char *g_ch;
static text_run  *g_run;
static int       *g_order;
static int        g_ch_cap, g_run_cap, g_order_cap;

static kb_config          g_config[CONFIGS_MAX];
static int                g_configs;
static kbts_glyph_storage g_storage;

static glyph_slot *g_slot;
static int         g_slot_cap, g_slots, g_full;
static glyph_page  g_page[PAGES_MAX];
static int         g_pages, g_page_at;
static Uint8      *g_bitmap;
static Uint32     *g_pixels;
static int         g_bitmap_cap, g_pixel_cap;

static SDL_Renderer *g_renderer;
static text_draw    *g_draw;
static int           g_draws, g_draw_cap;
static unsigned      g_seq;
static int           g_seq_known, g_took;

static void *
grow(void *p, int *cap, int need, size_t size)
{
    int n = *cap > 0 ? *cap : 64;

    if (need <= *cap) return p;
    while (n < need) n *= 2;
    p = SDL_realloc(p, (size_t)n * size);
    if (p) *cap = n;
    return p;
}

static int
font_add(const char *path)
{
    int i;

    for (i = 0; i < g_fonts; i++)
        if (SDL_strcmp(g_font[i].path, path) == 0) return i;
    if (g_fonts == FONTS_MAX || SDL_strlen(path) >= sizeof g_font[0].path)
        return -1;
    SDL_zero(g_font[g_fonts]);
    SDL_strlcpy(g_font[g_fonts].path, path, sizeof g_font[0].path);
    return g_fonts++;
}

static void
font_name(text_font *f)
{
    const char *s, *base, *dot;
    int         len = 0, i, at = 0;

    s = stbtt_GetFontNameString(&f->info, &len, STBTT_PLATFORM_ID_MICROSOFT,
                                STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1);
    for (i = 0; s && i + 1 < len && at + 1 < (int)sizeof f->name; i += 2)
        if (s[i] == 0 && s[i + 1] >= 0x20 && s[i + 1] < 0x7F)
            f->name[at++] = s[i + 1];
    f->name[at] = 0;
    if (at > 0) return;
    base = SDL_strrchr(f->path, '/');
    if (SDL_strrchr(f->path, 92) > base) base = SDL_strrchr(f->path, 92);
    base = base ? base + 1 : f->path;
    dot  = SDL_strrchr(base, '.');
    SDL_snprintf(f->name, sizeof f->name, "%.*s",
                 dot ? (int)(dot - base) : (int)SDL_strlen(base), base);
}

static int
font_open(int i)
{
    text_font *f = &g_font[i];
    size_t     size = 0;
    int        at;

    if (f->state) return f->state > 0;
    f->state = -1;
    f->data  = SDL_LoadFile(f->path, &size);
    if (f->data && size <= (size_t)SDL_MAX_SINT32) {
        at = stbtt_GetFontOffsetForIndex(f->data, 0);
        if (at >= 0 && stbtt_InitFont(&f->info, f->data, at)) {
            /* Nuklear's stb_truetype allocates through userdata. */
            if (!g_raster.alloc) g_raster = nk_sdl_allocator();
            f->info.userdata = &g_raster;
            f->kb = kbts_FontFromMemory(f->data, (int)size, 0, NULL, NULL);
            if (kbts_FontIsValid(&f->kb)) {
                f->unit  = stbtt_ScaleForPixelHeight(&f->info, 1.0f);
                f->state = 1;
                font_name(f);
                return 1;
            }
            kbts_FreeFont(&f->kb);
        }
    }
    SDL_Log("text: could not open the font %s", f->path);
    SDL_free(f->data);
    f->data = NULL;
    return 0;
}

void
reaktor_text_add_fallback(const char *path)
{
    int i = path ? font_add(path) : -1;

    if (i >= 0 && g_fallbacks < FONTS_MAX) g_fallback[g_fallbacks++] = i;
}

static int
face_of(const struct nk_font *font)
{
    static int last;
    int        i;

    if (last < g_faces && g_face[last].font == font) return last;
    for (i = 0; i < g_faces; i++)
        if (g_face[i].font == font) return last = i;
    return -1;
}

static int
atlas_draws(const struct nk_font *font, const char *s, int len)
{
    int i = 0;

    while (i < len) {
        const struct nk_font_glyph *g;
        nk_rune                     cp;
        int                         n;

        if (!((unsigned char)s[i] & 0x80)) {
            i++;
            continue;
        }
        n = nk_utf_decode(s + i, &cp, len - i);
        if (n <= 0) return 1;
        g = nk_font_find_glyph(font, cp);
        if (g == font->fallback && cp != font->fallback_codepoint) return 0;
        i += n;
    }
    return 1;
}

static float
baseline(text_face *f)
{
    const struct nk_font_glyph *x;
    text_font                  *t = &g_font[f->file];
    float                       scale;
    int                         x0, y0, x1, y1;

    if (f->baseline_known) return f->baseline;
    f->baseline_known = 1;
    f->baseline = f->font->info.ascent + 0.5f;
    x = nk_font_find_glyph(f->font, 'x');
    if (x && x != f->font->fallback && font_open(f->file)) {
        scale = stbtt_ScaleForPixelHeight(&t->info, f->font->info.height);
        stbtt_GetCodepointBitmapBox(&t->info, 'x', scale, scale, &x0, &y0, &x1, &y1);
        f->baseline = x->y1 - (float)y1;
    }
    return f->baseline;
}

static int
decode(const char *s, int len)
{
    text_char *p;
    int        n = 0, at = 0;

    if (!(p = grow(g_ch, &g_ch_cap, len, sizeof *g_ch))) return 0;
    g_ch = p;
    while (at < len) {
        kbts_decode d = kbts_DecodeUtf8(s + at, (kbts_un)(len - at));

        SDL_zero(g_ch[n]);
        g_ch[n++].cp = d.Valid ? d.Codepoint : 0xFFFD;
        at += d.SourceCharactersConsumed > 0 ? d.SourceCharactersConsumed : 1;
    }
    return n;
}

static void
levels(const char *s, int len, int n)
{
    mjb_bidi_paragraph p;
    int                i, k = 0;

    for (i = 0; i < n && g_ch[i].cp < 0x0590; i++) {}
    if (i == n ||
        mjb_bidi_resolve(s, (size_t)len, MJB_ENC_UTF_8, MJB_DIRECTION_AUTO, &p)
            != MJB_STATUS_OK)
        return;
    /* mojibake 0.3.6 puts byte_offset at a character's last byte. */
    for (i = 0; i < n; i++) {
        if (k < (int)p.count && (int)p.chars[k].codepoint == g_ch[i].cp)
            g_ch[i].level = p.chars[k++].level;
        else
            g_ch[i].level = i > 0 ? g_ch[i - 1].level : p.paragraph_level;
    }
    mjb_bidi_paragraph_free(&p);
}

static void
segment(int n)
{
    kbts_break_state st;
    kbts_break       b;
    kbts_script      known = KBTS_SCRIPT_DONT_KNOW;
    int              i;

    kbts_BreakBegin(&st, KBTS_DIRECTION_DONT_KNOW,
                    KBTS_JAPANESE_LINE_BREAK_STYLE_NORMAL, 0);
    for (i = 0; i < n; i++) {
        kbts_BreakAddCodepoint(&st, g_ch[i].cp, 1, i == n - 1);
        while (kbts_Break(&st, &b)) {
            if (b.Position < 0 || b.Position >= n) continue;
            if (b.Flags & KBTS_BREAK_FLAG_GRAPHEME) g_ch[b.Position].start = 1;
            if (b.Flags & KBTS_BREAK_FLAG_SCRIPT) g_ch[b.Position].script = b.Script;
        }
    }
    g_ch[0].start = 1;
    for (i = 0; i < n && known == KBTS_SCRIPT_DONT_KNOW; i++) known = g_ch[i].script;
    for (i = 0; i < n; i++) {
        if (g_ch[i].script != KBTS_SCRIPT_DONT_KNOW) known = g_ch[i].script;
        else g_ch[i].script = known;
    }
}

static int
covers(int font, int from, int to)
{
    kbts_font_coverage_test t;
    int                     i;

    kbts_FontCoverageTestBegin(&t, &g_font[font].kb);
    for (i = from; i < to; i++) kbts_FontCoverageTestCodepoint(&t, g_ch[i].cp);
    return kbts_FontCoverageTestEnd(&t);
}

static int
first_covering(int base, int from, int to)
{
    int k;

    if (covers(base, from, to)) return base;
    for (k = 0; k < g_fallbacks; k++)
        if (font_open(g_fallback[k]) && covers(g_fallback[k], from, to))
            return g_fallback[k];
    return -1;
}

static void
choose_fonts(int base, int n)
{
    int i, j, k, pick;

    for (i = 0; i < n; i = j) {
        for (j = i + 1; j < n && !g_ch[j].start; j++) {}
        pick = first_covering(base, i, j);
        if (pick < 0 && j > i + 1) pick = first_covering(base, i, i + 1);
        if (pick < 0) pick = i > 0 ? g_ch[i - 1].font : base;
        for (k = i; k < j; k++) g_ch[k].font = pick;
    }
}

static int
split(int n)
{
    text_run *p;
    int       i, r = 0;

    for (i = 0; i < n; i++) {
        if (r == 0 || g_ch[i].level != g_ch[i - 1].level ||
            g_ch[i].script != g_ch[i - 1].script ||
            g_ch[i].font != g_ch[i - 1].font) {
            if ((p = grow(g_run, &g_run_cap, r + 1, sizeof *g_run)) != NULL) {
                g_run = p;
                g_run[r].from   = i;
                g_run[r].font   = g_ch[i].font;
                g_run[r].level  = g_ch[i].level;
                g_run[r].script = g_ch[i].script;
                r++;
            } else if (r == 0) {
                return 0;
            }
        }
        g_run[r - 1].to = i + 1;
    }
    return r;
}

static void
order(int runs)
{
    int hi = 0, lo = 256, lv, i, j, a, b, t;

    for (i = 0; i < runs; i++) {
        g_order[i] = i;
        if (g_run[i].level > hi) hi = g_run[i].level;
        if ((g_run[i].level & 1) && g_run[i].level < lo) lo = g_run[i].level;
    }
    for (lv = hi; lv >= lo; lv--) {
        for (i = 0; i < runs;) {
            if (g_run[g_order[i]].level < lv) {
                i++;
                continue;
            }
            for (j = i; j < runs && g_run[g_order[j]].level >= lv; j++) {}
            for (a = i, b = j - 1; a < b; a++, b--) {
                t = g_order[a];
                g_order[a] = g_order[b];
                g_order[b] = t;
            }
            i = j;
        }
    }
}

static void
config_free(kb_config *c)
{
    if (c->scratch) kbts_DestroyShapeScratchpad(c->scratch);
    if (c->glyph) kbts_DestroyGlyphConfig(c->glyph);
    if (c->shape) kbts_DestroyShapeConfig(c->shape);
    SDL_zerop(c);
}

static kb_config *
config_for(int font, kbts_script script)
{
    kbts_feature_override plain;
    kb_config            *c = NULL;
    int                   i;

    for (i = 0; i < g_configs && !c; i++)
        if (g_config[i].font == font && g_config[i].script == script)
            c = &g_config[i];
    if (c && c->scratch) return c;
    if (!c) {
        if (g_configs == CONFIGS_MAX) {
            for (i = 0; i < g_configs; i++) config_free(&g_config[i]);
            g_configs = 0;
        }
        c = &g_config[g_configs];
        c->font   = font;
        c->script = script;
        c->shape  = kbts_CreateShapeConfig(&g_font[font].kb, script,
                                           KBTS_LANGUAGE_DONT_KNOW, NULL, NULL);
        plain.Tag   = KBTS_FEATURE_TAG_kern;
        plain.Value = 0;
        c->glyph = c->shape ? kbts_CreateGlyphConfig(c->shape, &plain, 1, NULL, NULL)
                            : NULL;
        if (!c->glyph) {
            config_free(c);
            return NULL;
        }
        g_configs++;
    }
    c->scratch = kbts_CreateShapeScratchpad(c->shape, NULL, NULL);
    return c->scratch ? c : NULL;
}

static float
shape_run(const text_run *r, float pen)
{
    text_font          *f = &g_font[r->font];
    kb_config          *c = config_for(r->font, r->script);
    kbts_glyph_iterator it;
    kbts_glyph         *g;
    shaped             *out;
    void               *p;
    int                 i;

    if (!c) return pen;
    kbts_ClearActiveGlyphs(&g_storage);
    for (i = r->from; i < r->to; i++)
        if (!kbts_PushGlyph(&g_storage, &f->kb, g_ch[i].cp, c->glyph, i)) return pen;
    if (kbts_ShapeDirect(c->scratch, &g_storage,
                         (r->level & 1) ? KBTS_DIRECTION_RTL : KBTS_DIRECTION_LTR,
                         &it) != KBTS_SHAPE_ERROR_NONE) {
        /* A scratchpad keeps its error. */
        kbts_DestroyShapeScratchpad(c->scratch);
        c->scratch = NULL;
        return pen;
    }
    while (kbts_GlyphIteratorNext(&it, &g)) {
        if (!(p = grow(g_glyph, &g_glyph_cap, g_glyphs + 1, sizeof *g_glyph)))
            break;
        g_glyph   = p;
        out       = &g_glyph[g_glyphs++];
        out->x    = pen + (float)g->OffsetX * f->unit;
        out->y    = (float)g->OffsetY * f->unit;
        out->id   = g->Id;
        out->font = (Uint8)r->font;
        pen += (float)g->AdvanceX * f->unit;
    }
    return pen;
}

static int
shape(int file, const char *s, int len)
{
    shape_entry *e;
    Uint32       hash = 2166136261u ^ (Uint32)file;
    void        *p;
    float        pen = 0.0f;
    int          i, n, runs, first;

    for (i = 0; i < len; i++) hash = (hash ^ (Uint8)s[i]) * 16777619u;
    for (i = g_bucket[hash & (BUCKETS - 1)]; i > 0; i = g_entry[i - 1].next) {
        e = &g_entry[i - 1];
        if (e->hash == hash && e->file == file && e->len == len &&
            SDL_memcmp(g_bytes + e->text, s, (size_t)len) == 0)
            return i - 1;
    }

    if (len <= 0 || !font_open(file) || (n = decode(s, len)) <= 0) return -1;
    levels(s, len, n);
    segment(n);
    choose_fonts(file, n);
    runs = split(n);
    if (!(p = grow(g_order, &g_order_cap, runs, sizeof *g_order))) return -1;
    g_order = p;
    order(runs);
    if (!(p = grow(g_entry, &g_entry_cap, g_entries + 1, sizeof *g_entry))) return -1;
    g_entry = p;
    if (!(p = grow(g_bytes, &g_byte_cap, g_byte_n + len, 1))) return -1;
    g_bytes = p;

    first = g_glyphs;
    for (i = 0; i < runs; i++) pen = shape_run(&g_run[g_order[i]], pen);

    e = &g_entry[g_entries];
    e->hash   = hash;
    e->file   = file;
    e->len    = len;
    e->text   = g_byte_n;
    e->glyph  = first;
    e->glyphs = g_glyphs - first;
    e->width  = pen;
    e->next   = g_bucket[hash & (BUCKETS - 1)];
    g_bucket[hash & (BUCKETS - 1)] = g_entries + 1;
    SDL_memcpy(g_bytes + g_byte_n, s, (size_t)len);
    g_byte_n += len;
    return g_entries++;
}

static void
shapes_forget(void)
{
    SDL_free(g_entry);
    SDL_free(g_glyph);
    SDL_free(g_bytes);
    g_entry = NULL;
    g_glyph = NULL;
    g_bytes = NULL;
    g_entries = g_entry_cap = g_glyphs = g_glyph_cap = g_byte_n = g_byte_cap = 0;
    SDL_memset(g_bucket, 0, sizeof g_bucket);
}

static int
page_new(void)
{
    glyph_page  *p = &g_page[g_pages];
    SDL_Texture *tex;

    if (g_pages == PAGES_MAX || !g_renderer) return 0;
    SDL_zerop(p);
    tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_INDEX8,
                            SDL_TEXTUREACCESS_STATIC, PAGE, PAGE);
    if (tex && !nk_sdl_set_coverage_palette(tex)) {
        SDL_DestroyTexture(tex);
        tex = NULL;
    }
    if (!tex) {
        tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STATIC, PAGE, PAGE);
        p->argb = 1;
    }
    if (!tex) return 0;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    p->tex = tex;
    g_pages++;
    return 1;
}

static int
pack(int w, int h, int *x, int *y)
{
    glyph_page *p = g_page_at > 0 ? &g_page[g_page_at - 1] : NULL;

    if (w + 2 > PAGE || h + 2 > PAGE) return -1;
    if (p && p->x + w + 2 > PAGE) {
        p->x = 0;
        p->y += p->row;
        p->row = 0;
    }
    if (!p || p->y + h + 2 > PAGE) {
        if (g_page_at == g_pages && !page_new()) return -1;
        p = &g_page[g_page_at++];
        p->x = p->y = p->row = 0;
    }
    *x = p->x + 1;
    *y = p->y + 1;
    p->x += w + 2;
    if (h + 2 > p->row) p->row = h + 2;
    return g_page_at - 1;
}

static int
upload(int page, int x, int y, int w, int h, text_font *f, int id, float scale)
{
    SDL_Rect r;
    void    *p;
    int      bw = w + 2, bh = h + 2, i;

    if (!(p = grow(g_bitmap, &g_bitmap_cap, bw * bh, 1))) return 0;
    g_bitmap = p;
    SDL_memset(g_bitmap, 0, (size_t)(bw * bh));
    stbtt_MakeGlyphBitmap(&f->info, g_bitmap + bw + 1, w, h, bw, scale, scale, id);
    r.x = x - 1;
    r.y = y - 1;
    r.w = bw;
    r.h = bh;
    if (!g_page[page].argb)
        return SDL_UpdateTexture(g_page[page].tex, &r, g_bitmap, bw);
    if (!(p = grow(g_pixels, &g_pixel_cap, bw * bh, sizeof *g_pixels))) return 0;
    g_pixels = p;
    for (i = 0; i < bw * bh; i++)
        g_pixels[i] = (Uint32)g_bitmap[i] << 24 | 0x00FFFFFF;
    return SDL_UpdateTexture(g_page[page].tex, &r, g_pixels, bw * 4);
}

static glyph_slot *
slot_at(glyph_slot *table, int cap, Uint32 key)
{
    int i = (int)(((key * 2654435761u) >> 16) & (Uint32)(cap - 1));

    while (table[i].key && table[i].key != key) i = (i + 1) & (cap - 1);
    return &table[i];
}

static int
slots_grow(void)
{
    int         cap = g_slot_cap ? g_slot_cap * 2 : 1024, i;
    glyph_slot *t;

    if (cap > SLOTS_MAX || !(t = SDL_calloc((size_t)cap, sizeof *t))) return 0;
    for (i = 0; i < g_slot_cap; i++)
        if (g_slot[i].key) *slot_at(t, cap, g_slot[i].key) = g_slot[i];
    SDL_free(g_slot);
    g_slot     = t;
    g_slot_cap = cap;
    return 1;
}

static const glyph_slot *
glyph_get(int font, int id, int px)
{
    text_font  *f = &g_font[font];
    glyph_slot *s;
    Uint32      key;
    float       scale;
    int         x0, y0, x1, y1, x = 0, y = 0, page = 0;

    if (px < 1) px = 1;
    if (px > 4095) px = 4095;
    key = (Uint32)font << 28 | (Uint32)px << 16 | (Uint32)id;
    if (!g_slot && !slots_grow()) return NULL;
    s = slot_at(g_slot, g_slot_cap, key);
    if (s->key) return s;
    if (g_slots + 1 > g_slot_cap / 4 * 3) {
        if (!slots_grow()) {
            g_full = 1;
            return NULL;
        }
        s = slot_at(g_slot, g_slot_cap, key);
    }

    scale = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    stbtt_GetGlyphBitmapBox(&f->info, id, scale, scale, &x0, &y0, &x1, &y1);
    if (x1 > x0 && y1 > y0) {
        page = pack(x1 - x0, y1 - y0, &x, &y);
        if (page < 0) {
            g_full = 1;
            return NULL;
        }
        if (!upload(page, x, y, x1 - x0, y1 - y0, f, id, scale)) return NULL;
    } else {
        x1 = x0;
        y1 = y0;
    }
    s->key  = key;
    s->left = (Sint16)x0;
    s->top  = (Sint16)y0;
    s->x    = (Uint16)x;
    s->y    = (Uint16)y;
    s->w    = (Uint16)(x1 - x0);
    s->h    = (Uint16)(y1 - y0);
    s->page = (Uint8)page;
    g_slots++;
    return s;
}

static void
glyphs_forget(void)
{
    SDL_free(g_slot);
    g_slot = NULL;
    g_slot_cap = g_slots = g_page_at = g_full = 0;
}

static void
mark_drawn(int font, int id)
{
    text_font *f = &g_font[font];

    if (!f->drawn && !(f->drawn = SDL_calloc(8192, 1))) return;
    if (f->drawn[id >> 3] & (1 << (id & 7))) return;
    f->drawn[id >> 3] |= (Uint8)(1 << (id & 7));
    f->drawn_n++;
}

static float
measure(nk_handle handle, float height, const char *s, int len)
{
    const struct nk_font *font = (const struct nk_font *)handle.ptr;
    int                   face = face_of(font), e = -1;

    if (face < 0) return 0.0f;
    if (!atlas_draws(font, s, len)) {
        g_took = 1;
        e = shape(g_face[face].file, s, len);
    }
    return e < 0 ? g_face[face].atlas_width(handle, height, s, len)
                 : g_entry[e].width * height;
}

void
reaktor_text_attach_font(struct nk_font *font, const char *path)
{
    text_face *f;
    int        file;

    if (!font) {
        g_faces = g_draws = g_seq_known = 0;
        glyphs_forget();
        return;
    }
    if (!path || g_faces == FACES_MAX || font->handle.width == measure) return;
    if ((file = font_add(path)) < 0) return;
    f = &g_face[g_faces++];
    f->font           = font;
    f->file           = file;
    f->baseline_known = 0;
    f->atlas_width    = font->handle.width;
    font->handle.width = measure;
}

static void
draw(void *canvas, short x, short y, unsigned short w, unsigned short h,
     nk_handle data)
{
    struct nk_draw_list *list = (struct nk_draw_list *)canvas;
    struct nk_rect       clip = list->clip_rect;
    const text_draw     *d;
    text_face           *f;
    struct nk_color      fg;
    float                px, k, top;
    int                  e, i;

    if (data.id < 0 || data.id >= g_draws) return;
    if (!(clip.x < x + w && x < clip.x + clip.w && clip.y < y + h && y < clip.y + clip.h))
        return;
    d = &g_draw[data.id];
    f = &g_face[d->face];
    if ((e = shape(f->file, d->text, d->len)) < 0) return;
    px   = f->font->info.height * d->height / f->font->handle.height;
    k    = d->height / px;
    top  = baseline(f) * px / f->font->info.height;
    fg   = d->fg;
    fg.a = (nk_byte)((float)fg.a * list->config.global_alpha);
    for (i = 0; i < g_entry[e].glyphs; i++) {
        const shaped     *g = &g_glyph[g_entry[e].glyph + i];
        const glyph_slot *s = glyph_get(g->font, g->id, (int)(px + 0.5f));
        struct nk_image   im;
        float             gx, gy;

        if (!s || !s->w) continue;
        gx = SDL_floorf(g->x * px + 0.5f) + (float)s->left;
        gy = SDL_floorf(top - g->y * px + 0.5f) + (float)s->top;
        im = nk_subimage_ptr(g_page[s->page].tex, PAGE, PAGE,
                             nk_rect((float)s->x, (float)s->y, (float)s->w, (float)s->h));
        nk_draw_list_add_image(list, im,
                               nk_rect((float)x + gx * k, (float)y + gy * k,
                                       (float)s->w * k, (float)s->h * k),
                               fg);
        if (g->font != f->file) mark_drawn(g->font, g->id);
    }
}

void
reaktor_text_prepare(struct nk_context *ctx, struct SDL_Renderer *renderer)
{
    const struct nk_command *cmd;

    g_renderer = renderer;
    /* The same frame again: its text commands are already custom ones. */
    if (g_faces == 0 || (g_seq_known && ctx->seq == g_seq)) return;
    g_seq       = ctx->seq;
    g_seq_known = 1;
    g_draws     = 0;
    if (!g_took) return;
    g_took = 0;
    if (g_glyphs > SHAPED_MAX) shapes_forget();
    if (g_full) glyphs_forget();

    nk_foreach(cmd, ctx) {
        struct nk_command_text   *t = (struct nk_command_text *)cmd;
        struct nk_command_custom *c = (struct nk_command_custom *)cmd;
        const struct nk_font     *font;
        text_draw                 d;
        void                     *p;
        short                     x, y;
        unsigned short            w, h;
        int                       face;

        if (cmd->type != NK_COMMAND_TEXT || t->font->width != measure) continue;
        font = (const struct nk_font *)t->font->userdata.ptr;
        face = face_of(font);
        if (face < 0 || atlas_draws(font, t->string, t->length) ||
            !font_open(g_face[face].file))
            continue;
        if (!(p = grow(g_draw, &g_draw_cap, g_draws + 1, sizeof *g_draw))) break;
        g_draw   = p;
        d.face   = face;
        d.len    = t->length;
        d.height = t->height;
        d.fg     = t->foreground;
        d.text   = t->string;
        x = t->x;
        y = t->y;
        w = t->w;
        h = t->h;
        c->header.type   = NK_COMMAND_CUSTOM;
        c->x             = x;
        c->y             = y;
        c->w             = w;
        c->h             = h;
        c->callback_data = nk_handle_id(g_draws);
        c->callback      = draw;
        g_draw[g_draws++] = d;
    }
}

int
reaktor_text_break(const char *s, int len, int limit)
{
    mjb_next_line_state st;
    mjb_break_type      t;
    int                 best = 0;

    if (len <= 0 || limit <= 0) return 0;
    st.index = 0;
    while ((t = mjb_next_line_break(s, (size_t)len, MJB_ENC_UTF_8, &st))
           != MJB_BT_NOT_SET) {
        /* The boundary is before the character ending at st.index. */
        int at = (int)st.index;

        if (at > len) {
            at = len;
        } else {
            at--;
            while (at > 0 && ((unsigned char)s[at] & 0xC0) == 0x80) at--;
        }
        if (at > limit) break;
        if (t == MJB_BT_ALLOWED || t == MJB_BT_MANDATORY) best = at;
    }
    return best;
}

int
reaktor_text_rtl(const char *s, int len)
{
    int i = 0, isolate = 0;

    while (i < len) {
        kbts_decode   d = kbts_DecodeUtf8(s + i, (kbts_un)(len - i));
        mjb_character ch;

        i += d.SourceCharactersConsumed > 0 ? d.SourceCharactersConsumed : 1;
        if (!d.Valid) continue;
        if (d.Codepoint < 0x80) {
            if (!isolate && ((d.Codepoint | 0x20) >= 'a' && (d.Codepoint | 0x20) <= 'z'))
                return 0;
            continue;
        }
        if (mjb_codepoint_info((mjb_codepoint)d.Codepoint, &ch) != MJB_STATUS_OK)
            continue;
        switch (ch.bidirectional) {
        case MJB_PR_BIDI_CLASS_LRI:
        case MJB_PR_BIDI_CLASS_RLI:
        case MJB_PR_BIDI_CLASS_FSI: isolate++; break;
        case MJB_PR_BIDI_CLASS_PDI: if (isolate) isolate--; break;
        case MJB_PR_BIDI_CLASS_L:   if (!isolate) return 0; break;
        case MJB_PR_BIDI_CLASS_R:
        case MJB_PR_BIDI_CLASS_AL:  if (!isolate) return 1; break;
        default: break;
        }
    }
    return 0;
}

void
reaktor_text_describe(char *buf, int cap)
{
    int i, at = 0;

    if (cap <= 0) return;
    buf[0] = 0;
    for (i = 0; i < g_fallbacks && at < cap; i++) {
        const text_font *f = &g_font[g_fallback[i]];

        if (f->drawn_n > 0)
            at += SDL_snprintf(buf + at, (size_t)(cap - at), "%s%s for %d glyph%s",
                               at ? ", " : "", f->name, f->drawn_n,
                               f->drawn_n == 1 ? "" : "s");
    }
}
