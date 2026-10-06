#ifndef REAKTOR_ANIM_H
#define REAKTOR_ANIM_H

#include "reaktor/anim.h"

struct nka_context *reaktor_anim_create(void);
void reaktor_anim_destroy(struct nka_context *a);

/* Before the page: makes a the context reaktor_animate uses and moves its time on. */
void reaktor_anim_frame(struct nka_context *a, float dt_ms);

/* After the page: nonzero while something it drew is still moving. */
int reaktor_anim_settle(void);

#endif
