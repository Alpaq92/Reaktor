/* declare.c - see declare.h.
 *
 * Every widget here is four steps and no drawing of its own:
 *
 *   1. report itself to the accessibility tree, which answers with an id;
 *   2. declare a box under that id, and ask layout where the same id went on
 *      the previous frame;
 *   3. hand that rect to Nuklear as an absolute placement;
 *   4. draw through the imperative helper that already knows how.
 *
 * Step 4 is why this file is short and why behaviour cannot drift: nothing is
 * reimplemented. Step 1 comes first because the id is what steps 2 and 3 both
 * need, and reaktor_note computes it from the tree's shape rather than from
 * anything on screen - so it is available before the widget knows where it is.
 *
 * Step 3 is the one with a trap in it. nk_layout_space_begin opens a row, and
 * calling it per widget stacks a row per widget and advances the panel's
 * cursor under every one of them. So the *outermost* container opens exactly
 * one space, every widget inside pushes into that, and the rect is converted
 * with nk_layout_space_rect_to_local because a pushed rect is local to the space
 * and ours are the window's.
 */
#include <stdio.h>

#include "internal.h"
#include "declare.h"

/* The frame being described. See declare.h on why this is not an argument. */
static App               *g_app;
static struct nk_context *g_ctx;
static int                g_depth;   /* containers open */
static int                g_space;   /* a Nuklear space is open */
/* Boxes that had nowhere to go this frame, or that are still changing size.
 * Non-zero means the frame just drawn is not the final one. */
static int                g_unsettled;
static int                g_settled_run;

/* A tree that never stops moving.
 *
 * Settling is normally three frames: a box is measured on the frame that
 * declares it and drawn on the next, and a wrapped paragraph needs one more
 * for the container above it. But two mistakes make it never happen, and
 * both are silent - the page simply never draws, because every frame asks
 * for another one.
 *
 *   - A box is found again next frame by an id built from its parent, its
 *     role and its name. A widget whose name changes every frame is a new
 *     box every frame, so it never has a rect and never settles. A readout
 *     is the usual way in: its text is its name unless it is given one.
 *
 *   - Reporting a node only on the frames where something is known changes
 *     the shape of the tree, and an id is computed from that shape - so the
 *     boxes after it are renumbered, lose their rects, and take the node
 *     away again on the next frame. It oscillates and never converges.
 *
 * So the asking is bounded. Past this many consecutive unsettled frames the
 * frame stops requesting redraws, says what it knows, and lets the page draw
 * whatever it has - a page missing a widget beats an application spinning at
 * 100% that never puts anything on screen. Well above the three a correct
 * page takes, and above the handful a theme change costs. */
#define REAKTOR_SETTLE_TRIES 16
static int                g_settle_tries;
static const char        *g_stuck;    /* first box with no rect this frame */

/* The width each open container will hand its children, innermost last.
 *
 * A wrapped paragraph needs a width before it can have a height, and asking
 * the layout means asking about last frame - which on the first frame is
 * nothing, so the paragraph came out one line tall and everything below it
 * rode up until the frame after. That is a visible jump, and it is avoidable:
 * the container's width is known the moment it is declared, so a child that
 * fills can be measured against it straight away. */
static float              g_width[REAKTOR_LAY_DEPTH + 1];
static int                g_widths;

/* Where the open tree starts, in the layout's own coordinates.
 *
 * Everything below is pushed relative to this rather than converted from it,
 * and that is the whole of what makes scrolling work. A rect pushed into a
 * Nuklear space is local to the space, and the space's screen position moves
 * with the scroll every frame. A rect that has been through the layout is a
 * frame old. Subtracting a fresh screen position from a stale rect leaves the
 * frame's worth of scrolling in the answer - which is a page whose contents
 * trail the scrollbar for as long as it is dragged.
 *
 * So the layout never sees the scroll at all: local coordinates go in, local
 * coordinates come out, and Nuklear applies this frame's scroll to them. */
static float              g_ox, g_oy;

/* Where each open container landed on screen, innermost last, and whether it
 * has landed at all. A row is not a widget and has no bounds of its own, so
 * anything painted behind a container's children - a table's zebra, a card's
 * fill - has to ask. Index 0 is unused: depth 0 means no container is open. */
static struct nk_rect     g_boxrect[REAKTOR_LAY_DEPTH + 1];
static unsigned char      g_boxok[REAKTOR_LAY_DEPTH + 1];

void
reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area)
{
    g_app   = app;
    g_ctx   = ctx;
    g_depth     = 0;
    g_space     = 0;
    g_unsettled = 0;
    g_width[0]  = area.w;
    g_widths    = 1;
    g_stuck     = NULL;
    reaktor_layout_begin(&app->lay, area);
}

