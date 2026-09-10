/* declare.h - saying what the screen holds, instead of drawing it.
 *
 * A container is two things: what it is, and what is in it. That is Capy's
 * grammar, and C99 spells it natively - a compound literal for the first, a
 * scope for the second:
 *
 *     REAKTOR_COLUMN(.gap = 15, .ml = 22, .mr = 22) {
 *         REAKTOR_ROW(.h = 28, .gap = 10) {
 *             reaktor_icon(&(reaktor_icon_spec){ .name = "person-circle" });
 *             reaktor_label(&(reaktor_label_spec){
 *                 .text = "Proceed with login", .style = ".card-title",
 *                 .box  = { .flags = REAKTOR_LAY_FILL_X } });
 *         }
 *         if (reaktor_button(&(reaktor_button_spec){
 *                 .label = "Continue with email", .accent = 1 }))
 *             submit(&form);
 *     }
 *
 * That `if` is why there are no callbacks here. A retained toolkit has to hand
 * you one: its widget outlives the code that built it, so the click arrives
 * somewhere else, later, with a void * to find its way home. This widget *is*
 * that code. Its answer is a return value, and the enclosing scope - locals,
 * loop variables and all - is still on the stack when it comes back.
 *
 * Sizes are optional because a widget knows its own, and `.style` names a CSS
 * selector rather than a number, because a size written here would be the one
 * thing the premise does not allow. See assets/reaktor.css.
 *
 * ONE RULE THE COMPILER CANNOT ENFORCE: do not `return`, `break` or `goto` out
 * of a container scope. C has no defer, so the closing call would be skipped
 * and the tree left open. It is the same contract nk_begin/nk_end already
 * impose on every page in this tree.
 */
#ifndef REAKTOR_DECLARE_H
#define REAKTOR_DECLARE_H

#include "layout.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

/* --- the frame ---------------------------------------------------------- */

/* One screen is being described at a time - that is what immediate mode
 * means - so the frame's context is held here rather than threaded through
 * every call. The runtime opens and closes this; a page never does. */
void reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area);
void reaktor_frame_end(void);

/* Whether the last frame drew everything where it finally belongs. A box on
 * its first frame has nowhere to be, and a wrapped paragraph needs one more
 * still: its height depends on its width, so the frame that learns the width
 * is not the frame that can be the right height. Both ask for another frame
 * on their own; this is for anything that wants to wait for the answer, and
 * the accessibility dump is the reason it exists. */
int reaktor_frame_settled(void);

/* --- containers --------------------------------------------------------- */

void reaktor_box_open(unsigned char dir, const reaktor_box *b);
void reaktor_box_close(void);

/* A run-once for, so the body is a scope rather than a pair of calls the
 * caller has to balance. The odd variable name is deliberate: it lands in the
 * caller's scope, and something shorter would collide with a local. */
#define REAKTOR_BOX_SCOPE_(axis, ...)                                        \
    for (int reaktor_box_scope_ =                                            \
             (reaktor_box_open((unsigned char)(axis),                        \
                               &(reaktor_box){ 0, __VA_ARGS__ }), 1);        \
         reaktor_box_scope_;                                                 \
         reaktor_box_scope_ = (reaktor_box_close(), 0))

#define REAKTOR_ROW(...)    REAKTOR_BOX_SCOPE_(REAKTOR_LAY_ROW, __VA_ARGS__)
#define REAKTOR_COLUMN(...) REAKTOR_BOX_SCOPE_(REAKTOR_LAY_COLUMN, __VA_ARGS__)

/* Nothing, taking up room. A gap between two children is the container's
 * `gap`; this is for the one place a single hole is wanted. */
void reaktor_gap(float w, float h);

/* Nothing, taking whatever is left. What a page reaches for when the boxes
 * beside it are meant to keep the width they asked for rather than share the
 * window between them - nk_layout_row_template's trailing dynamic column. */
void reaktor_soak(void);

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

/* Common to the specs below. `name` is what a screen reader hears when the
 * visible text is not enough. `style` is a CSS selector - ".card-title" - and
 * supplies the type; without one the widget takes the frame's font. `box`
 * overrides the size the widget would otherwise choose for itself. */
