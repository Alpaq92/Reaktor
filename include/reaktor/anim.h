#ifndef REAKTOR_ANIM_PUBLIC_H
#define REAKTOR_ANIM_PUBLIC_H

#include "reaktor/nuklear.h"
#include <nukanim.h>

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

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

/* The window's NukAnim context, for all of nukanim.h; its time moves every frame. */
struct nka_context *reaktor_anim(App *app);

#endif
