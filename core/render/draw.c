#include "internal.h"
#include "locale.h"

static const int g_font_px[FONT_STEPS] =
    { 12, 14, 16, 19, 23, 28, 34, 41 };

static int
parse_colour(const char *spec, char *out, size_t cap)
{
    if (spec[0] != '#' || strlen(spec) >= cap) return 0;
    strcpy(out, spec);
    return 1;
}

static struct tex_slot *
slot_at(void *slots, size_t size, int i)
{
    return (struct tex_slot *)((char *)slots + (size_t)i * size);
}

/* Never evicts a slot this frame's commands use. */
static struct tex_slot *
slot_take(App *app, void *slots, size_t size, int *count, int max)
{
    unsigned now = app->ctx->seq;
    struct tex_slot *k = NULL, *s;
    int i;

    if (*count < max) return slot_at(slots, size, (*count)++);
    for (i = 0; i < max; i++) {
        s = slot_at(slots, size, i);
        if (s->seen != now && (!k || now - s->seen > now - k->seen)) k = s;
    }
    if (k && k->tex) SDL_DestroyTexture(k->tex);
    return k;
}

void
img_cache_clear(App *app)
{
    int i;

    for (i = 0; i < app->img_count; i++)
        if (app->img[i].s.tex) SDL_DestroyTexture(app->img[i].s.tex);
    app->img_count = 0;
}

void
img_cache_free(App *app)
{
    img_cache_clear(app);
    SDL_free(app->img);
    app->img     = NULL;
    app->img_cap = 0;
    if (app->blank) SDL_DestroyTexture(app->blank);
    app->blank = NULL;
}

static struct img_slot *
img_grow(App *app)
{
    int cap = app->img_cap ? app->img_cap * 2 : IMG_CACHE_MAX;
    struct img_slot *more =
        (struct img_slot *)SDL_realloc(app->img, (size_t)cap * sizeof *more);

    if (!more) return NULL;
    app->img     = more;
    app->img_cap = cap;
    return &app->img[app->img_count++];
}

/* A missing icon is blank, not Nuklear's white quad. */
static struct nk_image
blank_image(App *app)
{
    static const Uint32 clear = 0;

    if (!app->blank) {
        app->blank = SDL_CreateTexture(app->ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC, 1, 1);
        if (!app->blank) return nk_image_id(0);
        SDL_UpdateTexture(app->blank, NULL, &clear, (int)sizeof clear);
        SDL_SetTextureBlendMode(app->blank, SDL_BLENDMODE_BLEND);
    }
    return nk_image_ptr(app->blank);
}

static const char *
icon_source(App *app, const char *rel, char *out, size_t cap)
{
    static const char ion[] = "external/ionicons/src/svg/";
    const char *base;
    int i;

    if (SDL_strncmp(rel, ion, sizeof(ion) - 1) != 0) return rel;
    base = rel + sizeof(ion) - 1;
    for (i = 0; i < app->icon_dir_count; i++) {
        SDL_snprintf(out, cap, "%s/%s", app->icon_dirs[i], base);
        if (reaktor_asset_exists(out)) return out;
    }
    SDL_snprintf(out, cap, "icons/%s", base);
    return reaktor_asset_exists(out) ? out : rel;
}