typedef struct reaktor_button_spec {
    const char     *label;
    const char     *icon;      /* an Ionicons name, without path or suffix */
    const char     *name;
    const char     *style;
    const char     *keys;      /* the chords that reach it; see keys.h */
    reaktor_box     box;
    reaktor_handler on_press;
    unsigned char   accent;    /* the stylesheet's --links colour */
    unsigned char   disabled;
    /* Fires for as long as it is held, rather than once on release. */
    unsigned char   repeat;
} reaktor_button_spec;

/* Non-zero on the frame it was pressed. */
int reaktor_button(const reaktor_button_spec *s);

typedef struct reaktor_label_spec {
    const char   *text;
    const char   *name;        /* when the text is not what should be read */
    const char   *style;
    const char   *colour;      /* a palette token, e.g. "--text-muted" */
    reaktor_box   box;
    unsigned char centred;
    /* Wrap to the width the layout gives it, and be as tall as that takes.
     * Which is circular - the height is wanted before the width is known -
     * and the frame of lag is what breaks it: the width used is the one this
     * label had last frame. On its first frame it has none, so it is not
     * drawn at all, which is what every box does on its first frame. */
    unsigned char wrap;
} reaktor_label_spec;

void reaktor_label(const reaktor_label_spec *s);

/* The button rule with no label and the given fill: a colour swatch that is
 * otherwise a button. `name` is what a reader is told it is, since there is
 * no text to read. */
typedef struct reaktor_swatch_spec {
    const char     *name;
    struct nk_color fill;
    reaktor_box     box;
    reaktor_handler on_press;
} reaktor_swatch_spec;

int reaktor_swatch(const reaktor_swatch_spec *s);

/* An Ionicon, drawn at the size of its box. `stroke` is a "#rrggbb" the
 * artwork is recoloured to, or NULL for the accent. */
typedef struct reaktor_icon_spec {
    const char   *name;     /* an Ionicons name, without path or suffix */
    reaktor_box   box;
    unsigned char accent;   /* the stylesheet's --links, not the muted stroke */
} reaktor_icon_spec;

void reaktor_icon(const reaktor_icon_spec *s);

/* A text field, with `hint` painted into it while it is empty. `len` is the
 * caller's, because the text is the caller's. */
typedef struct reaktor_field_spec {
    char       *buf;
    int        *len;
    int         cap;
    const char *hint;
    const char *name;
    const char *style;
    reaktor_box box;
} reaktor_field_spec;

void reaktor_field(const reaktor_field_spec *s);

/* A checkbox. It writes either a bool of the caller's, or one bit of a
 * flags word of the caller's - which is how a set of independent options
 * lives in a single value, and is the only reason there are two fields here
 * rather than one. Set exactly one of them.
 *
 * Non-zero on the frame it changed. */
typedef struct reaktor_check_spec {
    const char   *label;
    const char   *name;
    nk_bool      *on;
    unsigned     *flags;
    unsigned      bit;
    reaktor_box   box;
    unsigned char box_right;   /* the box at the far edge, label first */
} reaktor_check_spec;

int reaktor_check(const reaktor_check_spec *s);

/* One of a group. `choice` is the group's answer and `value` is this one's,
 * so being chosen is the two matching - which is all a radio group ever is.
 * Chooses this one when clicked, and answers whether it did. */
typedef struct reaktor_radio_spec {
    const char   *label;
    const char   *name;
    int          *choice;
    int           value;
    reaktor_box   box;
} reaktor_radio_spec;

int reaktor_radio(const reaktor_radio_spec *s);

/* Text that acts. Reported as a link rather than a button, because that is
 * what it looks like and what it should be read as. */
typedef struct reaktor_link_spec {
    const char     *text;
    const char     *name;
    const char     *style;
    reaktor_box     box;
    reaktor_handler on_press;
    unsigned char   active;    /* drawn in the link colour rather than muted */
} reaktor_link_spec;

int reaktor_link(const reaktor_link_spec *s);

#endif /* REAKTOR_DECLARE_H */
