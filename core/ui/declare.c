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
    if (g_unsettled) {
        SDL_Event e;

        g_app->dirty = 1;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
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
        } else {
            g_unsettled++;
        }
    }
    g_depth++;
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
    return emit(id, keys, b);
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
    reaktor_box box;
    unsigned    id = 0;
    int         hit, styled;

    if (!g_app || !s) return 0;
    box = s->box;
    size_to_text(&box, s->label, s->style,
                 g_ctx->style.button.padding.x, g_ctx->style.button.padding.y);

    if (!place(REAKTOR_A11Y_BUTTON, s->name ? s->name : s->label, NULL,
               s->disabled ? REAKTOR_A11Y_DISABLED : 0u, s->keys, &box, &id))
        return 0;

    styled = push_style_font(s->style);
    if (s->repeat) nk_button_set_behavior(g_ctx, NK_BUTTON_REPEATER);
    if (s->disabled) nk_widget_disable_begin(g_ctx);
    /* The helpers below report themselves - they were written to be called
     * directly by a page. Here the node already exists, so the inner one is
     * muted rather than allowed to arrive as a duplicate. */
    reaktor_note_mute(g_app, 1);
    if (s->icon && !s->label)
        hit = nk_button_image(g_ctx, reaktor_ionicon(g_app, s->icon,
                                                     (int)(box.h * 0.6f)));
    else if (s->icon)   hit = reaktor_button_icon(g_app, g_ctx, s->icon, s->label);
    else if (s->accent) hit = reaktor_button_accent(g_app, g_ctx, s->label);
    else                hit = reaktor_button_label(g_app, g_ctx, s->label);
    reaktor_note_mute(g_app, 0);
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
    id  = note_of(REAKTOR_A11Y_LABEL, s->name ? s->name : s->text, NULL, 0u);
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

    styled = push_style_font(s->style);
    if (s->colour) {
        struct nk_color c = reaktor_token(s->colour, g_ctx->style.text.color);
        if (s->wrap) nk_label_colored_wrap(g_ctx, s->text, c);
        else nk_label_colored(g_ctx, s->text,
                              s->centred ? NK_TEXT_CENTERED : NK_TEXT_LEFT, c);
    } else if (s->wrap) {
        nk_label_wrap(g_ctx, s->text);
    } else {
        nk_label(g_ctx, s->text, s->centred ? NK_TEXT_CENTERED : NK_TEXT_LEFT);
    }
    if (styled) nk_style_pop_font(g_ctx);
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
    (void)reaktor_field_text(g_app, g_ctx, NK_EDIT_FIELD, s->buf, s->len,
                             s->cap, s->hint, NULL);
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
    return g_settled_run >= 2;
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
