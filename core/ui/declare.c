#include <stdio.h>

#include "internal.h"
#include "declare.h"

static App               *g_app;
static struct nk_context *g_ctx;
static int                g_depth;
static int                g_space;
static int                g_unsettled;
static int                g_settled_run;

#define REAKTOR_SETTLE_TRIES 16
static int                g_settle_tries;
static const char        *g_stuck;

static float              g_width[REAKTOR_LAY_DEPTH + 1];
static int                g_widths;

static float              g_ox, g_oy;

static struct nk_rect     g_boxrect[REAKTOR_LAY_DEPTH + 1];
static unsigned char      g_boxok[REAKTOR_LAY_DEPTH + 1];
static unsigned           g_boxid[REAKTOR_LAY_DEPTH + 1];

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
    if (g_space) { nk_layout_space_end(g_ctx); g_space = 0; }
    reaktor_layout_end(&g_app->lay);

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

    if (g_depth == 0 && box.w <= 0.0f)
        box.w = nk_window_get_content_region_size(g_ctx).x;

    id = reaktor_note_push(g_app, REAKTOR_A11Y_GROUP, box.name, NULL, 0u,
                           nk_rect(0, 0, 0, 0));
    reaktor_layout_open(&g_app->lay, id, &box);
    if (g_depth < REAKTOR_LAY_DEPTH) g_boxid[g_depth + 1] = id;

    if (g_widths < (int)(sizeof(g_width) / sizeof(g_width[0]))) {
        float w = box.w > 0.0f ? box.w : g_width[g_widths - 1];

        w -= box.ml + box.mr;
        g_width[g_widths++] = w > 0.0f ? w : 0.0f;
    }

    {
        struct nk_rect r;

        if (reaktor_layout_rect(&g_app->lay, id, &r)) {
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

static unsigned
note_of(unsigned char role, const char *name, const char *value, unsigned state)
{
    return reaktor_note(g_app, role, name, value, state, nk_rect(0, 0, 0, 0));
}

static int
emit(unsigned id, const char *keys, const reaktor_box *b)
{
    struct nk_rect r;

    reaktor_layout_leaf(&g_app->lay, id, b);
    if (keys) reaktor_note_keys(g_app, id, keys);

    if (!g_space) { g_unsettled++; return 0; }
    if (!reaktor_layout_rect(&g_app->lay, id, &r)) { g_unsettled++; return 0; }
    reaktor_note_bounds(g_app, id, on_screen(r));

    nk_layout_space_push(g_ctx, in_tree(r));
    return 1;
}

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

static void
box_default(reaktor_box *b, float w, float h)
{
    if (w > 0.0f && b->w <= 0.0f && !(b->flags & REAKTOR_LAY_FILL_X)) b->w = w;
    if (h > 0.0f && b->h <= 0.0f && !(b->flags & REAKTOR_LAY_FILL_Y)) b->h = h;
}

static float
row_height(float extra)
{
    return g_ctx->style.font->height + extra;
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

static const char *
rule_or(const char *selector, const char *element)
{
    reaktor_style st;

    if (!selector) return element;
    reaktor_style_get(selector, &st);
    return st.matched ? selector : element;
}

static void
box_from_style(reaktor_box *box, const char *text, const char *selector,
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
    f = style_font(sel);

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
        if (box->mt <= 0.0f) box->mt = st.margin[REAKTOR_SIDE_TOP];
        if (box->mr <= 0.0f) box->mr = st.margin[REAKTOR_SIDE_RIGHT];
        if (box->mb <= 0.0f) box->mb = st.margin[REAKTOR_SIDE_BOTTOM];
        if (box->ml <= 0.0f) box->ml = st.margin[REAKTOR_SIDE_LEFT];
    }
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
    const char    *sel;
    unsigned       id = 0;
    int            hit, styled, fitted;

    if (!g_app || !s) return 0;
    box = s->box;
    box_from_style(&box, s->label, s->style, "button",
                   g_ctx->style.button.padding.x,
                   g_ctx->style.button.padding.y);

    if (!place(REAKTOR_A11Y_BUTTON, s->name ? s->name : s->label, NULL,
               s->disabled ? REAKTOR_A11Y_DISABLED : 0u, s->keys, &box, &id))
        return 0;

    r = nk_widget_bounds(g_ctx);

    sel = rule_or(s->style, "button");
    styled = push_style_font(s->style);
    if (s->repeat) nk_button_set_behavior(g_ctx, NK_BUTTON_REPEATER);
    if (s->disabled) nk_widget_disable_begin(g_ctx);
    fitted = reaktor_fit_label(g_app, g_ctx, r);
    reaktor_note_mute(g_app, 1);
    if (s->icon && !s->label)
        hit = nk_button_image(g_ctx, reaktor_ionicon(g_app, s->icon,
                                                     (int)(r.h * 0.6f)));
    else if (s->icon)
        hit = reaktor_button_icon_as(g_app, g_ctx, sel, s->icon, s->label);
    else if (s->accent)
        hit = reaktor_button_accent_as(g_app, g_ctx, sel, s->label);
    else
        hit = reaktor_button_label_as(g_app, g_ctx, sel, s->label);
    reaktor_note_mute(g_app, 0);
    reaktor_unfit_label(g_ctx, fitted);
    if (s->disabled) nk_widget_disable_end(g_ctx);
    if (s->repeat) nk_button_set_behavior(g_ctx, NK_BUTTON_DEFAULT);
    if (styled) nk_style_pop_font(g_ctx);

    if (reaktor_focus_activated(g_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    return hit;
}

#define WRAP_LINES 256

typedef struct wrapped {
    float space;
    int   lines, end[WRAP_LINES];
} wrapped;

static int
wrap_guess(const struct nk_user_font *f, const char *s, int len, float space)
{
    float   w = 0.0f;
    int     at = 0;
    nk_rune cp;

    while (at < len) {
        int   n = nk_utf_decode(s + at, &cp, len - at);
        float g;

        if (n <= 0) return at ? at : len;
        g = f->width(f->userdata, f->height, s + at, n);
        if (w + g > space && at > 0) {
            int brk;

            if (cp == ' ') return at + n;
            brk = reaktor_text_break(s, len, at);
            return brk > 0 ? brk : at;
        }
        w  += g;
        at += n;
    }
    return len;
}

static int
wrap_fit(const struct nk_user_font *f, const char *s, int len, float space)
{
    int end = wrap_guess(f, s, len, space);

    for (;;) {
        int trim = end, brk;

        while (trim > 0 && s[trim - 1] == ' ') trim--;
        if (trim <= 0 || f->width(f->userdata, f->height, s, trim) <= space)
            return end;
        brk = reaktor_text_break(s, len, trim - 1);
        if (brk <= 0 || brk >= end) return end;
        end = brk;
    }
}

static int
wrap_lines(const struct nk_user_font *f, const char *s, float space, wrapped *w)
{
    int len = (int)strlen(s), done = 0, lines = 0;

    while (done < len) {
        done += wrap_fit(f, s + done, len - done, space);
        if (lines < WRAP_LINES) w->end[lines] = done;
        lines++;
    }
    w->space = space;
    w->lines = lines;
    return lines > 0 ? lines : 1;
}

static void
wrap_draw(const struct nk_user_font *f, const char *s, struct nk_color fg,
          const wrapped *w)
{
    const struct nk_style *st = &g_ctx->style;
    struct nk_rect         b, line;
    int                    len = (int)strlen(s), done = 0, k, rtl;
    char                   marked[1024];

    if (nk_widget(&b, g_ctx) == NK_WIDGET_INVALID) return;
    rtl  = reaktor_text_rtl(s, len);
    line = nk_rect(b.x + st->text.padding.x, b.y + st->text.padding.y,
                   b.w - 2.0f * st->text.padding.x, f->height);
    fg = nk_rgb_factor(fg, st->text.color_factor);
    for (k = 0; done < len && line.y + line.h <= b.y + b.h; k++) {
        int            n = w && w->space == line.w && k < w->lines && k < WRAP_LINES
                         ? w->end[k] - done
                         : wrap_fit(f, s + done, len - done, line.w);
        const char    *text = s + done;
        int            bytes = n;
        struct nk_rect at = line;

        if (rtl) {
            float width;

            if (n + 3 <= (int)sizeof marked) {
                memcpy(marked, "\xE2\x80\x8F", 3);
                memcpy(marked + 3, text, (size_t)n);
                text  = marked;
                bytes = n + 3;
            }
            width = f->width(f->userdata, f->height, text, bytes);
            at.x += at.w - width;
            at.w  = width;
        } else {
            while (bytes > 0 && text[bytes - 1] == ' ') bytes--;
        }
        nk_draw_text(nk_window_get_canvas(g_ctx), at, text, bytes, f,
                     st->window.background, fg);
        done   += n;
        line.y += f->height;
    }
}

void
reaktor_label(const reaktor_label_spec *s)
{
    reaktor_box                box;
    const struct nk_user_font *f;
    wrapped                    lines;
    unsigned                   id;
    int                        styled, measured = 0;

    if (!g_app || !s || !s->text) return;
    box = s->box;
    id  = note_of(s->silent ? REAKTOR_A11Y_NONE : REAKTOR_A11Y_LABEL,
                  s->name ? s->name : (s->silent ? NULL : s->text),
                  s->value, 0u);
    f   = style_font(s->style);

    if (s->wrap) {
        struct nk_vec2 pad = g_ctx->style.text.padding;
        float own   = width_of(id);
        float avail = (float)(int)(own > 0.0f ? own : g_width[g_widths - 1]);
        struct nk_rect prev;

        if (box.h <= 0.0f) {
            box.h = f->height * (float)wrap_lines(f, s->text, avail - 2.0f * pad.x,
                                                  &lines)
                  + 2.0f * pad.y;
            measured = 1;
        }
        if (!reaktor_layout_rect(&g_app->lay, id, &prev) || prev.h != box.h)
            g_unsettled++;
    } else {
        box_default(&box,
                    f->width(f->userdata, f->height, s->text,
                             (int)strlen(s->text)),
                    f->height);
    }

    if (!emit(id, NULL, &box)) return;

    {
        nk_flags a = s->align == REAKTOR_CENTRE ? NK_TEXT_CENTERED
                   : s->align == REAKTOR_RIGHT  ? NK_TEXT_RIGHT
                                                : NK_TEXT_LEFT;
        struct nk_color c = reaktor_token(s->color, g_ctx->style.text.color);

        styled = push_style_font(s->style);
        if (s->wrap) wrap_draw(f, s->text, c, measured ? &lines : NULL);
        else         nk_label_colored(g_ctx, s->text, a, c);
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

    if (!g_app || !s || !s->buf || !s->len) return;
    box = s->box;
    box_from_style(&box, NULL, s->style, "input", 0.0f, 10.0f);

    if (!place(REAKTOR_A11Y_TEXTBOX, s->name ? s->name : s->hint,
               s->buf[0] ? s->buf : NULL, 0u, NULL, &box, NULL))
        return;
    reaktor_note_mute(g_app, 1);
    (void)reaktor_field_text(g_app, g_ctx,
                             s->multiline ? NK_EDIT_BOX : NK_EDIT_FIELD,
                             s->buf, s->len, s->cap, s->hint, s->filter,
                             s->pad_x, s->pad_y);
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
    box_from_style(&box, s->text, s->style, "a", 0.0f, 0.0f);

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
    box_default(&box, 40.0f, 0.0f);
    box_default(&box, 0.0f, box.w);

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
    int         changed, styled;

    if (!g_app || !s || (!s->on && !s->flags)) return 0;
    on = s->on ? *s->on : (nk_bool)((*s->flags & s->bit) != 0);
    was = on;

    box = s->box;
    box_from_style(&box, s->label, s->style, "input", 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_CHECKBOX, s->name ? s->name : s->label, NULL,
               on ? REAKTOR_A11Y_CHECKED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_app, id)) on = !on;

    styled = push_style_font(s->style ? s->style : "input");

    reaktor_note_mute(g_app, 1);
    if (s->box_right)
        nk_checkbox_label_align(g_ctx, s->label, &on,
                                NK_WIDGET_RIGHT, NK_TEXT_LEFT);
    else
        nk_checkbox_label(g_ctx, s->label, &on);
    reaktor_note_mute(g_app, 0);
    if (styled) nk_style_pop_font(g_ctx);

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
    int         on, hit, styled;

    if (!g_app || !s || !s->choice) return 0;
    on = (*s->choice == s->value);

    box = s->box;
    box_from_style(&box, s->label, s->style, "input", 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_RADIO, s->name ? s->name : s->label, NULL,
               on ? REAKTOR_A11Y_CHECKED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_app, id)) { *s->choice = s->value; on = 1; }

    styled = push_style_font(s->style ? s->style : "input");

    reaktor_note_mute(g_app, 1);
    hit = reaktor_radio_label(g_app, g_ctx, s->label, on);
    reaktor_note_mute(g_app, 0);
    if (styled) nk_style_pop_font(g_ctx);

    if (hit) *s->choice = s->value;
    return hit;
}

int
reaktor_select(const reaktor_select_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;
    nk_bool     was;
    int         hit, styled;

    if (!g_app || !s || !s->on) return 0;
    was = *s->on;

    box = s->box;
    box_from_style(&box, s->label, s->style, "select", 0.0f, 0.0f);

    if (!place(REAKTOR_A11Y_LISTITEM, s->name ? s->name : s->label, NULL,
               was ? REAKTOR_A11Y_SELECTED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_app, id)) *s->on = !*s->on;

    styled = push_style_font(s->style ? s->style : "select");

    {
        nk_flags align = s->centered ? NK_TEXT_CENTERED : NK_TEXT_LEFT;
        struct nk_rect b = nk_widget_bounds(g_ctx);

        reaktor_note_mute(g_app, 1);
        if (s->icon) {
            struct nk_color accent = reaktor_token("--links",
                                                   g_ctx->style.text.color);
            struct nk_color ink = *s->on
                ? reaktor_on(accent)
                : reaktor_token("--text-muted", g_ctx->style.text.color);

            hit = nk_selectable_image_label(g_ctx,
                      reaktor_ionicon_col(g_app, s->icon, 16, ink),
                      s->label, align, s->on);
        } else if (s->disc) {
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
    if (styled) nk_style_pop_font(g_ctx);
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
    box_from_style(&box, NULL,
                   s->style ? s->style : "input.type-range",
                   "input", 0.0f, 7.0f);

    if (!place(REAKTOR_A11Y_SLIDER, s->name, text, 0u, NULL, &box, &id))
        return;

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
    box_from_style(&box, NULL, s->style, "progress", 0.0f, 7.0f);

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
    box_default(&box, 62.0f, 0.0f);
    box_default(&box, 0.0f, box.w);

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
    box_from_style(&box, NULL, s->style, "input", 0.0f, 7.0f);

    if (!place(REAKTOR_A11Y_SPINBUTTON, s->name ? s->name : s->label, text,
               0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_app, id, (float)now, (float)s->lo, (float)s->hi,
                       (float)s->step);

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
    float          cw;
    int            open;

    if (!g_app || !s) return 0;
    box = s->box;
    box_from_style(&box, NULL, s->style, "select", 0.0f, 9.0f);

    if (!place(REAKTOR_A11Y_COMBOBOX, s->name ? s->name : s->label, s->label,
               0u, NULL, &box, NULL))
        return 0;

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

    if (s->disc) {
        struct nk_rect im = reaktor_combo_content(g_ctx, h);

        im.w = im.h = h.h - 2.0f * g_ctx->style.combo.content_padding.y;
        im.x = h.x + g_ctx->style.combo.content_padding.x;
        im.y = h.y + g_ctx->style.combo.content_padding.y;
        reaktor_glyph_at(g_app, g_ctx, im, REAKTOR_DISC_ROUND,
                         g_ctx->style.combo.symbol_normal, (int)im.w, 0.0f);
    } else if (s->swatch) {
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

    if (!g_app || !s || !s->value) return;
    c = nk_rgb_cf(*s->value);
    SDL_snprintf(hex, sizeof(hex), "#%02x%02x%02x", c.r, c.g, c.b);

    box = s->box;
    box_default(&box, 210.0f, 132.0f);

    if (!place(REAKTOR_A11Y_GROUP, s->name, hex, 0u, NULL, &box, NULL))
        return;

    reaktor_note_mute(g_app, 1);
    nk_color_pick(g_ctx, s->value, NK_RGB);
    reaktor_note_mute(g_app, 0);
}
