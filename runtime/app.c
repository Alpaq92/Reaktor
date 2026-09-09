/* app.c - Reaktor's entry point: start up, take events, draw, shut down.
 *
 * Nuklear draws and lays out the widgets; tiny.css says what they look like.
 * Before each widget the computed values behind its selector are pushed into
 * nk_style and popped after - core/style_map.c is that seam.
 *
 * SDL3's callback model rather than a while(): a browser tab cannot be
 * blocked, so under Emscripten SDL must hand control back each frame. One
 * source then serves Windows, macOS, Linux, the BSDs and WASM.
 *
 * Lifted out of main.c unchanged. The entry point comes with it, because this
 * is the file that has the callbacks SDL calls.
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "internal.h"
#include "sample.h"

int
env_int(const char *name, int fallback)
{
    const char *v = SDL_getenv(name);
    return (v && *v) ? SDL_atoi(v) : fallback;
}

/* The effective scheme. reaktor_prefers_dark() returns -1 when the platform
 * will not say, and an unknown answer is treated as light. */
static int
effective_dark(const App *app)
{
    if (app->theme_mode == THEME_LIGHT) return 0;
    if (app->theme_mode == THEME_DARK)  return 1;
    return reaktor_prefers_dark() > 0;
}

/* Reloads the stylesheets and re-reads the surfaces this file paints itself.
 * Cheap enough to do on a click: the whole set is ~7 KB. */
/* Defined further down, with the rest of the CSS seam. */

void
load_theme(App *app)
{
    char pal[1024], core[1024];
    const char *sheets[SHEET_COUNT];
    unsigned char c[4];
    Uint64 t0, t1;

    app->dark = effective_dark(app);

    if (!reaktor_path(pal, sizeof(pal), theme_sheet(app->dark)) ||
        !reaktor_path(core, sizeof(core), CORE_SHEET)) {
        SDL_Log("could not resolve the tiny.css sources");
        return;
    }
    sheets[0] = pal;
    sheets[1] = core;

    t0 = SDL_GetPerformanceCounter();
    if (!reaktor_style_init(sheets, SHEET_COUNT))
        SDL_Log("stylesheets failed to load; Nuklear defaults apply");
    t1 = SDL_GetPerformanceCounter();
    app->style_ms_x100 = (int)(100000.0 * (double)(t1 - t0) /
                               (double)SDL_GetPerformanceFrequency());

    /* The window and the card are painted by this file, not by a widget, so
     * their colours are read from the same :root the rules use. */
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

    /* The desktop's own title bar, so a pinned scheme is not contradicted by
     * the frame around it. */
    reaktor_window_set_dark(
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
int renderer_is_software(SDL_Renderer *ren) { (void)ren; return 0; }
#endif

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
 * measurably costs this process - see calibrate_hover_gap.
 * REAKTOR_HOVER_GAP_MS pins it instead, which is how the trade-off was
 * measured in the first place. */
Uint64 g_hover_gap_ms = HOVER_GAP_MS;   /* read by reaktor_diagnostics */
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
    now = reaktor_process_cpu_ms();
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
        /* REAKTOR_RENDERER takes "auto" (the default), "cpu", "gpu", or the
         * name of any driver SDL has compiled in - on Windows that is
         * direct3d11, opengl, opengles2 or software; elsewhere whatever the
         * platform builds. "list" prints them and exits, so the names do not
         * have to be guessed.
         *
         * "auto" is SDL's software rasteriser, on every platform - Windows,
         * macOS, Linux, the BSDs and the web alike. It is not SDL's own first
         * choice, and deliberately not:
         *
         *   - This app draws nothing at rest and builds a frame only when
         *     something changed, so a GPU buys it nothing it can measure. A
         *     drawn frame is 7-12 ms of CPU, once, on a UI that draws a
         *     handful of frames a second at its busiest.
         *   - What a GPU path does cost is unconditional. On the reference
         *     Windows machine direct3d11 lands on WARP and costs six times
         *     SDL's software renderer - 78.7 ms a frame against 13.1 - and
         *     on a Linux desktop with no GPU the opengl driver is Mesa's
         *     llvmpipe, which rasterises in software anyway and maps 53 MB of
         *     libLLVM plus 10 MB of libgallium to do it. Both were the
         *     default here once, and both were paying a GPU's price for a
         *     software renderer's result.
         *   - One rasteriser on every platform is also one set of pixels to
         *     reason about. The feathering rules in nk_sdl_render_ex are
         *     written against the software renderer; a GPU path is the one
         *     that draws the page differently.
         *
         * "gpu" pins the first non-software driver and keeps it, which is how
         * the two were measured against each other, and a driver by name does
         * the same for one in particular. Nothing about the GPU paths is
         * removed - they are no longer what you get without asking. */
        const char *mode = SDL_getenv("REAKTOR_RENDERER");
        int i, n = SDL_GetNumRenderDrivers();

        if (!mode || !*mode) mode = "auto";
        /* What Diagnostics shows. "auto" alone said nothing about which
         * renderer it had settled on, which is the first thing anyone asks. */
        if (SDL_strcmp(mode, "auto") == 0)
            SDL_strlcpy(app->render_mode, "auto: software",
                        sizeof(app->render_mode));
        else
            SDL_strlcpy(app->render_mode, mode, sizeof(app->render_mode));

        if (SDL_strcmp(mode, "list") == 0) {
            for (i = 0; i < n; i++) SDL_Log("%s", SDL_GetRenderDriver(i));
            return SDL_APP_SUCCESS;
        }
        if (SDL_strcmp(mode, "cpu") == 0 || SDL_strcmp(mode, "auto") == 0) {
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
                SDL_Log("REAKTOR_RENDERER=%s is not a driver this build has; "
                        "try REAKTOR_RENDERER=list", mode);
                SDL_strlcpy(app->render_mode, "auto", sizeof(app->render_mode));
            }
        }
    }

    /* Borderless by default, so the titlebar follows the stylesheet like
     * everything else. REAKTOR_BORDERLESS=0 restores the desktop's frame,
     * which is worth keeping - a custom titlebar gives up what the platform
     * does for free. */
    /* Off in a browser: no desktop frame to replace, the canvas is the whole
     * window, and a titlebar inside it could neither move nor resize. */
