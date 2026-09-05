/* main.c - Curie: pure C UI on Nuklear, rendered through SDL3.
 *
 * The loop is SDL3's *callback* model (SDL_AppInit / SDL_AppIterate /
 * SDL_AppEvent / SDL_AppQuit) rather than a while() loop. That is not a style
 * choice: a browser tab cannot be blocked, so under Emscripten SDL must hand
 * control back each frame. The same source is an ordinary loop natively, so
 * one file serves Windows, macOS, Linux, the BSDs and WASM. */
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>

#include "nk_common.h"
#include "../third_party/nuklear/demo/sdl3_renderer/nuklear_sdl3_renderer.h"
#include "appicon.h"
#include "theme.h"
#include "metrics.h"
#include "curie.h"
#include "container_nk.h"

#define WINDOW_WIDTH  960      /* logical px; scaled via curie_px() */
#define WINDOW_HEIGHT 680
#define FONT_SIZE     16

#define CLIP_MAX 32
#define DOC_CACHE_MAX 8
#define IMG_CACHE_MAX 32
#define ICON_RASTER_PX 128   /* icons are rasterised once, then scaled down */

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

    /* litehtml document + the canvas it paints into for the current frame */
    curie_doc               *doc;      /* the active document */
    curie_painter            painter;  /* kept so documents can be built later */
    curie_doc               *scratch;  /* for states that are never cached */

    /* Rebuilding costs 45-65 ms, because litehtml accepts stylesheets only as
     * strings and re-parses ~83 KB of pico every time. Documents are therefore
     * built once per UI state and swapped by pointer afterwards. */
    struct {
        char       key[24];
        curie_doc *doc;
    } doc_cache[DOC_CACHE_MAX];
    int doc_cache_n;
    struct nk_command_buffer *canvas;
    struct nk_rect           clip[CLIP_MAX];
    int                      clip_depth;
    struct nk_rect           content;   /* widget region, device px */
    char                     font_status[160];

    /* Decoded <img> sources, keyed by their src string. Small and linear:
     * a screen references a handful of icons, not hundreds. */
    struct img_slot img[IMG_CACHE_MAX];
    int img_count;

    /* The page template, read once; the diagnostics block is substituted in
     * before each load so the hint can show live values. */
    char *page_template;
    int   show_diag;
    int   page_dirty;
    int   mouse_was_down;

    /* 0 = follow the system, 1 = force light, 2 = force dark. */
    int   theme_mode;

    /* Layout is only redone when the viewport changes or the document does;
     * running it every frame was most of the idle CPU. */
    int   laid_w, laid_h;
    int   relayout;

    int    redraw_always;    /* CURIE_REDRAW=always */
    float  last_rebuild_ms;
    float  last_layout_ms;
    int    aa;               /* CURIE_AA: fringe geometry for smooth edges */
    int    hover_interval;   /* ms between hover hit-tests */
    Uint64 hover_t0;
    char   render_mode[16];

    /* Instrumentation: guessing at where frame time goes is how the last
     * "optimisation" made things worse. */
    int    vsync_on;
    Uint64 fps_t0;
    int    fps_frames;
    float  fps;
    /* Set by anything that can change what is on screen. When it is clear
     * the frame is skipped entirely: no document walk, no tessellation, no
     * present. A static page then costs almost nothing. */
    int    dirty;

    int    n_paints;       /* frames actually drawn */
    int    n_layouts;      /* litehtml render() calls */
    int    n_rebuilds;     /* full document re-parses */

    /* Text editing. The buffer and cursor live in Nuklear's stb_textedit
     * state machine, which already implements insertion, deletion and
     * selection correctly - see FINDINGS 15.3, option A. The document is not
     * rebuilt per keystroke; the text is painted over the field's box. */
    struct nk_text_edit edit;
    char                edit_buf[256];
    int                 focused;
    struct nk_rect      field;      /* last known box, document coords */
    int                 field_ok;
    int                 field_dirty; /* re-query the box after a layout */
    float               last_mx, last_my;

    /* System cursors, created once. litehtml resolves the CSS `cursor`
     * property for whatever is under the pointer; this turns that into the
     * real pointer shape, which is what makes buttons and links feel live. */
    SDL_Cursor *cur_default;
    SDL_Cursor *cur_pointer;
    SDL_Cursor *cur_text;
    SDL_Cursor *cur_active;
} App;

#define FIELD_SELECTOR "#emailvalue"

/* Runtime knobs, in the spirit of CURIE_SCALE. Read once at startup.
 *   CURIE_RENDERER = auto | gpu | cpu     (cpu forces SDL's software driver)
 *   CURIE_VSYNC    = 0 | 1
 *   CURIE_REDRAW   = ondemand | always    (always = redraw every frame)
 *   CURIE_HOVER_HZ = <n>                  (hover hit-test rate cap) */
static int env_int(const char *name, int fallback)
{
    const char *v = SDL_getenv(name);
    return (v && *v) ? SDL_atoi(v) : fallback;
}

#define THEME_SYSTEM 0
#define THEME_LIGHT  1
#define THEME_DARK   2

static const char *const g_sheets[] = {
    "third_party/open-color/open-color.css",
"third_party/pico/css/pico.min.css",
"assets/app.css",
};

