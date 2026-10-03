#include "internal.h"
#include "declare.h"
#include "reaktor/launch.h"

#define WINDOW_MAX 8

typedef struct rwin {
    int                 id;
    App                *app;
    reaktor_window      spec;
    char               *title;
    int                 closing;
} rwin;

static rwin g_win[WINDOW_MAX];
static int  g_win_n, g_win_next = 1;

static rwin *
by_id(int id)
{
    int i;

    for (i = 0; i < g_win_n; i++)
        if (g_win[i].id == id) return &g_win[i];
    return NULL;
}

static void
take_look(App *a, const App *m)
{
    a->theme_mode = m->theme_mode;
    a->dark       = m->dark;
    a->page       = m->page;
    a->card_bg    = m->card_bg;
    a->text       = m->text;
    a->clear      = m->clear;
    SDL_memcpy(a->icon_hex, m->icon_hex, sizeof a->icon_hex);
    SDL_memcpy(a->accent_hex, m->accent_hex, sizeof a->accent_hex);
}

static void
teardown(App *a)
{
    if (a->win) SDL_SetWindowModal(a->win, false);
    img_cache_clear(a);
    if (a->ctx) nk_sdl_shutdown(a->ctx);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_free(a->glyphs);
    SDL_free(a);
}

static void
shut(int i, int tell)
{
    rwin w = g_win[i];
    App *m = reaktor_main_app();

    SDL_memmove(&g_win[i], &g_win[i + 1], (size_t)(g_win_n - i - 1) * sizeof *g_win);
    g_win_n--;
    teardown(w.app);
    SDL_free(w.title);
    if (tell && w.spec.closed) w.spec.closed(m, w.spec.user);
    if (m) {
        int k;

        /* Its cursor would outlive it; each window sets its own again. */
        SDL_SetCursor(m->cur_default);
        m->cur_shown = 0;
        m->dirty = 1;
        for (k = 0; k < g_win_n; k++) g_win[k].app->cur_shown = 0;
    }
}

int
reaktor_window_open(App *app, const reaktor_window *spec)
{
#ifdef __EMSCRIPTEN__
    (void)app; (void)spec;
    SDL_Log("a page opens no windows of its own");
    return 0;
#else
    App  *m = reaktor_main_app(), *a;
    rwin *w;
    int   i;

    (void)app;
    if (!m || !spec || !spec->page || g_win_n == WINDOW_MAX) return 0;
    if (!(a = (App *)SDL_calloc(1, sizeof *a))) return 0;
    a->secondary = 1;
    a->hot_last  = -1;
    a->win = SDL_CreateWindow(spec->window.title ? spec->window.title : "",
                              reaktor_px(spec->window.w > 0 ? (float)spec->window.w : 420.0f),
                              reaktor_px(spec->window.h > 0 ? (float)spec->window.h : 280.0f),
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                              SDL_WINDOW_HIDDEN |
                              (spec->window.borderless ? SDL_WINDOW_BORDERLESS : 0));
    if (a->win && (spec->window.min_w > 0 || spec->window.min_h > 0))
        SDL_SetWindowMinimumSize(a->win, reaktor_px((float)spec->window.min_w),
                                 reaktor_px((float)spec->window.min_h));
    if (a->win && spec->window.borderless && spec->window.hit_test)
        SDL_SetWindowHitTest(a->win, spec->window.hit_test, a);
    if (a->win && spec->modal) {
        SDL_SetWindowParent(a->win, m->win);
        SDL_SetWindowModal(a->win, true);
    }
    if (a->win) a->ren = SDL_CreateRenderer(a->win, SDL_GetRendererName(m->ren));
    if (a->ren) a->ctx = nk_sdl_init(a->win, a->ren, nk_sdl_allocator());
    if (!a->ctx) {
        SDL_Log("window: %s", SDL_GetError());
        teardown(a);
        return 0;
    }
    SDL_SetRenderVSync(a->ren, 1);
    a->renderer_is_sw = SDL_strcmp(SDL_GetRendererName(a->ren), "software") == 0;
    a->sw_noaa        = m->sw_noaa;
    a->aa             = 1;
    a->font_face      = m->font_face;
    a->font_face_bold = m->font_face_bold;
    for (i = 0; i < m->icon_dir_count; i++) a->icon_dirs[i] = m->icon_dirs[i];
    a->icon_dir_count = m->icon_dir_count;
    a->cur_default    = m->cur_default;
    a->cur_pointer    = m->cur_pointer;
    a->cur_text       = m->cur_text;
    a->wake_event     = m->wake_event;
    take_look(a, m);
    apply_render_scale(a);
    rebuild_font(a);
    apply_widget_style(a);
    nk_textedit_init_fixed(&a->edit, a->edit_buf, sizeof(a->edit_buf));
    set_window_icon(a->win, reaktor_launch_icon());
    reaktor_window_set_dark(a->win, a->dark);
    nk_input_begin(a->ctx);
    a->dirty = 1;

    w = &g_win[g_win_n++];
    SDL_zero(*w);
    w->id    = g_win_next++;
    w->app   = a;
    w->spec  = *spec;
    w->title = SDL_strdup(spec->window.title ? spec->window.title : "");
    SDL_ShowWindow(a->win);
    reaktor_wake(m);
    return w->id;
#endif
}