static struct img_slot *
img_lookup(App *app, const char *src, int px)
{
    char rel[192], found[1024], ocol[16] = {0}, icol[16] = {0};
    float swk = 0.0f;
    const char *q;
    plutovg_surface_t *surf;
    SDL_Surface *sdlsurf;
    SDL_Texture *tex;
    struct img_slot *slot;
    int i, w, h, stride;

    if (px < 8) px = 8;
    if (px > 512) px = 512;

    for (i = 0; i < app->img_count; i++)
        if (app->img[i].px == px && strcmp(app->img[i].src, src) == 0) {
            app->img[i].s.seen = app->ctx->seq;
            return app->img[i].s.tex ? &app->img[i] : NULL;
        }

    slot = (struct img_slot *)slot_take(app, app->img, sizeof app->img[0],
                                        &app->img_count, app->img_cap);
    if (!slot) slot = img_grow(app);
    if (!slot) return NULL;

    SDL_strlcpy(rel, src, sizeof(rel));
    q = strchr(rel, '?');
    if (q) {
        char *opts = (char *)q + 1;
        char *cut = (char *)q;
        const char *tok;
        *cut = 0;
        for (tok = SDL_strtok_r(opts, "&", &opts); tok;
             tok = SDL_strtok_r(NULL, "&", &opts)) {
            if (SDL_strncmp(tok, "stroke=", 7) == 0)
                parse_colour(tok + 7, ocol, sizeof(ocol));
            else if (SDL_strncmp(tok, "fill=", 5) == 0)
                parse_colour(tok + 5, icol, sizeof(icol));
            else if (SDL_strncmp(tok, "sw=", 3) == 0)
                swk = (float)SDL_atof(tok + 3);
        }
    }

    surf = reaktor_svg_surface_path(icon_source(app, rel, found, sizeof(found)),
                                    px, ocol[0] ? ocol : NULL,
                                    icol[0] ? icol : NULL, swk);

    SDL_strlcpy(slot->src, src, sizeof(slot->src));
    slot->px     = px;
    slot->s.tex  = NULL;
    slot->w      = slot->h = 0;
    slot->s.seen = app->ctx->seq;

    if (!surf) return NULL;

    w = plutovg_surface_get_width(surf);
    h = plutovg_surface_get_height(surf);
    stride = plutovg_surface_get_stride(surf);
    reaktor_unpremultiply(plutovg_surface_get_data(surf), w, h, stride);

    sdlsurf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888,
                                    plutovg_surface_get_data(surf), stride);
    tex = sdlsurf ? SDL_CreateTextureFromSurface(app->ren, sdlsurf) : NULL;
    if (sdlsurf) SDL_DestroySurface(sdlsurf);
    plutovg_surface_destroy(surf);

    if (!tex) return NULL;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    slot->s.tex = tex;
    slot->w     = w;
    slot->h     = h;
    return slot;
}

struct nk_image
icon_over(App *app, const char *src, int px, float over)
{
    int raster = (int)(px * reaktor_scale() * over + 0.5f);
    struct img_slot *slot = img_lookup(app, src, raster);

    if (!slot) return blank_image(app);
    return nk_subimage_ptr(slot->s.tex, (nk_ushort)slot->w, (nk_ushort)slot->h,
                           nk_rect(0.0f, 0.0f, (float)slot->w, (float)slot->h));
}

struct nk_image
icon(App *app, const char *src, int px)
{
    return icon_over(app, src, px, 2.0f);
}

struct nk_image
reaktor_svg(App *app, const char *src, int px)
{
    return icon(app, src, px);
}

void
image_centred(struct nk_context *ctx, struct nk_image im, int px)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    float s = (float)px;
    struct nk_rect r = nk_rect(b.x + (b.w - s) * 0.5f,
                               b.y + (b.h - s) * 0.5f, s, s);

    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
    nk_spacing(ctx, 1);
}