/* Replaces the first occurrence of `token` with `value`, whatever their
 * lengths. Doing this with a fixed-width memcpy is how an embedded NUL ends
 * up truncating the document. */
static void
replace_token(char *buf, size_t cap, const char *token, const char *value)
{
    char *at = strstr(buf, token);
    size_t tlen, vlen, tail;

    if (!at) return;
    tlen = strlen(token);
    vlen = strlen(value);
    tail = strlen(at + tlen);
    if ((size_t)(at - buf) + vlen + tail + 1 > cap) return;

    memmove(at + vlen, at + tlen, tail + 1);   /* move the tail, NUL included */
    memcpy(at, value, vlen);                   /* then write the value */
}

/* Resolves the effective scheme. The system answer comes from SDL, which is
 * the only portable source - litehtml cannot evaluate prefers-color-scheme. */
static void
resolve_theme(App *app)
{
    if (app->theme_mode == THEME_LIGHT)     app->dark = 0;
    else if (app->theme_mode == THEME_DARK) app->dark = 1;
    else                                    app->dark = curie_prefers_dark() != 0;
}

/* Rebuilds the page from the template by substituting three tokens.
 * litehtml has no scripting, so "interactive" means the application rebuilds
 * the document - cheap here, since the page is a few kilobytes. */
static void
rebuild_page(App *app)
{
    static const char *const names[3] = { "system", "light", "dark" };
    char diag[900];
    char sw[420];
    char *page;
    size_t cap;
    int i, off = 0;

    char key[24];
    curie_doc *target;
    int cacheable, ci;

    if (!app->page_template) return;

    /* The diagnostics panel carries live counters, so that state is never
     * cached; every other rendering is stable for a given theme and selector
     * position. */
    cacheable = !app->show_diag;
    SDL_snprintf(key, sizeof(key), "%d-%d",
                 app->dark ? 1 : 0, app->theme_mode);

    if (cacheable) {
        for (ci = 0; ci < app->doc_cache_n; ci++) {
            if (SDL_strcmp(app->doc_cache[ci].key, key) == 0) {
                app->doc         = app->doc_cache[ci].doc;   /* instant */
                app->relayout    = 1;
                app->field_dirty = 1;
                app->page_dirty  = 0;
                return;
            }
        }
    }

    /* --- diagnostics: a hint link, or the expanded panel --- */
    if (!app->show_diag) {
        SDL_snprintf(diag, sizeof(diag),
            "<a class=\"hint\" href=\"#diag\">diagnostics</a>");
    } else {
        SDL_snprintf(diag, sizeof(diag),
            "<div class=\"diag\">"
"<div class=\"row\"><b>renderer</b><span>%s</span></div>"
"<div class=\"row\"><b>theme</b><span>%s (%s)</span></div>"
"<div class=\"row\"><b>scale</b><span>%.2fx</span></div>"
"<div class=\"row\"><b>font</b><span>%s</span></div>"
"<div class=\"row\"><b>page</b><span>%d px</span></div>"
"<div class=\"row\"><b>fps</b><span>%.0f  vsync %s</span></div>"
"<div class=\"row\"><b>work</b><span>%d layouts, %d rebuilds</span></div>"
"<a class=\"hint\" href=\"#diag\">hide</a>"
"</div>",
            SDL_GetRendererName(app->ren),
            app->dark ? "dark" : "light", names[app->theme_mode],
            curie_scale(), app->font_status,
            curie_doc_height(app->doc),
            app->fps, app->vsync_on ? "on" : "OFF",
            app->n_layouts, app->n_rebuilds);
    }

    /* --- scheme selector: three links, active one marked --- */
    off += SDL_snprintf(sw + off, sizeof(sw) - off, "<div class=\"themesw\">");
    for (i = 0; i < 3; i++)
        off += SDL_snprintf(sw + off, sizeof(sw) - off,
                 "<a href=\"#theme=%s\">%s</a>", names[i], names[i]);
    SDL_snprintf(sw + off, sizeof(sw) - off, "</div>");

    cap = strlen(app->page_template) + sizeof(diag) + sizeof(sw) + 32;
    page = (char *)SDL_malloc(cap);
    if (!page) return;
    SDL_strlcpy(page, app->page_template, cap);

    replace_token(page, cap, "<!--DIAG-->", diag);
    replace_token(page, cap, "<!--THEMESW-->", sw);
    /* The system preference reaches CSS as pico's data-theme attribute,
     * since prefers-color-scheme is unavailable. */
    replace_token(page, cap, "__THEME__", app->dark ? "dark" : "light");
    replace_token(page, cap, "__MODE__",
                  app->theme_mode == THEME_LIGHT ? "light"
                  : app->theme_mode == THEME_DARK ? "dark" : "system");
    /* CSS cannot reach inside an <img>, so the icon shade is substituted here.
     * gray-7 vanished against the dark hover background. */
    replace_token(page, cap, "__ICON__", app->dark ? "gray-2" : "gray-7");
    replace_token(page, cap, "__ICON__", app->dark ? "gray-2" : "gray-7");
    replace_token(page, cap, "__ICON__", app->dark ? "gray-2" : "gray-7");

    /* A cached entry must not be overwritten, so a cacheable state gets its
     * own document; the uncacheable one reuses a single scratch document. */
    if (cacheable) {
        target = curie_doc_create(&app->painter);
    } else {
        if (!app->scratch) app->scratch = curie_doc_create(&app->painter);
        target = app->scratch;
    }
    if (!target) target = app->doc;
    if (!target) { SDL_free(page); return; }
    app->doc = target;

    {
        /* How long a click that toggles state actually costs: the document is
         * rebuilt, which re-parses every stylesheet because litehtml only
         * accepts CSS as strings. */
        Uint64 t0 = SDL_GetPerformanceCounter();
        int ok = curie_doc_load_html(app->doc, page, g_sheets, 3);
        Uint64 t1 = SDL_GetPerformanceCounter();
        app->last_rebuild_ms = 1000.0f * (float)(t1 - t0) /
                               (float)SDL_GetPerformanceFrequency();
        if (!ok)
            SDL_Log("page rebuild failed: %s", curie_doc_last_error(app->doc));
        {
            FILE *lf = fopen("build/stats.log", "a");
            if (lf) {
                char ln[96];
                SDL_snprintf(ln, sizeof(ln), "rebuild=%.1f ms",
                             app->last_rebuild_ms);
                fputs(ln, lf); fputc(10, lf); fclose(lf);
            }
        }
    }
    SDL_free(page);

    if (cacheable && app->doc_cache_n < DOC_CACHE_MAX) {
        SDL_strlcpy(app->doc_cache[app->doc_cache_n].key, key,
                    sizeof(app->doc_cache[0].key));
        app->doc_cache[app->doc_cache_n].doc = app->doc;
        app->doc_cache_n++;
    }

    app->n_rebuilds++;
    app->relayout    = 1;
    app->field_dirty = 1;
    app->page_dirty  = 0;
}

