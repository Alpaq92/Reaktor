#include <stdio.h>

#include "internal.h"
#include "declare.h"
#include "reaktor/launch.h"

App                      *g_frame_app;
struct nk_context        *g_frame_ctx;
static int                g_depth;
static int                g_space;
int                       g_frame_unsettled;

#define REAKTOR_SETTLE_TRIES 16
static const char        *g_stuck;

float                     g_frame_width[REAKTOR_LAY_DEPTH + 1];
int                       g_frame_widths;

static float              g_ox, g_oy;

static struct nk_rect     g_boxrect[REAKTOR_LAY_DEPTH + 1];
static unsigned char      g_boxok[REAKTOR_LAY_DEPTH + 1];
static unsigned           g_boxid[REAKTOR_LAY_DEPTH + 1];

void
reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area)
{
    g_frame_app   = app;
    g_frame_ctx   = ctx;
    g_depth     = 0;
    g_space     = 0;
    g_frame_unsettled = 0;
    g_frame_width[0]  = area.w;
    g_frame_widths    = 1;
    g_stuck     = NULL;
    reaktor_layout_begin(&app->lay, area);
}

void
reaktor_frame_end(void)
{
    if (!g_frame_app) return;
    if (g_space) { nk_layout_space_end(g_frame_ctx); g_space = 0; }
    reaktor_layout_end(&g_frame_app->lay);

    /* This frame's rectangles were laid out with the sizes of an older sheet. */
    if (g_frame_app->style_loads != reaktor_style_loads()) {
        g_frame_app->style_loads = reaktor_style_loads();
        g_frame_unsettled++;
    }
    g_frame_app->settled_run = g_frame_unsettled ? 0 : g_frame_app->settled_run + 1;
    if (!g_frame_unsettled) {
        g_frame_app->settle_tries = 0;
    } else if (g_frame_app->settle_tries < REAKTOR_SETTLE_TRIES) {
        g_frame_app->settle_tries++;
        g_frame_app->dirty = 1;
        reaktor_wake(g_frame_app);
    } else if (g_frame_app->settle_tries == REAKTOR_SETTLE_TRIES) {
        g_frame_app->settle_tries++;
        fprintf(stderr,
                "reaktor: the declared tree did not settle after %d frames - "
                "%d of %d boxes have no rect, the first of them named \"%s\".\n"
                "reaktor: a box is found again by its name, so a widget whose "
                "name changes every frame is a new one every frame; and a node "
                "reported only on some frames renumbers everything after it.\n",
                REAKTOR_SETTLE_TRIES, g_frame_unsettled, g_frame_app->lay.count,
                g_stuck ? g_stuck : "(unnamed)");
    }
    g_frame_app = NULL;
    g_frame_ctx = NULL;
}

int
decl_push_font(const char *selector)
{
    reaktor_style st;

    if (!selector || !g_frame_ctx) return 0;
    reaktor_style_get(selector, &st);
    if (!st.matched || st.font_px <= 0) return 0;
    nk_style_push_font(g_frame_ctx, pick_font(g_frame_app, st.font_px, st.bold));
    return 1;
}

static struct nk_rect
in_tree(struct nk_rect r)
{
    r.x -= g_ox;
    r.y -= g_oy;
    return r;
}

static struct nk_rect
on_screen(struct nk_rect r)
{
    return g_space ? nk_layout_space_rect_to_screen(g_frame_ctx, in_tree(r)) : r;
}

