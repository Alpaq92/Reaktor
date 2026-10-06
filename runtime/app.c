#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL_main.h>
#include "internal.h"
#include "declare.h"
#include "anim.h"
#include "locale.h"
#include "reaktor/launch.h"
#include "lifecycle.h"
#ifdef __EMSCRIPTEN__
#include "keyboard_web.h"
#endif

static int
effective_dark(const App *app)
{
    if (app->theme_mode == REAKTOR_THEME_LIGHT) return 0;
    if (app->theme_mode == REAKTOR_THEME_DARK)  return 1;
    return reaktor_prefers_dark() > 0;
}

size_t reaktor_rss[REAKTOR_RSS_STEPS];
size_t reaktor_priv[REAKTOR_RSS_STEPS];

static reaktor_launch g_launch;
static App           *g_app;
static int            g_started, g_stopped;
static SDL_AtomicInt  g_exit_code, g_hard_quit, g_running;
static char           g_quit_tag;
static char         **g_owned;
static int            g_owned_n;
static const char    *g_icon_name, *g_font, *g_font_bold, *g_cli_font,
                     *g_cli_font_bold;
static char          *g_title, *g_pending_title;
static reaktor_asset  g_css_last[REAKTOR_LAYER_MAX];
static int            g_css_last_n = -1;
static const char    *g_css[REAKTOR_LAYER_MAX], *g_cli_css[REAKTOR_LAYER_MAX];
static int            g_css_n, g_cli_css_n, g_css_pending;
static const char    *g_fallbacks[REAKTOR_LAYER_MAX];
static int            g_fallback_n;
static const char    *g_icon_dirs[REAKTOR_LAYER_MAX], *g_cli_icons[REAKTOR_LAYER_MAX];
static int            g_icon_dir_n, g_cli_icon_n;

static const char *
own(const char *str)
{
    char *d, **grown;
    int i;

    if (!str) return NULL;
    for (i = 0; i < g_owned_n; i++)
        if (SDL_strcmp(g_owned[i], str) == 0) return g_owned[i];
    d = SDL_strdup(str);
    if (!d) return NULL;
    grown = (char **)SDL_realloc(g_owned,
                                 (size_t)(g_owned_n + 1) * sizeof *g_owned);
    if (!grown) {
        SDL_free(d);
        return NULL;
    }
    g_owned = grown;
    g_owned[g_owned_n++] = d;
    return d;
}

static void
forget_owned(void)
{
    while (g_owned_n > 0) SDL_free(g_owned[--g_owned_n]);
    SDL_free(g_owned);
    g_owned = NULL;
}

const char *
reaktor_asset_name(const reaktor_asset *a, const char *fallback)
{
    const char *name;

    if (a->path) return own(a->path);
    if (!a->data) return NULL;
    name = own(a->name ? a->name : fallback);
    if (name && !reaktor_asset_register(name, a->data, a->size))
        SDL_Log("too many registered assets; %s is not one", name);
    return name;
}

static const char *
launch_title(void)
{
    if (g_title) return g_title;
    if (g_launch.window.title) return g_launch.window.title;
    return g_launch.name ? g_launch.name : "Reaktor";
}

static void
hint(const char *name, const char *value)
{
    SDL_SetHintWithPriority(name, value, SDL_HINT_OVERRIDE);
}

void
reaktor_rss_mark(int step)
{
    reaktor_process_memory(&reaktor_rss[step], &reaktor_priv[step]);
}

/* SDL_free the result. */
static char *
from_here(const char *path)
{
    char *cwd, *out = NULL;

    if (reaktor_path_absolute(path)) return SDL_strdup(path);
    cwd = SDL_GetCurrentDirectory();
    if (!cwd) return NULL;
    if (SDL_asprintf(&out, "%s%s", cwd, path) < 0) out = NULL;
    SDL_free(cwd);
    return out;
}

static void
add_fallback(const char *path)
{
    char *full = from_here(path);

    reaktor_text_add_fallback(full ? full : path);
    SDL_free(full);
}

static const char *
own_here(const char *path)
{
    char *full = from_here(path);
    const char *owned = own(full ? full : path);

    SDL_free(full);
    return owned;
}

static int
take_flags(App *app, int argc, char **argv)
{
    int i, n = 0;

    g_cli_css_n = g_cli_icon_n = 0;
    g_cli_font = g_cli_font_bold = NULL;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (v && SDL_strcmp(a, "--shot") == 0)       { app->shot_path = v; i++; }
        else if (v && SDL_strcmp(a, "--console") == 0) {
            g_launch.console = SDL_strcmp(v, "debug") == 0  ? REAKTOR_CONSOLE_DEBUG
                             : SDL_strcmp(v, "parent") == 0 ? REAKTOR_CONSOLE_PARENT
                             : REAKTOR_CONSOLE_NONE;
            i++;
        }
        else if (v && SDL_strcmp(a, "--css") == 0) {
            if (g_cli_css_n < REAKTOR_LAYER_MAX) g_cli_css[g_cli_css_n++] = own_here(v);
            else SDL_Log("--css: four at most; %s is not read", v);
            i++;
        }
        else if (v && SDL_strcmp(a, "--icons") == 0) {
            if (g_cli_icon_n < REAKTOR_LAYER_MAX) g_cli_icons[g_cli_icon_n++] = own_here(v);
            else SDL_Log("--icons: four at most; %s is not read", v);
            i++;
        }
        else if (v && SDL_strcmp(a, "--font") == 0)      { g_cli_font = own_here(v); i++; }
        else if (v && SDL_strcmp(a, "--font-bold") == 0) { g_cli_font_bold = own_here(v); i++; }
        else if (v && SDL_strcmp(a, "--a11y-dump") == 0) { app->dump_path = v; i++; }
        else if (v && SDL_strcmp(a, "--renderer") == 0) { app->renderer_pref = v; i++; }
        else if (v && SDL_strcmp(a, "--video") == 0)    { app->video_pref = v; i++; }
        else if (v && SDL_strcmp(a, "--lang") == 0)     { app->lang_pref = v; i++; }
        else if (v && SDL_strcmp(a, "--font-fallback") == 0) { add_fallback(v); i++; }
        else if (v && SDL_strcmp(a, "--theme") == 0) {
            if (SDL_strcmp(v, "light") == 0)     app->theme_mode = REAKTOR_THEME_LIGHT;
            else if (SDL_strcmp(v, "dark") == 0) app->theme_mode = REAKTOR_THEME_DARK;
            else                                 app->theme_mode = REAKTOR_THEME_SYSTEM;
            i++;
        } else {
            argv[n++] = argv[i];
        }
    }
    if (n < argc) argv[n] = NULL;
    return n;
}

static const char *
theme_sheet(int dark)
{
    return dark ? "external/tinycss/src/variables-dark.css"
                : "external/tinycss/src/variables-light.css";
}

void
load_theme(App *app)
{
    const char *sheets[SHEET_MAX];
    int n = 0, i;
    unsigned char c[4];
    Uint64 t0, t1;

    app->dark = effective_dark(app);

    sheets[n++] = theme_sheet(app->dark);
    sheets[n++] = CORE_SHEET;
    if (!g_launch.no_reaktor_css) sheets[n++] = APP_SHEET;
    for (i = 0; i < g_css_n && n < SHEET_MAX; i++) sheets[n++] = g_css[i];
    for (i = 0; i < g_cli_css_n && n < SHEET_MAX; i++) sheets[n++] = g_cli_css[i];
    app->sheets = n;

    t0 = SDL_GetPerformanceCounter();
    if (!reaktor_style_init(sheets, n, app->dark ? "dark" : "light"))
        SDL_Log("stylesheets failed to load; Nuklear defaults apply");
    t1 = SDL_GetPerformanceCounter();
    app->style_ms_x100 = (int)(100000.0 * (double)(t1 - t0) /
                               (double)SDL_GetPerformanceFrequency());

    app->page    = reaktor_style_token("--background-body", c)
                 ? nk_rgba(c[0], c[1], c[2], c[3]) : nk_rgb(247, 247, 247);
    app->card_bg = reaktor_style_token("--background", c)
                 ? nk_rgba(c[0], c[1], c[2], c[3]) : nk_rgb(226, 226, 226);
    app->text    = reaktor_style_token("--text-main", c)
                 ? nk_rgba(c[0], c[1], c[2], c[3]) : nk_rgb(51, 51, 51);
    if (reaktor_style_token("--text-muted", c))
        SDL_snprintf(app->icon_hex, sizeof(app->icon_hex),
                     "#%02x%02x%02x", c[0], c[1], c[2]);
    else
        SDL_strlcpy(app->icon_hex, "#6a6a6a", sizeof(app->icon_hex));

    if (reaktor_style_token("--links", c))
        SDL_snprintf(app->accent_hex, sizeof(app->accent_hex),
                     "#%02x%02x%02x", c[0], c[1], c[2]);
    else
        SDL_strlcpy(app->accent_hex, REAKTOR_BRAND, sizeof(app->accent_hex));

    reaktor_window_set_dark(app->win, app->dark);

    img_cache_clear(app);
    apply_widget_style(app);

    app->clear   = app->page;
    app->dirty   = 1;
    reaktor_windows_restyle();
}

