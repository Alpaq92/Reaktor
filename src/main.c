/* main.c - Curie: pure C UI on Nuklear, rendered through SDL3, styled by CSS.
 *
 * The loop is SDL3's callback model (SDL_AppInit / SDL_AppIterate /
 * SDL_AppEvent / SDL_AppQuit), not a while(): a browser tab cannot be blocked,
 * so under Emscripten SDL must hand control back each frame. One source then
 * serves Windows, macOS, Linux, the BSDs and WASM.
 *
 * Nuklear draws and lays out the widgets; tiny.css says what they look like.
 * Before each widget, the computed values behind its selector are pushed into
 * nk_style and popped after - src/style.c is that seam. */
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nk_common.h"
#include "nk_sdl3_renderer.h"   /* the vendored backend nk_impl.c compiles */
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

/* 16 is what tiny.css resolves to; the rest of the ladder is for headings and
 * small print. Nearest wins, so a size in between costs no fidelity. */
/* Only sizes something asks for: each step is a full glyph set in the atlas,
 * and 23 was a megapixel nothing ever drew from. */
#define FONT_STEPS 5
static const int g_font_px[FONT_STEPS] = { 12, 13, 14, 16, 19 };

/* One slot per icon per size per theme - the stroke colour is part of the key,
 * so a scheme change empties the cache rather than doubling it. */
#define IMG_CACHE_MAX 96
#define CARD_W        420

/* Height of the titlebar this app draws for itself when the native one is
 * turned off, and how wide a strip along each edge grabs for a resize. */
#define TITLEBAR_H    36
#define TAB_H         34      /* the strip under it */
#define TAB_PAD_X     14      /* either side of a tab's label */
#define RESIZE_EDGE    6
#define CTL_SIZE      28   /* the control's square hit area */
/* Nuklear adds 4px between columns and the group pads again, so the gap on
 * screen is ~10px wider than these numbers. */
#define TITLE_PAD      1   /* before the app mark */
#define TITLE_PX      16   /* the name's size; the one size the bold is baked at */
#define MARK_SIZE     18   /* the app mark, drawn size */

/* Both memory counters at each startup milestone (the list is in ui.h). File
 * statics because the first two samples land before App exists. Two counters
 * because they differ: resident says what a step maps in, mostly the driver's
 * shared pages; private says what it cost this process. */
static size_t g_rss[RSS_STEPS];
static size_t g_priv[RSS_STEPS];

/* How SDL should pace SDL_AppIterate, as the hint value. Asked before SDL_Init
 * and again once App exists, so it reads the environment rather than App. */
static const char *frame_rate_wanted(void)
{
    const char *rate = SDL_getenv("CURIE_FRAME_RATE");
    const char *redraw = SDL_getenv("CURIE_REDRAW");

    if (rate && *rate) return rate;
    return (redraw && SDL_strcmp(redraw, "always") == 0) ? "0" : "waitevent";
}

static void rss_mark(int step)
{
    curie_process_memory(&g_rss[step], &g_priv[step]);
}

/* Ionicons draw a 32-unit stroke on a 512 viewBox - 6.25% - so below 16px the
 * line falls under a pixel and greys out. Scaling the SVG's stroke widths puts
 * it back over one and lets the maximise sit a size below the other two. */
/* Even, so (CTL_SIZE - glyph) / 2 is a whole pixel: at 17 the minimise dash
 * straddled a boundary and came out one bright row between two dim. */
#define GLYPH_MINIMISE 18
#define GLYPH_MAXIMISE 14
#define GLYPH_CLOSE    18

/* At this scale the artwork's line is about one device pixel and reads as a
 * hairline; doubling it puts two solid pixels down. */
#define GLYPH_STROKE  2.0f

/* tiny.css keeps palette and rules in separate files, so a theme is which
 * variables file loads in front of core.css. Read from the submodule. */
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

/* One disc mask per radius, for curie_fill_round. Eight is more radii than
 * any one screen asks for. */
#define ROUND_CACHE_MAX 8
struct round_slot {
    int          r;
    SDL_Texture *tex;
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

    struct round_slot round[ROUND_CACHE_MAX];
    int round_count;

    /* Nuklear bakes a glyph atlas per size, so a face is baked once per size
     * the stylesheet asks for and selected per draw. */
    struct nk_font *faces[FONT_STEPS];
    struct nk_font *face_bold;      /* Aileron-Bold at TITLE_PX, for the title */
    char            font_status[160];
    /* The backend's atlas, kept only so a rebake can free the bake before it -
     * see rebuild_font. */
    struct nk_font_atlas *atlas;
    /* The baked atlas, for the diagnostics. Bytes per pixel is read off the
     * texture, not assumed: the backend can fall back to RGBA32. */
    int             atlas_w, atlas_h, atlas_bpp;

    /* Nuklear owns the editing behaviour, but the state lives here because a
     * context menu has to act on it: nk_edit_buffer takes ours. */
    struct nk_text_edit edit;
    char                edit_buf[128];

    /* Decided while the frame is built - Nuklear knows what is under the
     * pointer only as each widget is emitted - and applied once at the end. */
    SDL_Cursor *cur_default, *cur_pointer, *cur_text;
    int         want_cursor, cur_shown;   /* 0 default, 1 pointer, 2 text */

    char render_mode[32];   /* wide enough for "auto (software: no GPU)" */
    int  vsync_on, aa, redraw_always;

    /* How SDL paces SDL_AppIterate: "waitevent", a frame rate, or 0 for
     * uncapped. See the note where it is applied. */
    char frame_rate[16];
    char drag_rate[16];        /* the display's refresh, as a rate string */
    int  first_frame_done;
    int  dragging;             /* a mouse button is held */

    /* Borderless: the titlebar is drawn like any widget, and
     * SDL_SetWindowHitTest tells the desktop what drags and what resizes. The
     * hit test runs on the OS thread and cannot ask Nuklear anything, so the
     * control rects are recorded as they are emitted. */
    /* Frames owed after a click before the rate returns to "waitevent". Two:
     * a click completes on release, which is where the rate was restored, so
     * the frame showing the result was never scheduled - and one is not enough
     * because a page lays out top to bottom, so a toggle halfway down is read
     * after the sizes above it were fixed. */
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

    /* Where the *focused* field is, in window coordinates, for the IME to put
     * its candidate list beside rather than at the window's origin. Separate
     * from field_rect above, which follows the pointer. */
    SDL_Rect ime_rect;
    int      ime_cursor;       /* caret offset from ime_rect.x */
    int      ime_valid;

    /* The platform file picker. SDL runs it on its own thread on most
     * platforms and calls back from there, so the callback does the least it
     * can - fill in the answer, publish it, wake the loop - and the main
     * thread does everything else when it collects. */
    SDL_AtomicInt file_ready;      /* 0 nothing waiting, 1 answer in place */
    char          file_answer[SC_PATH_CAP];
    int           file_pending;    /* a picker is up; main thread only */
    Uint32        wake_event;      /* pushed from the callback's thread */

    int   theme_mode;          /* THEME_SYSTEM | THEME_LIGHT | THEME_DARK */
    /* A scheme picked mid-frame, as mode + 1, for the next frame to apply
     * where no style is pushed. Zero when there is nothing waiting. */
    int   theme_pending;
    struct nk_color page, card_bg, text;
    /* --text-muted and --links as "#rrggbb" - strings, because that is what
     * goes into the SVG before it is parsed. */
    char  icon_hex[10];
    char  accent_hex[10];

    /* Where the interactive widgets landed last frame, and what hovering each
     * costs. Two questions: every control wants a cursor, but only some change
     * appearance on hover. So the cursor is read straight off this list with
     * no frame at all, and a repaint asked for only when the pointer crosses
     * something whose looks depend on hover. */
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
    /* What hot_last referred to: hot[] is rebuilt every frame, so an index is
     * only meaningful within the frame that filled it. */
    struct nk_rect hot_last_r;
    unsigned char  hot_last_repaint;

    int   dirty;
    int   show_contact;

    /* Pointer-driven redraws are coalesced: see hover_redraw, and
     * calibrate_hover_gap for how the gap is chosen. */
    Uint64 last_draw_ms;
    int    hover_pending;
    int    renderer_is_sw;      /* SDL's software rasteriser - see the
                                * antialiasing note in the frame body */
    int    sw_noaa;             /* CURIE_SW_NOAA: no feathering at all there -
                                * see the frame body */
    int    drag_moved;          /* pointer moved since the last drawn frame */
    int    cal_frames, cal_done;
    double cal_cpu0;
    float  cpu_ms_per_frame;    /* 0 until calibrated */

    /* The retained description of the frame - see a11y.h. Rebuilt every frame
     * from the widgets as they are drawn, and diffed against the previous one.
     * Large (arenas, not pointers), so it lives here rather than on a stack. */
    curie_a11y a11y;

    /* Keyboard focus - phase 3 of docs/ACCESSIBILITY.md. The tree above is in
     * reading order, so its focusable subset is the tab order and nothing
     * separate is kept. focus_id names a node in it, 0 for none. */
    unsigned       focus_id;
    int            focus_visible;  /* the ring: a key shows it, a click hides it */
    struct nk_rect focus_rect;     /* where the focused node landed this frame */
    /* Arrows pressed while a range had focus, in steps, taken by the widget
     * on the next frame and cleared there whether or not it was drawn. They
     * accumulate: key repeat outruns the frame rate. */
    int            focus_step;
    /* A node Enter or a platform client asked to activate, for the widget to
     * take on the next frame. A widget that takes it acts on itself, which is
     * what makes activation reliable; the frame that ends with it untaken
     * falls back to the synthetic click below, which is what everything that
     * has not opted in still gets. */
    unsigned       activate_id;
    int            focus_seen;
    /* Enter or Space on the focused node, delivered to Nuklear as a press at
     * its centre and released on the frame after. */
    int            key_click;
    float          key_click_x, key_click_y;
    /* Focus landing outside the page's visible band scrolls the page to it
     * on the next frame. body_rect is that band, in window coordinates, as
     * of the last frame; page_node is the tree id of the page's group, so
     * only nodes inside the page ask for a scroll - the tab strip is
     * outside the band too, and must not. */
    struct nk_rect body_rect;
    unsigned       page_node;
    int            focus_scroll;
    struct nk_rect focus_scroll_rect;

    /* Which page is on screen, and what the showcase pages remember between
     * frames. Tab 0 is the login card this app began as. */
    int            tab;
    /* Where a showcase page opens, scrolled - same reason as CURIE_TAB: a
     * screenshot should not depend on synthesised wheel events. */
    int            scroll0;
    showcase_state show;
    int   laid_w, laid_h;
    float fps;
    int   fps_frames;
    Uint64 fps_t0;
    /* The gap to the previous drawn frame. fps counts frames over a second and
     * that second only ticks on when one is drawn, so with frames arriving
     * rarely it reads stale; the interval says the same thing at once. */
    Uint64 last_frame_ms;
    float  frame_gap_ms;
    int   style_ms_x100;   /* time the last stylesheet load took */
    /* Where a frame goes, in hundredths of a millisecond. Split three ways
     * because guessing which one dominates has been wrong twice. */
    int   build_ms_x100, render_ms_x100, present_ms_x100;
};

static int
env_int(const char *name, int fallback)
{
    const char *v = SDL_getenv(name);
    return (v && *v) ? SDL_atoi(v) : fallback;
}

/* The effective scheme. curie_prefers_dark() returns -1 when the platform will
 * not say, and an unknown answer is treated as light. */
static int
effective_dark(const App *app)
{
    if (app->theme_mode == THEME_LIGHT) return 0;
    if (app->theme_mode == THEME_DARK)  return 1;
    return curie_prefers_dark() > 0;
}

/* Reloads the stylesheets and re-reads the surfaces this file paints itself.
 * Cheap enough to do on a click: the whole set is ~7 KB. */
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

static void
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

/* `over` is how far above the drawn size the artwork is rasterised: a linear
 * filter downscales cleanly and upscales blurrily, and the widget rect is
 * often taller than the nominal size, so an image goes in at twice. One is
 * for a caller that draws at exactly px and needs the edge where plutovg put
 * it - a resample is a blur, and on a rim one pixel wide it reads as a smear
 * three pixels across rather than a line. */
static struct nk_image
icon_over(App *app, const char *src, int px, float over)
{
    int raster = (int)(px * curie_scale() * over + 0.5f);
    struct img_slot *slot = img_lookup(app, src, raster);

    if (!slot) return nk_image_id(0);
    /* A sub-image with real dimensions, not nk_image_ptr, which leaves w, h
     * and the source region zero. Widgets fill those in themselves;
     * nk_draw_image does not, and a degenerate region drew a white quad. */
    return nk_subimage_ptr(slot->tex, (nk_ushort)slot->w, (nk_ushort)slot->h,
                           nk_rect(0.0f, 0.0f, (float)slot->w, (float)slot->h));
}

static struct nk_image
icon(App *app, const char *src, int px)
{
    return icon_over(app, src, px, 2.0f);
}

