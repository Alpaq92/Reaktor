#ifndef REAKTOR_ANIM_H
#define REAKTOR_ANIM_H

#include "reaktor/anim.h"

struct nka_context *reaktor_anim_create(void);
void reaktor_anim_destroy(struct nka_context *a);

void reaktor_anim_frame(struct nka_context *a, float dt_ms);

int reaktor_anim_settle(void);
void reaktor_anim_reduce(int on);

#endif
