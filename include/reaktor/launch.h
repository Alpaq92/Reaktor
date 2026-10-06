#ifndef REAKTOR_LAUNCH_H
#define REAKTOR_LAUNCH_H

#include <stddef.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nk_context;
#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

typedef enum reaktor_console {
    REAKTOR_CONSOLE_NONE = 0,
    REAKTOR_CONSOLE_PARENT,
    REAKTOR_CONSOLE_DEBUG
} reaktor_console;

typedef enum reaktor_theme {
    REAKTOR_THEME_SYSTEM = 0,
    REAKTOR_THEME_LIGHT,
    REAKTOR_THEME_DARK
} reaktor_theme;

typedef enum reaktor_quit_reason {
    REAKTOR_QUIT_WINDOW = 0,
    REAKTOR_QUIT_APP,
    REAKTOR_QUIT_CONSOLE,
    REAKTOR_QUIT_SIGNAL,
    REAKTOR_QUIT_SESSION
} reaktor_quit_reason;

/* A path, absolute or under the root, or named bytes that outlive the run. */
typedef struct reaktor_asset {
    const char *path;
    const char *name;
    const void *data;
    size_t      size;
} reaktor_asset;

#define REAKTOR_LAYER_MAX 4

typedef struct reaktor_window_spec {
    const char   *title;
    int           w, h;
    int           min_w, min_h;
    int           borderless;
    reaktor_asset icon;
    SDL_HitTestResult (SDLCALL *hit_test)(SDL_Window *win,
                                          const SDL_Point *pt, void *data);
} reaktor_window_spec;

typedef struct reaktor_launch {
    const char          *name, *id, *version;
    reaktor_window_spec  window;
    reaktor_console      console;
    reaktor_theme        theme;
    const char          *confirm_close;

    reaktor_asset        css[REAKTOR_LAYER_MAX];
    int                  no_reaktor_css;
    reaktor_asset        font, font_bold;
    reaktor_asset        font_fallbacks[REAKTOR_LAYER_MAX];
    const char          *icon_dirs[REAKTOR_LAYER_MAX];
    const reaktor_asset *assets;
    int                  asset_count;

    void (*page)(App *app, struct nk_context *ctx, int w, int h);
    int  (*start)(App *app, int argc, char **argv);
    int  (*key)(App *app, const SDL_Event *e);
    int  (*closing)(App *app, reaktor_quit_reason why);
    void (*stop)(App *app);
    void (*file_opened)(App *app, const char *path);
    void  *user;
} reaktor_launch;

/* page gets the window's App, closed the main one's. */
typedef struct reaktor_window {
    reaktor_window_spec window;
    int                 modal;
    void (*page)(App *app, struct nk_context *ctx, int w, int h, void *user);
    void (*closed)(App *app, void *user);
    void  *user;
} reaktor_window;

/* In the main window; hooks get its App. */
typedef struct reaktor_floater {
    const char *title;
    float       w, h;
    int         modal;
    void (*body)(App *app, struct nk_context *ctx, int w, int h, void *user);
    void (*closed)(App *app, void *user);
    void  *user;
} reaktor_floater;

typedef struct reaktor_tray_item {
    const char *label;
    int         checkbox, checked, disabled;
    void      (*chosen)(App *app, int checked, void *user);
    void       *user;
} reaktor_tray_item;

typedef struct reaktor_tray {
    const char              *tooltip;
    const reaktor_tray_item *items;
    int                      count;
} reaktor_tray;

/* Returns the exit code; on the web it returns at once. */
int   reaktor_launch_app(int argc, char **argv, const reaktor_launch *launch);
void  reaktor_quit(App *app, int exit_code);
void  reaktor_request_quit(App *app);
void  reaktor_wake(App *app);
void  reaktor_set_theme(App *app, reaktor_theme theme);
void  reaktor_set_css(App *app, const reaktor_asset *css, int count);
void  reaktor_set_title(App *app, const char *title);
void  reaktor_set_confirm_close(App *app, const char *question);
void *reaktor_user(App *app);

SDL_Window   *reaktor_sdl_window(App *app);
SDL_Renderer *reaktor_sdl_renderer(App *app);
int           reaktor_dark(App *app);
reaktor_theme reaktor_get_theme(App *app);
/* --lang's value, or NULL; the language in use is reaktor_locale_current(). */
const char   *reaktor_lang_pref(App *app);
/* The main window's. */
void          reaktor_set_borderless(App *app, int on);
int           reaktor_borderless(App *app);

/* Main thread. Answers 0 on the web. */
int   reaktor_window_open(App *app, const reaktor_window *window);
void  reaktor_window_close(App *app, int id);
int   reaktor_window_is_open(App *app, int id);

/* Main thread. Answers the floater's id. */
int   reaktor_floater_open(App *app, const reaktor_floater *floater);
void  reaktor_floater_close(App *app, int id);
int   reaktor_floater_is_open(App *app, int id);

/* Main thread. Answers 0 where there is no tray. */
int   reaktor_tray_open(App *app, const reaktor_tray *tray);
void  reaktor_tray_check(App *app, int item, int checked);
void  reaktor_tray_close(App *app);

#ifndef REAKTOR_NO_SHORT_NAMES
static SDL_INLINE int
launchApp(int argc, char **argv, const reaktor_launch *launch)
{
    return reaktor_launch_app(argc, argv, launch);
}
#endif

#ifdef __cplusplus
}
#endif

#endif