static struct nk_image
round_mask(App *app, int r, int t)
{
    const int ss = 4;
    int d = 2 * r, x, y, i;
    float in = (float)(r - t);
    struct round_slot *slot;
    SDL_Surface *surf;
    SDL_Texture *tex;
    unsigned char *px;

    for (i = 0; i < app->round_count; i++)
        if (app->round[i].r == r && app->round[i].t == t) {
            app->round[i].s.seen = app->ctx->seq;
            return app->round[i].s.tex
                 ? nk_subimage_ptr(app->round[i].s.tex, (nk_ushort)d,
                                   (nk_ushort)d,
                                   nk_rect(0.0f, 0.0f, (float)d, (float)d))
                 : nk_image_id(0);
        }
    slot = (struct round_slot *)slot_take(app, app->round, sizeof app->round[0],
                                          &app->round_count, ROUND_CACHE_MAX);
    if (!slot) return nk_image_id(0);

    surf = SDL_CreateSurface(d, d, SDL_PIXELFORMAT_ARGB8888);
    tex  = NULL;
    if (surf) {
        px = (unsigned char *)surf->pixels;
        for (y = 0; y < d; y++) {
            for (x = 0; x < d; x++) {
                int sx, sy, hit = 0;
                unsigned char *p = px + (size_t)y * surf->pitch + x * 4;

                for (sy = 0; sy < ss; sy++) {
                    for (sx = 0; sx < ss; sx++) {
                        float fx = (float)x + ((float)sx + 0.5f) / (float)ss
                                 - (float)r;
                        float fy = (float)y + ((float)sy + 0.5f) / (float)ss
                                 - (float)r;
                        float q = fx * fx + fy * fy;

                        if (q <= (float)r * (float)r &&
                            (t <= 0 || in <= 0.0f || q > in * in))
                            hit++;
                    }
                }
                p[0] = p[1] = p[2] = 255;
                p[3] = (unsigned char)((hit * 255) / (ss * ss));
            }
        }
        tex = SDL_CreateTextureFromSurface(app->ren, surf);
        SDL_DestroySurface(surf);
    }
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    }
    slot->r      = r;
    slot->t      = t;
    slot->s.tex  = tex;
    slot->s.seen = app->ctx->seq;
    if (!tex) return nk_image_id(0);
    return nk_subimage_ptr(tex, (nk_ushort)d, (nk_ushort)d,
                           nk_rect(0.0f, 0.0f, (float)d, (float)d));
}

static int
mask_px(float logical)
{
    int n = reaktor_px(logical);

    return n < 1 ? 1 : n;
}

static nk_size
splice_fill(App *app, nk_size at, struct nk_rect clip)
{
    struct nk_context *ctx = app->ctx;
    struct nk_command_rect_filled f =
        *(struct nk_command_rect_filled *)((nk_byte *)ctx->memory.memory.ptr + at);
    struct nk_command_buffer run;
    nk_byte *base;
    nk_size first;

    SDL_zero(run);
    run.base         = &ctx->memory;
    run.use_clipping = NK_CLIPPING_OFF;
    run.clip         = clip;
    run.begin = run.end = run.last = ctx->memory.allocated;
    nk_push_scissor(&run, clip);
    first = run.last;
    reaktor_fill_round(app, &run, nk_rect(f.x, f.y, f.w, f.h), f.rounding, f.color);
    base = (nk_byte *)ctx->memory.memory.ptr;
    ((struct nk_command *)(base + run.last))->next = f.header.next;
    ((struct nk_command *)(base + at))->type = NK_COMMAND_NOP;
    ((struct nk_command *)(base + at))->next = first;
    return run.last;
}

static void
mask_rounded_fills(App *app)
{
    struct nk_context *ctx = app->ctx;
    const struct nk_command *c = nk__begin(ctx);
    struct nk_rect clip = REAKTOR_NO_CLIP;
    nk_size end = ctx->memory.allocated, at, next, tail = 0;
    int spliced = 0;

    if (!c) return;
    at = (nk_size)((const nk_byte *)c - (const nk_byte *)ctx->memory.memory.ptr);
    for (;;) {
        struct nk_command *cmd =
            (struct nk_command *)((nk_byte *)ctx->memory.memory.ptr + at);

        next = cmd->next;
        tail = at;
        if (cmd->type == NK_COMMAND_SCISSOR) {
            const struct nk_command_scissor *s =
                (const struct nk_command_scissor *)cmd;

            clip = nk_rect(s->x, s->y, s->w, s->h);
        } else if (cmd->type == NK_COMMAND_RECT_FILLED &&
                   ((struct nk_command_rect_filled *)cmd)->rounding &&
                   ((struct nk_command_rect_filled *)cmd)->color.a) {
            tail = splice_fill(app, at, clip);
            spliced = 1;
        }
        if (next >= end) break;
        at = next;
    }
    /* What ended the list at the old end must end it at the new one. */
    if (spliced)
        ((struct nk_command *)((nk_byte *)ctx->memory.memory.ptr + tail))->next =
            ctx->memory.allocated;
}

