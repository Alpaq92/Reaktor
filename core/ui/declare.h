/* declare.h - saying what the screen holds, instead of drawing it.
 *
 * A container is two things: what it is, and what is in it. That is Capy's
 * grammar, and C99 spells it natively - a compound literal for the first, a
 * scope for the second:
 *
 *     REAKTOR_COLUMN(.gap = 10) {
 *         REAKTOR_ROW(.gap = 5) {
 *             if (reaktor_button(&(reaktor_button_spec){ .label = "Save" }))
 *                 save_to(&doc);
 *         }
 *         reaktor_label(&(reaktor_label_spec){ .text = doc.title });
 *     }
 *
 * That `if` is why there are no callbacks here. A retained toolkit has to hand
 * you one: its widget outlives the code that built it, so the click arrives
 * somewhere else, later, with a void * to find its way home. This widget *is*
 * that code. Its answer is a return value, and the enclosing scope - locals,
 * loop variables and all - is still on the stack when it comes back. Nothing
 * a callback can carry beats that.
 *
 * Sizes are optional because a widget knows its own: a label is as wide as its
 * text in the font the stylesheet chose, and `.label = "Save"` is a whole
 * button. Give a size only where you mean to override one.
 *
 * ONE RULE THE COMPILER CANNOT ENFORCE: do not `return`, `break` or `goto` out
 * of a container scope. C has no defer, so the closing call would be skipped
 * and the tree left open. It is the same contract nk_begin/nk_end already
 * impose on every page in this tree, and it fails the same way - loudly, on
 * the next frame, not silently.
 */
#ifndef REAKTOR_DECLARE_H
#define REAKTOR_DECLARE_H

#include "layout.h"

typedef struct App App;

/* --- the frame ---------------------------------------------------------- */

/* One screen is being described at a time - that is what immediate mode
 * means - so the frame's context is held here rather than threaded through
 * every call. Passing it everywhere would put the plumbing back into the page,
 * which is the thing being taken out. The runtime opens and closes this; a
 * page never does. */
void reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area);
void reaktor_frame_end(void);

/* --- containers --------------------------------------------------------- */

void reaktor_box_open(unsigned char dir, const reaktor_box *b);
void reaktor_box_close(void);

/* A run-once for, so the body is a scope rather than a pair of calls the
 * caller has to balance. The odd name is deliberate: it appears in the
 * caller's scope, and something shorter would collide with a local. */
#define REAKTOR_BOX_SCOPE_(axis, ...)                                        \
    for (int reaktor_box_scope_ =                                            \
             (reaktor_box_open((unsigned char)(axis),                        \
                               &(reaktor_box){ 0, __VA_ARGS__ }), 1);        \
         reaktor_box_scope_;                                                 \
         reaktor_box_scope_ = (reaktor_box_close(), 0))

#define REAKTOR_ROW(...)    REAKTOR_BOX_SCOPE_(REAKTOR_LAY_ROW, __VA_ARGS__)
#define REAKTOR_COLUMN(...) REAKTOR_BOX_SCOPE_(REAKTOR_LAY_COLUMN, __VA_ARGS__)

/* --- handlers ----------------------------------------------------------- */

/* Sugar for a caller who would rather name a function than write an `if`. It
 * fires exactly where the bool would have been true, on the same frame, in
 * the same place - there is no second dispatch path and no casting macro, and
 * the return value still works whether or not one of these is set. */
typedef void (*reaktor_action)(void *user);

typedef struct reaktor_handler {
    reaktor_action fn;
    void          *user;
} reaktor_handler;

/* --- widgets ------------------------------------------------------------ */

/* Common to every spec. `name` is what a screen reader hears when the visible
 * text is not enough - an icon-only button, a field whose label sits beside
 * it. `box` overrides the size the widget would choose for itself. */
typedef struct reaktor_button_spec {
    const char     *label;
    const char     *name;
    const char     *keys;      /* the chords that reach it; see keys.h */
    reaktor_box     box;
    reaktor_handler on_press;
    unsigned char   accent;    /* the stylesheet's --links colour */
    unsigned char   disabled;
} reaktor_button_spec;

/* Non-zero on the frame it was pressed. */
int reaktor_button(const reaktor_button_spec *s);

typedef struct reaktor_label_spec {
    const char *text;
    const char *name;          /* when the text is not what should be read */
    reaktor_box box;
} reaktor_label_spec;

void reaktor_label(const reaktor_label_spec *s);

#endif /* REAKTOR_DECLARE_H */