/* Defined below, with the text-field helpers. */
static void        apply_theme(App *app);
static const char *edit_text(App *app, int *len);
static float       edit_width(App *app, int n);
static void        set_focus(App *app, int on);

/* Press and release are handled the moment SDL delivers them, not polled
 * once per frame. Polling cost a frame for the press, a frame for the
 * release, and a third for the rebuild - with an idle sleep before each,
 * which is what made clicks feel laggy. */
static void
handle_button(App *app, float wx, float wy, int down)
{
    int dx, dy;

    if (!app->doc) return;

    /* The page fills the window, so the content origin is the frame's region
     * origin recorded during the last paint. */
    dx = (int)(wx - app->content.x);
    dy = (int)(wy - app->content.y);

    if (down) {
        curie_doc_mouse_down(app->doc, dx, dy);

        if (app->field_ok) {
            int inside = wx >= app->field.x && wx <= app->field.x + app->field.w &&
                         wy >= app->field.y - 8 && wy <= app->field.y + app->field.h + 8;
            set_focus(app, inside);
            if (inside) {
                int i, len, best = 0;
                float want = wx - app->field.x, bd = 1e9f;
                edit_text(app, &len);
                for (i = 0; i <= len; i++) {
                    float d = edit_width(app, i) - want;
                    if (d < 0) d = -d;
                    if (d < bd) { bd = d; best = i; }
                }
                app->edit.cursor = best;
                app->edit.select_start = app->edit.select_end = best;
            }
        }
    } else {
        curie_doc_mouse_up(app->doc, dx, dy);   /* fires the anchor click */
    }
    app->dirty = 1;
}

/* A link in the document reaching back into the application. */
static void
pf_anchor_click(void *u, const char *url)
{
    App *app = (App *)u;
    if (!url) return;

    if (strcmp(url, "#diag") == 0) {
        app->show_diag = !app->show_diag;
        app->page_dirty = 1;   /* rebuilt after the frame, not mid-paint */
    } else if (strncmp(url, "#theme=", 7) == 0) {
        const char *m = url + 7;
        app->theme_mode = strcmp(m, "light") == 0 ? THEME_LIGHT
                        : strcmp(m, "dark")  == 0 ? THEME_DARK
                        : THEME_SYSTEM;
        resolve_theme(app);

        /* Both the scheme and the selector highlight are attribute-driven, so
         * this is a restyle rather than a rebuild: no stylesheet is re-parsed
         * and the switch is immediate. */
        Uint64 rt0 = SDL_GetPerformanceCounter();
        if (curie_doc_set_root_attr(app->doc, "data-theme",
                                    app->dark ? "dark" : "light") &&
            curie_doc_set_root_attr(app->doc, "data-mode", m)) {
            Uint64 rt1 = SDL_GetPerformanceCounter();
            FILE *lf = fopen("build/stats.log", "a");
            if (lf) {
                char ln[96];
                SDL_snprintf(ln, sizeof(ln), "restyle=%.1f ms",
                             1000.0 * (double)(rt1 - rt0) /
                             (double)SDL_GetPerformanceFrequency());
                fputs(ln, lf); fputc(10, lf); fclose(lf);
            }
            apply_theme(app);          /* Nuklear clear colour follows too */
            app->relayout = 1;
            app->field_dirty = 1;
            app->dirty = 1;
        } else {
            app->page_dirty = 1;       /* fall back to a full rebuild */
        }
    }
}

/* --- curie_painter: the only place Nuklear and litehtml meet -------------
 * The C++ side never sees an nk_* type; it calls through these. Swapping to
 * SDL_Renderer later means rewriting only this block (FINDINGS §16). */

