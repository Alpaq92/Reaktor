/* theme.c - system colour-scheme detection.
 *
 * This file used to carry a Windows registry read, a DwmSetWindowAttribute
 * call, and two unimplemented stubs for macOS and X11. SDL3 provides all of
 * it portably, so the whole platform layer collapses to what follows.
 * See FINDINGS §8bis. */
#include "theme.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

int curie_prefers_dark(void)
{
    switch (SDL_GetSystemTheme()) {
    case SDL_SYSTEM_THEME_DARK:  return 1;
    case SDL_SYSTEM_THEME_LIGHT: return 0;
    default:                     return -1;   /* unknown */
    }
}

/* Darkens or lightens the window frame the desktop draws.
 *
 * The previous comment here claimed SDL did this already. It does not, and the
 * distinction matters: SDL follows the *system* scheme, while this has to
 * follow the *app's*, which the user can pin to light or dark independently.
 *
 * dwmapi is reached through GetProcAddress rather than linked, so the build
 * gains no import and nothing breaks on a Windows old enough to lack the
 * attribute. DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on Windows 10 2004 and
 * later, and was 19 before that; both are tried.
 *
 * Elsewhere this stays a no-op: macOS themes the frame from NSAppearance,
 * which SDL already follows, and on Linux and the BSDs the frame belongs to
 * the window manager. */
int curie_window_set_dark(void *native_window, int dark)
{
#ifdef _WIN32
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
    (void)native_window;
    (void)dark;
    return 1;
#endif
}
