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

void
img_cache_clear(App *app)
{
    int i;

    for (i = 0; i < app->img_count; i++)
        if (app->img[i].tex) SDL_DestroyTexture(app->img[i].tex);
    app->img_count = 0;
    for (i = 0; i < app->round_count; i++)
        if (app->round[i].tex) SDL_DestroyTexture(app->round[i].tex);
    app->round_count = 0;
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

/* A full cache gives up the slot drawn longest ago, never one this frame
   has drawn: its texture is still in the frame's commands. */
static int
img_take(App *app)
{
    unsigned now = app->ctx->seq;
    int i, k = -1;

    if (app->img_count < IMG_CACHE_MAX) return app->img_count++;
    for (i = 0; i < IMG_CACHE_MAX; i++)
        if (app->img[i].seen != now &&
            (k < 0 || now - app->img[i].seen > now - app->img[k].seen))
            k = i;
    if (k >= 0 && app->img[k].tex) SDL_DestroyTexture(app->img[k].tex);
    return k;
}

static int
round_take(App *app)
{
    unsigned now = app->ctx->seq;
    int i, k = -1;

    if (app->round_count < ROUND_CACHE_MAX) return app->round_count++;
    for (i = 0; i < ROUND_CACHE_MAX; i++)
        if (app->round[i].seen != now &&
            (k < 0 || now - app->round[i].seen > now - app->round[k].seen))
            k = i;
    if (k >= 0 && app->round[k].tex) SDL_DestroyTexture(app->round[k].tex);
    return k;
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
            app->img[i].seen = app->ctx->seq;
            return app->img[i].tex ? &app->img[i] : NULL;
        }

    if ((i = img_take(app)) < 0) return NULL;
    slot = &app->img[i];

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
    slot->px   = px;
    slot->tex  = NULL;
    slot->w    = slot->h = 0;
    slot->seen = app->ctx->seq;

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

    slot->tex = tex;
    slot->w   = w;
    slot->h   = h;
    return slot;
}

struct nk_image
icon_over(App *app, const char *src, int px, float over)
{
    int raster = (int)(px * reaktor_scale() * over + 0.5f);
    struct img_slot *slot = img_lookup(app, src, raster);

    if (!slot) return nk_image_id(0);
    return nk_subimage_ptr(slot->tex, (nk_ushort)slot->w, (nk_ushort)slot->h,
                           nk_rect(0.0f, 0.0f, (float)slot->w, (float)slot->h));
}

struct nk_image
icon(App *app, const char *src, int px)
{
    return icon_over(app, src, px, 2.0f);
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

/* A disc of radius r texels, or with t > 0 the ring t wide inside it. */
static struct nk_image
round_mask(App *app, int r, int t)
{
    const int ss = 4;
    int d = 2 * r, x, y, i;
    float in = (float)(r - t);
    SDL_Surface *surf;
    SDL_Texture *tex;
    unsigned char *px;

    for (i = 0; i < app->round_count; i++)
        if (app->round[i].r == r && app->round[i].t == t) {
            app->round[i].seen = app->ctx->seq;
            return app->round[i].tex
                 ? nk_subimage_ptr(app->round[i].tex, (nk_ushort)d,
                                   (nk_ushort)d,
                                   nk_rect(0.0f, 0.0f, (float)d, (float)d))
                 : nk_image_id(0);
        }
    if ((i = round_take(app)) < 0) return nk_image_id(0);

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
    app->round[i].r    = r;
    app->round[i].t    = t;
    app->round[i].tex  = tex;
    app->round[i].seen = app->ctx->seq;
    if (!tex) return nk_image_id(0);
    return nk_subimage_ptr(tex, (nk_ushort)d, (nk_ushort)d,
                           nk_rect(0.0f, 0.0f, (float)d, (float)d));
}

/* Masks are drawn at device resolution. */
static int
mask_px(float logical)
{
    int n = reaktor_px(logical);

    return n < 1 ? 1 : n;
}

/* Rounded shapes are masks; a feathered rect seams against them. */
void
reaktor_render_aa(const App *app, enum nk_anti_aliasing *fill,
                  enum nk_anti_aliasing *line)
{
    *fill = NK_ANTI_ALIASING_OFF;
    *line = app->aa && !(app->renderer_is_sw && app->sw_noaa)
          ? NK_ANTI_ALIASING_ON : NK_ANTI_ALIASING_OFF;
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

    /* b is whole: two corners never share a row. */
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

    if (b.w <= 0.0f || b.h <= 0.0f) return;
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
    const char *base = SDL_strrchr(name, '/');

    if (SDL_strrchr(name, 92) > base) base = SDL_strrchr(name, 92);
    base = base ? base + 1 : name;
    return SDL_strncmp(base, "Aileron-", 8) == 0 ? "Aileron" : base;
}

static char *
load_face(const char **name, const char *stand_in, size_t *size)
{
    char *b = reaktor_asset_load(*name, size);

    if (!b || reaktor_font_valid(b, *size)) return b;
    SDL_Log("%s is not a font; %s stands in", *name, stand_in);
    reaktor_free(b);
    *name = stand_in;
    return reaktor_asset_load(*name, size);
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
icon_surface(const char *name, int px)
{
    plutovg_surface_t *svg;
    SDL_Surface *view, *copy = NULL;
    int w, h, stride;

    svg = reaktor_svg_surface_path(name ? name : REAKTOR_MARK, px, NULL, NULL,
                                   0.0f);
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
set_window_icon(SDL_Window *win, const char *name)
{
    SDL_Surface *ico = icon_surface(name, 64);

    if (!ico) return;
    SDL_SetWindowIcon(win, ico);
    SDL_DestroySurface(ico);
}

/* A border drawn inside b. */
void
reaktor_edge_round(App *app, struct nk_command_buffer *cv, struct nk_rect b,
                   float rounding, float width, struct nk_color col)
{
    struct nk_image ring = nk_image_id(0);
    float R, T, side, inset;
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
        /* The mask's middle texels, so the sides match the corners. */
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
    inset = R < T ? R : T;
    side  = R > T ? R : T;
    if (inset > 0.0f) {
        nk_fill_rect(cv, nk_rect(b.x + R, b.y, b.w - 2.0f * R, inset), 0.0f, col);
        nk_fill_rect(cv, nk_rect(b.x + R, b.y + b.h - inset, b.w - 2.0f * R, inset),
                     0.0f, col);
    }
    if (T > inset) {
        nk_fill_rect(cv, nk_rect(b.x, b.y + inset, b.w, T - inset), 0.0f, col);
        nk_fill_rect(cv, nk_rect(b.x, b.y + b.h - T, b.w, T - inset), 0.0f, col);
    }
    nk_fill_rect(cv, nk_rect(b.x, b.y + side, T, b.h - 2.0f * side), 0.0f, col);
    nk_fill_rect(cv, nk_rect(b.x + b.w - T, b.y + side, T, b.h - 2.0f * side),
                 0.0f, col);
}
