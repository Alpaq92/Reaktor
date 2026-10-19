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
    switch (SDL_GetSystemTheme()) {
    case SDL_SYSTEM_THEME_DARK:  return 1;
    case SDL_SYSTEM_THEME_LIGHT: return 0;
    default:                     return -1;
    }
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
    int   tried;
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
#undef DBUS_FN
    g_dbus.error_init(&e);
    g_dbus.conn = g_dbus.bus_get(0, &e);
    g_dbus.error_free(&e);
    return g_dbus.conn != NULL;
}

/* A setting as a number, or -1 when the portal does not have it. */
static int portal_read(const char *ns, const char *key)
{
    static const char *const how[2] = { "ReadOne", "Read" };
    int i, out = -1;

    if (!dbus_ready()) return -1;
    for (i = 0; i < 2 && out < 0; i++) {
        void      *msg, *reply;
        dbus_error e;
        dbus_iter  it, in;
        int        type;

        msg = g_dbus.method_call("org.freedesktop.portal.Desktop",
                                 "/org/freedesktop/portal/desktop",
                                 "org.freedesktop.portal.Settings", how[i]);
        if (!msg) return -1;
        if (!g_dbus.append_args(msg, 's', &ns, 's', &key, 0)) {
            g_dbus.message_unref(msg);
            return -1;
        }
        g_dbus.error_init(&e);
        reply = g_dbus.send_block(g_dbus.conn, msg, 300, &e);
        g_dbus.error_free(&e);
        g_dbus.message_unref(msg);
        if (!reply) continue;
        if (g_dbus.iter_init(reply, &it)) {
            while ((type = g_dbus.iter_type(&it)) == 'v') {
                g_dbus.iter_recurse(&it, &in);
                it = in;
            }
            if (type == 'u' || type == 'i' || type == 'b') {
                Uint32 v = 0;

                g_dbus.iter_basic(&it, &v);
                out = (int)v;
            }
        }
        g_dbus.message_unref(reply);
    }
    return out;
}
#endif

int reaktor_prefers_reduced_motion(void)
{
#if defined(_WIN32)
    BOOL on = TRUE;

    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) && !on;
#elif defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        return window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 1 : 0;
    });
#elif defined(__APPLE__)
    return workspace_says("accessibilityDisplayShouldReduceMotion");
#elif !defined(__ANDROID__)
    int v = portal_read("org.freedesktop.appearance", "reduced-motion");

    if (v >= 0) return v == 1;
    v = portal_read("org.gnome.desktop.interface", "enable-animations");
    return v == 0;
#else
    return 0;
#endif
}

int reaktor_prefers_contrast(void)
{
#if defined(_WIN32)
    HIGHCONTRASTW hc;

    hc.cbSize = sizeof hc;
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) &&
           (hc.dwFlags & HCF_HIGHCONTRASTON);
#elif defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        return window.matchMedia && (window.matchMedia('(prefers-contrast: more)').matches ||
                                     window.matchMedia('(forced-colors: active)').matches) ? 1 : 0;
    });
#elif defined(__APPLE__)
    return workspace_says("accessibilityDisplayShouldIncreaseContrast");
#elif !defined(__ANDROID__)
    int v = portal_read("org.freedesktop.appearance", "contrast");

    if (v >= 0) return v == 1;
    return portal_read("org.gnome.desktop.a11y.interface", "high-contrast") == 1;
#else
    return 0;
#endif
}

int reaktor_window_set_dark(SDL_Window *win, int dark)
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

    if (!native_window) return 0;
    if (!looked_up) {
        void *dll = (void *)LoadLibraryA("dwmapi.dll");
        if (dll)
            set_attr = (set_attr_fn)(void *)GetProcAddress(
                (HMODULE)dll, "DwmSetWindowAttribute");
        looked_up = 1;
    }
    if (!set_attr) return 0;

    if (set_attr(native_window, 20, &value, sizeof(value)) != 0)
        set_attr(native_window, 19, &value, sizeof(value));
    return 1;
#else
    (void)win;
    (void)dark;
    return 1;
#endif
}