void
reaktor_frame_end(void)
{
    if (!g_app) return;
    /* A page that left a container open still gets its space closed, because
     * Nuklear would draw the rest of the frame into it otherwise. */
    if (g_space) { nk_layout_space_end(g_ctx); g_space = 0; }
    reaktor_layout_end(&g_app->lay);

    /* Anything declared for the first time was measured just now and has
     * nowhere to have been drawn, so the frame that shows it is the next one -
     * and this application only draws when something asks it to. On a desktop
     * a stray event usually arrives and hides that; in a browser nothing does,
     * and the page came up as an empty card that stayed empty. So the frame
     * that measures asks for the frame that draws. */
    /* A run, not a flag. A paragraph settles one frame before the container
     * holding it does, because the container's height is only known once the
     * paragraph's is - so a single quiet frame is not proof the tree has
     * stopped moving, and two consecutive ones are. */
    g_settled_run = g_unsettled ? 0 : g_settled_run + 1;
    if (!g_unsettled) {
        g_settle_tries = 0;
    } else if (g_settle_tries < REAKTOR_SETTLE_TRIES) {
        SDL_Event e;

        g_settle_tries++;
        g_app->dirty = 1;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
    } else if (g_settle_tries == REAKTOR_SETTLE_TRIES) {
        /* Once per run of them, not once per frame. */
        g_settle_tries++;
        fprintf(stderr,
                "reaktor: the declared tree did not settle after %d frames - "
                "%d of %d boxes have no rect, the first of them named \"%s\".\n"
                "reaktor: a box is found again by its name, so a widget whose "
                "name changes every frame is a new one every frame; and a node "
                "reported only on some frames renumbers everything after it.\n",
                REAKTOR_SETTLE_TRIES, g_unsettled, g_app->lay.count,
                g_stuck ? g_stuck : "(unnamed)");
    }
    g_app = NULL;
    g_ctx = NULL;
}

/* The type a selector asks for, pushed for one widget. Answers whether
 * anything was pushed, so the caller knows whether to pop. A selector with no
 * rule behind it leaves the frame's own font in place rather than guessing. */
static int
push_style_font(const char *selector)
{
    reaktor_style st;

    if (!selector || !g_ctx) return 0;
    reaktor_style_get(selector, &st);
    if (!st.matched || st.font_px <= 0) return 0;
    nk_style_push_font(g_ctx, pick_font(g_app, st.font_px, st.bold));
    return 1;
}

/* A rect the layout produced, as Nuklear's space wants it: local to the tree
 * rather than to the window. */
static struct nk_rect
in_tree(struct nk_rect r)
{
    r.x -= g_ox;
    r.y -= g_oy;
    return r;
}

/* And where that lands on screen this frame, scroll and all - which is what
 * the accessibility tree reports and what a magnifier follows. */
static struct nk_rect
on_screen(struct nk_rect r)
{
    return g_space ? nk_layout_space_rect_to_screen(g_ctx, in_tree(r)) : r;
}

void
reaktor_box_open(unsigned char dir, const reaktor_box *b)
{
    reaktor_box box;
    unsigned    id;

    if (!g_app) return;
    box = *b;
    box.dir = dir;

    /* A tree with no width of its own fills the panel it is in.
     *
     * Asked of the panel rather than peeked from the row cursor, and that is
     * the whole difference. nk_widget_bounds answers where the *next* widget
     * would go, so a peek taken after another declared tree has closed its
     * space answers whatever that space left behind - which is how a page
     * ended up with containers 0 and 27 pixels wide, and why "two declared
     * blocks side by side must be one container" was a rule anyone writing a
     * page had to know. The panel's content region does not move with the
     * cursor, so there is nothing to be stale. */
    if (g_depth == 0 && box.w <= 0.0f)
        box.w = nk_window_get_content_region_size(g_ctx).x;

    /* A container is a group in the tree: no name of its own, and a reader
     * walks through it to its children. */
    id = reaktor_note_push(g_app, REAKTOR_A11Y_GROUP, box.name, NULL, 0u,
                           nk_rect(0, 0, 0, 0));
    reaktor_layout_open(&g_app->lay, id, &box);

    if (g_widths < (int)(sizeof(g_width) / sizeof(g_width[0]))) {
        float w = box.w > 0.0f ? box.w : g_width[g_widths - 1];

        w -= box.ml + box.mr;
        g_width[g_widths++] = w > 0.0f ? w : 0.0f;
    }

    {
        struct nk_rect r;

        if (reaktor_layout_rect(&g_app->lay, id, &r)) {
            /* One space for the whole tree, opened by whichever container is
             * outermost. A page that declares nothing opens none, which is
             * what lets the old API keep working beside this one. */
            if (g_depth == 0) {
                g_ox = r.x;
                g_oy = r.y;
                nk_layout_space_begin(g_ctx, NK_STATIC, r.h, REAKTOR_LAY_MAX);
                g_space = 1;
            }
            reaktor_note_bounds(g_app, id, on_screen(r));
            if (g_depth < REAKTOR_LAY_DEPTH) {
                g_boxrect[g_depth + 1] = on_screen(r);
                g_boxok[g_depth + 1]   = 1;
            }
        } else {
            if (g_depth < REAKTOR_LAY_DEPTH) g_boxok[g_depth + 1] = 0;
            if (!g_stuck) g_stuck = box.name ? box.name : "(a container)";
            g_unsettled++;
        }
    }
    g_depth++;
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
    if (!g_app) return;
    if (g_depth > 0) g_depth--;
    if (g_widths > 1) g_widths--;
    if (g_depth == 0 && g_space) {
        nk_layout_space_end(g_ctx);
        g_space = 0;
    }
    reaktor_layout_close(&g_app->lay);
    reaktor_note_pop(g_app);
}

