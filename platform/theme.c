#include "theme.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

int reaktor_prefers_dark(void)
{
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
static int workspace_says(const char *what)
{
    id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    BOOL (*ask)(id, SEL) = (BOOL (*)(id, SEL))objc_msgSend;
    Class c = objc_getClass("NSWorkspace");
    id ws = c ? send((id)c, sel_registerName("sharedWorkspace")) : nil;

    return ws && ask(ws, sel_registerName(what));
}
#elif !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
/* The desktop portal's settings, through a libdbus loaded when first asked. */
typedef struct { void *a, *b; unsigned c; void *d; } dbus_error;
typedef union { void *p[16]; long long l[16]; } dbus_iter;

static struct {
    int   tried, gone;
    void *lib, *conn;
    void (*error_init)(dbus_error *);
    void (*error_free)(dbus_error *);
    void *(*bus_get)(int, dbus_error *);
    void *(*method_call)(const char *, const char *, const char *, const char *);
    int (*append_args)(void *, int, ...);
    void *(*send_block)(void *, void *, int, dbus_error *);
    void (*message_unref)(void *);
    int (*iter_init)(void *, dbus_iter *);
    int (*iter_type)(dbus_iter *);
    void (*iter_recurse)(dbus_iter *, dbus_iter *);
    void (*iter_basic)(dbus_iter *, void *);
    int (*iter_next)(dbus_iter *);
    int (*error_has_name)(const dbus_error *, const char *);
    int (*has_signature)(void *, const char *);
} g_dbus;

static int dbus_ready(void)
{
    dbus_error e;

    if (g_dbus.tried) return g_dbus.conn != NULL;
    g_dbus.tried = 1;
    if (!(g_dbus.lib = SDL_LoadObject("libdbus-1.so.3"))) return 0;
#define DBUS_FN(f, name) if (!(*(void **)&g_dbus.f = (void *)SDL_LoadFunction(g_dbus.lib, name))) return 0
    DBUS_FN(error_init, "dbus_error_init");
    DBUS_FN(error_free, "dbus_error_free");
    DBUS_FN(bus_get, "dbus_bus_get");
    DBUS_FN(method_call, "dbus_message_new_method_call");
    DBUS_FN(append_args, "dbus_message_append_args");
    DBUS_FN(send_block, "dbus_connection_send_with_reply_and_block");
    DBUS_FN(message_unref, "dbus_message_unref");
    DBUS_FN(iter_init, "dbus_message_iter_init");
    DBUS_FN(iter_type, "dbus_message_iter_get_arg_type");
    DBUS_FN(iter_recurse, "dbus_message_iter_recurse");
    DBUS_FN(iter_basic, "dbus_message_iter_get_basic");
    DBUS_FN(iter_next, "dbus_message_iter_next");
    DBUS_FN(error_has_name, "dbus_error_has_name");
    DBUS_FN(has_signature, "dbus_message_has_signature");
#undef DBUS_FN
    g_dbus.error_init(&e);
    g_dbus.conn = g_dbus.bus_get(0, &e);
    g_dbus.error_free(&e);
    return g_dbus.conn != NULL;
}

/* A setting's value as a number, or -1 when it is not one. */
static int portal_value(dbus_iter *it)
{
    dbus_iter in;
    int       type;

    while ((type = g_dbus.iter_type(it)) == 'v') {
        g_dbus.iter_recurse(it, &in);
        *it = in;
    }
    if (type == 'u' || type == 'i' || type == 'b') {
        Uint32 v = 0;

        g_dbus.iter_basic(it, &v);
        return (int)v;
    }
    return -1;
}

/* The settings below in one round trip, -1 for each the portal does not
   have. A desktop with no portal, or one without ReadAll, is not asked again;
   one slow to start is. */
static const char *const g_portal[][2] = {
    { "org.freedesktop.appearance", "reduced-motion" },
    { "org.gnome.desktop.interface", "enable-animations" },
    { "org.freedesktop.appearance", "contrast" },
    { "org.gnome.desktop.a11y.interface", "high-contrast" },
};
#define PORTAL_N ((int)(sizeof g_portal / sizeof g_portal[0]))

static void portal_read(int out[PORTAL_N])
{
    static const char *const ns[] = { "org.freedesktop.appearance",
                                      "org.gnome.desktop.interface",
                                      "org.gnome.desktop.a11y.interface" };
    const char *const *names = ns;
    void      *msg, *reply;
    dbus_error e;
    dbus_iter  top, all, one, keys, kv;
    int        i;

    for (i = 0; i < PORTAL_N; i++) out[i] = -1;
    if (g_dbus.gone || !dbus_ready()) return;
    msg = g_dbus.method_call("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.Settings", "ReadAll");
    if (!msg) return;
    if (!g_dbus.append_args(msg, 'a', 's', &names, (int)SDL_arraysize(ns), 0)) {
        g_dbus.message_unref(msg);
        return;
    }
    g_dbus.error_init(&e);
    reply = g_dbus.send_block(g_dbus.conn, msg, 300, &e);
    if (!reply)
        g_dbus.gone = g_dbus.error_has_name(&e, "org.freedesktop.DBus.Error.ServiceUnknown") ||
                      g_dbus.error_has_name(&e, "org.freedesktop.DBus.Error.UnknownMethod");
    g_dbus.error_free(&e);
    g_dbus.message_unref(msg);
    if (!reply) return;
    if (g_dbus.has_signature(reply, "a{sa{sv}}") && g_dbus.iter_init(reply, &top)) {
        for (g_dbus.iter_recurse(&top, &all); g_dbus.iter_type(&all) == 'e'; g_dbus.iter_next(&all)) {
            const char *n = NULL;

            g_dbus.iter_recurse(&all, &one);
            g_dbus.iter_basic(&one, &n);
            g_dbus.iter_next(&one);
            for (g_dbus.iter_recurse(&one, &keys); g_dbus.iter_type(&keys) == 'e';
                 g_dbus.iter_next(&keys)) {
                const char *k = NULL;

                g_dbus.iter_recurse(&keys, &kv);
                g_dbus.iter_basic(&kv, &k);
                g_dbus.iter_next(&kv);
                for (i = 0; i < PORTAL_N; i++)
                    if (SDL_strcmp(n, g_portal[i][0]) == 0 && SDL_strcmp(k, g_portal[i][1]) == 0)
                        out[i] = portal_value(&kv);
            }
        }
    }
    g_dbus.message_unref(reply);
}
#endif

void reaktor_prefers(int *reduced_motion, int *contrast)
{
#if defined(_WIN32)
    BOOL          on = TRUE;
    HIGHCONTRASTW hc;

    hc.cbSize       = sizeof hc;
    *reduced_motion = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) && !on;
    *contrast       = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) &&
                      (hc.dwFlags & HCF_HIGHCONTRASTON);