void
reaktor_box_open(unsigned char dir, const reaktor_box *b)
{
    reaktor_box box;
    unsigned    id;

    if (!g_frame_app) return;
    box = *b;
    box.dir = dir;

    if (g_depth == 0) {
        float room = nk_window_get_content_region_size(g_frame_ctx).x;

        if (box.w <= 0.0f || ((box.flags & REAKTOR_LAY_FILL_X) && box.w < room))
            box.w = room;
        box.flags &= ~(unsigned)REAKTOR_LAY_FILL_X;
    }

    id = reaktor_note_push(g_frame_app, REAKTOR_A11Y_GROUP, box.name, NULL, 0u,
                           nk_rect(0, 0, 0, 0));
    reaktor_layout_open(&g_frame_app->lay, id, &box);
    if (g_depth < REAKTOR_LAY_DEPTH) g_boxid[g_depth + 1] = id;

    if (g_frame_widths < (int)(sizeof(g_frame_width) / sizeof(g_frame_width[0]))) {
        float w = box.w > 0.0f ? box.w : g_frame_width[g_frame_widths - 1];

        w -= box.ml + box.mr;
        g_frame_width[g_frame_widths++] = w > 0.0f ? w : 0.0f;
    }

    {
        struct nk_rect r;

        if (reaktor_layout_rect(&g_frame_app->lay, id, &r)) {
            if (g_depth == 0) {
                g_ox = r.x;
                g_oy = r.y;
                nk_layout_space_begin(g_frame_ctx, NK_STATIC, r.h, REAKTOR_LAY_MAX);
                g_space = 1;
            }
            reaktor_note_bounds(g_frame_app, id, on_screen(r));
            if (g_depth < REAKTOR_LAY_DEPTH) {
                g_boxrect[g_depth + 1] = on_screen(r);
                g_boxok[g_depth + 1]   = 1;
            }
        } else {
            if (g_depth < REAKTOR_LAY_DEPTH) g_boxok[g_depth + 1] = 0;
            if (!g_stuck) g_stuck = box.name ? box.name : "(a container)";
            g_frame_unsettled++;
        }
    }
    g_depth++;
}

unsigned
reaktor_box_id(void)
{
    if (g_depth <= 0 || g_depth > REAKTOR_LAY_DEPTH) return 0;
    return g_boxid[g_depth];
}

int
reaktor_box_rect(struct nk_rect *out)
{
    if (!out || g_depth <= 0 || g_depth > REAKTOR_LAY_DEPTH) return 0;
    if (!g_boxok[g_depth]) return 0;
    *out = g_boxrect[g_depth];
    return 1;
}

void
reaktor_box_close(void)
{
    if (!g_frame_app) return;
    if (g_depth > 0) g_depth--;
    if (g_frame_widths > 1) g_frame_widths--;
    if (g_depth == 0 && g_space) {
        nk_layout_space_end(g_frame_ctx);
        g_space = 0;
    }
    reaktor_layout_close(&g_frame_app->lay);
    reaktor_note_pop(g_frame_app);
}

unsigned
decl_note(unsigned char role, const char *name, const char *value, unsigned state)
{
    return reaktor_note(g_frame_app, role, name, value, state, nk_rect(0, 0, 0, 0));
}

int
decl_emit(unsigned id, const char *keys, const reaktor_box *b)
{
    struct nk_rect r;

    reaktor_layout_leaf(&g_frame_app->lay, id, b);
    if (keys) reaktor_note_keys(g_frame_app, id, keys);

    if (!g_space) { g_frame_unsettled++; return 0; }
    if (!reaktor_layout_rect(&g_frame_app->lay, id, &r)) { g_frame_unsettled++; return 0; }
    reaktor_note_bounds(g_frame_app, id, on_screen(r));

    nk_layout_space_push(g_frame_ctx, in_tree(r));
    return 1;
}

float
decl_width_of(unsigned id)
{
    struct nk_rect r;

    return reaktor_layout_rect(&g_frame_app->lay, id, &r) ? r.w : 0.0f;
}

int
decl_place(unsigned char role, const char *name, const char *value, unsigned state,
           const char *keys, const reaktor_box *b, unsigned *out_id)
{
    unsigned id;

    if (!g_frame_app) return 0;
    id = decl_note(role, name, value, state);
    if (out_id) *out_id = id;
    if (decl_emit(id, keys, b)) return 1;
    if (!g_stuck) g_stuck = name;
    return 0;
}

