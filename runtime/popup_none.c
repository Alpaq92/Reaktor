#include "internal.h"

int
reaktor_window_popup(struct nk_rect r,
                     void (*page)(App *, struct nk_context *, int, int, void *),
                     void *user)
{
    (void)r; (void)page; (void)user;
    return 0;
}

int  reaktor_window_bounds(struct nk_rect *out)      { (void)out; return 0; }