#elif defined(__EMSCRIPTEN__)
    *reduced_motion = EM_ASM_INT({
        return window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 1 : 0;
    });
    *contrast = EM_ASM_INT({
        return window.matchMedia && (window.matchMedia('(prefers-contrast: more)').matches ||
                                     window.matchMedia('(forced-colors: active)').matches) ? 1 : 0;
    });
#elif defined(__APPLE__)
    *reduced_motion = workspace_says("accessibilityDisplayShouldReduceMotion");
    *contrast       = workspace_says("accessibilityDisplayShouldIncreaseContrast");
#elif !defined(__ANDROID__)
    int v[PORTAL_N];

    portal_read(v);
    *reduced_motion = v[0] >= 0 ? v[0] == 1 : v[1] == 0;
    *contrast       = v[2] >= 0 ? v[2] == 1 : v[3] == 1;
#else
    *reduced_motion = 0;
    *contrast       = 0;
#endif
}

void reaktor_window_set_dark(SDL_Window *win, int dark)
{
#ifdef _WIN32
    void *native_window =
        win ? SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                                     SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL)
            : NULL;
    typedef long(__stdcall * set_attr_fn)(void *, unsigned long, void *,
                                          unsigned long);
    static set_attr_fn set_attr;
    static int looked_up;
    int value = dark ? 1 : 0;

    if (!native_window) return;
    if (!looked_up) {
        void *dll = (void *)LoadLibraryExA("dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dll)
            set_attr = (set_attr_fn)(void *)GetProcAddress(
                (HMODULE)dll, "DwmSetWindowAttribute");
        looked_up = 1;
    }
    if (!set_attr) return;

    if (set_attr(native_window, 20, &value, sizeof(value)) != 0)
        set_attr(native_window, 19, &value, sizeof(value));
#else
    (void)win;
    (void)dark;
#endif
}
