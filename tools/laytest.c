/* laytest.c - the Onlay shim: ids in, rects out, one frame later.
 *
 * Onlay's own arithmetic is checked by onlay/test_onlay.c, against the real
 * application's accessibility dump. This checks what the shim owes on top of
 * it: that a box is found by the id the accessibility tree gave it, that the
 * tree's origin is added back, that a frame reads the frame before it, and
 * that a box nobody has laid out yet answers "not yet" instead of a guess.
 */
#include <stdio.h>
#include <string.h>

#include "layout.h"

static int g_fail;

/* nk_rect() lives in Nuklear's implementation, which this does not link. */
static struct nk_rect
R(float x, float y, float w, float h)
{
    struct nk_rect r;

    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

static void
ok(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("  %-52s got %d, wanted %d   FAILED\n", what, got, want);
    } else {
        printf("  %-52s ok\n", what);
    }
}

/* One frame: declare a row of three boxes, two of them filling. */
static void
frame(reaktor_layout *l, float gap)
{
    reaktor_box root = { 0 };
    reaktor_box fixed = { 0 };
    reaktor_box fill  = { 0 };

    root.dir   = REAKTOR_LAY_ROW;
    root.gap   = gap;
    root.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y;

    fixed.w = 100.0f;
    fixed.h = 30.0f;

    fill.h      = 30.0f;
    fill.flags  = REAKTOR_LAY_FILL_X;

    reaktor_layout_begin(l, R(10.0f, 20.0f, 400.0f, 200.0f));
    reaktor_layout_open(l, 100u, &root);
    reaktor_layout_leaf(l, 101u, &fixed);
    fill.weight = 1.0f;
    reaktor_layout_leaf(l, 102u, &fill);
    fill.weight = 3.0f;
    reaktor_layout_leaf(l, 103u, &fill);
    reaktor_layout_close(l);
    reaktor_layout_end(l);
}

static void
test_lag(reaktor_layout *l)
{
    struct nk_rect r;

    puts("a frame reads the frame before it");
    ok("nothing is placed before the first layout runs",
       reaktor_layout_rect(l, 101u, &r), 0);
    frame(l, 0.0f);
    ok("...and everything is, after it",
       reaktor_layout_rect(l, 101u, &r), 1);
    ok("a box nobody declared is still not placed",
       reaktor_layout_rect(l, 999u, &r), 0);
}

static void
test_origin(reaktor_layout *l)
{
    struct nk_rect root, first;

    puts("");
    puts("the tree's origin is added back");
    ok("the root answers where it was put",
       reaktor_layout_rect(l, 0u, &root) && root.x == 10.0f && root.y == 20.0f,
       1);
    ok("...at the size it was given",
       root.w == 400.0f && root.h == 200.0f, 1);
    reaktor_layout_rect(l, 101u, &first);
    ok("the first child starts at the tree's left edge, not at zero",
       first.x == 10.0f, 1);
    ok("...and at its top, not floating in the middle", first.y == 20.0f, 1);
    ok("a fixed box keeps the size it asked for",
       first.w == 100.0f && first.h == 30.0f, 1);
}

/* Onlay's own default is the opposite of ours, so both are worth pinning. */
static void
test_centring(reaktor_layout *l)
{
    reaktor_box row = { 0 }, mid = { 0 }, top = { 0 };
    struct nk_rect a, b;

    puts("");
    puts("centring is asked for, never assumed");
    row.dir   = REAKTOR_LAY_ROW;
    row.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y;
    top.w = mid.w = 40.0f;
    top.h = mid.h = 20.0f;
    mid.flags = REAKTOR_LAY_CENTER_Y;

    reaktor_layout_begin(l, R(0.0f, 0.0f, 200.0f, 100.0f));
    reaktor_layout_open(l, 300u, &row);
    reaktor_layout_leaf(l, 301u, &top);
    reaktor_layout_leaf(l, 302u, &mid);
    reaktor_layout_close(l);
    reaktor_layout_end(l);

    reaktor_layout_rect(l, 301u, &a);
    reaktor_layout_rect(l, 302u, &b);
    ok("a box that says nothing sits at the top", a.y == 0.0f, 1);
    ok("one that asks to be centred is centred",
       b.y == (100.0f - 20.0f) / 2.0f, 1);
}