/* A window's own wake carries its id; the main window's carries none. */
static bool
wake_app(const App *app)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    if (app && app->secondary) e.user.windowID = app->win_id;
    return SDL_PushEvent(&e);
}

static bool
wake_loop(void)
{
    return wake_app(NULL);
}

#define TEXT_ROOM (NK_INPUT_MAX - 1)

#ifdef __EMSCRIPTEN__
static void
web_page_size(int *w, int *h)
{
    int cw = EM_ASM_INT({ return window.innerWidth | 0; });
    int ch = EM_ASM_INT({ return window.innerHeight | 0; });
    if (cw > 64 && ch > 64) { *w = cw; *h = ch; }
}
static Uint32       g_page_key;
static enum nk_keys g_web_key;

/* A phone's named keys, REAKTOR_WEB_KEY_BACKSPACE (-1) first. */
static const struct { SDL_Keycode sdl; enum nk_keys nk; } g_soft[] = {
    { SDLK_BACKSPACE, NK_KEY_BACKSPACE }, { SDLK_RETURN, NK_KEY_ENTER },
    { SDLK_LEFT, NK_KEY_LEFT },           { SDLK_RIGHT, NK_KEY_RIGHT },
    { SDLK_UP, NK_KEY_UP },               { SDLK_DOWN, NK_KEY_DOWN },
    { SDLK_HOME, NK_KEY_TEXT_START },     { SDLK_END, NK_KEY_TEXT_END },
    { SDLK_DELETE, NK_KEY_DEL }
};
#define SOFT_KEYS ((int)(sizeof(g_soft) / sizeof(g_soft[0])))

/* Let go next frame, or Nuklear never sees it pressed. */
static void
page_key_take(struct nk_context *ctx, int code)
{
    if (code > 0) {
        nk_input_unicode(ctx, (nk_rune)code);
        return;
    }
    g_web_key = code < 0 && -code - 1 < SOFT_KEYS ? g_soft[-code - 1].nk : NK_KEY_NONE;
    if (g_web_key) nk_input_key(ctx, g_web_key, nk_true);
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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define COBJMACROS
#include <dxgi.h>
int
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
int renderer_is_software(SDL_Renderer *ren) { (void)ren; return 0; }
#endif

#define HOVER_GAP_MS 50

Uint64 g_hover_gap_ms = HOVER_GAP_MS;
static int    g_hover_gap_pinned;

#define CAL_SKIP    3
#define CAL_FRAMES  8

static void
calibrate_hover_gap(App *app)
{
    double now;

    if (g_hover_gap_pinned || app->cal_done) return;
    if (app->cal_frames < CAL_SKIP) { app->cal_frames++; return; }
    now = reaktor_process_cpu_ms();
    if (app->cal_frames == CAL_SKIP) app->cal_cpu0 = now;
    app->cal_frames++;
    if (app->cal_frames < CAL_SKIP + CAL_FRAMES + 1) return;

    app->cpu_ms_per_frame = (float)((now - app->cal_cpu0) / (double)CAL_FRAMES);
    app->cal_done = 1;
    g_hover_gap_ms = app->cpu_ms_per_frame > 40.0f ? 150
                   : app->cpu_ms_per_frame > 15.0f ? 100
                   : HOVER_GAP_MS;
}

int
reaktor_hot_motion(App *app, float mx, float my)
{
    struct nk_rect nr;
    int i, over = -1, over_top = -1, had, has;

    for (i = 0; i < app->hot_n; i++) {
        struct nk_rect r = app->hot[i].r;

        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
            over = i;
            if (app->hot[i].top) over_top = i;
        }
    }
    if (over_top >= 0) over = over_top;
    nr  = over >= 0 ? app->hot[over].r : nk_rect(0, 0, 0, 0);
    had = app->hot_last >= 0;
    has = over >= 0;
    if (had != has || (has && (nr.x != app->hot_last_r.x || nr.y != app->hot_last_r.y ||
                               nr.w != app->hot_last_r.w || nr.h != app->hot_last_r.h))) {
        int was = had && app->hot_last_repaint;

        app->hot_last         = over;
        app->hot_last_r       = nr;
        app->hot_last_repaint = has ? app->hot[over].repaint : 0;
        app->want_cursor      = has ? app->hot[over].cursor : 0;
        return was || app->hot_last_repaint;
    }
    return has && app->hot[over].track;
}

void
reaktor_show_cursor(App *app)
{
    SDL_Cursor *c;

    if (app->want_cursor == app->cur_shown) return;
    c = app->want_cursor == 1 ? app->cur_pointer
      : app->want_cursor == 2 ? app->cur_text
      : app->cur_default;
    if (c) SDL_SetCursor(c);
    app->cur_shown = app->want_cursor;
}

void
reaktor_place_ime(App *app)
{
    SDL_Window *keys = SDL_GetKeyboardFocus();

    if (app->ime_valid && (!keys || keys == app->win))
        SDL_SetTextInputArea(app->win, &app->ime_rect, app->ime_cursor);
    app->ime_valid = 0;
}

static void
hover_redraw(App *app)
{
    if (app->dragging || SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms) {
        app->dirty = 1;
    } else if (!app->hover_pending) {
        app->hover_pending = 1;
        hint(SDL_HINT_MAIN_CALLBACK_RATE, "20");
    }
}

static SDL_AppResult
fail(App *app, const char *what)
{
    char msg[512];

    SDL_Log("%s: %s", what, SDL_GetError());
    if (app->shot_path || app->dump_path) return SDL_APP_FAILURE;
    SDL_snprintf(msg, sizeof(msg), "%s.\n\n%s", what, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, launch_title(), msg, NULL);
    return SDL_APP_FAILURE;
}

static void
push_quit(reaktor_quit_reason why)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    e.user.code = (Sint32)why;
    e.user.data1 = &g_quit_tag;
    SDL_PushEvent(&e);
}

void
reaktor_console_quit(void)
{
    push_quit(REAKTOR_QUIT_CONSOLE);
}

void
reaktor_hard_quit(int code)
{
    SDL_SetAtomicInt(&g_exit_code, code);
    SDL_SetAtomicInt(&g_hard_quit, 1);
    wake_loop();
}

static void
run_stop(App *app)
{
    if (g_started && !g_stopped) {
        g_stopped = 1;
        if (g_launch.stop) g_launch.stop(app);
    }
    reaktor_process_stopped();
}

void
reaktor_session_end(void)
{
    if (!g_app) return;
    if (g_started && !g_stopped && g_launch.closing)
        g_launch.closing(g_app, REAKTOR_QUIT_SESSION);
    run_stop(g_app);
    reaktor_hard_quit(0);
}

