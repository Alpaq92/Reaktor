#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "internal.h"
#include "declare.h"
#include "anim.h"
#include "locale.h"
#include "sample.h"
#ifdef __EMSCRIPTEN__
#include "keyboard_web.h"
#endif

static int
effective_dark(const App *app)
{
    if (app->theme_mode == THEME_LIGHT) return 0;
    if (app->theme_mode == THEME_DARK)  return 1;
    return reaktor_prefers_dark() > 0;
}

size_t reaktor_rss[RSS_STEPS];
size_t reaktor_priv[RSS_STEPS];

void
reaktor_rss_mark(int step)
{
    reaktor_process_memory(&reaktor_rss[step], &reaktor_priv[step]);
}

static int
take_flags(App *app, int argc, char **argv)
{
    int i, n = 0;

    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (v && SDL_strcmp(a, "--shot") == 0)       { app->shot_path = v; i++; }
        else if (v && SDL_strcmp(a, "--a11y-dump") == 0) { app->dump_path = v; i++; }
        else if (v && SDL_strcmp(a, "--renderer") == 0) { app->renderer_pref = v; i++; }
        else if (v && SDL_strcmp(a, "--lang") == 0)     { app->lang_pref = v; i++; }
        else if (v && SDL_strcmp(a, "--font-fallback") == 0) { reaktor_text_add_fallback(v); i++; }
        else if (v && SDL_strcmp(a, "--theme") == 0) {
            if (SDL_strcmp(v, "light") == 0)     app->theme_mode = THEME_LIGHT;
            else if (SDL_strcmp(v, "dark") == 0) app->theme_mode = THEME_DARK;
            else                                 app->theme_mode = THEME_SYSTEM;
            i++;
        } else {
            argv[n++] = argv[i];
        }
    }
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
    char pal[1024], core[1024], own[1024], user[1024];
    const char *sheets[SHEET_MAX];
    int n = 0;
    unsigned char c[4];
    Uint64 t0, t1;

    app->dark = effective_dark(app);

    if (!reaktor_path(pal, sizeof(pal), theme_sheet(app->dark)) ||
        !reaktor_path(core, sizeof(core), CORE_SHEET) ||
        !reaktor_path(own, sizeof(own), APP_SHEET)) {
        SDL_Log("could not resolve the stylesheet sources");
        return;
    }
    sheets[n++] = pal;
    sheets[n++] = core;
    sheets[n++] = own;

    if (!app->css_override_off) {
        const char *want = USER_SHEET;
        SDL_IOStream *f;

        SDL_strlcpy(user, want, sizeof(user));
        f = SDL_IOFromFile(user, "rb");
        if (!f && reaktor_path(user, sizeof(user), want))
            f = SDL_IOFromFile(user, "rb");
        if (f) {
            SDL_CloseIO(f);
            sheets[n++] = user;
        }
    }
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

    reaktor_window_set_dark(
        SDL_GetPointerProperty(SDL_GetWindowProperties(app->win),
                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL),
        app->dark);

    img_cache_clear(app);
    apply_widget_style(app);

    app->clear   = app->page;
    app->dirty   = 1;
}

#ifdef __EMSCRIPTEN__
static void
web_page_size(int *w, int *h)
{
    int cw = EM_ASM_INT({ return window.innerWidth | 0; });
    int ch = EM_ASM_INT({ return window.innerHeight | 0; });
    if (cw > 64 && ch > 64) { *w = cw; *h = ch; }
}
static void
web_keys_woke(void)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    SDL_PushEvent(&e);
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

static reaktor_window_spec g_win;

SDL_AppResult
SDL_AppInit(void **appstate, int argc, char *argv[])
{
    App *app;

    SDL_zero(g_win);
    sample_window(&g_win);

    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");

    reaktor_rss_mark(RSS_ENTRY);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    reaktor_rss_mark(RSS_SDL);

    app = (App *)SDL_calloc(1, sizeof(App));
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->theme_mode = THEME_SYSTEM;
    argc = take_flags(app, argc, argv);

    if (app->renderer_pref && SDL_strcmp(app->renderer_pref, "software") != 0) {
        SDL_strlcpy(app->render_mode, app->renderer_pref,
                    sizeof(app->render_mode));
        if (SDL_strcmp(app->renderer_pref, "auto") != 0)
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, app->renderer_pref);
    } else {
        SDL_strlcpy(app->render_mode, "auto: software",
                    sizeof(app->render_mode));
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }

#ifdef __EMSCRIPTEN__
    app->borderless = 0;
#else
    app->borderless = g_win.borderless != 0;
