#ifndef REAKTOR_LAYOUT_H
#define REAKTOR_LAYOUT_H

#include "nk_common.h"
#include "onlay.h"
#include "reaktor/widgets.h"

#define REAKTOR_LAY_MAX   256
#define REAKTOR_LAY_DEPTH 32
#define REAKTOR_LAY_SLOTS 512

/* Sides whose margin a stylesheet set, in Onlay's left, top, right, bottom order. */
#define REAKTOR_LAY_SHEET_L (1u << 28)
#define REAKTOR_LAY_SHEET_T (1u << 29)
#define REAKTOR_LAY_SHEET_R (1u << 30)
#define REAKTOR_LAY_SHEET_B (1u << 31)

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

    unsigned      flags[REAKTOR_LAY_MAX];
    unsigned char dir[REAKTOR_LAY_MAX];

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

int reaktor_layout_rect(const reaktor_layout *l, unsigned id,
                        struct nk_rect *out);

void reaktor_layout_free(reaktor_layout *l);

#endif
