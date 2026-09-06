/* main.c - Curie: pure C UI on Nuklear, rendered through SDL3, styled by CSS.
 *
 * The loop is SDL3's *callback* model (SDL_AppInit / SDL_AppIterate /
 * SDL_AppEvent / SDL_AppQuit) rather than a while() loop. That is not a style
 * choice: a browser tab cannot be blocked, so under Emscripten SDL must hand
 * control back each frame. The same source is an ordinary loop natively, so
 * one file serves Windows, macOS, Linux, the BSDs and WASM.
 *
 * How the look is decided
 * -----------------------
 * Nuklear draws the widgets and lays them out. Pure.css says what they look
 * like, and LCUI's libcss parses it: before each widget is drawn, the computed
 * values behind its selector - ".pure-button", or the same with ":hover" - are
 * pushed into nk_style, and popped after. src/style.c is that seam.
 *
 * This replaced a document engine that parsed HTML and laid out a page. That
 * direction asked LCUI for things it does not have (no HTML parser, no
 * inheritance, no rem, no @media, no attribute selectors) and asked Nuklear
 * for nothing but a command buffer. Turning it around uses each for what it is
 * good at, and the CSS that remains - colour, border, radius, padding, font -
 * is exactly the part libcss implements well. */
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nk_common.h"
#include "../third_party/nuklear/demo/sdl3_renderer/nuklear_sdl3_renderer.h"
#include "appicon.h"
#include "theme.h"
#include "metrics.h"
#include "curie.h"
#include "style.h"
#include "ui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#define WINDOW_WIDTH  960      /* logical px; scaled via curie_px() */
#define WINDOW_HEIGHT 680
#define FONT_SIZE     16

/* Pure asks for font-size: 100% on buttons and inherits elsewhere, so 16 does
 * most of the work; the rest of the ladder is here for the headings and small
 * print the screen sets itself. Nearest wins, so a size in between costs no
 * fidelity worth another atlas. */
/* Exactly the sizes something asks for. Every step is a full set of glyphs
 * in the atlas, and 23 was never requested by anything: the largest rule
 * tiny.css has resolves to 16 and the headings ask for 19, so it was a
 * megapixel of texture nothing could ever draw from. */
#define FONT_STEPS 5
static const int g_font_px[FONT_STEPS] = { 12, 13, 14, 16, 19 };

/* One slot per icon per size per theme: switching the scheme rewrites every
 * icon's stroke colour, and the colour is part of the key. The cache is
 * emptied on a theme change rather than sized for the union of both. */
#define IMG_CACHE_MAX 96
#define CARD_W        420

/* Height of the titlebar this app draws for itself when the native one is
 * turned off, and how wide a strip along each edge grabs for a resize. */
#define TITLEBAR_H    36
#define TAB_H         34      /* the strip under it */
#define TAB_PAD_X     14      /* either side of a tab's label */
#define RESIZE_EDGE    6
#define CTL_SIZE      28   /* the control's square hit area */
/* On top of each of these Nuklear inserts its own 4px between columns, and
 * the group pads its contents again, so the gap on screen is about ten pixels
 * wider than the number here - which is why the mark sat further from the
 * edge than from the title while these read 6 and 2. */
#define TITLE_PAD      1   /* before the app mark */
#define TITLE_GAP      6   /* between the app mark and the name */
#define MARK_SIZE     18   /* the app mark, drawn size */

/* Ionicons draw a 32-unit stroke on a 512 viewBox - 6.25% of the glyph - so
 * below 16px the line falls under one pixel and anti-aliases to grey. That,
 * not the artwork, is why a glyph looked "greyed out": at 12px the minimise
 * faded, and dropping the maximise to 14px faded that one instead. Every
 * glyph therefore stays at or above 16, and the remaining differences are
 * optical - a cross and a dash read smaller than a square of equal size. */
/* Even sizes, so that (CTL_SIZE - glyph) / 2 is a whole number of pixels.
 * At 17 the padding was 5.5 and the minimise dash - a single horizontal line
 * through the middle of the box - straddled a pixel boundary and came out as
 * one bright row between two dim ones however heavy the stroke was. */
#define GLYPH_MINIMISE 18
#define GLYPH_MAXIMISE 16
#define GLYPH_CLOSE    18

/* Size alone only got them off the grey floor; at this scale the artwork's
 * own line is about one device pixel and reads as a hairline against the bar.
 * Doubling it puts two solid pixels down - see curie_svg_surface_path. */
#define GLYPH_STROKE  2.0f

/* tiny.css keeps its palette and its rules in separate files - its own light
 * and dark builds are nothing but "@import variables-X; @import core" - so a
 * theme here is which variables file is loaded in front of core.css. That is
 * the same shape cssflat.c already resolves, which is why switching costs one
 * string rather than a translation layer.
 *
 * Read from the submodule at runtime, like everything else. */
#define SHEET_COUNT 2
static const char *
theme_sheet(int dark)
{
    return dark ? "third_party/tinycss/src/variables-dark.css"
                : "third_party/tinycss/src/variables-light.css";
}
#define CORE_SHEET "third_party/tinycss/src/core.css"

/* Which scheme the user asked for, as opposed to which one is in force. */
#define THEME_SYSTEM 0
#define THEME_LIGHT  1
#define THEME_DARK   2
static const char *const g_theme_names[3] = { "system", "light", "dark" };

struct img_slot {
    char         src[192];
    int          px;        /* raster size this slot was built at */
    SDL_Texture *tex;
    int          w, h;
};

/* Named, and declared as an incomplete type in ui.h, so a page can be handed
 * an App * without being handed the window, the renderer or the atlas. */
struct App {
    SDL_Window        *win;
    SDL_Renderer      *ren;
    struct nk_context *ctx;
    struct nk_color    clear;
    int                dark;

    /* Decoded SVG sources, keyed by their path. Small and linear: a screen
     * references a handful of icons, not hundreds. */
    struct img_slot img[IMG_CACHE_MAX];
    int img_count;

    /* Nuklear bakes a glyph atlas per size, so a face is baked once per size
     * the stylesheet asks for and selected per draw. */
    struct nk_font *faces[FONT_STEPS];
    char            font_status[160];
    /* The baked atlas, so the diagnostics can say what the text costs: it is
     * one RGBA32 texture and the largest single allocation the app makes. */
    int             atlas_w, atlas_h;

    /* Nuklear owns the editing behaviour - caret, selection, clipboard, IME -
     * but the state lives here rather than inside the widget, because a
     * context menu has to act on it: nk_edit_string keeps its buffer private,
     * nk_edit_buffer takes ours. */
    struct nk_text_edit edit;
    char                edit_buf[128];

    /* Decided while the frame is built - Nuklear knows what is under the
     * pointer only as each widget is emitted - and applied once at the end,
     * so the cursor is not set several times per frame. */
    SDL_Cursor *cur_default, *cur_pointer, *cur_text;
    int         want_cursor, cur_shown;   /* 0 default, 1 pointer, 2 text */

    char render_mode[8];
    int  vsync_on, aa, redraw_always;

    /* How SDL paces SDL_AppIterate: "waitevent", a frame rate, or 0 for
     * uncapped. See the note where it is applied. */
    char frame_rate[16];
    char drag_rate[16];        /* the display's refresh, as a rate string */
    int  first_frame_done;
    int  dragging;             /* a mouse button is held */

    /* Borderless: the window has no native frame, so the titlebar above is
     * drawn like any other widget and SDL_SetWindowHitTest tells the desktop
     * which parts of it drag and which resize. The control rects are recorded
     * as they are emitted, because the hit test runs on the OS's thread and
     * cannot ask Nuklear anything. */
    /* Frames still owed after a click, before the callback rate goes back to
     * "waitevent".
     *
     * Two, not one. A click completes on release, and the release is also
     * where the rate used to be restored - so the frame showing what the
     * click did was not scheduled until some later event arrived, which is
     * the delay the diagnostics link had. But one frame is not enough either:
     * a page is laid out top to bottom, so a link that toggles something
     * halfway down is read *after* the sizes above it were decided. The card
     * grew its diagnostic rows into a height computed before the toggle, and
     * they were clipped. The second frame is the one that lays them out. */
    int            restore_rate;
    int            borderless;
    struct nk_rect ctl[3];     /* minimise, maximise, close */
    int            ctl_n;
    int            want_quit;

    /* Where the text field was on the last painted frame, and whether the
     * current drag started inside it. See the clamp in SDL_AppEvent. */
    struct nk_rect field_rect;
    int            field_rect_valid;
    int            drag_in_field;

    int   theme_mode;          /* THEME_SYSTEM | THEME_LIGHT | THEME_DARK */
    struct nk_color page, card_bg, text;
    /* --text-muted and --links as "#rrggbb", so an icon can be recoloured to
     * match the label beside it or to carry the accent. Held as strings
     * because that is what goes into the SVG before it is parsed. */
    char  icon_hex[10];
    char  accent_hex[10];

    /* Where the interactive widgets ended up on the last painted frame, and
     * what hovering each one actually costs.
     *
     * Two separate questions were being conflated. Every control wants a
     * cursor when the pointer is over it - but only some of them *look*
     * different when hovered. A button does: tiny.css gives it
     * --button-hover. A text link does not; nor does the field, whose hover
     * background is set to its resting colour. Repainting for those was
     * paying a full immediate-mode rebuild to change nothing on screen.
     *
     * So the cursor is decided straight from this list, with no frame at all,
     * and a repaint is asked for only when the pointer crosses into or out of
     * something whose appearance depends on hover. The rects are the ones
     * Nuklear itself laid out, recorded as each widget was emitted, so this is
     * exact rather than a heuristic. */
    struct hot_region {
        struct nk_rect r;
        unsigned char  cursor;    /* 0 default, 1 pointer, 2 text */
        unsigned char  repaint;   /* does hover change what is drawn? */
        /* Emitted inside a popup, which is drawn over the page - so it wins
         * against anything it covers however the two were recorded. */
        unsigned char  top;
        /* Repaint on every move *inside* it, not only on crossing into it.
         * A tooltip is drawn at the pointer, so it has to follow one. */
        unsigned char  track;
    } hot[192];
    int hot_n, hot_last;
    /* What hot_last referred to, kept because hot[] is rebuilt every frame:
     * an index is only meaningful within the frame that filled it, and a menu
     * opening changes how many regions there are. */
    struct nk_rect hot_last_r;
    unsigned char  hot_last_repaint;

    int   dirty;
    int   show_contact;

    /* Which page is on screen, and what the showcase pages remember between
     * frames. Tab 0 is the login card this app began as. */
    int            tab;
    /* Where a showcase page opens, scrolled. Same reason as CURIE_TAB: a
     * screenshot of one section should not depend on synthesising wheel
     * events from outside the process, which the OS delivers unevenly. */
    int            scroll0;
    showcase_state show;
    int   laid_w, laid_h;
    float fps;
    int   fps_frames;
    Uint64 fps_t0;
    /* The gap to the previous drawn frame, and the rate it implies.
     *
     * fps above is frames counted over a second, and that second only ticks
     * on when a frame is drawn - so with frames arriving rarely it takes many
     * seconds to close and reads stale until it does. The interval says the
     * same thing at once, and costs a subtraction on a value already read. */
    Uint64 last_frame_ms;
    float  frame_gap_ms;
    int   n_paints;
    int   style_ms_x100;   /* time the last stylesheet load took */
    /* Where a frame goes, in hundredths of a millisecond: building the UI,
     * converting and submitting it, and waiting on present. Split three ways
     * because guessing which one dominates has been wrong twice. */
    int   build_ms_x100, render_ms_x100, present_ms_x100;
};

static int
env_int(const char *name, int fallback)
{
    const char *v = SDL_getenv(name);
    return (v && *v) ? SDL_atoi(v) : fallback;
}

/* The effective scheme. curie_prefers_dark() is SDL_GetSystemTheme() behind a
 * one-line wrapper, and returns -1 when the platform will not say; an unknown
 * answer is treated as light, which is what a desktop without a preference
 * has always looked like. */
static int
effective_dark(const App *app)
{
    if (app->theme_mode == THEME_LIGHT) return 0;
    if (app->theme_mode == THEME_DARK)  return 1;
    return curie_prefers_dark() > 0;
}

/* Reloads the stylesheets for the current scheme and re-reads the surfaces
 * this file paints itself. Cheap enough to do on a click: the whole set is
 * ~7 KB, and the parse is reported in diagnostics. */
/* Defined further down, with the rest of the CSS seam. */
static void apply_widget_style(App *app);
static void img_cache_clear(App *app);

static void
load_theme(App *app)
{
    char pal[1024], core[1024];
    const char *sheets[SHEET_COUNT];
    unsigned char c[4];
    Uint64 t0, t1;

    app->dark = effective_dark(app);

    if (!curie_path(pal, sizeof(pal), theme_sheet(app->dark)) ||
        !curie_path(core, sizeof(core), CORE_SHEET)) {
        SDL_Log("could not resolve the tiny.css sources");
        return;
    }
    sheets[0] = pal;
    sheets[1] = core;

    t0 = SDL_GetPerformanceCounter();
    if (!curie_style_init(sheets, SHEET_COUNT))
        SDL_Log("stylesheets failed to load; Nuklear defaults apply");
    t1 = SDL_GetPerformanceCounter();
    app->style_ms_x100 = (int)(100000.0 * (double)(t1 - t0) /
                               (double)SDL_GetPerformanceFrequency());

    /* The window and the card are painted by this file, not by a widget, so
     * their colours are read from the same :root the rules use. */
    app->page    = curie_style_token("--background-body", c)
                 ? nk_rgba(c[0], c[1], c[2], c[3]) : nk_rgb(247, 247, 247);
    app->card_bg = curie_style_token("--background", c)
                 ? nk_rgba(c[0], c[1], c[2], c[3]) : nk_rgb(226, 226, 226);
    app->text    = curie_style_token("--text-main", c)
                 ? nk_rgba(c[0], c[1], c[2], c[3]) : nk_rgb(51, 51, 51);
    if (curie_style_token("--text-muted", c))
        SDL_snprintf(app->icon_hex, sizeof(app->icon_hex),
                     "#%02x%02x%02x", c[0], c[1], c[2]);
    else
        SDL_strlcpy(app->icon_hex, "#6a6a6a", sizeof(app->icon_hex));

    if (curie_style_token("--links", c))
        SDL_snprintf(app->accent_hex, sizeof(app->accent_hex),
                     "#%02x%02x%02x", c[0], c[1], c[2]);
    else
        SDL_strlcpy(app->accent_hex, CURIE_BRAND, sizeof(app->accent_hex));

    /* The desktop's own title bar, so a pinned scheme is not contradicted by
     * the frame around it. */
    curie_window_set_dark(
        SDL_GetPointerProperty(SDL_GetWindowProperties(app->win),
                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL),
        app->dark);

    /* Every icon is rasterised with the theme's stroke colour baked into it,
     * so none of the cached textures survive a scheme change. */
    img_cache_clear(app);
    apply_widget_style(app);

    app->clear   = app->page;
    app->dirty   = 1;
}

