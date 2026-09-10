/* anim.h - a number that takes time to change.
 *
 * An immediate-mode frame has nowhere to keep one. It computes what a widget
 * looks like now and forgets, which is the property that makes the rest of
 * this library work and the one property an animation cannot have: something
 * has to remember that a value was 0.2 last frame and is on its way to 1.
 *
 * So the memory lives here, keyed by the accessibility id - the same identity
 * the layout shim uses to find last frame's rect, and the same one the
 * platform bridges use to name a node. One identity, three consumers, and
 * nothing here computes it. A caller passes the id its widget was given.
 *
 * That key is the one place this differs from the prior art it was read
 * against. RaidcoreGG/ImAnimate does the same job for Dear ImGui in two files
 * and keys on `&value` - the address of the caller's float. In immediate mode
 * that address is often a stack slot, and the next frame's widget may own the
 * same one; two animations then share an entry and neither is right. An id
 * derived from the shape of the tree cannot collide that way.
 *
 * Lifetimes:
 *
 *   the layout table  is rebuilt every frame - a box is declared, placed, and
 *                     forgotten.
 *   this table        outlives frames but not elements. An entry is born when
 *                     a value is first eased and dies when the accessibility
 *                     diff says its node is REMOVED, which is a signal that
 *                     already exists and costs nothing to reuse.
 *
 * Staying awake: an animation in flight asks for the next frame the way a
 * hover does - it marks the frame dirty and pushes an event. At rest it asks
 * for nothing, which is what keeps a still page at zero frames a second.
 */
#ifndef REAKTOR_ANIM_H
#define REAKTOR_ANIM_H

#include "a11y.h"

/* The curves, in the shape everyone spells them: a polynomial family in three
 * directions each, plus the three that are not polynomials.
 *
 * IN accelerates from rest, OUT decelerates into rest, IN_OUT does both. For
 * a thing appearing, OUT is almost always right - it arrives calm - and for a
 * thing leaving, IN. A UI that eases everything IN_OUT reads as sluggish,
 * because the slow middle is where the eye is looking. */
enum {
    REAKTOR_EASE_LINEAR = 0,

    REAKTOR_EASE_QUAD_IN,   REAKTOR_EASE_QUAD_OUT,   REAKTOR_EASE_QUAD_IN_OUT,
    REAKTOR_EASE_CUBIC_IN,  REAKTOR_EASE_CUBIC_OUT,  REAKTOR_EASE_CUBIC_IN_OUT,
    REAKTOR_EASE_QUART_IN,  REAKTOR_EASE_QUART_OUT,  REAKTOR_EASE_QUART_IN_OUT,
    REAKTOR_EASE_QUINT_IN,  REAKTOR_EASE_QUINT_OUT,  REAKTOR_EASE_QUINT_IN_OUT,

    REAKTOR_EASE_SINE_IN,   REAKTOR_EASE_SINE_OUT,   REAKTOR_EASE_SINE_IN_OUT,
    REAKTOR_EASE_EXPO_IN,   REAKTOR_EASE_EXPO_OUT,   REAKTOR_EASE_EXPO_IN_OUT,
    REAKTOR_EASE_CIRC_IN,   REAKTOR_EASE_CIRC_OUT,   REAKTOR_EASE_CIRC_IN_OUT,

    /* These three leave the 0..1 range on the way, which is the point of
     * them: BACK undershoots then overshoots, ELASTIC oscillates into place,
     * BOUNCE lands and rebounds. Anything laid out from an eased value has to
     * tolerate that, so they are worth knowing about before they are used. */
    REAKTOR_EASE_BACK_IN,   REAKTOR_EASE_BACK_OUT,   REAKTOR_EASE_BACK_IN_OUT,
    REAKTOR_EASE_ELASTIC_IN, REAKTOR_EASE_ELASTIC_OUT,
    REAKTOR_EASE_ELASTIC_IN_OUT,
    REAKTOR_EASE_BOUNCE_IN, REAKTOR_EASE_BOUNCE_OUT,
    REAKTOR_EASE_BOUNCE_IN_OUT,

    REAKTOR_EASE_COUNT
};

/* `t` from 0 to 1, answered on the curve. Pure, and exposed because a page
 * that wants to draw a curve should not have to reimplement it - the sample's
 * animation page draws all thirty from this. */
float reaktor_ease_at(unsigned char curve, float t);
const char *reaktor_ease_name(unsigned char curve);

/* The value for `id` on its way to `to`, over `ms`, along `curve`.
 *
 * Answers where it is this frame. The first call with a given key starts
 * there and stays there - a value has to begin somewhere, and beginning at
 * the target means a widget does not fly in from zero the first time it is
 * drawn. Changing `to` afterwards is what starts a run: it eases from
 * wherever it currently is, so a target that changes mid-flight redirects
 * rather than restarts.
 *
 * `channel` distinguishes several animations on one widget - a fade and a
 * slide are two numbers and one id. Pass 0 when there is only one. */
float reaktor_animate(unsigned id, unsigned channel, float to,
                      float ms, unsigned char curve);

/* Where a run is along its curve, 0 to 1, or -1 when nothing is in flight.
 * For anything that wants to show a run rather than be moved by it. */
float reaktor_anim_progress(unsigned id, unsigned channel);

/* Called by the frame, not by a page. `dt_ms` advances every run in flight;
 * answers how many are still moving, which is what decides whether the frame
 * asks for another. */
int reaktor_anim_tick(float dt_ms);

/* Drops the entries whose nodes the accessibility diff says are gone, and the
 * ones nothing asked for this frame. Called once a frame, after the diff.
 *
 * Handed the change list rather than the App it came from: this file knows
 * about identities and numbers, and nothing else. That is also what lets it
 * be tested without a window. */
void reaktor_anim_evict(const reaktor_a11y_change *changes, int n);

/* How many entries are live, for the diagnostics page. */
int reaktor_anim_live(void);

#endif /* REAKTOR_ANIM_H */