static void
take_assets(App *app)
{
    int i;

    app->font_face = g_cli_font ? g_cli_font : g_font ? g_font : FONT_FILE;
    app->font_face_bold = g_cli_font_bold ? g_cli_font_bold
                        : g_cli_font      ? g_cli_font
                        : g_font_bold     ? g_font_bold
                        : g_font          ? g_font
                        : FONT_BOLD_FILE;
    for (i = 0; i < g_fallback_n; i++) reaktor_text_add_fallback(g_fallbacks[i]);
    app->icon_dir_count = 0;
    for (i = 0; i < g_cli_icon_n; i++)
        app->icon_dirs[app->icon_dir_count++] = g_cli_icons[i];
    for (i = 0; i < g_icon_dir_n; i++)
        app->icon_dirs[app->icon_dir_count++] = g_icon_dirs[i];
}

static void take_pending(App *app);

static SDL_AppResult
app_init(void **appstate, int argc, char *argv[])
{
    App *app;

    reaktor_rss_mark(REAKTOR_RSS_ENTRY);
    app = (App *)SDL_calloc(1, sizeof(App));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;
    g_app = app;

    app->queue.moved_ok = 1;
    app->theme_mode = (int)g_launch.theme;
    argc = take_flags(app, argc, argv);
    reaktor_process_start((int)g_launch.console, launch_title());

    if (g_launch.name || g_launch.version || g_launch.id)
        SDL_SetAppMetadata(g_launch.name, g_launch.version, g_launch.id);
    hint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    hint(SDL_HINT_TIMER_RESOLUTION, "0");
    hint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (app->video_pref) hint(SDL_HINT_VIDEO_DRIVER, app->video_pref);
#ifndef __APPLE__
    /* SDL drops the click that activates a window; only macOS expects that. */
    hint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) return fail(app, "SDL could not start");
    /* SDL gives the first registration SDL_EVENT_USER, the type wakes push. */
    SDL_RegisterEvents(1);
    reaktor_signal_watch();
    if (g_launch.console == REAKTOR_CONSOLE_DEBUG)
        SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG);

    reaktor_rss_mark(REAKTOR_RSS_SDL);
    take_assets(app);

    if (app->renderer_pref && SDL_strcmp(app->renderer_pref, "software") != 0) {
        SDL_strlcpy(app->render_mode, app->renderer_pref,
                    sizeof(app->render_mode));
        if (SDL_strcmp(app->renderer_pref, "auto") != 0)
            hint(SDL_HINT_RENDER_DRIVER, app->renderer_pref);
    } else {
        SDL_strlcpy(app->render_mode, "auto: software",
                    sizeof(app->render_mode));
        hint(SDL_HINT_RENDER_DRIVER, "software");
    }

#ifdef __EMSCRIPTEN__
    app->borderless = 0;
#else
    app->borderless = g_launch.window.borderless != 0;
#endif

    {
        float pre = reaktor_dpi_query_scale(NULL);
        int win_w = (int)((g_launch.window.w > 0 ? g_launch.window.w : WINDOW_WIDTH)
                          * pre + 0.5f);
        int win_h = (int)((g_launch.window.h > 0 ? g_launch.window.h : WINDOW_HEIGHT)
                          * pre + 0.5f);

#ifdef __EMSCRIPTEN__
        web_page_size(&win_w, &win_h);
#endif
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE |
                                SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                (app->borderless ? SDL_WINDOW_BORDERLESS : 0);

        if (!SDL_CreateWindowAndRenderer(launch_title(), win_w, win_h, flags,
                                         &app->win, &app->ren)) {
            SDL_Log("renderer '%s' would not start (%s); falling back",
                    app->render_mode, SDL_GetError());
            if (app->win) { SDL_DestroyWindow(app->win); app->win = NULL; }
            hint(SDL_HINT_RENDER_DRIVER, NULL);
            SDL_strlcpy(app->render_mode, "auto", sizeof(app->render_mode));
            if (!SDL_CreateWindowAndRenderer(launch_title(), win_w, win_h, flags,
                                             &app->win, &app->ren))
                return fail(app, "The window could not be opened");
        }

        if (renderer_is_software(app->ren)) {
            if (SDL_strcmp(app->render_mode, "auto") == 0) {
                SDL_Renderer *sw;

                SDL_DestroyRenderer(app->ren);
                app->ren = NULL;
                sw = SDL_CreateRenderer(app->win, "software");
                if (!sw) sw = SDL_CreateRenderer(app->win, NULL);
                if (!sw) return fail(app, "No renderer could draw the window");
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
        app->sw_noaa = 0;
#ifndef __EMSCRIPTEN__
        if (g_launch.window.min_w > 0 || g_launch.window.min_h > 0)
            SDL_SetWindowMinimumSize(app->win,
                                     (int)(g_launch.window.min_w * pre + 0.5f),
                                     (int)(g_launch.window.min_h * pre + 0.5f));
#endif
        reaktor_session_watch(app->win);
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 0,
                                   web_on_resize);
#endif
    reaktor_a11y_platform_init(reader_activate, reader_focus, app);

    app->vsync_on = SDL_SetRenderVSync(app->ren, 1) ? 1 : 0;
    app->aa = 1;
    app->redraw_always = 0;

    SDL_strlcpy(app->frame_rate, "waitevent", sizeof(app->frame_rate));

    {
        const SDL_DisplayMode *m =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app->win));
        float hz = (m && m->refresh_rate > 1.0f) ? m->refresh_rate : 60.0f;
        SDL_snprintf(app->drag_rate, sizeof(app->drag_rate), "%.2f", hz);
    }

    if (app->borderless && g_launch.window.hit_test)
        SDL_SetWindowHitTest(app->win, g_launch.window.hit_test, app);

    reaktor_rss_mark(REAKTOR_RSS_WINDOW);

    reaktor_set_window_icon(app->win, reaktor_launch_icon());
    reaktor_set_scale(reaktor_dpi_query_scale(app->win));
    apply_render_scale(app);
    reaktor_rss_mark(REAKTOR_RSS_ICON);

    app->ctx = nk_sdl_init(app->win, app->ren, nk_sdl_allocator());
    if (!app->ctx) return fail(app, "The interface could not start");
    reaktor_rss_mark(REAKTOR_RSS_NUKLEAR);

    /* Before the atlas, which bakes what the catalogs need. */
    reaktor_locale_start(app->lang_pref);
    rebuild_font(app);
    reaktor_rss_mark(REAKTOR_RSS_FONT);

    app->cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->cur_text    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);

    if (g_launch.start) {
        int rc = g_launch.start(app, argc, argv);

        if (rc) {
            SDL_SetAtomicInt(&g_exit_code, rc);
            return SDL_APP_FAILURE;
        }
    }

    nk_textedit_init_fixed(&app->edit, app->edit_buf, sizeof(app->edit_buf));

    take_pending(app);
    load_theme(app);
    reaktor_rss_mark(REAKTOR_RSS_STYLE);

    app->dirty = 1;

#ifdef __EMSCRIPTEN__
    g_page_key = SDL_RegisterEvents(1);
    reaktor_web_keys_init(g_page_key);
#endif
    nk_input_begin(app->ctx);
    g_started = 1;
    SDL_SetAtomicInt(&g_running, 1);
    return SDL_APP_CONTINUE;
}

enum {
    HELD_MOTION = 1, HELD_BUTTON, HELD_KEY, HELD_TEXT, HELD_PAGE, HELD_WHEEL
};

static int
held_kind(Uint32 type)
{
#ifdef __EMSCRIPTEN__
    if (g_page_key && type == g_page_key) return HELD_PAGE;
#endif
    switch (type) {
    case SDL_EVENT_MOUSE_MOTION:      return HELD_MOTION;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:   return HELD_BUTTON;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:            return HELD_KEY;
    case SDL_EVENT_TEXT_INPUT:        return HELD_TEXT;
    case SDL_EVENT_MOUSE_WHEEL:       return HELD_WHEEL;
    default:                          return 0;
    }
}

enum { KEY_TYPES, KEY_MODIFIES, KEY_CHANGES };

static int
key_role(const App *app, const SDL_KeyboardEvent *k)
{
    if (k->scancode >= SDL_SCANCODE_LCTRL && k->scancode <= SDL_SCANCODE_RGUI)
        return KEY_MODIFIES;
    if (k->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) return KEY_CHANGES;
    if (k->key == SDLK_SPACE) return app->editing ? KEY_TYPES : KEY_CHANGES;
    if (k->key > SDLK_SPACE && k->key != SDLK_DELETE &&
        !(k->key & SDLK_SCANCODE_MASK))
        return KEY_TYPES;
    return KEY_CHANGES;
}