/* Draws an image centred at exactly px, and consumes the widget slot. nk_image
 * stretches to fill its rect, so the raster size asked for changes nothing. */
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
curie_fill_round(App *app, struct nk_command_buffer *cv, struct nk_rect b,
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
static const struct nk_user_font *
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
static void
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
    have_font = curie_path(path, sizeof(path), FONT_FILE);
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
                atlas, path, (float)curie_px(g_font_px[i]), &cfg);
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
                atlas, bpath, (float)curie_px(TITLE_PX), &cfg);
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
        font = nk_font_atlas_add_default(atlas, (float)curie_px(FONT_SIZE), NULL);
        if (font) font->handle.height = (float)FONT_SIZE;
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "Aileron, %d sizes %d-%dpx%s", FONT_STEPS,
                     curie_px(g_font_px[0]), curie_px(g_font_px[FONT_STEPS - 1]),
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
static void
apply_render_scale(App *app)
{
    float s = curie_scale();
    SDL_SetRenderScale(app->ren, s, s);
}

/* --- window icon ------------------------------------------------------- */
/* SDL_SetWindowIcon is portable, so this replaces the Win32 HICON path. What
 * Explorer shows is a separate thing: branding/curie-icon.ico, linked as a
 * resource, because a file has an icon before it has a process. */
static void
set_window_icon(SDL_Window *win)
{
    plutovg_surface_t *svg;
    SDL_Surface *ico;
    int w, h, stride;

    svg = curie_svg_surface_path(CURIE_MARK, 64, NULL, NULL, 0.0f);
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

/* Records the rect a widget occupied, the cursor it wants, and whether hover
 * changes anything. Motion is tested against this instead of forcing a frame. */
static void
hot_push_ex(App *app, struct nk_rect r, int cursor, int repaint, int top,
            int track)
{
    int n = app->hot_n;

    /* Clipped to the panel being built. nk_widget_bounds answers where a
     * widget *would* go with no clip test, so a page scrolled past the top
     * registered rects for widgets above the body - over the tab strip, where
     * they won the hit test, showed the wrong cursor, and on the Popups page
     * turned the strip into a pointer-following tooltip region. Found by the
     * redraw audit; measured as frames drawn for widgets nobody could see. */
    if (app->ctx && app->ctx->current && app->ctx->current->layout) {
        struct nk_rect c = app->ctx->current->layout->clip;
        float x0 = r.x > c.x ? r.x : c.x;
        float y0 = r.y > c.y ? r.y : c.y;
        float x1 = (r.x + r.w) < (c.x + c.w) ? (r.x + r.w) : (c.x + c.w);
        float y1 = (r.y + r.h) < (c.y + c.h) ? (r.y + r.h) : (c.y + c.h);
        if (x1 <= x0 || y1 <= y0) return;     /* entirely off screen */
        r = nk_rect(x0, y0, x1 - x0, y1 - y0);
    }

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
 * returns how many entries, so the caller pops the same number. A state given
 * as a gradient or box-shadow becomes a shade of the background instead. */
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

    /* tiny.css names the hover (--button-hover), so it is read. Only :active
     * is shaded: tiny.css states that as a transform. */
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
    unsigned id;
    int clicked;

    /* tiny.css gives the button a --button-hover fill, so this one does
     * need a frame when the pointer arrives. */
    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    id = curie_note_here(app, ctx, CURIE_A11Y_BUTTON, label, 0);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_label(ctx, label);
    pop_style(ctx, f);
    if (curie_focus_activated(app, id)) clicked = 1;
    return clicked;
}

/* WCAG relative luminance and contrast ratio. The accent button's label cannot
 * be named here: --links is #0070E0 in light, where white reads cleanly, and
 * #56c7ff in dark, where it does not. */
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

/* The stylesheet's own label colour, unless unreadable on `bg`. Not "whichever
 * measures higher": on the light accent the candidates are 4.39 and 4.46, and
 * taking the maximum flipped the label to near-white for 0.07. On the dark
 * accent white measures 1.91, and the fallback is worth taking at 8.04. */
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

/* A colour, unless it cannot be told from what is behind it. tiny.css's dark
 * palette sets --table to --background-body, so `thead` came out exactly the
 * surface it is drawn on; `details` too. */
struct nk_color
curie_visible(struct nk_color want, struct nk_color behind,
              struct nk_color fallback)
{
    unsigned char a[4], b[4];

    a[0] = want.r;   a[1] = want.g;   a[2] = want.b;   a[3] = want.a;
    b[0] = behind.r; b[1] = behind.g; b[2] = behind.b; b[3] = behind.a;
    return contrast_ratio(a, b) < 1.12f ? fallback : want;
}

/* The same button filled from a palette token: tiny.css is classless and has
 * one button style, so a primary action comes from --links. Base first, accent
 * on top - the other way round the base rule was pushed last and won. */
static int
css_button_accent(App *app, struct nk_context *ctx, const char *selector,
                  const char *label, const char *token)
{
    style_frame f, a = { 0, 0, 0, 0, 0 };
    unsigned char c[4];
    int clicked;

    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    curie_note_here(app, ctx, CURIE_A11Y_BUTTON, label, 0);

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
    curie_note_here(app, ctx, CURIE_A11Y_BUTTON, label, 0);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_image_label(ctx, im, label, NK_TEXT_CENTERED);
    pop_style(ctx, f);
    return clicked;
}

/* The text field. tiny.css has `input`, `input:focus` and `input::placeholder`;
 * the pseudo-element is beyond libcss, but the value behind it is
 * --text-muted. `hint` is painted into the empty field, never in the buffer. */

/* Copies the selection, as nk_edit_buffer does internally for Ctrl+C: the
 * bounds are glyph indices, so the text comes from nk_str_at_const. */
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
 * by the showcase's, which differ only in where the buffer lives.
 *
 * Two departures from the rule as written. A field takes the button's radius
 * rather than its own: tiny.css rounds an input tighter than a button, and
 * beside the card's buttons that read as a different kind of thing. And with
 * `inset`, the fill leans toward the page body - tiny.css gives an input the
 * same colour as the card it sits on, so on the login card the field was a
 * hairline rectangle and nothing else. Leaning toward the body puts it a
 * step lighter than the card on the light scheme and a step darker on the
 * dark one, which is how a field is expected to sit. The showcase's fields
 * are on the page itself and keep the rule's fill. */
static style_frame
push_edit_style(struct nk_context *ctx, curie_style *out, int inset)
{
    curie_style s, foc, btn;
    style_frame f = { 0, 0, 0, 0, 0 };
    unsigned char body[4];

    curie_style_get("input", &s);
    curie_style_get("input:focus", &foc);
    curie_style_get("button", &btn);
    if (btn.matched) s.rounding = btn.rounding;
    /* Nuklear insets the text by border + padding, and the border is drawn
     * here as a stroke with Nuklear's own set to 0 - so the border's width
     * goes back into the padding, or the text would sit two pixels further
     * left than the rule says. Then the button's horizontal padding, if it
     * is the larger: the text starts where a button's label would. */
    s.pad_x += s.border;
    if (btn.matched && btn.pad_x > s.pad_x) s.pad_x = btn.pad_x;
    /* And never tighter than a 40px field wants: the rule's 0.6rem was
     * written for a field on a page, and next to the card's rounding the
     * text sat against the edge. */
    if (s.pad_x < 16.0f) s.pad_x = 16.0f;
    if (inset && curie_style_token("--background-body", body)) {
        int k;
        for (k = 0; k < 3; k++)
            s.bg[k] = (unsigned char)((s.bg[k] * 3 + body[k] * 2) / 5);
    }
    *out = s;
    if (!s.matched) return f;

    nk_style_push_style_item(ctx, &ctx->style.edit.normal,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.edit.hover,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.edit.active,
                             nk_style_item_color(col_of(s.bg)));
    f.items = 3;

    /* Nuklear carries one border colour for every edit state, so the choice
     * is made here, before the widget draws. Which field is focused is not a
     * guess: nk_edit_buffer takes `hash = win->edit.seq++` and is active when
     * that matches win->edit.name, so seq == name identifies the widget about
     * to be emitted. tiny.css gives an unfocused input `2px solid transparent`
     * and only :focus colours it - bordering every field in --focus made four
     * fields look like four focused ones. */
    {
        const struct nk_window *win = ctx->current;
        int focused = win && win->edit.active &&
                      win->edit.seq == win->edit.name;
        struct nk_color line;

        if (focused && foc.matched) {
            line = col_of(foc.border_col);
        } else {
            /* Transparent works on a page, where the fill alone separates the
             * field from --background-body. On the login card both are
             * --background, so it leaves nothing to see. */
            unsigned char c[4];
            line = col_of(s.border_col);
            if (line.a == 0)
                line = curie_style_token("--background-hover", c)
                     ? col_of(c) : nk_rgba(128, 128, 128, 90);
        }
        nk_style_push_color(ctx, &ctx->style.edit.border_color, line);
        /* Handed back for stroke_edit_edge, which draws the ring. */
        out->border_col[0] = line.r; out->border_col[1] = line.g;
        out->border_col[2] = line.b; out->border_col[3] = line.a;
    }
    nk_style_push_color(ctx, &ctx->style.edit.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_hover, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_active, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.cursor_normal, col_of(s.fg));

    /* Selection in --focus - the colour tiny.css already puts on a focused
     * input's border - with whichever palette end stays readable on it. */
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
    /* No border from Nuklear: it draws one as two fills, and the ring is
     * stroked instead - see stroke_edit_edge. */
    nk_style_push_float(ctx, &ctx->style.edit.border, 0.0f);
    f.floats = 2;

    nk_style_push_vec2(ctx, &ctx->style.edit.padding,
                       nk_vec2(s.pad_x, s.pad_y));
    f.vec2s = 1;
    return f;
}

/* Nuklear draws an edit's border as two fills - the border colour, then the
 * background shrunk by the width. On the software renderer fills are not
 * feathered (see the note at nk_sdl_render_ex in the frame body), so that
 * ring came out as a staircase at a 6px radius, which reads as a square
 * corner. Strokes are feathered there, so the ring is drawn as one, on the
 * same footprint: a stroke of the border's width, centred half a width in,
 * covers exactly the band the two fills did. Buttons already worked this
 * way, which is why they kept their corners and the fields lost theirs. */
static void
stroke_edit_edge(struct nk_context *ctx, struct nk_rect b, const curie_style *s)
{
    float t = s->border, r;

    if (!s->matched || t <= 0.0f) return;
    r = s->rounding - t * 0.5f;
    nk_stroke_rect(nk_window_get_canvas(ctx),
                   nk_rect(b.x + t * 0.5f, b.y + t * 0.5f, b.w - t, b.h - t),
                   r > 0.0f ? r : 0.0f, t, col_of(s->border_col));
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
    /* pad_x already carries the border - see push_edit_style - so this is
     * exactly where Nuklear starts the typed text. */
    float pad = s->matched ? s->pad_x : 9.0f;
    struct nk_rect r = nk_rect(bounds.x + pad,
                               bounds.y + (bounds.h - font->height) * 0.5f,
                               bounds.w - pad * 2.0f, font->height + 2.0f);

    nk_draw_text(canvas, r, hint, (int)strlen(hint), font,
                 nk_rgba(0, 0, 0, 0), grey);
}

/* Records the field under the pointer for the drag clamp in SDL_AppEvent. Only
 * the hovered one: the clamp has to pin the gesture to where it started. */
static void
note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
        app->field_rect = bounds;
        app->field_rect_valid = 1;
    }
}

/* Where the IME should put its candidate list: beside the caret of the field
 * that has focus, in window coordinates.
 *
 * The SDL3 backend cannot do this itself - its own FIXME says so - because
 * Nuklear exposes no way to ask which edit widget is active or where it is.
 * The app can, though: it is the one laying the widget out, so it knows the
 * rect, and ctx->text_edit is the state nk_edit_string was just working on.
 * Without it every IME candidate window opens at the window's origin, which
 * on a full-screen app is nowhere near what is being typed. */
