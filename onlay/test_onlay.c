/* test_onlay.c - Onlay against known-good geometry.
 *
 * The first case is not invented: it is Reaktor's login card, with the
 * expected coordinates taken from the accessibility dump of the running
 * application rather than from Onlay. If Onlay disagrees with those numbers it
 * is Onlay that is wrong.
 *
 * The rest cover weighted tracks, which is the one piece of arithmetic Onlay
 * adds to what it was forked from. */
#include <stdio.h>
#include <string.h>

#include "onlay.h"

static int g_fail;

static void
check(const char *what, int got, int expect)
{
    int ok = got == expect;
    if (!ok) g_fail++;
    printf("    %-26s %5d  expected %5d   %s\n",
           what, got, expect, ok ? "ok" : "MISMATCH");
}

/* --- the login card ------------------------------------------------------
 * Card 420x354. Content inset by 22 horizontally and 18 vertically, rows
 * stacked with a 15px gap. */
#define CARD_W  420
#define CARD_H  354
#define PAD_X    22
#define PAD_Y    18
#define ROW_GAP  15

static void
test_login_card(void)
{
    static const struct { const char *name; int h, y; } rows[] = {
        { "brand row",           28,  18 },
        { "field",               38,  61 },
        { "continue with email", 40, 114 },
        { "or",                  18, 169 },
        { "use a passkey",       40, 202 },
        { "use biometrics",      40, 257 },
        { "contact",             18, 312 },
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    lay_context ctx;
    lay_id card, col, kid[7];
    int i;

    puts("  Reaktor's login card, against the app's own accessibility dump");
    lay_init_context(&ctx);
    lay_reserve_items_capacity(&ctx, 64);

    card = lay_item(&ctx);
    lay_set_size_xy(&ctx, card, CARD_W, CARD_H);
    lay_set_contain(&ctx, card, LAY_COLUMN);

    col = lay_item(&ctx);
    lay_set_behave(&ctx, col, LAY_FILL);
    /* A container that does not say how to distribute slack centres it -
     * LAY_MIDDLE is zero. Saying START is not optional. */
    lay_set_contain(&ctx, col, LAY_COLUMN | LAY_START);
    lay_set_margins_ltrb(&ctx, col, PAD_X, PAD_Y, PAD_X, PAD_Y);
    /* The gap is the container's business, not each child's. Expressed as a
     * margin on every row but the first, this produces the same seven
     * coordinates - which is what makes it a test of gap rather than of the
     * numbers. */
    lay_set_gap(&ctx, col, ROW_GAP);
    lay_insert(&ctx, card, col);

    for (i = 0; i < n; i++) {
        kid[i] = lay_item(&ctx);
        lay_set_size_xy(&ctx, kid[i], 0, (lay_scalar)rows[i].h);
        lay_set_behave(&ctx, kid[i], LAY_HFILL | LAY_TOP);
        lay_insert(&ctx, col, kid[i]);
    }

    lay_run_context(&ctx);

    for (i = 0; i < n; i++) {
        lay_vec4 r = lay_get_rect(&ctx, kid[i]);
        check(rows[i].name, (int)r.v[1], rows[i].y);
        if (r.v[0] != PAD_X || r.v[2] != CARD_W - 2 * PAD_X) {
            printf("    %-26s x=%d w=%d  expected x=%d w=%d   MISMATCH\n",
                   rows[i].name, (int)r.v[0], (int)r.v[2],
                   PAD_X, CARD_W - 2 * PAD_X);
            g_fail++;
        }
    }
    lay_destroy_context(&ctx);
}

/* --- proportional columns ------------------------------------------------
 * What nk_layout_row_push(0.2f / 0.5f / 0.3f) means on the Layout page. */
static void
test_weighted_columns(void)
{
    static const struct { float w; int expect_x, expect_w; } cols[] = {
        { 0.2f,   0,  60 },
        { 0.5f,  60, 150 },
        { 0.3f, 210,  90 },
    };
    const int n = 3;
    lay_context ctx;
    lay_id row, kid[3];
    int i;

    puts("  proportional columns: 0.2 / 0.5 / 0.3 of 300px");
    lay_init_context(&ctx);
    lay_reserve_items_capacity(&ctx, 16);

    row = lay_item(&ctx);
    lay_set_size_xy(&ctx, row, 300, 30);
    lay_set_contain(&ctx, row, LAY_ROW | LAY_START);

    for (i = 0; i < n; i++) {
        kid[i] = lay_item(&ctx);
        lay_set_behave(&ctx, kid[i], LAY_HFILL | LAY_TOP);
        lay_set_weight(&ctx, kid[i], cols[i].w);
        lay_insert(&ctx, row, kid[i]);
    }
    lay_run_context(&ctx);

    for (i = 0; i < n; i++) {
        lay_vec4 r = lay_get_rect(&ctx, kid[i]);
        char label[32];
        sprintf(label, "%.0f%% width", (double)(cols[i].w * 100.0f));
        check(label, (int)r.v[2], cols[i].expect_w);
        sprintf(label, "%.0f%% x", (double)(cols[i].w * 100.0f));
        check(label, (int)r.v[0], cols[i].expect_x);
    }
    lay_destroy_context(&ctx);
}

/* --- a floor, then growth ------------------------------------------------
 * nk_layout_row_template_push_variable(80): at least 80 pixels, then take an
 * equal share of whatever is left. */
static void
test_minimum_then_grow(void)
{
    lay_context ctx;
    lay_id row, a, b;
    lay_vec4 ra, rb;

    puts("  a floor of 80 beside an unconstrained sibling, in 300px");
    lay_init_context(&ctx);
    lay_reserve_items_capacity(&ctx, 16);

    row = lay_item(&ctx);
    lay_set_size_xy(&ctx, row, 300, 30);
    lay_set_contain(&ctx, row, LAY_ROW | LAY_START);

    a = lay_item(&ctx);
    lay_set_size_xy(&ctx, a, 80, 0);          /* the floor */
    lay_set_behave(&ctx, a, LAY_HFILL | LAY_TOP);
    lay_insert(&ctx, row, a);

    b = lay_item(&ctx);
    lay_set_behave(&ctx, b, LAY_HFILL | LAY_TOP);
    lay_insert(&ctx, row, b);

    lay_run_context(&ctx);
    ra = lay_get_rect(&ctx, a);
    rb = lay_get_rect(&ctx, b);
    /* 300 - 80 reserved = 220 shared two ways */
    check("floored item width", (int)ra.v[2], 190);
    check("sibling width",      (int)rb.v[2], 110);
    check("sibling x",          (int)rb.v[0], 190);
    lay_destroy_context(&ctx);
}


/* --- template rows, which is what "grid" means here ----------------------
 * nk_layout_row_template_* defines column rules once and reuses them down the
 * page: a fixed track, a "no narrower than 80, then grow" track, and a track
 * that just grows. Onlay has no grid primitive and does not need one - rows
 * that carry the same weights and floors put their columns in the same place,
 * which is what makes a template a grid.
 *
 * This test exists to prove that, so that nothing is added to Onlay for it. */
static void
test_template_rows(void)
{
    const int W = 500, COL_GAP = 10, ROW_GAP2 = 8, ROWS = 3;
    lay_scalar want_x[3] = { 0, 130, 360 };
    lay_scalar want_w[3] = { 120, 220, 140 };
    lay_context ctx;
    lay_id page, row, cell[3][3];
    int r, c;

    puts("  template rows: fixed 120 | floor 80 grow | grow, in 500px");
    lay_init_context(&ctx);
    lay_reserve_items_capacity(&ctx, 32);

    page = lay_item(&ctx);
    lay_set_size_xy(&ctx, page, W, 200);
    lay_set_contain(&ctx, page, LAY_COLUMN | LAY_START);
    lay_set_gap(&ctx, page, ROW_GAP2);

    for (r = 0; r < ROWS; r++) {
        row = lay_item(&ctx);
        lay_set_behave(&ctx, row, LAY_HFILL);
        lay_set_size_xy(&ctx, row, 0, 30);
        lay_set_contain(&ctx, row, LAY_ROW | LAY_START);
        lay_set_gap(&ctx, row, COL_GAP);
        lay_insert(&ctx, page, row);

        for (c = 0; c < 3; c++) {
            cell[r][c] = lay_item(&ctx);
            lay_insert(&ctx, row, cell[r][c]);
        }
        /* the template, identical in every row */
        lay_set_size_xy(&ctx, cell[r][0], 120, 0);              /* fixed */
        lay_set_behave(&ctx, cell[r][0], LAY_TOP);
        lay_set_size_xy(&ctx, cell[r][1], 80, 0);               /* floor */
        lay_set_behave(&ctx, cell[r][1], LAY_HFILL | LAY_TOP);
        lay_set_behave(&ctx, cell[r][2], LAY_HFILL | LAY_TOP);  /* grow */
    }

    lay_run_context(&ctx);

    for (r = 0; r < ROWS; r++)
        for (c = 0; c < 3; c++) {
            lay_vec4 q = lay_get_rect(&ctx, cell[r][c]);
            char label[40];
            sprintf(label, "row %d col %d x", r, c);
            check(label, (int)q.v[0], (int)want_x[c]);
            sprintf(label, "row %d col %d w", r, c);
            check(label, (int)q.v[2], (int)want_w[c]);
        }
    lay_destroy_context(&ctx);
}

int
main(void)
{
    printf("onlay: item %d bytes, rect %d bytes, scalar %d\n\n",
           (int)sizeof(lay_item_t), (int)sizeof(lay_vec4),
           (int)sizeof(lay_scalar));
    test_login_card();
    puts("");
    test_weighted_columns();
    puts("");
    test_minimum_then_grow();
    puts("");
    test_template_rows();
    printf("\n%s\n", g_fail ? "FAILED" : "onlay: all checks passed");
    return g_fail ? 1 : 0;
}
