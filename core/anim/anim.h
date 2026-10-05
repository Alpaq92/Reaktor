#ifndef REAKTOR_ANIM_H
#define REAKTOR_ANIM_H

#include "a11y.h"
#include "reaktor/anim.h"

void reaktor_anim_scope(const void *owner);

int reaktor_anim_tick(float dt_ms);

void reaktor_anim_evict(const reaktor_a11y_change *changes, int n);

#endif