/* --- images -------------------------------------------------------------
 * An icon path may carry "?stroke=#rrggbb&fill=#rrggbb", which recolours the
 * SVG at load time. Kept from the previous design: it is
 * how an icon follows a palette, since CSS cannot reach inside an SVG.
 * "&sw=<k>" is the same idea for weight: it multiplies the stroke the artwork
 * declares, which is what a glyph drawn at titlebar size needs to stop
 * reading as a hairline. The query string is also the cache key, so two
 * weights of one glyph are simply two slots. */

/* A "#rrggbb" out of the query string. It used to accept an Open-Color
 * family and shade too - "violet-7" - and that was the whole reason a palette
 * submodule was being carried; the stylesheet's own tokens do the job, and
 * unlike a fixed shade they follow the theme. */
static int
parse_colour(const char *spec, char *out, size_t cap)
{
    if (spec[0] != '#' || strlen(spec) >= cap) return 0;
    strcpy(out, spec);
    return 1;
}

static void
img_cache_clear(App *app)
{
    int i;

    for (i = 0; i < app->img_count; i++)
        if (app->img[i].tex) SDL_DestroyTexture(app->img[i].tex);
    app->img_count = 0;
}

/* SVG is resolution-independent, so an icon is rasterised at the size it is
 * actually drawn (times the display scale) rather than once and scaled down.
 * Slots are keyed by src *and* size. Returns NULL on error. */
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

    surf = curie_svg_surface_path(rel, px, ocol[0] ? ocol : NULL,
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
    curie_unpremultiply(plutovg_surface_get_data(surf), w, h, stride);

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

/* Rasterised above the size it is drawn at. An SVG has no intrinsic
 * resolution, and a linear filter downscales cleanly where it upscales
 * blurrily - and the widget rect an icon lands in is often taller than the
 * nominal size, so asking for exactly `px` guaranteed an upscale. Twice the
 * device size covers both. */
static struct nk_image
icon(App *app, const char *src, int px)
{
    int raster = (int)(px * curie_scale() * 2.0f + 0.5f);
    struct img_slot *slot = img_lookup(app, src, raster);

    if (!slot) return nk_image_id(0);
    /* A sub-image with the real dimensions, not nk_image_ptr, which leaves
     * w, h and the source region at zero. Nuklear's widget path fills those
     * in for itself, so nk_image and nk_button_image never noticed; drawing
     * straight onto the canvas with nk_draw_image does not, and a degenerate
     * source region came out as a white quad - which is what the window
     * controls were, while the very same SVGs rasterised correctly. */
    return nk_subimage_ptr(slot->tex, (nk_ushort)slot->w, (nk_ushort)slot->h,
                           nk_rect(0.0f, 0.0f, (float)slot->w, (float)slot->h));
}

/* Draws an image centred at exactly px, and consumes the widget slot.
 *
 * nk_image stretches the image to fill its widget rect, so the drawn size is
 * the slot's size and the raster size asked for makes no difference at all -
 * which is why an 18px mark came out at the titlebar's full row height and a
 * row of 28px icons came out at 48. Painting onto the canvas is the same
 * technique the placeholder text in a field uses. */
static void
image_centred(struct nk_context *ctx, struct nk_image im, int px)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    float s = (float)px;
    struct nk_rect r = nk_rect(b.x + (b.w - s) * 0.5f,
                               b.y + (b.h - s) * 0.5f, s, s);

    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
    nk_spacing(ctx, 1);
}

/* --- fonts --------------------------------------------------------------- */
/* Nuklear's built-in default is ProggyClean, a 13px *bitmap* font: blocky at
 * any real size and unable to scale. Baking a TTF through stb_truetype is what
 * makes text look like text - and this is the path the CC0 Zerove font will
 * use once per-widget font selection is wanted for display type; only
 * FONT_FILE changes. Zerove is unicase (measured: 'a' and 'A' are the same
 * outline at 1434 units), so it is a wordmark face, not a UI face. */
#define FONT_FILE "third_party/nuklear/extra_font/Karla-Regular.ttf"

/* Nearest baked size. `bold` is accepted and ignored: Karla ships regular
 * only, and synthesising a bold by double-striking looks worse than not having
 * one - so font-weight still has no face to select. */
static const struct nk_user_font *
pick_font(App *app, int px, int bold)
{
    int best = 0, i, bd = 1 << 30;

    (void)bold;
    if (px <= 0) px = FONT_SIZE;
    for (i = 0; i < FONT_STEPS; i++) {
        int d = g_font_px[i] > px ? g_font_px[i] - px : px - g_font_px[i];
        if (d < bd) { bd = d; best = i; }
    }
    if (app->faces[best]) return &app->faces[best]->handle;
    return app->ctx->style.font;
}

/* Rebuilds the atlas at the current scale. Layout stays in logical px and
 * never learns that this happened. */
static void
rebuild_font(App *app)
{
    struct nk_font_atlas *atlas;
    struct nk_font *font = NULL;
    char path[1024];

    atlas = nk_sdl_font_stash_begin(app->ctx);

    if (curie_path(path, sizeof(path), FONT_FILE)) {
        struct nk_font_config cfg = nk_font_config(0);
        int i;

        /* 2x1, not 3x2.
         *
         * Oversampling rasterises each glyph several times at sub-pixel
         * offsets so it can be positioned off the pixel grid without
         * shimmering, and it costs exactly its area: 3x2 stores six copies of
         * every glyph at every size, which is what made the atlas a 1024x512
         * RGBA texture - two megabytes, and the largest allocation in the
         * app by an order of magnitude. Horizontal offsets are the ones that
         * matter for horizontal text; the vertical pass buys almost nothing
         * here because rows sit on integer baselines. */
        cfg.oversample_h = 2;
        cfg.oversample_v = 1;
        cfg.pixel_snap   = 0;
        for (i = 0; i < FONT_STEPS; i++) {
            app->faces[i] = nk_font_atlas_add_from_file(
                atlas, path, (float)curie_px(g_font_px[i]), &cfg);
            if (g_font_px[i] == FONT_SIZE) font = app->faces[i];
            if (!font) font = app->faces[i];
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
        font = nk_font_atlas_add_default(atlas, (float)curie_px(FONT_SIZE), NULL);
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "Karla, %d sizes %d-%dpx", FONT_STEPS,
                     curie_px(g_font_px[0]), curie_px(g_font_px[FONT_STEPS - 1]));
    }

    nk_sdl_font_stash_end(app->ctx);

    /* Read off the texture, not off the atlas: nk_font_atlas_end clears the
     * baked dimensions as part of releasing the staging buffers. Every baked
     * face shares the one atlas texture, so any of them will do. */
    app->atlas_w = app->atlas_h = 0;
    if (font && font->texture.ptr) {
        float tw = 0.0f, th = 0.0f;
        if (SDL_GetTextureSize((SDL_Texture *)font->texture.ptr, &tw, &th)) {
            app->atlas_w = (int)tw;
            app->atlas_h = (int)th;
        }
    }
    if (font) nk_style_set_font(app->ctx, &font->handle);
}

/* --- window icon ------------------------------------------------------- */
/* SDL_SetWindowIcon is portable, so this replaces the Win32 HICON path
 * outright. The .ico for the Windows executable resource is still produced at
 * build time by tools/mkicon.c, from this same code path. */
static void
set_window_icon(SDL_Window *win)
{
    plutovg_surface_t *svg;
    SDL_Surface *ico;
    int w, h, stride;

    svg = curie_svg_surface("browsers-outline", 64, CURIE_BRAND, NULL);
    if (!svg) return;

    w      = plutovg_surface_get_width(svg);
    h      = plutovg_surface_get_height(svg);
    stride = plutovg_surface_get_stride(svg);
    curie_unpremultiply(plutovg_surface_get_data(svg), w, h, stride);

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

/* --- CSS -> nk_style ----------------------------------------------------- */

static struct nk_color
col_of(const unsigned char c[4])
{
    return nk_rgba(c[0], c[1], c[2], c[3]);
}

/* Records the rect a widget just occupied, the cursor it wants, and whether
 * hovering it changes anything on screen. Motion is tested against this list
 * instead of forcing a frame: see SDL_AppIterate. */
static void
hot_push_ex(App *app, struct nk_rect r, int cursor, int repaint, int top,
            int track)
{
    int n = app->hot_n;

    if (n >= (int)(sizeof(app->hot) / sizeof(app->hot[0]))) return;
    app->hot[n].r       = r;
    app->hot[n].cursor  = (unsigned char)cursor;
    app->hot[n].repaint = (unsigned char)repaint;
    app->hot[n].top     = (unsigned char)top;
    app->hot[n].track   = (unsigned char)track;
    app->hot_n = n + 1;
}

static void
hot_push(App *app, struct nk_rect r, int cursor, int repaint)
{
    hot_push_ex(app, r, cursor, repaint, 0, 0);
}

/* Pushes the computed style behind `selector` onto Nuklear's button style and
 * returns how many entries were pushed, so the caller pops the same number.
 *
 * Pure states its hover as a translucent black gradient laid over the base
 * colour. libcss does not parse linear-gradient and Nuklear would not paint
 * one here anyway, so the same intent is applied as a shade of the resolved
 * background - the value still comes from the stylesheet, only the blend is
 * ours. The active state is Pure's inset box-shadow, treated the same way. */
typedef struct style_frame { int items, colors, floats, vec2s, fonts; } style_frame;

static style_frame
push_button_style(App *app, struct nk_context *ctx, const char *selector)
{
    curie_style s, hov;
    unsigned char hover[4], active[4];
    style_frame f = { 0, 0, 0, 0, 0 };
    char hsel[80];

    curie_style_get(selector, &s);
    if (!s.matched) return f;

    /* tiny.css writes the hover as a colour of its own (--button-hover), so it
     * is read rather than derived. Only :active is still shaded here: tiny.css
     * expresses that as a transform, which Nuklear has no equivalent for. */
    snprintf(hsel, sizeof(hsel), "%s:hover", selector);
    curie_style_get(hsel, &hov);

    memcpy(hover, hov.matched && hov.bg[3] ? hov.bg : s.bg, 4);
    memcpy(active, hover, 4);
    curie_style_darken(active, 0.10f);

    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(col_of(hover)));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(col_of(active)));
    f.items = 3;

    nk_style_push_color(ctx, &ctx->style.button.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.text_hover,  col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.text_active, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.border_color,
                        col_of(s.border_col));
    f.colors = 4;

    nk_style_push_float(ctx, &ctx->style.button.rounding, s.rounding);
    nk_style_push_float(ctx, &ctx->style.button.border, s.border);
    f.floats = 2;

    nk_style_push_vec2(ctx, &ctx->style.button.padding,
                       nk_vec2(s.pad_x, s.pad_y));
    f.vec2s = 1;

    if (s.font_px > 0) {
        nk_style_push_font(ctx, pick_font(app, s.font_px, s.bold));
        f.fonts = 1;
    }
    return f;
}

static void
pop_style(struct nk_context *ctx, style_frame f)
{
    int i;
    for (i = 0; i < f.fonts;  i++) nk_style_pop_font(ctx);
    for (i = 0; i < f.vec2s;  i++) nk_style_pop_vec2(ctx);
    for (i = 0; i < f.floats; i++) nk_style_pop_float(ctx);
    for (i = 0; i < f.colors; i++) nk_style_pop_color(ctx);
    for (i = 0; i < f.items;  i++) nk_style_pop_style_item(ctx);
}

static int
css_button(App *app, struct nk_context *ctx, const char *selector,
           const char *label)
{
    style_frame f;
    int clicked;

    /* tiny.css gives the button a --button-hover fill, so this one does
     * need a frame when the pointer arrives. */
    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_label(ctx, label);
    pop_style(ctx, f);
    return clicked;
}

/* WCAG relative luminance, and the contrast ratio between two colours.
 *
 * The accent button needs a label colour, and neither candidate is right in
 * both themes: tiny.css's --links is #0070E0 in light, where white reads
 * cleanly, and #56c7ff in dark, where white does not - that pale blue is
 * bright enough that white on it is barely legible. Choosing by measured
 * contrast gets both right without naming a colour here, and keeps working if
 * the palette changes. */
