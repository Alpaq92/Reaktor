#include "internal.h"

#include "toml.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* Settings live here as flat dotted keys, in the order they were added; the
   file is TOML, every key quoted at the top level, so `window` and
   `window.maximized` can both be strings. */
typedef struct { char *key, *value; } setting;

static setting *g_set;
static int      g_count, g_cap, g_loaded;
static int      g_dirty;
static unsigned g_save_timer;

#ifdef __EMSCRIPTEN__
EM_JS(void, settings_persist, (void), {
    FS.syncfs(false, function (err) { if (err) console.warn('settings: ' + err); });
});
#else
static void settings_persist(void) {}
#endif

static char *
settings_path(void)
{
    char *dir = SDL_GetPrefPath(NULL, reaktor_app_ident());
    char *path = NULL;

    if (dir) SDL_asprintf(&path, "%ssettings.toml", dir);
    SDL_free(dir);
    return path;
}

static int
setting_index(const char *key)
{
    int i;

    for (i = 0; i < g_count; i++)
        if (SDL_strcmp(g_set[i].key, key) == 0) return i;
    return -1;
}

static void
setting_put(char *key, char *value)
{
    int i = setting_index(key);

    if (i >= 0) {
        SDL_free(key);
        SDL_free(g_set[i].value);
        g_set[i].value = value;
        return;
    }
    if (g_count == g_cap) {
        int      cap  = g_cap ? g_cap * 2 : 16;
        setting *grow = (setting *)SDL_realloc(g_set, (size_t)cap * sizeof *grow);

        if (!grow) { SDL_free(key); SDL_free(value); return; }
        g_set = grow;
        g_cap = cap;
    }
    g_set[g_count].key     = key;
    g_set[g_count++].value = value;
}

/* A table nested in a hand-written file reads as dotted keys; a value that is
   not a string keeps its TOML spelling, so `maximized = 1` reads as "1". */
static void
settings_take(const toml_table_t *t, const char *prefix)
{
    int i;

    for (i = 0; i < t->nkval; i++) {
        const char  *name = t->kval[i]->key;
        toml_value_t v    = toml_table_string(t, name);
        char        *key  = NULL;

        SDL_asprintf(&key, "%s%s", prefix, name);
        if (!key) { if (v.ok) free(v.u.s); continue; }
        if (v.ok) {
            setting_put(key, SDL_strdup(v.u.s));
            free(v.u.s);
        } else {
            setting_put(key, SDL_strdup(t->kval[i]->val));
        }
    }
    for (i = 0; i < t->ntbl; i++) {
        char *sub = NULL;

        SDL_asprintf(&sub, "%s%s.", prefix, t->tbl[i]->key);
        if (sub) settings_take(t->tbl[i], sub);
        SDL_free(sub);
    }
}

static void
settings_load(void)
{
    char *text, *path;

    if (g_loaded) return;
    g_loaded = 1;
    if (reaktor_capturing()) return;
    path = settings_path();
    text = path ? (char *)SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (text) {
        char          err[200];
        toml_table_t *root = toml_parse(text, err, sizeof err);

        if (root) settings_take(root, "");
        else SDL_Log("settings not read: %s", err);
        toml_free(root);
    }
    SDL_free(text);
}

static void
put_string(SDL_IOStream *out, const char *s)
{
    SDL_WriteU8(out, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\') SDL_IOprintf(out, "\\%c", c);
        else if (c == '\n') SDL_IOprintf(out, "\\n");
        else if (c == '\t') SDL_IOprintf(out, "\\t");
        else if (c < 0x20 || c == 0x7f) SDL_IOprintf(out, "\\u%04X", c);
        else SDL_WriteU8(out, c);
    }
    SDL_WriteU8(out, '"');
}

void
reaktor_settings_save(void)
{
    char         *path, *tmp = NULL;
    SDL_IOStream *out;
    int           i, ok;

    if (!g_dirty || reaktor_capturing()) return;
    path = settings_path();
    if (path) SDL_asprintf(&tmp, "%s.new", path);
    out = tmp ? SDL_IOFromFile(tmp, "wb") : NULL;
    for (i = 0; out && i < g_count; i++) {
        put_string(out, g_set[i].key);
        SDL_IOprintf(out, " = ");
        put_string(out, g_set[i].value);
        SDL_WriteU8(out, '\n');
    }
    ok = out && SDL_CloseIO(out) && SDL_RenamePath(tmp, path);
    if (!ok) SDL_Log("settings not saved: %s", SDL_GetError());
    SDL_free(tmp);
    SDL_free(path);
    settings_persist();
    g_dirty = 0;
}

static void
settings_due(App *app, void *user)
{
    (void)app; (void)user;
    g_save_timer = 0;
    reaktor_settings_save();
}

static void
settings_changed(void)
{
    int          ms;
    reaktor_save mode = reaktor_save_mode(&ms);

    g_dirty = 1;
#ifdef __EMSCRIPTEN__
    if (mode == REAKTOR_SAVE_ON_QUIT) mode = REAKTOR_SAVE_ON_CHANGE;
#endif
    if (mode == REAKTOR_SAVE_ON_CHANGE)
        reaktor_settings_save();
    else if (mode == REAKTOR_SAVE_ON_TIMER && !g_save_timer)
        g_save_timer = reaktor_timer(NULL, ms, 0, settings_due, NULL);
}

const char *
reaktor_setting(const char *key)
{
    int i;

    if (!key) return NULL;
    settings_load();
    i = setting_index(key);
    return i < 0 ? NULL : g_set[i].value;
}

void
reaktor_setting_set(const char *key, const char *value)
{
    int i;

    if (!key || !*key) return;
    settings_load();
    i = setting_index(key);
    if (i < 0 && !value) return;
    if (i >= 0 && value && SDL_strcmp(g_set[i].value, value) == 0) return;
    if (value) {
        setting_put(SDL_strdup(key), SDL_strdup(value));
    } else {
        SDL_free(g_set[i].key);
        SDL_free(g_set[i].value);
        SDL_memmove(&g_set[i], &g_set[i + 1], (size_t)(g_count - i - 1) * sizeof *g_set);
        g_count--;
    }
    settings_changed();
}

static void
settings_empty(void)
{
    int i;

    for (i = 0; i < g_count; i++) {
        SDL_free(g_set[i].key);
        SDL_free(g_set[i].value);
    }
    SDL_free(g_set);
    g_set   = NULL;
    g_count = g_cap = 0;
    g_dirty = 0;
}

void
reaktor_settings_clear(void)
{
    settings_empty();
    g_loaded = 1;
    if (g_save_timer) reaktor_timer_stop(g_save_timer);
    g_save_timer = 0;
    if (reaktor_capturing()) return;
    {
        char *path = settings_path();

        if (path) SDL_RemovePath(path);
        SDL_free(path);
    }
    settings_persist();
}

void
reaktor_settings_free(void)
{
    settings_empty();
    g_loaded     = 0;
    g_save_timer = 0;
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
