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

#define WINDOW_WIDTH  960      /* logical px; scaled via curie_px() */
#define WINDOW_HEIGHT 680
#define FONT_SIZE     16

/* Pure asks for font-size: 100% on buttons and inherits elsewhere, so 16 does
 * most of the work; the rest of the ladder is here for the headings and small
 * print the screen sets itself. Nearest wins, so a size in between costs no
 * fidelity worth another atlas. */
#define FONT_STEPS 6
static const int g_font_px[FONT_STEPS] = { 12, 13, 14, 16, 19, 23 };

#define IMG_CACHE_MAX 32
#define CARD_W        420

/* Height of the titlebar this app draws for itself when the native one is
 * turned off, and how wide a strip along each edge grabs for a resize. */
#define TITLEBAR_H    36
#define RESIZE_EDGE    6
#define CTL_SIZE      28   /* the control's square hit area */
/* On top of each of these Nuklear inserts its own 4px between columns, and
 * the group pads its contents again, so the gap on screen is about ten pixels
 * wider than the number here - which is why the mark sat further from the
 * edge than from the title while these read 6 and 2. */
#define TITLE_PAD      1   /* before the app mark */
#define TITLE_GAP      6   /* between the app mark and the name */

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

typedef struct {
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
    /* --text-muted as "#rrggbb", so button icons can be recoloured to match
     * the label instead of being pinned to one Open-Color shade. */
    char  icon_hex[10];

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
    } hot[12];
    int hot_n, hot_last;

    int   dirty;
    int   show_diag;
    int   laid_w, laid_h;
    float fps;
    int   fps_frames;
    Uint64 fps_t0;
    int   n_paints;
    int   style_ms_x100;   /* time the last stylesheet load took */
    /* Where a frame goes, in hundredths of a millisecond: building the UI,
     * converting and submitting it, and waiting on present. Split three ways
     * because guessing which one dominates has been wrong twice. */
    int   build_ms_x100, render_ms_x100, present_ms_x100;
} App;

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

    /* The desktop's own title bar, so a pinned scheme is not contradicted by
     * the frame around it. */
    curie_window_set_dark(
        SDL_GetPointerProperty(SDL_GetWindowProperties(app->win),
                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL),
        app->dark);

    app->clear   = app->page;
    app->dirty   = 1;
}

/* --- images -------------------------------------------------------------
 * An icon path may carry "?stroke=<family>-<shade>&fill=<family>-<shade>",
 * which recolours the SVG at load time. Kept from the previous design: it is
 * how an icon follows a palette, since CSS cannot reach inside an SVG.
 * "&sw=<k>" is the same idea for weight: it multiplies the stroke the artwork
 * declares, which is what a glyph drawn at titlebar size needs to stop
 * reading as a hairline. The query string is also the cache key, so two
 * weights of one glyph are simply two slots. */

/* "violet-7" names an Open-Color shade; "#rrggbb" is taken literally, which
 * is how an icon follows the stylesheet's own text colour. */
static int
parse_shade(const char *spec, char *family, size_t cap, int *idx)
{
    const char *dash;
    size_t n;

    if (spec[0] == '#') {
        if (strlen(spec) >= cap) return 0;
        strcpy(family, spec);
        *idx = 0;
        return 1;
    }
    dash = strrchr(spec, '-');
    if (!dash || !dash[1]) return 0;
    n = (size_t)(dash - spec);
    if (n == 0 || n >= cap) return 0;
    memcpy(family, spec, n);
    family[n] = 0;
    *idx = SDL_atoi(dash + 1);
    return 1;
}

/* SVG is resolution-independent, so an icon is rasterised at the size it is
 * actually drawn (times the display scale) rather than once and scaled down.
 * Slots are keyed by src *and* size. Returns NULL on error. */
