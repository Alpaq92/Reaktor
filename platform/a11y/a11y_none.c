#include "a11y.h"

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    (void)activate; (void)focus; (void)user;
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    (void)a; (void)focus_id;
}

void
reaktor_a11y_platform_drain(void)
{
}

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