static void
note_ime_caret(App *app, struct nk_context *ctx, struct nk_rect bounds,
               nk_flags state, const struct nk_text_edit *edit)
{
    const struct nk_user_font *font = ctx->style.font;
    float caret = 0.0f;

    if (!(state & NK_EDIT_ACTIVE)) return;

    /* Bytes up to the caret, which is a rune index. A NULL answer means the
     * caret is past the end, and the whole string is the right measure. */
    if (font && font->width) {
        const char *txt = nk_str_get_const(&edit->string);
        nk_rune unicode;
        int glyph_len;
        char *at = nk_str_at_rune((struct nk_str *)&edit->string,
                                  edit->cursor, &unicode, &glyph_len);
        int bytes = at ? (int)(at - txt) : nk_str_len_char(&edit->string);
        if (bytes > 0)
            caret = font->width(font->userdata, font->height, txt, bytes);
    }

    app->ime_rect.x = (int)bounds.x;
    app->ime_rect.y = (int)bounds.y;
    app->ime_rect.w = (int)bounds.w;
    app->ime_rect.h = (int)bounds.h;
    app->ime_cursor = (int)caret;
    app->ime_valid  = 1;
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

    f = push_edit_style(ctx, &s, 1);
    {
        nk_flags st = nk_edit_buffer(ctx, NK_EDIT_FIELD, &app->edit,
                                     nk_filter_default);
        note_ime_caret(app, ctx, bounds, st, &app->edit);
        curie_note(app, CURIE_A11Y_TEXTBOX, hint,
                   nk_str_get_const(&app->edit.string),
                   st & NK_EDIT_ACTIVE ? CURIE_A11Y_FOCUSED : 0u, bounds);
    }
    pop_style(ctx, f);
    stroke_edit_edge(ctx, bounds, &s);

    /* Right-click menu. Nuklear places and dismisses it; the items act on the
     * edit state directly, which is why it is ours to hold. The theme leaves
     * window.rounding at 0 - the page is square - so the popup's radius is
     * pushed here for as long as the popup can draw, as the showcase's menus
     * do; without it this one menu came out square-cornered. NK_WINDOW_BORDER
     * because a contextual popup is dynamic - it fills its body at
     * nk_panel_end, sized to its rows - and that is also where Nuklear
     * strokes the border, at the final height and at the rounding; without
     * the flag the fill's corners were a staircase on the software renderer,
     * where a fill is not feathered and only a stroke is. */
    nk_style_push_float(ctx, &ctx->style.window.rounding,
                        curie_popup_rounding());
    if (nk_contextual_begin(ctx, NK_WINDOW_BORDER, nk_vec2(160, 172), bounds)) {
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
    nk_style_pop_float(ctx);

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
}

/* Which parts of a frameless window the desktop treats as chrome - without it
 * the window cannot be moved, resized, snapped or maximised by double-click.
 * SDL calls this from its own event handling, so it reads only recorded
 * rects and never touches Nuklear. */
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
        /* Grown a few pixels, and the gaps between them matter as much as the
         * hits: a draggable region is HTCAPTION, and over a caption Windows
         * sends WM_NCMOUSEMOVE, which SDL does not deliver as motion. */
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

/* The diagnostics page forces no frames of its own - it did briefly, and the
 * page ended up measuring the frames it made itself draw. */
/* A frame asked for by pointer motion - a hover crossing, or a tooltip that
 * follows the pointer - is drawn at most every HOVER_GAP_MS, and the last one
 * is never dropped.
 *
 * Measured here: on a machine without a GPU every presented frame costs about
 * 78 ms of CPU in the display stack's own threads, so a pointer swept along a
 * row of buttons drew 41 frames in two seconds and read as 20-30% of the
 * machine. Capping that at 20 Hz costs nothing a person can see - a hover
 * wash arriving 50 ms late is below reaction time - and halves the worst case.
 *
 * The cap cannot simply drop the frame: at rest the loop is "waitevent", and
 * the event that would have drawn the final hover state may never come. So a
 * deferred frame pins the callback rate for one tick, draws, and hands the
 * rate back through restore_rate, the same path a drag uses. A drag itself is
 * never throttled - it has its own rate, and every move is a frame there. */
#define HOVER_GAP_MS 50

/* The gap in force. Starts at HOVER_GAP_MS and is then set from what a frame
 * measurably costs this process - see calibrate_hover_gap. CURIE_HOVER_GAP_MS
 * pins it instead, which is how the trade-off was measured in the first
 * place. */
static Uint64 g_hover_gap_ms = HOVER_GAP_MS;
static int    g_hover_gap_pinned;

/* What a drawn frame costs, in CPU across every thread of the process, and
 * the hover gap that follows from it. Sampled over the first drawn frames
 * after startup has settled, because startup frames carry the font bake and
 * the stylesheet parse and would say the wrong thing.
 *
 * Why this is measured and not configured: on a GPU a frame is a few hundred
 * microseconds and a 50 ms hover gap is already invisible; on a machine that
 * rasterises in software - the reference VM - a frame is ~78 ms of CPU in
 * threads this app never created, and a pointer waved across the page at the
 * 50 ms gap reads as a fifth of four cores. The same binary has to do the
 * right thing on both, and the only way to know which it is on is to ask. */
#define CAL_SKIP    3      /* drawn frames ignored after startup */
#define CAL_FRAMES  8      /* then averaged over this many */

static void
calibrate_hover_gap(App *app)
{
    double now;

    if (g_hover_gap_pinned || app->cal_done) return;
    if (app->cal_frames < CAL_SKIP) { app->cal_frames++; return; }
    now = curie_process_cpu_ms();
    if (app->cal_frames == CAL_SKIP) app->cal_cpu0 = now;
    app->cal_frames++;
    if (app->cal_frames < CAL_SKIP + CAL_FRAMES + 1) return;

    app->cpu_ms_per_frame = (float)((now - app->cal_cpu0) / (double)CAL_FRAMES);
    app->cal_done = 1;
    /* Thresholds from the measurement table in docs/PERFORMANCE.md: at 78 ms
     * a frame the 150 ms gap took sustained waving from 27% to 9% of four
     * cores and nobody could see the difference; at 15 ms a frame 100 ms
     * halves the cost; under that a frame is cheap and 50 ms is only there
     * to stop a hover storm. */
    g_hover_gap_ms = app->cpu_ms_per_frame > 40.0f ? 150
                   : app->cpu_ms_per_frame > 15.0f ? 100
                   : HOVER_GAP_MS;
}

static void
hover_redraw(App *app)
{
    if (app->dragging || SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms) {
        app->dirty = 1;
    } else if (!app->hover_pending) {
        app->hover_pending = 1;
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "20");
    }
}

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

/* One window control: an Ionicon on a circular hover wash, through
 * nk_button_image rather than the canvas - the hand-rolled version painted
 * three white squares from SVGs that rasterise correctly at this size. A
 * rounding of half the height makes the highlight a circle, and image_padding
 * sets the glyph size; without it the icon fills the whole slot. */
static int
titlebar_button(App *app, struct nk_context *ctx, const char *glyph,
                const char *name, int px)
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
    /* The glyph is the whole button, so the name is the only thing a reader
     * would have to go on - which is why it is a parameter and not derived
     * from the icon. */
    curie_note(app, CURIE_A11Y_BUTTON, name, NULL, 0, b);
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
    /* nk_draw_button_image tints the glyph by this factor, and it is not part
     * of any style we set: at zero the image multiplies to black. */
    nk_style_push_float(ctx, &ctx->style.button.color_factor_background, 1.0f);
    f.floats = 3;

    /* Zeroed first: nk_do_button_image insets by padding *and then* by
     * image_padding, so the default collapsed the content rect. The glyph
     * square then comes from the button's *height*, so on a square button
     * image_padding alone sizes and centres it. */
    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));

    pad_x = pad_y = (b.h - (float)px) * 0.5f;
    if (pad_x < 0.0f) pad_x = pad_y = 0.0f;
    nk_style_push_vec2(ctx, &ctx->style.button.image_padding,
                       nk_vec2(pad_x, pad_y));
    f.vec2s = 2;

    /* Every other icon is --text-muted. On the light scheme that leaves the
     * window controls a grey barely off the titlebar, and these three are
     * the one set of glyphs that must never be hunted for - so on a light
     * surface they take --text-main, as the platform's own do. Dark keeps
     * the muted stroke, which reads fine there. */
    if (app->dark) {
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                     glyph, app->icon_hex, (double)GLYPH_STROKE);
    } else {
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg"
                     "?stroke=#%02x%02x%02x&sw=%.2f",
                     glyph, app->text.r, app->text.g, app->text.b,
                     (double)GLYPH_STROKE);
    }

    /* nk_button_image_label with an empty label: nk_button_image draws the
     * background and then nothing at all here. */
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
    struct nk_rect bar;

    app->ctl_n = 0;
    /* Before nk_group_begin: inside the group, nk_window_get_bounds answers
     * the enclosing window's rect rather than the group's, and a node whose
     * bounds are the whole window is one a magnifier cannot follow. */
    bar = nk_widget_bounds(ctx);
    if (!nk_group_begin(ctx, "titlebar", NK_WINDOW_NO_SCROLLBAR)) return;
    curie_note_push(app, CURIE_A11Y_GROUP, "Title bar", NULL, 0, bar);

    nk_layout_row_begin(ctx, NK_STATIC, (float)CTL_SIZE, 6);

    nk_layout_row_push(ctx, (float)TITLE_PAD);
    nk_spacing(ctx, 1);

    /* The same mark the desktop shows for the window and the executable, so
     * a frameless window still identifies itself. No query string, so nothing
     * in it is recoloured: the artwork carries its own colours, and its yellow
     * ground keeps it legible on either theme. */
    nk_layout_row_push(ctx, (float)MARK_SIZE);
    image_centred(ctx, icon(app, CURIE_MARK, MARK_SIZE), MARK_SIZE);

    /* No spacer between the mark and the name: the 4px Nuklear puts between
     * any two columns is the whole gap, and it read as a word-space with a
     * spacer column adding its own width and a second 4px. */

    /* Exactly the remainder, so the controls finish flush with the right edge:
     * the group's padding either side plus the five gaps Nuklear inserts
     * across six columns. A guessed constant left a strip of dead titlebar. */
    nk_layout_row_push(ctx, (float)(win_w - TITLE_PAD - MARK_SIZE -
                                    3 * CTL_SIZE - 2 * 4 - 5 * 4));
    {
        /* At the body size, in the same ink as the window controls beside
         * it: --text-muted on the dark scheme, --text-main on the light one -
         * the rule titlebar_button follows. */
        nk_style_push_font(ctx, pick_font(app, TITLE_PX, 1));
        if (app->dark && curie_style_token("--text-muted", c))
            nk_style_push_color(ctx, &ctx->style.text.color, col_of(c));
        else
            nk_style_push_color(ctx, &ctx->style.text.color, app->text);
        /* 3px of text padding rather than Nuklear's 4: with the column gap
         * and the mark's own margin, 4 held the name a word-space off the
         * mark and 0 put it against it. */
        nk_style_push_vec2(ctx, &ctx->style.text.padding, nk_vec2(3.0f, 0.0f));
        curie_note_here(app, ctx, CURIE_A11Y_LABEL, "Curie", 0);
        nk_label(ctx, "Curie", NK_TEXT_LEFT);
        nk_style_pop_vec2(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_font(ctx);
    }

    /* Glyph names only - titlebar_button builds the path and appends the
     * theme's stroke colour. */
    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "remove-outline", "Minimise", GLYPH_MINIMISE))
        SDL_MinimizeWindow(app->win);

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx,
                        maximised ? "copy-outline" : "square-outline",
                        maximised ? "Restore" : "Maximise",
                        GLYPH_MAXIMISE)) {
        if (maximised) SDL_RestoreWindow(app->win);
        else           SDL_MaximizeWindow(app->win);
    }

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "close-outline", "Close", GLYPH_CLOSE))
        app->want_quit = 1;

    nk_layout_row_end(ctx);
    curie_note_pop(app);
    nk_group_end(ctx);
}

/* --- the screen ---------------------------------------------------------- */

/* Row heights in logical px, and one gap between every row - which gives the
 * card an even rhythm and makes its height a sum that can be stated up front,
 * as Nuklear needs before the contents are emitted. */
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

/* A clickable line of text: Nuklear has no link widget and tiny.css no
 * component for one, so this is a label that reports its own hover and click.
 * The resting colour is tiny.css's --links, so it tracks the theme. */
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
        curie_note(app, CURIE_A11Y_LINK, label, NULL,
                   active ? CURIE_A11Y_SELECTED : 0u, b);
        /* Released inside, not pressed - the same reason the buttons use
         * NK_BUTTON_TRIGGER_ON_RELEASE. Checked against clicked_pos, so
         * letting go elsewhere does not count. */
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

/* The card, centred in whatever region the shell hands it. Placed with
 * nk_layout_space, which takes an explicit rect: the row APIs advance a
 * cursor, and mixing nk_spacing into a pushed row does not advance it the way
 * centring arithmetic assumes. */
static void
login_card(App *app, struct nk_context *ctx, float win_w, float body_y,
           float body_h)
{
    float card_h = card_height(app->show_contact);
    float side = (win_w - (float)CARD_W) * 0.5f;
    float top  = body_y + (body_h - card_height(0)) * 0.5f;

    if (side < 8.0f) side = 8.0f;

    /* Centred on the collapsed height so the card does not jump when contact
     * opens, but slid up if the expanded card would run off the bottom. */
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
        /* The rect pushed above, which is where the card actually is. */
        curie_note_push(app, CURIE_A11Y_GROUP, "Proceed with login", NULL, 0,
                        nk_rect(side, top, (float)CARD_W, card_h));
        nk_style_push_vec2(ctx, &ctx->style.window.spacing,
                           nk_vec2(0, (float)ROW_GAP));

        /* A square slot for the icon so it is never stretched, and a column
         * of its own for the gap: row spacing does not apply within a row. */
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
        curie_note_here(app, ctx, CURIE_A11Y_LABEL, "Proceed with login", 0);
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
        nk_style_push_font(ctx, pick_font(app, 16, 0));
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

        /* The scheme switch is a property of the window, not this page, so it
         * lives in the tab strip beside the titlebar switch. */
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
                curie_note_here(app, ctx, CURIE_A11Y_LABEL, lines[i], 0);
                nk_label(ctx, lines[i], NK_TEXT_CENTERED);
            }
        }
        nk_style_pop_font(ctx);

        nk_style_pop_vec2(ctx);
        curie_note_pop(app);
        nk_group_end(ctx);
    }
    nk_style_pop_vec2(ctx);            /* group_padding */
    nk_style_pop_style_item(ctx);
}

