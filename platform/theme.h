#ifndef REAKTOR_THEME_H
#define REAKTOR_THEME_H

int reaktor_prefers_dark(void);

struct SDL_Window;

int reaktor_window_set_dark(struct SDL_Window *win, int dark);

#endif
