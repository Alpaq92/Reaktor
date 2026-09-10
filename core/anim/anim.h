#ifndef REAKTOR_ANIM_H
#define REAKTOR_ANIM_H

#include "a11y.h"

enum {
    REAKTOR_EASE_LINEAR = 0,

    REAKTOR_EASE_QUAD_IN,   REAKTOR_EASE_QUAD_OUT,   REAKTOR_EASE_QUAD_IN_OUT,
    REAKTOR_EASE_CUBIC_IN,  REAKTOR_EASE_CUBIC_OUT,  REAKTOR_EASE_CUBIC_IN_OUT,
    REAKTOR_EASE_QUART_IN,  REAKTOR_EASE_QUART_OUT,  REAKTOR_EASE_QUART_IN_OUT,
    REAKTOR_EASE_QUINT_IN,  REAKTOR_EASE_QUINT_OUT,  REAKTOR_EASE_QUINT_IN_OUT,

    REAKTOR_EASE_SINE_IN,   REAKTOR_EASE_SINE_OUT,   REAKTOR_EASE_SINE_IN_OUT,
    REAKTOR_EASE_EXPO_IN,   REAKTOR_EASE_EXPO_OUT,   REAKTOR_EASE_EXPO_IN_OUT,
    REAKTOR_EASE_CIRC_IN,   REAKTOR_EASE_CIRC_OUT,   REAKTOR_EASE_CIRC_IN_OUT,

    REAKTOR_EASE_BACK_IN,   REAKTOR_EASE_BACK_OUT,   REAKTOR_EASE_BACK_IN_OUT,
    REAKTOR_EASE_ELASTIC_IN, REAKTOR_EASE_ELASTIC_OUT,
    REAKTOR_EASE_ELASTIC_IN_OUT,
    REAKTOR_EASE_BOUNCE_IN, REAKTOR_EASE_BOUNCE_OUT,
    REAKTOR_EASE_BOUNCE_IN_OUT,

    REAKTOR_EASE_COUNT
};

float reaktor_ease_at(unsigned char curve, float t);
const char *reaktor_ease_name(unsigned char curve);

float reaktor_animate(unsigned id, unsigned channel, float to,
                      float ms, unsigned char curve);

float reaktor_anim_progress(unsigned id, unsigned channel);

int reaktor_anim_tick(float dt_ms);

void reaktor_anim_evict(const reaktor_a11y_change *changes, int n);


#endif