/* --- the palette half of the seam ----------------------------------------
 *
 * tiny.css is classless: it has rules for `button`, `input`, `select`,
 * `textarea` and `table`, and nothing for a slider, knob, chart, tree,
 * scrollbar, menu or popup. Those are styled from the palette instead - the
 * same custom properties the rules are written in terms of - so the whole UI
 * still comes out of the stylesheet. Written into ctx->style once per theme
 * rather than pushed per widget. src/style.c is the rule half. */

/* `pad` is not decoration: nk_do_button subtracts it, the border and the corner
 * radius from the content rect, and a symbol fills what is left - so it is the
 * only control over the glyph's size. */
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
     * generous makes a checkbox taller than the menu items above it. */
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

    /* A label on a filled accent, by measured contrast: the accent is a mid
     * blue in light and a pale one in dark, and no single choice reads on
     * both. */
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

    /* button and input are the two widgets tiny.css names, so they come from
     * their rules. Set globally as well as pushed per widget, so a page can
     * call nk_button_label with no ceremony. */
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

    /* Outlined in the muted text colour, not `edge`: --background-hover
     * against --background is sixteen levels, which disappears outright on a
     * panel of that colour - a checkbox in a menu had no visible box. */
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
    /* The glyph square is the row height less the padding, so without an inset
     * a 32px row gives a 28px icon. */
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
    /* Six pixels and a radius of three, rather than Nuklear's four and two.
     * A two-pixel arc does not survive the software renderer: every vertex of
     * it lands on the same pixel once the backend puts them on the grid, and
     * the cap came out square, which on a bar flush with the column edge read
     * as one running off it. Three is enough of an arc to stay an arc. */
    st->slider.bar_height    = 6.0f;
    st->slider.rounding      = 3.0f;
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
    /* No outline on the track either: at the button's corner it is a dark
     * ring round a bright fill, and the ring's own arc is a fill's arc, so it
     * steps where the fill under it does. The track's tone is enough to say
     * where the bar ends. */
    st->progress.border              = 0.0f;
    /* No outline on the fill: Nuklear strokes it in cursor_border_color on top,
     * which on a solid accent is a lighter line inside the bar. */
    st->progress.cursor_border       = 0.0f;
    /* tiny.css has no `progress` rule, so the track and the fill borrow the
     * button's corner. A rounded rect whose radius exceeds half its width
     * self-intersects and Nuklear does not clamp it, so a bar in its first
     * few per cent would draw as a knot; progress_cell clamps the fill's
     * radius to its own width before each call. */
    {
        curie_style bs;

        curie_style_get("button", &bs);
        st->progress.rounding        = bs.matched ? bs.rounding : 3.0f;
        st->progress.cursor_rounding = st->progress.rounding;
    }

    st->property.normal       = i_base;
    st->property.hover        = i_hover;
    st->property.active       = i_hover;
    st->property.border_color = edge;
    st->property.label_normal = st->property.label_hover =
        st->property.label_active = text;
    /* The value is a real edit widget, so it inherits `input` - whose 0.6rem
     * padding plus 2px border is more than a 30px row has, leaving the number
     * no height at all. It keeps the colours and takes its own geometry. */
    st->property.edit = st->edit;
    st->property.edit.normal = st->property.edit.hover =
        st->property.edit.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    st->property.edit.padding  = nk_vec2(2.0f, 2.0f);
    st->property.edit.border   = 0.0f;
    st->property.edit.rounding = 0.0f;
    /* A property's steppers are half the font's height square, not the row's,
     * so they take almost no inset before the arrow vanishes. */
    style_flat_button(&st->property.inc_button, hover, muted, 1.0f);
    style_flat_button(&st->property.dec_button, hover, muted, 1.0f);

    /* A combo is a select, and tiny.css has a rule for one - its own fill,
     * border, radius and padding rather than the card's background. */
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
    /* No symbol: nk_draw_symbol builds its chevron from the corners of the box,
     * so the angle is the aspect ratio - a wide, flat V. The page paints the
     * Ionicon over the button instead. */
    st->combo.sym_normal = st->combo.sym_hover = st->combo.sym_active =
        NK_SYMBOL_NONE;
    if (sel.matched) {
        /* Unmodified. Nuklear's antialiased stroke lands unevenly when the
         * rect is off whole pixels - 1px left against 2px right - and
         * widening only moved it; the fix is on the layout side. */
        /* Zero; the page strokes it - see combo_chrome in showcase.c. Same
         * stroke bias, and no style value evens it out. */
        st->combo.border   = 0.0f;
        st->combo.rounding = sel.rounding;

        /* CSS measures padding inside the border, Nuklear from the outer edge,
         * and the border is the page's to draw - so the two are added. Floored
         * at `input`'s inset: select's 0.25rem against input's 0.55rem read as
         * an oversight side by side. */
        {
            float inset = sel.pad_x + sel.border;
            if (inp.matched && inp.pad_x + inp.border > inset)
                inset = inp.pad_x + inp.border;
            st->combo.content_padding = nk_vec2(inset, sel.pad_y);
        }
    }
    style_flat_button(&st->combo.button, hover, muted, 7.0f);

    /* A tree is a <details> and its header a <summary>; both have rules. The
     * arrow comes from summary::before, which libcss cannot reach. */
    curie_style_get("details", &det);
    curie_style_get("summary", &sum);
    st->tab.background   = nk_style_item_color(
        det.matched ? curie_visible(col_of(det.bg), body, base) : base);
    /* Nuklear fills a tab header twice: the border rect at a literal 0
     * rounding, then the background at tab.rounding. The first is square
     * whatever the style says, so painting it in the surface behind, with no
     * border, leaves the rounded fill alone. */
    st->tab.border_color = body;
    /* One text colour for both kinds of tree row. `summary` resolves dimmer
     * and reaches only the plain nodes - element rows draw through
     * nk_style_selectable - so honouring it split the tree's colours. */
    st->tab.text         = text;
    st->tab.border = 0.0f;
    if (det.matched) {
        st->tab.rounding = det.rounding;
        st->tab.padding  = nk_vec2(det.pad_x, det.pad_y);
    }
    /* No symbol from Nuklear on tree headers or property steppers. Its
     * chevron is two one-pixel lines corner to corner of the box it is
     * handed - thin, and small beside the combo's - so the slots are left
     * empty and the showcase draws the Ionicon into them: chevron_at in
     * showcase.c. */
    st->tab.sym_minimize = NK_SYMBOL_NONE;
    st->tab.sym_maximize = NK_SYMBOL_NONE;
    st->property.sym_left  = NK_SYMBOL_NONE;
    st->property.sym_right = NK_SYMBOL_NONE;

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
    /* Inset from both ends of its track, so the thumb clears the tab strip and
     * the window edge. Only useful with the zeroed footer below. */
    st->scrollv.padding = nk_vec2(0.0f, 8.0f);

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

    /* Nuklear has one rounding for every panel, and the page, titlebar, tab
     * strip and body are all panels - taking it from `dialog` rounded the
     * window and then the titlebar inside it. The transient panels push their
     * own; see curie_popup_rounding. */
    st->window.rounding = 0.0f;
    st->window.combo_border_color      = edge;
    st->window.contextual_border_color = edge;
    /* 2px, not Nuklear's 1: a contextual popup is dynamic and fills its body
     * at nk_panel_end, unfeathered on the software renderer, so its corners
     * are a staircase up to half a pixel either side of the arc. A 1px rim
     * centred on that arc covers half of it; a 2px rim covers all of it and
     * the corner reads as the stroke's own feathered curve. */
    st->window.contextual_border       = 2.0f;
    st->window.menu_border_color       = edge;
    st->window.group_border_color      = edge;
    /* Not `edge`: --background-hover is both border and hover fill, so the
     * frame vanished into the button under it. */
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
 * Nuklear has no tab widget - nk_style_tab is the tree header - so a tab is a
 * flat button with an accent rule under the active one, sized to its own
 * label through the font. */
