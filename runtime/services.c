#include "internal.h"

typedef struct timer {
    SDL_TimerID  id;
    App         *app;
    reaktor_task fn;
    void        *user;
    int          repeat;
} timer;

static timer *g_timers;
static int    g_timer_n, g_timer_cap;

static int
timer_find(SDL_TimerID id)
{
    int i;

    for (i = 0; i < g_timer_n; i++)
        if (g_timers[i].id == id) return i;
    return -1;
}

static void SDLCALL
timer_fire(void *data)
{
    int   i = timer_find((SDL_TimerID)(uintptr_t)data);
    timer t;

    if (i < 0) return;
    t = g_timers[i];
    if (!t.repeat) g_timers[i] = g_timers[--g_timer_n];
    t.fn(t.app, t.user);
    reaktor_wake(t.app);
}

static Uint32 SDLCALL
timer_tick(void *repeat, SDL_TimerID id, Uint32 ms)
{
    SDL_RunOnMainThread(timer_fire, (void *)(uintptr_t)id, false);
    return repeat ? ms : 0;
}

unsigned
reaktor_timer(App *app, int ms, int repeat, reaktor_task fn, void *user)
{
    timer *t;

    if (!fn || ms < 0) return 0;
    if (g_timer_n == g_timer_cap) {
        int    cap  = g_timer_cap ? g_timer_cap * 2 : 8;
        timer *more = (timer *)SDL_realloc(g_timers, (size_t)cap * sizeof *more);

        if (!more) return 0;
        g_timers    = more;
        g_timer_cap = cap;
    }
    t       = &g_timers[g_timer_n];
    t->app  = app ? app : reaktor_main_app();
    t->fn   = fn;
    t->user = user;
    t->repeat = repeat != 0;
    t->id   = SDL_AddTimer((Uint32)ms, timer_tick, repeat ? (void *)1 : NULL);
    if (!t->id) return 0;
    g_timer_n++;
    return t->id;
}

void
reaktor_timer_stop(unsigned id)
{
    int i = timer_find(id);

    if (i < 0) return;
    SDL_RemoveTimer(id);
    g_timers[i] = g_timers[--g_timer_n];
}

void
reaktor_timers_drop(App *app)
{
    int i;

    for (i = g_timer_n - 1; i >= 0; i--)
        if (!app || g_timers[i].app == app) {
            SDL_RemoveTimer(g_timers[i].id);
            g_timers[i] = g_timers[--g_timer_n];
        }
    if (!app) {
        SDL_free(g_timers);
        g_timers  = NULL;
        g_timer_cap = 0;
    }
}

typedef struct post {
    App         *app;
    reaktor_task fn;
    void        *user;
} post;

static void SDLCALL
post_run(void *data)
{
    post p = *(post *)data;

    SDL_free(data);
    if (!p.app) p.app = reaktor_main_app();
    if (!reaktor_app_alive(p.app)) return;
    p.fn(p.app, p.user);
    reaktor_wake(p.app);
}

void
reaktor_post(App *app, reaktor_task fn, void *user)
{
    post *p = fn ? (post *)SDL_malloc(sizeof *p) : NULL;

    if (!p) return;
    p->app  = app;
    p->fn   = fn;
    p->user = user;
    if (!SDL_RunOnMainThread(post_run, p, false)) SDL_free(p);
}
