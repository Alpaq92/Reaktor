/* theme.h - system colour-scheme detection and window-frame theming.
 *
 * The title bar is drawn by the OS or window manager, never by Nuklear, so
 * matching it to the system theme is inherently platform-specific. The two
 * calls below are the whole portable surface; `native_window` is an opaque
 * handle (HWND on Windows, NSWindow* on macOS, an X11 Window on Unix), which
 * keeps this header free of platform includes. */
#ifndef REAKTOR_THEME_H
#define REAKTOR_THEME_H

/* 1 = system is in dark mode, 0 = light, -1 = could not determine. */
int reaktor_prefers_dark(void);

/* Applies a dark or light window frame. Returns 1 if the platform applied it,
 * 0 if unsupported or it failed. */
int reaktor_window_set_dark(void *native_window, int dark);

#endif /* REAKTOR_THEME_H */