static int
takes_change(struct input_queue *q, SDL_Keycode key)
{
    if (q->changed || q->text) return 0;
    q->changed = 1;
    q->key = key;
    return 1;
}

static int
takes_text(struct input_queue *q, size_t len)
{
    if (q->changed || q->text + len > TEXT_ROOM) return 0;
    q->text += len;
    return 1;
}

static int
frame_takes(App *app, const SDL_Event *e)
{
    struct input_queue *q = &app->queue;
    int c;

    if (app->key_click == KEY_CLICK_ASKED || app->key_click == KEY_CLICK_PRESS)
        return 0;
    switch (held_kind(e->type)) {
    case HELD_MOTION:
        if (q->moved_ok) q->moved = 1;
        return q->moved_ok;
    case HELD_BUTTON:
        if (q->moved && !e->button.down) return 0;
        if (!takes_change(q, SDLK_UNKNOWN)) return 0;
        q->moved_ok = 0;
        return 1;
    case HELD_KEY:
        switch (key_role(app, &e->key)) {
        case KEY_TYPES:    return 1;
        case KEY_MODIFIES: return !q->changed;
        default:
            if (!e->key.down && e->key.key == q->key) return 1;
            return takes_change(q, e->key.down ? e->key.key : SDLK_UNKNOWN);
        }
    case HELD_TEXT:
        return takes_text(q, SDL_strlen(e->text.text));
    case HELD_PAGE:
        c = e->user.code;
        if (c <= 0) return takes_change(q, SDLK_UNKNOWN);
        return takes_text(q, c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4);
    default:
        return 1;
    }
}

static int
hold(struct input_queue *q, const SDL_Event *event, const char *text, size_t len)
{
    SDL_Event *more;
    char *copy = NULL;
    int cap;

    if (q->n == q->cap) {
        cap = q->cap ? q->cap * 2 : 32;
        more = (SDL_Event *)SDL_realloc(q->ev, (size_t)cap * sizeof(*more));
        if (!more) return 0;
        q->ev = more;
        q->cap = cap;
    }
    /* SDL frees an event's text on its next pump. */
    if (text) {
        if (!(copy = (char *)SDL_malloc(len + 1))) return 0;
        SDL_memcpy(copy, text, len);
        copy[len] = '\0';
    }
    q->ev[q->n] = *event;
    if (copy) q->ev[q->n].text.text = copy;
    q->n++;
    return 1;
}

static int
hold_for_next_frame(App *app, const SDL_Event *event)
{
    struct input_queue *q = &app->queue;
    const char *t;
    size_t n;

    if (!held_kind(event->type)) {
        /* Window events stay behind held input. */
        if (!q->n || event->type < SDL_EVENT_WINDOW_FIRST ||
            event->type > SDL_EVENT_WINDOW_LAST)
            return 0;
        return hold(q, event, NULL, 0);
    }
    if (event->type == SDL_EVENT_TEXT_INPUT &&
        SDL_strlen(event->text.text) > TEXT_ROOM) {
        for (t = event->text.text; *t; t += n) {
            n = SDL_strlen(t);
            if (n > TEXT_ROOM) {
                n = TEXT_ROOM;
                while (n > 1 && (t[n] & 0xC0) == 0x80) n--;
            }
            if (!hold(q, event, t, n)) return t != event->text.text;
        }
        return 1;
    }
    if (!q->n && frame_takes(app, event)) return 0;
    if (event->type == SDL_EVENT_MOUSE_MOTION && q->n &&
        q->ev[q->n - 1].type == SDL_EVENT_MOUSE_MOTION) {
        q->ev[q->n - 1] = *event;
        return 1;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat && q->n &&
        q->ev[q->n - 1].type == SDL_EVENT_KEY_DOWN &&
        q->ev[q->n - 1].key.key == event->key.key)
        return 1;
    if (event->type != SDL_EVENT_TEXT_INPUT) return hold(q, event, NULL, 0);
    return hold(q, event, event->text.text, SDL_strlen(event->text.text));
}

void
reaktor_app_input_free(App *app)
{
    struct input_queue *q = &app->queue;

    while (q->n--)
        if (q->ev[q->n].type == SDL_EVENT_TEXT_INPUT)
            SDL_free((void *)q->ev[q->n].text.text);
    SDL_free(q->ev);
    q->ev = NULL;
    q->n = q->cap = 0;
}

static void take_event(App *app, SDL_Event *event);

static void
replay_held(App *app)
{
    struct input_queue *q = &app->queue;
    SDL_Event e;
    int taken = 0;

    q->changed  = 0;
    q->key      = SDLK_UNKNOWN;
    q->moved    = 0;
    q->moved_ok = 1;
    q->text     = 0;
#ifdef __EMSCRIPTEN__
    if (g_web_key) {
        nk_input_key(app->ctx, g_web_key, nk_false);
        g_web_key = NK_KEY_NONE;
    }
#endif
    if (q->pointer_back) {
        nk_input_motion(app->ctx, (int)q->pointer.x, (int)q->pointer.y);
        q->pointer_back = 0;
    }
    if (app->key_click >= KEY_CLICK_RELEASE) {
        q->changed  = 1;
        q->moved_ok = 0;
    }
    while (q->n && frame_takes(app, &q->ev[0])) {
        e = q->ev[0];
        q->n--;
        SDL_memmove(q->ev, q->ev + 1, (size_t)q->n * sizeof(*q->ev));
        take_event(app, &e);
        if (e.type == SDL_EVENT_TEXT_INPUT) SDL_free((void *)e.text.text);
        taken = 1;
    }
    if (taken) wake_app(app);
}

static void
take_event(App *app, SDL_Event *event)
{
#ifdef __EMSCRIPTEN__
    if (event->type == g_page_key) {
        int c = event->user.code;

        app->dirty = 1;
        /* Not Backspace, which also replays edits. */
        if (c <= REAKTOR_WEB_KEY_ENTER && c >= REAKTOR_WEB_KEY_DELETE) {
            SDL_Event k;

            SDL_zero(k);
            k.type         = SDL_EVENT_KEY_DOWN;
            k.key.down     = true;
            k.key.key      = g_soft[-c - 1].sdl;
            k.key.scancode = SDL_GetScancodeFromKey(k.key.key, NULL);
            k.key.mod      = SDL_GetModState();
            k.key.windowID = SDL_GetWindowID(app->win);
            if (g_launch.key && g_launch.key(app, &k)) return;
            if (!app->editing && focus_key(app, &k)) return;
        }
        page_key_take(app->ctx, c);
        return;
    }
#endif
    if (app->secondary) reaktor_windows_take(app, event);

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
    case SDL_EVENT_LOCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        app->dirty = 1;
        break;
    default:
        break;
    }

    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (app->dragging) { app->drag_moved = 1; app->dirty = 1; }
        if (reaktor_hot_motion(app, event->motion.x, event->motion.y))
            hover_redraw(app);
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        app->focus_visible = 0;
        app->dragging = 1;
        app->drag_mouse = event->button.which != SDL_TOUCH_MOUSEID &&
                          event->button.which != SDL_PEN_MOUSEID;
        app->restore_rate = 0;
        if (app->field_rect_valid) {
            struct nk_rect r = app->field_rect;
            float bx = event->button.x, by = event->button.y;
            app->drag_in_field = bx >= r.x && bx <= r.x + r.w &&
                                 by >= r.y && by <= r.y + r.h;
        }
        hint(SDL_HINT_MAIN_CALLBACK_RATE, app->drag_rate);
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (app->dragging) {
            app->dragging      = 0;
            app->drag_in_field = 0;
            app->restore_rate  = 2;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        app->dragging = 0;
        app->drag_in_field = 0;
        app->restore_rate = 2;
        break;

    /* Nuklear scrolls a panel at its end, after drawing what it holds. */
    case SDL_EVENT_MOUSE_WHEEL:
        if (!app->dragging) app->restore_rate = 2;
        break;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        app->hot_last = -1;
        app->hot_last_repaint = 0;
        app->want_cursor = 0;
        break;

    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        if (app->theme_mode == REAKTOR_THEME_SYSTEM) load_theme(app);
        break;

    case SDL_EVENT_LOCALE_CHANGED:
        if (!app->secondary && reaktor_locale_system() && reaktor_locale_set(NULL))
            reaktor_windows_dirty();
        break;

    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (app->secondary) break;
        reaktor_set_scale(reaktor_dpi_query_scale(app->win));
        apply_render_scale(app);
        img_cache_clear(app);
        rebuild_font(app);
        reaktor_windows_rescale();
        break;

    case SDL_EVENT_KEY_DOWN:
        app->queue.hook_ate = g_launch.key && g_launch.key(app, event);
        if (app->queue.hook_ate) return;
        if (focus_key(app, event)) return;
        break;

    case SDL_EVENT_KEY_UP:
        app->queue.hook_ate = 0;
        break;

    case SDL_EVENT_TEXT_INPUT:
        if (app->queue.hook_ate) {
            app->queue.hook_ate = 0;
            return;
        }
        break;

    case SDL_EVENT_DROP_FILE:
        if (g_launch.file_opened && event->drop.data)
            g_launch.file_opened(app, event->drop.data);
        app->dirty = 1;
        break;

    default:
        /* A wake: the runtime's own, or a type the a11y bridge registered. */
        if (event->type >= SDL_EVENT_USER) {
            SDL_SetAtomicInt(&app->wake, 0);
            app->dirty = 1;
        }
        break;
    }
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

