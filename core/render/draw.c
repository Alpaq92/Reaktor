/* draw.c - images, the rounded-rect primitive, fonts, and the window icon.
 *
 * Lifted out of main.c unchanged. Everything here was already one section or
 * another of that file; the only edits were the ones a file boundary forces.
 */
#include "internal.h"

/* --- images -------------------------------------------------------------
 * An icon path may carry "?stroke=#rrggbb&fill=#rrggbb" to recolour the SVG at
 * load time, since CSS cannot reach inside one, and "&sw=<k>" to multiply the
 * declared stroke - what a glyph at titlebar size needs to stop reading as a
 * hairline. The query string is the cache key, so two weights are two slots. */

/* A "#rrggbb" out of the query string. It used to take an Open-Color family
 * and shade, which is why a palette submodule was carried; the stylesheet's
 * own tokens do the job and follow the theme. */
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
    /* The disc masks are white and tinted when drawn, so they outlive a
     * scheme change - but not the renderer, and this is where that is torn
     * down. */
    for (i = 0; i < app->round_count; i++)
        if (app->round[i].tex) SDL_DestroyTexture(app->round[i].tex);
    app->round_count = 0;
}

/* Rasterised at the size actually drawn, times the display scale, so slots are
 * keyed by src *and* size. Returns NULL on error. */
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

    /* Cache the failure too, so a bad src is not retried every frame. */
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

/* `over` is how far above the drawn size the artwork is rasterised: a linear
 * filter downscales cleanly and upscales blurrily, and the widget rect is
 * often taller than the nominal size, so an image goes in at twice. One is
 * for a caller that draws at exactly px and needs the edge where plutovg put
 * it - a resample is a blur, and on a rim one pixel wide it reads as a smear
 * three pixels across rather than a line. */
struct nk_image
icon_over(App *app, const char *src, int px, float over)
{
    int raster = (int)(px * reaktor_scale() * over + 0.5f);
    struct img_slot *slot = img_lookup(app, src, raster);

    if (!slot) return nk_image_id(0);
    /* A sub-image with real dimensions, not nk_image_ptr, which leaves w, h
     * and the source region zero. Widgets fill those in themselves;
     * nk_draw_image does not, and a degenerate region drew a white quad. */
    return nk_subimage_ptr(slot->tex, (nk_ushort)slot->w, (nk_ushort)slot->h,
                           nk_rect(0.0f, 0.0f, (float)slot->w, (float)slot->h));
}

struct nk_image
icon(App *app, const char *src, int px)
{
    return icon_over(app, src, px, 2.0f);
}

/* Draws an image centred at exactly px, and consumes the widget slot. nk_image
 * stretches to fill its rect, so the raster size asked for changes nothing. */
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

/* --- a rounded rect that is actually round -------------------------------
 *
 * Nuklear fills one as a polygon, and with fill feathering off - which is what
 * the software renderer wants, see nk_sdl_render_ex - the arc steps in whole
 * pixels. A stroke over it grades the step, but only by a fixed half: the pass
 * that puts every vertex on the pixel grid quantises the stroke's feather onto
 * the same staircase, so the corner reads as stairs with a halo rather than a
 * curve. Every curve on a page went to a texture for this reason; a rounded
 * rect is the one that has no artwork to go to.
 *
 * So the mask is computed instead. One disc of 2r, white, its alpha the
 * coverage of the circle - sampled four by four, which is finer than the eye
 * asks of a corner - and each quadrant of it drawn into a corner as a
 * sub-image, with three plain rects for what is left. Exact at any radius,
 * identical on both backends, and one texture per radius for the session. */
