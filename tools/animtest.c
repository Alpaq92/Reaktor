#include <stdio.h>

#include <SDL3/SDL.h>

#include "anim.h"

static int g_fail;
static int g_moving;

static void
ok(const char *what, int cond)
{
    printf("  %-56s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond) g_fail = 1;
}

/* A frame of a page that shows one value: time moves, the page asks, the runtime settles. */
static float
frame(struct nka_context *a, unsigned id, unsigned channel, float to, float ms,
      unsigned char curve, float gap_ms)
{
    float v;
    reaktor_anim_frame(a, gap_ms);
    v = reaktor_animate(id, channel, to, ms, curve);
    g_moving = reaktor_anim_settle();
    return v;
}

/* Frames 16 ms apart until total has passed. */
static float
frames(struct nka_context *a, unsigned id, unsigned channel, float to, float ms,
       unsigned char curve, float total)
{
    float v = 0.0f;
    while (total > 0.0f) {
        float step = total > 16.0f ? 16.0f : total;
        v = frame(a, id, channel, to, ms, curve, step);
        total -= step;
    }
    return v;
}

static int
near(float a, float b)
{
    float d = a - b;
    return d < 0.001f && d > -0.001f;
}

static int
overshoots(int c)
{
    return c >= REAKTOR_EASE_BACK_IN && c <= REAKTOR_EASE_BOUNCE_IN_OUT;
}

static void
test_curves(void)
{
    int c, i;
    int ends_wrong = 0, strayed = 0, should_stray = 0, named = 0;

    puts("");
    puts("every curve starts at 0 and ends at 1");
    for (c = 0; c < REAKTOR_EASE_COUNT; c++) {
        if (!near(reaktor_ease_at((unsigned char)c, 0.0f), 0.0f) ||
            !near(reaktor_ease_at((unsigned char)c, 1.0f), 1.0f))
            ends_wrong++;
        if (reaktor_ease_name((unsigned char)c)[0] != '?') named++;
    }
    ok("all thirty-one of them", ends_wrong == 0);
    ok("and every one has a name", named == REAKTOR_EASE_COUNT);

    puts("");
    puts("only the three that are meant to overshoot do");
    for (c = 0; c < REAKTOR_EASE_COUNT; c++) {
        int out = 0;
        for (i = 1; i < 64; i++) {
            float v = reaktor_ease_at((unsigned char)c, (float)i / 64.0f);
            if (v < -0.0005f || v > 1.0005f) out = 1;
        }
        if (out && !overshoots(c)) strayed++;
        if (out && overshoots(c)) should_stray++;
    }
    ok("no ordinary curve leaves the range", strayed == 0);
    ok("and back, elastic and bounce all do", should_stray >= 6);

    puts("");
    puts("a curve moves in one direction unless it is one of those three");
    {
        int backwards = 0;
        for (c = 0; c < REAKTOR_EASE_COUNT; c++) {
            float prev = 0.0f;
            if (overshoots(c)) continue;
            for (i = 0; i <= 64; i++) {
                float v = reaktor_ease_at((unsigned char)c, (float)i / 64.0f);
                if (v < prev - 0.0005f) backwards++;
                prev = v;
            }
        }
        ok("never turns back on itself", backwards == 0);
    }

    puts("");
    puts("in and out are each other's reflection");
    {
        int pairs[] = {
            REAKTOR_EASE_QUAD_IN, REAKTOR_EASE_CUBIC_IN, REAKTOR_EASE_QUART_IN,
            REAKTOR_EASE_QUINT_IN, REAKTOR_EASE_SINE_IN, REAKTOR_EASE_CIRC_IN
        };
        int bad = 0, p;
        for (p = 0; p < (int)(sizeof(pairs) / sizeof(pairs[0])); p++)
            for (i = 1; i < 64; i++) {
                float t = (float)i / 64.0f;
                float a = reaktor_ease_at((unsigned char)pairs[p], t);
                float b = reaktor_ease_at((unsigned char)(pairs[p] + 1),
                                          1.0f - t);
                if (!near(a, 1.0f - b)) bad++;
            }
        ok("for quad, cubic, quart, quint, sine and circ", bad == 0);
    }
}

static void
test_table(void)
{
    const unsigned id = 4242u;
    const unsigned char lin = REAKTOR_EASE_LINEAR;
    struct nka_context *a = reaktor_anim_create();
    float v;
    int n;

    puts("");
    puts("a value starts where it is asked to, not at zero");
    v = frame(a, id, 0, 0.75f, 200.0f, lin, 16.0f);
    ok("the first sight of it is its target", near(v, 0.75f));
    ok("and nothing is moving", !g_moving);

    puts("");
    puts("a new target starts a run, and the run ends");
    v = frame(a, id, 0, 0.0f, 200.0f, lin, 0.0f);
    ok("the frame that asks still sees the old value", near(v, 0.75f));
    ok("something is moving now", g_moving);
    v = frames(a, id, 0, 0.0f, 200.0f, lin, 100.0f);
    ok("half way through a linear run is half way there", near(v, 0.375f));
    ok("and the dot on its curve says so", near(reaktor_anim_progress(id, 0), 0.5f));
    v = frames(a, id, 0, 0.0f, 200.0f, lin, 100.0f);
    ok("the frame that lands shows it arrived exactly", near(v, 0.0f));
    ok("and asks for no more", !g_moving && reaktor_anim_progress(id, 0) < 0.0f);

    puts("");
    puts("a target that changes mid-flight redirects rather than restarts");
    frame(a, id, 0, 1.0f, 200.0f, lin, 0.0f);
    v = frames(a, id, 0, 1.0f, 200.0f, lin, 100.0f);
    ok("half way to 1", near(v, 0.5f));
    frame(a, id, 0, 0.0f, 200.0f, lin, 0.0f);
    v = frames(a, id, 0, 0.0f, 200.0f, lin, 100.0f);
    ok("and half way back from there, not from the end", near(v, 0.25f));

    puts("");
    puts("a channel is a separate value on the same widget");
    v = frame(a, id, 1, 10.0f, 200.0f, lin, 16.0f);
    ok("channel 1 starts at its own target", near(v, 10.0f));
    reaktor_anim_frame(a, 0.0f);
    v = reaktor_animate(id, 0, 0.0f, 200.0f, lin);
    ok("and channel 0 is still on its way", v > 0.0f && v < 0.25f);

    puts("");
    puts("a long gap between frames does not teleport a run");
    {
        float start = frame(a, id, 0, 1.0f, 1000.0f, lin, 16.0f);

        v = frame(a, id, 0, 1.0f, 1000.0f, lin, 5000.0f);
        ok("five seconds of sleep move it forward", v > start);
        ok("...by one capped step, not by all five seconds", v - start < 0.10f);
        ok("and the run is still going", g_moving);
        for (n = 0; g_moving && n < 200; n++)
            v = frame(a, id, 0, 1.0f, 1000.0f, lin, 16.0f);
        ok("it does finish, given the frames", !g_moving && n < 200);
        ok("exactly where it was sent", near(v, 1.0f));
    }

    puts("");
    puts("a settled value holds still when only its curve changes");
    v = frame(a, id, 0, 1.0f, 300.0f, REAKTOR_EASE_BOUNCE_OUT, 16.0f);
    ok("it stays where it is", near(v, 1.0f) && !g_moving);
    ok("with no dot running on its curve", reaktor_anim_progress(id, 0) < 0.0f);
    frame(a, id, 0, 0.0f, 300.0f, REAKTOR_EASE_BOUNCE_OUT, 0.0f);
    v = frames(a, id, 0, 0.0f, 300.0f, REAKTOR_EASE_BOUNCE_OUT, 150.0f);
    ok("and the next run takes the new curve",
       near(v, 1.0f - reaktor_ease_at(REAKTOR_EASE_BOUNCE_OUT, 0.5f)));

    puts("");
    puts("each window has a table of its own");
    {
        struct nka_context *b = reaktor_anim_create();

        v = frame(b, id, 0, 42.0f, 200.0f, lin, 16.0f);
        ok("the same id starts afresh in another", near(v, 42.0f));
        v = frame(a, id, 0, 0.0f, 300.0f, REAKTOR_EASE_BOUNCE_OUT, 0.0f);
        ok("and the first one is left as it was", v > 0.0f && v < 1.0f);
        reaktor_anim_destroy(b);
    }
    reaktor_anim_destroy(a);
    ok("a destroyed table leaves reaktor_animate answering its target",
       near(reaktor_animate(id, 0, 3.0f, 200.0f, lin), 3.0f));
}

int
main(void)
{
    test_curves();
    test_table();

    printf("\n%s\n", g_fail ? "FAILED" : "anim: all checks passed");
    return g_fail ? 1 : 0;
}
