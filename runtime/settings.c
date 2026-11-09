#include "internal.h"

#include "toml.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
    if (!reaktor_grow((void **)&g_set, &g_cap, g_count + 1, sizeof *g_set)) {
        SDL_free(key);
        SDL_free(value);
        return;
    }
    g_set[g_count].key     = key;
    g_set[g_count++].value = value;
}

static void
settings_take(const toml_table_t *t, const char *prefix)
{
    int i, len, n = toml_table_len(t);

    for (i = 0; i < n; i++) {
        const char   *name = toml_table_key(t, i, &len);
        toml_value_t  v;
        toml_table_t *sub;
        char         *key = NULL, *value = NULL;

        SDL_asprintf(&key, "%s%s", prefix, name);
        if (!key) continue;
        if ((sub = toml_table_table(t, name)) != NULL) {
            SDL_asprintf(&value, "%s.", key);
            if (value) settings_take(sub, value);
            SDL_free(value);
            SDL_free(key);
            continue;
        }
        if ((v = toml_table_string(t, name)).ok) {
            value = SDL_strdup(v.u.s);
            free(v.u.s);
        } else if ((v = toml_table_int(t, name)).ok) {
            SDL_asprintf(&value, "%lld", (long long)v.u.i);
        } else if ((v = toml_table_double(t, name)).ok) {
            SDL_asprintf(&value, "%g", v.u.d);
        } else if ((v = toml_table_bool(t, name)).ok) {
            value = SDL_strdup(v.u.b ? "true" : "false");
        }
        if (value) setting_put(key, value);
        else SDL_free(key);
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
    if (i >= 0 && value) {
        SDL_free(g_set[i].value);
        g_set[i].value = SDL_strdup(value);
    } else if (value) {
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
    char *path;

    settings_empty();
    g_loaded = 1;
    if (g_save_timer) reaktor_timer_stop(g_save_timer);
    g_save_timer = 0;
    if (reaktor_capturing()) return;
    path = settings_path();
    if (path) SDL_RemovePath(path);
    SDL_free(path);
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