static void
pf_fill_rect(void *u, float x, float y, float w, float h, float rounding,
             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    App *app = (App *)u;
    if (!app->canvas) return;
    nk_fill_rect(app->canvas, nk_rect(x, y, w, h), rounding, nk_rgba(r, g, b, a));
}

static void
pf_stroke_rect(void *u, float x, float y, float w, float h, float rounding,
               float thickness,
               unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    App *app = (App *)u;
    if (!app->canvas) return;
    nk_stroke_rect(app->canvas, nk_rect(x, y, w, h), rounding, thickness,
                   nk_rgba(r, g, b, a));
}

/* Nuklear interpolates between four corner colours, so a two-stop gradient is
 * expressed by repeating each stop along one axis. */
static void
pf_fill_gradient(void *u, float x, float y, float w, float h,
                 const unsigned char *from, const unsigned char *to,
                 int vertical)
{
    App *app = (App *)u;
    struct nk_color a, b;
    if (!app->canvas) return;
    a = nk_rgba(from[0], from[1], from[2], from[3]);
    b = nk_rgba(to[0],   to[1],   to[2],   to[3]);
    if (vertical)
        nk_fill_rect_multi_color(app->canvas, nk_rect(x, y, w, h), a, a, b, b);
    else
        nk_fill_rect_multi_color(app->canvas, nk_rect(x, y, w, h), a, b, b, a);
}

/* --- images -------------------------------------------------------------
 * An <img src> may carry "?stroke=<family>-<shade>&fill=<family>-<shade>",
 * which recolours the SVG from Open-Color at load time. CSS cannot reach
 * inside an <img>, so this is how icons follow the palette. */

static int
parse_shade(const char *spec, char *family, size_t cap, int *idx)
{
    const char *dash = strrchr(spec, '-');
    size_t n;
    if (!dash || !dash[1]) return 0;
    n = (size_t)(dash - spec);
    if (n == 0 || n >= cap) return 0;
    memcpy(family, spec, n);
    family[n] = 0;
    *idx = SDL_atoi(dash + 1);
    return 1;
}

/* Returns the cache slot for `src`, decoding it on first use. NULL on error. */
/* SVG is resolution-independent, so an icon is rasterised at the size it is
 * actually drawn (times the display scale) rather than once at a fixed size
 * and scaled down. Slots are keyed by src *and* size. */
static struct img_slot *
img_lookup(App *app, const char *src, int px)
{
    char rel[192], ofam[32] = {0}, ifam[32] = {0};
    int oidx = 0, iidx = 0;
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

    /* Split "path?stroke=a-1&fill=b-2" into path plus colour requests. */
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
        }
    }

    surf = curie_svg_surface_path(rel, px,
                                  ofam[0] ? ofam : NULL, oidx,
                                  ifam[0] ? ifam : NULL, iidx);

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

static int
pf_image_size(void *u, const char *src, int *w, int *h)
{
    App *app = (App *)u;
    /* Only existence matters here; the CSS decides the drawn size. */
    struct img_slot *slot = img_lookup(app, src, 32);
    if (!slot) return 0;
    /* Reported at CSS px, not raster px: the icon is drawn at whatever size
     * the stylesheet asks for, and 128 is only the rasterisation detail. */
    *w = 24;
    *h = 24;
    return 1;
}

static void
pf_image_draw(void *u, const char *src, float x, float y, float w, float h)
{
    App *app = (App *)u;
    struct nk_image img;
    struct img_slot *slot;
    int px = (int)((w > h ? w : h) * curie_scale() + 0.5f);
    slot = img_lookup(app, src, px);
    if (!slot || !app->canvas) return;
    img = nk_image_ptr(slot->tex);
    nk_draw_image(app->canvas, nk_rect(x, y, w, h), &img, nk_rgb(255, 255, 255));
}

static void
pf_draw_text(void *u, float x, float y, float w, float h,
             const char *text, int len,
             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    App *app = (App *)u;
    const struct nk_user_font *font;
    if (!app->canvas) return;
    font = app->ctx->style.font;
    nk_draw_text(app->canvas, nk_rect(x, y, w, h), text, len, font,
                 nk_rgba(0, 0, 0, 0), nk_rgba(r, g, b, a));
}

static void
pf_push_clip(void *u, float x, float y, float w, float h)
{
    App *app = (App *)u;
    if (!app->canvas || app->clip_depth >= CLIP_MAX) return;
    app->clip[app->clip_depth++] = nk_rect(x, y, w, h);
    nk_push_scissor(app->canvas, nk_rect(x, y, w, h));
}

static void
pf_pop_clip(void *u)
{
    App *app = (App *)u;
    if (!app->canvas || app->clip_depth <= 0) return;
    app->clip_depth--;
    /* Restore the enclosing clip, or the widget region at depth zero. */
    nk_push_scissor(app->canvas, app->clip_depth > 0
                    ? app->clip[app->clip_depth - 1] : app->content);
}

/* Measurement and drawing share one font, so wrapping and painting agree. */
static float
pf_text_width(void *u, const char *text, int len)
{
    App *app = (App *)u;
    const struct nk_user_font *font = app->ctx->style.font;
    if (!font || len <= 0) return 0.0f;
    return font->width(font->userdata, font->height, text, len);
}

static float
pf_font_height(void *u)
{
    App *app = (App *)u;
    return app->ctx->style.font ? app->ctx->style.font->height : 16.0f;
}

