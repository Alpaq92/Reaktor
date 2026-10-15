#include "internal.h"
#include "reaktor/launch.h"

int
reaktor_window_popup(struct nk_rect r,
                     void (*page)(App *, struct nk_context *, int, int, void *),
                     void *user)
{
    App *m = reaktor_main_app();
    reaktor_window spec;

    if (!m || m->popups_inline) return 0;
    SDL_zero(spec);
    spec.page = page;
    spec.user = user;
    return reaktor_window_spawn(&spec, &r);
}

/* The usable display, in the main window's logical units. */
int
reaktor_window_bounds(struct nk_rect *out)
{
    App *m = reaktor_main_app();
    SDL_Rect d;
    int x, y;
    float k = reaktor_window_points();

    if (!m || m->popups_inline ||
        !SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(m->win), &d) ||
        !SDL_GetWindowPosition(m->win, &x, &y))
        return 0;
    *out = nk_rect((float)(d.x - x) / k, (float)(d.y - y) / k, (float)d.w / k, (float)d.h / k);
    return 1;
}
