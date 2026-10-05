#include "reaktor/reaktor.h"
#include "reaktor/main.h"

static void
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
            reaktor_request_quit(app);
    }
}

static int
sample_key(App *app, const SDL_Event *e)
{
    if (reaktor_chord(e, 0, SDLK_ESCAPE)) {
        reaktor_request_quit(app);
        return 1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    return launchApp(argc, argv, &(reaktor_launch){
        .name   = "Simple",
        .window = { .w = 340, .h = 180 },
        .page   = page_shell,
        .key    = sample_key });
}