void
reaktor_window_close(App *app, int id)
{
    rwin *w = by_id(id);

    if (!w) return;
    w->closing = 1;
    reaktor_wake(app);
}

int
reaktor_window_is_open(App *app, int id)
{
    rwin *w = by_id(id);

    (void)app;
    return w && !w->closing;
}

static void frame(rwin *w);

/* Nuklear takes NK_INPUT_MAX - 1 bytes a frame; a longer run spans frames. */
static void
take_text(rwin *w, const char *t)
{
    const struct nk_input *in = &w->app->ctx->input;
    int len = (int)SDL_strlen(t), n;
    nk_rune rune;

    while (len > 0 && (n = nk_utf_decode(t, &rune, len)) > 0) {
        if (in->keyboard.text_len + n >= NK_INPUT_MAX) frame(w);
        nk_input_unicode(w->app->ctx, rune);
        t += n;
        len -= n;
    }
}

int
reaktor_windows_event(SDL_Event *event)
{
    SDL_Window *sw;
    rwin       *w = NULL;
    int         i;

    if (!g_win_n || !(sw = SDL_GetWindowFromEvent(event))) return 0;
    for (i = 0; i < g_win_n && !w; i++)
        if (g_win[i].app->win == sw) w = &g_win[i];
    if (!w) return 0;
    switch (event->type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        w->closing = 1;
        break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        /* One cursor serves every window; the main one sets its own again. */
        SDL_SetCursor(reaktor_main_app()->cur_default);
        reaktor_main_app()->cur_shown = 0;
        w->app->cur_shown = 0;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        w->app->hot_last         = -1;
        w->app->hot_last_repaint = 0;
        w->app->want_cursor      = 0;
        reaktor_show_cursor(w->app);
        nk_sdl_handle_event(w->app->ctx, event);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.state ||
            reaktor_hot_motion(w->app, event->motion.x, event->motion.y))
            w->app->dirty = 1;
        reaktor_show_cursor(w->app);
        nk_sdl_handle_event(w->app->ctx, event);
        return 1;
    case SDL_EVENT_TEXT_INPUT:
        take_text(w, event->text.text);
        break;
    default:
        nk_sdl_handle_event(w->app->ctx, event);
        break;
    }
    w->app->dirty = 1;
    return 1;
}

static void
frame(rwin *w)
{
    App *a = w->app;
    struct nk_context *ctx = a->ctx;
    enum nk_anti_aliasing fill, line;
    float s = reaktor_scale();
    int ww = 0, wh = 0;
    struct nk_rect area;

    SDL_GetWindowSizeInPixels(a->win, &ww, &wh);
    if (s > 0.0f) {
        ww = (int)(ww / s + 0.5f);
        wh = (int)(wh / s + 0.5f);
    }
    area = nk_rect(0, 0, (float)ww, (float)wh);
    a->dirty    = 0;
    a->hot_n    = 0;
    a->editing  = 0;
    a->popup_lo = a->popup_hi = a->trap_lo = a->trap_hi = -1;

    nk_input_end(ctx);
    ctx->style.text.color = a->text;
    reaktor_a11y_begin(&a->a11y, w->title, area);
    reaktor_frame_begin(a, ctx, area);
    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(a->clear));
    if (nk_begin(ctx, "page", area, NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR)) {
        nk_style_pop_vec2(ctx);
        w->spec.page(a, ctx, ww, wh, w->spec.user);
        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_vec2(ctx);
    reaktor_frame_end();
    reaktor_a11y_end(&a->a11y);
    nk_sdl_update_TextInput(ctx);
    reaktor_place_ime(a);

    reaktor_render_aa(a, &fill, &line);
    SDL_SetRenderDrawColor(a->ren, a->clear.r, a->clear.g, a->clear.b, a->clear.a);
    SDL_RenderClear(a->ren);
    nk_sdl_render_ex(ctx, fill, line);
    SDL_RenderPresent(a->ren);
    nk_input_begin(ctx);
}

void
reaktor_windows_draw(void)
{
    int i;

    for (i = 0; i < g_win_n; i++)
        if (!g_win[i].closing && g_win[i].app->dirty) frame(&g_win[i]);
    for (i = g_win_n - 1; i >= 0; i--)
        if (g_win[i].closing) shut(i, 1);
}

void
reaktor_windows_restyle(void)
{
    int i;

    for (i = 0; i < g_win_n; i++) {
        App *a = g_win[i].app;

        take_look(a, reaktor_main_app());
        img_cache_clear(a);
        apply_widget_style(a);
        reaktor_window_set_dark(a->win, a->dark);
        a->dirty = 1;
    }
}

void
reaktor_windows_rescale(void)
{
    int i;

    for (i = 0; i < g_win_n; i++) {
        App *a = g_win[i].app;

        apply_render_scale(a);
        img_cache_clear(a);
        rebuild_font(a);
        a->dirty = 1;
    }
}

/* At quit; `closed` is for a window that closes while the app runs. */
void
reaktor_windows_close_all(void)
{
    while (g_win_n) shut(g_win_n - 1, 0);
}
