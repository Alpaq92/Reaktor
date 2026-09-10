#include "theme.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

int reaktor_prefers_dark(void)
{
    switch (SDL_GetSystemTheme()) {
    case SDL_SYSTEM_THEME_DARK:  return 1;
    case SDL_SYSTEM_THEME_LIGHT: return 0;
    default:                     return -1;
    }
}

int reaktor_window_set_dark(void *native_window, int dark)
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