/* Rounded shapes are masks; a feathered rect seams against them. */
void
reaktor_render(App *app)
{
    SDL_SetRenderDrawColor(app->ren, app->clear.r, app->clear.g, app->clear.b,
                           app->clear.a);
    SDL_RenderClear(app->ren);
    mask_rounded_fills(app);
    nk_sdl_render_ex(app->ctx, NK_ANTI_ALIASING_OFF,
                     app->aa && !(app->renderer_is_sw && app->sw_noaa)
                     ? NK_ANTI_ALIASING_ON : NK_ANTI_ALIASING_OFF);
}

struct nk_rect
reaktor_rect_trunc(struct nk_rect b)
{
    return nk_rect((float)(int)b.x, (float)(int)b.y, (float)(int)b.w, (float)(int)b.h);
}

static struct nk_rect
whole_px(struct nk_rect b)
{
    float x0 = (float)(int)(b.x + 0.5f);
    float y0 = (float)(int)(b.y + 0.5f);
    float x1 = (float)(int)(b.x + b.w + 0.5f);
    float y1 = (float)(int)(b.y + b.h + 0.5f);

    return nk_rect(x0, y0, x1 - x0, y1 - y0);
}

static int
corner_px(struct nk_rect b, float rounding)
{
    int r = (int)(rounding + 0.5f);

    if (2 * r > (int)b.w) r = (int)b.w / 2;
    if (2 * r > (int)b.h) r = (int)b.h / 2;
    return r;
}

static void
draw_corners(struct nk_command_buffer *cv, struct nk_image mask, int m,
             struct nk_rect b, int r, struct nk_color col)
{
    float M = (float)m, D = (float)(2 * m), R = (float)r;
    int i;

    for (i = 0; i < 4; i++) {
        struct nk_rect at = nk_rect((i & 1) ? b.x + b.w - R : b.x,
                                    (i & 2) ? b.y + b.h - R : b.y, R, R);
        struct nk_image q = nk_subimage_handle(
            mask.handle, (nk_ushort)D, (nk_ushort)D,
            nk_rect((i & 1) ? M : 0.0f, (i & 2) ? M : 0.0f, M, M));

        nk_draw_image(cv, at, &q, col);
    }
}

void
reaktor_fill_round(App *app, struct nk_command_buffer *cv, struct nk_rect b,
                   float rounding, struct nk_color col)
{
    struct nk_image disc;
    float R, D;
    int r, m;

    if (b.w <= 0.0f || b.h <= 0.0f || !col.a) return;
    b = whole_px(b);
    if (b.w <= 0.0f || b.h <= 0.0f) return;
    r = corner_px(b, rounding);
    if (r < 1) { nk_fill_rect(cv, b, 0.0f, col); return; }
    m = mask_px((float)r);
    disc = round_mask(app, m, 0);
    if (!disc.handle.ptr) { nk_fill_rect(cv, b, (float)r, col); return; }

    draw_corners(cv, disc, m, b, r, col);
    R = (float)r;
    D = 2.0f * R;
    if (b.h > D)
        nk_fill_rect(cv, nk_rect(b.x, b.y + R, b.w, b.h - D), 0.0f, col);
    if (b.w > D) {
        nk_fill_rect(cv, nk_rect(b.x + R, b.y, b.w - D, R), 0.0f, col);
        nk_fill_rect(cv, nk_rect(b.x + R, b.y + b.h - R, b.w - D, R), 0.0f, col);
    }
}