static struct nk_image
round_mask(App *app, int r)
{
    const int ss = 4;                       /* samples per axis */
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
                p[0] = p[1] = p[2] = 255;   /* B, G, R - tinted when drawn */
                p[3] = (unsigned char)((hit * 255) / (ss * ss));
            }
        }
        tex = SDL_CreateTextureFromSurface(app->ren, surf);
        SDL_DestroySurface(surf);
    }
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        /* Drawn one to one, so nothing is sampled between texels. */
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
        /* The cross between the four caps: a band the full width and two
         * short ones above and below it. Either can be empty - at a radius of
         * half the height the shape is a pill, at half of both a disc - and a
         * zero-extent rect is not something to hand the rasteriser. */
        if (b.h > d)
            nk_fill_rect(cv, nk_rect(b.x, b.y + R, b.w, b.h - d), 0.0f, col);
        if (b.w > d) {
            nk_fill_rect(cv, nk_rect(b.x + R, b.y, b.w - d, R), 0.0f, col);
            nk_fill_rect(cv, nk_rect(b.x + R, b.y + b.h - R, b.w - d, R), 0.0f,
                         col);
        }
    }
}

/* --- fonts --------------------------------------------------------------- */
/* Nuklear's default is ProggyClean, a 13px bitmap font; baking a TTF through
 * stb_truetype is what makes text look like text.
 *
 * Aileron, CC0, vendored under assets/fonts/ - the one exception to this
 * project's submodule rule. Its download has no licence file, so the terms are
 * read off the font's own name table into Aileron-Notice.txt. An OTF:
 * stb_truetype reads CFF outlines too. See docs/NOTICE.md. */
#define FONT_FILE      "assets/fonts/Aileron-Regular.otf"
#define FONT_BOLD_FILE "assets/fonts/Aileron-Bold.otf"

/* Nearest baked size. The bold is baked at one size only, TITLE_PX, for the
 * title: a whole second family would put another FONT_STEPS glyph sets in the
 * atlas and roughly double the largest allocation here. A bold asked for at
 * any other size gets the regular. */
const struct nk_user_font *
pick_font(App *app, int px, int bold)
{
    int best = 0, i, bd = 1 << 30;

    if (px <= 0) px = FONT_SIZE;
    if (bold && px == TITLE_PX && app->face_bold)
        return &app->face_bold->handle;
    for (i = 0; i < FONT_STEPS; i++) {
        int d = g_font_px[i] > px ? g_font_px[i] - px : px - g_font_px[i];
        if (d < bd) { bd = d; best = i; }
    }
    if (app->faces[best]) return &app->faces[best]->handle;
    return app->ctx->style.font;
}

/* Rebuilds the atlas at the current scale. Layout stays in logical px and
 * never learns that this happened. */