static void
tab_strip(App *app, struct nk_context *ctx, int win_w)
{
    const struct nk_user_font *font = pick_font(app, 16, 0);
    unsigned char c[4];
    struct nk_color accent = curie_style_token("--links", c)
                           ? col_of(c) : app->text;
    struct nk_color muted  = curie_style_token("--text-muted", c)
                           ? col_of(c) : app->text;
    struct nk_color wash   = curie_style_token("--background-hover", c)
                           ? col_of(c) : nk_rgba(128, 128, 128, 40);
    struct nk_color clear  = nk_rgba(0, 0, 0, 0);
    struct nk_command_buffer *canvas;
    struct nk_rect strip;
    struct nk_rect active_r = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    /* Which frame the window wears. Here rather than on the login card because
     * the card is one page of seven, and with the native frame in use there is
     * no drawn titlebar to put it in. */
    const char *swl = app->borderless ? "native titlebar" : "custom titlebar";
    float tabw[TAB_COUNT], themew[3], sw_w = 0.0f, rest = 0.0f;
    /* Clear air between the scheme switch and the titlebar switch: they do
     * unrelated things and should not read as one row of five words. */
    const float TAB_SEP = 26.0f;
    int i;

    (void)win_w;
    strip = nk_widget_bounds(ctx);      /* see the note in titlebar() */
    if (!nk_group_begin(ctx, "tabs", NK_WINDOW_NO_SCROLLBAR)) return;
    canvas = nk_window_get_canvas(ctx);
    /* Two lists in one row, and a reader should not be told they are one: the
     * pages are a tablist, the colour schemes are a group of their own. */
    curie_note_push(app, CURIE_A11Y_TABLIST, "Pages", NULL, 0, strip);

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
#ifdef __EMSCRIPTEN__
        /* No desktop frame to switch to: the canvas is the window. */
        sw_w = 0.0f;
#endif
        /* The group pads either side, and Nuklear inserts 4px between each
         * column: tabs, spacer, three scheme links, separator, switch. */
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

        {
            unsigned id = curie_note(app, CURIE_A11Y_TAB, name, NULL,
                                     i == app->tab ? CURIE_A11Y_SELECTED : 0u,
                                     b);
            /* Not `||`: that short-circuits, so a tab that was clicked would
             * leave the activation unconsumed and the frame would turn it
             * into a second press. */
            int hit = nk_button_label(ctx, name);

            if (curie_focus_activated(app, id)) hit = 1;
            if (hit) set_tab(app, i);
        }
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
    curie_note_pop(app);

    /* "system" follows SDL_GetSystemTheme(), the other two pin it. Changing it
     * reloads the stylesheets - tiny.css ships light and dark as two files. */
    curie_note_push(app, CURIE_A11Y_GROUP, "Colour scheme", NULL, 0, strip);
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

        curie_note(app, CURIE_A11Y_RADIO, g_theme_names[i], NULL,
                   i == app->theme_mode ? CURIE_A11Y_CHECKED : 0u, b);
        if (nk_button_label(ctx, g_theme_names[i]) && i != app->theme_mode) {
            /* Not applied here: see the top of SDL_AppIterate. */
            app->theme_pending = i + 1;
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

    nk_layout_row_push(ctx, TAB_SEP);
    nk_spacing(ctx, 1);
    curie_note_pop(app);

#ifndef __EMSCRIPTEN__
    nk_layout_row_push(ctx, sw_w);
    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 1);
        /* Its label says what it will switch to, so the checked state is the
         * frame it is *not* wearing - name it by what it does. */
        curie_note(app, CURIE_A11Y_BUTTON, swl, NULL, 0, b);
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
            /* Live, not at startup. SDL_SetWindowBordered puts the frame
             * back, and the hit test has to go with it or it keeps claiming
             * the top of a window that no longer draws a titlebar. */
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

#endif

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
 * Titlebar (only when frameless), tab strip, body. The body is a group so a
 * page can be taller than the window and need not know where on screen it is. */
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

    /* The login card is placed absolutely and never scrolls, so it goes into
     * this same space. Wrapping it in a body group cost a second full-window
     * fill every frame - four times the CPU while the pointer swept the card,
     * and invisible in build/render/present because it was in the renderer. */
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
    app->body_rect = nk_rect(0, top, (float)win_w, body_h);
    /* Keyboard focus landed outside the band above: bring it in, with a
     * little air, so the ring is not drawn against the edge. */
    if (app->focus_scroll) {
        const float air = 12.0f;
        struct nk_rect r = app->focus_scroll_rect;
        nk_uint sx, sy;
        float dy = 0.0f;

        nk_group_get_scroll(ctx, "body", &sx, &sy);
        if (r.y < top + air)
            dy = r.y - (top + air);
        else if (r.y + r.h > top + body_h - air)
            dy = r.y + r.h - (top + body_h - air);
        if ((float)sy + dy < 0.0f) dy = -(float)sy;
        nk_group_set_scroll(ctx, "body", sx, (nk_uint)((float)sy + dy));
        app->focus_scroll = 0;
        app->dirty = 1;
    }

    /* A showcase page can be taller than the window. No background of its
     * own: the window was already cleared to exactly this colour. */
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_hide());
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(20.0f, 12.0f));
    /* The y is the height reserved for a horizontal scrollbar, which
     * nk_panel_begin subtracts whether or not one is shown, with no matching
     * adjustment at the top - so the thumb ran flush into the tab strip while
     * keeping a 10px gap below. These pages never scroll sideways. */
    nk_style_push_vec2(ctx, &ctx->style.window.scrollbar_size,
                       nk_vec2(ctx->style.window.scrollbar_size.x, 0.0f));
    if (nk_group_begin(ctx, "body", 0)) {
        struct nk_vec2 sz = nk_window_get_content_region_size(ctx);

        /* Nuklear puts the first widget of a row one pixel left of the
         * group's content edge, and the group's scissor is exactly that
         * edge - so the leftmost widget in every row lost the outer pixel of
         * its 2px border and read as cut down one side. Visible on both
         * renderers, so it is the layout and not the rasteriser. The scissor
         * is widened by two pixels either side; the group spans the whole
         * window, so this stays well inside it. */
        {
            struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
            struct nk_rect c = cv->clip;

            nk_push_scissor(cv, nk_rect(c.x - 2.0f, c.y, c.w + 4.0f, c.h));
        }

        /* Popped here rather than after the group ends: both were read when
         * the panel began, and leaving the hidden background on the stack
         * meant every popup and menu inside the page inherited it. */
        nk_style_pop_vec2(ctx);            /* scrollbar_size */
        nk_style_pop_vec2(ctx);            /* group_padding */
        nk_style_pop_style_item(ctx);

        /* A backstop under the whole page, pushed first so the last-match rule
         * lets any widget override it. It asks for no repaint: controls that
         * change on hover register themselves. */
        hot_push(app, nk_rect(0, top, (float)win_w, body_h), 0, 0);

        /* The page is a container, named by its tab, so a reader is told
         * which page it is walking rather than handed a flat list. */
        app->page_node =
            curie_note_push(app, CURIE_A11Y_GROUP, curie_tab_names[app->tab],
                            NULL, 0, nk_rect(0, top, (float)win_w, body_h));
        curie_showcase_page(app, ctx, app->tab, sz.x, sz.y);
        curie_note_pop(app);
        nk_group_end(ctx);
    } else {
        nk_style_pop_vec2(ctx);
        nk_style_pop_vec2(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_space_end(ctx);
}

/* --- what a page may ask of the shell ------------------------------------
 * Declared in ui.h. Thin on purpose: the point of the indirection is only that
 * showcase.c never sees inside App. */

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

/* Below about twenty pixels an Ionicon's stroke - a fixed 6.25% of the glyph -
 * falls under one device pixel and greys out. The window controls hit this
 * first; the rule lives here so every small icon comes out solid. */
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

/* Rasterised at exactly the size it is drawn, at an explicit stroke weight:
 * both matter to a rim a pixel wide. `sw` at zero takes the hairline rule. */
struct nk_image
curie_ionicon_exact(App *app, const char *name, int px, struct nk_color stroke,
                    float sw)
{
    char src[192];

    if (sw <= 0.0f) sw = px < ICON_HAIRLINE_BELOW ? GLYPH_STROKE : 1.0f;
    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=#%02x%02x%02x"
                 "&sw=%.2f", name, stroke.r, stroke.g, stroke.b, (double)sw);
    return icon_over(app, src, px, 1.0f);
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

/* The label colour for something drawn on `bg`: the stylesheet's own unless it
 * fails the contrast floor, as for the accent button and the selection. */
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

/* A colour swatch: the button rule exactly as its neighbours draw it, with
 * the fill replaced and no label. Not nk_button_color, which draws its own
 * rect and so keeps none of the rule's geometry. */
int
curie_button_color(App *app, struct nk_context *ctx, const char *name,
                   struct nk_color fill)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    unsigned char hov[4];
    char hex[10];
    int clicked;

    hot_push(app, b, 1, 1);
    SDL_snprintf(hex, sizeof(hex), "#%02x%02x%02x", fill.r, fill.g, fill.b);
    curie_note(app, CURIE_A11Y_BUTTON, name, hex, 0, b);

    hov[0] = fill.r; hov[1] = fill.g; hov[2] = fill.b; hov[3] = fill.a;
    curie_style_darken(hov, 0.12f);

    /* Only the colours are pushed. Size, radius and padding stay whatever
     * the caller has in force, which is what makes this the same button as
     * the nk_button_image beside it rather than a lookalike - pushing the
     * `button` rule here instead put the CSS radius and padding over the
     * caller's, and the two stopped matching.
     *
     * The border takes the fill colour too, which is the one deviation and
     * the reason the corner matches. nk_draw_button fills the bounds in the
     * border colour and then fills the inset rect in the background, so the
     * border is a band and the fill's own arc sits at rounding - border.
     * With a grey rim the outer arc is there and measures identical to the
     * neighbour's, but grey on the page is a two-step difference nobody
     * sees: the eye reads the blue, and the blue is the tighter corner.
     * Colouring the band blue puts the visible edge back on the button's
     * outer arc, where its neighbour's is. */
    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(fill));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(col_of(hov)));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(col_of(hov)));
    nk_style_push_color(ctx, &ctx->style.button.border_color, fill);
    clicked = nk_button_label(ctx, "");
    nk_style_pop_color(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);

    return clicked;
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

/* The showcase's own fields. nk_edit_string keeps the caret and selection
 * inside Nuklear, so a page can have several; the login field uses
 * nk_edit_buffer because its context menu has to reach that state. */
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

    f = push_edit_style(ctx, &s, 0);
    state = nk_edit_string(ctx, flags, buf, len, cap, filter);
    pop_style(ctx, f);
    stroke_edit_edge(ctx, bounds, &s);
    note_ime_caret(app, ctx, bounds, state, &ctx->text_edit);
    /* The hint doubles as the label: it is the only text the field carries,
     * and an unnamed field is unusable to a reader. A box with no hint gets
     * its shape instead, which is at least a description. */
    curie_note(app, CURIE_A11Y_TEXTBOX,
               hint ? hint : ((flags & NK_EDIT_BOX) ? "Notes" : "Text"), buf,
               state & NK_EDIT_ACTIVE ? CURIE_A11Y_FOCUSED : 0u, bounds);

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
    return state;
}

/* --- the platform file picker ------------------------------------------
 * SDL_ShowOpenFileDialog returns at once and calls back later, possibly from
 * another thread. So the callback touches nothing but this struct: it fills in
 * the answer, publishes it with an atomic store, and pushes an event to wake a
 * main loop that may be parked in SDL_WaitEvent. Nuklear, the renderer and the
 * style are main-thread only and are left to curie_file_taken. */

static const SDL_DialogFileFilter g_file_filters[] = {
    { "Stylesheets", "css" },
    { "All files",   "*"   }
};

static void SDLCALL
file_chosen(void *userdata, const char * const *filelist, int filter)
{
    App *app = (App *)userdata;
    SDL_Event wake;

    (void)filter;
    if (!filelist)
        SDL_snprintf(app->file_answer, sizeof(app->file_answer),
                     "unavailable: %s", SDL_GetError());
    else if (!filelist[0])
        SDL_strlcpy(app->file_answer, "cancelled", sizeof(app->file_answer));
    else
        SDL_strlcpy(app->file_answer, filelist[0], sizeof(app->file_answer));

    /* Release: everything written above is visible to the thread that sees
     * this. SDL's atomics are full barriers. */
    SDL_SetAtomicInt(&app->file_ready, 1);

    SDL_zero(wake);
    wake.type = app->wake_event;
    SDL_PushEvent(&wake);
}

int
curie_file_open(App *app)
{
    if (app->file_pending) return 0;
    app->file_pending = 1;
    SDL_ShowOpenFileDialog(file_chosen, app, app->win, g_file_filters,
                           (int)NK_LEN(g_file_filters), NULL, false);
    return 1;
}

int
curie_file_taken(App *app, char *out, int cap)
{
    if (!SDL_GetAtomicInt(&app->file_ready)) return 0;
    SDL_SetAtomicInt(&app->file_ready, 0);
    app->file_pending = 0;
    SDL_strlcpy(out, app->file_answer, (size_t)cap);
    return 1;
}

/* CURIE_A11Y_DUMP=<path> writes the tree once, after the first frame is built,
 * and is how phase 2 is checked: the instrumentation is invisible on screen, so
 * the only way to see whether a widget reported itself is to read the tree. */
static void
a11y_dump_once(App *app)
{
    static int done;
    const char *path;
    FILE *f;

    if (done) return;
    path = SDL_getenv("CURIE_A11Y_DUMP");
    if (!path) { done = 1; return; }
    done = 1;
    f = fopen(path, "w");
    if (!f) return;
    curie_a11y_dump(&app->a11y, f);
    {
        int n = 0;
        curie_a11y_tree(&app->a11y, &n);
        fprintf(f, "\n# %d of %d nodes, %d of %d string bytes, "
                   "%d interned, struct %d bytes\n",
                n, CURIE_A11Y_MAX_NODES, app->a11y.pool_used,
                CURIE_A11Y_POOL, app->a11y.pool_entries,
                (int)sizeof(curie_a11y));
    }
    fclose(f);
}

/* --- describing the frame ----------------------------------------------- */
/* Thin forwards onto app->a11y, so App stays opaque to the pages and a11y.h
 * stays free of it. Every report passes through focus_saw, which is how the
 * frame learns where the focused node was drawn without any widget knowing
 * that focus exists. */

static void
focus_saw(App *app, unsigned id, struct nk_rect b)
{
    if (id && id == app->focus_id) {
        app->focus_rect = b;
        app->focus_seen = 1;
    }
}

unsigned
curie_note(App *app, unsigned char role, const char *name, const char *value,
           unsigned state, struct nk_rect bounds)
{
    unsigned id = curie_a11y_add(&app->a11y, role, name, value, state, bounds);

    focus_saw(app, id, bounds);
    return id;
}

unsigned
curie_note_push(App *app, unsigned char role, const char *name,
                const char *value, unsigned state, struct nk_rect bounds)
{
    unsigned id = curie_a11y_push(&app->a11y, role, name, value, state, bounds);

    focus_saw(app, id, bounds);
    return id;
}

void
curie_note_pop(App *app)
{
    curie_a11y_pop(&app->a11y);
}

void
curie_note_range(App *app, unsigned id, float num, float lo, float hi,
                 float step)
{
    curie_a11y_set_range(&app->a11y, id, num, lo, hi, step);
}

/* nk_widget_bounds answers where the *next* widget goes, so this is called
 * before drawing, not after - which is also when the caller still knows what
 * it is about to draw. */
unsigned
curie_note_here(App *app, struct nk_context *ctx, unsigned char role,
                const char *name, unsigned state)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    unsigned id = curie_a11y_add(&app->a11y, role, name, NULL, state, b);

    focus_saw(app, id, b);
    return id;
}

/* --- keyboard focus ----------------------------------------------------- */
/* Phase 3 of docs/ACCESSIBILITY.md. Nuklear has no focus model - NK_KEY_TAB
 * inserts a tab - so the shell keeps one, over the tree the frame just
 * described. */

enum {
    FOCUS_NEXT = 1, FOCUS_PREV,        /* Tab, Shift-Tab: every focusable node */
    FOCUS_FIRST, FOCUS_LAST,           /* Home, End */
    FOCUS_SIB_NEXT, FOCUS_SIB_PREV     /* arrows: siblings only */
};

/* Whether the node at `i` has `ancestor` above it. The tree is flat and a
 * node names its parent by id, so this is the walk up; the guard is the
 * tree's own depth limit, which a cycle could not exceed. */
static int
node_under(const curie_a11y_node *t, int n, int i, unsigned ancestor)
{
    int guard = 0;

    while (t[i].parent && guard++ < CURIE_A11Y_MAX_DEPTH) {
        int k;

        if (t[i].parent == ancestor) return 1;
        for (k = 0; k < n; k++) if (t[k].id == t[i].parent) break;
        if (k == n) return 0;
        i = k;
    }
    return 0;
}

