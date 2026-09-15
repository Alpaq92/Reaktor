#include "internal.h"
#include "declare.h"
#include "sample.h"
#include "keys.h"

#include <math.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define BOXES 64

static int g_boxes = BOXES;

static double g_seconds = 5.0;
static double g_cpu0;
static Uint64 g_t0;
static int    g_frames;
static size_t g_priv_peak;
static int    g_started;
static int    g_no_vsync;
static double g_fps;
static Uint64 g_due_ns;
static double g_work_ms;
static double g_build_ms, g_render_ms, g_present_ms;

void
page_shell(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    double now_ms;
    float  t;
    int    i;

    if (!g_started) {
        g_started = 1;
        if (g_no_vsync) SDL_SetRenderVSync(app->ren, 0);
        g_t0   = SDL_GetTicks();
        g_cpu0 = reaktor_process_cpu_ms();
    }

    g_build_ms   += app->build_ms_x100 / 100.0;
    g_render_ms  += app->render_ms_x100 / 100.0;
    g_present_ms += app->present_ms_x100 / 100.0;
    g_work_ms     = g_build_ms + g_render_ms + g_present_ms;

    t = (float)(SDL_GetTicks() - g_t0) / 1000.0f;

    for (i = 0; i < g_boxes; i++) {
        float a  = t * (1.0f + 0.05f * i);
        float cx = (float)((i % 8) * (win_w / 8) + win_w / 16);
        float cy = (float)((i / 8) * (win_h / 8) + win_h / 16);
        float r  = (float)(win_h / 24);
        struct nk_vec2 p[4];
        int k;

        for (k = 0; k < 4; k++) {
            float ang = a + (float)k * 1.5707963f;

            p[k].x = cx + r * (float)cos((double)ang);
            p[k].y = cy + r * (float)sin((double)ang);
        }
        nk_fill_polygon(cv, (float *)p, 4,
                        nk_rgb(60 + (i * 3) % 190, 140, 220));
    }

    {
        size_t rss = 0, priv = 0;

        reaktor_process_memory(&rss, &priv);
        if (priv > g_priv_peak) g_priv_peak = priv;
    }
    g_frames++;

    now_ms = (double)(SDL_GetTicks() - g_t0);
    if (now_ms >= g_seconds * 1000.0) {
        double cpu_ms = reaktor_process_cpu_ms() - g_cpu0;

        printf("size          %dx%d\n", win_w, win_h);
        printf("frames        %d\n", g_frames);
        printf("fps           %.1f\n", g_frames / (now_ms / 1000.0));
        printf("ms_build      %.2f\n", g_build_ms / g_frames);
        printf("ms_render     %.2f\n", g_render_ms / g_frames);
        printf("ms_present    %.2f\n", g_present_ms / g_frames);
        printf("ms_per_frame  %.2f\n", g_work_ms / g_frames);
        printf("cpu_at_60fps  %.1f\n",
               100.0 * (g_work_ms / g_frames) / (1000.0 / 60.0));
        printf("cpu_percent   %.1f\n", 100.0 * cpu_ms / now_ms);
        printf("private_mb    %.1f\n", g_priv_peak / (1024.0 * 1024.0));
        fflush(stdout);
        app->want_quit = 1;
        return;
    }

    if (g_fps > 0.0) {
        Uint64 period = (Uint64)(1000000000.0 / g_fps + 0.5);
        Uint64 now    = SDL_GetTicksNS();

        if (!g_due_ns) g_due_ns = now;
        g_due_ns += period;
        if (g_due_ns > now) SDL_DelayNS(g_due_ns - now);
        else                g_due_ns = now;
    }

    {
        SDL_Event e;

        app->dirty = 1;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
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
    out->w = 800;
    out->h = 600;
    out->title = "Bench";
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
    int i;

    (void)app;

#ifdef _WIN32
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

        if ((!out || out == INVALID_HANDLE_VALUE) &&
            AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE *f;

            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }
#endif
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--no-vsync") == 0)
            g_no_vsync = 1;
        else if (i + 1 < argc && SDL_strcmp(argv[i], "--boxes") == 0)
            g_boxes = SDL_atoi(argv[++i]);
        else if (i + 1 < argc && SDL_strcmp(argv[i], "--bench-seconds") == 0)
            g_seconds = SDL_atof(argv[++i]);
        else if (i + 1 < argc && SDL_strcmp(argv[i], "--fps") == 0)
            g_fps = SDL_atof(argv[++i]);
    }
}
