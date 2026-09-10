/* anim.c - see anim.h. */
#include <math.h>

#include <SDL3/SDL.h>

#include "anim.h"

/* Room for every animated value on the busiest page, several times over, and
 * the same failure mode as the other arenas here: a table that is full drops
 * the new entry rather than the frame, so an over-animated page loses an
 * eased value and keeps its layout. Power of two, so the mask below works. */
#define REAKTOR_ANIM_SLOTS 128

typedef struct reaktor_anim_slot {
    unsigned key;          /* 0 means empty */
    float    from, to, value;
    float    elapsed, duration;
    unsigned char curve;
    unsigned char moving;
    unsigned char started;   /* has been asked for at least once */
    /* Set every frame the entry is asked for, cleared by the eviction pass.
     * A page that stops drawing a widget without its node being REMOVED -
     * a tab change, where the whole tree is replaced - would otherwise keep
     * its entries for the life of the process. */
    unsigned char seen;
    unsigned char dead;      /* marked by an eviction pass, swept by compact */
} reaktor_anim_slot;

static reaktor_anim_slot g_slot[REAKTOR_ANIM_SLOTS];
static int               g_live;

/* --- the curves ---------------------------------------------------------
 *
 * Written out rather than generated from a power, because the standard set is
 * not quite a family: the IN_OUT of an odd power keeps its sign and the even
 * ones do not, and BACK, ELASTIC and BOUNCE are not powers at all. The
 * constants are the ones every implementation of these uses - 1.70158 is the
 * overshoot that makes BACK reach exactly 10% past its target. */

#define PI_F 3.14159265358979323846f

static float
pow_in(float t, int n)
{
    float r = t;
    int   i;
    for (i = 1; i < n; i++) r *= t;
    return r;
}

static float
pow_out(float t, int n)
{
    float r = pow_in(1.0f - t, n);
    return 1.0f - r;
}

static float
pow_in_out(float t, int n)
{
    if (t < 0.5f) return pow_in(2.0f * t, n) * 0.5f;
    return 1.0f - pow_in(2.0f * (1.0f - t), n) * 0.5f;
}

