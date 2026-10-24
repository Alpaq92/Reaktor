#ifndef REAKTOR_THEME_H
#define REAKTOR_THEME_H

int reaktor_prefers_dark(void);
int reaktor_prefers_reduced_motion(void);
int reaktor_prefers_contrast(void);

struct SDL_Window;

void reaktor_window_set_dark(struct SDL_Window *win, int dark);

#endif