const struct nk_user_font *
decl_style_font(const char *selector)
{
    reaktor_style st;

    if (selector) {
        reaktor_style_get(selector, &st);
        if (st.matched && st.font_px > 0)
            return pick_font(g_frame_app, st.font_px, st.bold);
    }
    return g_frame_ctx->style.font;
}

void
decl_box_default(reaktor_box *b, float w, float h)
{
    if (w > 0.0f && b->w <= 0.0f && !(b->flags & REAKTOR_LAY_FILL_X)) b->w = w;
    if (h > 0.0f && b->h <= 0.0f && !(b->flags & REAKTOR_LAY_FILL_Y)) b->h = h;
}

static void
overlay(reaktor_style *base, const reaktor_style *over)
{
    int i;

    if (!over->matched) return;
    base->matched = 1;
    memcpy(base->bg, over->bg, 4);
    memcpy(base->fg, over->fg, 4);
    memcpy(base->border_col, over->border_col, 4);
    for (i = 0; i < 4; i++) {
        if (over->pad[i] > 0.0f)      base->pad[i]      = over->pad[i];
        if (over->border_w[i] > 0.0f) base->border_w[i] = over->border_w[i];
        if (over->radius[i] > 0.0f)   base->radius[i]   = over->radius[i];
        if (over->margin[i] > 0.0f)   base->margin[i]   = over->margin[i];
    }
    if (over->width > 0.0f)       base->width       = over->width;
    if (over->height > 0.0f)      base->height      = over->height;
    if (over->min_width > 0.0f)   base->min_width   = over->min_width;
    if (over->min_height > 0.0f)  base->min_height  = over->min_height;
    if (over->max_width > 0.0f)   base->max_width   = over->max_width;
    if (over->max_height > 0.0f)  base->max_height  = over->max_height;
    if (over->line_height > 0.0f) base->line_height = over->line_height;
    if (over->font_px > 0)        base->font_px     = over->font_px;
}

const char *
decl_rule_or(const char *selector, const char *element)
{
    reaktor_style st;

    if (!selector) return element;
    reaktor_style_get(selector, &st);
    return st.matched ? selector : element;
}

void
decl_box_from_style(reaktor_box *box, const char *text, const char *selector,
                    const char *element, float pad_x, float pad_y)
{
    const char                *sel = element ? element : selector;
    const struct nk_user_font *f;
    reaktor_style              st;
    float                      extra_w, extra_h;

    memset(&st, 0, sizeof(st));
    if (element) reaktor_style_get(element, &st);
    if (selector) {
        reaktor_style narrow;

        reaktor_style_get(selector, &narrow);
        if (narrow.matched) {
            sel = selector;
            overlay(&st, &narrow);
        }
    }
    f = decl_style_font(sel);

    if (st.matched) {
        extra_w = st.pad[REAKTOR_SIDE_LEFT] + st.pad[REAKTOR_SIDE_RIGHT]
                + st.border_w[REAKTOR_SIDE_LEFT]
                + st.border_w[REAKTOR_SIDE_RIGHT];
        extra_h = st.pad[REAKTOR_SIDE_TOP] + st.pad[REAKTOR_SIDE_BOTTOM]
                + st.border_w[REAKTOR_SIDE_TOP]
                + st.border_w[REAKTOR_SIDE_BOTTOM];
    } else {
        extra_w = 2.0f * pad_x;
        extra_h = 2.0f * pad_y;
    }

    if (box->w <= 0.0f && text && !(box->flags & REAKTOR_LAY_FILL_X)) {
        float w = st.width > 0.0f
                ? st.width
                : f->width(f->userdata, f->height, text, (int)strlen(text))
                  + extra_w;
        if (w < st.min_width) w = st.min_width;
        if (st.max_width > 0.0f && w > st.max_width) w = st.max_width;
        box->w = w;
    }
    if (box->h <= 0.0f && !(box->flags & REAKTOR_LAY_FILL_Y)) {
        float line = st.line_height > 0.0f ? st.line_height : f->height;
        float h    = st.height > 0.0f ? st.height : line + extra_h;
        if (h < st.min_height) h = st.min_height;
        if (st.max_height > 0.0f && h > st.max_height) h = st.max_height;
        box->h = h;
    }

    if (st.matched) {
        static const int side[4] = { REAKTOR_SIDE_LEFT, REAKTOR_SIDE_TOP,
                                     REAKTOR_SIDE_RIGHT, REAKTOR_SIDE_BOTTOM };
        float *m[4] = { &box->ml, &box->mt, &box->mr, &box->mb };
        unsigned char dir = reaktor_layout_open_dir(&g_frame_app->lay);
        int k;

        for (k = 0; k < 4; k++)
            if (*m[k] <= 0.0f && dir != (k & 1 ? REAKTOR_LAY_ROW : REAKTOR_LAY_COLUMN)) {
                *m[k] = st.margin[side[k]];
                box->flags |= REAKTOR_LAY_SHEET_L << k;
            }
    }
}