static float
luminance(const unsigned char c[4])
{
    float ch[3];
    int i;

    for (i = 0; i < 3; i++) {
        float v = c[i] / 255.0f;
        ch[i] = v <= 0.04045f ? v / 12.92f
                              : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    return 0.2126f * ch[0] + 0.7152f * ch[1] + 0.0722f * ch[2];
}

static float
contrast_ratio(const unsigned char a[4], const unsigned char b[4])
{
    float la = luminance(a), lb = luminance(b);
    float hi = la > lb ? la : lb, lo = la > lb ? lb : la;

    return (hi + 0.05f) / (lo + 0.05f);
}

/* The stylesheet's own label colour, unless it is unreadable on `bg`.
 *
 * Not "whichever measures higher". On tiny.css's light accent (#0070E0) the
 * two candidates are 4.39 and 4.46 - a coin flip - and picking the maximum
 * flipped the label from the black the stylesheet asked for to near-white for
 * a gain of 0.07, which is a change nobody asked for and everybody notices.
 * On the dark accent (#56c7ff) white measures 1.91, which is genuinely
 * illegible, and the fallback is worth taking at 8.04.
 *
 * So the stylesheet wins by default and is only overridden when it fails. */
#define READABLE_MIN 3.0f

static struct nk_color
readable_on(const unsigned char bg[4], const char *preferred,
            const char *fallback)
{
    unsigned char a[4], b[4];

    if (curie_style_token(preferred, a)) {
        if (contrast_ratio(bg, a) >= READABLE_MIN) return col_of(a);
        if (curie_style_token(fallback, b) &&
            contrast_ratio(bg, b) > contrast_ratio(bg, a))
            return col_of(b);
        return col_of(a);
    }
    if (curie_style_token(fallback, b)) return col_of(b);
    return nk_rgb(255, 255, 255);
}

/* A colour, unless it is indistinguishable from what is behind it.
 *
 * tiny.css's dark palette sets --table to --table-bg-alt, which is
 * --background-body - the page's own colour - so `thead` comes out exactly
 * the background it is drawn on and the table header disappears. `details`
 * has the same problem. The value is still the stylesheet's; this only
 * declines to use one that cannot be seen, and says so by falling back to the
 * surface colour the rest of the app already uses for a raised panel. */
struct nk_color
curie_visible(struct nk_color want, struct nk_color behind,
              struct nk_color fallback)
{
    unsigned char a[4], b[4];

    a[0] = want.r;   a[1] = want.g;   a[2] = want.b;   a[3] = want.a;
    b[0] = behind.r; b[1] = behind.g; b[2] = behind.b; b[3] = behind.a;
    return contrast_ratio(a, b) < 1.12f ? fallback : want;
}

/* The same button, filled from a palette token instead of its own rule.
 *
 * tiny.css is classless and has exactly one button style, so a primary action
 * has to come from somewhere: --links is its accent, the colour it already
 * uses to mean "this is the thing to press". The value is still the
 * stylesheet's.
 *
 * The base style is pushed first and the accent on top of it. Doing it the
 * other way round - which is how this was written first - meant the base rule
 * was pushed last and simply won, and the primary button came out identical to
 * the secondaries. */
static int
css_button_accent(App *app, struct nk_context *ctx, const char *selector,
                  const char *label, const char *token)
{
    style_frame f, a = { 0, 0, 0, 0, 0 };
    unsigned char c[4];
    int clicked;

    hot_push(app, nk_widget_bounds(ctx), 1, 1);

    f = push_button_style(app, ctx, selector);

    if (curie_style_token(token, c)) {
        unsigned char hov[4];
        memcpy(hov, c, 4);
        curie_style_darken(hov, 0.12f);

        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(col_of(c)));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(col_of(hov)));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(col_of(hov)));
        a.items = 3;
        nk_style_push_color(ctx, &ctx->style.button.border_color, col_of(c));

        {
            struct nk_color label_col =
                readable_on(c, "--text-bright", "--background-body");
            nk_style_push_color(ctx, &ctx->style.button.text_normal, label_col);
            nk_style_push_color(ctx, &ctx->style.button.text_hover, label_col);
            nk_style_push_color(ctx, &ctx->style.button.text_active, label_col);
        }
        a.colors = 4;
    }

    clicked = nk_button_label(ctx, label);
    pop_style(ctx, a);
    pop_style(ctx, f);
    return clicked;
}

static int
css_button_icon(App *app, struct nk_context *ctx, const char *selector,
                const char *icon_src, const char *label)
{
    style_frame f;
    struct nk_image im = icon(app, icon_src, 18);
    int clicked;

    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_image_label(ctx, im, label, NK_TEXT_CENTERED);
    pop_style(ctx, f);
    return clicked;
}

/* The text field. tiny.css has a plain `input` rule - no attribute selector
 * to work around, which is what Pure needed - plus `input:focus` for the
 * focus border and `input::placeholder` for the hint colour. The pseudo-element
 * is beyond libcss, but the value behind it is the --text-muted token, so the
 * placeholder is coloured from the same declaration the stylesheet uses.
 *
 * `hint` is a placeholder, not a value: it is painted into the empty field and
 * never enters the buffer, so it cannot be submitted and does not need
 * clearing on the first click. Nuklear has no placeholder of its own. */

/* Copies the current selection to the clipboard, the way nk_edit_buffer does
 * it internally for Ctrl+C: selection bounds are glyph indices, so the text
 * pointer comes from nk_str_at_const rather than from the raw buffer. */
static void
edit_copy_selection(struct nk_context *ctx, struct nk_text_edit *edit)
{
    int b = edit->select_start, e = edit->select_end;
    int begin = NK_MIN(b, e), end = NK_MAX(b, e);
    int glyph_len;
    nk_rune unicode;
    const char *text;

    if (begin == end || !ctx->clip.copy) return;
    text = nk_str_at_const(&edit->string, begin, &unicode, &glyph_len);
    if (text) ctx->clip.copy(ctx->clip.userdata, text, end - begin);
}

static void
edit_paste_clipboard(struct nk_text_edit *edit)
{
    char *text = SDL_GetClipboardText();

    if (!text) return;
    if (*text) {
        /* Glyph count, not byte length: the same Nuklear bug the SDL backend
         * works around in its own paste hook. */
        nk_textedit_paste(edit, text, (int)SDL_utf8strlen(text));
    }
    SDL_free(text);
}

/* The `input` rule pushed onto nk_style.edit, shared by the login field and
 * by the showcase's, which differ only in where the buffer lives. */
static style_frame
push_edit_style(struct nk_context *ctx, curie_style *out)
{
    curie_style s, foc;
    style_frame f = { 0, 0, 0, 0, 0 };

    curie_style_get("input", &s);
    curie_style_get("input:focus", &foc);
    *out = s;
    if (!s.matched) return f;

    nk_style_push_style_item(ctx, &ctx->style.edit.normal,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.edit.hover,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.edit.active,
                             nk_style_item_color(col_of(s.bg)));
    f.items = 3;

    /* Nuklear's edit style carries one border colour with no per-state
     * variants, so the focus colour is used: the field is bordered in
     * --focus whenever it is on screen rather than only when focused. */
    nk_style_push_color(ctx, &ctx->style.edit.border_color,
                        col_of(foc.matched ? foc.border_col : s.border_col));
    nk_style_push_color(ctx, &ctx->style.edit.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_hover, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_active, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.cursor_normal, col_of(s.fg));

    /* Selection in --focus, tiny.css's own blue for exactly this - the
     * colour it already puts on a focused input's border - rather than
     * Nuklear's grey. The text on top is whichever of the palette's two
     * ends stays readable against it. */
    {
        unsigned char sel[4];
        struct nk_color selbg, seltx;

        if (curie_style_token("--focus", sel)) {
            selbg = col_of(sel);
            seltx = readable_on(sel, "--text-bright", "--background-body");
        } else {
            selbg = nk_rgb(0x7a, 0xa3, 0xfc);
            seltx = nk_rgb(0x20, 0x20, 0x20);
        }
        nk_style_push_color(ctx, &ctx->style.edit.selected_normal, selbg);
        nk_style_push_color(ctx, &ctx->style.edit.selected_hover, selbg);
        nk_style_push_color(ctx, &ctx->style.edit.selected_text_normal, seltx);
        nk_style_push_color(ctx, &ctx->style.edit.selected_text_hover, seltx);
    }
    f.colors = 9;

    nk_style_push_float(ctx, &ctx->style.edit.rounding, s.rounding);
    nk_style_push_float(ctx, &ctx->style.edit.border, s.border);
    f.floats = 2;

    nk_style_push_vec2(ctx, &ctx->style.edit.padding,
                       nk_vec2(s.pad_x, s.pad_y));
    f.vec2s = 1;
    return f;
}

static void
draw_hint(struct nk_context *ctx, struct nk_rect bounds, const char *hint,
          const curie_style *s)
{
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    const struct nk_user_font *font = ctx->style.font;
    unsigned char muted[4];
    struct nk_color grey = curie_style_token("--text-muted", muted)
                         ? col_of(muted) : nk_rgb(0x9a, 0x9a, 0x9a);
    float pad = (s->matched ? s->pad_x : 8.0f) + (s->matched ? s->border : 1.0f);
    struct nk_rect r = nk_rect(bounds.x + pad,
                               bounds.y + (bounds.h - font->height) * 0.5f,
                               bounds.w - pad * 2.0f, font->height + 2.0f);

    nk_draw_text(canvas, r, hint, (int)strlen(hint), font,
                 nk_rgba(0, 0, 0, 0), grey);
}

/* Records the field under the pointer for the drag clamp in SDL_AppEvent.
 * Only the hovered one, because a page may hold several and the clamp has to
 * pin the gesture to the field it started in. */
static void
note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
        app->field_rect = bounds;
        app->field_rect_valid = 1;
    }
}

static void
css_field(App *app, struct nk_context *ctx, char *buf, int *len, int cap,
          const char *hint)
{
    struct nk_rect bounds = nk_widget_bounds(ctx);
    curie_style s;
    style_frame f;

    (void)buf; (void)cap;
    note_field_rect(app, ctx, bounds);

    /* The field's hover background is its resting colour, so hovering it
     * changes only the cursor - which needs no frame. */
    hot_push(app, bounds, 2, 0);

    f = push_edit_style(ctx, &s);
    nk_edit_buffer(ctx, NK_EDIT_FIELD, &app->edit, nk_filter_default);
    pop_style(ctx, f);

    /* Right-click menu. Nuklear places and dismisses it; the items act on the
     * edit state directly, which is why it is ours to hold. */
    if (nk_contextual_begin(ctx, 0, nk_vec2(160, 172), bounds)) {
        int has_sel = app->edit.select_start != app->edit.select_end;

        nk_layout_row_dynamic(ctx, 26, 1);
        if (has_sel && nk_contextual_item_label(ctx, "Cut", NK_TEXT_LEFT)) {
            edit_copy_selection(ctx, &app->edit);
            nk_textedit_cut(&app->edit);
        }
        if (has_sel && nk_contextual_item_label(ctx, "Copy", NK_TEXT_LEFT))
            edit_copy_selection(ctx, &app->edit);
        if (nk_contextual_item_label(ctx, "Paste", NK_TEXT_LEFT))
            edit_paste_clipboard(&app->edit);
        if (nk_contextual_item_label(ctx, "Select all", NK_TEXT_LEFT))
            nk_textedit_select_all(&app->edit);
        nk_contextual_end(ctx);
    }

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
}

/* Which parts of a frameless window the desktop should treat as chrome.
 *
 * Without this a borderless window cannot be moved, resized, snapped or
 * double-clicked to maximise - all of which the native frame was providing.
 * SDL calls this from its own event handling, so it reads only the rects the
 * last frame recorded and never touches Nuklear. */
static SDL_HitTestResult SDLCALL
window_hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
    App *app = (App *)data;
    int w = 0, h = 0, i;
    int left, right, top, bottom;

    SDL_GetWindowSize(win, &w, &h);

    /* A maximised window has no outside edge to grab. */
    if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)) {
        left   = pt->x < RESIZE_EDGE;
        right  = pt->x >= w - RESIZE_EDGE;
        top    = pt->y < RESIZE_EDGE;
        bottom = pt->y >= h - RESIZE_EDGE;

        if (top && left)     return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right)    return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top)             return SDL_HITTEST_RESIZE_TOP;
        if (bottom)          return SDL_HITTEST_RESIZE_BOTTOM;
        if (left)            return SDL_HITTEST_RESIZE_LEFT;
        if (right)           return SDL_HITTEST_RESIZE_RIGHT;
    }

    if (pt->y < TITLEBAR_H) {
        /* The buttons are holes in the drag region, or they could never be
         * clicked - the desktop would start moving the window instead. */
        /* Each control, grown by a few pixels.
         *
         * Not only so they can be clicked - the gaps between them matter as
         * much. A draggable region is HTCAPTION, and over a caption Windows
         * sends WM_NCMOUSEMOVE, which SDL does not deliver as motion: the app
         * goes blind while the pointer crosses it. Keeping the whole cluster
         * client-side means motion keeps arriving as the pointer travels from
         * one control to the next, which is what the hover depends on. */
        for (i = 0; i < app->ctl_n; i++) {
            struct nk_rect r = app->ctl[i];
            const int m = 4;
            if (pt->x >= r.x - m && pt->x <= r.x + r.w + m &&
                pt->y >= r.y - m && pt->y <= r.y + r.h + m)
                return SDL_HITTEST_NORMAL;
        }
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

/* The three window controls.
 *
 * Drawn as line work rather than from Ionicons. A minimise, a maximise and a
 * close are geometry, not iconography - every desktop draws them as hairlines
 * - and an icon set's outline weight is tuned for 24px content, so at 14px in
 * a titlebar it came out heavy and blunt next to the system's own. Two
 * strokes and a rectangle are also fewer moving parts than three SVG loads,
 * a raster cache and a texture upload.
 *
 * The button is built by hand for the same reason: nk_button_image would fill
 * a background and centre an image, and what is wanted is a hover wash with a
 * crisp 1px glyph on top. */
/* The diagnostics page forces no frames of its own.
 *
 * It did, briefly, so that the readings would move - and that was circular:
 * the page was measuring the frames it made itself draw, so the frame rate
 * reported whatever clock it had been given rather than anything about the
 * app. It also broke the property the whole design rests on, that nothing is
 * drawn when nothing has happened. The figures come from real frames instead,
 * so they hold still while the app is idle. That stillness is the
 * measurement. */
static void
set_tab(App *app, int tab)
{
    if (tab == app->tab) return;
    app->tab = tab;
    app->dirty = 1;
}

/* Defined below; the titlebar switch needs to (un)install it. */
static SDL_HitTestResult SDLCALL window_hit_test(SDL_Window *win,
                                                const SDL_Point *pt,
                                                void *data);

/* One window control: an Ionicon on a circular hover wash.
 *
 * Built from nk_button_image rather than by drawing onto the canvas by hand.
 * The hand-rolled version painted the glyph with nk_draw_image and got three
 * white squares - the same SVGs rasterise correctly at this exact size, so
 * the fault was the direct-to-canvas path, not the artwork. Going through the
 * widget also makes the hover Nuklear's job, and a rounding of half the
 * button height turns its highlight into the circle a titlebar wants.
 *
 * image_padding is what sets the drawn glyph size: nk_button_image fills the
 * whole widget otherwise, which is how a 14px icon came out stretched across
 * a 36x28 slot. */
static int
titlebar_button(App *app, struct nk_context *ctx, const char *glyph, int px)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    style_frame f = { 0, 0, 0, 0, 0 };
    unsigned char c[4];
    struct nk_color clear = nk_rgba(0, 0, 0, 0);
    struct nk_color wash = curie_style_token("--background-hover", c)
                         ? col_of(c) : nk_rgba(255, 255, 255, 26);
    char src[176];
    float pad_x, pad_y;
    int clicked;

    hot_push(app, b, 1, 1);
    if (app->ctl_n < (int)(sizeof(app->ctl) / sizeof(app->ctl[0])))
        app->ctl[app->ctl_n++] = b;

    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(clear));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(wash));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(wash));
    f.items = 3;

    nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
    nk_style_push_float(ctx, &ctx->style.button.rounding,
                        (b.h > 0.0f ? b.h : (float)CTL_SIZE) * 0.5f);
    /* nk_draw_button_image tints the glyph by this factor. It is not part of
     * any style we set, so whatever it holds decides whether the icon is
     * visible at all - at zero the image multiplies to black, which on a dark
     * titlebar is indistinguishable from not being drawn. */
    nk_style_push_float(ctx, &ctx->style.button.color_factor_background, 1.0f);
    f.floats = 3;

    /* The button's own padding is zeroed first. nk_do_button_image insets by
     * padding *and then* by image_padding, so leaving the default in place
     * made the content rect collapse and the glyph disappear entirely. With
     * it at zero, image_padding alone centres the glyph.
     *
     * nk_do_button_text_image derives the glyph square from the button's
     * *height* and then insets it by image_padding, so on a square button one
     * value both sizes and centres it. */
    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));

    /* nk_do_button_text_image derives the glyph square from the button's
     * *height* and then insets it by image_padding, so with a square button
     * one padding value both sizes and centres it. That is also why the slot
     * is square: in a wider button the glyph is pinned to the left edge, not
     * centred, which is what had them sitting far apart. */
    pad_x = pad_y = (b.h - (float)px) * 0.5f;
    if (pad_x < 0.0f) pad_x = pad_y = 0.0f;
    nk_style_push_vec2(ctx, &ctx->style.button.image_padding,
                       nk_vec2(pad_x, pad_y));
    f.vec2s = 2;

    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                 glyph, app->icon_hex, (double)GLYPH_STROKE);

    /* nk_button_image_label with an empty label, not nk_button_image: the
     * latter draws the background and then nothing at all here, while this is
     * the same call the card's icon buttons already use and it works. */
    clicked = nk_button_image_label(ctx, icon(app, src, px), "",
                                    NK_TEXT_CENTERED);
    pop_style(ctx, f);
    return clicked;
}