static char *g_confirm;
static int   g_confirm_id;
static float g_confirm_h;

static void
confirm_body(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    (void)w; (void)h; (void)user;
    nk_layout_row_dynamic(ctx, g_confirm_h, 1);
    nk_style_push_font(ctx, reaktor_style_font(app, ".popup-title", 16, 1));
    nk_label_colored_wrap(ctx, g_confirm ? g_confirm : "",
                          reaktor_token("--text-bright",
                                        ctx->style.text.color));
    nk_style_pop_font(ctx);
    nk_layout_row_dynamic(ctx, 12.0f, 1);
    nk_spacer(ctx);
    nk_layout_row_template_begin(ctx, 36.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 96.0f);
    nk_layout_row_template_push_static(ctx, 96.0f);
    nk_layout_row_template_end(ctx);
    nk_spacer(ctx);
    if (reaktor_button_label(app, ctx, "Cancel"))
        reaktor_floater_close(app, g_confirm_id);
    if (reaktor_button_accent(app, ctx, "Quit")) reaktor_quit(app, 0);
}

static int
confirm_open(App *app)
{
    const struct nk_user_font *f;
    SDL_WindowFlags flags;
    int n;

    if (!g_confirm) return 0;
    flags = SDL_GetWindowFlags(app->win);
    if (flags & SDL_WINDOW_HIDDEN) SDL_ShowWindow(app->win);
    if (flags & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(app->win);
    SDL_RaiseWindow(app->win);
    if (reaktor_floater_is_open(app, g_confirm_id)) return 1;
    f = reaktor_style_font(app, ".popup-title", 16, 1);
    n = (int)SDL_strlen(g_confirm);
    g_confirm_h = f->width(f->userdata, f->height, g_confirm, n) > 328.0f
                  ? 44.0f : 22.0f;
    g_confirm_id = reaktor_floater_open(app, &(reaktor_floater){
        .title = g_confirm, .w = 328.0f, .modal = 1,
        .h = g_confirm_h + 12.0f + 36.0f + 2.0f * REAKTOR_MENU_GAP,
        .body = confirm_body });
    return g_confirm_id != 0;
}

static SDL_AppResult
quit_asked(App *app, reaktor_quit_reason why)
{
    int refused = app && g_started && g_launch.closing &&
                  g_launch.closing(app, why);

    if (why != REAKTOR_QUIT_WINDOW && why != REAKTOR_QUIT_APP &&
        why != REAKTOR_QUIT_CONSOLE)
        return SDL_APP_SUCCESS;
    if (refused || (app && g_started && confirm_open(app)))
        return SDL_APP_CONTINUE;
    return SDL_APP_SUCCESS;
}

static SDL_AppResult
bare_quit(App *app)
{
    int again, r = reaktor_signal_reason(&again);

    if (again) {
        reaktor_hard_quit(130);
        return SDL_APP_SUCCESS;
    }
    if (r) return quit_asked(app, (reaktor_quit_reason)(r - 1));
#ifdef __APPLE__
    return quit_asked(app, REAKTOR_QUIT_APP);
#else
    return quit_asked(app, REAKTOR_QUIT_SIGNAL);
#endif
}

static void
feed(App *app, SDL_Event *event)
{
    if (hold_for_next_frame(app, event)) app->dirty = 1;
    else take_event(app, event);
}

enum { TOUCH_NONE, TOUCH_PRESSED, TOUCH_CAUGHT, TOUCH_DRAGGING, TOUCH_SCROLLING };

#define TOUCH_SLOP   8.0f
#define FLING_TAU_MS 325.0f
#define FLING_MIN    60.0f
#define FLING_STOP   20.0f

static void
touch_pointer(App *app, float x, float y, int release)
{
    SDL_Event e;

    SDL_zero(e);
    e.type            = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(app->win);
    e.motion.which    = SDL_TOUCH_MOUSEID;
    e.motion.x        = x;
    e.motion.y        = y;
    feed(app, &e);
    if (!release) return;
    SDL_zero(e);
    e.type            = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(app->win);
    e.button.which    = SDL_TOUCH_MOUSEID;
    e.button.button   = SDL_BUTTON_LEFT;
    e.button.clicks   = 1;
    e.button.x        = x;
    e.button.y        = y;
    feed(app, &e);
}

static void
touch_wheel(App *app, float dy)
{
    SDL_Event e;

    if (dy == 0.0f || app->touch_unit <= 0.0f) return;
    SDL_zero(e);
    e.type           = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = SDL_GetWindowID(app->win);
    e.wheel.which    = SDL_TOUCH_MOUSEID;
    e.wheel.y        = dy / app->touch_unit;
    e.wheel.mouse_x  = app->touch_x0;
    e.wheel.mouse_y  = app->touch_y0;
    feed(app, &e);
}

static void
touch_sample(App *app, float y, Uint64 ns)
{
    app->touch_ys[app->touch_n % 4] = y;
    app->touch_ns[app->touch_n % 4] = ns;
    app->touch_n++;
}

/* Nuklear moves a panel a tenth of its height per wheel step. */
static float
touch_unit(const App *app)
{
    struct nk_rect b = app->body_rect;
    float h = (float)app->laid_h;

    if (b.h > 0.0f && app->touch_x0 >= b.x && app->touch_x0 < b.x + b.w &&
        app->touch_y0 >= b.y && app->touch_y0 < b.y + b.h)
        h = b.h;
    return 0.1f * h;
}

static void
fling_start(App *app, Uint64 now)
{
    int    last = app->touch_n - 1, first = last, i;
    Uint64 t;
    float  v;

    if (last < 1 || now - app->touch_ns[last % 4] > 100000000u) return;
    for (i = last - 1; i >= 0 && i > last - 4; i--) {
        if (app->touch_ns[last % 4] - app->touch_ns[i % 4] > 100000000u) break;
        first = i;
    }
    t = app->touch_ns[last % 4] - app->touch_ns[first % 4];
    if (first == last || !t) return;
    v = (app->touch_ys[last % 4] - app->touch_ys[first % 4]) / ((float)t / 1e9f);
    if (SDL_fabsf(v) < FLING_MIN) return;
    app->fling_v      = v;
    app->fling_ns     = now;
    app->restore_rate = 0;
    hint(SDL_HINT_MAIN_CALLBACK_RATE, app->drag_rate);
    app->dirty = 1;
}

static void
fling_step(App *app)
{
    Uint64 now = SDL_GetTicksNS();
    float  dt;

    if (app->fling_v == 0.0f) return;
    dt = (float)(now - app->fling_ns) / 1e9f;
    app->fling_ns = now;
    if (app->touch_unit > 0.0f)
        nk_input_scroll(app->ctx, nk_vec2(0.0f, app->fling_v * dt / app->touch_unit));
    app->fling_v *= SDL_expf(-dt * 1000.0f / FLING_TAU_MS);
    if (SDL_fabsf(app->fling_v) < FLING_STOP) {
        app->fling_v      = 0.0f;
        app->restore_rate = 2;
    }
}

/* A finger that moves mostly up or down scrolls, and presses nothing. */
static int
touch_takes(App *app, const SDL_Event *e)
{
    struct nk_rect r = app->field_rect;
    float dx, dy;
    int   was = app->touch;

    switch (e->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.which != SDL_TOUCH_MOUSEID || e->button.button != SDL_BUTTON_LEFT)
            return 0;
        app->touch_x0 = e->button.x;
        app->touch_y0 = e->button.y;
        app->touch_n  = 0;
        touch_sample(app, e->button.y, e->button.timestamp);
        app->touch_in_field = app->field_rect_valid &&
                              e->button.x >= r.x && e->button.x <= r.x + r.w &&
                              e->button.y >= r.y && e->button.y <= r.y + r.h;
        if (app->fling_v != 0.0f) {
            app->fling_v      = 0.0f;
            app->restore_rate = 2;
            app->touch        = TOUCH_CAUGHT;
            return 1;
        }
        app->touch = TOUCH_PRESSED;
        return 0;

    case SDL_EVENT_MOUSE_MOTION:
        if (e->motion.which != SDL_TOUCH_MOUSEID) return 0;
        switch (app->touch) {
        case TOUCH_SCROLLING:
            touch_wheel(app, e->motion.y - app->touch_last);
            app->touch_last = e->motion.y;
            touch_sample(app, e->motion.y, e->motion.timestamp);
            return 1;
        case TOUCH_PRESSED:
        case TOUCH_CAUGHT:
            dx = e->motion.x - app->touch_x0;
            dy = e->motion.y - app->touch_y0;
            if (SDL_fabsf(dx) <= TOUCH_SLOP && SDL_fabsf(dy) <= TOUCH_SLOP)
                return app->touch == TOUCH_CAUGHT;
            if (app->touch_in_field || SDL_fabsf(dy) <= SDL_fabsf(dx)) {
                if (app->touch == TOUCH_CAUGHT) return 1;
                app->touch = TOUCH_DRAGGING;
                return 0;
            }
            /* Released away from every widget, the press clicks nothing. */
            if (app->touch == TOUCH_PRESSED) touch_pointer(app, -1e4f, -1e4f, 1);
            touch_pointer(app, app->touch_x0, app->touch_y0, 0);
            app->touch      = TOUCH_SCROLLING;
            app->touch_unit = touch_unit(app);
            app->touch_last = app->touch_y0 + (dy > 0.0f ? TOUCH_SLOP : -TOUCH_SLOP);
            touch_wheel(app, e->motion.y - app->touch_last);
            app->touch_last = e->motion.y;
            touch_sample(app, e->motion.y, e->motion.timestamp);
            return 1;
        default:
            return 0;
        }

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.which != SDL_TOUCH_MOUSEID || e->button.button != SDL_BUTTON_LEFT)
            return 0;
        app->touch = TOUCH_NONE;
        if (was == TOUCH_SCROLLING) {
            fling_start(app, e->button.timestamp);
            return 1;
        }
        return was == TOUCH_CAUGHT;

    default:
        return 0;
    }
}