static void
centre_glyphs_optically(struct nk_font *f)
{
    const struct nk_font_glyph *ex;
    float shift;
    nk_rune i;

    if (!f || !f->glyphs || f->info.glyph_count == 0) return;
    ex = nk_font_find_glyph(f, 'x');
    if (!ex) return;

    shift = f->info.height * 0.5f - (ex->y0 + ex->y1) * 0.5f;
    if (shift > -0.01f && shift < 0.01f) return;

    for (i = 0; i < f->info.glyph_count; i++) {
        f->glyphs[i].y0 += shift;
        f->glyphs[i].y1 += shift;
    }
}

static void
nbsp_as_space(struct nk_font *f)
{
    const struct nk_font_glyph *sp;
    struct nk_font_glyph       *nbsp;

    if (!f || !f->glyphs) return;
    sp   = nk_font_find_glyph(f, 0x20);
    nbsp = (struct nk_font_glyph *)nk_font_find_glyph(f, 0xA0);
    if (!sp || !nbsp || nbsp == sp || nbsp == f->fallback) return;
    *nbsp = *sp;
    nbsp->codepoint = 0xA0;
}

static void
finish_face(struct nk_font *f, float height, const char *path)
{
    if (!f) return;
    f->handle.height = height;
    centre_glyphs_optically(f);
    nbsp_as_space(f);
    /* After centering: the Text module reads the baseline. */
    reaktor_text_attach_font(f, path);
}

const struct nk_user_font *
pick_font(App *app, int px, int bold)
{
    int best = 0, i, bd = 1 << 30;

    if (px <= 0) px = FONT_SIZE;
    for (i = 0; i < FONT_STEPS; i++) {
        int d = g_font_px[i] > px ? g_font_px[i] - px : px - g_font_px[i];
        if (d < bd) { bd = d; best = i; }
    }
    if (bold && app->bolds[best]) return &app->bolds[best]->handle;
    if (app->faces[best]) return &app->faces[best]->handle;
    return app->ctx->style.font;
}

static const char *
face_label(const char *name)
{
    const char *base = reaktor_path_leaf(name);

    return SDL_strncmp(base, "Aileron-", 8) == 0 ? "Aileron" : base;
}

static char *
take_face(const char *name, size_t *size)
{
    char *b = reaktor_asset_load(name, size);

    if (b && !reaktor_font_valid(b, *size)) {
        reaktor_free(b);
        b = NULL;
    }
    return b;
}

static char *
load_face(const char **name, const char *stand_in, size_t *size)
{
    char *b = take_face(*name, size);

    if (b || SDL_strcmp(*name, stand_in) == 0) return b;
    SDL_Log("%s is not a font that can be read; %s stands in", *name, stand_in);
    *name = stand_in;
    return take_face(*name, size);
}