static int
focusable(const curie_a11y_node *n)
{
    switch (n->role) {
    case CURIE_A11Y_TAB:      case CURIE_A11Y_BUTTON:   case CURIE_A11Y_LINK:
    case CURIE_A11Y_CHECKBOX: case CURIE_A11Y_RADIO:    case CURIE_A11Y_TEXTBOX:
    case CURIE_A11Y_SLIDER:   case CURIE_A11Y_SPINBUTTON:
    case CURIE_A11Y_COMBOBOX: case CURIE_A11Y_LISTITEM: case CURIE_A11Y_TREEITEM:
    case CURIE_A11Y_MENUITEM:
        /* Off-window nodes count: the page scrolls to them - see focus_move. */
        return !(n->state & CURIE_A11Y_DISABLED);
    default:
        return 0;
    }
}

/* Moves focus now, against the tree of the last drawn frame - which is
 * complete, and still valid between frames. At the key rather than after
 * the next frame, because keys arrive faster than frames and two Tabs that
 * landed before one frame were merging into a single move. Tab walks every
 * focusable node in reading order and wraps; the arrows stay among
 * siblings, which is what makes a tab strip or a menu behave as one
 * control. A node that scrolled out of the window is skipped rather than
 * scrolled to: nothing here can move a group's scroll yet. */
static void
focus_move(App *app, int step)
{
    int n, i, cur = -1, pick = -1;
    const curie_a11y_node *t = curie_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) { cur = i; break; }

    if (step == FOCUS_FIRST || step == FOCUS_LAST) {
        int d = step == FOCUS_FIRST ? 1 : -1;
        for (i = d > 0 ? 0 : n - 1; i >= 0 && i < n; i += d)
            if (focusable(&t[i])) { pick = i; break; }
    } else {
        int d   = (step == FOCUS_NEXT || step == FOCUS_SIB_NEXT) ? 1 : -1;
        int sib = (step == FOCUS_SIB_NEXT || step == FOCUS_SIB_PREV) && cur >= 0;
        int k, start = cur >= 0 ? cur : (d > 0 ? -1 : n);
        for (k = 1; k <= n; k++) {
            i = ((start + d * k) % n + n) % n;
            if (i == cur) break;
            if (!focusable(&t[i])) continue;
            if (sib && t[i].parent != t[cur].parent) continue;
            pick = i;
            break;
        }
    }
    if (pick < 0) return;

    app->focus_id = t[pick].id;
    curie_a11y_set_focus(&app->a11y, app->focus_id);
    /* The rect comes with the pick, so Enter a moment later presses this
     * node and not the one before it. The frame after draws the ring. */
    app->focus_rect    = t[pick].bounds;
    app->focus_seen    = 1;
    app->focus_visible = 1;
    app->dirty = 1;
    /* Inside the page and outside its visible band: the page scrolls to it
     * on the frame that follows, where the group's scroll can be set. The
     * band and the node's bounds are both as of the last frame, which is
     * the frame the node was measured in, so they agree. */
    {
        const struct nk_rect *b = &app->body_rect, *r = &t[pick].bounds;

        if (node_under(t, n, pick, app->page_node) &&
            (r->y < b->y || r->y + r->h > b->y + b->h)) {
            app->focus_scroll      = 1;
            app->focus_scroll_rect = *r;
        }
    }
    /* Tab into a field puts the caret in it, as it does everywhere else. */
    if (t[pick].role == CURIE_A11Y_TEXTBOX) {
        app->key_click   = 1;
        app->key_click_x = t[pick].bounds.x + t[pick].bounds.w * 0.5f;
        app->key_click_y = t[pick].bounds.y + t[pick].bounds.h * 0.5f;
    }
}

/* After a frame: a focus whose node went away - another page, a closed
 * menu, a scroll - is dropped rather than left pointing at nothing. */
static void
focus_resolve(App *app)
{
    int n, i;
    const curie_a11y_node *t = curie_a11y_tree(&app->a11y, &n);

    if (!app->focus_id) return;
    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) {
            if (focusable(&t[i])) return;
            break;
        }
    app->focus_id = 0;
    curie_a11y_set_focus(&app->a11y, 0);
}

/* Whether what has focus takes the arrows as a value rather than as a move. */
static int
focus_is_range(App *app)
{
    int n, i;
    const curie_a11y_node *t = curie_a11y_tree(&app->a11y, &n);

    if (!app->focus_id) return 0;
    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id)
            return t[i].role == CURIE_A11Y_SLIDER ||
                   t[i].role == CURIE_A11Y_SPINBUTTON;
    return 0;
}

/* Tab walks the focusable nodes, the arrows walk siblings, Home and End
 * jump, Enter and Space press. Answers whether the key was taken, so the
 * caller keeps it from Nuklear - otherwise Tab would still land in a field
 * as a character. While a field is being edited the editor keeps every key
 * but Tab, which leaves it. The keypad arrows count: with Num Lock off
 * they are the only arrows some keyboards have. */
static int
focus_key(App *app, const SDL_Event *event)
{
    struct nk_window *pw = nk_window_find(app->ctx, "page");
    int editing = pw && pw->edit.active;

    if (event->key.key == SDLK_TAB) {
        if (editing) pw->edit.active = nk_false;
        focus_move(app, (event->key.mod & SDL_KMOD_SHIFT) ? FOCUS_PREV
                                                         : FOCUS_NEXT);
        app->dirty = 1;
        return 1;
    }
    if (editing) return 0;

    switch (event->key.key) {
    /* On a range the arrows are the value, not the focus - Right and Up up,
     * Left and Down down, which is what every platform's slider does. The
     * shell cannot apply the step itself: it knows the node's value as the
     * text a reader would hear and nothing of its bounds or its grain, so it
     * records the direction and the widget answers it (curie_focus_step). */
    case SDLK_RIGHT: case SDLK_KP_6: case SDLK_UP: case SDLK_KP_8:
        if (focus_is_range(app)) { app->focus_step++; break; }
        focus_move(app, (event->key.key == SDLK_UP ||
                         event->key.key == SDLK_KP_8) ? FOCUS_SIB_PREV
                                                      : FOCUS_SIB_NEXT);
        break;
    case SDLK_LEFT:  case SDLK_KP_4: case SDLK_DOWN: case SDLK_KP_2:
        if (focus_is_range(app)) { app->focus_step--; break; }
        focus_move(app, (event->key.key == SDLK_DOWN ||
                         event->key.key == SDLK_KP_2) ? FOCUS_SIB_NEXT
                                                      : FOCUS_SIB_PREV);
        break;
    case SDLK_HOME:  case SDLK_KP_7: focus_move(app, FOCUS_FIRST); break;
    case SDLK_END:   case SDLK_KP_1: focus_move(app, FOCUS_LAST);  break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
        if (!app->focus_id || !app->focus_seen) return 0;
        /* Offered to the widget first; the frame that ends without it taken
         * turns it into the click this used to be. */
        app->activate_id   = app->focus_id;
        app->focus_visible = 1;
        break;
    case SDLK_ESCAPE:
        /* Hides the ring; Nuklear may want the key as well. */
        app->focus_visible = 0;
        app->dirty = 1;
        return 0;
    default:
        return 0;
    }
    app->dirty = 1;
    return 1;
}

/* What a reader's press and a reader's move become: the same click and the
 * same focus a key would make, so a node reached through the platform
 * behaves exactly as one reached through Tab - see a11y_web.c. */
static void
reader_focus(void *user, unsigned id)
{
    App *app = (App *)user;
    int n, i;
    const curie_a11y_node *t = curie_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == id) {
            app->focus_id = id;
            curie_a11y_set_focus(&app->a11y, id);
            app->focus_rect    = t[i].bounds;
            app->focus_seen    = 1;
            app->focus_visible = 1;
            app->dirty = 1;
            return;
        }
}

static void
reader_activate(void *user, unsigned id)
{
    App *app = (App *)user;
    SDL_Event e;

    reader_focus(user, id);
    if (app->focus_id != id) return;
    app->activate_id = id;
    /* The widget takes it on the next frame, and dirty is cleared at the end
     * of this one - so the frame has to be asked for. */
    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    SDL_PushEvent(&e);
}

/* Whether this node was asked to activate, taken once.
 *
 * Enter and a platform client's press both used to become a synthetic mouse
 * click at the node's centre, and for widgets inside the page group that was
 * not reliable: the click reaches Nuklear's input and the widget does not act
 * on it, while the identical injection from a key does. Rather than keep
 * chasing that, a widget can be told directly - it knows its own state and
 * changing it is a line - and the click stays only as the fallback for the
 * ones that have not been taught to listen. */
int
curie_focus_activated(App *app, unsigned id)
{
    if (!id || id != app->activate_id) return 0;
    app->activate_id = 0;
    return 1;
}

/* How many steps the arrows asked this node for, taken once. Zero for every
 * node but the focused one, and for that one only until it has read it. */
int
curie_focus_step(App *app, unsigned id)
{
    int s;

    if (!id || id != app->focus_id || !app->focus_step) return 0;
    s = app->focus_step;
    app->focus_step = 0;
    return s;
}

/* The ring, from one place once the page has drawn: 2px in --focus, a pixel
 * outside the node so it never covers the widget's own edge. Shown once a
 * key has moved focus and hidden by the next click - the :focus-visible
 * rule every desktop follows.
 *
 * Clipped to the page's band when the focused node is in the page. The ring
 * is drawn after the group has ended, so nothing else would clip it, and a
 * node half scrolled under the band's edge was getting a whole ring - drawn
 * over the tab strip above it, or over the window's edge below. */
static void
focus_ring(App *app, struct nk_context *ctx)
{
    unsigned char c[4];
    struct nk_color col = curie_style_token("--focus", c)
                        ? col_of(c) : nk_rgb(0x56, 0xc7, 0xff);
    struct nk_rect r = app->focus_rect;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_rect save = cv->clip;
    int n, i, in_page = 0;
    const curie_a11y_node *t = curie_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) {
            in_page = node_under(t, n, i, app->page_node);
            break;
        }
    if (in_page) nk_push_scissor(cv, app->body_rect);
    nk_stroke_rect(cv,
                   nk_rect(r.x - 2.0f, r.y - 2.0f, r.w + 4.0f, r.h + 4.0f),
                   5.0f, 2.0f, col);
    if (in_page) nk_push_scissor(cv, save);
}

/* The radius for a popup, tooltip or menu: `dialog`'s, capped, because 1rem is
 * a pill at tooltip height. Pushed around those calls, not set globally. */
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
    /* what the frame is drawn with, not what was asked for - see the note at
     * nk_sdl_render_ex in the frame body */
    out->aa = !app->aa ? "off"
            : !app->renderer_is_sw ? "on"
            : app->sw_noaa ? "off (software renderer)"
            : "strokes only (software renderer)";
    out->dark       = app->dark;
    out->sheets     = SHEET_COUNT;
    out->tab        = app->tab;
    out->scale      = curie_scale();
    out->style_ms   = app->style_ms_x100 / 100.0f;
    out->frame_gap_ms = app->frame_gap_ms;
    out->cpu_ms_per_frame = app->cpu_ms_per_frame;
    out->hover_gap_ms     = (int)g_hover_gap_ms;

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

    {
        size_t rss = 0, priv = 0;
        curie_process_memory(&rss, &priv);
        out->rss_bytes     = (unsigned long)rss;
        out->private_bytes = (unsigned long)priv;
    }
    out->atlas_w    = app->atlas_w;
    out->atlas_h    = app->atlas_h;
    out->atlas_bpp  = app->atlas_bpp;
    {
        int i;
        for (i = 0; i < RSS_STEPS; i++) {
            out->rss_at[i]  = (unsigned long)g_rss[i];
            out->priv_at[i] = (unsigned long)g_priv[i];
        }
    }
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
/* The viewport, in CSS pixels. Read from the window, not measured off an
 * element: the body's box grows to whatever the canvas is, and SDL sizes the
 * canvas from this answer. */
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

/* Is the renderer we were given a hardware one, or Direct3D talking to a
 * software rasteriser?
 *
 * It matters more than it sounds. Where there is no GPU, SDL's direct3d11
 * backend still succeeds - it falls back to WARP, Microsoft's software
 * implementation - and the app then pays for a full D3D11 pipeline and a
 * WDDM swapchain present to draw a few hundred flat triangles. Measured on a
 * VirtualBox VM with no 3D: 78.7 ms of CPU per frame through WARP against
 * 13.1 ms through SDL's own software renderer, medians of four interleaved
 * runs. Six times, for the same picture.
 *
 * WARP identifies itself in the adapter description, so this asks the device
 * SDL created rather than guessing from the adapter list: VirtualBox presents
 * a WDDM adapter that looks real until a hardware device fails to create on
 * it. Anything other than a confident "this is software" answers 0 and the
 * renderer is left alone. */
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
/* COBJMACROS gives the C-callable Xxx_Method() forms; dxgi.h alone, because
 * the device is only ever handled as an IUnknown here, so d3d11.h and its
 * dependency chain are not needed. */