#endif

    {
        float pre = reaktor_dpi_query_scale(NULL);
        int win_w = (int)((g_win.w > 0 ? g_win.w : WINDOW_WIDTH) * pre + 0.5f);
        int win_h = (int)((g_win.h > 0 ? g_win.h : WINDOW_HEIGHT) * pre + 0.5f);

#ifdef __EMSCRIPTEN__
        web_page_size(&win_w, &win_h);
#endif
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE |
                                SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                (app->borderless ? SDL_WINDOW_BORDERLESS : 0);

        if (!SDL_CreateWindowAndRenderer(g_win.title ? g_win.title : "Reaktor",
                                    win_w, win_h, flags,
                                         &app->win, &app->ren)) {
            SDL_Log("renderer '%s' would not start (%s); falling back",
                    app->render_mode, SDL_GetError());
            if (app->win) { SDL_DestroyWindow(app->win); app->win = NULL; }
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, NULL);
            SDL_strlcpy(app->render_mode, "auto", sizeof(app->render_mode));
            if (!SDL_CreateWindowAndRenderer(g_win.title ? g_win.title : "Reaktor",
                                    win_w, win_h, flags,
                                             &app->win, &app->ren)) {
                SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
                return SDL_APP_FAILURE;
            }
        }

        if (renderer_is_software(app->ren)) {
            if (SDL_strcmp(app->render_mode, "auto") == 0) {
                SDL_Renderer *sw;

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
        app->sw_noaa = 0;
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

    if (app->borderless) SDL_SetWindowHitTest(app->win, window_hit_test, app);

    reaktor_rss_mark(RSS_WINDOW);

    set_window_icon(app->win);
    reaktor_set_scale(reaktor_dpi_query_scale(app->win));
    apply_render_scale(app);
    reaktor_rss_mark(RSS_ICON);

    app->ctx = nk_sdl_init(app->win, app->ren, nk_sdl_allocator());
    if (!app->ctx) return SDL_APP_FAILURE;
    reaktor_rss_mark(RSS_NUKLEAR);

    /* Before the atlas, which bakes what the catalogs need. */
    reaktor_locale_start(app->lang_pref);
    rebuild_font(app);
    reaktor_rss_mark(RSS_FONT);

    app->wake_event = SDL_RegisterEvents(1);


    app->cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->cur_text    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);

    app->css_override_off = 1;

    sample_args(app, argc, argv);

    nk_textedit_init_fixed(&app->edit, app->edit_buf, sizeof(app->edit_buf));

    load_theme(app);
    reaktor_rss_mark(RSS_STYLE);

    app->dirty = 1;

#ifdef __EMSCRIPTEN__
    reaktor_web_keys_init(web_keys_woke);
#endif
    nk_input_begin(app->ctx);
    return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = (App *)appstate;

    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;

    if (app) {
        if (app->wake_event && event->type == app->wake_event) app->dirty = 1;

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
            float mx = event->motion.x, my = event->motion.y;
            int i, over = -1;

            if (app->dragging) { app->drag_moved = 1; app->dirty = 1; }

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
                if (over_top >= 0) over = over_top;
            }
            {
                struct nk_rect nr = over >= 0 ? app->hot[over].r
                                              : nk_rect(0, 0, 0, 0);
                int had = app->hot_last >= 0, has = over >= 0;
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
                    hover_redraw(app);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            app->focus_visible = 0;
            app->dragging = 1;
            if (app->field_rect_valid) {
                struct nk_rect r = app->field_rect;
                float bx = event->button.x, by = event->button.y;
                app->drag_in_field = bx >= r.x && bx <= r.x + r.w &&
                                     by >= r.y && by <= r.y + r.h;
            }
            SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->drag_rate);
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

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            app->hot_last = -1;
            app->hot_last_repaint = 0;
            app->want_cursor = 0;
            break;

        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            if (app->theme_mode == THEME_SYSTEM) load_theme(app);
            break;

        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            reaktor_set_scale(reaktor_dpi_query_scale(app->win));
            apply_render_scale(app);
            rebuild_font(app);
            break;

        case SDL_EVENT_KEY_DOWN:
            if (sample_key(app, event)) break;
            if (focus_key(app, event)) {
                return SDL_APP_CONTINUE;
            }
            break;

        case SDL_EVENT_USER:
            app->dirty = 1;
            break;

        default:
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

#ifdef __EMSCRIPTEN__
        /* The element's to deliver, or keypress types every character twice. */
        if (event->type == SDL_EVENT_TEXT_INPUT && reaktor_web_keys_holds())
            return SDL_APP_CONTINUE;
#endif
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

    if (app->theme_pending) {
        app->theme_mode    = app->theme_pending - 1;
        app->theme_pending = 0;
        load_theme(app);
    }

#ifdef __APPLE__
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
        SDL_SyncWindow(app->win);
        SDL_GetWindowSize(app->win, &w, &h);
        SDL_SetWindowSize(app->win, w, h);
        app->ctl_n = 0;
        app->dirty = 1;
        app->borderless_lock_frames = 3;
    }