static void
titlebar(App *app, struct nk_context *ctx, int win_w)
{
    unsigned char c[4];
    int maximised = (SDL_GetWindowFlags(app->win) & SDL_WINDOW_MAXIMIZED) != 0;

    app->ctl_n = 0;
    if (!nk_group_begin(ctx, "titlebar", NK_WINDOW_NO_SCROLLBAR)) return;

    nk_layout_row_begin(ctx, NK_STATIC, (float)CTL_SIZE, 7);

    nk_layout_row_push(ctx, (float)TITLE_PAD);
    nk_spacing(ctx, 1);

    /* The same mark the desktop shows for the window and the executable, so
     * a frameless window still identifies itself. */
    nk_layout_row_push(ctx, (float)MARK_SIZE);
    {
        char src[176];

        /* Outline only, in the theme's accent. Filled, it read as a solid
         * violet block rather than as an icon - the outline detail in a
         * 512-unit glyph is simply gone at eighteen pixels. */
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/browsers-outline.svg"
                     "?stroke=%s&sw=%.2f", app->accent_hex,
                     (double)GLYPH_STROKE);
        image_centred(ctx, icon(app, src, MARK_SIZE), MARK_SIZE);
    }

    nk_layout_row_push(ctx, (float)TITLE_GAP);
    nk_spacing(ctx, 1);

    /* Exactly the remainder, so the controls finish flush with the right
     * edge: the group's own padding either side, plus the five inter-column
     * gaps Nuklear inserts across six columns. A guessed constant here left a
     * visible strip of dead titlebar past the close button. */
    nk_layout_row_push(ctx, (float)(win_w - TITLE_PAD - MARK_SIZE - TITLE_GAP -
                                    3 * CTL_SIZE - 2 * 4 - 6 * 4));
    {
        nk_style_push_font(ctx, pick_font(app, 13, 0));
        if (curie_style_token("--text-muted", c))
            nk_style_push_color(ctx, &ctx->style.text.color, col_of(c));
        else
            nk_style_push_color(ctx, &ctx->style.text.color, app->text);
        nk_label(ctx, "Curie", NK_TEXT_LEFT);
        nk_style_pop_color(ctx);
        nk_style_pop_font(ctx);
    }

    /* Glyph names only - titlebar_button builds the path and appends the
     * theme's stroke colour. */
    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "remove-outline", GLYPH_MINIMISE))
        SDL_MinimizeWindow(app->win);

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx,
                        maximised ? "copy-outline" : "square-outline",
                        GLYPH_MAXIMISE)) {
        if (maximised) SDL_RestoreWindow(app->win);
        else           SDL_MaximizeWindow(app->win);
    }

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "close-outline", GLYPH_CLOSE))
        app->want_quit = 1;

    nk_layout_row_end(ctx);
    nk_group_end(ctx);
}

/* --- the screen ---------------------------------------------------------- */

/* Row heights, in logical px, and the single gap between every row. Keeping
 * one gap rather than scattering spacer rows is what gives the card an even
 * rhythm - and it makes the card's height a sum that can be stated up front,
 * which Nuklear needs before the contents are emitted. */
#define ROW_BRAND   28
#define ROW_LABEL   16
#define ROW_FIELD   38
#define ROW_BUTTON  40
#define ROW_SMALL   18
#define ROW_GAP     15
#define CONTACT_ROWS 3

/* Nuklear's default group padding is 4px, which put the field and the buttons
 * hard against the card's edge. */
#define CARD_PAD_X  22
#define CARD_PAD_Y  18

static float
card_height(int with_diag)
{
    int rows = 7;                    /* brand..contact link */
    float h = (float)(ROW_BRAND + ROW_FIELD + ROW_BUTTON +
                      ROW_SMALL + ROW_BUTTON + ROW_BUTTON + ROW_SMALL);
    if (with_diag) {
        rows += CONTACT_ROWS;
        h += CONTACT_ROWS * ROW_SMALL;
    }
    /* Plus the card's own padding, top and bottom. */
    return h + (rows - 1) * ROW_GAP + 2.0f * CARD_PAD_Y + 6.0f;
}

/* A clickable line of text. Nuklear has no link widget and tiny.css has no
 * component for one, so this is a label that reports its own hover and click -
 * which is all a link is here. The colour comes from tiny.css's --links for
 * the resting state, so it still tracks the theme. */
static int
text_link(App *app, struct nk_context *ctx, const char *label, int active)
{
    unsigned char c[4];
    struct nk_color col;
    int clicked = 0;

    /* A link does not change colour on hover, only the cursor. */
    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 0);
        /* Released inside, rather than pressed: same reason the buttons use
         * NK_BUTTON_TRIGGER_ON_RELEASE - see src/nk_common.h. This checks
         * clicked_pos against the rect, so letting go somewhere else does
         * not count. */
        if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, b))
            clicked = 1;
    }

    col = active && curie_style_token("--links", c)
        ? col_of(c)
        : (curie_style_token("--text-muted", c) ? col_of(c) : app->text);

    nk_style_push_color(ctx, &ctx->style.text.color, col);
    nk_label(ctx, label, NK_TEXT_CENTERED);
    nk_style_pop_color(ctx);
    return clicked;
}

/* The card, centred in whatever region the shell hands it. The titlebar and
 * the tab strip are the shell's business now, so this is only the card.
 *
 * Placed with nk_layout_space, which takes an explicit rect, rather than a
 * spacer row above a static row: the row APIs advance a cursor, and mixing
 * nk_spacing into a row built with nk_layout_row_push does not advance it the
 * way centring arithmetic assumes. An absolute rect has no such question in
 * it.
 *
 * Centred on the collapsed height, always. Centring on the current height
 * would re-centre the card every time diagnostics opens, so the whole screen
 * jumped on a click that should only have added rows underneath. */
static void
login_card(App *app, struct nk_context *ctx, float win_w, float body_y,
           float body_h)
{
    float card_h = card_height(app->show_contact);
    float side = (win_w - (float)CARD_W) * 0.5f;
    float top  = body_y + (body_h - card_height(0)) * 0.5f;

    if (side < 8.0f) side = 8.0f;

    /* Centred on the collapsed height so the card does not jump when
     * diagnostics opens - but slid up if the expanded card would run off the
     * bottom, which it now can: the tab strip took 34px off the body and the
     * four diagnostic rows no longer fit under it. */
    if (top + card_h > body_y + body_h - 8.0f)
        top = body_y + body_h - card_h - 8.0f;
    if (top  < body_y + 8.0f) top = body_y + 8.0f;

    /* Pushed into the shell's space, not a space of its own: the caller has
     * one open already, and a widget is what a pushed rect expects. */
    nk_layout_space_push(ctx, nk_rect(side, top, (float)CARD_W, card_h));

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(app->card_bg));
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(CARD_PAD_X, CARD_PAD_Y));
    if (nk_group_begin(ctx, "card", NK_WINDOW_NO_SCROLLBAR)) {
        nk_style_push_vec2(ctx, &ctx->style.window.spacing,
                           nk_vec2(0, (float)ROW_GAP));

        /* brand: a square slot for the icon so it is never stretched, and a
         * column of its own for the gap - the row spacing set above applies
         * between rows, not between columns of one row. */
        nk_layout_row_begin(ctx, NK_STATIC, ROW_BRAND, 3);
        nk_layout_row_push(ctx, ROW_BRAND);
        {
            char src[176];
            SDL_snprintf(src, sizeof(src),
                         "third_party/ionicons/src/svg/"
                         "person-circle-outline.svg?stroke=%s",
                         app->accent_hex);
            image_centred(ctx, icon(app, src, ROW_BRAND), ROW_BRAND);
        }
        nk_layout_row_push(ctx, 10);
        nk_spacing(ctx, 1);
        nk_layout_row_push(ctx, CARD_W - 2.0f * CARD_PAD_X - ROW_BRAND - 10.0f);
        nk_style_push_font(ctx, pick_font(app, 19, 1));
        nk_label(ctx, "Proceed with login", NK_TEXT_LEFT);
        nk_style_pop_font(ctx);
        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, ROW_FIELD, 1);
        {
            int used = (int)app->edit.string.len;
            css_field(app, ctx, app->edit_buf, &used, sizeof(app->edit_buf),
                      "you@example.com");
        }

        nk_layout_row_dynamic(ctx, ROW_BUTTON, 1);
        css_button_accent(app, ctx, "button", "Continue with email", "--links");

        nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
        nk_style_push_font(ctx, pick_font(app, 13, 0));
        nk_label(ctx, "or", NK_TEXT_CENTERED);
        nk_style_pop_font(ctx);

        {
            char key_src[160], bio_src[160];

            SDL_snprintf(key_src, sizeof(key_src),
                         "third_party/ionicons/src/svg/"
                         "key-outline.svg?stroke=%s", app->icon_hex);
            SDL_snprintf(bio_src, sizeof(bio_src),
                         "third_party/ionicons/src/svg/"
                         "finger-print-outline.svg?stroke=%s", app->icon_hex);

            nk_layout_row_dynamic(ctx, ROW_BUTTON, 1);
            css_button_icon(app, ctx, "button", key_src, "Use a passkey");

            nk_layout_row_dynamic(ctx, ROW_BUTTON, 1);
            css_button_icon(app, ctx, "button", bio_src, "Use biometrics");
        }

        /* The scheme switch used to sit here. It is a property of the
         * window, not of this page, so it lives in the tab strip beside the
         * titlebar switch. */
        nk_style_push_font(ctx, pick_font(app, 14, 0));

        nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
        if (text_link(app, ctx,
                      app->show_contact ? "hide contact" : "contact", 0))
            app->show_contact = !app->show_contact;

        if (app->show_contact) {
            /* Stub, and obviously so: the reserved example.com domain and an
             * Ofcom drama number, which cannot reach anyone. */
            static const char *const lines[CONTACT_ROWS] = {
                "support@curie.example",
                "+44 20 7946 0958",
                "Mon-Fri, 09:00-17:00 UTC"
            };
            int i;

            for (i = 0; i < CONTACT_ROWS; i++) {
                nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
                nk_label(ctx, lines[i], NK_TEXT_CENTERED);
            }
        }
        nk_style_pop_font(ctx);

        nk_style_pop_vec2(ctx);
        nk_group_end(ctx);
    }
    nk_style_pop_vec2(ctx);            /* group_padding */
    nk_style_pop_style_item(ctx);
}

/* --- the palette half of the seam ----------------------------------------
 *
 * tiny.css is classless. It has rules for `button`, `input`, `select`,
 * `textarea` and `table`, and nothing whatever for a slider, a knob, a chart,
 * a tree, a scrollbar, a menu or a popup - so those cannot be styled from a
 * selector, because there is no selector to compute. They are styled from the
 * palette instead: the same custom properties the rules themselves are
 * written in terms of. The whole UI still comes out of the stylesheet, even
 * where the stylesheet has no name for the widget.
 *
 * Written into ctx->style once per theme rather than pushed per widget, so a
 * page can call nk_slider_float directly and still match the rest of the app.
 * src/style.c is the rule half; this is the token half, and the showcase tabs
 * exist to show where the boundary between them falls. */

/* `pad` is not decoration: nk_do_button subtracts it (and the border, and the
 * corner radius) from the content rect, and a symbol is drawn to fill exactly
 * that. It is therefore the only control over how large the glyph comes out.
 * Nuklear's own defaults are 2px for a combo's arrow and nothing at all for a
 * property's steppers, which on a 30px row gives a triangle nearly as tall as
 * the widget. */