static void
test_weight(reaktor_layout *l)
{
    struct nk_rect a, b, c;

    puts("");
    puts("what is left over is split by weight");
    reaktor_layout_rect(l, 101u, &a);
    reaktor_layout_rect(l, 102u, &b);
    reaktor_layout_rect(l, 103u, &c);
    ok("the fixed box took none of it", a.w == 100.0f, 1);
    ok("three parts to one, not one to one", c.w == 3.0f * b.w, 1);
    ok("and between them they took the rest",
       a.w + b.w + c.w == 400.0f, 1);
    ok("laid end to end, in the order declared",
       b.x == a.x + a.w && c.x == b.x + b.w, 1);
}

static void
test_gap(reaktor_layout *l)
{
    struct nk_rect a, b, c;

    puts("");
    puts("a gap separates children and costs them the room");
    frame(l, 12.0f);
    reaktor_layout_rect(l, 101u, &a);
    reaktor_layout_rect(l, 102u, &b);
    reaktor_layout_rect(l, 103u, &c);
    ok("the second box starts a gap after the first",
       b.x == a.x + a.w + 12.0f, 1);
    ok("and the third after the second",
       c.x == b.x + b.w + 12.0f, 1);
    ok("two gaps came out of the fillers, not out of the row",
       a.w + b.w + c.w == 400.0f - 24.0f, 1);
    ok("the fixed box is still fixed", a.w == 100.0f, 1);
}

static void
test_nesting(reaktor_layout *l)
{
    reaktor_box col = { 0 }, row = { 0 }, leaf = { 0 };
    struct nk_rect outer, inner;

    puts("");
    puts("a box inside a box");
    col.dir   = REAKTOR_LAY_COLUMN;
    col.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y;
    row.dir   = REAKTOR_LAY_ROW;
    row.h     = 40.0f;
    row.flags = REAKTOR_LAY_FILL_X;
    row.ml    = 8.0f;
    leaf.w    = 50.0f;
    leaf.h    = 20.0f;

    reaktor_layout_begin(l, R(0.0f, 0.0f, 300.0f, 300.0f));
    reaktor_layout_open(l, 200u, &col);
    reaktor_layout_open(l, 201u, &row);
    reaktor_layout_leaf(l, 202u, &leaf);
    reaktor_layout_close(l);
    reaktor_layout_close(l);
    reaktor_layout_end(l);

    ok("the inner row is inside the outer column",
       reaktor_layout_rect(l, 201u, &outer) && outer.h == 40.0f, 1);
    ok("its left margin moved it, and only it",
       outer.x == 8.0f, 1);
    ok("the leaf is inside the row",
       reaktor_layout_rect(l, 202u, &inner) &&
       inner.x >= outer.x && inner.y >= outer.y, 1);
    ok("last frame's boxes are gone, not merged with these",
       reaktor_layout_rect(l, 101u, &inner), 0);
}

