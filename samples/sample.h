#ifndef REAKTOR_SAMPLE_H
#define REAKTOR_SAMPLE_H

#include "nk_common.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

void page_shell(App *app, struct nk_context *ctx, int win_w, int win_h);

int sample_key(App *app, const SDL_Event *e);

typedef struct reaktor_window_spec {
    int w, h;
    const char *title;
    int borderless;
} reaktor_window_spec;

void sample_window(reaktor_window_spec *out);

SDL_HitTestResult SDLCALL window_hit_test(SDL_Window *win,
                                          const SDL_Point *pt, void *data);

void sample_file_taken(App *app);

void sample_args(App *app, int argc, char **argv);

#endif