void
rebuild_font(App *app)
{
    struct nk_font_atlas *atlas;
    struct nk_font *font = NULL;
    size_t size = 0, bsize = 0;
    char *ttf, *bttf = NULL;
    int is_default = 0, i;

    ttf = load_face(&app->font_face, FONT_FILE, &size);
    if (app->atlas && !ttf) {
        SDL_Log("could not load %s; keeping the font already baked",
                app->font_face);
        return;
    }
    if (ttf) bttf = load_face(&app->font_face_bold, FONT_BOLD_FILE, &bsize);

    /* The Text module draws in the main window only. */
    if (!app->secondary) reaktor_text_attach_font(NULL, NULL);
    if (app->atlas) {
        nk_font_atlas_clear(app->atlas);
        SDL_memset(app->faces, 0, sizeof(app->faces));
        SDL_memset(app->bolds, 0, sizeof(app->bolds));
        app->face_bold = NULL;
    }
    SDL_free(app->glyphs);
    app->glyphs = NULL;

    atlas = nk_sdl_font_stash_begin(app->ctx);
    app->atlas = atlas;

    if (ttf) {
        struct nk_font_config cfg = nk_font_config(0);

        app->glyphs = reaktor_locale_glyphs((const unsigned char *)ttf);

        cfg.oversample_h = 1;
        cfg.oversample_v = 1;
        cfg.pixel_snap   = 1;
        if (app->glyphs) cfg.range = app->glyphs;
        for (i = 0; i < FONT_STEPS; i++) {
            app->faces[i] = nk_font_atlas_add_from_memory(
                atlas, ttf, size, (float)reaktor_px(g_font_px[i]), &cfg);
            if (g_font_px[i] == FONT_SIZE) font = app->faces[i];
            if (!font) font = app->faces[i];
        }
        for (i = 0; bttf && i < FONT_STEPS; i++)
            app->bolds[i] = nk_font_atlas_add_from_memory(
                atlas, bttf, bsize, (float)reaktor_px(g_font_px[i]), &cfg);
        app->face_bold = app->bolds[0];
        if (!font)
            SDL_snprintf(app->font_status, sizeof(app->font_status),
                         "FALLBACK (ProggyClean) - could not load %s",
                         app->font_face);
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "FALLBACK - could not load %s", app->font_face);
    }
    /* The atlas keeps its own copy of each. */
    reaktor_free(ttf);
    reaktor_free(bttf);

    if (!font) {
        font = nk_font_atlas_add_default(atlas, (float)reaktor_px(FONT_SIZE), NULL);
        is_default = 1;
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "%s, %d sizes %d-%dpx%s", face_label(app->font_face), FONT_STEPS,
                     reaktor_px(g_font_px[0]), reaktor_px(g_font_px[FONT_STEPS - 1]),
                     app->bolds[0] ? ", bold at each" : "");
    }

    nk_sdl_font_stash_end(app->ctx);

    for (i = 0; i < FONT_STEPS; i++) {
        finish_face(app->faces[i], (float)g_font_px[i],
                    app->secondary ? NULL : app->font_face);
        finish_face(app->bolds[i], (float)g_font_px[i],
                    app->secondary ? NULL : app->font_face_bold);
    }
    if (is_default) finish_face(font, (float)FONT_SIZE, NULL);

    nk_font_atlas_cleanup(atlas);

    app->atlas_w = app->atlas_h = app->atlas_bpp = 0;
    if (font && font->texture.ptr) {
        SDL_Texture *tex = (SDL_Texture *)font->texture.ptr;
        float tw = 0.0f, th = 0.0f;
        if (SDL_GetTextureSize(tex, &tw, &th)) {
            app->atlas_w = (int)tw;
            app->atlas_h = (int)th;
        }
        app->atlas_bpp = SDL_BYTESPERPIXEL(tex->format);
    }
    if (font) nk_style_set_font(app->ctx, &font->handle);
}

void
apply_render_scale(App *app)
{
    float s = reaktor_scale();
    SDL_SetRenderScale(app->ren, s, s);
}

SDL_Surface *
reaktor_icon_surface(const char *name, int px)
{
    plutovg_surface_t *svg;
    SDL_Surface *view, *copy = NULL;
    int w, h, stride;

    svg = reaktor_svg_surface_path(name, px, NULL, NULL, 0.0f);
    if (!svg && SDL_strcmp(name, REAKTOR_MARK) != 0)
        svg = reaktor_svg_surface_path(REAKTOR_MARK, px, NULL, NULL, 0.0f);
    if (!svg) return NULL;

    w      = plutovg_surface_get_width(svg);
    h      = plutovg_surface_get_height(svg);
    stride = plutovg_surface_get_stride(svg);
    reaktor_unpremultiply(plutovg_surface_get_data(svg), w, h, stride);

    view = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888,
                                 plutovg_surface_get_data(svg), stride);
    if (view) {
        copy = SDL_DuplicateSurface(view);
        SDL_DestroySurface(view);
    }
    plutovg_surface_destroy(svg);
    return copy;
}