/* Steps 1 to 3, which every widget shares. Answers 0 when layout has not
 * placed this box yet - its first frame, and the frame after the tree changed
 * shape - in which case the caller draws nothing rather than somewhere wrong.
 * `role` of NONE reports no node at all, for a box that is only spacing. */
static unsigned
note_of(unsigned char role, const char *name, const char *value, unsigned state)
{
    return reaktor_note(g_app, role, name, value, state, nk_rect(0, 0, 0, 0));
}

/* Declares the box under an id already taken, finds where that id went last
 * frame, and hands the rect to Nuklear. Answers 0 when there is nowhere to
 * draw yet - a box's first frame, and the frame after the tree changed shape -
 * in which case the caller draws nothing rather than somewhere wrong.
 *
 * Split from note_of because a wrapped label has to know its width before it
 * can say how tall it is, and the width it gets is the one its id had last
 * frame. So: take the id, measure against it, then declare. */
static int
emit(unsigned id, const char *keys, const reaktor_box *b)
{
    struct nk_rect r;

    /* Declared before anything else can go wrong. A box that is not declared
     * is not measured, and a box that is not measured never gets a rect. */
    reaktor_layout_leaf(&g_app->lay, id, b);
    if (keys) reaktor_note_keys(g_app, id, keys);

    if (!g_space) { g_unsettled++; return 0; }
    if (!reaktor_layout_rect(&g_app->lay, id, &r)) { g_unsettled++; return 0; }
    reaktor_note_bounds(g_app, id, on_screen(r));

    nk_layout_space_push(g_ctx, in_tree(r));
    return 1;
}

/* How wide this id came out last frame, or 0 if it has not been placed. */
static float
width_of(unsigned id)
{
    struct nk_rect r;

    return reaktor_layout_rect(&g_app->lay, id, &r) ? r.w : 0.0f;
}

static int
place(unsigned char role, const char *name, const char *value, unsigned state,
      const char *keys, const reaktor_box *b, unsigned *out_id)
{
    unsigned id;

    if (!g_app) return 0;
    id = note_of(role, name, value, state);
    if (out_id) *out_id = id;
    if (emit(id, keys, b)) return 1;
    if (!g_stuck) g_stuck = name;
    return 0;
}

/* The font a selector asks for, or the frame's own when it names no rule. */
static const struct nk_user_font *
style_font(const char *selector)
{
    reaktor_style st;

    if (selector) {
        reaktor_style_get(selector, &st);
        if (st.matched && st.font_px > 0)
            return pick_font(g_app, st.font_px, st.bold);
    }
    return g_ctx->style.font;
}

/* A box's own size, or what the text needs in the font a selector chose. */
static void
size_to_text(reaktor_box *box, const char *text, const char *selector,
             float pad_x, float pad_y)
{
    const struct nk_user_font *f = g_ctx->style.font;
    reaktor_style              st;

    if (selector) {
        reaktor_style_get(selector, &st);
        if (st.matched && st.font_px > 0)
            f = pick_font(g_app, st.font_px, st.bold);
    }
    /* Only on an axis nothing else decides. A box that fills has its size
     * chosen for it, and an intrinsic size there is not a preference but a
     * floor - which is why three buttons in a row came out as wide as their
     * labels plus a share each, rather than as three equal columns. A caller
     * that does want a floor sets it, and this leaves it alone. */
    if (box->w <= 0.0f && text && !(box->flags & REAKTOR_LAY_FILL_X))
        box->w = f->width(f->userdata, f->height, text, (int)strlen(text))
               + 2.0f * pad_x;
    if (box->h <= 0.0f && !(box->flags & REAKTOR_LAY_FILL_Y))
        box->h = f->height + 2.0f * pad_y;
}