#define COBJMACROS
#include <dxgi.h>
static int
renderer_is_software(SDL_Renderer *ren)
{
    IUnknown *dev;
    IDXGIDevice *dxgi = NULL;
    IDXGIAdapter *ad = NULL;
    DXGI_ADAPTER_DESC desc;
    int software = 0;

    dev = (IUnknown *)SDL_GetPointerProperty(
        SDL_GetRendererProperties(ren),
        SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL);
    if (!dev) return 0;

    if (SUCCEEDED(IUnknown_QueryInterface(dev, &IID_IDXGIDevice,
                                          (void **)&dxgi)) && dxgi) {
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &ad)) && ad) {
            if (SUCCEEDED(IDXGIAdapter_GetDesc(ad, &desc))) {
                /* 0x1414 is Microsoft; WARP and the Basic Render Driver both
                 * sit under it. The description is checked as well, because a
                 * vendor id alone would also match a real Microsoft device. */
                software = desc.VendorId == 0x1414 ||
                           wcsstr(desc.Description, L"Basic Render") != NULL ||
                           wcsstr(desc.Description, L"WARP") != NULL;
            }
            IDXGIAdapter_Release(ad);
        }
        IDXGIDevice_Release(dxgi);
    }
    return software;
}
#else
static int renderer_is_software(SDL_Renderer *ren) { (void)ren; return 0; }
#endif

/* --- SDL application callbacks ----------------------------------------- */

SDL_AppResult
SDL_AppInit(void **appstate, int argc, char *argv[])
{
    App *app;

    /* Two SDL defaults a desktop app should not inherit, both read during
     * SDL_Init: the screensaver is disabled unless told otherwise
     * (ES_DISPLAY_REQUIRED for the process's life), and
     * SDL_HINT_TIMER_RESOLUTION defaults to a system-wide timeBeginPeriod(1)
     * that only a run which paces has anything to spend. */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_strcmp(frame_rate_wanted(), "waitevent") == 0)
        SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");

    rss_mark(RSS_ENTRY);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    rss_mark(RSS_SDL);

    app = (App *)SDL_calloc(1, sizeof(App));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    {
        /* CURIE_RENDERER takes "auto" (the default), "cpu", "gpu", or the
         * name of any driver SDL has compiled in - on Windows that is
         * direct3d11, opengl, opengles2 or software; elsewhere whatever the
         * platform builds. "list" prints them and exits, so the names do not
         * have to be guessed.
         *
         * "auto" is not simply SDL's own choice: see the software fallback
         * after the window is created. "gpu" pins the first non-software
         * driver and keeps it, which is how the two were measured against
         * each other. */
        const char *mode = SDL_getenv("CURIE_RENDERER");
        int i, n = SDL_GetNumRenderDrivers();

        if (!mode || !*mode) mode = "auto";
        SDL_strlcpy(app->render_mode, mode, sizeof(app->render_mode));

        if (SDL_strcmp(mode, "list") == 0) {
            for (i = 0; i < n; i++) SDL_Log("%s", SDL_GetRenderDriver(i));
            return SDL_APP_SUCCESS;
        }
        if (SDL_strcmp(mode, "cpu") == 0) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        } else if (SDL_strcmp(mode, "gpu") == 0) {
            for (i = 0; i < n; i++) {
                const char *d = SDL_GetRenderDriver(i);
                if (d && SDL_strcmp(d, "software") != 0) {
                    SDL_SetHint(SDL_HINT_RENDER_DRIVER, d);
                    break;
                }
            }
        } else if (SDL_strcmp(mode, "auto") != 0) {
            /* A driver by name. Checked against the list rather than passed
             * through, so a typo says so instead of silently landing back on
             * SDL's default and looking like the setting did nothing. */
            int known = 0;
            for (i = 0; i < n; i++)
                if (SDL_strcmp(SDL_GetRenderDriver(i), mode) == 0) known = 1;
            if (known) {
                SDL_SetHint(SDL_HINT_RENDER_DRIVER, mode);
            } else {
                SDL_Log("CURIE_RENDERER=%s is not a driver this build has; "
                        "try CURIE_RENDERER=list", mode);
                SDL_strlcpy(app->render_mode, "auto", sizeof(app->render_mode));
            }
        }
    }

    /* Borderless by default, so the titlebar follows the stylesheet like
     * everything else. CURIE_BORDERLESS=0 restores the desktop's frame, which
     * is worth keeping - a custom titlebar gives up what the platform does
     * for free. */
    /* Off in a browser: no desktop frame to replace, the canvas is the whole
     * window, and a titlebar inside it could neither move nor resize. */
#ifdef __EMSCRIPTEN__
    app->borderless = env_int("CURIE_BORDERLESS", 0) != 0;
#else
    app->borderless = env_int("CURIE_BORDERLESS", 1) != 0;
#endif

    /* Which page opens - the starting tab only. A screenshot of one page
     * should not depend on a click at coordinates guessed from outside. */
    app->tab = env_int("CURIE_TAB", TAB_LOGIN);
    if (app->tab < 0 || app->tab >= TAB_COUNT) app->tab = TAB_LOGIN;
    app->scroll0 = env_int("CURIE_SCROLL", 0);
    if (app->scroll0 < 0) app->scroll0 = 0;

    {
        /* On a real HiDPI display SDL_WINDOW_HIGH_PIXEL_DENSITY gives this
         * window scale-times as many pixels, which is exactly the room the
         * same logical layout needs. A forced CURIE_SCALE has no such display
         * behind it, so the window is asked for that many pixels here instead
         * - otherwise the simulation draws a scaled UI into an unscaled window
         * and simply clips it. curie_dpi_query_scale(NULL) is the override, or
         * 1.0 when unset. */
        float pre = curie_dpi_query_scale(NULL);
        int win_w = (int)(WINDOW_WIDTH * pre + 0.5f);
        int win_h = (int)(WINDOW_HEIGHT * pre + 0.5f);

#ifdef __EMSCRIPTEN__
        /* On the web the window is the page: a fixed canvas in a blank
         * document is a screenshot of a desktop app, and the app could never
         * see a size the user chose. */
        web_page_size(&win_w, &win_h);
#endif
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE |
                                SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                (app->borderless ? SDL_WINDOW_BORDERLESS : 0);

        if (!SDL_CreateWindowAndRenderer("Curie", win_w, win_h, flags,
                                         &app->win, &app->ren)) {
            /* A driver can be in SDL's list and still fail here: opengl and
             * opengles2 are both compiled in on Windows and neither creates
             * on a VM with no 3D. A named driver that cannot run is worth a
             * line and a retry, not a dead app. */
            SDL_Log("renderer '%s' would not start (%s); falling back",
                    app->render_mode, SDL_GetError());
            if (app->win) { SDL_DestroyWindow(app->win); app->win = NULL; }
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, NULL);
            SDL_strlcpy(app->render_mode, "auto", sizeof(app->render_mode));
            if (!SDL_CreateWindowAndRenderer("Curie", win_w, win_h, flags,
                                             &app->win, &app->ren)) {
                SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
                return SDL_APP_FAILURE;
            }
        }

        /* No GPU: Direct3D has landed on WARP, Microsoft's software
         * implementation, and costs six times what SDL's own software
         * renderer costs here - 78.7 ms of CPU a frame against 13.1. So
         * `auto` swaps to SDL's. That swap was written once before and taken
         * out, because the software rasteriser drew the page differently;
         * the renderer section in DEVELOPMENT.md is the account of closing
         * that gap, and what remains of it is accepted. A driver asked for
         * by name is kept, and reported as running with no GPU behind it. */
        if (renderer_is_software(app->ren)) {
            if (SDL_strcmp(app->render_mode, "auto") == 0) {
                SDL_Renderer *sw;

                /* SDL_CreateRenderer on a window that already has one fails
                 * quietly, so the old one goes first. */
                SDL_DestroyRenderer(app->ren);
                app->ren = NULL;
                sw = SDL_CreateRenderer(app->win, "software");
                if (!sw) sw = SDL_CreateRenderer(app->win, NULL);
                if (!sw) {
                    SDL_Log("no renderer after the WARP swap: %s",
                            SDL_GetError());
                    return SDL_APP_FAILURE;
                }
                app->ren = sw;
                SDL_strlcpy(app->render_mode, "auto: software (no GPU)",
                            sizeof(app->render_mode));
            } else {
                SDL_strlcpy(app->render_mode + SDL_strlen(app->render_mode),
                            " (no GPU)",
                            sizeof(app->render_mode) -
                                SDL_strlen(app->render_mode));
            }
        }
        app->renderer_is_sw =
            SDL_strcmp(SDL_GetRendererName(app->ren), "software") == 0;
        app->sw_noaa = env_int("CURIE_SW_NOAA", 0) != 0;
    }

#ifdef __EMSCRIPTEN__
    /* SDL sizes the canvas, so a browser resize has to be pushed back into
     * SDL rather than the other way round. */
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 0,
                                   web_on_resize);