static SDL_AppResult
app_event(void *appstate, SDL_Event *event)
{
    App *app = (App *)appstate, *to;

    if (SDL_GetAtomicInt(&g_hard_quit)) return SDL_APP_SUCCESS;
    if (!(to = reaktor_windows_app(event))) {
        switch (event->type) {
        case SDL_EVENT_TERMINATING:
            if (app) run_stop(app);
            return SDL_APP_SUCCESS;
        case SDL_EVENT_QUIT:
            return bare_quit(app);
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return quit_asked(app, REAKTOR_QUIT_WINDOW);
        case SDL_EVENT_USER:
            if (event->user.data1 == &g_quit_tag)
                return quit_asked(app, (reaktor_quit_reason)event->user.code);
            break;
        default:
            break;
        }
        if (!(to = app)) return SDL_APP_CONTINUE;
    }
    if (!touch_takes(to, event)) feed(to, event);
    return SDL_APP_CONTINUE;
}

static void
main_page(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    (void)user;
    g_launch.page(app, ctx, w, h);
}

static void
take_pending(App *app)
{
    char fallback[32];
    const char *n;
    int i;

    if (g_css_pending) {
        for (g_css_n = i = 0; i < g_css_last_n; i++) {
            SDL_snprintf(fallback, sizeof(fallback), "app/set%d.css", i);
            if ((n = reaktor_asset_name(&g_css_last[i], fallback))) g_css[g_css_n++] = n;
        }
        g_css_pending = 0;
    }
    if (app->theme_pending) {
        app->theme_mode    = app->theme_pending - 1;
        app->theme_pending = 0;
    }
}

void
reaktor_app_size(const App *app, int *w, int *h)
{
    float s = reaktor_scale();

    *w = *h = 0;
    SDL_GetWindowSizeInPixels(app->win, w, h);
    if (s > 0.0f) {
        *w = (int)(*w / s + 0.5f);
        *h = (int)(*h / s + 0.5f);
    }
}

void
reaktor_app_due(App *app, int w, int h)
{
    SDL_Window *over = SDL_GetMouseFocus();

    if (w != app->laid_w || h != app->laid_h) app->dirty = 1;
    if (app->fling_v != 0.0f) app->dirty = 1;

    if (app->hover_pending &&
        SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms) {
        app->hover_pending = 0;
        app->dirty = 1;
        if (!app->dragging && app->restore_rate < 1) app->restore_rate = 1;
    }

    if (app->dragging && app->drag_mouse && !app->queue.n &&
        !(SDL_GetGlobalMouseState(NULL, NULL) &
          (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK | SDL_BUTTON_RMASK |
           SDL_BUTTON_X1MASK | SDL_BUTTON_X2MASK))) {
        app->dragging      = 0;
        app->drag_in_field = 0;
        app->restore_rate  = 2;
        app->dirty = 1;
    }

    if (app->dragging) {
        if (app->drag_moved || app->hot_last_repaint ||
            SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms)
            app->dirty = 1;
    }

    if (over == app->win || (!over && !app->secondary)) reaktor_show_cursor(app);
}

static void
file_answer(App *app)
{
    if (!SDL_GetAtomicInt(&app->file_ready)) return;
    SDL_SetAtomicInt(&app->file_ready, 0);
    app->file_pending = 0;
    if (g_launch.file_opened)
        g_launch.file_opened(app, app->file_ok ? app->file_answer : NULL);
}

static int
take_shot(App *app)
{
    static int done;
    SDL_Surface *sh;

    if (!app->shot_path || !*app->shot_path || done) return 0;
    if (!reaktor_frame_settled(app)) {
        app->dirty = 1;
        wake_app(app);
        return 0;
    }
    done = 1;
    sh = SDL_RenderReadPixels(app->ren, NULL);
    if (!sh || !SDL_SaveBMP(sh, app->shot_path)) {
        SDL_Log("screenshot: %s", SDL_GetError());
        app->shot_failed = 1;
    }
    if (sh) SDL_DestroySurface(sh);
    return 1;
}

static void
key_click(App *app)
{
    struct nk_context *ctx = app->ctx;
    int x, y;

    if (app->key_click == KEY_CLICK_PRESS &&
        (app->dragging || !app->focus_seen)) {
        app->key_click = KEY_CLICK_NONE;
        return;
    }
    if (app->key_click < KEY_CLICK_PRESS) return;
    x = (int)(app->focus_rect.x + app->focus_rect.w * 0.5f);
    y = (int)(app->focus_rect.y + app->focus_rect.h * 0.5f);
    if (app->key_click == KEY_CLICK_PRESS) {
        app->queue.pointer = SDL_GetMouseFocus() == app->win
                             ? ctx->input.mouse.pos : nk_vec2(-1.0f, -1.0f);
        nk_input_motion(ctx, x, y);
        nk_input_button(ctx, NK_BUTTON_LEFT, x, y, nk_true);
        app->key_click = KEY_CLICK_RELEASE;
        wake_app(app);
    } else {
        nk_input_button(ctx, NK_BUTTON_LEFT, x, y, nk_false);
        app->key_click = app->key_click == KEY_CLICK_AGAIN
                         ? KEY_CLICK_ASKED : KEY_CLICK_NONE;
        app->queue.pointer_back = 1;
        if (!app->dragging) app->restore_rate = 2;
    }
    app->dirty = 1;
}

