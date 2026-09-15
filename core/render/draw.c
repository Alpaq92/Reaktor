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

static struct img_slot *
img_lookup(App *app, const char *src, int px)
{
    char rel[192], ocol[16] = {0}, icol[16] = {0};
    float swk = 0.0f;
    const char *q;
    plutovg_surface_t *surf;
    SDL_Surface *sdlsurf;
    SDL_Texture *tex;
    int i, w, h, stride;

    if (px < 8) px = 8;
    if (px > 512) px = 512;

    for (i = 0; i < app->img_count; i++)
        if (app->img[i].px == px && strcmp(app->img[i].src, src) == 0)
            return app->img[i].tex ? &app->img[i] : NULL;

    if (app->img_count >= IMG_CACHE_MAX) return NULL;

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

    surf = reaktor_svg_surface_path(rel, px, ocol[0] ? ocol : NULL,
                                    icol[0] ? icol : NULL, swk);

    SDL_strlcpy(app->img[app->img_count].src, src,
                sizeof(app->img[app->img_count].src));
    app->img[app->img_count].px  = px;
    app->img[app->img_count].tex = NULL;
    app->img[app->img_count].w = 0;
    app->img[app->img_count].h = 0;

    if (!surf) { app->img_count++; return NULL; }

    w = plutovg_surface_get_width(surf);
    h = plutovg_surface_get_height(surf);
    stride = plutovg_surface_get_stride(surf);
    reaktor_unpremultiply(plutovg_surface_get_data(surf), w, h, stride);

    sdlsurf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888,
                                    plutovg_surface_get_data(surf), stride);
    tex = sdlsurf ? SDL_CreateTextureFromSurface(app->ren, sdlsurf) : NULL;
    if (sdlsurf) SDL_DestroySurface(sdlsurf);
    plutovg_surface_destroy(surf);

    if (!tex) { app->img_count++; return NULL; }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    app->img[app->img_count].tex = tex;
    app->img[app->img_count].w = w;
    app->img[app->img_count].h = h;
    return &app->img[app->img_count++];
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

static struct nk_image
round_mask(App *app, int r)
{
    const int ss = 4;
    int d = 2 * r, x, y, i;
    SDL_Surface *surf;
    SDL_Texture *tex;
    unsigned char *px;

    for (i = 0; i < app->round_count; i++)
        if (app->round[i].r == r)
            return app->round[i].tex
                 ? nk_subimage_ptr(app->round[i].tex, (nk_ushort)d,
                                   (nk_ushort)d,
                                   nk_rect(0.0f, 0.0f, (float)d, (float)d))
                 : nk_image_id(0);
    if (app->round_count >= ROUND_CACHE_MAX) return nk_image_id(0);

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
                        if (fx * fx + fy * fy <= (float)r * (float)r) hit++;
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
    app->round[app->round_count].r   = r;
    app->round[app->round_count].tex = tex;
    app->round_count++;
    if (!tex) return nk_image_id(0);
    return nk_subimage_ptr(tex, (nk_ushort)d, (nk_ushort)d,
                           nk_rect(0.0f, 0.0f, (float)d, (float)d));
}

void
reaktor_fill_round(App *app, struct nk_command_buffer *cv, struct nk_rect b,
                   float rounding, struct nk_color col)
{
    struct nk_image disc;
    nk_handle h;
    float fr = rounding;
    int r;

    if (b.w <= 0.0f || b.h <= 0.0f) return;

    /* Snapped first, or the pieces round apart. */
    {
        float x0 = (float)(int)(b.x + 0.5f);
        float y0 = (float)(int)(b.y + 0.5f);
        float x1 = (float)(int)(b.x + b.w + 0.5f);
        float y1 = (float)(int)(b.y + b.h + 0.5f);

        b.x = x0; b.y = y0;
        b.w = x1 - x0; b.h = y1 - y0;
        if (b.w <= 0.0f || b.h <= 0.0f) return;
    }

    if (fr > b.w * 0.5f) fr = b.w * 0.5f;
    if (fr > b.h * 0.5f) fr = b.h * 0.5f;
    r = (int)(fr + 0.5f);
    if (r < 1) { nk_fill_rect(cv, b, 0.0f, col); return; }

    disc = round_mask(app, r);
    if (!disc.handle.ptr) { nk_fill_rect(cv, b, (float)r, col); return; }
    h = disc.handle;

    {
        float R = (float)r, d = (float)(2 * r);
        struct nk_image q;
        struct nk_rect corner[4];
        struct nk_rect from[4];
        int i;

        corner[0] = nk_rect(b.x, b.y, R, R);
        corner[1] = nk_rect(b.x + b.w - R, b.y, R, R);
        corner[2] = nk_rect(b.x, b.y + b.h - R, R, R);
        corner[3] = nk_rect(b.x + b.w - R, b.y + b.h - R, R, R);
        from[0] = nk_rect(0.0f, 0.0f, R, R);
        from[1] = nk_rect(R, 0.0f, R, R);
        from[2] = nk_rect(0.0f, R, R, R);
        from[3] = nk_rect(R, R, R, R);
        for (i = 0; i < 4; i++) {
            q = nk_subimage_handle(h, (nk_ushort)d, (nk_ushort)d, from[i]);
            nk_draw_image(cv, corner[i], &q, col);
        }
        if (b.h > d)
            nk_fill_rect(cv, nk_rect(b.x, b.y + R, b.w, b.h - d), 0.0f, col);
        if (b.w > d) {
            nk_fill_rect(cv, nk_rect(b.x + R, b.y, b.w - d, R), 0.0f, col);
            nk_fill_rect(cv, nk_rect(b.x + R, b.y + b.h - R, b.w - d, R),
                         0.0f, col);
        }
    }
}

