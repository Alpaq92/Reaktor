#include <string.h>

#include "layout.h"

static struct nk_rect
rect_of(float x, float y, float w, float h)
{
    struct nk_rect r;

    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
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

static lay_id
parent_of(const reaktor_layout *l)
{
    int d = l->depth;

    if (d <= 0) return LAY_INVALID_ID;
    if (d > REAKTOR_LAY_DEPTH) d = REAKTOR_LAY_DEPTH;
    return l->stack[d - 1];
}

static lay_id
declare(reaktor_layout *l, unsigned id, const reaktor_box *b)
{
    lay_id it;

    if (l->count >= REAKTOR_LAY_MAX) { l->overflow_boxes++; return LAY_INVALID_ID; }

    it = lay_item(&l->ctx);
    lay_set_size_xy(&l->ctx, it, (lay_scalar)b->w, (lay_scalar)b->h);
    lay_set_contain(&l->ctx, it, contain_of(b));
    lay_set_behave(&l->ctx, it, behave_of(b));
    if (b->gap > 0.0f)    lay_set_gap(&l->ctx, it, (lay_scalar)b->gap);
    if (b->weight > 0.0f) lay_set_weight(&l->ctx, it, b->weight);
    if (b->ml || b->mt || b->mr || b->mb)
        lay_set_margins_ltrb(&l->ctx, it, (lay_scalar)b->ml, (lay_scalar)b->mt,
                             (lay_scalar)b->mr, (lay_scalar)b->mb);

    {
        lay_id parent = parent_of(l);
        if (parent != LAY_INVALID_ID) lay_insert(&l->ctx, parent, it);
    }

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
    if (it != LAY_INVALID_ID) l->stack[l->depth++] = it;
}

void
reaktor_layout_open(reaktor_layout *l, unsigned id, const reaktor_box *b)
{
    lay_id it = declare(l, id, b);

    if (l->depth < REAKTOR_LAY_DEPTH) l->stack[l->depth] = it;
    l->depth++;
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

void
reaktor_layout_end(reaktor_layout *l)
{
    int i;

    if (!l->started || !l->count) return;
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

void
reaktor_layout_free(reaktor_layout *l)
{
    if (!l->started) return;
    lay_destroy_context(&l->ctx);
    l->started = 0;
    l->count = l->depth = 0;
}