/* --- text field ---------------------------------------------------------
 * litehtml renders no value inside a form control, so the field is an empty
 * sized <span> and the application paints into its box. */

static const char *
edit_text(App *app, int *len)
{
    *len = app->edit.string.len;
    return (const char *)nk_str_get_const(&app->edit.string);
}

/* Width of the first `n` bytes, using the same font the page is drawn with -
 * one measurement path for caret placement and painting alike. */
static float
edit_width(App *app, int n)
{
    const struct nk_user_font *f = app->ctx->style.font;
    int len;
    const char *t = edit_text(app, &len);
    if (!f || n <= 0) return 0.0f;
    if (n > len) n = len;
    return f->width(f->userdata, f->height, t, n);
}

/* Applies the cursor for whatever the pointer is over. Called every frame,
 * but SDL_SetCursor only fires when the shape actually changes. */
static void
apply_cursor(App *app, int over_field)
{
    const char *css = app->doc ? curie_doc_cursor(app->doc) : "";
    SDL_Cursor *want = app->cur_default;

    if (over_field)                             want = app->cur_text;
    else if (SDL_strcmp(css, "pointer") == 0)   want = app->cur_pointer;
    else if (SDL_strcmp(css, "text") == 0)      want = app->cur_text;

    if (want && want != app->cur_active) {
        SDL_SetCursor(want);
        app->cur_active = want;
    }
}

static void
set_focus(App *app, int on)
{
    if (app->focused == on) return;
    app->focused = on;
    if (on) SDL_StartTextInput(app->win);
    else    SDL_StopTextInput(app->win);
}

/* Backspace/Delete/arrows/Home/End over the stb_textedit state. The key
 * handler inside Nuklear is NK_LIB (file-local), so the public buffer calls
 * are used directly. */
static void
edit_key(App *app, SDL_Keycode key, SDL_Keymod mod)
{
    struct nk_text_edit *e = &app->edit;
    int len = e->string.len;

    if (e->select_start != e->select_end &&
        (key == SDLK_BACKSPACE || key == SDLK_DELETE)) {
        nk_textedit_delete_selection(e);
        return;
    }

    switch (key) {
    case SDLK_BACKSPACE:
        if (e->cursor > 0) {
            nk_textedit_delete(e, e->cursor - 1, 1);
            e->cursor--;
        }
        break;
    case SDLK_DELETE:
        if (e->cursor < len) nk_textedit_delete(e, e->cursor, 1);
        break;
    case SDLK_LEFT:  if (e->cursor > 0)   e->cursor--; break;
    case SDLK_RIGHT: if (e->cursor < len) e->cursor++; break;
    case SDLK_HOME:  e->cursor = 0;   break;
    case SDLK_END:   e->cursor = len; break;
    case SDLK_A:
        if (mod & SDL_KMOD_CTRL) nk_textedit_select_all(e);
        break;
    case SDLK_V:
        if (mod & SDL_KMOD_CTRL && SDL_HasClipboardText()) {
            char *clip = SDL_GetClipboardText();
            if (clip) {
                nk_textedit_text(e, clip, (int)SDL_strlen(clip));
                SDL_free(clip);
            }
        }
        break;
    case SDLK_C:
        if (mod & SDL_KMOD_CTRL) {
            int n;
            const char *t = edit_text(app, &n);
            if (n > 0) {
                char *tmp = (char *)SDL_malloc((size_t)n + 1);
                if (tmp) { SDL_memcpy(tmp, t, n); tmp[n] = 0;
                           SDL_SetClipboardText(tmp); SDL_free(tmp); }
            }
        }
        break;
    default: break;
    }
    e->select_start = e->select_end = e->cursor;
}

/* --- window icon ------------------------------------------------------- */
/* SDL_SetWindowIcon is portable, so this replaces the Win32 HICON path
 * outright. The .ico for the Windows executable resource is still produced
 * at build time by tools/mkicon.c, from this same code path. */
static void
set_window_icon(SDL_Window *win)
{
    plutovg_surface_t *svg;
    SDL_Surface *icon;
    int w, h, stride;

    svg = curie_svg_surface("browsers-outline", 64, "violet", 9, "indigo", 3);
    if (!svg) return;

    w      = plutovg_surface_get_width(svg);
    h      = plutovg_surface_get_height(svg);
    stride = plutovg_surface_get_stride(svg);
    curie_unpremultiply(plutovg_surface_get_data(svg), w, h, stride);

    /* plutovg's ARGB32 is B,G,R,A in memory on little-endian, which is what
     * SDL calls ARGB8888. */
    icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888,
                                 plutovg_surface_get_data(svg), stride);
    if (icon) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }
    plutovg_surface_destroy(svg);
}

/* --- theme ------------------------------------------------------------- */
/* Content theming stays deliberately thin; once litehtml renders pico.css
 * this becomes the stylesheet's job via prefers-color-scheme. */
