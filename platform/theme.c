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
#elif defined(REAKTOR_HAVE_RDBUS)
#include "rdbus.h"

static int portal_value(rdbus_iter *it)
{
    rdbus_iter in;
    int        type;

    while ((type = rdbus_iter_type(it)) == RDBUS_TYPE_VARIANT) {
        rdbus_iter_recurse(it, &in);
        *it = in;
    }
    if (type == RDBUS_TYPE_UINT32 || type == RDBUS_TYPE_INT32 || type == RDBUS_TYPE_BOOLEAN) {
        Uint32 v = 0;

        rdbus_iter_get_basic(it, &v);
        return (int)v;
    }
    return -1;
}

static const char *const g_portal[][2] = {
    { "org.freedesktop.appearance", "reduced-motion" },
    { "org.gnome.desktop.interface", "enable-animations" },
    { "org.freedesktop.appearance", "contrast" },
    { "org.gnome.desktop.a11y.interface", "high-contrast" },
};
#define PORTAL_N ((int)(sizeof g_portal / sizeof g_portal[0]))

static int g_portal_gone;

static void portal_read(int out[PORTAL_N])
{
    static const char *const ns[] = { "org.freedesktop.appearance",
                                      "org.gnome.desktop.interface",
                                      "org.gnome.desktop.a11y.interface" };
    rdbus_conn *bus;
    rdbus_msg  *msg, *reply = NULL;
    rdbus_error e;
    rdbus_iter  top, all, one, keys, kv;
    int         i;

    for (i = 0; i < PORTAL_N; i++) out[i] = -1;
    if (g_portal_gone) return;
    rdbus_error_init(&e);
    if (!(bus = rdbus_session(&e))) {
        g_portal_gone = 1;
        rdbus_error_free(&e);
        return;
    }
    msg = rdbus_msg_call("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.Settings", "ReadAll");
    if (msg) {
        rdbus_iter_init_append(msg, &top);
        rdbus_iter_open(&top, RDBUS_TYPE_ARRAY, "s", &all);
        for (i = 0; i < (int)SDL_arraysize(ns); i++)
            rdbus_iter_append_basic(&all, RDBUS_TYPE_STRING, &ns[i]);
        rdbus_iter_close(&top, &all);
        reply = rdbus_call(bus, msg, 300, &e);
        if (!reply)
            g_portal_gone = SDL_strcmp(e.name, "org.freedesktop.DBus.Error.ServiceUnknown") == 0 ||
                            SDL_strcmp(e.name, RDBUS_ERROR_UNKNOWN_METHOD) == 0;
        rdbus_msg_free(msg);
    }
    rdbus_error_free(&e);
    rdbus_unref(bus);
    if (!reply) return;
    if (rdbus_msg_has_signature(reply, "a{sa{sv}}") && rdbus_iter_init(reply, &top)) {
        for (rdbus_iter_recurse(&top, &all); rdbus_iter_type(&all) == RDBUS_TYPE_DICT_ENTRY;
             rdbus_iter_next(&all)) {
            const char *n = NULL;

            rdbus_iter_recurse(&all, &one);
            rdbus_iter_get_basic(&one, &n);
            rdbus_iter_next(&one);
            for (rdbus_iter_recurse(&one, &keys); rdbus_iter_type(&keys) == RDBUS_TYPE_DICT_ENTRY;
                 rdbus_iter_next(&keys)) {
                const char *k = NULL;

                rdbus_iter_recurse(&keys, &kv);
                rdbus_iter_get_basic(&kv, &k);
                rdbus_iter_next(&kv);
                for (i = 0; i < PORTAL_N; i++)
                    if (SDL_strcmp(n, g_portal[i][0]) == 0 && SDL_strcmp(k, g_portal[i][1]) == 0)
                        out[i] = portal_value(&kv);
            }
        }
    }
    rdbus_msg_free(reply);
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
#elif defined(REAKTOR_HAVE_RDBUS)
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