static void
style_flat_button(struct nk_style_button *b, struct nk_color hover,
                  struct nk_color text, float pad)
{
    b->normal      = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    b->hover       = nk_style_item_color(hover);
    b->active      = nk_style_item_color(hover);
    b->text_normal = b->text_hover = b->text_active = text;
    b->text_background = hover;
    b->border      = 0.0f;
    b->rounding    = 2.0f;
    b->padding     = nk_vec2(pad, pad);
}

static void
style_toggle(struct nk_style_toggle *t, struct nk_style_item bg,
             struct nk_style_item hov, struct nk_style_item cursor,
             struct nk_color border, struct nk_color text,
             struct nk_color back)
{
    t->normal        = bg;
    t->hover         = hov;
    t->active        = hov;
    t->cursor_normal = cursor;
    t->cursor_hover  = cursor;
    t->border_color  = border;
    t->border        = 1.0f;
    /* nk_do_toggle grows its row to font->height + 2 * padding.y, so anything
     * generous here makes a checkbox taller than the menu items above it and
     * its label no longer lines up with theirs. */
    t->padding       = nk_vec2(2.0f, 2.0f);
    t->text_normal   = t->text_hover = t->text_active = text;
    t->text_background = back;
}

static void
apply_widget_style(App *app)
{
    struct nk_context *ctx;
    struct nk_style *st;
    struct nk_color body, base, hover, text, muted, bright, accent, focus, edge;
    struct nk_color on_accent, thumb, thumb_hi;
    struct nk_style_item i_none, i_base, i_hover, i_accent;
    curie_style btn, btn_hov, inp, sel, det, sum, dlg;
    unsigned char c[4], ac[4];

    if (!app->ctx) return;
    ctx = app->ctx;
    st  = &ctx->style;

    body   = curie_style_token("--background-body", c) ? col_of(c) : app->page;
    base   = curie_style_token("--background", c)      ? col_of(c) : app->card_bg;
    hover  = curie_style_token("--background-hover", c) ? col_of(c) : base;
    text   = curie_style_token("--text-main", c)   ? col_of(c) : app->text;
    muted  = curie_style_token("--text-muted", c)  ? col_of(c) : text;
    bright = curie_style_token("--text-bright", c) ? col_of(c) : text;
    accent = curie_style_token("--links", c)       ? col_of(c) : text;
    focus  = curie_style_token("--focus", c)       ? col_of(c) : accent;

    /* tiny.css draws its borders in --background-hover; there is no --border
     * token to read, so the same value serves both. */
    edge = hover;

    /* A label on a filled accent, decided by measured contrast rather than
     * named here - the accent is a mid blue in light and a pale one in dark,
     * and no single choice reads on both. */
    on_accent = curie_style_token("--links", ac)
              ? readable_on(ac, "--text-bright", "--background-body") : bright;

    /* A scrollbar thumb has no colour of its own in any stylesheet. It is the
     * muted text colour at partial alpha, which is what every desktop's is. */
    thumb    = nk_rgba(muted.r, muted.g, muted.b, 110);
    thumb_hi = nk_rgba(muted.r, muted.g, muted.b, 175);

    i_none   = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    i_base   = nk_style_item_color(base);
    i_hover  = nk_style_item_color(hover);
    i_accent = nk_style_item_color(accent);

    st->text.color = text;

    /* button and input are the two widgets tiny.css does name, so they come
     * from their rules. Setting them globally as well as pushing them per
     * widget is what lets a page call nk_button_label with no ceremony. */
    curie_style_get("button", &btn);
    curie_style_get("button:hover", &btn_hov);
    if (btn.matched) {
        unsigned char act[4];

        memcpy(act, btn_hov.matched && btn_hov.bg[3] ? btn_hov.bg : btn.bg, 4);
        curie_style_darken(act, 0.10f);

        st->button.normal = nk_style_item_color(col_of(btn.bg));
        st->button.hover  = nk_style_item_color(
            btn_hov.matched && btn_hov.bg[3] ? col_of(btn_hov.bg)
                                             : col_of(btn.bg));
        st->button.active = nk_style_item_color(col_of(act));
        st->button.text_normal = st->button.text_hover =
            st->button.text_active = col_of(btn.fg);
        st->button.border_color = col_of(btn.border_col);
        st->button.border       = btn.border;
        st->button.rounding     = btn.rounding;
        st->button.padding      = nk_vec2(btn.pad_x, btn.pad_y);
    }

    curie_style_get("input", &inp);
    if (inp.matched) {
        struct nk_color sel_text = curie_style_token("--focus", c)
                                 ? readable_on(c, "--text-bright",
                                               "--background-body") : bright;

        st->edit.normal = st->edit.hover = st->edit.active =
            nk_style_item_color(col_of(inp.bg));
        st->edit.border_color = col_of(inp.border_col);
        st->edit.text_normal  = st->edit.text_hover = st->edit.text_active =
            col_of(inp.fg);
        st->edit.cursor_normal = col_of(inp.fg);
        st->edit.selected_normal = st->edit.selected_hover = focus;
        st->edit.selected_text_normal = st->edit.selected_text_hover =
            sel_text;
        st->edit.border   = inp.border;
        st->edit.rounding = inp.rounding;
        st->edit.padding  = nk_vec2(inp.pad_x, inp.pad_y);
    }

    /* The box is outlined in the muted text colour rather than in `edge`.
     * --background-hover against --background is a step of sixteen levels,
     * which disappears outright on a panel of that same colour - a checkbox
     * inside a menu had no visible box at all. */
    {
        struct nk_color box = nk_rgba(muted.r, muted.g, muted.b, 150);

        style_toggle(&st->checkbox, i_base, i_hover, i_accent, box, text, body);
        style_toggle(&st->option,   i_base, i_hover, i_accent, box, text, body);
    }

    st->selectable.normal         = i_none;
    st->selectable.hover          = i_hover;
    st->selectable.pressed        = i_hover;
    st->selectable.normal_active  = i_accent;
    st->selectable.hover_active   = i_accent;
    st->selectable.pressed_active = i_accent;
    st->selectable.text_normal    = st->selectable.text_hover =
        st->selectable.text_pressed = text;
    st->selectable.text_normal_active = st->selectable.text_hover_active =
        st->selectable.text_pressed_active = on_accent;
    st->selectable.text_background = body;
    st->selectable.rounding = 3.0f;
    /* The glyph square is the row's height less the padding, so without an
     * inset a 32px row gives a 28px icon - which is what made the star beside
     * "With an image" tower over its own label. */
    st->selectable.image_padding = nk_vec2(7.0f, 7.0f);

    st->slider.normal        = i_none;
    st->slider.hover         = i_none;
    st->slider.active        = i_none;
    st->slider.bar_normal    = edge;
    st->slider.bar_hover     = edge;
    st->slider.bar_active    = edge;
    st->slider.bar_filled    = accent;
    st->slider.cursor_normal = i_accent;
    st->slider.cursor_hover  = nk_style_item_color(focus);
    st->slider.cursor_active = nk_style_item_color(focus);
    st->slider.border_color  = edge;
    st->slider.rounding      = 2.0f;
    st->slider.show_buttons  = nk_false;

    st->knob.normal            = i_none;
    st->knob.hover             = i_none;
    st->knob.active            = i_none;
    st->knob.knob_normal       = base;
    st->knob.knob_hover        = hover;
    st->knob.knob_active       = hover;
    st->knob.knob_border_color = edge;
    st->knob.cursor_normal     = accent;
    st->knob.cursor_hover      = focus;
    st->knob.cursor_active     = focus;
    st->knob.border_color      = edge;

    st->progress.normal              = i_base;
    st->progress.hover               = i_base;
    st->progress.active              = i_base;
    st->progress.cursor_normal       = i_accent;
    st->progress.cursor_hover        = nk_style_item_color(focus);
    st->progress.cursor_active       = nk_style_item_color(focus);
    st->progress.border_color        = edge;
    st->progress.cursor_border_color = accent;
    st->progress.border              = 1.0f;
    /* The filled part gets no outline of its own: Nuklear strokes it in
     * cursor_border_color on top of the fill, which on a solid accent is a
     * lighter line tracing the inside of the bar and nothing else. */
    st->progress.cursor_border       = 0.0f;
    /* Rounded, at the radius the stylesheet gives a button. There is no
     * `progress` rule in tiny.css to read one from, and a square bar standing
     * among controls with an 8px radius is the only square thing on the
     * page. The filled part takes the same radius so it does not sit in the
     * track with square ends. */
    /* A small radius on both, rather than the button's.
     *
     * A rounded rect whose radius exceeds half its width self-intersects, and
     * a progress bar spends the first few per cent of its range narrower than
     * its own radius - at the button's 8px that drew the fill as a blue knot.
     * Nuklear does not clamp it and no style field will. Leaving the fill
     * square instead put square ends inside a rounded track, which looked
     * like a mistake at every value. Three pixels reads as rounded on both
     * and degenerates into something too small to see. */
    st->progress.rounding            = 3.0f;
    /* Square, and small enough on the track that it does not show.
     *
     * A rounded rect whose radius exceeds half its width self-intersects, and
     * a progress bar spends the first few per cent of its range narrower than
     * any radius worth having - which drew the fill as a blue knot instead of
     * a sliver. Nuklear does not clamp it and no style field will, so the
     * fill takes square ends; against a 3px track the difference is invisible
     * and it can never degenerate. */
    st->progress.cursor_rounding     = 0.0f;

    st->property.normal       = i_base;
    st->property.hover        = i_hover;
    st->property.active       = i_hover;
    st->property.border_color = edge;
    st->property.label_normal = st->property.label_hover =
        st->property.label_active = text;
    /* The value inside a property is a real edit widget, so it inherits the
     * `input` rule - and that rule's 0.6rem padding plus 2px border is more
     * than a 30px property row has to give, which left the number with a
     * content rect of no height and drew nothing at all. It keeps the input's
     * colours and gets its own geometry: no fill, since the property already
     * painted one behind it. */
    st->property.edit = st->edit;
    st->property.edit.normal = st->property.edit.hover =
        st->property.edit.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    st->property.edit.padding  = nk_vec2(2.0f, 2.0f);
    st->property.edit.border   = 0.0f;
    st->property.edit.rounding = 0.0f;
    /* A property's steppers are only half the font's height square, not the
     * row's height, so they take almost no inset before the arrow vanishes
     * entirely - which at 8 it did. */
    style_flat_button(&st->property.inc_button, hover, muted, 1.0f);
    style_flat_button(&st->property.dec_button, hover, muted, 1.0f);

    /* A combo box is a select, and tiny.css has a rule for one. Its own
     * fill, border, radius and padding - not the card's background and a
     * guess, which is what it had while this was read from tokens. */
    curie_style_get("select", &sel);
    st->combo.normal        = sel.matched ? nk_style_item_color(col_of(sel.bg))
                                          : i_base;
    st->combo.hover         = i_hover;
    st->combo.active        = i_hover;
    st->combo.border_color  = sel.matched ? col_of(sel.border_col) : edge;
    st->combo.label_normal  = st->combo.label_hover =
        st->combo.label_active = sel.matched ? col_of(sel.fg) : text;
    st->combo.symbol_normal = st->combo.symbol_hover =
        st->combo.symbol_active = muted;
    /* No symbol at all: nk_draw_symbol builds its chevron from the corners
     * of whatever box it is given, so the angle is the box's aspect ratio and
     * nothing else - which came out as a wide, flat V. The page paints the
     * Ionicon over the button instead, at a fixed size, like every other icon
     * in the app. See combo_chevron in showcase.c. */
    st->combo.sym_normal = st->combo.sym_hover = st->combo.sym_active =
        NK_SYMBOL_NONE;
    if (sel.matched) {
        /* The stylesheet's width, unmodified.
         *
         * Nuklear's antialiased rect stroke lands unevenly when the widget's
         * rect is not on whole pixels - measured at 1px along the left edge
         * against 2px along the right. Widening the border only moved the
         * unevenness to the top and bottom and made it more obvious, so the
         * fix is on the layout side: the combos are laid out on a static row
         * with a whole-pixel width. See page_inputs. */
        /* Zero, and the page strokes it instead - see combo_chrome in
          * showcase.c. Nuklear's own nk_stroke_rect is biased: measured at
          * one pixel down the left edge against two down the right for the
          * same 2px border, and no style value can even that out because it
          * is where the rasteriser puts the line, not how wide it is. */
        st->combo.border   = 0.0f;
        st->combo.rounding = sel.rounding;

        /* CSS measures padding *inside* the border; Nuklear measures
         * content_padding from the widget's outer edge - and the border is
         * the page's to draw now - so the two are added to reproduce the
         * spacing the rule actually asks for.
         *
         * Floored at the `input` rule's inset on top of that. tiny.css gives
         * select 0.25rem against input's 0.55rem, and a combo standing beside
         * a text field with half its padding reads as an oversight rather
         * than as a decision. */
        {
            float inset = sel.pad_x + sel.border;
            if (inp.matched && inp.pad_x + inp.border > inset)
                inset = inp.pad_x + inp.border;
            st->combo.content_padding = nk_vec2(inset, sel.pad_y);
        }
    }
    style_flat_button(&st->combo.button, hover, muted, 7.0f);

    /* A tree is a <details>, and its header is the <summary>. Both have
     * rules; the arrow is drawn from summary::before, which libcss cannot
     * reach, so only that stays Nuklear's. */
    curie_style_get("details", &det);
    curie_style_get("summary", &sum);
    st->tab.background   = nk_style_item_color(
        det.matched ? curie_visible(col_of(det.bg), body, base) : base);
    st->tab.border_color = det.matched ? col_of(det.border_col) : edge;
    /* The main text colour for both kinds of tree row. `summary` resolves to
     * something dimmer, and it only reaches the plain nodes - the element
     * rows draw their label through nk_style_selectable - so honouring it
     * made one row in the tree a different colour from its siblings. */
    st->tab.text         = text;
    if (det.matched) {
        st->tab.border   = det.border;
        st->tab.rounding = det.rounding;
        st->tab.padding  = nk_vec2(det.pad_x, det.pad_y);
    }
    /* Chevrons everywhere, to match the combo's arrow. These boxes are
     * square, so nk_draw_symbol's corner-to-corner chevron comes out at a
     * sane angle - it was the combo's wide button that flattened it. */
    st->tab.sym_minimize = NK_SYMBOL_CHEVRON_RIGHT;
    st->tab.sym_maximize = NK_SYMBOL_CHEVRON_DOWN;
    st->property.sym_left  = NK_SYMBOL_CHEVRON_LEFT;
    st->property.sym_right = NK_SYMBOL_CHEVRON_RIGHT;

    style_flat_button(&st->tab.tab_maximize_button, hover, muted, 3.0f);
    style_flat_button(&st->tab.tab_minimize_button, hover, muted, 3.0f);
    style_flat_button(&st->tab.node_maximize_button, hover, muted, 3.0f);
    style_flat_button(&st->tab.node_minimize_button, hover, muted, 3.0f);

    st->chart.background     = nk_style_item_color(base);
    st->chart.border_color   = edge;
    st->chart.color          = accent;
    st->chart.selected_color = focus;
    st->chart.border         = 1.0f;

    st->scrollh.normal        = nk_style_item_color(body);
    st->scrollh.hover         = nk_style_item_color(body);
    st->scrollh.active        = nk_style_item_color(body);
    st->scrollh.cursor_normal = nk_style_item_color(thumb);
    st->scrollh.cursor_hover  = nk_style_item_color(thumb_hi);
    st->scrollh.cursor_active = nk_style_item_color(thumb_hi);
    st->scrollh.border_color  = body;
    st->scrollh.rounding      = 3.0f;
    st->scrollh.rounding_cursor = 3.0f;
    st->scrollh.show_buttons  = nk_false;
    st->scrollv = st->scrollh;

    style_flat_button(&st->contextual_button, hover, text, 4.0f);
    style_flat_button(&st->menu_button, hover, text, 4.0f);
    /* A 2px radius on a 26px row is square in all but name, and the wash sat
     * in a rounded menu with hard corners of its own. */
    st->contextual_button.rounding = 5.0f;
    st->menu_button.rounding       = 5.0f;

    st->window.background              = base;
    st->window.fixed_background        = i_base;
    /* A popup is a <dialog>. Only its frame is reachable - the backdrop is
     * ::backdrop, which is a pseudo-element. */
    curie_style_get("dialog", &dlg);
    st->window.border_color            = edge;
    st->window.popup_border_color      = dlg.matched ? col_of(dlg.border_col)
                                                     : edge;
    if (dlg.matched) st->window.popup_border = dlg.border;

    /* Nuklear has one rounding for every panel it draws, and the page, the
     * titlebar, the tab strip and the body are all panels - so setting it
     * from `dialog` rounded the window's own corners and then rounded the
     * titlebar's on top of them, one inside the other. It stays square here;
     * the transient panels that should be rounded push it themselves, which
     * is what curie_popup_rounding is for. */
    st->window.rounding = 0.0f;
    st->window.combo_border_color      = edge;
    st->window.contextual_border_color = edge;
    st->window.menu_border_color       = edge;
    st->window.group_border_color      = edge;
    /* Not `edge`. A tooltip appears over the thing it describes, which is
     * hovered - and --background-hover is both the border colour and the
     * hover fill, so the frame vanished into the button under it and the
     * whole tooltip read as half-transparent. The muted text colour at low
     * alpha stands off either surface. */
    st->window.tooltip_border_color    = nk_rgba(muted.r, muted.g, muted.b, 90);
    st->window.scaler                  = i_hover;
    st->window.header.normal           = i_base;
    st->window.header.hover            = i_base;
    st->window.header.active           = i_base;
    st->window.header.label_normal     = st->window.header.label_hover =
        st->window.header.label_active = text;
    style_flat_button(&st->window.header.close_button, hover, muted, 5.0f);
    style_flat_button(&st->window.header.minimize_button, hover, muted, 5.0f);
}