static struct img_slot *
img_lookup(App *app, const char *src, int px)
{
    char rel[192], ofam[32] = {0}, ifam[32] = {0};
    int oidx = 0, iidx = 0;
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
                parse_shade(tok + 7, ofam, sizeof(ofam), &oidx);
            else if (SDL_strncmp(tok, "fill=", 5) == 0)
                parse_shade(tok + 5, ifam, sizeof(ifam), &iidx);
            else if (SDL_strncmp(tok, "sw=", 3) == 0)
                swk = (float)SDL_atof(tok + 3);
        }
    }

    surf = curie_svg_surface_path(rel, px,
                                  ofam[0] ? ofam : NULL, oidx,
                                  ifam[0] ? ifam : NULL, iidx, swk);

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

        cfg.oversample_h = 3;
        cfg.oversample_v = 2;
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

    svg = curie_svg_surface("browsers-outline", 64, "violet", 7, "indigo", 2);
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
hot_push(App *app, struct nk_rect r, int cursor, int repaint)
{
    int n = app->hot_n;

    if (n >= (int)(sizeof(app->hot) / sizeof(app->hot[0]))) return;
    app->hot[n].r       = r;
    app->hot[n].cursor  = (unsigned char)cursor;
    app->hot[n].repaint = (unsigned char)repaint;
    app->hot_n = n + 1;
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

static void
css_field(App *app, struct nk_context *ctx, char *buf, int *len, int cap,
          const char *hint)
{
    struct nk_rect bounds = nk_widget_bounds(ctx);
    curie_style s, foc;
    style_frame f = { 0, 0, 0, 0, 0 };
    unsigned char muted[4];

    app->field_rect = bounds;
    app->field_rect_valid = 1;

    /* The field's hover background is its resting colour, so hovering it
     * changes only the cursor - which needs no frame. */
    hot_push(app, bounds, 2, 0);

    curie_style_get("input", &s);
    curie_style_get("input:focus", &foc);

    if (s.matched) {
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
    }

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

    if (hint && *len == 0) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        const struct nk_user_font *font = ctx->style.font;
        struct nk_color grey = curie_style_token("--text-muted", muted)
                             ? col_of(muted) : nk_rgb(0x9a, 0x9a, 0x9a);
        float pad = (s.matched ? s.pad_x : 8.0f) + (s.matched ? s.border : 1.0f);
        struct nk_rect r = nk_rect(bounds.x + pad,
                                   bounds.y + (bounds.h - font->height) * 0.5f,
                                   bounds.w - pad * 2.0f, font->height + 2.0f);
        nk_draw_text(canvas, r, hint, (int)strlen(hint), font,
                     nk_rgba(0, 0, 0, 0), grey);
    }
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
        for (i = 0; i < app->ctl_n; i++) {
            struct nk_rect r = app->ctl[i];
            if (pt->x >= r.x && pt->x <= r.x + r.w &&
                pt->y >= r.y && pt->y <= r.y + r.h)
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
    nk_layout_row_push(ctx, (float)CTL_SIZE);
    nk_image(ctx, icon(app, "third_party/ionicons/src/svg/"
                            "browsers-outline.svg"
                            "?stroke=violet-7&fill=indigo-2", 18));

    nk_layout_row_push(ctx, (float)TITLE_GAP);
    nk_spacing(ctx, 1);

    /* Exactly the remainder, so the controls finish flush with the right
     * edge: the group's own padding either side, plus the five inter-column
     * gaps Nuklear inserts across six columns. A guessed constant here left a
     * visible strip of dead titlebar past the close button. */
    nk_layout_row_push(ctx, (float)(win_w - TITLE_PAD - CTL_SIZE - TITLE_GAP -
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
#define DIAG_ROWS   4

/* Nuklear's default group padding is 4px, which put the field and the buttons
 * hard against the card's edge. */
#define CARD_PAD_X  22
#define CARD_PAD_Y  18

static float
card_height(int with_diag)
{
    int rows = 9;                    /* brand..diagnostics link */
    float h = (float)(ROW_BRAND + ROW_FIELD + ROW_BUTTON +
                      ROW_SMALL + ROW_BUTTON + ROW_BUTTON + ROW_SMALL +
                      ROW_SMALL + ROW_SMALL);
    if (with_diag) {
        rows += DIAG_ROWS;
        h += DIAG_ROWS * ROW_SMALL;
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
    hot_push(app, nk_widget_bounds(ctx), 1, 0);
    if (nk_widget_is_hovered(ctx) &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT))
        clicked = 1;

    col = active && curie_style_token("--links", c)
        ? col_of(c)
        : (curie_style_token("--text-muted", c) ? col_of(c) : app->text);

    nk_style_push_color(ctx, &ctx->style.text.color, col);
    nk_label(ctx, label, NK_TEXT_CENTERED);
    nk_style_pop_color(ctx);
    return clicked;
}

static void
login_screen(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    /* Placed with nk_layout_space, which takes an explicit rect, rather than a
     * spacer row above a static row: the row APIs advance a cursor, and mixing
     * nk_spacing into a row built with nk_layout_row_push does not advance it
     * the way centring arithmetic assumes. An absolute rect has no such
     * question in it.
     *
     * Centred on the collapsed height, always. Centring on the current height
     * would re-centre the card every time diagnostics opens, so the whole
     * screen jumped on a click that should only have added rows underneath. */
    float card_h = card_height(app->show_diag);
    float chrome = app->borderless ? (float)TITLEBAR_H : 0.0f;
    float side = (win_w - CARD_W) * 0.5f;
    float top  = chrome + (win_h - chrome - card_height(0)) * 0.5f;

    if (side < 8.0f) side = 8.0f;
    if (top  < chrome + 8.0f) top = chrome + 8.0f;

    nk_layout_space_begin(ctx, NK_STATIC, (float)win_h, 2);

    if (app->borderless) {
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                 nk_style_item_color(app->card_bg));
        nk_layout_space_push(ctx, nk_rect(0, 0, (float)win_w, (float)TITLEBAR_H));
        titlebar(app, ctx, win_w);
        nk_style_pop_style_item(ctx);
    }

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
        nk_image(ctx, icon(app, "third_party/ionicons/src/svg/"
                                "person-circle-outline.svg"
                                "?stroke=violet-7&fill=indigo-2", ROW_BRAND));
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

        /* scheme switch: system follows SDL_GetSystemTheme(), the other two
         * pin it. Changing it reloads the stylesheets, which is the whole
         * mechanism - tiny.css ships light and dark as two palette files. */
        nk_style_push_font(ctx, pick_font(app, 14, 0));
        {
            /* Three links together, centred as a group. Splitting the card's
             * width into three equal columns spread them to the corners and
             * left the set very slightly off-centre against the row below it,
             * which is what made "diagnostics" look misaligned - it was
             * centred all along, just not on the same thing. */
            const float link_w = 64.0f;
            float inner = CARD_W - 2.0f * CARD_PAD_X;
            float side  = (inner - 3.0f * link_w) * 0.5f;
            int i;

            if (side < 0.0f) side = 0.0f;
            nk_layout_row_begin(ctx, NK_STATIC, ROW_SMALL, 4);
            nk_layout_row_push(ctx, side);
            nk_spacing(ctx, 1);
            for (i = 0; i < 3; i++) {
                nk_layout_row_push(ctx, link_w);
                if (text_link(app, ctx, g_theme_names[i], i == app->theme_mode) &&
                    i != app->theme_mode) {
                    app->theme_mode = i;
                    load_theme(app);
                }
            }
            nk_layout_row_end(ctx);
        }

        nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
        if (text_link(app, ctx, app->borderless ? "native titlebar"
                                                : "custom titlebar", 0)) {
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

        nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
        if (text_link(app, ctx,
                      app->show_diag ? "hide diagnostics" : "diagnostics", 0))
            app->show_diag = !app->show_diag;

        if (app->show_diag) {
            char line[160];

            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            SDL_snprintf(line, sizeof(line),
                         "%s (%s)   vsync %s   aa %s   rate %s / %s",
                         SDL_GetRendererName(app->ren), app->render_mode,
                         app->vsync_on ? "on" : "off", app->aa ? "on" : "off",
                         app->frame_rate, app->drag_rate);
            nk_label(ctx, line, NK_TEXT_CENTERED);

            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            SDL_snprintf(line, sizeof(line), "scale %.2fx      %s",
                         curie_scale(), app->font_status);
            nk_label(ctx, line, NK_TEXT_CENTERED);

            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            SDL_snprintf(line, sizeof(line),
                         "%s theme, %d sheets in %.2f ms      %.0f fps",
                         app->dark ? "dark" : "light", SHEET_COUNT,
                         app->style_ms_x100 / 100.0f, app->fps);
            nk_label(ctx, line, NK_TEXT_CENTERED);

            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            SDL_snprintf(line, sizeof(line),
                         "build %.2f   render %.2f   present %.2f ms",
                         app->build_ms_x100 / 100.0f,
                         app->render_ms_x100 / 100.0f,
                         app->present_ms_x100 / 100.0f);
            nk_label(ctx, line, NK_TEXT_CENTERED);
        }
        nk_style_pop_font(ctx);

        nk_style_pop_vec2(ctx);
        nk_group_end(ctx);
    }
    nk_style_pop_vec2(ctx);            /* group_padding */
    nk_style_pop_style_item(ctx);
    nk_layout_space_end(ctx);
}

/* --- SDL application callbacks ----------------------------------------- */

SDL_AppResult
SDL_AppInit(void **appstate, int argc, char *argv[])
{
    App *app;

    /* Headless check of the icon pipeline: curie --dump-icon <out.png> */
    if (argc >= 3 && strcmp(argv[1], "--dump-icon") == 0) {
        int ok = curie_svg_icon_dump("browsers-outline", 256,
                                     "violet", 7, "indigo", 2, argv[2]);
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
    app->borderless = env_int("CURIE_BORDERLESS", 1) != 0;

    if (!SDL_CreateWindowAndRenderer("Curie", WINDOW_WIDTH, WINDOW_HEIGHT,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
            (app->borderless ? SDL_WINDOW_BORDERLESS : 0),
            &app->win, &app->ren)) {
        SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

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

    /* Follow the desktop until told otherwise. load_theme() reads
     * SDL_GetSystemTheme() through curie_prefers_dark() and picks the
     * matching tiny.css palette file. */
    nk_textedit_init_fixed(&app->edit, app->edit_buf, sizeof(app->edit_buf));

    app->theme_mode = THEME_SYSTEM;
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

            for (i = 0; i < app->hot_n; i++) {
                struct nk_rect r = app->hot[i].r;
                if (mx >= r.x && mx <= r.x + r.w &&
                    my >= r.y && my <= r.y + r.h) { over = i; break; }
            }
            if (over != app->hot_last) {
                int was = app->hot_last >= 0 && app->hot[app->hot_last].repaint;
                int is  = over >= 0 && app->hot[over].repaint;

                app->hot_last = over;
                app->want_cursor = over >= 0 ? app->hot[over].cursor : 0;
                if (was || is) app->dirty = 1;
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
            SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
            SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
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
            if (event->key.key == SDLK_F1) app->show_diag = !app->show_diag;
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
        login_screen(app, ctx, win_w, win_h);
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
    if (app->want_quit) return SDL_APP_SUCCESS;

    /* Applied after the first frame, not before it. SDL only guarantees a
     * free SDL_AppIterate immediately after the hint is set from 3.6.0; this
     * builds against 3.5.0, where setting "waitevent" up front can leave the
     * window blank until the user happens to move the mouse over it. */
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