static void
test_limits(reaktor_layout *l)
{
    reaktor_box b = { 0 };
    int i;

    puts("");
    puts("two trees at absolute positions, each minding its own business");
    {
        reaktor_box a = { 0 }, b2 = { 0 }, kid = { 0 };
        struct nk_rect ra, rb;

        /* What a page actually does: a card here, a section there. In a
         * stacking root the second one started below the first and then added
         * its own offset on top, which is how one section became several. */
        a.dir = REAKTOR_LAY_COLUMN;  a.w = 100.0f; a.ml = 30.0f; a.mt = 40.0f;
        b2.dir = REAKTOR_LAY_COLUMN; b2.w = 100.0f; b2.ml = 30.0f; b2.mt = 200.0f;
        kid.h = 25.0f; kid.flags = REAKTOR_LAY_FILL_X;

        reaktor_layout_begin(l, R(0.0f, 0.0f, 400.0f, 400.0f));
        reaktor_layout_open(l, 600u, &a);
        reaktor_layout_leaf(l, 601u, &kid);
        reaktor_layout_close(l);
        reaktor_layout_open(l, 610u, &b2);
        reaktor_layout_leaf(l, 611u, &kid);
        reaktor_layout_close(l);
        reaktor_layout_end(l);

        reaktor_layout_rect(l, 600u, &ra);
        reaktor_layout_rect(l, 610u, &rb);
        ok("the first is where its margins put it",
           (int)ra.x == 30 && (int)ra.y == 40, 1);
        ok("the second is where ITS margins put it, not after the first",
           (int)rb.x == 30 && (int)rb.y == 200, 1);
        ok("and each is as tall as its own child",
           (int)ra.h == 25 && (int)rb.h == 25, 1);
    }

    puts("");
    puts("a container with no height of its own");
    {
        reaktor_box col = { 0 }, a = { 0 }, c = { 0 };
        struct nk_rect r;

        col.dir   = REAKTOR_LAY_COLUMN;
        col.w     = 200.0f;          /* width given, height left to the children */
        col.gap   = 7.0f;
        a.h = 30.0f; a.flags = REAKTOR_LAY_FILL_X;
        c.h = 19.0f; c.flags = REAKTOR_LAY_FILL_X;

        reaktor_layout_begin(l, R(0.0f, 0.0f, 400.0f, 400.0f));
        reaktor_layout_open(l, 500u, &col);
        reaktor_layout_leaf(l, 501u, &a);
        reaktor_layout_leaf(l, 502u, &c);
        reaktor_layout_close(l);
        reaktor_layout_end(l);

        ok("it is as tall as what is in it, gap included",
           reaktor_layout_rect(l, 500u, &r) ? (int)r.h : -1, 30 + 7 + 19);
        ok("...and as wide as it asked to be",
           reaktor_layout_rect(l, 500u, &r) ? (int)r.w : -1, 200);
    }

    puts("");
    puts("more boxes than there is room for");
    b.w = 4.0f;
    b.h = 4.0f;
    reaktor_layout_begin(l, R(0.0f, 0.0f, 100.0f, 100.0f));
    for (i = 0; i < REAKTOR_LAY_MAX + 40; i++)
        reaktor_layout_leaf(l, (unsigned)(1000 + i), &b);
    reaktor_layout_end(l);
    {
        struct nk_rect r;
        /* 256 boxes fit, and the root is one of them. */
        ok("the overflow is counted", l->overflow_boxes, 41);
        ok("the boxes that fitted still came out",
           reaktor_layout_rect(l, 1000u, &r), 1);
        ok("the ones past the end did not",
           reaktor_layout_rect(l, (unsigned)(1000 + REAKTOR_LAY_MAX + 10), &r), 0);
    }

    puts("");
    puts("nested deeper than the stack");
    {
        struct nk_rect r;
        int i;

        b.w = 10.0f; b.h = 10.0f;
        reaktor_layout_begin(l, R(0.0f, 0.0f, 100.0f, 100.0f));
        for (i = 0; i < REAKTOR_LAY_DEPTH + 5; i++)
            reaktor_layout_open(l, (unsigned)(3000 + i), &b);
        /* The one that discriminates: an open past the stack must still count.
         * Stopping at the stack's size is what let a later close pop something
         * it never pushed, and the closing loop below hides that on its own -
         * the clamp brings depth back to 1 either way. */
        ok("an open past the stack still counts",
           l->depth, REAKTOR_LAY_DEPTH + 6);
        for (i = 0; i < REAKTOR_LAY_DEPTH + 5; i++)
            reaktor_layout_close(l);
        /* The whole point: a box declared after all that is still the root's
         * child, not stranded inside a container that was never popped. */
        reaktor_layout_leaf(l, 3999u, &b);
        reaktor_layout_end(l);
        ok("every open was balanced by its close", l->depth, 1);
        ok("...so what came after is still placed",
           reaktor_layout_rect(l, 3999u, &r), 1);
    }

    puts("");
    puts("a container that overflowed the arena");
    {
        struct nk_rect r;
        int i;

        reaktor_layout_begin(l, R(0.0f, 0.0f, 100.0f, 100.0f));
        for (i = 0; i < REAKTOR_LAY_MAX; i++)
            reaktor_layout_leaf(l, (unsigned)(4000 + i), &b);
        /* No room left, so this container has no item at all - and its
         * children must not be handed one that is not there. */
        reaktor_layout_open(l, 4900u, &b);
        reaktor_layout_leaf(l, 4901u, &b);
        reaktor_layout_close(l);
        reaktor_layout_end(l);
        ok("it survived being asked", l->depth, 1);
        ok("the lost container is not placed",
           reaktor_layout_rect(l, 4900u, &r), 0);
    }

    puts("");
    puts("closing more than was opened");
    reaktor_layout_begin(l, R(0.0f, 0.0f, 100.0f, 100.0f));
    reaktor_layout_close(l);
    reaktor_layout_close(l);
    reaktor_layout_leaf(l, 2000u, &b);
    reaktor_layout_end(l);
    {
        struct nk_rect r;
        ok("the root survived it", reaktor_layout_rect(l, 2000u, &r), 1);
    }
}

int
main(void)
{
    static reaktor_layout l;

    memset(&l, 0, sizeof(l));
    test_lag(&l);
    test_origin(&l);
    test_weight(&l);
    test_gap(&l);
    test_nesting(&l);
    test_centring(&l);
    test_limits(&l);
    reaktor_layout_free(&l);

    printf("\n%s\n", g_fail ? "FAILED" : "layout: all checks passed");
    return g_fail ? 1 : 0;
}
