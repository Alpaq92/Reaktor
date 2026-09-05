/* theme.c - system colour-scheme detection.
 *
 * This file used to carry a Windows registry read, a DwmSetWindowAttribute
 * call, and two unimplemented stubs for macOS and X11. SDL3 provides all of
 * it portably, so the whole platform layer collapses to what follows.
 * See FINDINGS §8bis. */
#include "theme.h"

#include <SDL3/SDL.h>

int curie_prefers_dark(void)
{
    switch (SDL_GetSystemTheme()) {
    case SDL_SYSTEM_THEME_DARK:  return 1;
    case SDL_SYSTEM_THEME_LIGHT: return 0;
    default:                     return -1;   /* unknown */
    }
}

int curie_window_set_dark(void *native_window, int dark)
{
    /* Nothing to do: SDL themes the window frame itself — it calls
     * DwmSetWindowAttribute on Windows and follows NSAppearance on macOS.
     * Kept so callers have one place to hook if a platform ever needs it. */
    (void)native_window;
    (void)dark;
    return 1;
}