int
reaktor_app_frame(App *app, int win_w, int win_h, const char *title,
                  reaktor_page_fn page, void *user)
{
    struct nk_context *ctx = app->ctx;
    struct nk_rect area = nk_rect(0, 0, (float)win_w, (float)win_h);
    int is_main = !app->secondary, shot = 0, shown;
    Uint64 now = SDL_GetTicks(), f, t_build0, t_render0, t_present0, t_end;

    app->laid_w  = win_w;
    app->laid_h  = win_h;
    app->hot_n   = 0;
    app->editing = 0;
    if (app->last_frame_ms) app->frame_gap_ms = (float)(now - app->last_frame_ms);
    app->last_frame_ms = now;
    app->fps_frames++;
    if (now - app->fps_t0 >= 1000) {
        app->fps = app->fps_frames * 1000.0f / (float)(now - app->fps_t0);
        app->fps_frames = 0;
        app->fps_t0 = now;
    }

    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(app->clear));

    t_build0 = SDL_GetPerformanceCounter();
    if (is_main) reaktor_a11y_platform_drain();
    else reaktor_a11y_platform_window_drain(app->win);
    key_click(app);
    fling_step(app);
    nk_input_end(ctx);
    ctx->style.text.color = app->text;
    file_answer(app);

    app->focus_seen = 0;
    app->popup_lo = app->popup_hi = app->trap_lo = app->trap_hi = -1;
    if (!app->anim) app->anim = reaktor_anim_create();
    if (app->anim) reaktor_anim_frame(app->anim, app->frame_gap_ms);
    reaktor_a11y_begin(&app->a11y, title, area);
    reaktor_frame_begin(app, ctx, area);

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_hide());
    shown = reaktor_layer_begin(app, ctx, "page", area,
                                (nk_flags)NK_WINDOW_BACKGROUND |
                                (nk_flags)NK_WINDOW_NO_SCROLLBAR,
                                FOCUS_PAGE, 0,
                                is_main && reaktor_floaters_modal() ? HOLD_ALL
                                                                    : HOLD_NONE,
                                &app->page_held);
    nk_style_pop_style_item(ctx);
    if (shown) {
        nk_style_pop_vec2(ctx);
        page(app, ctx, win_w, win_h, user);
        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    }
    reaktor_layer_end(app, ctx, shown);
    if (is_main) {
        struct nk_window *pw = nk_window_find(ctx, "page");

        reaktor_floaters_draw(app, ctx, win_w, win_h);
        reaktor_toasts_draw(app, ctx, win_w, win_h,
                            reaktor_floaters_modal() ||
                            (reaktor_popup_open(pw) && pw->popup.type == NK_PANEL_POPUP));
    }
    app->focus_step = 0;
    if (app->focus_visible && app->focus_seen && app->focus_layer == FOCUS_POPUP)
        reaktor_focus_ring_overlay(app, ctx);
    app->stop_editing = 0;

    /* A click for a held widget would land on whatever holds it. */
    if (app->activate_id && app->focus_seen && !app->focus_held &&
        !reaktor_focus_covered(app))
        reaktor_key_click_ask(app);
    app->activate_id = 0;
    if (app->key_click == KEY_CLICK_ASKED) {
        if (app->focus_seen && (app->focus_held || reaktor_focus_covered(app))) {
            app->key_click = KEY_CLICK_NONE;
        } else {
            app->key_click = KEY_CLICK_PRESS;
            app->dirty = 1;
            wake_app(app);
        }
    }

    reaktor_frame_end();
    reaktor_a11y_end(&app->a11y);
    focus_resolve(app);
    if (is_main) {
        reaktor_a11y_platform_push(&app->a11y, app->focus_id);
        a11y_dump_once(app);
    } else {
        reaktor_a11y_platform_window_push(app->win, &app->a11y, app->focus_id,
                                          reader_activate, reader_focus, app);
    }

    if (app->anim && reaktor_anim_settle()) {
        app->dirty = 1;
        wake_app(app);
    }

    nk_sdl_update_TextInput(ctx);
#ifdef __EMSCRIPTEN__
    /* After the frame, when a field has said whether it is being edited. */
    reaktor_web_keys_wants(app->editing);
#endif
    reaktor_place_ime(app);

    nk_style_pop_style_item(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_vec2(ctx);

    f = SDL_GetPerformanceFrequency();
    t_render0 = SDL_GetPerformanceCounter();
    if (is_main) reaktor_text_prepare(ctx, app->ren);
    reaktor_render(app);
    t_present0 = SDL_GetPerformanceCounter();
    if (is_main) shot = take_shot(app);
    SDL_RenderPresent(app->ren);
    t_end = SDL_GetPerformanceCounter();

    app->last_draw_ms = SDL_GetTicks();
    if (is_main && app->first_frame_done) calibrate_hover_gap(app);
    app->build_ms_x100   = (int)(100000.0 * (double)(t_render0 - t_build0) / (double)f);
    app->render_ms_x100  = (int)(100000.0 * (double)(t_present0 - t_render0) / (double)f);
    app->present_ms_x100 = (int)(100000.0 * (double)(t_end - t_present0) / (double)f);

    nk_input_begin(ctx);

    app->dirty = 0;
    app->drag_moved = 0;
    if (app->restore_rate > 0) {
        if (--app->restore_rate > 0) {
            app->dirty = 1;
            wake_app(app);
        } else {
            hint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
        }
    }
    replay_held(app);
    return shot;
}

static SDL_AppResult
app_iterate(void *appstate)
{
    App *app = (App *)appstate;
    int win_w, win_h;

    if (SDL_GetAtomicInt(&g_hard_quit)) return SDL_APP_SUCCESS;
    if (g_pending_title) {
        SDL_free(g_title);
        g_title = g_pending_title;
        g_pending_title = NULL;
        SDL_SetWindowTitle(app->win, g_title);
    }
    if (g_css_pending || app->theme_pending) {
        take_pending(app);
        load_theme(app);
    }
    reaktor_windows_draw();

#ifdef __APPLE__
    if (app->borderless_lock_frames > 0)
        app->borderless_lock_frames--;
    if (app->borderless_pending) {
        int w = 0, h = 0;
        app->borderless_pending = 0;
        app->borderless = !app->borderless;
        SDL_SetWindowBordered(app->win, app->borderless ? false : true);
        SDL_SetWindowHitTest(app->win,
                             app->borderless ? g_launch.window.hit_test : NULL,
                             app->borderless ? app : NULL);
        SDL_SyncWindow(app->win);
        SDL_GetWindowSize(app->win, &w, &h);
        SDL_SetWindowSize(app->win, w, h);
        app->dirty = 1;
        app->borderless_lock_frames = 3;
    }
#endif

    reaktor_app_size(app, &win_w, &win_h);
    reaktor_app_due(app, win_w, win_h);
    if (!app->dirty && !app->redraw_always)
        return SDL_APP_CONTINUE;
    if (reaktor_app_frame(app, win_w, win_h, launch_title(), main_page, NULL))
        return SDL_APP_SUCCESS;

    if (!app->first_frame_done) {
        app->first_frame_done = 1;
        hint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
        reaktor_release_free_memory();
    }
    return SDL_APP_CONTINUE;
}