/* --- the tab strip -------------------------------------------------------
 *
 * Nuklear has no tab widget - nk_style_tab is the tree header, not this - so
 * a tab here is a flat button with an accent rule under the active one. Each
 * is sized to its own label, measured through the font rather than given a
 * column width, because six equal columns across a 960px window would have
 * spread six short words to the corners. */
static void
tab_strip(App *app, struct nk_context *ctx, int win_w)
{
    const struct nk_user_font *font = pick_font(app, 14, 0);
    unsigned char c[4];
    struct nk_color accent = curie_style_token("--links", c)
                           ? col_of(c) : app->text;
    struct nk_color muted  = curie_style_token("--text-muted", c)
                           ? col_of(c) : app->text;
    struct nk_color wash   = curie_style_token("--background-hover", c)
                           ? col_of(c) : nk_rgba(128, 128, 128, 40);
    struct nk_color clear  = nk_rgba(0, 0, 0, 0);
    struct nk_command_buffer *canvas;
    struct nk_rect active_r = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    /* Which frame the window wears. It lives here rather than on the login
     * card because the card is one page of six, and this is a property of the
     * window - and because with the native frame in use there is no drawn
     * titlebar left to put it in. */
    const char *swl = app->borderless ? "native titlebar" : "custom titlebar";
    float tabw[TAB_COUNT], themew[3], sw_w = 0.0f, rest = 0.0f;
    /* Clear air between the scheme switch and the titlebar switch: they do
     * unrelated things and should not read as one row of five words. */
    const float TAB_SEP = 26.0f;
    int i;

    (void)win_w;
    if (!nk_group_begin(ctx, "tabs", NK_WINDOW_NO_SCROLLBAR)) return;
    canvas = nk_window_get_canvas(ctx);

    /* Measured first, because the switch at the far end needs to know what
     * is left over. */
    {
        float used = 0.0f;
        for (i = 0; i < TAB_COUNT; i++) {
            const char *n = curie_tab_names[i];
            tabw[i] = font->width(font->userdata, font->height, n,
                                  (int)strlen(n)) + 2.0f * (float)TAB_PAD_X;
            used += tabw[i];
        }
        for (i = 0; i < 3; i++) {
            const char *n = g_theme_names[i];
            themew[i] = font->width(font->userdata, font->height, n,
                                    (int)strlen(n)) + 2.0f * (float)TAB_PAD_X;
            used += themew[i];
        }
        sw_w = font->width(font->userdata, font->height, swl,
                           (int)strlen(swl)) + 2.0f * (float)TAB_PAD_X;
        /* The group pads either side, and Nuklear inserts 4px between each of
         * the columns: the tabs, a stretching spacer, the three scheme links,
         * the separator and the switch. */
        rest = (float)win_w - used - sw_w - TAB_SEP - 2.0f * 4.0f
             - (float)(TAB_COUNT + 5) * 4.0f;
        if (rest < 0.0f) rest = 0.0f;
    }

    nk_style_push_font(ctx, font);
    nk_layout_row_begin(ctx, NK_STATIC, (float)(TAB_H - 6), TAB_COUNT + 6);
    for (i = 0; i < TAB_COUNT; i++) {
        const char *name = curie_tab_names[i];
        float w = tabw[i];
        struct nk_color fg = (i == app->tab) ? accent : muted;
        struct nk_rect b;

        nk_layout_row_push(ctx, w);
        b = nk_widget_bounds(ctx);
        hot_push(app, b, 1, 1);

        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(clear));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(wash));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(wash));
        nk_style_push_color(ctx, &ctx->style.button.text_normal, fg);
        nk_style_push_color(ctx, &ctx->style.button.text_hover,  fg);
        nk_style_push_color(ctx, &ctx->style.button.text_active, fg);
        nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
        nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);

        if (nk_button_label(ctx, name)) set_tab(app, i);
        if (i == app->tab) active_r = b;

        nk_style_pop_float(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
    }
    nk_layout_row_push(ctx, rest);
    nk_spacing(ctx, 1);

    /* Which palette is in force: "system" follows SDL_GetSystemTheme(), the
     * other two pin it. Changing it reloads the stylesheets, which is the
     * whole mechanism - tiny.css ships light and dark as two palette files. */
    for (i = 0; i < 3; i++) {
        struct nk_color fg = (i == app->theme_mode) ? accent : muted;
        struct nk_rect b;

        nk_layout_row_push(ctx, themew[i]);
        b = nk_widget_bounds(ctx);
        hot_push(app, b, 1, 1);

        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(clear));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(wash));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(wash));
        nk_style_push_color(ctx, &ctx->style.button.text_normal, fg);
        nk_style_push_color(ctx, &ctx->style.button.text_hover,  fg);
        nk_style_push_color(ctx, &ctx->style.button.text_active, fg);
        nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
        nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);

        if (nk_button_label(ctx, g_theme_names[i]) && i != app->theme_mode) {
            app->theme_mode = i;
            load_theme(app);
        }

        nk_style_pop_float(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_row_push(ctx, TAB_SEP);
    nk_spacing(ctx, 1);

    nk_layout_row_push(ctx, sw_w);
    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 1);
        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(clear));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(wash));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(wash));
        nk_style_push_color(ctx, &ctx->style.button.text_normal, muted);
        nk_style_push_color(ctx, &ctx->style.button.text_hover,  muted);
        nk_style_push_color(ctx, &ctx->style.button.text_active, muted);
        nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
        nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);

        if (nk_button_label(ctx, swl)) {
            /* Switched live rather than at startup. SDL_SetWindowBordered
             * puts the desktop's frame back, and the hit test has to go with
             * it - leaving it installed would keep claiming the top of a
             * window that no longer draws a titlebar there. */
            app->borderless = !app->borderless;
            SDL_SetWindowBordered(app->win, app->borderless ? false : true);
            SDL_SetWindowHitTest(app->win,
                                 app->borderless ? window_hit_test : NULL,
                                 app->borderless ? app : NULL);
            app->ctl_n = 0;
            app->dirty = 1;
        }

        nk_style_pop_float(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_row_end(ctx);
    nk_style_pop_font(ctx);

    /* After the buttons, so it lands on top of the hover wash. */
    if (active_r.w > 0.0f)
        nk_fill_rect(canvas, nk_rect(active_r.x, active_r.y + active_r.h,
                                     active_r.w, 2.0f), 0.0f, accent);
    nk_group_end(ctx);
}

/* --- the shell -----------------------------------------------------------
 *
 * Titlebar (only when the window is frameless), tab strip, body. The body is
 * a group so that a page can be taller than the window and scroll, and so a
 * page never has to know where on screen it is. */
static void
page_shell(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    float chrome = app->borderless ? (float)TITLEBAR_H : 0.0f;
    float top    = chrome + (float)TAB_H;
    float body_h = (float)win_h - top;

    if (body_h < 1.0f) body_h = 1.0f;

    nk_layout_space_begin(ctx, NK_STATIC, (float)win_h, 3);

    if (app->borderless) {
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                 nk_style_item_color(app->card_bg));
        nk_layout_space_push(ctx, nk_rect(0, 0, (float)win_w,
                                          (float)TITLEBAR_H));
        titlebar(app, ctx, win_w);
        nk_style_pop_style_item(ctx);
    }

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(app->card_bg));
    nk_layout_space_push(ctx, nk_rect(0, chrome, (float)win_w, (float)TAB_H));
    tab_strip(app, ctx, win_w);
    nk_style_pop_style_item(ctx);

    /* The login card is placed absolutely and never scrolls, so it is pushed
     * into this same space. Wrapping it in a body group cost a second
     * full-window fill every frame - four times the CPU of the untabbed
     * version while the pointer swept the card, and none of it visible in
     * build/render/present, because it was spent inside the renderer rather
     * than in anything this file times. */
    if (app->tab == TAB_LOGIN) {
        login_card(app, ctx, (float)win_w, top, body_h);
        nk_layout_space_end(ctx);
        return;
    }

    nk_layout_space_push(ctx, nk_rect(0, top, (float)win_w, body_h));
    if (app->scroll0 > 0) {
        nk_group_set_scroll(ctx, "body", 0, (nk_uint)app->scroll0);
        app->scroll0 = 0;          /* a starting position, not a lock */
    }

    /* A showcase page can be taller than the window, so it does need one -
     * with no background of its own, since the window was already cleared to
     * exactly this colour. */
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_hide());
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(20.0f, 12.0f));
    if (nk_group_begin(ctx, "body", 0)) {
        struct nk_vec2 sz = nk_window_get_content_region_size(ctx);

        /* Both were read when this group's panel began, so they are popped
         * here rather than after it ends. Leaving the hidden background on
         * the stack meant every popup, tooltip and menu opened inside the
         * page inherited it.
         *
         * What is left underneath is the page's own colour, which is right
         * for the *groups* a page nests inside itself - they should read as
         * part of the page. It is wrong for a popup, which has to sit above
         * it; those push the raised surface themselves, because Nuklear
         * gives both kinds of panel the same style field to read. */
        nk_style_pop_vec2(ctx);
        nk_style_pop_style_item(ctx);

        /* A backstop under the whole page, pushed before the page's own
         * widgets so that the last-match rule in SDL_AppEvent lets any of
         * them override it. It asks for no repaint: the controls that change
         * on hover register themselves as they are emitted, so a pointer
         * travelling over headings and paragraphs costs no frames - which is
         * the difference between about ten percent of a core and nothing
         * while the mouse crosses one of these pages. */
        hot_push(app, nk_rect(0, top, (float)win_w, body_h), 0, 0);

        curie_showcase_page(app, ctx, app->tab, sz.x, sz.y);
        nk_group_end(ctx);
    } else {
        nk_style_pop_vec2(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_space_end(ctx);
}

/* --- what a page may ask of the shell ------------------------------------
 * Declared in ui.h. Thin on purpose: everything below is already written
 * above, and the point of the indirection is only that showcase.c never sees
 * inside App. */

struct nk_color
curie_col(const unsigned char rgba[4])
{
    return col_of(rgba);
}

struct nk_color
curie_token(const char *name, struct nk_color def)
{
    unsigned char c[4];
    return curie_style_token(name, c) ? col_of(c) : def;
}

const struct nk_user_font *
curie_font(App *app, int px, int bold)
{
    return pick_font(app, px, bold);
}

struct nk_image
curie_glyph(App *app, const char *rel_src, int px)
{
    return icon(app, rel_src, px);
}

/* Below about twenty pixels an Ionicon's stroke - a fixed 6.25% of the glyph
 * - falls under one device pixel and anti-aliases to a grey smudge. The
 * window controls hit this first and were fixed one at a time; the rule
 * belongs here instead, so every small icon in the app comes out solid. */
#define ICON_HAIRLINE_BELOW 20

struct nk_image
curie_ionicon(App *app, const char *name, int px)
{
    char src[192];

    if (px < ICON_HAIRLINE_BELOW)
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                     name, app->icon_hex, (double)GLYPH_STROKE);
    else
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg?stroke=%s",
                     name, app->icon_hex);
    return icon(app, src, px);
}

