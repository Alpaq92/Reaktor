#include "internal.h"
#include "declare.h"
#include "sample.h"
#include "keys.h"

void
page_shell(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    (void)ctx;

    REAKTOR_FREE(.w = (float)win_w, .h = (float)win_h) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Close",
                .keys  = "Escape",
                .box   = { .w = 160.0f, .h = 40.0f,
                           .ml = (win_w - 160.0f) * 0.5f,
                           .mt = (win_h - 40.0f) * 0.5f } }))
            app->want_quit = 1;
    }
}

int
sample_key(App *app, const SDL_Event *e)
{
    if (reaktor_chord(e, 0, SDLK_ESCAPE)) {
        app->want_quit = 1;
        return 1;
    }
    return 0;
}

void
sample_window(reaktor_window_spec *out)
{
    out->w = 340;
    out->h = 180;
    out->title = "Simple";
}

SDL_HitTestResult SDLCALL
window_hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
    (void)win; (void)pt; (void)data;
    return SDL_HITTEST_NORMAL;
}

void
sample_file_taken(App *app)
{
    (void)app;
}

void
sample_args(App *app, int argc, char **argv)
{
    (void)app; (void)argc; (void)argv;
}
