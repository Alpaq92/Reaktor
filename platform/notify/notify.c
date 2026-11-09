#include "internal.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(void, notify_web, (const char *title, const char *text), {
    var t = UTF8ToString(title), b = UTF8ToString(text);
    if (!('Notification' in window) || Notification.permission === 'denied') return;
    if (Notification.permission === 'granted') { new Notification(t, { body: b }); return; }
    Notification.requestPermission().then(function (p) {
        if (p === 'granted') new Notification(t, { body: b });
    });
});

static int
notify_os(App *app, const char *title, const char *text)
{
    (void)app;
    notify_web(title, text);
    return 1;
}

#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>

#define NOTIFY_ID 0x5245u

static NOTIFYICONDATAW g_icon;

static void
notify_remove(void)
{
    if (g_icon.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_icon);
    g_icon.hWnd = NULL;
}

static void
notify_gone(App *app, void *user)
{
    (void)app; (void)user;
    notify_remove();
}

static void
notify_text(const char *s, WCHAR *out, int cap)
{
    int    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = n > 0 ? (WCHAR *)SDL_malloc((size_t)n * sizeof *w) : NULL;

    out[0] = 0;
    if (w && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) > 0) SDL_wcslcpy(out, w, (size_t)cap);
    SDL_free(w);
}

static int
notify_os(App *app, const char *title, const char *text)
{
    static unsigned gone;
    static int      watched;
    HWND hwnd;
    int  added;

    if (!app) return 0;
    hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(app->win),
                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd) return 0;
    added = g_icon.hWnd != NULL;
    SDL_zero(g_icon);
    g_icon.cbSize = sizeof g_icon;
    g_icon.hWnd   = hwnd;
    g_icon.uID    = NOTIFY_ID;
    g_icon.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    g_icon.hIcon  = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!g_icon.hIcon) g_icon.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    notify_text(title, g_icon.szInfoTitle, (int)SDL_arraysize(g_icon.szInfoTitle));
    notify_text(title, g_icon.szTip, (int)SDL_arraysize(g_icon.szTip));
    notify_text(text, g_icon.szInfo, (int)SDL_arraysize(g_icon.szInfo));
    g_icon.dwInfoFlags = NIIF_USER | NIIF_RESPECT_QUIET_TIME;
    if (!Shell_NotifyIconW(added ? NIM_MODIFY : NIM_ADD, &g_icon)) {
        g_icon.hWnd = NULL;
        return 0;
    }
    /* A timer dies with the window; a quit before it fires would leave the icon. */
    if (!watched) watched = atexit(notify_remove) == 0;
    if (gone) reaktor_timer_stop(gone);
    gone = reaktor_timer(app, 10000, 0, notify_gone, NULL);
    return 1;
}

#elif defined(REAKTOR_HAVE_GIBUS)
#include "gibus.h"

static int
notify_os(App *app, const char *title, const char *body)
{
    gibus_conn  *c;
    gibus_msg   *m;
    gibus_iter   it, sub;
    const char  *name = reaktor_app_ident(), *icon = "";
    uint32_t     replaces = 0;
    int32_t      expire = -1;
    int          ok = 0;
    char        *t = reaktor_utf8_clean_dup(title), *b = reaktor_utf8_clean_dup(body);

    (void)app;
    if (!t || !b || !(c = gibus_session(NULL))) goto done;
    m = gibus_msg_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                       "org.freedesktop.Notifications", "Notify");
    if (!m) {
        gibus_unref(c);
        goto done;
    }
    gibus_iter_init_append(m, &it);
    gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &name);
    gibus_iter_append_basic(&it, GIBUS_TYPE_UINT32, &replaces);
    gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &icon);
    gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &t);
    gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &b);
    gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "s", &sub);
    gibus_iter_close(&it, &sub);
    gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "{sv}", &sub);
    gibus_iter_close(&it, &sub);
    gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &expire);
    ok = gibus_send(c, m, NULL);
    gibus_flush(c);
    gibus_msg_free(m);
    gibus_unref(c);
done:
    SDL_free(t);
    SDL_free(b);
    return ok;
}

#elif defined(__APPLE__)
int reaktor_notify_macos(const char *title, const char *text);

static int
notify_os(App *app, const char *title, const char *text)
{
    (void)app;
    return reaktor_notify_macos(title, text);
}
#endif

#if defined(__EMSCRIPTEN__) || defined(_WIN32) || defined(REAKTOR_HAVE_GIBUS) || defined(__APPLE__)
int
reaktor_notify(App *app, const reaktor_notification *n)
{
    if (!n || !n->title) return 0;
    return notify_os(app, n->title, n->text ? n->text : "");
}
#endif