void
reaktor_gap(float w, float h)
{
    reaktor_box box;

    memset(&box, 0, sizeof(box));
    box.w = w;
    box.h = h;
    (void)place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &box, NULL);
}

void
reaktor_soak(void)
{
    reaktor_box box;

    memset(&box, 0, sizeof(box));
    box.flags = REAKTOR_LAY_FILL_X;
    (void)place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &box, NULL);
}

int
reaktor_button(const reaktor_button_spec *s)
{
    reaktor_box    box;
    struct nk_rect r;
    unsigned       id = 0;
    int            hit, styled, fitted;

    if (!g_app || !s) return 0;
    box = s->box;
    size_to_text(&box, s->label, s->style,
                 g_ctx->style.button.padding.x, g_ctx->style.button.padding.y);

    if (!place(REAKTOR_A11Y_BUTTON, s->name ? s->name : s->label, NULL,
               s->disabled ? REAKTOR_A11Y_DISABLED : 0u, s->keys, &box, &id))
        return 0;

    /* The rect the layout gave it, which place() has just pushed into the
     * space - and which nk_widget_bounds answers, because that is the rect
     * the next widget will be drawn into. Wanted twice below, and neither
     * time can use the caller's box: a box that fills has no size of its own
     * on that axis, and size_to_text leaves it alone on purpose. */
    r = nk_widget_bounds(g_ctx);

    styled = push_style_font(s->style);
    if (s->repeat) nk_button_set_behavior(g_ctx, NK_BUTTON_REPEATER);
    if (s->disabled) nk_widget_disable_begin(g_ctx);
    /* For the icon-only branch below, which draws through Nuklear directly.
     * Everything else here goes through css_button, and push_button_style
     * bounds the padding itself - it has to, because it pushes the
     * stylesheet's padding after this and would otherwise undo it. */
    fitted = reaktor_fit_label(g_app, g_ctx, r);
    /* The helpers below report themselves - they were written to be called
     * directly by a page. Here the node already exists, so the inner one is
     * muted rather than allowed to arrive as a duplicate. */
    reaktor_note_mute(g_app, 1);
    /* At 0.6 of the height the layout gave it. Sizing the glyph from the
     * caller's box instead asked for 0.6 of nothing on a button that fills
     * its row, and nk_button_image stretched whatever came back across the
     * whole button - which read as a bad icon rather than as a size asked
     * for wrong. */
    if (s->icon && !s->label)
        hit = nk_button_image(g_ctx, reaktor_ionicon(g_app, s->icon,
                                                     (int)(r.h * 0.6f)));
    else if (s->icon)   hit = reaktor_button_icon(g_app, g_ctx, s->icon, s->label);
    else if (s->accent) hit = reaktor_button_accent(g_app, g_ctx, s->label);
    else                hit = reaktor_button_label(g_app, g_ctx, s->label);
    reaktor_note_mute(g_app, 0);
    reaktor_unfit_label(g_ctx, fitted);
    if (s->disabled) nk_widget_disable_end(g_ctx);
    if (s->repeat) nk_button_set_behavior(g_ctx, NK_BUTTON_DEFAULT);
    if (styled) nk_style_pop_font(g_ctx);

    if (reaktor_focus_activated(g_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    return hit;
}

void
reaktor_label(const reaktor_label_spec *s)
{
    reaktor_box                box;
    const struct nk_user_font *f;
    unsigned                   id;
    int                        styled;

    if (!g_app || !s || !s->text) return;
    box = s->box;
    /* A silent label adds no node, so its name is never read - it is only
     * ever an identity. Identifying it by its own text would make it a new
     * box every time that text changed, which for a readout is every frame,
     * and a box that is new every frame never has a rect to draw into. So a
     * silent label with no name of its own has none at all, and its siblings
     * are told apart the way a row of unlabelled buttons is: by how many
     * with the same parent and role came before it. */
    id  = note_of(s->silent ? REAKTOR_A11Y_NONE : REAKTOR_A11Y_LABEL,
                  s->name ? s->name : (s->silent ? NULL : s->text),
                  s->value, 0u);
    f   = style_font(s->style);

    if (s->wrap) {
        /* As many lines as this width takes, at the width it had last frame.
         * The +2 is the same slack the imperative caption used: a wrap breaks
         * on words, so the last line is short and one more may be started. */
        /* This label's own width if the layout has one, and the width its
         * container is about to hand it if not - which is what makes the
         * first drawn frame the right height rather than one line. */
        float own   = width_of(id);
        float avail = (own > 0.0f ? own : g_width[g_widths - 1]) - 4.0f;
        float tw    = f->width(f->userdata, f->height, s->text,
                               (int)strlen(s->text));
        int   lines = (avail > 1.0f && tw > avail) ? (int)(tw / avail) + 2 : 1;
        struct nk_rect prev;

        if (box.h <= 0.0f) box.h = (f->height + 3.0f) * (float)lines;
        /* The height just worked out is not the height it was drawn at last
         * time, so the frame being measured is not the one to keep. Without
         * this the paragraph settles at one line and stays there, because
         * nothing else has any reason to ask for a redraw. */
        if (!reaktor_layout_rect(&g_app->lay, id, &prev) || prev.h != box.h)
            g_unsettled++;
    } else {
        if (box.w <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_X))
            box.w = f->width(f->userdata, f->height, s->text,
                             (int)strlen(s->text));
        if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y))
            box.h = f->height;
    }

    if (!emit(id, NULL, &box)) return;

    {
        nk_flags a = s->align == REAKTOR_CENTRE ? NK_TEXT_CENTERED
                   : s->align == REAKTOR_RIGHT  ? NK_TEXT_RIGHT
                                                : NK_TEXT_LEFT;

        styled = push_style_font(s->style);
        if (s->colour) {
            struct nk_color c = reaktor_token(s->colour,
                                              g_ctx->style.text.color);
            if (s->wrap) nk_label_colored_wrap(g_ctx, s->text, c);
            else         nk_label_colored(g_ctx, s->text, a, c);
        } else if (s->wrap) {
            nk_label_wrap(g_ctx, s->text);
        } else {
            nk_label(g_ctx, s->text, a);
        }
        if (styled) nk_style_pop_font(g_ctx);
    }
}

