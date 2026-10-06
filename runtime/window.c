#include "internal.h"
#include "anim.h"
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

/* Closed with the picker still open: its thread answers into the App. */
static App *g_parked[WINDOW_MAX];
static int  g_parked_n;

static rwin *
by_id(int id)
{
    int i;

    for (i = 0; i < g_win_n; i++)
        if (g_win[i].id == id) return &g_win[i];
    return NULL;
}

static rwin *
by_app(const App *a)
{
    int i;

    for (i = 0; i < g_win_n; i++)
        if (g_win[i].app == a) return &g_win[i];
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
unpark(int all)
{
    int i;

    for (i = g_parked_n - 1; i >= 0; i--)
        if (all || SDL_GetAtomicInt(&g_parked[i]->file_done)) {
            if (!all) SDL_free(g_parked[i]);
            g_parked[i] = g_parked[--g_parked_n];
        }
}

static void
teardown(App *a)
{
    reaktor_a11y_platform_window_gone(a->win);
    if (a->win) SDL_SetWindowModal(a->win, false);
    reaktor_app_input_free(a);
    field_undo_clear(a);
    reaktor_layout_free(&a->lay);
    if (a->ren) img_cache_free(a);
    if (a->ctx) nk_sdl_shutdown(a->ctx);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_free(a->glyphs);
    reaktor_anim_destroy(a->anim);
    a->anim = NULL;
    a->win = NULL;
    a->ren = NULL;
    a->ctx = NULL;
    if (a->file_pending && g_parked_n < WINDOW_MAX) g_parked[g_parked_n++] = a;
    else if (!a->file_pending) SDL_free(a);
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
    a->secondary      = 1;
    a->hot_last       = -1;
    a->queue.moved_ok = 1;
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
    a->win_id = SDL_GetWindowID(a->win);
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
    SDL_memcpy(a->frame_rate, m->frame_rate, sizeof a->frame_rate);
    SDL_memcpy(a->drag_rate, m->drag_rate, sizeof a->drag_rate);
    take_look(a, m);
    apply_render_scale(a);
    rebuild_font(a);
    apply_widget_style(a);
    nk_textedit_init_fixed(&a->edit, a->edit_buf, sizeof(a->edit_buf));
    {
        const char *icon = reaktor_asset_name(&spec->window.icon,
                                              "app/window-icon.svg");

        reaktor_set_window_icon(a->win, icon ? icon : reaktor_launch_icon());
    }
    reaktor_window_set_dark(a->win, a->dark);
    /* Served before it shows, as screen readers ask a new window at once. */
    reaktor_a11y_platform_window_push(a->win, &a->a11y, 0, reader_activate,
                                      reader_focus, a);
    nk_input_begin(a->ctx);
    a->dirty = 1;

    w = &g_win[g_win_n++];
    SDL_zero(*w);
    w->id    = g_win_next++;
    w->app   = a;
    w->spec  = *spec;
    w->title = SDL_strdup(spec->window.title ? spec->window.title : "");
    SDL_ShowWindow(a->win);
    reaktor_wake(a);
    return w->id;
#endif
}

void
reaktor_window_close(App *app, int id)
{
    rwin *w = by_id(id);

    if (!w || w->closing) return;
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

App *
reaktor_windows_app(const SDL_Event *event)
{
    SDL_Window *sw;
    int i;

    if (!g_win_n || !(sw = SDL_GetWindowFromEvent(event))) return NULL;
    for (i = 0; i < g_win_n; i++)
        if (g_win[i].app->win == sw) return g_win[i].app;
    return NULL;
}

void
reaktor_windows_take(App *app, const SDL_Event *event)
{
    rwin *w = by_app(app);
    App  *m = reaktor_main_app();

    if (!w) return;
    switch (event->type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        w->closing = 1;
        app->dirty = 1;
        break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        if (m) {
            SDL_SetCursor(m->cur_default);
            m->cur_shown = 0;
        }
        app->cur_shown = 0;
        break;
    default:
        break;
    }
}

int
reaktor_windows_retitle(App *app, const char *title)
{
    rwin *w = by_app(app);
    char *t;

    if (!w || !title || SDL_strcmp(title, w->title) == 0) return w != NULL;
    if (!(t = SDL_strdup(title))) return 1;
    SDL_free(w->title);
    w->title = t;
    SDL_SetWindowTitle(app->win, t);
    app->dirty = 1;
    reaktor_wake(app);
    return 1;
}

void
reaktor_windows_draw(void)
{
    int i, ww, wh;

    for (i = 0; i < g_win_n; i++) {
        rwin *w = &g_win[i];

        if (w->closing) continue;
        reaktor_app_size(w->app, &ww, &wh);
        reaktor_app_due(w->app, ww, wh);
        if (w->app->dirty)
            reaktor_app_frame(w->app, ww, wh, w->title, w->spec.page, w->spec.user);
    }
    for (i = g_win_n - 1; i >= 0; i--)
        if (g_win[i].closing) shut(i, 1);
    unpark(0);
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
reaktor_windows_dirty(void)
{
    int i;

    for (i = 0; i < g_win_n; i++)
        g_win[i].app->dirty = 1;
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

/* No `closed` hook at quit. */
void
reaktor_windows_close_all(void)
{
    while (g_win_n) shut(g_win_n - 1, 0);
    unpark(1);
}
