#ifndef REAKTOR_LAYOUT_H
#define REAKTOR_LAYOUT_H

#include "nk_common.h"
#include "onlay.h"

enum {
    REAKTOR_LAY_ROW = 0,
    REAKTOR_LAY_COLUMN,
    REAKTOR_LAY_FREE
};

enum {
    REAKTOR_LAY_FILL_X = 1u << 0,
    REAKTOR_LAY_FILL_Y = 1u << 1,
    REAKTOR_LAY_WRAP   = 1u << 2,
    REAKTOR_LAY_CENTER_X = 1u << 3,
    REAKTOR_LAY_CENTER_Y = 1u << 4,
    REAKTOR_LAY_PACK_CENTER = 1u << 5,
    REAKTOR_LAY_PACK_END    = 1u << 6,
    REAKTOR_LAY_PACK_SPREAD = 1u << 7
};

/* A box. w/h is a size, and a *floor* when the same axis also fills:
 * a fixed track is w alone, a minimum that grows is w with FILL_X, a
 * fraction is FILL_X with weight. gap is between a box's children,
 * margins are outside the box itself. */
typedef struct reaktor_box {
    unsigned char dir;
    const char   *name;
    float         w, h;
    float         weight;
    float         gap;
    float         ml, mt, mr, mb;
    unsigned      flags;
} reaktor_box;

#define REAKTOR_LAY_MAX   256
#define REAKTOR_LAY_DEPTH 32
#define REAKTOR_LAY_SLOTS 512

/* Where a box landed, kept by id from one frame to the next: gen is
 * the frame that wrote it, so a stale slot can be told from a fresh
 * one without clearing the table. */
typedef struct reaktor_lay_slot {
    unsigned       id;
    unsigned       gen;
    struct nk_rect rect;
} reaktor_lay_slot;

typedef struct reaktor_layout {
    lay_context ctx;
    int         started;

    unsigned    id[REAKTOR_LAY_MAX];
    lay_id      item[REAKTOR_LAY_MAX];
    int         count;

    lay_id      stack[REAKTOR_LAY_DEPTH];
    int         depth;

    reaktor_lay_slot slot[REAKTOR_LAY_SLOTS];
    unsigned         gen;

    float       ox, oy;

    int overflow_boxes;
} reaktor_layout;

void reaktor_layout_begin(reaktor_layout *l, struct nk_rect root);

void reaktor_layout_open(reaktor_layout *l, unsigned id, const reaktor_box *b);
void reaktor_layout_leaf(reaktor_layout *l, unsigned id, const reaktor_box *b);
void reaktor_layout_close(reaktor_layout *l);

void reaktor_layout_end(reaktor_layout *l);

/* The rect for a box, or 0 if it has none yet.
 *
 * Boxes are placed a frame late, and that is structural rather than a
 * shortcut: a frame declares its boxes as it draws them, so the last
 * sibling is declared long after the first is drawn and no sibling's
 * share is known when it is needed. A frame therefore draws into the
 * rects computed at the end of the previous one. A box seen for the
 * first time answers 0 and is skipped for one frame rather than drawn
 * somewhere wrong.
 *
 * The id is the accessibility node's, so a box is found again by its
 * name: a widget whose text changes every frame is a new box every
 * frame and never gets a rect at all. */
int reaktor_layout_rect(const reaktor_layout *l, unsigned id,
                        struct nk_rect *out);

void reaktor_layout_free(reaktor_layout *l);

#endif