static void
apply_theme(App *app)
{
    struct nk_context *ctx = app->ctx;
    struct nk_color fg, panel, accent;

    resolve_theme(app);

    if (app->dark) {
        app->clear = nk_rgb(30, 30, 30);
        panel  = nk_rgb(45, 45, 48);
        fg     = nk_rgb(225, 225, 230);
        accent = nk_rgb(90, 90, 100);
    } else {
        app->clear = nk_rgb(243, 244, 246);
        panel  = nk_rgb(255, 255, 255);
        fg     = nk_rgb(28, 30, 34);
        accent = nk_rgb(200, 202, 208);
    }

    ctx->style.window.fixed_background    = nk_style_item_color(panel);
    ctx->style.window.background          = panel;
    ctx->style.window.border_color        = accent;
    ctx->style.window.header.normal       = nk_style_item_color(accent);
    ctx->style.window.header.hover        = nk_style_item_color(accent);
    ctx->style.window.header.active       = nk_style_item_color(accent);
    ctx->style.window.header.label_normal = fg;
    ctx->style.window.header.label_hover  = fg;
    ctx->style.window.header.label_active = fg;
    ctx->style.text.color                 = fg;
    ctx->style.button.normal              = nk_style_item_color(accent);
    ctx->style.button.text_normal         = fg;
    ctx->style.button.text_hover          = fg;
    ctx->style.button.text_active         = fg;
}

/* Rebuilds the font atlas at the current scale. Layout stays in logical px
 * and never learns that this happened.
 *
 * Nuklear's built-in default is ProggyClean, a 13px *bitmap* font: blocky at
 * any real size and unable to scale. Baking a TTF through stb_truetype is
 * what makes text look like text - and this is exactly the path the CC0
 * Zerove font will use once vendored (PLAN M3); only FONT_FILE changes. */
/* Zerove is CC0, so it carries no attribution or notice obligation at all -
 * strictly more permissive than the project's MIT policy (FINDINGS §8.1).
 * itch.io is not a git host, so this is the one *vendored* asset rather than
 * a submodule (FINDINGS §8.3); its licence travels beside it. */
/* Zerove is UNICASE - measured: 'a' and 'A' are both 1434 units tall with
 * identical outlines, so every lowercase glyph draws as a capital. Fine for a
 * wordmark, unreadable as UI body text. It stays vendored in assets/fonts/ for
 * display use once per-element font selection exists (PLAN M3); until then a
 * single font serves the whole UI, so it has to be a text face. */
#define FONT_FILE "third_party/nuklear/extra_font/Karla-Regular.ttf"

static void
rebuild_font(App *app)
{
    struct nk_font_atlas *atlas;
    struct nk_font *font = NULL;
    char path[1024];

    atlas = nk_sdl_font_stash_begin(app->ctx);

    /* Baked at device size so glyphs stay crisp on HiDPI, then reported to
     * Nuklear at that same size - layout above still thinks in logical px. */
    if (curie_path(path, sizeof(path), FONT_FILE)) {
        struct nk_font_config cfg = nk_font_config(0);
        cfg.oversample_h = 3;
        cfg.oversample_v = 2;
        cfg.pixel_snap   = 0;
        font = nk_font_atlas_add_from_file(atlas, path,
                                           (float)curie_px(FONT_SIZE), &cfg);
        if (!font)
            SDL_snprintf(app->font_status, sizeof(app->font_status),
                         "FALLBACK (ProggyClean) - could not load %s", path);
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "FALLBACK - could not resolve %s", FONT_FILE);
    }

    /* A silent fallback here would be indistinguishable from "the font never
     * loaded", so the outcome is always recorded and shown in the UI. */
    if (!font) {
        font = nk_font_atlas_add_default(atlas, (float)curie_px(FONT_SIZE), NULL);
    } else {
        SDL_snprintf(app->font_status, sizeof(app->font_status),
                     "Karla @ %dpx", curie_px(FONT_SIZE));
    }

    nk_sdl_font_stash_end(app->ctx);
    if (font) nk_style_set_font(app->ctx, &font->handle);
}

/* --- SDL application callbacks ----------------------------------------- */