static float
bounce_out(float t)
{
    /* Four arcs of a parabola, each shorter and lower than the last. */
    const float n = 7.5625f, d = 2.75f;

    if (t < 1.0f / d)        return n * t * t;
    if (t < 2.0f / d)      { t -= 1.5f / d;   return n * t * t + 0.75f; }
    if (t < 2.5f / d)      { t -= 2.25f / d;  return n * t * t + 0.9375f; }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

float
reaktor_ease_at(unsigned char curve, float t)
{
    const float c1 = 1.70158f;              /* BACK's overshoot */
    const float c2 = c1 * 1.525f;           /* and its IN_OUT variant */
    const float c3 = c1 + 1.0f;
    const float e1 = 2.0f * PI_F / 3.0f;    /* ELASTIC's period */
    const float e2 = 2.0f * PI_F / 4.5f;

    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    switch (curve) {
    case REAKTOR_EASE_LINEAR:        return t;

    case REAKTOR_EASE_QUAD_IN:       return pow_in(t, 2);
    case REAKTOR_EASE_QUAD_OUT:      return pow_out(t, 2);
    case REAKTOR_EASE_QUAD_IN_OUT:   return pow_in_out(t, 2);
    case REAKTOR_EASE_CUBIC_IN:      return pow_in(t, 3);
    case REAKTOR_EASE_CUBIC_OUT:     return pow_out(t, 3);
    case REAKTOR_EASE_CUBIC_IN_OUT:  return pow_in_out(t, 3);
    case REAKTOR_EASE_QUART_IN:      return pow_in(t, 4);
    case REAKTOR_EASE_QUART_OUT:     return pow_out(t, 4);
    case REAKTOR_EASE_QUART_IN_OUT:  return pow_in_out(t, 4);
    case REAKTOR_EASE_QUINT_IN:      return pow_in(t, 5);
    case REAKTOR_EASE_QUINT_OUT:     return pow_out(t, 5);
    case REAKTOR_EASE_QUINT_IN_OUT:  return pow_in_out(t, 5);

    case REAKTOR_EASE_SINE_IN:       return 1.0f - cosf(t * PI_F * 0.5f);
    case REAKTOR_EASE_SINE_OUT:      return sinf(t * PI_F * 0.5f);
    case REAKTOR_EASE_SINE_IN_OUT:   return -(cosf(PI_F * t) - 1.0f) * 0.5f;

    case REAKTOR_EASE_EXPO_IN:       return powf(2.0f, 10.0f * t - 10.0f);
    case REAKTOR_EASE_EXPO_OUT:      return 1.0f - powf(2.0f, -10.0f * t);
    case REAKTOR_EASE_EXPO_IN_OUT:
        return t < 0.5f ? powf(2.0f, 20.0f * t - 10.0f) * 0.5f
                        : (2.0f - powf(2.0f, -20.0f * t + 10.0f)) * 0.5f;

    case REAKTOR_EASE_CIRC_IN:       return 1.0f - sqrtf(1.0f - t * t);
    case REAKTOR_EASE_CIRC_OUT:      return sqrtf(1.0f - (t - 1.0f) * (t - 1.0f));
    case REAKTOR_EASE_CIRC_IN_OUT:
        return t < 0.5f
             ? (1.0f - sqrtf(1.0f - 4.0f * t * t)) * 0.5f
             : (sqrtf(1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f))
                + 1.0f) * 0.5f;

    case REAKTOR_EASE_BACK_IN:       return c3 * t * t * t - c1 * t * t;
    case REAKTOR_EASE_BACK_OUT: {
        float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    case REAKTOR_EASE_BACK_IN_OUT:
        return t < 0.5f
             ? (4.0f * t * t * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
             : ((2.0f * t - 2.0f) * (2.0f * t - 2.0f)
                * ((c2 + 1.0f) * (2.0f * t - 2.0f) + c2) + 2.0f) * 0.5f;

    case REAKTOR_EASE_ELASTIC_IN:
        return -powf(2.0f, 10.0f * t - 10.0f)
             * sinf((t * 10.0f - 10.75f) * e1);
    case REAKTOR_EASE_ELASTIC_OUT:
        return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * e1) + 1.0f;
    case REAKTOR_EASE_ELASTIC_IN_OUT:
        return t < 0.5f
             ? -(powf(2.0f, 20.0f * t - 10.0f)
                 * sinf((20.0f * t - 11.125f) * e2)) * 0.5f
             : (powf(2.0f, -20.0f * t + 10.0f)
                * sinf((20.0f * t - 11.125f) * e2)) * 0.5f + 1.0f;

    case REAKTOR_EASE_BOUNCE_IN:     return 1.0f - bounce_out(1.0f - t);
    case REAKTOR_EASE_BOUNCE_OUT:    return bounce_out(t);
    case REAKTOR_EASE_BOUNCE_IN_OUT:
        return t < 0.5f ? (1.0f - bounce_out(1.0f - 2.0f * t)) * 0.5f
                        : (1.0f + bounce_out(2.0f * t - 1.0f)) * 0.5f;

    default:                         return t;
    }
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

/* --- the table ---------------------------------------------------------- */

/* One widget may animate several numbers, so the key is the node's id and the
 * channel mixed. Zero is reserved for "empty", so a key that lands there is
 * nudged rather than rejected - one collision is cheaper than a branch on
 * every lookup for a value the hash reaches once in four billion. */
static unsigned
key_of(unsigned id, unsigned channel)
{
    unsigned k = id * 2654435761u + channel * 0x9e3779b9u;
    k ^= k >> 15;
    return k ? k : 1u;
}

static reaktor_anim_slot *
slot_of(unsigned key, int make)
{
    unsigned i = key & (REAKTOR_ANIM_SLOTS - 1);
    unsigned n;

    for (n = 0; n < REAKTOR_ANIM_SLOTS; n++) {
        reaktor_anim_slot *s = &g_slot[i];

        if (s->key == key) return s;
        if (!s->key) {
            if (!make) return NULL;
            s->key = key;
            g_live++;
            return s;
        }
        i = (i + 1) & (REAKTOR_ANIM_SLOTS - 1);
    }
    return NULL;   /* full: the caller gets its target and no easing */
}

/* Open addressing cannot simply blank a slot. A key that probed past it on
 * the way in becomes unreachable behind the hole, and the next lookup walks
 * to the hole, decides the entry is absent and makes a second one for the
 * same thing - which then animates independently of the first. So a pass that
 * removes anything rebuilds the table instead. 128 slots, at most once a
 * frame, and only on the frames where something actually went. */
static void
compact(void)
{
    reaktor_anim_slot old[REAKTOR_ANIM_SLOTS];
    int i;

    SDL_memcpy(old, g_slot, sizeof(old));
    SDL_memset(g_slot, 0, sizeof(g_slot));
    g_live = 0;
    for (i = 0; i < REAKTOR_ANIM_SLOTS; i++) {
        reaktor_anim_slot *s;

        if (!old[i].key || old[i].dead) continue;
        s = slot_of(old[i].key, 1);
        if (s) *s = old[i];
    }
}

/* Where a run is along its curve, 0 to 1, or -1 when nothing is in flight.
 * For anything that wants to show the run rather than be moved by it - the
 * sample's plot marks the head with it. */
float
reaktor_anim_progress(unsigned id, unsigned channel)
{
    reaktor_anim_slot *s = slot_of(key_of(id, channel), 0);

    if (!s || !s->moving || s->duration <= 0.0f) return -1.0f;
    return s->elapsed / s->duration;
}

float
reaktor_animate(unsigned id, unsigned channel, float to, float ms,
                unsigned char curve)
{
    reaktor_anim_slot *s;
    unsigned           key;

    if (ms <= 0.0f) return to;

    key = key_of(id, channel);
    s = slot_of(key, 1);
    if (!s) return to;           /* table full */
    s->seen = 1;

    /* First sight: start at the target rather than at zero, so a widget does
     * not fly in the first time it is drawn. An animation is a *change*, and
     * nothing has changed yet. */
    if (!s->started) {
        s->started = 1;
        s->from = s->to = s->value = to;
        s->curve = curve;
        return to;
    }

    /* A new target starts a run from wherever the value is now, which is what
     * makes a target that changes mid-flight redirect rather than restart. */
    if (to != s->to) {
        s->from     = s->value;
        s->to       = to;
        s->elapsed  = 0.0f;
        s->duration = ms;
        s->curve    = curve;
        s->moving   = 1;
    }
    return s->value;
}

int
reaktor_anim_tick(float dt_ms)
{
    int i, moving = 0;

    if (dt_ms < 0.0f) dt_ms = 0.0f;
    /* A frame that arrives after a long wait - the app sleeps in
     * SDL_WaitEvent - must not teleport an animation that was mid-flight. It
     * is capped at roughly four frames of 60Hz, which is long enough that a
     * busy frame still advances honestly and short enough that a gap does
     * not skip the whole run. */
    if (dt_ms > 64.0f) dt_ms = 64.0f;

    for (i = 0; i < REAKTOR_ANIM_SLOTS; i++) {
        reaktor_anim_slot *s = &g_slot[i];

        if (!s->key || !s->moving) continue;
        s->elapsed += dt_ms;
        if (s->elapsed >= s->duration) {
            s->elapsed = s->duration;
            s->value   = s->to;
            s->moving  = 0;
        } else {
            float t = s->elapsed / s->duration;
            s->value = s->from
                     + (s->to - s->from) * reaktor_ease_at(s->curve, t);
            moving++;
        }
    }
    return moving;
}

void
reaktor_anim_evict(const reaktor_a11y_change *c, int n)
{
    int i, removed = 0;

    for (i = 0; c && i < n; i++) {
        unsigned ch;

        if (c[i].kind != REAKTOR_A11Y_REMOVED) continue;
        /* Every channel of a node that is gone. Sixteen is more than any one
         * widget animates, and cheaper than a second index from id to slots.
         * Marked rather than blanked, so the lookups after it in this same
         * loop still walk an intact table. */
        for (ch = 0; ch < 16u; ch++) {
            reaktor_anim_slot *s = slot_of(key_of(c[i].id, ch), 0);
            if (s && !s->dead) { s->dead = 1; removed++; }
        }
    }

    /* And the ones nothing asked for this frame. A page swap replaces the
     * whole tree at once, which the diff reports - but a widget that simply
     * stops being drawn inside a page that stays does not, and its entry
     * would otherwise live for the life of the process. */
    for (i = 0; i < REAKTOR_ANIM_SLOTS; i++) {
        reaktor_anim_slot *s = &g_slot[i];

        if (!s->key || s->dead) continue;
        if (!s->seen) { s->dead = 1; removed++; continue; }
        s->seen = 0;
    }

    if (removed) compact();
}

int
reaktor_anim_live(void)
{
    return g_live;
}
