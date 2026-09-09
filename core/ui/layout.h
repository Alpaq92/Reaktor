/* layout.h - the seam between a declared tree and Onlay.
 *
 * A frame declares its boxes as it draws them, which is one frame too early
 * to know where any of them go: Onlay cannot place an item until its siblings
 * exist, and the last sibling is declared long after the first has been
 * drawn. So a frame draws into the rects Onlay computed at the end of the
 * frame before it, found by an id that survives a redraw - the id the
 * accessibility tree already assigns, and for the same reason. Nothing here
 * computes an id, which is why nothing here can disagree with the tree about
 * which box is which.
 *
 * A box declared for the first time has no rect yet. It is skipped for one
 * frame rather than guessed at. That is the cheaper of the two answers to the
 * empty first frame, and the one a screen reader already tolerates: an element
 * that appears is announced on the frame after it appears either way.
 *
 * One rule about padding, written here because getting it wrong insets every
 * widget twice: **this file owns the outer box and the gaps between boxes;
 * Nuklear owns the inside of a widget.** A CSS padding that Nuklear already
 * applies is not applied again here.
 */
#ifndef REAKTOR_LAYOUT_H
#define REAKTOR_LAYOUT_H

#include "nk_common.h"
#include "onlay.h"

enum { REAKTOR_LAY_ROW = 0, REAKTOR_LAY_COLUMN };

enum {
    REAKTOR_LAY_FILL_X = 1u << 0,
    REAKTOR_LAY_FILL_Y = 1u << 1,
    REAKTOR_LAY_WRAP   = 1u << 2,  /* children flow onto a second line */
    /* Onlay spells centring as zero, so a box that says nothing about its
     * cross axis comes out centred. That is not what a row of controls means,
     * and not what Nuklear was doing before, so the default here is the top
     * left corner and centring is something you ask for. */
    REAKTOR_LAY_CENTER_X = 1u << 3,
    REAKTOR_LAY_CENTER_Y = 1u << 4,
    /* And the same trap on the other axis: LAY_MIDDLE is 0x000 too, so a
     * container that says nothing centres its children along its own axis and
     * every one of them drifts by half the leftover. Children start at the
     * beginning here; these ask for anything else. */
    REAKTOR_LAY_PACK_CENTER = 1u << 5,
    REAKTOR_LAY_PACK_END    = 1u << 6,
    REAKTOR_LAY_PACK_SPREAD = 1u << 7
};

/* One box. Every field is optional, which is the whole point: a designated
 * initialiser then reads as a description rather than an argument list.
 *
 * `w` and `h` are the box's own size, and become a floor when it fills - "at
 * least 80 wide, then grow" is w = 80 with FILL_X. `weight` is its share of
 * whatever is left over, 0 meaning an equal share. `gap` separates its
 * children, and the margins are its own. */
typedef struct reaktor_box {
    /* First on purpose. The scope macros write `{ 0, __VA_ARGS__ }` so that a
     * container with nothing to say still has a legal initialiser, and that 0
     * lands on whichever member comes first - which a caller then overrides,
     * and every compiler with -Winitializer-overrides says so. `dir` is the
     * one member no caller ever sets: the macro supplies it from the axis it
     * was called as. So the 0 lands somewhere harmless and stays quiet. */
    unsigned char dir;      /* a container's axis: _ROW or _COLUMN */
    /* What a reader is told this container is. Nothing in this file reads it -
     * it rides along because the scope macros take one struct - but a named
     * container is a named group in the accessibility tree. */
    const char   *name;
    float         w, h;
    float         weight;
    float         gap;
    float         ml, mt, mr, mb;
    unsigned      flags;    /* REAKTOR_LAY_FILL_* | REAKTOR_LAY_WRAP */
} reaktor_box;

/* Enough for the busiest page twice over, and the same failure as the
 * accessibility arenas when it is not: a lost box rather than a lost frame. */
#define REAKTOR_LAY_MAX   256
#define REAKTOR_LAY_DEPTH 32
/* Power of two, twice the box arena, so the table never passes half load. */
#define REAKTOR_LAY_SLOTS 512

typedef struct reaktor_lay_slot {
    unsigned       id;
    unsigned       gen;      /* stale gen means empty, so a frame starts free */
    struct nk_rect rect;
} reaktor_lay_slot;

typedef struct reaktor_layout {
    lay_context ctx;
    int         started;     /* the context has been reserved */

    /* Being declared now: Onlay's item for each box, and the id it will be
     * found by on the next frame. */
    unsigned    id[REAKTOR_LAY_MAX];
    lay_id      item[REAKTOR_LAY_MAX];
    int         count;

    lay_id      stack[REAKTOR_LAY_DEPTH];
    int         depth;

    /* Computed at the end of the previous frame: what this frame draws into.
     * Keyed rather than scanned - the accessibility model was quadratic in
     * widgets once, and this would have been the second time. */
    reaktor_lay_slot slot[REAKTOR_LAY_SLOTS];
    unsigned         gen;

    /* Onlay lays out from the origin; the tree may not sit there. */
    float       ox, oy;

    /* Counters rather than flags: a page that overflows once and then fits
     * should stop saying so. */
    int overflow_boxes, overflow_depth;
} reaktor_layout;

/* Starts a frame's tree. `root` is where it sits and how big it is; every box
 * declared before the matching end is inside it. */
void reaktor_layout_begin(reaktor_layout *l, struct nk_rect root);

/* Declares a box. `open` makes it the parent of everything up to the matching
 * close; `leaf` does not. `id` is what reaktor_a11y_add answered with for the
 * same widget. */
void reaktor_layout_open(reaktor_layout *l, unsigned id, const reaktor_box *b);
void reaktor_layout_leaf(reaktor_layout *l, unsigned id, const reaktor_box *b);
void reaktor_layout_close(reaktor_layout *l);

/* Runs the layout and keeps the result for the next frame to draw into. */
void reaktor_layout_end(reaktor_layout *l);

/* Where the box `id` was placed, in window coordinates. Answers 0 for a box
 * that has not been laid out yet - which is every box on its first frame, and
 * every box on the frame after the tree changed shape. A caller that gets 0
 * draws nothing and reports the widget unhittable, rather than guessing. */
int reaktor_layout_rect(const reaktor_layout *l, unsigned id,
                        struct nk_rect *out);

/* Gives back Onlay's buffer. The frame never allocates: this is for shutdown,
 * and for a test that wants to run clean. */
void reaktor_layout_free(reaktor_layout *l);

#endif /* REAKTOR_LAYOUT_H */