void
reaktor_icon(const reaktor_icon_spec *s)
{
    reaktor_box     box;
    struct nk_image im;
    int             px;

    if (!g_app || !s || !s->name) return;
    box = s->box;
    if (box.w <= 0.0f) box.w = box.h > 0.0f ? box.h : 24.0f;
    if (box.h <= 0.0f) box.h = box.w;
    px = (int)box.w;

    /* The bare name, not a path: reaktor_ionicon builds the path, applies the
     * hairline rule for small sizes and reads the artwork straight out of the
     * submodule. Handing it a path made it build a second one and the icon
     * came out blank. */
    im = s->accent
       ? reaktor_ionicon_col(g_app, s->name, px,
                             reaktor_token("--links", g_app->text))
       : reaktor_ionicon(g_app, s->name, px);

    if (!place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &box, NULL)) return;
    reaktor_image(g_app, g_ctx, im, px);
}

void
reaktor_field(const reaktor_field_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;

    if (!g_app || !s || !s->buf || !s->len) return;
    box = s->box;
    if (box.h <= 0.0f) box.h = g_ctx->style.font->height + 20.0f;

    /* The hint names the field when nothing else does - it is what a sighted
     * user reads off the empty box - and the value is whatever has been typed
     * into it, which is nothing until it is. */
    if (!place(REAKTOR_A11Y_TEXTBOX, s->name ? s->name : s->hint,
               s->buf[0] ? s->buf : NULL, 0u, NULL, &box, &id))
        return;
    reaktor_note_mute(g_app, 1);
    (void)reaktor_field_text(g_app, g_ctx,
                             s->multiline ? NK_EDIT_BOX : NK_EDIT_FIELD,
                             s->buf, s->len, s->cap, s->hint, s->filter);
    reaktor_note_mute(g_app, 0);
}