SDL_AppResult
SDL_AppInit(void **appstate, int argc, char *argv[])
{
    App *app;

    /* Headless check of the icon pipeline: curie --dump-icon <out.png> */
    if (argc >= 3 && strcmp(argv[1], "--dump-icon") == 0) {
        int ok = curie_svg_icon_dump("browsers-outline", 256,
"violet", 9, "indigo", 3, argv[2]);
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
        /* "cpu" forces SDL's software rasteriser; "gpu" pins a hardware
         * driver; "auto" lets SDL choose and fall back on its own. */
        const char *mode = SDL_getenv("CURIE_RENDERER");
        if (!mode || !*mode) mode = "auto";
        SDL_strlcpy(app->render_mode, mode, sizeof(app->render_mode));
        if (SDL_strcmp(mode, "cpu") == 0)
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }

    if (!SDL_CreateWindowAndRenderer("Curie", WINDOW_WIDTH, WINDOW_HEIGHT,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &app->win, &app->ren)) {
        SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Without this the loop presents as fast as the GPU allows, which was
     * the other half of the idle CPU. */
    {
        int want = env_int("CURIE_VSYNC", 1);
        app->vsync_on = SDL_SetRenderVSync(app->ren, want ? 1 : 0) ? want : 0;
    }
    /* Anti-aliasing makes nk_convert emit fringe geometry for every shape,
     * which is the bulk of a repaint. On by default because it is what makes
     * rounded corners and strokes look clean. */
    app->aa = env_int("CURIE_AA", 1);
    app->redraw_always  = SDL_getenv("CURIE_REDRAW") &&
                          SDL_strcmp(SDL_getenv("CURIE_REDRAW"), "always") == 0;
    /* Hit-testing walks the render tree, so it is capped rather than run on
     * every pointer sample - that, not painting, was the cost while moving. */
    app->hover_interval = 1000 / (env_int("CURIE_HOVER_HZ", 30) > 0
                                  ? env_int("CURIE_HOVER_HZ", 30) : 30);

    set_window_icon(app->win);

    /* SDL reports the scale only once the window exists. */
    curie_set_scale(curie_dpi_query_scale(app->win));

    app->ctx = nk_sdl_init(app->win, app->ren, nk_sdl_allocator());
    if (!app->ctx) return SDL_APP_FAILURE;

    rebuild_font(app);
    apply_theme(app);

    app->cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->cur_text    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);

    app->dirty = 1;
    nk_textedit_init_fixed(&app->edit, app->edit_buf, sizeof(app->edit_buf));
    nk_textedit_text(&app->edit, "you@example.com", 15);
    app->edit.cursor = app->edit.string.len;

    /* The document: markup and stylesheet both read from disk at runtime,
     * pico.css straight out of the submodule. */
    {
        curie_painter painter;
        SDL_zero(painter);
        painter.user        = app;
        painter.fill_rect   = pf_fill_rect;
        painter.stroke_rect = pf_stroke_rect;
        painter.draw_text   = pf_draw_text;
        painter.fill_gradient = pf_fill_gradient;
        painter.anchor_click  = pf_anchor_click;
        painter.image_size  = pf_image_size;
        painter.image_draw  = pf_image_draw;
        painter.push_clip   = pf_push_clip;
        painter.pop_clip    = pf_pop_clip;
        painter.text_width  = pf_text_width;
        painter.font_height = pf_font_height;

        /* Order matters: Open-Color defines the custom properties that the
         * app stylesheet reads, and simple.css styles the bare semantics
         * underneath. All three are read from disk at runtime. */
        char path[1024];
        app->painter = painter;      /* documents are built on demand */
        if (curie_path(path, sizeof(path), "assets/login.html"))
            app->page_template = curie_read_file(path, NULL);
        if (!app->page_template)
            SDL_Log("could not read assets/login.html");
        else
            rebuild_page(app);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = (App *)appstate;

    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;

    if (app) {
        if (event->type != SDL_EVENT_MOUSE_MOTION)
            app->dirty = 1;
        switch (event->type) {
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            /* One portable event replaces WM_SETTINGCHANGE and the macOS and
             * XDG-portal paths that never got written. The page carries the
             * scheme as an attribute, so it has to be rebuilt too. */
            if (app->theme_mode == THEME_SYSTEM) {
                apply_theme(app);
                app->page_dirty = 1;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT)
                handle_button(app, event->button.x, event->button.y, 1);
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT)
                handle_button(app, event->button.x, event->button.y, 0);
            break;

        case SDL_EVENT_TEXT_INPUT:
            if (app->focused && app->doc)
                nk_textedit_text(&app->edit, event->text.text,
                                 (int)SDL_strlen(event->text.text));
            break;

        case SDL_EVENT_KEY_DOWN:
            if (event->key.key == SDLK_F2) {
                static const char *cyc[3] = { "#theme=light", "#theme=dark",
                                              "#theme=system" };
                pf_anchor_click(app, cyc[app->theme_mode % 3]);
                break;
            }
            if (app->focused) edit_key(app, event->key.key, event->key.mod);
            break;

        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            curie_set_scale(curie_dpi_query_scale(app->win));
            rebuild_font(app);
            break;

        default:
            break;
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
    if (app->page_dirty) app->dirty = 1;

    /* Hover tracking runs before the dirty gate, because it is what decides
     * whether a repaint is needed at all. A hit-test is cheap; repainting the
     * document for every pixel of pointer movement is not. */
    if (app->doc) {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        Uint64 now_ms = SDL_GetTicks();
        if ((mx != app->last_mx || my != app->last_my) &&
            now_ms - app->hover_t0 >= (Uint64)app->hover_interval) {
            app->hover_t0 = now_ms;
            int over_field = app->field_ok &&
                             mx >= app->field.x && mx <= app->field.x + app->field.w &&
                             my >= app->field.y - 8 && my <= app->field.y + app->field.h + 8;

            if (curie_doc_mouse_move(app->doc, (int)mx, (int)my))
                app->dirty = 1;       /* the hovered element changed */
            apply_cursor(app, over_field);
            app->last_mx = mx;
            app->last_my = my;
        }
    }

    /* Nothing changed since the last frame: yield instead of redrawing.
     * SDL drives this loop, so the sleep is what keeps the CPU idle. */
    if (!app->dirty && !app->redraw_always) {
        /* Short enough that an incoming click is picked up almost at once;
         * long enough that an idle app still costs nothing measurable. */
        SDL_Delay(2);
        return SDL_APP_CONTINUE;
    }

    {
        Uint64 now = SDL_GetTicks();
        app->fps_frames++;
        if (now - app->fps_t0 >= 1000) {
            app->fps = app->fps_frames * 1000.0f / (float)(now - app->fps_t0);
            app->fps_frames = 0;
            app->fps_t0 = now;

            /* Temporary instrumentation: a GUI-subsystem binary has no
             * console, so the per-second counters go to a file. */
            {
                FILE *lf = fopen("build/stats.log", "a");
                if (lf) {
                    char ln[128];
                    SDL_snprintf(ln, sizeof(ln),
                                 "fps=%.0f paints=%d layouts=%d rebuilds=%d",
                                 app->fps, app->n_paints,
                                 app->n_layouts, app->n_rebuilds);
                    fputs(ln, lf);
                    fputc(10, lf);
                    fclose(lf);
                }
            }

        }
    }

    /* The document owns the whole window: no title bar, no border, no
     * padding, and a transparent panel background so the page's own backdrop
     * reaches every edge. Nuklear is reduced to supplying a command buffer,
     * which is exactly the role FINDINGS 2.1 assigns it. */
    if (app->doc) {
        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
        nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                 nk_style_item_color(nk_rgba(0, 0, 0, 0)));

        if (nk_begin(ctx, "page", nk_rect(0, 0, (float)win_w, (float)win_h),
                     NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR)) {
            struct nk_rect region = nk_window_get_content_region(ctx);

            app->canvas     = nk_window_get_canvas(ctx);
            app->content    = region;
            app->clip_depth = 0;

            /* litehtml layout is expensive and only changes when the
             * viewport or the document does. */
            if (app->relayout ||
                (int)region.w != app->laid_w || (int)region.h != app->laid_h) {
                {
                    Uint64 lt0 = SDL_GetPerformanceCounter();
                    curie_doc_render(app->doc, (int)region.w, (int)region.h);
                    app->last_layout_ms = 1000.0f *
                        (float)(SDL_GetPerformanceCounter() - lt0) /
                        (float)SDL_GetPerformanceFrequency();
                    {
                        FILE *lf = fopen("build/stats.log", "a");
                        if (lf) {
                            char ln[96];
                            SDL_snprintf(ln, sizeof(ln), "layout=%.1f ms",
                                         app->last_layout_ms);
                            fputs(ln, lf); fputc(10, lf); fclose(lf);
                        }
                    }
                }
                app->n_layouts++;
                app->laid_w = (int)region.w;
                app->laid_h = (int)region.h;
                app->relayout = 0;
                app->field_dirty = 1;
            }
            curie_doc_draw(app->doc, (int)region.x, (int)region.y,
                           (int)region.w, (int)region.h);

            /* The editable field: litehtml lays out an empty span, and the
             * text plus caret are painted into its box. Querying the box is
             * what avoids rebuilding the document on every keystroke. */
            {
                int fx, fy, fw, fh;
                if (app->field_dirty &&
                    curie_doc_element_rect(app->doc, FIELD_SELECTOR,
                                           &fx, &fy, &fw, &fh)) {
                    app->field = nk_rect(region.x + fx, region.y + fy,
                                         (float)fw, (float)fh);
                    app->field_ok = 1;
                    app->field_dirty = 0;
                }
                if (app->field_ok) {
                    const struct nk_user_font *f = ctx->style.font;
                    struct nk_color fg = app->dark ? nk_rgb(233, 236, 239)
                                                   : nk_rgb(33, 37, 41);
                    int len;
                    const char *t = edit_text(app, &len);
                    float ty;

                    ty = app->field.y + (app->field.h - f->height) * 0.5f;

                    if (len > 0)
                        nk_draw_text(app->canvas,
                                     nk_rect(app->field.x, ty,
                                             app->field.w, f->height),
                                     t, len, f, nk_rgba(0, 0, 0, 0), fg);

                    if (app->focused) {
                        float cx = app->field.x + edit_width(app, app->edit.cursor);
                        nk_fill_rect(app->canvas,
                                     nk_rect(cx, ty, 1.0f, f->height), 0.0f, fg);
                    }
                }
            }

            app->canvas = NULL;
        }
        nk_end(ctx);

        nk_style_pop_style_item(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_vec2(ctx);
    }

    /* Still deferred - rebuilding mid-paint would free the tree being drawn -
     * but the frame that follows is not gated behind an idle sleep, so the
     * new page appears on the very next iteration. */
    if (app->page_dirty) {
        rebuild_page(app);
        app->dirty = 1;
    }

    SDL_SetRenderDrawColor(app->ren, app->clear.r, app->clear.g,
                           app->clear.b, 255);
    SDL_RenderClear(app->ren);
    nk_sdl_render(ctx, app->aa ? NK_ANTI_ALIASING_ON
                              : NK_ANTI_ALIASING_OFF);
    SDL_RenderPresent(app->ren);
    app->n_paints++;
    app->dirty = 0;
    return SDL_APP_CONTINUE;
}

void
SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    App *app = (App *)appstate;
    (void)result;

    if (app) {
        if (app->cur_default) SDL_DestroyCursor(app->cur_default);
        if (app->cur_pointer) SDL_DestroyCursor(app->cur_pointer);
        if (app->cur_text)    SDL_DestroyCursor(app->cur_text);
        if (app->page_template) curie_free(app->page_template);
        if (app->doc) curie_doc_destroy(app->doc);
        if (app->ctx) nk_sdl_shutdown(app->ctx);
        if (app->ren) SDL_DestroyRenderer(app->ren);
        if (app->win) SDL_DestroyWindow(app->win);
        SDL_free(app);
    }
}