struct nk_image
curie_ionicon_col(App *app, const char *name, int px, struct nk_color stroke)
{
    char src[192];

    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=#%02x%02x%02x"
                 "&sw=%.2f", name, stroke.r, stroke.g, stroke.b,
                 px < ICON_HAIRLINE_BELOW ? (double)GLYPH_STROKE : 1.0);
    return icon(app, src, px);
}

/* The label colour for something drawn on `bg` - the stylesheet's own, unless
 * it fails the contrast floor. The same rule the accent button and the text
 * selection already use, so an icon on a filled row matches its label. */
struct nk_color
curie_on(struct nk_color bg)
{
    unsigned char c[4];

    c[0] = bg.r; c[1] = bg.g; c[2] = bg.b; c[3] = bg.a;
    return readable_on(c, "--text-bright", "--background-body");
}

void
curie_image(App *app, struct nk_context *ctx, struct nk_image im, int px)
{
    (void)app;
    image_centred(ctx, im, px);
}

void
curie_hot(App *app, struct nk_rect r, int cursor, int repaint)
{
    hot_push(app, r, cursor, repaint);
}

void
curie_hot_top(App *app, struct nk_rect r, int cursor, int repaint)
{
    hot_push_ex(app, r, cursor, repaint, 1, 0);
}

void
curie_hot_follow(App *app, struct nk_rect r, int cursor)
{
    hot_push_ex(app, r, cursor, 1, 0, 1);
}

int
curie_button(App *app, struct nk_context *ctx, const char *label)
{
    return css_button(app, ctx, "button", label);
}

int
curie_button_accent(App *app, struct nk_context *ctx, const char *label)
{
    return css_button_accent(app, ctx, "button", label, "--links");
}

int
curie_button_icon(App *app, struct nk_context *ctx, const char *ionicon,
                  const char *label)
{
    char src[192];

    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=%s",
                 ionicon, app->icon_hex);
    return css_button_icon(app, ctx, "button", src, label);
}

int
curie_link(App *app, struct nk_context *ctx, const char *label, int active)
{
    return text_link(app, ctx, label, active);
}

/* The showcase's own fields. nk_edit_string keeps the caret and the selection
 * inside Nuklear, so a page can have several; the login field uses
 * nk_edit_buffer instead because its context menu has to reach that state. */
nk_flags
curie_field(App *app, struct nk_context *ctx, nk_flags flags,
            char *buf, int *len, int cap, const char *hint,
            nk_plugin_filter filter)
{
    struct nk_rect bounds = nk_widget_bounds(ctx);
    curie_style s;
    style_frame f;
    nk_flags state;

    note_field_rect(app, ctx, bounds);
    hot_push(app, bounds, 2, 0);

    f = push_edit_style(ctx, &s);
    state = nk_edit_string(ctx, flags, buf, len, cap, filter);
    pop_style(ctx, f);

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
    return state;
}

/* The radius a popup, a tooltip or a menu should have: `dialog`'s, capped,
 * because 1rem is a pill at tooltip height. Pushed around those calls rather
 * than set globally - see the note in apply_widget_style. */
float
curie_popup_rounding(void)
{
    curie_style dlg;

    curie_style_get("dialog", &dlg);
    if (!dlg.matched) return 6.0f;
    return dlg.rounding > 8.0f ? 8.0f : dlg.rounding;
}

void
curie_diagnostics(App *app, curie_diag *out)
{
    out->renderer   = SDL_GetRendererName(app->ren);
    out->mode       = app->render_mode;
    out->frame_rate = app->frame_rate;
    out->drag_rate  = app->drag_rate;
    out->font       = app->font_status;
    out->vsync      = app->vsync_on;
    out->aa         = app->aa;
    out->dark       = app->dark;
    out->sheets     = SHEET_COUNT;
    out->paints     = app->n_paints;
    out->tab        = app->tab;
    out->scale      = curie_scale();
    out->style_ms   = app->style_ms_x100 / 100.0f;
    out->fps        = app->fps;
    out->frame_gap_ms = app->frame_gap_ms;

    /* Nuklear grows this to fit the busiest frame it has been asked to draw
     * and keeps it; `used` is what the last frame actually needed. */
    out->nk_bytes = (unsigned long)app->ctx->memory.size;
    out->nk_used  = (unsigned long)app->ctx->memory.allocated;

    {
        int i;
        unsigned long b = 0;
        for (i = 0; i < app->img_count; i++)
            b += (unsigned long)app->img[i].w * (unsigned long)app->img[i].h * 4u;
        out->icon_bytes = b;
        out->icons      = app->img_count;
    }

    out->rss_bytes  = (unsigned long)curie_process_rss();
    out->atlas_w    = app->atlas_w;
    out->atlas_h    = app->atlas_h;
    out->build_ms   = app->build_ms_x100 / 100.0f;
    out->render_ms  = app->render_ms_x100 / 100.0f;
    out->present_ms = app->present_ms_x100 / 100.0f;
}

showcase_state *
curie_showcase(App *app)
{
    return &app->show;
}

#ifdef __EMSCRIPTEN__
/* The viewport, in CSS pixels.
 *
 * Read from the window rather than measured off an element: the body's box
 * grows to whatever the canvas inside it is, and the canvas is sized by SDL
 * from this answer, so measuring the page would be measuring our own output. */
static void
web_page_size(int *w, int *h)
{
    int cw = EM_ASM_INT({ return window.innerWidth | 0; });
    int ch = EM_ASM_INT({ return window.innerHeight | 0; });

    if (cw > 64 && ch > 64) { *w = cw; *h = ch; }
}

static EM_BOOL
web_on_resize(int type, const EmscriptenUiEvent *ev, void *user)
{
    App *app = (App *)user;
    int w = WINDOW_WIDTH, h = WINDOW_HEIGHT;

    (void)type; (void)ev;
    web_page_size(&w, &h);
    SDL_SetWindowSize(app->win, w, h);
    app->dirty = 1;
    return EM_TRUE;
}
#endif

/* --- SDL application callbacks ----------------------------------------- */

