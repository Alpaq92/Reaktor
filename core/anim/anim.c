#include <math.h>

#include <SDL3/SDL.h>

#include "anim.h"

#define REAKTOR_ANIM_SLOTS 128

typedef struct reaktor_anim_slot {
    unsigned key;
    float    from, to, value;
    float    elapsed, duration;
    unsigned char curve;
    unsigned char moving;
    unsigned char started;
    unsigned char seen;
    unsigned char dead;
} reaktor_anim_slot;

static reaktor_anim_slot g_slot[REAKTOR_ANIM_SLOTS];

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
    const float c1 = 1.70158f;
    const float c2 = c1 * 1.525f;
    const float c3 = c1 + 1.0f;
    const float e1 = 2.0f * PI_F / 3.0f;
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
            return s;
        }
        i = (i + 1) & (REAKTOR_ANIM_SLOTS - 1);
    }
    return NULL;
}

static void
compact(void)
{
    reaktor_anim_slot old[REAKTOR_ANIM_SLOTS];
    int i;

    SDL_memcpy(old, g_slot, sizeof(old));
    SDL_memset(g_slot, 0, sizeof(g_slot));
    for (i = 0; i < REAKTOR_ANIM_SLOTS; i++) {
        reaktor_anim_slot *s;

        if (!old[i].key || old[i].dead) continue;
        s = slot_of(old[i].key, 1);
        if (s) *s = old[i];
    }
}

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
    if (!s) return to;
    s->seen = 1;

    if (!s->started) {
        s->started = 1;
        s->from = s->to = s->value = to;
        s->curve = curve;
        return to;
    }

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
        for (ch = 0; ch < 16u; ch++) {
            reaktor_anim_slot *s = slot_of(key_of(c[i].id, ch), 0);
            if (s && !s->dead) { s->dead = 1; removed++; }
        }
    }

    for (i = 0; i < REAKTOR_ANIM_SLOTS; i++) {
        reaktor_anim_slot *s = &g_slot[i];

        if (!s->key || s->dead) continue;
        if (!s->seen) { s->dead = 1; removed++; continue; }
        s->seen = 0;
    }

    if (removed) compact();
}
