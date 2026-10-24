#ifndef REAKTOR_THEME_H
#define REAKTOR_THEME_H

int reaktor_prefers_dark(void);
void reaktor_prefers(int *reduced_motion, int *contrast);

struct SDL_Window;

void reaktor_window_set_dark(struct SDL_Window *win, int dark);

#endif