SDL_AppResult
SDL_AppInit(void **appstate, int argc, char *argv[])
{
    App *app;

    /* Headless check of the icon pipeline: curie --dump-icon <out.png> */
    if (argc >= 3 && strcmp(argv[1], "--dump-icon") == 0) {
        int ok = curie_svg_icon_dump("browsers-outline", 256,
                                     CURIE_BRAND, NULL, argv[2]);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app = (App *)SDL_calloc(1, sizeof(App));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    {
        /* "cpu" forces SDL's software rasteriser, "gpu" pins the first
         * hardware driver SDL reports, "auto" lets SDL choose and fall back on
         * its own.
         *
         * "gpu" used to be accepted and then do nothing at all - it fell
         * through to the same path as "auto" - so it was a switch that read as
         * working while changing nothing. It now names a driver. */
        const char *mode = SDL_getenv("CURIE_RENDERER");
        if (!mode || !*mode) mode = "auto";
        SDL_strlcpy(app->render_mode, mode, sizeof(app->render_mode));

        if (SDL_strcmp(mode, "cpu") == 0) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        } else if (SDL_strcmp(mode, "gpu") == 0) {
            int i, n = SDL_GetNumRenderDrivers();
            for (i = 0; i < n; i++) {
                const char *d = SDL_GetRenderDriver(i);
                if (d && SDL_strcmp(d, "software") != 0) {
                    SDL_SetHint(SDL_HINT_RENDER_DRIVER, d);
                    break;
                }
            }
        }
    }

    /* Borderless by default: the titlebar is drawn by this app, so it can
     * follow the stylesheet like everything else. CURIE_BORDERLESS=0 restores
     * the desktop's own frame, which is worth keeping - a custom titlebar
     * gives up whatever the platform does for free there. */
    /* Off in a browser: there is no desktop frame to replace, the canvas is
     * the whole window, and a titlebar drawn inside it could neither move nor
     * resize anything. */
#ifdef __EMSCRIPTEN__
    app->borderless = env_int("CURIE_BORDERLESS", 0) != 0;
#else
    app->borderless = env_int("CURIE_BORDERLESS", 1) != 0;
#endif

    /* Which page opens. Only the starting tab - the strip still switches them
     * - but a screenshot of one page should not depend on synthesising a
     * click at coordinates guessed from outside the process. */
    app->tab = env_int("CURIE_TAB", TAB_LOGIN);
    if (app->tab < 0 || app->tab >= TAB_COUNT) app->tab = TAB_LOGIN;
    app->scroll0 = env_int("CURIE_SCROLL", 0);
    if (app->scroll0 < 0) app->scroll0 = 0;

    {
        int win_w = WINDOW_WIDTH, win_h = WINDOW_HEIGHT;

#ifdef __EMSCRIPTEN__
        /* On the web the window is the page. A fixed 960x680 canvas in the
         * middle of a blank document is a screenshot of a desktop app, not a
         * web page - and it also means the app can never see a size the user
         * chose. */
        web_page_size(&win_w, &win_h);
#endif
        if (!SDL_CreateWindowAndRenderer("Curie", win_w, win_h,
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                (app->borderless ? SDL_WINDOW_BORDERLESS : 0),
                &app->win, &app->ren)) {
            SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }

#ifdef __EMSCRIPTEN__
    /* SDL sizes the canvas, so a browser resize has to be pushed back into
     * SDL rather than the other way round. */
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 0,
                                   web_on_resize);
#endif

    /* Without this the loop presents as fast as the GPU allows, which was
     * half of the idle CPU cost. */
    {
        int want = env_int("CURIE_VSYNC", 1);
        app->vsync_on = SDL_SetRenderVSync(app->ren, want ? 1 : 0) ? want : 0;
    }
    app->aa = env_int("CURIE_AA", 1);
    app->redraw_always = SDL_getenv("CURIE_REDRAW") &&
                         SDL_strcmp(SDL_getenv("CURIE_REDRAW"), "always") == 0;

    /* How often SDL calls SDL_AppIterate at all.
     *
     * The loop used to spin freely and sleep 2 ms whenever the frame was
     * clean: 500 wake-ups a second to decide there was nothing to do. SDL can
     * do better than a sleep - "waitevent" makes it block until an event
     * arrives, which is exactly this app's shape, since every reason to
     * repaint originates in one.
     *
     * CURIE_FRAME_RATE overrides it with a number (a frame cap) or 0
     * (uncapped), which is also what CURIE_REDRAW=always needs, since a
     * continuously redrawing app must not be waiting for input. */
    {
        const char *rate = SDL_getenv("CURIE_FRAME_RATE");
        if (!rate || !*rate) rate = app->redraw_always ? "0" : "waitevent";
        SDL_strlcpy(app->frame_rate, rate, sizeof(app->frame_rate));
    }

    /* The rate to run at while the pointer is held: the display's own
     * refresh, so a frame is produced for each one and no more.
     *
     * "0" - as fast as SDL will call - is worse, not better. Presenting more
     * often than the display accepts fills the swapchain, and then
     * SDL_RenderPresent blocks on it: measured at 29 ms a frame, against
     * 0.11 ms of building and 0.19 ms of rendering, which held a drag at
     * 35 fps. Asking for exactly the refresh rate keeps present from
     * queueing. */
    {
        const SDL_DisplayMode *m =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app->win));
        float hz = (m && m->refresh_rate > 1.0f) ? m->refresh_rate : 60.0f;
        SDL_snprintf(app->drag_rate, sizeof(app->drag_rate), "%.2f", hz);
    }

    if (app->borderless) SDL_SetWindowHitTest(app->win, window_hit_test, app);
    /* Either way it can be changed from the card at runtime. */

    set_window_icon(app->win);
    curie_set_scale(curie_dpi_query_scale(app->win));   /* needs the window */

    app->ctx = nk_sdl_init(app->win, app->ren, nk_sdl_allocator());
    if (!app->ctx) return SDL_APP_FAILURE;
    rebuild_font(app);

    app->cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->cur_text    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);

    /* Pinned from the environment for the same reason as CURIE_TAB: a
     * screenshot of the light palette should not depend on clicking a link
     * whose position was guessed from outside the process. The links on the
     * card still change it. */
    app->theme_mode = env_int("CURIE_THEME", THEME_SYSTEM);
    if (app->theme_mode < 0 || app->theme_mode > THEME_DARK)
        app->theme_mode = THEME_SYSTEM;

    /* Follow the desktop until told otherwise. load_theme() reads
     * SDL_GetSystemTheme() through curie_prefers_dark() and picks the
     * matching tiny.css palette file. */
    nk_textedit_init_fixed(&app->edit, app->edit_buf, sizeof(app->edit_buf));

    load_theme(app);

    app->dirty = 1;

    /* Nuklear accumulates input between nk_input_begin and nk_input_end, and
     * it is nk_input_end that turns what accumulated into the edge-triggered
     * state a frame reads - "this key was pressed", "this button was
     * clicked". Neither was ever called here.
     *
     * Typing still worked, because characters go into a separate buffer, and
     * so did clicks, because a press and its release usually straddled a
     * frame. Ctrl+V did not: paste is a key *edge*, and the edge was never
     * computed. Ctrl+A and Ctrl+Z were dead for the same reason.
     *
     * The window is opened here, closed at the top of a frame that is
     * actually drawn, and reopened after it. Frames that are skipped leave it
     * open, so input simply keeps accumulating until the next real frame -
     * which is what a key press causes anyway, by marking the app dirty. */
    nk_input_begin(app->ctx);
    return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = (App *)appstate;

    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;


    if (app) {
        /* Which events actually change what is on screen, named explicitly.
         *
         * This used to be the inverse - everything except mouse motion marked
         * the frame dirty - which quietly meant *every* event did. SDL emits
         * SDL_EVENT_POLL_SENTINEL at the end of each poll cycle, so the app
         * repainted once per cycle for the whole time it was running: the
         * dirty gate was in place and never closed. A whitelist cannot rot
         * that way, because a new event type defaults to costing nothing. */
        switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            app->dirty = 1;
            break;
        default:
            break;
        }

        switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            /* Answered here, from the event's own coordinates, because it
             * needs no frame: the rects were recorded when the last one was
             * built. The cursor follows the pointer at full motion rate for
             * free, and a rebuild is asked for only when the pointer crosses
             * into or out of a control that looks different when hovered.
             *
             * A held button is the exception, and it has to be, because
             * dragging is how text is selected. The pointer stays inside the
             * field for the whole gesture, so it never crosses anything, and
             * the crossing rule alone drew no frames at all - Nuklear tracks
             * a drag while it builds one, so the selection never moved. While
             * a button is down every move is a frame; vsync caps the rate,
             * and the cost only lasts as long as the gesture. */
            float mx = event->motion.x, my = event->motion.y;
            int i, over = -1;

            if (app->dragging) app->dirty = 1;

            /* The last match, not the first. The shell pushes one region
             * covering a showcase page before the page pushes its widgets -
             * a page has far too many controls to register one by one and
             * still be readable - so the catch-all is always overridden by
             * anything drawn inside it, and it can never be the entry that
             * an overflowing list drops. */
            {
                int over_top = -1;
                for (i = 0; i < app->hot_n; i++) {
                    struct nk_rect r = app->hot[i].r;
                    if (mx >= r.x && mx <= r.x + r.w &&
                        my >= r.y && my <= r.y + r.h) {
                        over = i;
                        if (app->hot[i].top) over_top = i;
                    }
                }
                /* Anything inside a popup outranks what it covers. */
                if (over_top >= 0) over = over_top;
            }
            {
                struct nk_rect nr = over >= 0 ? app->hot[over].r
                                              : nk_rect(0, 0, 0, 0);
                int had = app->hot_last >= 0, has = over >= 0;
                /* Compared by rect, not by index: hot[] is refilled every
                 * frame and a menu opening shifts every index in it. */
                int moved = (had != has) ||
                            (has && (nr.x != app->hot_last_r.x ||
                                     nr.y != app->hot_last_r.y ||
                                     nr.w != app->hot_last_r.w ||
                                     nr.h != app->hot_last_r.h));

                if (moved) {
                    int was = had && app->hot_last_repaint;
                    int is  = has && app->hot[over].repaint;

                    app->hot_last         = over;
                    app->hot_last_r       = nr;
                    app->hot_last_repaint = has ? app->hot[over].repaint : 0;
                    app->want_cursor      = has ? app->hot[over].cursor : 0;
                    if (was || is) app->dirty = 1;
                } else if (has && app->hot[over].track) {
                    /* Drawn at the pointer, so it has to be redrawn as the
                     * pointer moves - a crossing is not enough. */
                    app->dirty = 1;
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            /* Run a real frame loop for the duration of the gesture.
             *
             * "waitevent" is right when the app is idle and wrong while the
             * pointer is being dragged: SDL waits for an event, we draw in
             * half a millisecond, and then SDL_RenderPresent blocks on a full
             * swapchain - measured at 29 ms against 0.09 ms of building and
             * 0.46 ms of rendering. The whole frame was waiting, and the
             * waiting and the event-wait interleaved badly enough to hold the
             * drag at ~33 fps. Letting SDL call us continuously lets vsync do
             * the pacing it is meant to. */
            app->dragging = 1;
            /* A drag that begins in the field keeps extending the selection
             * even after the pointer leaves it; see the clamp below. */
            if (app->field_rect_valid) {
                struct nk_rect r = app->field_rect;
                float bx = event->button.x, by = event->button.y;
                app->drag_in_field = bx >= r.x && bx <= r.x + r.w &&
                                     by >= r.y && by <= r.y + r.h;
            }
            /* No rate limiter at all while the pointer is held - the events
             * themselves are the clock.
             *
             * Both candidates were worse, measured rather than assumed. Vsync
             * on: SDL_RenderPresent blocked two refresh intervals (29 ms
             * against 0.4 ms of actual work), holding the drag at 33 fps.
             * SDL pacing the callback at the display's 64 Hz: 45 fps, because
             * that pacing rests on the Windows timer, whose granularity is
             * ~15.6 ms. Stacking the two was worst of all.
             *
             * Nor is "waitevent" right here, though it is what the app uses
             * at rest. It makes event *arrival* the clock, and Windows
             * coalesces mouse moves - a synthetic 250 moves a second reached
             * this app as 40 - so the selection advanced in whatever uneven
             * steps the OS happened to deliver. Steady is what a drag needs,
             * not merely fast.
             *
             * So for the duration of the gesture SDL drives a fixed-rate
             * loop at the display's refresh and the frame below is drawn
             * unconditionally, at an even cadence, rather than one frame per
             * event.
             *
             * Vsync is left alone. Turning it off looked like a win and was
             * not: present still stalled ~30 ms on scattered frames, because
             * a windowed app is paced by the compositor whether or not it
             * asks to be. Leaving vsync on makes present block predictably
             * for one refresh interval instead of occasionally for two. */
            SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->drag_rate);
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            app->dragging = 0;
            app->drag_in_field = 0;
            /* Left at the drag rate until SDL_AppIterate has drawn two
             * more frames; see restore_rate. */
            app->restore_rate = 2;
            break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            /* Forget what the pointer was over.
             *
             * The hover logic works on crossings, and a crossing is only seen
             * if a motion event arrives - but a frameless window's titlebar
             * is HTCAPTION, and over a caption Windows sends WM_NCMOUSEMOVE,
             * which SDL does not deliver as motion. Leaving a window control
             * sideways into the drag region therefore left hot_last still
             * pointing at it, so returning was not a crossing and the
             * highlight was whatever the last frame had. This event does
             * arrive when the pointer leaves the client area, so it is where
             * the state is dropped. */
            app->hot_last = -1;
            app->hot_last_repaint = 0;
            app->want_cursor = 0;
            break;

        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            /* One portable event, in place of WM_SETTINGCHANGE plus the macOS
             * and XDG-portal paths that were never written. Only meaningful
             * while the scheme is not pinned. */
            if (app->theme_mode == THEME_SYSTEM) load_theme(app);
            break;

        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            curie_set_scale(curie_dpi_query_scale(app->win));
            rebuild_font(app);
            break;

        case SDL_EVENT_KEY_DOWN:
            /* F1 opens the diagnostics, which are a page of their own now
             * rather than four lines squeezed into the login card. */
            if (event->key.key == SDLK_F1) set_tab(app, TAB_DIAG);
            break;

        default:
            break;
        }
        /* Nuklear extends a text selection only while the pointer is inside
         * the widget: nk_do_edit guards nk_textedit_drag with is_hovered. So
         * dragging past the end of the field - which is exactly how you select
         * to the end of a line - silently stopped selecting, and letting go
         * outside left a partial selection.
         *
         * The position Nuklear sees is therefore clamped into the field for
         * the duration of a drag that started there. It keeps is_hovered true
         * and pins the caret to the nearer edge, which is the behaviour every
         * other text field has. Our own hover logic above already ran on the
         * real coordinates. */
        if (app->drag_in_field && event->type == SDL_EVENT_MOUSE_MOTION &&
            app->field_rect_valid) {
            struct nk_rect r = app->field_rect;
            float lo_x = r.x + 2.0f, hi_x = r.x + r.w - 2.0f;
            float lo_y = r.y + 2.0f, hi_y = r.y + r.h - 2.0f;

            if (event->motion.x < lo_x) event->motion.x = lo_x;
            if (event->motion.x > hi_x) event->motion.x = hi_x;
            if (event->motion.y < lo_y) event->motion.y = lo_y;
            if (event->motion.y > hi_y) event->motion.y = hi_y;
        }

        nk_sdl_handle_event(app->ctx, event);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppIterate(void *appstate)
{
    App *app = (App *)appstate;
    struct nk_context *ctx = app->ctx;
    int win_w = 0, win_h = 0;

    SDL_GetWindowSize(app->win, &win_w, &win_h);
    if (win_w != app->laid_w || win_h != app->laid_h) app->dirty = 1;

    /* While a button is held, every scheduled frame is drawn whether or not
     * an event arrived for it. That is what makes the cadence even: the frame
     * rate stops depending on how the OS happened to batch the mouse. */
    if (app->dragging) app->dirty = 1;

    /* Pointer motion only matters when it changes which widget is under the
     * cursor. Nuklear is immediate mode - a frame drawn because the mouse
     * moved rebuilds and re-emits the entire UI, costing exactly as much as a
     * frame drawn because something changed - so repainting per motion event,
     * or even at a fixed 30 Hz while the pointer merely travels, is paying
     * full price for nothing.
     *
     * The rects recorded while the last frame was built answer the question
     * exactly: sweeping across empty space now costs no frames at all, and
     * crossing between two buttons costs one. The interval still caps the rate
     * for a pointer dragged along a row of controls. */
    /* The cursor is set here rather than after the frame, because it no longer
     * depends on one having been drawn. */
    if (app->want_cursor != app->cur_shown) {
        SDL_Cursor *c = app->want_cursor == 1 ? app->cur_pointer
                      : app->want_cursor == 2 ? app->cur_text
                      : app->cur_default;
        if (c) SDL_SetCursor(c);
        app->cur_shown = app->want_cursor;
    }

    /* Nothing changed since the last frame. Under "waitevent" this barely
     * happens - SDL only calls us when something arrived - so there is nothing
     * left to sleep off. */
    if (!app->dirty && !app->redraw_always)
        return SDL_APP_CONTINUE;
    app->laid_w = win_w;
    app->laid_h = win_h;
    app->hot_n = 0;                /* refilled as the widgets are emitted */

    {
        Uint64 now = SDL_GetTicks();

        if (app->last_frame_ms)
            app->frame_gap_ms = (float)(now - app->last_frame_ms);
        app->last_frame_ms = now;

        app->fps_frames++;
        if (now - app->fps_t0 >= 1000) {
            app->fps = app->fps_frames * 1000.0f / (float)(now - app->fps_t0);
            app->fps_frames = 0;
            app->fps_t0 = now;

            /* Opt-in trace. A GUI-subsystem binary has no console, and
             * reading these off a screenshot proved too fragile to trust. */
            if (SDL_getenv("CURIE_STATS")) {
                FILE *lf = fopen("build/stats.log", "a");
                if (lf) {
                    char ln[160];
                    SDL_snprintf(ln, sizeof(ln),
                                 "fps=%.0f drag=%d build=%.2f render=%.2f present=%.2f",
                                 app->fps, app->dragging,
                                 app->build_ms_x100 / 100.0f,
                                 app->render_ms_x100 / 100.0f,
                                 app->present_ms_x100 / 100.0f);
                    fputs(ln, lf); fputc(10, lf); fclose(lf);
                }
            }
        }
    }

    /* One window filling the frame: no title bar, no border, no padding, so
     * the screen owns every edge. */
    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(app->clear));

    Uint64 t_build0 = SDL_GetPerformanceCounter();

    nk_input_end(ctx);
    ctx->style.text.color = app->text;

    if (nk_begin(ctx, "page", nk_rect(0, 0, (float)win_w, (float)win_h),
                 NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR)) {
        /* The zero padding above is only wanted for this panel's geometry,
         * which nk_begin has now read. Left on the stack it also reaches
         * nk_tooltip, which sizes itself as text_width + 4 * padding.x - so
         * every tooltip came out exactly as wide as its text and then clipped
         * it inside its own padding. Restored before nk_end so the pops after
         * it still balance. */
        nk_style_pop_vec2(ctx);
        page_shell(app, ctx, win_w, win_h);
        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    }
    nk_end(ctx);

    /* SDL3 delivers SDL_EVENT_TEXT_INPUT only while text input is started for
     * the window, and it is the backend that starts and stops it - from
     * whether Nuklear has an active edit widget, which it can only know after
     * the frame is built. Without this call the field took Backspace (a key
     * event) but never a character. */
    nk_sdl_update_TextInput(ctx);

    nk_style_pop_style_item(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_vec2(ctx);

    {
        Uint64 f = SDL_GetPerformanceFrequency();
        Uint64 t_render0 = SDL_GetPerformanceCounter();
        Uint64 t_present0, t_end;

        SDL_SetRenderDrawColor(app->ren, app->clear.r, app->clear.g,
                               app->clear.b, app->clear.a);
        SDL_RenderClear(app->ren);
        nk_sdl_render(ctx, app->aa ? NK_ANTI_ALIASING_ON
                                   : NK_ANTI_ALIASING_OFF);
        t_present0 = SDL_GetPerformanceCounter();
        SDL_RenderPresent(app->ren);
        t_end = SDL_GetPerformanceCounter();

        app->build_ms_x100   = (int)(100000.0 * (double)(t_render0 - t_build0) / (double)f);
        app->render_ms_x100  = (int)(100000.0 * (double)(t_present0 - t_render0) / (double)f);
        app->present_ms_x100 = (int)(100000.0 * (double)(t_end - t_present0) / (double)f);
    }

    nk_input_begin(ctx);       /* collect again for the next frame */

    app->n_paints++;
    app->dirty = 0;
    if (app->restore_rate > 0) {
        if (--app->restore_rate > 0) app->dirty = 1;
        else SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
    }
    if (app->want_quit) return SDL_APP_SUCCESS;

    /* Applied after the first frame, not before it. Setting "waitevent" up
     * front can leave the window blank until the user happens to move the
     * mouse over it: SDL only guarantees a free SDL_AppIterate immediately
     * after the hint is set in a later revision than the release this is
     * pinned to (release-3.4.16, see the submodule). */
    if (!app->first_frame_done) {
        app->first_frame_done = 1;
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
    }
    return SDL_APP_CONTINUE;
}

void
SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    App *app = (App *)appstate;

    (void)result;
    if (!app) return;

    if (app->ctx) nk_input_end(app->ctx);
    curie_style_shutdown();
    {
        int i;
        for (i = 0; i < app->img_count; i++)
            if (app->img[i].tex) SDL_DestroyTexture(app->img[i].tex);
    }
    if (app->cur_default) SDL_DestroyCursor(app->cur_default);
    if (app->cur_pointer) SDL_DestroyCursor(app->cur_pointer);
    if (app->cur_text)    SDL_DestroyCursor(app->cur_text);
    if (app->ctx) nk_sdl_shutdown(app->ctx);
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
