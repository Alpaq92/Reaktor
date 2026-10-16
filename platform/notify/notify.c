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

int
reaktor_notify(App *app, const reaktor_notification *n)
{
    (void)app;
    if (!n || !n->title) return 0;
    notify_web(n->title, n->text ? n->text : "");
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

/* As much of s as fits, cut short rather than lost. */
static void
notify_text(const char *s, WCHAR *out, int cap)
{
    int    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = n > 0 ? (WCHAR *)SDL_malloc((size_t)n * sizeof *w) : NULL;

    out[0] = 0;
    if (w && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) > 0) SDL_wcslcpy(out, w, (size_t)cap);
    SDL_free(w);
}

/* A balloon from an icon of our own, which Windows 10 and 11 show as a toast. */
int
reaktor_notify(App *app, const reaktor_notification *n)
{
    static unsigned gone;
    static int      watched;
    HWND hwnd;
    int  added;

    if (!app || !n || !n->title) return 0;
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
    notify_text(n->title, g_icon.szInfoTitle, (int)SDL_arraysize(g_icon.szInfoTitle));
    notify_text(n->title, g_icon.szTip, (int)SDL_arraysize(g_icon.szTip));
    notify_text(n->text ? n->text : "", g_icon.szInfo, (int)SDL_arraysize(g_icon.szInfo));
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

#elif defined(REAKTOR_HAVE_DBUS)
#include <dbus/dbus.h>

int
reaktor_notify(App *app, const reaktor_notification *n)
{
    DBusConnection *c;
    DBusMessage    *m;
    DBusMessageIter it, sub;
    const char     *name = reaktor_app_ident(), *icon = "", *body;
    dbus_uint32_t   replaces = 0;
    dbus_int32_t    expire = -1;
    int             ok;

    (void)app;
    if (!n || !n->title || !(c = dbus_bus_get(DBUS_BUS_SESSION, NULL))) return 0;
    m = dbus_message_new_method_call("org.freedesktop.Notifications",
                                     "/org/freedesktop/Notifications",
                                     "org.freedesktop.Notifications", "Notify");
    if (!m) return 0;
    body = n->text ? n->text : "";
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &replaces);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &n->title);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &body);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &sub);
    dbus_message_iter_close_container(&it, &sub);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &sub);
    dbus_message_iter_close_container(&it, &sub);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &expire);
    ok = dbus_connection_send(c, m, NULL);
    dbus_connection_flush(c);
    dbus_message_unref(m);
    dbus_connection_unref(c);
    return ok ? 1 : 0;
}

#elif defined(__APPLE__)
int reaktor_notify_macos(const char *title, const char *text);

int
reaktor_notify(App *app, const reaktor_notification *n)
{
    (void)app;
    return n && n->title ? reaktor_notify_macos(n->title, n->text ? n->text : "") : 0;
}

#else
int
reaktor_notify(App *app, const reaktor_notification *n)
{
    (void)app; (void)n;
    return 0;
}
#endif