#ifdef __EMSCRIPTEN__
    app->borderless = env_int("REAKTOR_BORDERLESS", 0) != 0;
#else
    app->borderless = env_int("REAKTOR_BORDERLESS", 1) != 0;
#endif

    {
        /* On a real HiDPI display SDL_WINDOW_HIGH_PIXEL_DENSITY gives this
         * window scale-times as many pixels, which is exactly the room the
         * same logical layout needs. A forced REAKTOR_SCALE has no such
         * display behind it, so the window is asked for that many pixels here
         * instead - otherwise the simulation draws a scaled UI into an
         * unscaled window and simply clips it. reaktor_dpi_query_scale(NULL)
         * is the override, or 1.0 when unset. */
        float pre = reaktor_dpi_query_scale(NULL);
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

        if (!SDL_CreateWindowAndRenderer("Reaktor", win_w, win_h, flags,
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
            if (!SDL_CreateWindowAndRenderer("Reaktor", win_w, win_h, flags,
                                             &app->win, &app->ren)) {
                SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
                return SDL_APP_FAILURE;
            }
        }

        /* A GPU driver that turned out to have no GPU behind it: on Windows
         * Direct3D has landed on WARP, Microsoft's software implementation,
         * which costs six times what SDL's own software renderer costs here -
         * 78.7 ms of CPU a frame against 13.1.
         *
         * `auto` no longer reaches this: it asks for the software renderer
         * outright, on every platform, so there is nothing to swap. What is
         * left is the report for the two modes that do ask for a GPU - `gpu`
         * and a driver by name - which are kept as asked for and labelled
         * with what they actually got. The swap itself stays because `auto`
         * can still land here if the software renderer failed to create and
         * the fallback above picked SDL's own default. */
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
        app->sw_noaa = env_int("REAKTOR_SW_NOAA", 0) != 0;
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
    reaktor_a11y_platform_init(reader_activate, reader_focus, app);

    /* Without this the loop presents as fast as the GPU allows, which was
     * half of the idle CPU cost. */
    {
        int want = env_int("REAKTOR_VSYNC", 1);
        app->vsync_on = SDL_SetRenderVSync(app->ren, want ? 1 : 0) ? want : 0;
    }
    app->aa = env_int("REAKTOR_AA", 1);
    app->redraw_always = SDL_getenv("REAKTOR_REDRAW") &&
                         SDL_strcmp(SDL_getenv("REAKTOR_REDRAW"),
                                    "always") == 0;

    /* How often SDL calls SDL_AppIterate. The loop used to spin and sleep 2 ms
     * whenever the frame was clean - 500 wake-ups a second to find nothing to
     * do. "waitevent" blocks until an event arrives. REAKTOR_FRAME_RATE
     * overrides with a cap, or 0 for uncapped as REAKTOR_REDRAW=always
     * needs. */
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
    /* needs the window */
    reaktor_set_scale(reaktor_dpi_query_scale(app->win));
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

    if (SDL_getenv("REAKTOR_HOVER_GAP_MS")) {
        int gap = env_int("REAKTOR_HOVER_GAP_MS", (int)HOVER_GAP_MS);
        if (gap >= 0 && gap <= 1000) {
            g_hover_gap_ms   = (Uint64)gap;
            g_hover_gap_pinned = 1;
        }
    }

    app->cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->cur_text    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);

    /* Pinned from the environment, same reason as REAKTOR_TAB: a screenshot of
     * the light palette should not depend on clicking a link. The links on the
     * strip still change it. */
    app->theme_mode = env_int("REAKTOR_THEME", THEME_SYSTEM);
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
            reaktor_set_scale(reaktor_dpi_query_scale(app->win));
            apply_render_scale(app);
            rebuild_font(app);
            break;

        case SDL_EVENT_KEY_DOWN:
            /* The application's own bindings first, then focus traversal -
             * which is the order NAppGUI settled on too, and the reason a
             * focused text field keeps plain Tab: nothing in the table claims
             * an unmodified Tab. What the bindings are is samples/shortcuts.c;
             * the order is the only part of it that is not the app's. */
            if (sample_key(app, event)) break;
            if (focus_key(app, event)) {
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

#ifdef __APPLE__
    /* See the note where borderless_pending is set. Applied here, outside
     * the frame's event dispatch, so the phantom click SDL delivers when
     * the styleMask changes is enqueued for a later frame - which
     * lock_frames swallows. */
    if (app->borderless_lock_frames > 0)
        app->borderless_lock_frames--;
    if (app->borderless_pending) {
        int w = 0, h = 0;
        app->borderless_pending = 0;
        app->borderless = !app->borderless;
        SDL_SetWindowBordered(app->win, app->borderless ? false : true);
        SDL_SetWindowHitTest(app->win,
                             app->borderless ? window_hit_test : NULL,
                             app->borderless ? app : NULL);
        /* macOS caches the old drawable across a styleMask change and the
         * new draw does not fully invalidate it - the pixels from before
         * the flip linger even though titlebar() is not called any more.
         * SDL_SyncWindow blocks until the pending window changes are
         * applied by the WindowServer; asking SDL to set the size to the
         * *current* size then forces a fresh SDL_EVENT_WINDOW_RESIZED,
         * which is what a SDL_Renderer takes as its cue to rebuild its
         * backing texture. Together those two calls make the flip visible
         * on the frame that draws it, rather than several frames later. */
        SDL_SyncWindow(app->win);
        SDL_GetWindowSize(app->win, &w, &h);
        SDL_SetWindowSize(app->win, w, h);
        app->ctl_n = 0;
        app->dirty = 1;
        app->borderless_lock_frames = 3;
    }
#endif

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
            if (SDL_getenv("REAKTOR_STATS")) {
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
    reaktor_a11y_platform_drain();

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
    sample_file_taken(app);

    /* Opened around the same region nk_begin gets, so the window node's bounds
     * are the window's. Closed after nk_end, below. */
    app->focus_seen = 0;
    reaktor_a11y_begin(&app->a11y, "Reaktor",
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
    reaktor_a11y_end(&app->a11y);
    focus_resolve(app);
    reaktor_a11y_platform_push(&app->a11y, app->focus_id);
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
         * grade. REAKTOR_SW_NOAA=1 drops both, which is 6% cheaper. */
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
        /* First frame's transient allocations - font atlas bake buffers,
         * plutosvg parse buffers, libcss's rule-tree scratch - are freed by
         * this point but macOS's libmalloc keeps their pages cached. Ask
         * for them back once here; no-op elsewhere. Called once, not per
         * frame: pressure relief walks every zone and is not free. */
        reaktor_release_free_memory();
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
    reaktor_style_shutdown();
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