static void
app_quit(void *appstate, SDL_AppResult result)
{
    App *app = (App *)appstate;

    (void)result;
    SDL_SetAtomicInt(&g_running, 0);
    if (!app) {
        reaktor_process_end();
        return;
    }
    run_stop(app);
    if (!SDL_GetAtomicInt(&g_exit_code) && (app->shot_failed || app->dump_failed))
        SDL_SetAtomicInt(&g_exit_code, app->shot_failed && app->dump_failed ? 4
                                       : app->shot_failed ? 2 : 3);

    if (app->ctx) nk_input_end(app->ctx);
    reaktor_app_input_free(app);
    field_undo_clear(app);
    reaktor_layout_free(&app->lay);
    reaktor_toast_clear();
    reaktor_floaters_clear(app);
    reaktor_windows_close_all();
    reaktor_tray_close(app);
    reaktor_style_shutdown();
    if (app->cur_default) SDL_DestroyCursor(app->cur_default);
    if (app->cur_pointer) SDL_DestroyCursor(app->cur_pointer);
    if (app->cur_text)    SDL_DestroyCursor(app->cur_text);
    if (app->ren) img_cache_free(app);
    if (app->ctx) nk_sdl_shutdown(app->ctx);
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    reaktor_anim_destroy(app->anim);
    app->anim = NULL;
    /* The picker's thread still writes into it. */
    if (!app->file_pending) SDL_free(app);
    g_app = NULL;
    reaktor_signal_unwatch();
    reaktor_process_end();
}

static void
launch_copy(const reaktor_launch *l)
{
    char fallback[32];
    int i;

    g_launch = *l;
    g_launch.name         = own(l->name);
    g_launch.id           = own(l->id);
    g_launch.version      = own(l->version);
    g_launch.window.title = own(l->window.title);
    g_confirm   = l->confirm_close ? SDL_strdup(l->confirm_close) : NULL;
    g_icon_name = reaktor_asset_name(&l->window.icon, "app/icon.svg");
    g_font      = reaktor_asset_name(&l->font, "app/font");
    g_font_bold = reaktor_asset_name(&l->font_bold, "app/font-bold");
    for (i = 0; i < REAKTOR_LAYER_MAX; i++) {
        const char *n;

        SDL_snprintf(fallback, sizeof(fallback), "app/%d.css", i);
        if ((n = reaktor_asset_name(&l->css[i], fallback))) g_css[g_css_n++] = n;
        SDL_snprintf(fallback, sizeof(fallback), "app/fallback%d", i);
        if ((n = reaktor_asset_name(&l->font_fallbacks[i], fallback)))
            g_fallbacks[g_fallback_n++] = n;
        if (l->icon_dirs[i]) g_icon_dirs[g_icon_dir_n++] = own(l->icon_dirs[i]);
    }
    for (i = 0; i < l->asset_count; i++)
        if (l->assets[i].data &&
            !reaktor_asset_register(own(l->assets[i].name), l->assets[i].data,
                                    l->assets[i].size))
            SDL_Log("too many registered assets; %s is not one", l->assets[i].name);
}

int
reaktor_launch_app(int argc, char **argv, const reaktor_launch *l)
{
    static int launched;
    int rc, code;

    if (!l || !l->page) {
        SDL_Log("launchApp: a page is required");
        return 1;
    }
    if (launched++) {
        SDL_Log("launchApp: once per process");
        return 1;
    }
    launch_copy(l);
    rc = SDL_EnterAppMainCallbacks(argc, argv, app_init, app_iterate,
                                   app_event, app_quit);
    code = SDL_GetAtomicInt(&g_exit_code);
#ifndef __EMSCRIPTEN__
    SDL_free(g_title);
    SDL_free(g_pending_title);
    SDL_free(g_confirm);
    g_title = g_pending_title = g_confirm = NULL;
    reaktor_asset_forget();
    forget_owned();
#endif
    return code ? code : rc;
}

void
reaktor_quit(App *app, int exit_code)
{
    (void)app;
#ifdef __EMSCRIPTEN__
    (void)exit_code;
    SDL_Log("quit is ignored on the web");
#else
    reaktor_hard_quit(exit_code);
#endif
}

void
reaktor_request_quit(App *app)
{
#ifdef __EMSCRIPTEN__
    (void)app;
    SDL_Log("quit is ignored on the web");
#else
    (void)app;
    push_quit(REAKTOR_QUIT_APP);
#endif
}

void
reaktor_wake(App *app)
{
    App *to = app ? app : g_app;

    if (!to || !SDL_GetAtomicInt(&g_running)) return;
    /* A lost wake would hold back every later one. */
    if (SDL_CompareAndSwapAtomicInt(&to->wake, 0, 1) && !wake_app(to))
        SDL_SetAtomicInt(&to->wake, 0);
}

void
reaktor_set_theme(App *app, reaktor_theme theme)
{
    App *m = g_app ? g_app : app;

    if (m->theme_pending ? m->theme_pending == (int)theme + 1
                         : m->theme_mode == (int)theme)
        return;
    m->theme_pending = (int)theme + 1;
    reaktor_wake(m);
}

static int
same_asset(const reaktor_asset *a, const reaktor_asset *b)
{
    return SDL_strcmp(a->path ? a->path : "", b->path ? b->path : "") == 0 &&
           SDL_strcmp(a->name ? a->name : "", b->name ? b->name : "") == 0 &&
           a->data == b->data && a->size == b->size;
}

void
reaktor_set_css(App *app, const reaktor_asset *css, int count)
{
    int i;

    if (!css || count < 0) count = 0;
    if (count > REAKTOR_LAYER_MAX) count = REAKTOR_LAYER_MAX;
    if (count == g_css_last_n) {
        for (i = 0; i < count && same_asset(&css[i], &g_css_last[i]); i++) {}
        if (i == count) return;
    }
    g_css_last_n = count;
    for (i = 0; i < count; i++) {
        g_css_last[i] = css[i];
        g_css_last[i].path = own(css[i].path);
        g_css_last[i].name = own(css[i].name);
    }
    g_css_pending = 1;
    reaktor_wake(app);
}

void
reaktor_set_title(App *app, const char *title)
{
    const char *now = g_pending_title ? g_pending_title : launch_title();

    if (app && app->secondary) {
        reaktor_windows_retitle(app, title);
        return;
    }
    if (!title || SDL_strcmp(title, now) == 0) return;
    SDL_free(g_pending_title);
    g_pending_title = SDL_strdup(title);
    reaktor_wake(app);
}

void
reaktor_set_confirm_close(App *app, const char *question)
{
    char *q = question ? SDL_strdup(question) : NULL;

    SDL_free(g_confirm);
    g_confirm = q;
    if (!q) reaktor_floater_close(g_app ? g_app : app, g_confirm_id);
}

App *
reaktor_main_app(void)
{
    return g_app;
}

struct nka_context *
reaktor_anim(App *app)
{
    if (app && !app->anim) app->anim = reaktor_anim_create();
    return app ? app->anim : NULL;
}

const char *
reaktor_launch_icon(void)
{
    return g_icon_name ? g_icon_name : REAKTOR_MARK;
}

void *
reaktor_user(App *app)
{
    (void)app;
    return g_launch.user;
}

SDL_Window *
reaktor_sdl_window(App *app)
{
    return app ? app->win : NULL;
}

SDL_Renderer *
reaktor_sdl_renderer(App *app)
{
    return app ? app->ren : NULL;
}

int
reaktor_dark(App *app)
{
    return app && app->dark;
}

reaktor_theme
reaktor_get_theme(App *app)
{
    return app ? (reaktor_theme)app->theme_mode : REAKTOR_THEME_SYSTEM;
}

const char *
reaktor_lang_pref(App *app)
{
    App *m = reaktor_main_app();

    return app && m ? m->lang_pref : NULL;
}

void
reaktor_set_borderless(App *app, int on)
{
    if (!app || !app->win || app->secondary || !on == !app->borderless) return;
#ifdef __APPLE__
    if (app->borderless_lock_frames == 0) {
        app->borderless_pending = 1;
        app->dirty = 1;
    }
#else
    app->borderless = on != 0;
    SDL_SetWindowBordered(app->win, app->borderless ? false : true);
    SDL_SetWindowHitTest(app->win,
                         app->borderless ? g_launch.window.hit_test : NULL,
                         app->borderless ? app : NULL);
    app->dirty = 1;
#endif
}

int
reaktor_borderless(App *app)
{
    return app && app->borderless;
}
