#include <stdio.h>

#include <SDL3/SDL.h>

#include "anim.h"

static int g_fail;

static void
ok(const char *what, int cond)
{
    printf("  %-56s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond) g_fail = 1;
}

static int
advance(float ms)
{
    int moving = 0;
    while (ms > 0.0f) {
        float step = ms > 16.0f ? 16.0f : ms;
        moving = reaktor_anim_tick(step);
        ms -= step;
    }
    return moving;
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
    float v;

    puts("");
    puts("a value starts where it is asked to, not at zero");
    v = reaktor_animate(id, 0, 0.75f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("the first sight of it is its target", near(v, 0.75f));
    ok("and nothing is moving", advance(16.0f) == 0);

    puts("");
    puts("a new target starts a run, and the run ends");
    v = reaktor_animate(id, 0, 0.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("the frame that asks still sees the old value", near(v, 0.75f));
    ok("something is moving now", advance(100.0f) == 1);
    v = reaktor_animate(id, 0, 0.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("half way through a linear run is half way there",
       near(v, 0.375f));
    ok("the rest of it finishes", advance(100.0f) == 0);
    v = reaktor_animate(id, 0, 0.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("and it arrives exactly", near(v, 0.0f));

    puts("");
    puts("a target that changes mid-flight redirects rather than restarts");
    reaktor_animate(id, 0, 1.0f, 200.0f, REAKTOR_EASE_LINEAR);
    advance(100.0f);
    v = reaktor_animate(id, 0, 1.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("half way to 1", near(v, 0.5f));
    reaktor_animate(id, 0, 0.0f, 200.0f, REAKTOR_EASE_LINEAR);
    advance(100.0f);
    v = reaktor_animate(id, 0, 0.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("and half way back from there, not from the end", near(v, 0.25f));

    puts("");
    puts("a channel is a separate value on the same widget");
    reaktor_animate(id, 1, 10.0f, 200.0f, REAKTOR_EASE_LINEAR);
    v = reaktor_animate(id, 1, 10.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("channel 1 starts at its own target", near(v, 10.0f));
    v = reaktor_animate(id, 0, 0.0f, 200.0f, REAKTOR_EASE_LINEAR);
    ok("and channel 0 is where it was left", near(v, 0.25f));

    puts("");
    puts("a long gap between frames does not teleport a run");
    {
        float start = reaktor_animate(id, 0, 1.0f, 1000.0f,
                                      REAKTOR_EASE_LINEAR);

        reaktor_anim_tick(5000.0f);
        v = reaktor_animate(id, 0, 1.0f, 1000.0f, REAKTOR_EASE_LINEAR);
        ok("five seconds of sleep move it forward", v > start);
        ok("...by one capped step, not by all five seconds",
           v - start < 0.10f);
        ok("and the run is still going", advance(16.0f) == 1);
        ok("it does finish, given the frames", advance(1000.0f) == 0);
        v = reaktor_animate(id, 0, 1.0f, 1000.0f, REAKTOR_EASE_LINEAR);
        ok("exactly where it was sent", near(v, 1.0f));
    }
}

int
main(void)
{
    test_curves();
    test_table();

    printf("\n%s\n", g_fail ? "FAILED" : "anim: all checks passed");
    return g_fail ? 1 : 0;
}