#endif

    SDL_GetWindowSizeInPixels(app->win, &win_w, &win_h);
    {
        float s = reaktor_scale();

        if (s > 0.0f) {
            win_w = (int)(win_w / s + 0.5f);
            win_h = (int)(win_h / s + 0.5f);
        }
    }
    if (win_w != app->laid_w || win_h != app->laid_h) app->dirty = 1;

    if (app->hover_pending &&
        SDL_GetTicks() - app->last_draw_ms >= g_hover_gap_ms) {
        app->hover_pending = 0;
        app->dirty = 1;
        if (!app->dragging) app->restore_rate = 1;
    }

    if (app->dragging &&
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

    if (app->want_cursor != app->cur_shown) {
        SDL_Cursor *c = app->want_cursor == 1 ? app->cur_pointer
                      : app->want_cursor == 2 ? app->cur_text
                      : app->cur_default;
        if (c) SDL_SetCursor(c);
        app->cur_shown = app->want_cursor;
    }

    if (!app->dirty && !app->redraw_always)
        return SDL_APP_CONTINUE;
    app->laid_w = win_w;
    app->laid_h = win_h;
    app->hot_n = 0;
    app->editing = 0;

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

        }
    }

    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(app->clear));

    Uint64 t_build0 = SDL_GetPerformanceCounter();

    reaktor_a11y_platform_drain();

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
#ifdef __EMSCRIPTEN__
    {
        int rune;

        while ((rune = reaktor_web_keys_take()) > 0)
            nk_input_unicode(ctx, (nk_rune)rune);
    }
#endif
    nk_input_end(ctx);
    ctx->style.text.color = app->text;

    sample_file_taken(app);

    app->focus_seen = 0;
    reaktor_a11y_begin(&app->a11y,
                       g_win.title ? g_win.title : "Reaktor",
                       nk_rect(0, 0, (float)win_w, (float)win_h));
    reaktor_frame_begin(app, ctx, nk_rect(0, 0, (float)win_w, (float)win_h));

    if (nk_begin(ctx, "page", nk_rect(0, 0, (float)win_w, (float)win_h),
                 NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR)) {
        nk_style_pop_vec2(ctx);
        page_shell(app, ctx, win_w, win_h);
        if (app->focus_visible && app->focus_seen) focus_ring(app, ctx);
        app->focus_step = 0;
        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    }
    nk_end(ctx);
    app->stop_editing = 0;

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

    reaktor_frame_end();

    reaktor_a11y_end(&app->a11y);
    focus_resolve(app);
    reaktor_a11y_platform_push(&app->a11y, app->focus_id);
    a11y_dump_once(app);

    {
        int n = 0;
        const reaktor_a11y_change *c = reaktor_a11y_changes(&app->a11y, &n);
        reaktor_anim_evict(c, n);
    }
    if (reaktor_anim_tick(app->frame_gap_ms) > 0) {
        SDL_Event e;

        app->dirty = 1;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
    }

    nk_sdl_update_TextInput(ctx);
#ifdef __EMSCRIPTEN__
    /* After the frame, when a field has said whether it is being edited. */
    reaktor_web_keys_wants(app->editing);
#endif
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
        {
            enum nk_anti_aliasing fill, line;
            fill = line = app->aa ? NK_ANTI_ALIASING_ON : NK_ANTI_ALIASING_OFF;
            if (app->renderer_is_sw) {
                fill = NK_ANTI_ALIASING_OFF;
                if (app->sw_noaa) line = NK_ANTI_ALIASING_OFF;
            }
            reaktor_text_prepare(ctx, app->ren);
            nk_sdl_render_ex(ctx, fill, line);
        }
        t_present0 = SDL_GetPerformanceCounter();
        {
            const char *shot = app->shot_path;
            static int shot_done;

            if (shot && *shot && !shot_done) {
                if (reaktor_frame_settled()) {
                    SDL_Surface *sh = SDL_RenderReadPixels(app->ren, NULL);

                    shot_done = 1;
                    if (sh) {
                        if (!SDL_SaveBMP(sh, shot))
                            SDL_Log("screenshot: %s", SDL_GetError());
                        SDL_DestroySurface(sh);
                    }
                    app->want_quit = 1;
                } else {
                    SDL_Event e;

                    app->dirty = 1;
                    SDL_zero(e);
                    e.type = SDL_EVENT_USER;
                    SDL_PushEvent(&e);
                }
            }
        }
        SDL_RenderPresent(app->ren);
        t_end = SDL_GetPerformanceCounter();

        app->last_draw_ms    = SDL_GetTicks();
        if (app->first_frame_done) calibrate_hover_gap(app);
        app->build_ms_x100   = (int)(100000.0 * (double)(t_render0 - t_build0) / (double)f);
        app->render_ms_x100  = (int)(100000.0 * (double)(t_present0 - t_render0) / (double)f);
        app->present_ms_x100 = (int)(100000.0 * (double)(t_end - t_present0) / (double)f);
    }

    nk_input_begin(ctx);

    app->dirty = 0;
    app->drag_moved = 0;
    if (app->restore_rate > 0) {
        if (--app->restore_rate > 0) app->dirty = 1;
        else SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
    }
    if (app->want_quit) return SDL_APP_SUCCESS;

    if (!app->first_frame_done) {
        app->first_frame_done = 1;
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, app->frame_rate);
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
