/* a11y_snapshot.h - the tree, copied for a platform that reads it on another
 * thread, and the way that platform's requests get back to this one.
 *
 * Every native bridge needs the same two things and for the same reason. A
 * client asks its questions whenever it likes, on a thread the app never
 * created - UIA's RPC thread, D-Bus's dispatch, Cocoa's run loop - while the
 * app's own thread may be asleep in SDL_WaitEvent and the frame rewrites the
 * shadow tree from scratch. So the tree cannot be read where it lives, and a
 * request cannot be answered where it arrives.
 *
 * This is that, once, portably: a copy taken by each drawn frame with its own
 * string arena (the model's strings live in an arena the next frame reuses),
 * and a pair of slots a client's thread writes and the app's thread drains.
 *
 * **No call here hands out anything that outlives the lock.** A node is
 * copied into the caller's buffer; navigation and hit testing answer with an
 * id. A bridge therefore never takes the lock, never holds a pointer into the
 * snapshot, and cannot get either wrong - which matters most for the bridges
 * that are written on one platform and run on another. */
#ifndef REAKTOR_A11Y_SNAPSHOT_H
#define REAKTOR_A11Y_SNAPSHOT_H

#include "a11y.h"

/* One node, copied out. `name` and `value` point into the buffer the caller
 * passed, or are NULL; everything else is by value. */
typedef struct reaktor_snap_node {
    unsigned      id, parent;
    unsigned char role, level;
    unsigned      state;
    const char   *name, *value;
    float         x, y, w, h;        /* window coordinates */
    /* Zero-width - lo == hi - means the node is not a range. */
    float         num, lo, hi, step;
} reaktor_snap_node;

/* Enough for any name or value the app produces; a longer one is truncated
 * rather than refused, because a clipped label reads better than none. */
#define REAKTOR_SNAP_TEXT 512

/* Called once, from the app's thread, with the callbacks a client's press and
 * a client's focus move turn into. Answers 0 if the lock could not be made,
 * in which case every call below is a no-op and the bridge should stand down. */
int  reaktor_snap_init(reaktor_a11y_action activate, reaktor_a11y_action focus,
                       void *user);

/* Takes the copy. From the app's thread, after each drawn frame. Cheap when
 * nothing changed: it looks at the change count and returns. Answers whether
 * it copied, which is what a bridge waits for before raising an event. */
int  reaktor_snap_update(const reaktor_a11y *a, unsigned focus_id);

/* Copies the node `id` names. Answers 0 if it is not in the snapshot - a node
 * a client is still holding after the page it was on went away. */
int  reaktor_snap_get(unsigned id, reaktor_snap_node *out, char *buf,
                      size_t cap);

/* Navigation, in the tree's own order, which is draw order and so reading
 * order. `parent` of 0 means the roots. All answer an id, or 0. */
unsigned reaktor_snap_child(unsigned parent, int last);
unsigned reaktor_snap_sibling(unsigned id, int back);
unsigned reaktor_snap_parent(unsigned id);

/* The innermost node containing a point in window coordinates, or 0. Depth
 * first, and among equals the last drawn: a group's bounds cover its children,
 * so the last containing node is not the same as the deepest one. */
unsigned reaktor_snap_hit(float x, float y);

unsigned reaktor_snap_focus(void);

/* Whether a role takes the keyboard - the same set the shell tabs through, so
 * what a platform reports focusable is what Tab actually reaches. */
int reaktor_snap_focusable(unsigned char role, unsigned state);

/* A client's request, from the client's thread. Records it and wakes the
 * loop; the app's thread runs it in reaktor_snap_drain before the next frame.
 * One of each is enough - a second before the first is served is the client
 * changing its mind, and the last one is what it wants. */
void reaktor_snap_request_focus(unsigned id);
void reaktor_snap_request_activate(unsigned id);
void reaktor_snap_drain(void);

#endif /* REAKTOR_A11Y_SNAPSHOT_H */