void
reaktor_set_window_icon(SDL_Window *win, const char *name)
{
    SDL_Surface *ico = reaktor_icon_surface(name, 64);

    if (!ico) return;
    SDL_SetWindowIcon(win, ico);
    SDL_DestroySurface(ico);
}

void
reaktor_edge_round(App *app, struct nk_command_buffer *cv, struct nk_rect b,
                   float rounding, float width, struct nk_color col)
{
    struct nk_image ring = nk_image_id(0);
    float R, T;
    int r, t, m;

    if (b.w <= 0.0f || b.h <= 0.0f || width <= 0.0f || !col.a) return;
    b = whole_px(b);
    t = (int)(width + 0.5f);
    if (t < 1) t = 1;
    if (2.0f * (float)t >= b.w || 2.0f * (float)t >= b.h) {
        reaktor_fill_round(app, cv, b, rounding, col);
        return;
    }
    r = corner_px(b, rounding);
    m = mask_px((float)r);
    if (r >= 1) ring = round_mask(app, m, mask_px((float)t));
    if (!ring.handle.ptr) r = 0;
    if (r) draw_corners(cv, ring, m, b, r, col);

    R = (float)r;
    T = (float)t;
    if (r >= t) {
        float M = (float)m, D = 2.0f * M;
        struct nk_image top = nk_subimage_handle(ring.handle, (nk_ushort)D, (nk_ushort)D,
                                                 nk_rect(M - 1.0f, 0.0f, 1.0f, M));
        struct nk_image low = nk_subimage_handle(ring.handle, (nk_ushort)D, (nk_ushort)D,
                                                 nk_rect(M - 1.0f, M, 1.0f, M));
        struct nk_image lft = nk_subimage_handle(ring.handle, (nk_ushort)D, (nk_ushort)D,
                                                 nk_rect(0.0f, M - 1.0f, M, 1.0f));
        struct nk_image rgt = nk_subimage_handle(ring.handle, (nk_ushort)D, (nk_ushort)D,
                                                 nk_rect(M, M - 1.0f, M, 1.0f));

        if (b.w > 2.0f * R) {
            nk_draw_image(cv, nk_rect(b.x + R, b.y, b.w - 2.0f * R, R), &top, col);
            nk_draw_image(cv, nk_rect(b.x + R, b.y + b.h - R, b.w - 2.0f * R, R),
                          &low, col);
        }
        if (b.h > 2.0f * R) {
            nk_draw_image(cv, nk_rect(b.x, b.y + R, R, b.h - 2.0f * R), &lft, col);
            nk_draw_image(cv, nk_rect(b.x + b.w - R, b.y + R, R, b.h - 2.0f * R),
                          &rgt, col);
        }
        return;
    }
    /* Square, or a radius under the width: no pixel is covered twice. */
    if (r) {
        nk_fill_rect(cv, nk_rect(b.x + R, b.y, b.w - 2.0f * R, R), 0.0f, col);
        nk_fill_rect(cv, nk_rect(b.x + R, b.y + b.h - R, b.w - 2.0f * R, R),
                     0.0f, col);
    }
    nk_fill_rect(cv, nk_rect(b.x, b.y + R, b.w, T - R), 0.0f, col);
    nk_fill_rect(cv, nk_rect(b.x, b.y + b.h - T, b.w, T - R), 0.0f, col);
    nk_fill_rect(cv, nk_rect(b.x, b.y + T, T, b.h - 2.0f * T), 0.0f, col);
    nk_fill_rect(cv, nk_rect(b.x + b.w - T, b.y + T, T, b.h - 2.0f * T), 0.0f, col);
}