#endif
    /* Phase 4: whatever platform can read the tree. The web mirrors it into
     * the DOM; the desktop bridges are still ahead, and until then this
     * registers the callbacks and does nothing else. */
    curie_a11y_platform_init(reader_activate, reader_focus, app);

    /* Without this the loop presents as fast as the GPU allows, which was
     * half of the idle CPU cost. */
    {
        int want = env_int("CURIE_VSYNC", 1);
        app->vsync_on = SDL_SetRenderVSync(app->ren, want ? 1 : 0) ? want : 0;
    }
    app->aa = env_int("CURIE_AA", 1);
    app->redraw_always = SDL_getenv("CURIE_REDRAW") &&
                         SDL_strcmp(SDL_getenv("CURIE_REDRAW"), "always") == 0;

    /* How often SDL calls SDL_AppIterate. The loop used to spin and sleep 2 ms
     * whenever the frame was clean - 500 wake-ups a second to find nothing to
     * do. "waitevent" blocks until an event arrives. CURIE_FRAME_RATE
     * overrides with a cap, or 0 for uncapped as CURIE_REDRAW=always needs. */
    SDL_strlcpy(app->frame_rate, frame_rate_wanted(), sizeof(app->frame_rate));

    /* The rate while the pointer is held: the display's refresh, one frame
     * each. "0" is worse - presenting faster than the display accepts fills
     * the swapchain and present blocks on it, measured at 29 ms a frame
     * against 0.11 ms of building, holding a drag at 35 fps. */
    {
        const SDL_DisplayMode *m =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app->win));
        float hz = (m && m->refresh_rate > 1.0f) ? m->refresh_rate : 60.0f;
        SDL_snprintf(app->drag_rate, sizeof(app->drag_rate), "%.2f", hz);
    }

    if (app->borderless) SDL_SetWindowHitTest(app->win, window_hit_test, app);
    /* Either way it can be changed from the card at runtime. */

    rss_mark(RSS_WINDOW);

    /* Its own milestone: the first thing to touch plutosvg, and folding that
     * into the renderer's figure is what this table exists to prevent. */
    set_window_icon(app->win);
    curie_set_scale(curie_dpi_query_scale(app->win));   /* needs the window */
    apply_render_scale(app);
    rss_mark(RSS_ICON);

    app->ctx = nk_sdl_init(app->win, app->ren, nk_sdl_allocator());
    if (!app->ctx) return SDL_APP_FAILURE;
    rss_mark(RSS_NUKLEAR);

    rebuild_font(app);
    rss_mark(RSS_FONT);

    /* One type, for the file picker's callback to wake the loop with. Zero
     * on failure, which the handler treats as "never matches". */
    app->wake_event = SDL_RegisterEvents(1);

    if (SDL_getenv("CURIE_HOVER_GAP_MS")) {
        int gap = env_int("CURIE_HOVER_GAP_MS", (int)HOVER_GAP_MS);
        if (gap >= 0 && gap <= 1000) {
            g_hover_gap_ms   = (Uint64)gap;
            g_hover_gap_pinned = 1;
        }
    }

    app->cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->cur_text    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);

    /* Pinned from the environment, same reason as CURIE_TAB: a screenshot of
     * the light palette should not depend on clicking a link. The links on the
     * strip still change it. */
    app->theme_mode = env_int("CURIE_THEME", THEME_SYSTEM);
    if (app->theme_mode < 0 || app->theme_mode > THEME_DARK)
        app->theme_mode = THEME_SYSTEM;

    /* Follow the desktop until told otherwise: load_theme() reads
     * SDL_GetSystemTheme() and picks the matching tiny.css palette. */
    nk_textedit_init_fixed(&app->edit, app->edit_buf, sizeof(app->edit_buf));

    load_theme(app);
    rss_mark(RSS_STYLE);

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
        /* The picker's callback pushes this from SDL's thread. Its only job is
         * to get the loop out of SDL_WaitEvent; the answer is collected during
         * the frame. Checked before the switch because the type is allocated
         * at startup and so cannot be a case label. */
        if (app->wake_event && event->type == app->wake_event) app->dirty = 1;

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

            if (app->dragging) { app->drag_moved = 1; app->dirty = 1; }

            /* The last match, not the first: the shell pushes one region over
             * a whole showcase page before the page pushes its widgets, so the
             * catch-all is always overridden by anything drawn inside it. */
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
                    if (was || is) hover_redraw(app);
                } else if (has && app->hot[over].track) {
                    /* Drawn at the pointer, so it has to be redrawn as the
                     * pointer moves - a crossing is not enough. */
                    hover_redraw(app);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            app->focus_visible = 0;
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
            /* A fixed-rate loop at the display's refresh for the gesture,
             * with the frame drawn unconditionally. Measured: vsync present
             * blocked two refresh intervals (29 ms against 0.4 ms of work),
             * 33 fps; SDL pacing off the ~15.6 ms Windows timer, 45 fps.
             * "waitevent" is wrong here too - Windows coalesced 250 synthetic
             * moves a second down to 40, so the selection advanced unevenly.
             * Vsync stays on: present then blocks for one interval rather
             * than occasionally two. */
            SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->drag_rate);
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            /* Whatever happens to the button now happens to another window.
             * Without this the app keeps redrawing at the display's refresh
             * until something else stops it. */
            if (app->dragging) {
                app->dragging      = 0;
                app->drag_in_field = 0;
                app->restore_rate  = 2;
            }
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
            /* One portable event in place of WM_SETTINGCHANGE plus the macOS
             * and XDG-portal paths. Only meaningful while unpinned. */
            if (app->theme_mode == THEME_SYSTEM) load_theme(app);
            break;

        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            curie_set_scale(curie_dpi_query_scale(app->win));
            apply_render_scale(app);
            rebuild_font(app);
            break;

        case SDL_EVENT_KEY_DOWN:
            /* F1 opens the diagnostics, which are a page of their own now
             * rather than four lines squeezed into the login card. */
            if (event->key.key == SDLK_F1) { set_tab(app, TAB_DIAG); break; }

            /* The tab strip, from the keyboard. It is the only navigation the
             * app owns and it was mouse-only, which is the cheapest piece of
             * accessibility available here: Nuklear has no focus traversal to
             * hook into, but a page can still be reached without a pointer.
             * Ctrl-modified so a focused text field keeps plain Tab. */
            if (event->key.mod & SDL_KMOD_CTRL) {
                if (event->key.key == SDLK_TAB) {
                    int d = (event->key.mod & SDL_KMOD_SHIFT) ? -1 : 1;
                    set_tab(app, (app->tab + d + TAB_COUNT) % TAB_COUNT);
                } else if (event->key.key >= SDLK_1 &&
                           event->key.key < SDLK_1 + TAB_COUNT) {
                    set_tab(app, (int)(event->key.key - SDLK_1));
                }
            } else if (focus_key(app, event)) {
                /* Taken: it does not reach Nuklear at all. */
                return SDL_APP_CONTINUE;
            }
            break;

        case SDL_EVENT_USER:
            /* Pushed by the frame that pressed a key-click, so the frame
             * that releases it is sure to follow. */
            app->dirty = 1;
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

    /* A scheme picked last frame, applied here and not where it was clicked.
     * The links that pick it push eight button style items around themselves,
     * and load_theme writes the new palette straight into ctx->style - so
     * reloading between a push and its pop had the pop put the old colours
     * back, and every button kept the scheme it had just left while the page
     * around it changed. Nothing is pushed at the top of a frame. */
    if (app->theme_pending) {
        app->theme_mode    = app->theme_pending - 1;
        app->theme_pending = 0;
        load_theme(app);
    }

    SDL_GetWindowSize(app->win, &win_w, &win_h);
    if (win_w != app->laid_w || win_h != app->laid_h) app->dirty = 1;

    /* A hover frame deferred by hover_redraw: draw it once the gap has passed
     * and hand the callback rate back. Not during a drag, which owns the rate
     * and draws every tick regardless. */
    if (app->hover_pending &&
        SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms) {
        app->hover_pending = 0;
        app->dirty = 1;
        if (!app->dragging) app->restore_rate = 1;
    }

    /* Self-heal a drag whose button-up never arrived. While `dragging` is set
     * the frame is drawn unconditionally *and* the callback rate is pinned to
     * the display's refresh, so a lost release leaves the app redrawing
     * forever - a stuck 30-110% of a core that looks exactly like a runaway
     * loop, because it is one. Something else can take the release: focus
     * stolen mid-press, or a modal system dialog opening, which is a real path
     * now that File > Open exists.
     *
     * It has to be SDL_GetGlobalMouseState and not SDL_GetMouseState: the
     * latter answers from SDL's own cache, which still says the button is
     * down for exactly the reason the app is stuck - it never saw the release.
     * The global call asks the OS, so it turns an unrecoverable state into a
     * one-frame hiccup. */
    if (app->dragging &&
        !(SDL_GetGlobalMouseState(NULL, NULL) &
          (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK | SDL_BUTTON_RMASK |
           SDL_BUTTON_X1MASK | SDL_BUTTON_X2MASK))) {
        app->dragging      = 0;
        app->drag_in_field = 0;
        app->restore_rate  = 2;
        /* The countdown that hands the callback rate back only runs after a
         * drawn frame, and nothing else here has asked for one - so without
         * this the rate stayed pinned at the display's refresh, ticking empty
         * iterates until some unrelated event drew. Found by review, not by
         * measurement: an empty tick is cheap enough to hide. */
        app->dirty = 1;
    }

    /* While a button is held and the pointer is moving, every scheduled frame
     * is drawn whether or not an event arrived, so the cadence stops depending
     * on how the OS batched the mouse - that is what makes a text selection
     * track smoothly, and relying on motion delivery alone measured 33-45 fps
     * with the selection advancing unevenly.
     *
     * A *stationary* hold is a different case and used to cost the same: the
     * button down on blank page area, nothing moving, nothing changing, and
     * the app drew at the display's refresh for as long as the finger was
     * down - 167% of a core, 42% of this four-core machine. Nothing on screen
     * differed between those frames.
     *
     * So a hold falls back to the pointer-redraw gap only when the pointer is
     * over something that cannot change under it. Coalescing every stationary
     * hold was the first attempt and it was wrong: it slowed the repeater on
     * the Buttons page from about 60 ticks a second to 8, which is a visible
     * change to how the app behaves in exchange for CPU. */
    if (app->dragging) {
        /* hot_last_repaint is the question "does what is under the pointer
         * change when it is interacted with" - it is what the hover logic
         * already uses to decide whether a crossing is worth a frame. A
         * repeater, a slider, a scrollbar arrow all answer yes and keep the
         * full rate; blank page, a label, a heading answer no, and holding a
         * button over those cannot change anything until the pointer moves. */
        if (app->drag_moved || app->hot_last_repaint ||
            SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms)
            app->dirty = 1;
    }

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
     * happens - SDL only calls us when something arrived. */
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

    /* Anything a platform client asked for since the last frame - a press, a
     * move - applied here, where the tree is whole and this is the only
     * thread touching it. Before the build, because a press records an id the
     * widget takes while it draws. On the web it is nothing; the browser
     * calls in on this thread already. */
    curie_a11y_platform_drain();

    /* A key on the focused node, delivered as the click it stands for.
     * Nuklear's default button fires on the press when it lands inside the
     * widget, so a press here and a release on the next frame is a click to
     * everything that takes one - buttons, tabs, checkboxes, menu items, and
     * a field, which takes the caret. The pointer Nuklear sees stays there
     * until the next real motion; the one the OS shows never moves. */
    if (app->key_click) {
        int x = (int)app->key_click_x, y = (int)app->key_click_y;

        if (app->key_click == 1) {
            SDL_Event e;
            nk_input_motion(ctx, x, y);
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, nk_true);
            app->key_click = 2;
            SDL_zero(e);
            e.type = SDL_EVENT_USER;
            SDL_PushEvent(&e);
        } else {
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, nk_false);
            app->key_click = 0;
        }
        app->dirty = 1;
    }
    nk_input_end(ctx);
    ctx->style.text.color = app->text;

    /* Drained here rather than on the page that shows it, so a picker answered
     * while some other tab is on screen still clears - otherwise the next
     * File > Open would find one still pending and do nothing. */
    curie_file_taken(app, app->show.file_pick,
                     (int)sizeof(app->show.file_pick));

    /* Opened around the same region nk_begin gets, so the window node's bounds
     * are the window's. Closed after nk_end, below. */
    app->focus_seen = 0;
    curie_a11y_begin(&app->a11y, "Curie",
                     nk_rect(0, 0, (float)win_w, (float)win_h));

    if (nk_begin(ctx, "page", nk_rect(0, 0, (float)win_w, (float)win_h),
                 NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR)) {
        /* The zero padding above is only for this panel's geometry, which
         * nk_begin has now read. Left on the stack it also reaches nk_tooltip,
         * which sizes itself as text_width + 4 * padding.x - so every tooltip
         * came out as wide as its text and clipped it. */
        nk_style_pop_vec2(ctx);
        page_shell(app, ctx, win_w, win_h);
        if (app->focus_visible && app->focus_seen) focus_ring(app, ctx);
        /* Whether or not the range that asked for it was drawn: a step that
         * outlived its frame belongs to a page nobody is looking at. */
        app->focus_step = 0;
        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    }
    nk_end(ctx);

    /* Nobody took the activation, so it becomes the click it used to be.
     * Every widget that has been taught to listen has already acted and
     * cleared it; this is what the rest still get, and it is why teaching one
     * more widget is a line rather than a migration. */
    if (app->activate_id && app->focus_seen) {
        SDL_Event e;

        app->activate_id = 0;
        app->key_click   = 1;
        app->key_click_x = app->focus_rect.x + app->focus_rect.w * 0.5f;
        app->key_click_y = app->focus_rect.y + app->focus_rect.h * 0.5f;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
        app->dirty = 1;
    } else {
        app->activate_id = 0;
    }

    /* Diffs against the previous frame and swaps. The change list is what a
     * platform bridge will consume in phase 4; nothing reads it yet. */
    curie_a11y_end(&app->a11y);
    focus_resolve(app);
    curie_a11y_platform_push(&app->a11y, app->focus_id);
    a11y_dump_once(app);

    /* SDL3 delivers SDL_EVENT_TEXT_INPUT only while text input is started for
     * the window, and the backend starts it from whether Nuklear has an active
     * edit widget - which it knows only after the frame is built. Without this
     * the field took Backspace but never a character. */
    nk_sdl_update_TextInput(ctx);
    /* After it, because it is what starts text input; SDL_SetTextInputArea on
     * a window with input stopped is discarded. Cleared each frame, so a field
     * that stops being drawn stops steering the IME. */
    if (app->ime_valid) {
        SDL_SetTextInputArea(app->win, &app->ime_rect, app->ime_cursor);
        app->ime_valid = 0;
    }

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
        /* Nuklear's antialiasing is not edge coverage - it emits geometry a
         * fraction of a pixel wide whose alpha fades to zero and lets the
         * rasteriser blend it. SDL's software rasteriser has no partial
         * coverage, so that geometry lands whole, and the two kinds of it
         * land differently: a fill's feather becomes a half-tone column
         * between a panel's border and its fill and a rule between menu
         * items, while a stroke's feather is what grades a border's curve.
         * Off entirely, every rounded corner steps in twos.
         *
         * So the software renderer keeps stroke feathering and drops fill
         * feathering: measured across a popup's edge it then matches the
         * hardware profile pixel for pixel, and the button corners still
         * grade. CURIE_SW_NOAA=1 drops both, which is 6% cheaper. */
        {
            enum nk_anti_aliasing fill, line;
            fill = line = app->aa ? NK_ANTI_ALIASING_ON : NK_ANTI_ALIASING_OFF;
            if (app->renderer_is_sw) {
                fill = NK_ANTI_ALIASING_OFF;
                if (app->sw_noaa) line = NK_ANTI_ALIASING_OFF;
            }
            nk_sdl_render_ex(ctx, fill, line);
        }
        t_present0 = SDL_GetPerformanceCounter();
        SDL_RenderPresent(app->ren);
        t_end = SDL_GetPerformanceCounter();

        app->last_draw_ms    = SDL_GetTicks();
        if (app->first_frame_done) calibrate_hover_gap(app);
        app->build_ms_x100   = (int)(100000.0 * (double)(t_render0 - t_build0) / (double)f);
        app->render_ms_x100  = (int)(100000.0 * (double)(t_present0 - t_render0) / (double)f);
        app->present_ms_x100 = (int)(100000.0 * (double)(t_end - t_present0) / (double)f);
    }

    nk_input_begin(ctx);       /* collect again for the next frame */

    app->dirty = 0;
    app->drag_moved = 0;
    if (app->restore_rate > 0) {
        if (--app->restore_rate > 0) app->dirty = 1;
        else SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
    }
    if (app->want_quit) return SDL_APP_SUCCESS;

    /* After the first frame, not before: setting "waitevent" up front can
     * leave the window blank until the pointer happens to move over it. SDL
     * guarantees a free SDL_AppIterate after the hint only in a later revision
     * than the pinned release-3.4.16. */
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
