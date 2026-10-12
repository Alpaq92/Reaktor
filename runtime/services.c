#include "internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

typedef struct setting {
    char *key, *value;
} setting;

static setting *g_set;
static int      g_set_n, g_set_cap, g_set_loaded, g_set_dirty;

#ifdef __EMSCRIPTEN__
EM_JS(char *, web_settings_load, (const char *name), {
    var v = window['localStorage'].getItem('reaktor:' + UTF8ToString(name));
    return v === null ? 0 : stringToNewUTF8(v);
});
EM_JS(void, web_settings_save, (const char *name, const char *text), {
    window['localStorage'].setItem('reaktor:' + UTF8ToString(name), UTF8ToString(text));
});
#else
static char *
settings_path(void)
{
    char *dir = SDL_GetPrefPath(NULL, reaktor_app_ident());
    char *path = NULL;

    if (dir) SDL_asprintf(&path, "%ssettings.txt", dir);
    SDL_free(dir);
    return path;
}
#endif

static int
setting_find(const char *key)
{
    int i;

    for (i = 0; i < g_set_n; i++)
        if (SDL_strcmp(g_set[i].key, key) == 0) return i;
    return -1;
}

static void
setting_put(const char *key, size_t klen, const char *value, size_t vlen)
{
    setting *s;

    if (g_set_n == g_set_cap) {
        int      cap  = g_set_cap ? g_set_cap * 2 : 16;
        setting *more = (setting *)SDL_realloc(g_set, (size_t)cap * sizeof *more);

        if (!more) return;
        g_set     = more;
        g_set_cap = cap;
    }
    s        = &g_set[g_set_n];
    s->key   = SDL_strndup(key, klen);
    s->value = SDL_strndup(value, vlen);
    if (s->key && s->value) g_set_n++;
    else { SDL_free(s->key); SDL_free(s->value); }
}

static void
settings_load(void)
{
    char *text, *line, *next;

    if (g_set_loaded) return;
    g_set_loaded = 1;
#ifdef __EMSCRIPTEN__
    text = web_settings_load(reaktor_app_ident());
#else
    {
        char *path = settings_path();

        text = path ? (char *)SDL_LoadFile(path, NULL) : NULL;
        SDL_free(path);
    }
#endif
    for (line = text; line && *line; line = next) {
        char *eq  = SDL_strchr(line, '=');
        char *end = SDL_strchr(line, '\n');

        next = end ? end + 1 : line + SDL_strlen(line);
        if (!end) end = next;
        if (eq && eq < end)
            setting_put(line, (size_t)(eq - line), eq + 1, (size_t)(end - eq - 1));
    }
    SDL_free(text);
}

void
reaktor_settings_save(void)
{
    size_t bytes = 1, at = 0;
    char  *text;
    int    i;

    if (!g_set_dirty) return;
    for (i = 0; i < g_set_n; i++)
        bytes += SDL_strlen(g_set[i].key) + SDL_strlen(g_set[i].value) + 2;
    text = (char *)SDL_malloc(bytes);
    if (!text) return;
    for (i = 0; i < g_set_n; i++)
        at += (size_t)SDL_snprintf(text + at, bytes - at, "%s=%s\n",
                                   g_set[i].key, g_set[i].value);
    text[at] = '\0';
#ifdef __EMSCRIPTEN__
    web_settings_save(reaktor_app_ident(), text);
#else
    {
        char *path = settings_path();

        if (path && !SDL_SaveFile(path, text, at))
            SDL_Log("settings not saved: %s", SDL_GetError());
        SDL_free(path);
    }
#endif
    SDL_free(text);
    g_set_dirty = 0;
}

const char *
reaktor_setting(const char *key)
{
    int i;

    if (!key) return NULL;
    settings_load();
    i = setting_find(key);
    return i < 0 ? NULL : g_set[i].value;
}

void
reaktor_setting_set(const char *key, const char *value)
{
    int i;

    if (!key || !*key || SDL_strchr(key, '=') || SDL_strchr(key, '\n') ||
        (value && SDL_strchr(value, '\n')))
        return;
    settings_load();
    i = setting_find(key);
    if (i >= 0 && value && SDL_strcmp(g_set[i].value, value) == 0) return;
    if (i >= 0) {
        SDL_free(g_set[i].key);
        SDL_free(g_set[i].value);
        g_set[i] = g_set[--g_set_n];
    }
    if (value) setting_put(key, SDL_strlen(key), value, SDL_strlen(value));
    g_set_dirty = 1;
#ifdef __EMSCRIPTEN__
    reaktor_settings_save();
#endif
}

void
reaktor_settings_free(void)
{
    int i;

    for (i = 0; i < g_set_n; i++) {
        SDL_free(g_set[i].key);
        SDL_free(g_set[i].value);
    }
    SDL_free(g_set);
    g_set = NULL;
    g_set_n = g_set_cap = g_set_loaded = 0;
}

void
reaktor_window_restore(App *app)
{
    const char *at = reaktor_setting("window");
    SDL_Rect    r;

    if (at && SDL_sscanf(at, "%d %d %d %d", &r.x, &r.y, &r.w, &r.h) == 4 &&
        r.w > 0 && r.h > 0) {
        SDL_SetWindowSize(app->win, r.w, r.h);
        if (SDL_GetDisplayForRect(&r)) SDL_SetWindowPosition(app->win, r.x, r.y);
    }
    if (reaktor_setting("window.maximized")) SDL_MaximizeWindow(app->win);
}

void
reaktor_window_remember(App *app)
{
    char     at[64];
    SDL_Rect r;

    if (SDL_GetWindowFlags(app->win) & SDL_WINDOW_MAXIMIZED) {
        reaktor_setting_set("window.maximized", "1");
        return;
    }
    SDL_GetWindowPosition(app->win, &r.x, &r.y);
    SDL_GetWindowSize(app->win, &r.w, &r.h);
    SDL_snprintf(at, sizeof at, "%d %d %d %d", r.x, r.y, r.w, r.h);
    reaktor_setting_set("window", at);
    reaktor_setting_set("window.maximized", NULL);
}