#define FONT_FILE      "assets/fonts/Aileron-Regular.otf"
#define FONT_BOLD_FILE "assets/fonts/Aileron-Bold.otf"

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

void
rebuild_font(App *app)
{
    struct nk_font_atlas *atlas;
    struct nk_font *font = NULL;
    char path[1024], bpath[1024];
    int have_font, is_default = 0, i;

    have_font = reaktor_path(path, sizeof(path), FONT_FILE);
    if (app->atlas && !have_font) {
        SDL_Log("could not resolve %s; keeping the font already baked",
                FONT_FILE);
        return;
    }

    reaktor_text_attach_font(NULL, NULL);
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

    if (have_font) {
        struct nk_font_config cfg = nk_font_config(0);
        const char *slash = SDL_strrchr(path, '/');
        const char *bslash = SDL_strrchr(path, 92);

        if (bslash > slash) slash = bslash;
        SDL_snprintf(bpath, sizeof(bpath), "%.*s%s",
                     slash ? (int)(slash - path + 1) : 0, path,
                     SDL_strrchr(FONT_BOLD_FILE, '/') + 1);
        app->glyphs = reaktor_locale_glyphs(path);

        cfg.oversample_h = 1;
        cfg.oversample_v = 1;
        cfg.pixel_snap   = 1;
        if (app->glyphs) cfg.range = app->glyphs;
        for (i = 0; i < FONT_STEPS; i++) {
            app->faces[i] = nk_font_atlas_add_from_file(
                atlas, path, (float)reaktor_px(g_font_px[i]), &cfg);
            if (g_font_px[i] == FONT_SIZE) font = app->faces[i];
            if (!font) font = app->faces[i];
        }
        for (i = 0; i < FONT_STEPS; i++)
            app->bolds[i] = nk_font_atlas_add_from_file(
                atlas, bpath, (float)reaktor_px(g_font_px[i]), &cfg);
        app->face_bold = app->bolds[0];
        if (!font)
            SDL_snprintf(app->font_status, sizeof(app->font_status),
                         "FALLBACK (ProggyClean) - could not load %s", path);
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "FALLBACK - could not resolve %s", FONT_FILE);
    }

    if (!font) {
        font = nk_font_atlas_add_default(atlas, (float)reaktor_px(FONT_SIZE), NULL);
        is_default = 1;
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "Aileron, %d sizes %d-%dpx%s", FONT_STEPS,
                     reaktor_px(g_font_px[0]), reaktor_px(g_font_px[FONT_STEPS - 1]),
                     app->bolds[0] ? ", bold at each" : "");
    }

    nk_sdl_font_stash_end(app->ctx);

    for (i = 0; i < FONT_STEPS; i++) {
        finish_face(app->faces[i], (float)g_font_px[i], path);
        finish_face(app->bolds[i], (float)g_font_px[i], bpath);
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

void
set_window_icon(SDL_Window *win)
{
    plutovg_surface_t *svg;
    SDL_Surface *ico;
    int w, h, stride;

    svg = reaktor_svg_surface_path(REAKTOR_MARK, 64, NULL, NULL, 0.0f);
    if (!svg) return;

    w      = plutovg_surface_get_width(svg);
    h      = plutovg_surface_get_height(svg);
    stride = plutovg_surface_get_stride(svg);
    reaktor_unpremultiply(plutovg_surface_get_data(svg), w, h, stride);

    ico = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888,
                                plutovg_surface_get_data(svg), stride);
    if (ico) {
        SDL_SetWindowIcon(win, ico);
        SDL_DestroySurface(ico);
    }
    plutovg_surface_destroy(svg);
}
