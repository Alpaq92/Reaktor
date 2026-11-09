#include "reaktor/reaktor.h"
#include "reaktor/main.h"

static int           g_frames, g_stale;
static size_t        g_most;
static SDL_AtomicInt g_play;

static void
page(App *app, struct reaktor_context *ctx, int win_w, int win_h)
{
    struct reaktor_anim_context *a = reaktor_anim(app);
    reaktor_hash clip = reaktor_anim_id("hold"), channel = reaktor_anim_id("value");

    (void)win_h;
    g_frames++;
    if (ctx->build) g_stale++;
    if (ctx->memory.allocated > g_most) g_most = ctx->memory.allocated;
    if (!reaktor_anim_clip_exists(a, clip)) {
        reaktor_anim_clip_begin(a, clip);
        reaktor_anim_clip_key_float(a, channel, 0.0f, 0.0f, REAKTOR_ANIM_EASE_LINEAR, NULL);
        reaktor_anim_clip_key_float(a, channel, 0.1f, 1.0f, REAKTOR_ANIM_EASE_LINEAR, NULL);
        reaktor_anim_clip_end(a);
    }
    if (SDL_SetAtomicInt(&g_play, 0))
        reaktor_anim_play_with_delay(a, clip, reaktor_anim_id("tile"), 1.0f);
    REAKTOR_COLUMN(.w = (float)win_w, .gap = 8.0f) {
        REAKTOR_COMBO(.label = "Fruit", .name = "Fruit", .body_h = 120.0f,
                      .box = { .w = 240.0f }) {
            reaktor_combo_item("Apple", 0);
        }
    }
}

static void
script(reaktor_test *t, void *user)
{
    reaktor_diag d;
    size_t most;
    int i, frames;
    Uint64 t0;

    (void)user;
    reaktor_test_click(t, "Fruit");
    reaktor_test_settle(t);
    most = g_most;
    for (i = 0; i < 40; i++) reaktor_test_key(t, "F9");
    reaktor_test_settle(t);
    reaktor_diagnostics(reaktor_test_app_of(t), &d);
    reaktor_test_check(t, d.frames_skipped > 0, "a frame that repeats the last is skipped");
    reaktor_test_check(t, g_stale == 0, "every frame starts from a cleared context");
    reaktor_test_check(t, g_most == most, "skipped frames leave no commands behind");

    frames = g_frames;
    t0 = SDL_GetTicks();
    SDL_SetAtomicInt(&g_play, 1);
    reaktor_test_key(t, "F9");
    SDL_Delay(500);
    reaktor_test_check(t, g_frames - frames < (int)(SDL_GetTicks() - t0), "a busy tween stays paced");
}

int
main(int argc, char **argv)
{
    return reaktor_test_app(argc, argv, &(reaktor_launch){
        .name = "Skiptest",
        .window = { .w = 400, .h = 300 },
        .console = REAKTOR_CONSOLE_PARENT,
        .skip_same_frames = 1,
        .page = page }, script, NULL);
}