void
reaktor_gap(float w, float h)
{
    reaktor_box box;

    memset(&box, 0, sizeof(box));
    box.w = w;
    box.h = h;
    (void)decl_place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &box, NULL);
}

void
reaktor_soak(void)
{
    reaktor_box box;

    memset(&box, 0, sizeof(box));
    box.flags = REAKTOR_LAY_FILL_X;
    (void)decl_place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &box, NULL);
}

void
decl_tip_at(const char *tip, struct nk_rect r, int cursor)
{
    const struct nk_input *in = &g_frame_ctx->input;

    if (!tip) return;
    reaktor_hot_follow(g_frame_app, r, cursor);
    if (nk_input_is_mouse_hovering_rect(in, r) &&
        nk_input_is_mouse_hovering_rect(in, g_frame_ctx->current->layout->clip))
        g_frame_app->tip = tip;
}

void
reaktor_tip_draw(App *app, struct nk_context *ctx, struct nk_command_buffer *cv)
{
    const struct nk_user_font *f = reaktor_style_font(app, ".tooltip", 13, 0);
    struct nk_vec2 m = ctx->input.mouse.pos;
    reaktor_style  s;
    struct nk_rect b;
    float          px, py, round;
    int            len = (int)SDL_strlen(app->tip);

    reaktor_style_get(".tooltip", &s);
    px    = s.pad_x;
    py    = s.pad_y;
    round = s.rounding;
    b = nk_rect(m.x + 14.0f, m.y + 18.0f,
                f->width(f->userdata, f->height, app->tip, len) + 2.0f * px,
                f->height + 2.0f * py);
    if (b.x + b.w > (float)app->laid_w) b.x = m.x - 4.0f - b.w;
    if (b.y + b.h > (float)app->laid_h) b.y = m.y - 4.0f - b.h;
    if (b.x < 0.0f) b.x = 0.0f;
    if (b.y < 0.0f) b.y = 0.0f;
    reaktor_fill_round(app, cv, b, round, reaktor_col(s.bg));
    if (s.border > 0.0f)
        reaktor_edge_round(app, cv, b, round, s.border, reaktor_col(s.border_col));
    nk_draw_text(cv, nk_rect(b.x + px, b.y + py, b.w - 2.0f * px, f->height),
                 app->tip, len, f, nk_rgba(0, 0, 0, 0),
                 s.fg[3] ? reaktor_col(s.fg) : ctx->style.text.color);
    app->tip = NULL;
}

int
reaktor_frame_settled(const App *app)
{
    return app->settled_run >= 2 || app->settle_tries > REAKTOR_SETTLE_TRIES;
}