int
reaktor_link(const reaktor_link_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;
    int         hit, styled;

    if (!g_app || !s || !s->text) return 0;
    box = s->box;
    size_to_text(&box, s->text, s->style, 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_LINK, s->name ? s->name : s->text, NULL,
               s->active ? REAKTOR_A11Y_SELECTED : 0u, NULL, &box, &id))
        return 0;

    styled = push_style_font(s->style);
    reaktor_note_mute(g_app, 1);
    hit = reaktor_link_label(g_app, g_ctx, s->text, s->active);
    reaktor_note_mute(g_app, 0);
    if (styled) nk_style_pop_font(g_ctx);

    if (reaktor_focus_activated(g_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    return hit;
}

int
reaktor_frame_settled(void)
{
    /* Giving up counts. Whoever is waiting for the tree to hold still - the
     * accessibility dump is the one that matters - would otherwise wait for
     * a frame that is never coming, and the broken tree is the thing they
     * wanted to look at. */
    return g_settled_run >= 2 || g_settle_tries > REAKTOR_SETTLE_TRIES;
}

int
reaktor_swatch(const reaktor_swatch_spec *s)
{
    reaktor_box box;
    char        hex[10];
    unsigned    id = 0;
    int         hit;

    if (!g_app || !s) return 0;
    box = s->box;
    if (box.w <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_X)) box.w = 40.0f;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y)) box.h = box.w;

    /* The colour is the value: there is no text on it, and "#56c6ff" is the
     * only thing a reader could be told about a swatch beyond its name. */
    SDL_snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                 s->fill.r, s->fill.g, s->fill.b);
    if (!place(REAKTOR_A11Y_BUTTON, s->name, hex, 0u, NULL, &box, &id))
        return 0;

    reaktor_note_mute(g_app, 1);
    hit = reaktor_button_color(g_app, g_ctx, s->name, s->fill);
    reaktor_note_mute(g_app, 0);

    if (reaktor_focus_activated(g_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    return hit;
}

int
reaktor_check(const reaktor_check_spec *s)
{
    reaktor_box box;
    nk_bool     on, was;
    unsigned    id = 0;
    int         changed;

    if (!g_app || !s || (!s->on && !s->flags)) return 0;
    on = s->on ? *s->on : (nk_bool)((*s->flags & s->bit) != 0);
    was = on;

    box = s->box;
    size_to_text(&box, s->label, NULL, 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_CHECKBOX, s->name ? s->name : s->label, NULL,
               on ? REAKTOR_A11Y_CHECKED : 0u, NULL, &box, &id))
        return 0;

    /* Enter, or a screen reader's press, taken here rather than delivered as
     * a click on the box - see reaktor_focus_activated. */
    if (reaktor_focus_activated(g_app, id)) on = !on;

    reaktor_note_mute(g_app, 1);
    if (s->box_right)
        nk_checkbox_label_align(g_ctx, s->label, &on,
                                NK_WIDGET_RIGHT, NK_TEXT_LEFT);
    else
        nk_checkbox_label(g_ctx, s->label, &on);
    reaktor_note_mute(g_app, 0);

    changed = (on != was);
    if (s->on) *s->on = on;
    else if (on) *s->flags |= s->bit;
    else         *s->flags &= ~s->bit;
    return changed;
}

int
reaktor_radio(const reaktor_radio_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;
    int         on, hit;

    if (!g_app || !s || !s->choice) return 0;
    on = (*s->choice == s->value);

    box = s->box;
    size_to_text(&box, s->label, NULL, 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_RADIO, s->name ? s->name : s->label, NULL,
               on ? REAKTOR_A11Y_CHECKED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_app, id)) { *s->choice = s->value; on = 1; }

    reaktor_note_mute(g_app, 1);
    hit = reaktor_radio_label(g_app, g_ctx, s->label, on);
    reaktor_note_mute(g_app, 0);

    if (hit) *s->choice = s->value;
    return hit;
}

int
reaktor_select(const reaktor_select_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;
    nk_bool     was;
    int         hit;

    if (!g_app || !s || !s->on) return 0;
    was = *s->on;

    box = s->box;
    size_to_text(&box, s->label, NULL, 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_LISTITEM, s->name ? s->name : s->label, NULL,
               was ? REAKTOR_A11Y_SELECTED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_app, id)) *s->on = !*s->on;

    {
        nk_flags align = s->centred ? NK_TEXT_CENTERED : NK_TEXT_LEFT;
        struct nk_rect b = nk_widget_bounds(g_ctx);

        reaktor_note_mute(g_app, 1);
        if (s->icon) {
            /* The ink follows the selection, so the glyph is rasterised at
             * the colour this row is about to be drawn in. */
            struct nk_color accent = reaktor_token("--links",
                                                   g_ctx->style.text.color);
            struct nk_color ink = *s->on
                ? reaktor_on(accent)
                : reaktor_token("--text-muted", g_ctx->style.text.color);

            hit = nk_selectable_image_label(g_ctx,
                      reaktor_ionicon_col(g_app, s->icon, 16, ink),
                      s->label, align, s->on);
        } else if (s->disc) {
            /* NK_SYMBOL_NONE and the disc drawn into the slot Nuklear sized
             * for it: a circle is the one shape the software rasteriser
             * cannot draw, and nk_do_selectable_symbol's icon rect sits at
             * twice the style's padding from the left edge for any alignment
             * but NK_TEXT_LEFT, as tall as the row less that padding. */
            const struct nk_style_selectable *st = &g_ctx->style.selectable;
            struct nk_rect icon;

            hit = nk_selectable_symbol_label(g_ctx, NK_SYMBOL_NONE, s->label,
                                             align, s->on);
            icon.y = b.y + st->padding.y + st->image_padding.y;
            icon.x = b.x + 2.0f * st->padding.x + st->image_padding.x;
            icon.w = icon.h = b.h - 2.0f * st->padding.y;
            icon.w -= 2.0f * st->image_padding.x;
            icon.h -= 2.0f * st->image_padding.y;
            reaktor_glyph_at(g_app, g_ctx, icon, REAKTOR_DISC_ROUND,
                             *s->on ? st->text_pressed : st->text_normal,
                             (int)(icon.w < icon.h ? icon.w : icon.h), 0.0f);
        } else {
            hit = nk_selectable_label(g_ctx, s->label, align, s->on);
        }
        reaktor_note_mute(g_app, 0);
    }
    return hit;
}

