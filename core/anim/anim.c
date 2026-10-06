#include <SDL3/SDL.h>

#include "reaktor/nuklear.h"
#define NUKANIM_IMPLEMENTATION
#include <nukanim.h>

#include "anim.h"

typedef char reaktor_curves_are_nukanims[(unsigned)REAKTOR_EASE_BOUNCE_IN_OUT == (unsigned)NKA_EASE_IN_OUT_BOUNCE ? 1 : -1];

static struct nka_context *g_anim;

static void *
anim_alloc(nk_handle h, void *old, nk_size n)
{
    (void)h;
    (void)old;
    return SDL_malloc(n);
}

static void
anim_free(nk_handle h, void *p)
{
    (void)h;
    SDL_free(p);
}

struct nka_context *
reaktor_anim_create(void)
{
    struct nk_allocator al;
    struct nka_context *a;

    al.userdata.ptr = NULL;
    al.alloc = anim_alloc;
    al.free = anim_free;
    a = nka_create(&al);
    /* A value starts where it is first asked to be, so it must be kept. */
    if (a) nka_set_lazy_init(a, 0);
    return a;
}

void
reaktor_anim_destroy(struct nka_context *a)
{
    if (g_anim == a) g_anim = NULL;
    nka_destroy(a);
}

void
reaktor_anim_frame(struct nka_context *a, float dt_ms)
{
    g_anim = a;
    if (dt_ms < 0.0f) dt_ms = 0.0f;
    if (dt_ms > 64.0f) dt_ms = 64.0f;
    nka_update(a, dt_ms / 1000.0f);
}

int
reaktor_anim_settle(void)
{
    if (!g_anim) return 0;
    nka_gc(g_anim, 600);
    return nka_busy(g_anim);
}

float
reaktor_ease_at(unsigned char curve, float t)
{
    return nka_eval_preset(curve, t);
}

const char *
reaktor_ease_name(unsigned char curve)
{
    static const char *const n[REAKTOR_EASE_COUNT] = {
        "linear",
        "quad in", "quad out", "quad in-out",
        "cubic in", "cubic out", "cubic in-out",
        "quart in", "quart out", "quart in-out",
        "quint in", "quint out", "quint in-out",
        "sine in", "sine out", "sine in-out",
        "expo in", "expo out", "expo in-out",
        "circ in", "circ out", "circ in-out",
        "back in", "back out", "back in-out",
        "elastic in", "elastic out", "elastic in-out",
        "bounce in", "bounce out", "bounce in-out"
    };
    return curve < REAKTOR_EASE_COUNT ? n[curve] : "?";
}

float
reaktor_animate(unsigned id, unsigned channel, float to, float ms,
                unsigned char curve)
{
    if (ms <= 0.0f || !g_anim) return to;
    return nka_tween_float(g_anim, id, channel, to, ms / 1000.0f,
                           nka_ease(curve), NKA_POLICY_CROSSFADE, to);
}

float
reaktor_anim_progress(unsigned id, unsigned channel)
{
    return g_anim ? nka_tween_progress(g_anim, id, channel) : -1.0f;
}
