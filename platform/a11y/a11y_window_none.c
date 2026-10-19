/* Windows other than the main one, where only UI Automation serves them. */
#include "a11y.h"

void
reaktor_a11y_platform_window_push(struct SDL_Window *win, const reaktor_a11y *a,
                                  unsigned focus_id, reaktor_a11y_action activate,
                                  reaktor_a11y_action focus, void *user)
{
    (void)win; (void)a; (void)focus_id; (void)activate; (void)focus; (void)user;
}

void
reaktor_a11y_platform_window_drain(struct SDL_Window *win)
{
    (void)win;
}

void
reaktor_a11y_platform_window_gone(struct SDL_Window *win)
{
    (void)win;
}