void
reaktor_slider(const reaktor_slider_spec *s)
{
    reaktor_box box;
    char        text[32];
    unsigned    id = 0;
    float       now;

    if (!g_app || !s || (!s->value && !s->ivalue)) return;
    now = s->value ? *s->value : (float)*s->ivalue;

    if (s->text) SDL_strlcpy(text, s->text, sizeof(text));
    else if (s->value) SDL_snprintf(text, sizeof(text), "%.2f", (double)now);
    else SDL_snprintf(text, sizeof(text), "%d", *s->ivalue);

    box = s->box;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y))
        box.h = g_ctx->style.font->height + 14.0f;

    if (!place(REAKTOR_A11Y_SLIDER, s->name, text, 0u, NULL, &box, &id))
        return;

    /* The numbers behind the text, for a client that computes rather than
     * reads - see reaktor_note_range. */
    reaktor_note_range(g_app, id, now, s->lo, s->hi, s->step);

    reaktor_note_mute(g_app, 1);
    if (s->value)
        reaktor_slider_bar(g_app, g_ctx, id, s->value, s->lo, s->hi, s->step);
    else
        reaktor_slider_bar_int(g_app, g_ctx, id, s->ivalue, (int)s->lo,
                               (int)s->hi, (int)s->step);
    reaktor_note_mute(g_app, 0);
}

void
reaktor_progress(const reaktor_progress_spec *s)
{
    reaktor_box box;
    char        text[32];
    unsigned    id = 0;

    if (!g_app || !s || !s->value) return;
    SDL_snprintf(text, sizeof(text), "%d", (int)*s->value);

    box = s->box;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y))
        box.h = g_ctx->style.font->height + 14.0f;

    if (!place(REAKTOR_A11Y_PROGRESS, s->name, text, 0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_app, id, (float)*s->value, 0.0f, (float)s->max, 1.0f);

    reaktor_note_mute(g_app, 1);
    reaktor_progress_bar(g_app, g_ctx, s->value, s->max,
                         s->modifiable ? NK_MODIFIABLE : NK_FIXED);
    reaktor_note_mute(g_app, 0);
}

void
reaktor_knob(const reaktor_knob_spec *s)
{
    reaktor_box box;
    char        text[32];
    unsigned    id = 0;

    if (!g_app || !s || !s->value) return;
    SDL_snprintf(text, sizeof(text), "%.2f", (double)*s->value);

    box = s->box;
    if (box.w <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_X)) box.w = 62.0f;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y)) box.h = box.w;

    /* A slider to a reader: it is a value between two bounds, and nothing in
     * any platform's vocabulary is shaped like a knob. */
    if (!place(REAKTOR_A11Y_SLIDER, s->name, text, 0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_app, id, *s->value, s->lo, s->hi, 0.0f);

    reaktor_note_mute(g_app, 1);
    reaktor_knob_dial(g_app, g_ctx, s->value, s->lo, s->hi, NK_DOWN);
    reaktor_note_mute(g_app, 0);
}

void
reaktor_property(const reaktor_property_spec *s)
{
    reaktor_box    box;
    struct nk_rect r;
    char           text[32];
    unsigned       id = 0;
    double         now;

    if (!g_app || !s || (!s->ivalue && !s->fvalue && !s->dvalue)) return;
    now = s->ivalue ? (double)*s->ivalue
        : s->fvalue ? (double)*s->fvalue : *s->dvalue;

    if (s->ivalue) SDL_snprintf(text, sizeof(text), "%d", *s->ivalue);
    else SDL_snprintf(text, sizeof(text), "%.2f", now);

    box = s->box;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y))
        box.h = g_ctx->style.font->height + 14.0f;

    if (!place(REAKTOR_A11Y_SPINBUTTON, s->name ? s->name : s->label, text,
               0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_app, id, (float)now, (float)s->lo, (float)s->hi,
                       (float)s->step);

    /* The rect it was just pushed into, which the chrome is drawn over. */
    r = nk_widget_bounds(g_ctx);

    reaktor_note_mute(g_app, 1);
    reaktor_property_push(g_ctx);
    if (s->ivalue)
        nk_property_int(g_ctx, s->label, (int)s->lo, s->ivalue, (int)s->hi,
                        (int)s->step, s->grain);
    else if (s->fvalue)
        nk_property_float(g_ctx, s->label, (float)s->lo, s->fvalue,
                          (float)s->hi, (float)s->step, s->grain);
    else
        nk_property_double(g_ctx, s->label, s->lo, s->dvalue, s->hi,
                           s->step, s->grain);
    reaktor_property_pop(g_ctx);
    reaktor_property_chrome(g_app, g_ctx, r);
    reaktor_note_mute(g_app, 0);
}

