#include <string.h>

#include "layout.h"

static struct nk_rect
rect_of(float x, float y, float w, float h)
{
    struct nk_rect r;

    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

/* The direction a box was declared with, read back from onlay's own item. */
static unsigned char
dir_of(const reaktor_layout *l, lay_id it)
{
    switch (lay_get_item(&l->ctx, it)->flags & 3u) {
    case LAY_ROW:    return REAKTOR_LAY_ROW;
    case LAY_COLUMN: return REAKTOR_LAY_COLUMN;
    default:         return REAKTOR_LAY_FREE;
    }
}

static uint32_t
contain_of(const reaktor_box *b)
{
    uint32_t f;

    if (b->dir == REAKTOR_LAY_FREE) return 0;

    f = (b->dir == REAKTOR_LAY_COLUMN) ? LAY_COLUMN : LAY_ROW;
    if (b->flags & REAKTOR_LAY_WRAP) f |= LAY_WRAP;

    if (b->flags & REAKTOR_LAY_PACK_CENTER)      f |= LAY_MIDDLE;
    else if (b->flags & REAKTOR_LAY_PACK_END)    f |= LAY_END;
    else if (b->flags & REAKTOR_LAY_PACK_SPREAD) f |= LAY_JUSTIFY;
    else                                         f |= LAY_START;
    return f;
}

static uint32_t
behave_of(const reaktor_box *b)
{
    uint32_t f = 0;

    if (b->flags & REAKTOR_LAY_FILL_X)          f |= LAY_HFILL;
    else if (!(b->flags & REAKTOR_LAY_CENTER_X)) f |= LAY_LEFT;
    if (b->flags & REAKTOR_LAY_FILL_Y)          f |= LAY_VFILL;
    else if (!(b->flags & REAKTOR_LAY_CENTER_Y)) f |= LAY_TOP;
    return f;
}

static void
attach(reaktor_layout *l, lay_id it)
{
    int d = l->depth > REAKTOR_LAY_DEPTH ? REAKTOR_LAY_DEPTH : l->depth;

    if (d <= 0 || l->stack[d - 1] == LAY_INVALID_ID) return;
    if (l->last[d - 1] == LAY_INVALID_ID) lay_insert(&l->ctx, l->stack[d - 1], it);
    else lay_append(&l->ctx, l->last[d - 1], it);
    l->last[d - 1] = it;
}

static void
push(reaktor_layout *l, lay_id it)
{
    if (l->depth < REAKTOR_LAY_DEPTH) {
        l->stack[l->depth] = it;
        l->last[l->depth]  = LAY_INVALID_ID;
    }
    l->depth++;
}

static lay_id
declare(reaktor_layout *l, unsigned id, const reaktor_box *b)
{
    lay_id it;

    if (l->count >= REAKTOR_LAY_MAX) { l->overflow_boxes++; return LAY_INVALID_ID; }

    it = lay_item(&l->ctx);
    if (it < REAKTOR_LAY_MAX) {
        l->flags[it] = b->flags;
    }
    lay_set_size_xy(&l->ctx, it, (lay_scalar)b->w, (lay_scalar)b->h);
    lay_set_contain(&l->ctx, it, contain_of(b));
    lay_set_behave(&l->ctx, it, behave_of(b));
    if (b->gap > 0.0f)    lay_set_gap(&l->ctx, it, (lay_scalar)b->gap);
    if (b->weight > 0.0f) lay_set_weight(&l->ctx, it, b->weight);
    if (b->ml || b->mt || b->mr || b->mb)
        lay_set_margins_ltrb(&l->ctx, it, (lay_scalar)b->ml, (lay_scalar)b->mt,
                             (lay_scalar)b->mr, (lay_scalar)b->mb);

    attach(l, it);

    l->id[l->count]   = id;
    l->item[l->count] = it;
    l->count++;
    return it;
}

void
reaktor_layout_begin(reaktor_layout *l, struct nk_rect root)
{
    reaktor_box rb;
    lay_id      it;

    if (!l->started) {
        lay_init_context(&l->ctx);
        lay_reserve_items_capacity(&l->ctx, REAKTOR_LAY_MAX);
        l->started = 1;
    } else {
        lay_reset_context(&l->ctx);
    }

    l->count = 0;
    l->depth = 0;
    l->ox    = root.x;
    l->oy    = root.y;

    memset(&rb, 0, sizeof(rb));
    rb.w   = root.w;
    rb.h   = root.h;
    rb.dir = REAKTOR_LAY_FREE;

    it = declare(l, 0u, &rb);
    if (it != LAY_INVALID_ID) push(l, it);
}

void
reaktor_layout_open(reaktor_layout *l, unsigned id, const reaktor_box *b)
{
    push(l, declare(l, id, b));
}

void
reaktor_layout_leaf(reaktor_layout *l, unsigned id, const reaktor_box *b)
{
    (void)declare(l, id, b);
}

void
reaktor_layout_close(reaktor_layout *l)
{
    if (l->depth > 1) l->depth--;
}

unsigned char
reaktor_layout_open_dir(const reaktor_layout *l)
{
    lay_id box;

    if (l->depth <= 1 || l->depth > REAKTOR_LAY_DEPTH) return REAKTOR_LAY_COLUMN;
    box = l->stack[l->depth - 1];
    return box < REAKTOR_LAY_MAX ? dir_of(l, box) : REAKTOR_LAY_COLUMN;
}

static reaktor_lay_slot *
slot_of(reaktor_layout *l, unsigned id)
{
    unsigned i = id & (REAKTOR_LAY_SLOTS - 1);
    unsigned n = 0;

    for (; n < REAKTOR_LAY_SLOTS; n++) {
        reaktor_lay_slot *s = &l->slot[i];

        if (s->gen != l->gen || s->id == id) return s;
        i = (i + 1) & (REAKTOR_LAY_SLOTS - 1);
    }
    return NULL;
}

static void
add_margin(reaktor_layout *l, lay_id it, int side, lay_scalar by)
{
    lay_vec4 m = lay_get_margins(&l->ctx, it);

    m.v[side] += by;
    lay_set_margins(&l->ctx, it, m);
}

static lay_scalar
take_margin(reaktor_layout *l, lay_id it, int side)
{
    lay_scalar was = lay_get_margins(&l->ctx, it).v[side];

    add_margin(l, it, side, -was);
    return was;
}

static void
part_neighbors(reaktor_layout *l, lay_id box)
{
    lay_id c, prev = LAY_INVALID_ID;
    int    lead, trail;

    if (box >= REAKTOR_LAY_MAX || dir_of(l, box) == REAKTOR_LAY_FREE) return;
    lead  = dir_of(l, box) == REAKTOR_LAY_ROW ? 0 : 1;
    trail = lead + 2;

    for (c = lay_first_child(&l->ctx, box); c != LAY_INVALID_ID;
         prev = c, c = lay_next_sibling(&l->ctx, c)) {
        lay_scalar moved;

        if (c >= REAKTOR_LAY_MAX || !(l->flags[c] & (REAKTOR_LAY_SHEET_L << lead)))
            continue;
        moved = take_margin(l, c, lead);
        if (prev != LAY_INVALID_ID) add_margin(l, prev, trail, moved);
    }
    if (prev != LAY_INVALID_ID && prev < REAKTOR_LAY_MAX
        && (l->flags[prev] & (REAKTOR_LAY_SHEET_L << trail)))
        take_margin(l, prev, trail);
}

void
reaktor_layout_end(reaktor_layout *l)
{
    int i;

    if (!l->started || !l->count) return;
    for (i = 0; i < l->count; i++) part_neighbors(l, l->item[i]);
    lay_run_context(&l->ctx);

    l->gen++;
    for (i = 0; i < l->count; i++) {
        reaktor_lay_slot *s;
        lay_vec4          r;

        if (l->item[i] == LAY_INVALID_ID) continue;
        s = slot_of(l, l->id[i]);
        if (!s) continue;

        r = lay_get_rect(&l->ctx, l->item[i]);
        s->id   = l->id[i];
        s->gen  = l->gen;
        s->rect = rect_of((float)r.v[0] + l->ox, (float)r.v[1] + l->oy,
                          (float)r.v[2], (float)r.v[3]);
    }
}

int
reaktor_layout_rect(const reaktor_layout *l, unsigned id, struct nk_rect *out)
{
    unsigned i = id & (REAKTOR_LAY_SLOTS - 1);
    unsigned n = 0;

    for (; n < REAKTOR_LAY_SLOTS; n++) {
        const reaktor_lay_slot *s = &l->slot[i];

        if (s->gen != l->gen) return 0;
        if (s->id == id) { *out = s->rect; return 1; }
        i = (i + 1) & (REAKTOR_LAY_SLOTS - 1);
    }
    return 0;
}

int
reaktor_layout_info(const reaktor_layout *l, unsigned id, unsigned *flags,
                    unsigned char *dir, float *w, float *h)
{
    int i;

    for (i = 0; i < l->count; i++) {
        lay_id it = l->item[i];

        if (l->id[i] != id || it >= REAKTOR_LAY_MAX) continue;
        *flags = l->flags[it];
        lay_vec2 size = lay_get_size((lay_context *)&l->ctx, it);

        *dir   = dir_of(l, it);
        *w     = size.v[0];
        *h     = size.v[1];
        return 1;
    }
    return 0;
}

void
reaktor_layout_free(reaktor_layout *l)
{
    if (!l->started) return;
    lay_destroy_context(&l->ctx);
    l->started = 0;
    l->count = l->depth = 0;
}