void
rebuild_font(App *app)
{
    struct nk_font_atlas *atlas;
    struct nk_font *font = NULL;
    char path[1024];
    int have_font;

    /* nk_sdl_font_stash_begin calls nk_font_atlas_init, which zeroes the atlas
     * struct - so a second bake drops the previous configs, blobs, fonts and
     * glyphs unfreed, visible only on a display-scale change. The clear frees
     * the nk_font ctx->style.font points at, so the path is resolved first:
     * with nothing to bake, the working atlas is worth keeping. */
    have_font = reaktor_path(path, sizeof(path), FONT_FILE);
    if (app->atlas && !have_font) {
        SDL_Log("could not resolve %s; keeping the font already baked",
                FONT_FILE);
        return;
    }

    if (app->atlas) {
        nk_font_atlas_clear(app->atlas);
        SDL_memset(app->faces, 0, sizeof(app->faces));
        app->face_bold = NULL;
    }
    atlas = nk_sdl_font_stash_begin(app->ctx);
    app->atlas = atlas;

    if (have_font) {
        struct nk_font_config cfg = nk_font_config(0);
        int i;

        /* No oversampling: it rasterises each glyph at several sub-pixel
         * offsets and costs exactly its area. The 3x2 default made the atlas
         * 1024x512 - two megabytes - against 1024x128 here. */
        cfg.oversample_h = 1;
        cfg.oversample_v = 1;
        /* nuklear.h pairs these: "align every character to pixel boundary (if
         * true set oversample (1,1))". */
        cfg.pixel_snap   = 1;
        for (i = 0; i < FONT_STEPS; i++) {
            app->faces[i] = nk_font_atlas_add_from_file(
                atlas, path, (float)reaktor_px(g_font_px[i]), &cfg);
            if (g_font_px[i] == FONT_SIZE) font = app->faces[i];
            if (!font) font = app->faces[i];
        }
        /* Baked at the device size for sharpness, reported at the logical one
         * so layout never sees the scale. nk_font_text_width and the glyph
         * quads both derive their scale from the height handed to them, not
         * from font->scale, so this one field is the whole of it. */
        for (i = 0; i < FONT_STEPS; i++)
            if (app->faces[i])
                app->faces[i]->handle.height = (float)g_font_px[i];
        /* The bold, one size, from the file beside the regular: the path
         * already resolved above, with its basename swapped. */
        {
            const char *slash = SDL_strrchr(path, '/');
            const char *bslash = SDL_strrchr(path, 92);   /* a backslash */
            const char *base = SDL_strrchr(FONT_BOLD_FILE, '/') + 1;
            char bpath[1024];

            if (bslash > slash) slash = bslash;
            SDL_snprintf(bpath, sizeof(bpath), "%.*s%s",
                         slash ? (int)(slash - path + 1) : 0, path, base);
            app->face_bold = nk_font_atlas_add_from_file(
                atlas, bpath, (float)reaktor_px(TITLE_PX), &cfg);
            if (app->face_bold)
                app->face_bold->handle.height = (float)TITLE_PX;
        }
        if (!font)
            SDL_snprintf(app->font_status, sizeof(app->font_status),
                         "FALLBACK (ProggyClean) - could not load %s", path);
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "FALLBACK - could not resolve %s", FONT_FILE);
    }

    /* A silent fallback would be indistinguishable from "the font never
     * loaded", so the outcome is always recorded and shown in diagnostics. */
    if (!font) {
        font = nk_font_atlas_add_default(atlas, (float)reaktor_px(FONT_SIZE), NULL);
        if (font) font->handle.height = (float)FONT_SIZE;
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "Aileron, %d sizes %d-%dpx%s", FONT_STEPS,
                     reaktor_px(g_font_px[0]), reaktor_px(g_font_px[FONT_STEPS - 1]),
                     app->face_bold ? ", bold at one" : "");
    }

    nk_sdl_font_stash_end(app->ctx);
    /* The bake keeps a copy of the whole font file per face - five of the same
     * 27 KB - and nothing reads them once stb_truetype has the outlines. */
    nk_font_atlas_cleanup(atlas);

    /* Off the texture, not the atlas: nk_font_atlas_end clears the baked
     * dimensions when it releases the staging buffers. */
    app->atlas_w = app->atlas_h = app->atlas_bpp = 0;
    if (font && font->texture.ptr) {
        SDL_Texture *tex = (SDL_Texture *)font->texture.ptr;
        float tw = 0.0f, th = 0.0f;
        if (SDL_GetTextureSize(tex, &tw, &th)) {
            app->atlas_w = (int)tw;
            app->atlas_h = (int)th;
        }
        /* Asked, not assumed: the backend picks the format and can fall back,
         * so a hard-coded value would report a quarter of the real size. */
        app->atlas_bpp = SDL_BYTESPERPIXEL(tex->format);
    }
    if (font) nk_style_set_font(app->ctx, &font->handle);
}

/* The paint boundary. Everything above lays out in logical pixels; this is
 * where they become device ones, so a HiDPI display draws the same geometry
 * into more pixels rather than the same pixels into a corner of the window.
 * The font atlas is baked at the device size and its faces then report their
 * logical height, so glyph quads stay logical while sampling a sharp texture -
 * see rebuild_font. */
void
apply_render_scale(App *app)
{
    float s = reaktor_scale();
    SDL_SetRenderScale(app->ren, s, s);
}

/* --- window icon ------------------------------------------------------- */
/* SDL_SetWindowIcon is portable, so this replaces the Win32 HICON path. What
 * Explorer shows is a separate thing: branding/reaktor-icon.ico, linked as a
 * resource, because a file has an icon before it has a process. */
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

    /* plutovg's ARGB32 is B,G,R,A in memory on little-endian, which is what
     * SDL calls ARGB8888. */
    ico = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888,
                                plutovg_surface_get_data(svg), stride);
    if (ico) {
        SDL_SetWindowIcon(win, ico);
        SDL_DestroySurface(ico);
    }
    plutovg_surface_destroy(svg);
}