int
reaktor_combo_open(const reaktor_combo_spec *s)
{
    reaktor_box    box;
    struct nk_rect h;
    unsigned       id = 0;
    float          cw;
    int            open;

    if (!g_app || !s) return 0;
    box = s->box;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y))
        box.h = g_ctx->style.font->height + 18.0f;

    if (!place(REAKTOR_A11Y_COMBOBOX, s->name ? s->name : s->label, s->label,
               0u, NULL, &box, &id))
        return 0;

    /* The popup takes an explicit size, so "as wide as the box" has to be
     * asked for - nk_widget_width is the box about to be emitted. */
    h  = nk_widget_bounds(g_ctx);
    cw = nk_widget_width(g_ctx);

    reaktor_note_mute(g_app, 1);
    if (s->disc)
        open = nk_combo_begin_symbol_label(g_ctx, s->label, NK_SYMBOL_NONE,
                                           nk_vec2(cw, s->body_h));
    else
        open = nk_combo_begin_label(g_ctx, s->swatch ? "" : s->label,
                                    nk_vec2(cw, s->body_h));
    reaktor_note_mute(g_app, 0);

    /* Drawn over the header whether the body opens or not, which is why it is
     * here and not beside the close. */
    if (s->disc) {
        struct nk_rect im = reaktor_combo_content(g_ctx, h);

        im.w = im.h = h.h - 2.0f * g_ctx->style.combo.content_padding.y;
        im.x = h.x + g_ctx->style.combo.content_padding.x;
        im.y = h.y + g_ctx->style.combo.content_padding.y;
        reaktor_glyph_at(g_app, g_ctx, im, REAKTOR_DISC_ROUND,
                         g_ctx->style.combo.symbol_normal, (int)im.w, 0.0f);
    } else if (s->swatch) {
        /* nk_combo_begin_color draws its swatch with a literal zero rounding
         * - not a style field - so a square block sits inside a box with an
         * 8px radius. There is no way to ask it for anything else. */
        struct nk_rect sw = reaktor_combo_content(g_ctx, h);
        float r = g_ctx->style.combo.rounding;

        nk_fill_rect(nk_window_get_canvas(g_ctx), sw,
                     r > sw.h * 0.5f ? sw.h * 0.5f : r, *s->swatch);
    }
    reaktor_combo_chrome(g_app, g_ctx, h, 2.0f);
    return open;
}

void
reaktor_combo_close(void)
{
    if (g_ctx) nk_combo_end(g_ctx);
}

int
reaktor_combo_item(const char *label, int chosen)
{
    struct nk_rect b;
    int            hit;

    if (!g_app || !label) return 0;
    b = nk_widget_bounds(g_ctx);
    reaktor_hot_top(g_app, b, 1, 1);
    reaktor_note(g_app, REAKTOR_A11Y_LISTITEM, label, NULL,
                 chosen ? REAKTOR_A11Y_SELECTED : 0u, b);
    hit = nk_combo_item_label(g_ctx, label, NK_TEXT_LEFT);
    return hit;
}

void
reaktor_colour_pick(const reaktor_colour_spec *s)
{
    reaktor_box     box;
    struct nk_color c;
    char            hex[10];
    unsigned        id = 0;

    if (!g_app || !s || !s->value) return;
    c = nk_rgb_cf(*s->value);
    SDL_snprintf(hex, sizeof(hex), "#%02x%02x%02x", c.r, c.g, c.b);

    box = s->box;
    if (box.w <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_X)) box.w = 210.0f;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y)) box.h = 132.0f;

    if (!place(REAKTOR_A11Y_GROUP, s->name, hex, 0u, NULL, &box, &id))
        return;

    reaktor_note_mute(g_app, 1);
    nk_color_pick(g_ctx, s->value, NK_RGB);
    reaktor_note_mute(g_app, 0);
}
