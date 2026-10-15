#include "internal.h"

#define INI_IMPLEMENTATION
#define INI_MALLOC(ctx, size)        ((void)(ctx), SDL_malloc(size))
#define INI_FREE(ctx, ptr)           ((void)(ctx), SDL_free(ptr))
#define INI_MEMCPY(dst, src, cnt)    SDL_memcpy(dst, src, cnt)
#define INI_STRLEN(s)                SDL_strlen(s)
#define INI_STRNICMP(s1, s2, cnt)    SDL_strncasecmp(s1, s2, cnt)
#include "ini.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static ini_t   *g_ini;
static int      g_ini_dirty;
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

    if (dir) SDL_asprintf(&path, "%ssettings.conf", dir);
    SDL_free(dir);
    return path;
}

static ini_t *
settings(void)
{
    char *text;

    if (g_ini) return g_ini;
    if (reaktor_capturing()) return g_ini = ini_create(NULL);
    {
        char *path = settings_path();

        text = path ? (char *)SDL_LoadFile(path, NULL) : NULL;
        SDL_free(path);
    }
    g_ini = text ? ini_load(text, NULL) : ini_create(NULL);
    SDL_free(text);
    return g_ini;
}

static int
setting_find(const char *key, int *section, int add)
{
    const char *dot = SDL_strrchr(key, '.');
    ini_t      *ini = settings();

    *section = INI_GLOBAL_SECTION;
    if (!ini) return INI_NOT_FOUND;
    if (dot) {
        *section = ini_find_section(ini, key, (int)(dot - key));
        if (*section == INI_NOT_FOUND && add)
            *section = ini_section_add(ini, key, (int)(dot - key));
        if (*section == INI_NOT_FOUND) return INI_NOT_FOUND;
        key = dot + 1;
    }
    return ini_find_property(ini, *section, key, 0);
}

void
reaktor_settings_save(void)
{
    int   size;
    char *text;

    if (!g_ini_dirty || !g_ini || reaktor_capturing()) return;
    size = ini_save(g_ini, NULL, 0);
    text = (char *)SDL_malloc((size_t)size);
    if (!text) return;
    ini_save(g_ini, text, size);
    {
        char *path = settings_path(), *tmp = NULL;

        if (path) SDL_asprintf(&tmp, "%s.new", path);
        if (!tmp || !SDL_SaveFile(tmp, text, (size_t)size - 1) || !SDL_RenamePath(tmp, path))
            SDL_Log("settings not saved: %s", SDL_GetError());
        SDL_free(tmp);
        SDL_free(path);
    }
    settings_persist();
    SDL_free(text);
    g_ini_dirty = 0;
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

    g_ini_dirty = 1;
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
    int section, property = key ? setting_find(key, &section, 0) : INI_NOT_FOUND;

    return property == INI_NOT_FOUND ? NULL
                                     : ini_property_value(g_ini, section, property);
}

void
reaktor_setting_set(const char *key, const char *value)
{
    const char *name;
    int         section, property;

    if (!key || !*key || SDL_strpbrk(key, "=[]\n") || (value && SDL_strchr(value, '\n')))
        return;
    property = setting_find(key, &section, value != NULL);
    name     = SDL_strrchr(key, '.') ? SDL_strrchr(key, '.') + 1 : key;
    if (property != INI_NOT_FOUND && value &&
        SDL_strcmp(ini_property_value(g_ini, section, property), value) == 0)
        return;
    if (!value && property == INI_NOT_FOUND) return;
    if (!value)
        ini_property_remove(g_ini, section, property);
    else if (property != INI_NOT_FOUND)
        ini_property_value_set(g_ini, section, property, value, 0);
    else if (section != INI_NOT_FOUND)
        ini_property_add(g_ini, section, name, 0, value, 0);
    else
        return;
    settings_changed();
}

void
reaktor_settings_clear(void)
{
    ini_destroy(g_ini);
    g_ini       = ini_create(NULL);
    g_ini_dirty = 0;
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
    ini_destroy(g_ini);
    g_ini        = NULL;
    g_ini_dirty  = 0;
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
