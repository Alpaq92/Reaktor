#include <stdio.h>

#include "internal.h"
#include "declare.h"
#include "reaktor/launch.h"

static void
fit_button(reaktor_box *box, const reaktor_button_spec *s)
{
    const struct nk_style_button *nb = &g_frame_ctx->style.button;
    const char                   *sel = decl_rule_or(s->style, "button");
    float least = box->w, w;
    char  src[192];

    box->w = box->h = 0.0f;
    box->flags &= ~(REAKTOR_LAY_FILL);
    decl_box_from_style(box, NULL, s->style, "button", nb->padding.x, nb->padding.y);
    if (s->icon && !s->label)
        w = box->h;
    else
        w = css_button_content_w(g_frame_app, g_frame_ctx, sel, decl_style_font(sel),
                                 reaktor_button_src(src, s->icon), s->label);
    box->w = SDL_ceilf(SDL_max(w, least));
}

static float
disabled_opacity(const char *sel, const char *dsel)
{
    reaktor_style on, off;

    reaktor_style_get(sel, &on);
    reaktor_style_get(dsel, &off);
    if (!off.matched) return 0.5f;
    if (off.opacity < 1.0f) return off.opacity;
    return memcmp(on.bg, off.bg, 4) || memcmp(on.fg, off.fg, 4) ||
           memcmp(on.border_col, off.border_col, 4) ? 1.0f : 0.5f;
}

int
reaktor_button(const reaktor_button_spec *s)
{
    reaktor_box    box;
    struct nk_rect r;
    const char    *sel;
    char           dsel[96];
    unsigned       id = 0;
    int            hit, styled, fitted;
    float          fade = 1.0f;
    nk_size        mark = 0;

    if (!g_frame_app || !s) return 0;
    box = s->box;
    if (s->fit_content)
        fit_button(&box, s);
    else
        decl_box_from_style(&box, s->label, s->style, "button",
                            g_frame_ctx->style.button.padding.x,
                            g_frame_ctx->style.button.padding.y);

    if (!decl_place(REAKTOR_A11Y_BUTTON, s->name ? s->name : s->label, NULL,
                    s->disabled ? REAKTOR_A11Y_DISABLED : 0u, s->keys, &box, &id))
        return 0;

    r = nk_widget_bounds(g_frame_ctx);
    decl_tip_at(s->tooltip, r, REAKTOR_CURSOR_POINTER);

    sel = decl_rule_or(s->style, "button");
    if (s->disabled) {
        SDL_snprintf(dsel, sizeof(dsel), "%s.disabled:disabled", sel);
        fade = disabled_opacity(sel, dsel);
        sel = dsel;
    }
    styled = decl_push_font(s->style);
    if (s->repeat) nk_button_set_behavior(g_frame_ctx, NK_BUTTON_REPEATER);
    if (s->disabled) {
        nk_widget_disable_begin(g_frame_ctx);
        mark = reaktor_fade_mark(nk_window_get_canvas(g_frame_ctx));
    }
    fitted = reaktor_fit_label(g_frame_app, g_frame_ctx, r);
    reaktor_note_mute(g_frame_app, 1);
    if (s->paint) {
        hit = reaktor_css_button_custom(g_frame_app, g_frame_ctx, sel,
                                        s->name ? s->name : s->label, s->paint,
                                        s->paint_user);
    } else if (s->flat && s->icon && !s->label) {
        hit = reaktor_css_button_flat(g_frame_app, g_frame_ctx, s->icon, s->name,
                                      s->accent);
    } else if (s->icon && !s->label) {
        float px = (float)(int)(r.h * 0.6f);

        hit = reaktor_css_button_image(g_frame_app, g_frame_ctx, sel,
                                       reaktor_ionicon(g_frame_app, s->icon, (int)px),
                                       px, s->name);
    } else if (s->accent && !s->disabled)
        hit = reaktor_button_accent_as(g_frame_app, g_frame_ctx, sel, s->icon, s->label);
    else
        hit = reaktor_button_icon_as(g_frame_app, g_frame_ctx, sel, s->icon, s->label);
    reaktor_note_mute(g_frame_app, 0);
    reaktor_unfit_label(g_frame_ctx, fitted);
    if (s->disabled) {
        nk_widget_disable_end(g_frame_ctx);
        if (fade < 1.0f)
            reaktor_fade_since(g_frame_ctx, nk_window_get_canvas(g_frame_ctx), mark, fade);
    }
    if (s->repeat) nk_button_set_behavior(g_frame_ctx, NK_BUTTON_DEFAULT);
    if (styled) nk_style_pop_font(g_frame_ctx);

    if (reaktor_focus_activated(g_frame_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    return hit;
}

#define WRAP_LINES 256

typedef struct wrapped {
    const struct nk_user_font *f;
    float    space, height;
    unsigned hash, loads;
    int      lines, end[WRAP_LINES];
} wrapped;

static wrapped g_wraps[64];

static int
wrap_guess(const struct nk_user_font *f, const char *s, int len, float space)
{
    float   w = 0.0f;
    int     at = 0;
    nk_rune cp;

    while (at < len) {
        int   n, e = at;
        float g;

        while (e < len && s[e] != ' ') e++;
        if (e < len) e++;
        g = f->width(f->userdata, f->height, s + at, e - at);
        if (w + g <= space) {
            w  += g;
            at  = e;
            continue;
        }
        n = nk_utf_decode(s + at, &cp, len - at);

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
wrap_lines(unsigned id, const struct nk_user_font *f, const char *s,
           float space, const wrapped **out)
{
    wrapped    *w = &g_wraps[id & 63];
    unsigned    h = reaktor_fnv(REAKTOR_FNV, s), loads = reaktor_style_loads();
    int         len, done = 0, lines = 0;

    *out = w;
    if (w->hash == h && w->f == f && w->space == space &&
        w->height == f->height && w->loads == loads)
        return w->lines > 0 ? w->lines : 1;
    len = (int)SDL_strlen(s);
    while (done < len) {
        done += wrap_fit(f, s + done, len - done, space);
        if (lines < WRAP_LINES) w->end[lines] = done;
        lines++;
    }
    w->f      = f;
    w->space  = space;
    w->height = f->height;
    w->hash   = h;
    w->loads  = loads;
    w->lines  = lines;
    return lines > 0 ? lines : 1;
}

static void
wrap_draw(const struct nk_user_font *f, const char *s, struct nk_color fg,
          const wrapped *w)
{
    const struct nk_style *st = &g_frame_ctx->style;
    struct nk_rect         b, line;
    int                    len = (int)strlen(s), done = 0, k, rtl;
    char                   marked[1024];

    if (nk_widget(&b, g_frame_ctx) == NK_WIDGET_INVALID) return;
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
        nk_draw_text(nk_window_get_canvas(g_frame_ctx), at, text, bytes, f,
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
    const wrapped             *lines = NULL;
    unsigned                   id;
    int                        styled;

    if (!g_frame_app || !s || !s->text) return;
    box = s->box;
    id  = decl_note(s->silent ? REAKTOR_A11Y_NONE : REAKTOR_A11Y_LABEL,
                    s->name ? s->name : (s->silent ? NULL : s->text),
                    s->value, 0u);
    f   = decl_style_font(s->style);

    if (s->wrap) {
        struct nk_vec2 pad = g_frame_ctx->style.text.padding;
        float own   = decl_width_of(id);
        float avail = (float)(int)(own > 0.0f ? own : g_frame_width[g_frame_widths - 1]);
        struct nk_rect prev;
        int rows = wrap_lines(id, f, s->text, avail - 2.0f * pad.x, &lines);

        if (box.h <= 0.0f) box.h = f->height * (float)rows + 2.0f * pad.y;
        if (!reaktor_layout_rect(&g_frame_app->lay, id, &prev) || prev.h != box.h)
            g_frame_unsettled++;
    } else {
        float w = box.w > 0.0f || (box.flags & REAKTOR_LAY_FILL_X) ? 0.0f
                : reaktor_text_w(f, s->text);

        decl_box_default(&box, w, f->height);
    }

    if (!decl_emit(id, NULL, &box)) return;

    {
        nk_flags a = s->align == REAKTOR_CENTER ? NK_TEXT_CENTERED
                   : s->align == REAKTOR_RIGHT  ? NK_TEXT_RIGHT
                                                : NK_TEXT_LEFT;
        struct nk_color c = reaktor_token(s->color, g_frame_ctx->style.text.color);

        /* A sheet can repaint the page without redefining the token. */
        if (s->color)
            c = reaktor_visible(c, reaktor_page_color(g_frame_app),
                                g_frame_ctx->style.text.color);

        styled = decl_push_font(s->style);
        if (s->wrap) wrap_draw(f, s->text, c, lines);
        else         nk_label_colored(g_frame_ctx, s->text, a, c);
        if (styled) nk_style_pop_font(g_frame_ctx);
    }
}

void
reaktor_icon(const reaktor_icon_spec *s)
{
    reaktor_box     box;
    struct nk_image im;
    int             px;

    if (!g_frame_app || !s || (!s->name && !s->src)) return;
    box = s->box;
    if (box.w <= 0.0f) box.w = box.h > 0.0f ? box.h : 24.0f;
    if (box.h <= 0.0f) box.h = box.w;
    px = (int)(box.w > box.h ? box.w : box.h);

    im = s->src    ? reaktor_image_file(g_frame_app, s->src, px)
       : s->accent ? reaktor_ionicon_col(g_frame_app, s->name, px, g_frame_app->accent)
                   : reaktor_ionicon(g_frame_app, s->name, px);

    if (!decl_place(s->tooltip ? REAKTOR_A11Y_LABEL : REAKTOR_A11Y_NONE, s->tooltip,
                    NULL, 0u, NULL, &box, NULL))
        return;
    decl_tip_at(s->tooltip, nk_widget_bounds(g_frame_ctx), REAKTOR_CURSOR_DEFAULT);
    if (s->src) nk_image(g_frame_ctx, im);
    else        reaktor_image_centered(g_frame_ctx, im, px);
}

void
reaktor_field_select(App *app, const char *buf, int from, int to)
{
    app->field_pick.buf   = buf;
    app->field_pick.from  = from;
    app->field_pick.to    = to;
    app->field_pick.asked = 0;
    app->dirty = 1;
    reaktor_wake(app);
}

/* Room for what is typed or pasted this frame: Nuklear never writes past cap. */
static char *
field_room(const reaktor_field_spec *s, int *cap)
{
    int need = *s->len + 256;

    if (nk_input_is_key_pressed(&g_frame_ctx->input, NK_KEY_PASTE)) {
        char *clip = SDL_GetClipboardText();

        if (clip) need += (int)SDL_strlen(clip);
        SDL_free(clip);
    }
    if (!*s->heap || need > *s->heap_cap) {
        int   room = NK_MAX(need, *s->heap_cap * 2);
        char *more = (char *)SDL_realloc(*s->heap, (size_t)room);

        if (!more) {
            *cap = *s->heap_cap;
            return *s->heap;
        }
        if (!*s->heap) more[0] = '\0';
        *s->heap     = more;
        *s->heap_cap = room;
    }
    *cap = *s->heap_cap;
    return *s->heap;
}

static float
field_gutter(const char *buf, int len, int *lines)
{
    const struct nk_user_font *f = g_frame_ctx->style.font;
    int i, n = 1, digits = 1, k;

    for (i = 0; i < len; i++) n += buf[i] == '\n';
    for (k = n; k >= 10; k /= 10) digits++;
    *lines = n;
    return (float)(int)(f->width(f->userdata, f->height, "0000000000", NK_MAX(digits, 3)) +
                        16.0f);
}

static void
field_numbers(struct nk_rect b, float gutter, int lines, int active)
{
    struct nk_context *ctx = g_frame_ctx;
    const struct nk_user_font *f = ctx->style.font;
    const struct nk_style_edit *st = &ctx->style.edit;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_color fg = reaktor_token("--text-muted", nk_rgb(150, 150, 150));
    float row = f->height + st->row_padding;
    float top = b.y + st->border + st->padding.y;
    float scroll = active ? ctx->current->edit.scrollbar.y : 0.0f;
    struct nk_rect keep = cv->clip;
    char num[16];
    int i;

    nk_push_scissor(cv, nk_rect(b.x, top, gutter, b.h - 2.0f * (st->border + st->padding.y)));
    for (i = (int)(scroll / row); i < lines && top + (float)i * row - scroll < b.y + b.h; i++) {
        int n = SDL_snprintf(num, sizeof num, "%d", i + 1);
        float w = f->width(f->userdata, f->height, num, n);

        nk_draw_text(cv, nk_rect(b.x + gutter - 8.0f - w, top + (float)i * row - scroll, w, f->height),
                     num, n, f, nk_rgba(0, 0, 0, 0), fg);
    }
    nk_push_scissor(cv, keep);
}

void
reaktor_field(const reaktor_field_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;
    char       *buf;
    int         cap, lines = 0;
    float       gutter = 0.0f, pad_x;
    struct nk_rect r;
    nk_flags    state;

    if (!g_frame_app || !s || (!s->buf && !(s->heap && s->heap_cap)) || !s->len) return;
    box = s->box;
    decl_box_from_style(&box, NULL, s->style ? s->style : "input.type-text", "input",
                        0.0f, 10.0f);

    if (!decl_place(REAKTOR_A11Y_TEXTBOX, s->name ? s->name : s->hint,
                    NULL, 0u, NULL, &box, &id))
        return;
    cap = s->cap;
    buf = s->heap ? field_room(s, &cap) : s->buf;
    if (!buf) return;
    r = nk_widget_bounds(g_frame_ctx);
    if (g_frame_app->field_pick.buf == buf && !g_frame_app->field_pick.asked) {
        g_frame_app->field_pick.asked = 1;
        reaktor_focus_take(g_frame_app, id, r);
        reaktor_key_click_ask(g_frame_app);
    }
    pad_x = s->pad_x;
    if (s->multiline && s->line_numbers) {
        gutter = field_gutter(buf, *s->len, &lines);
        pad_x  = (s->pad_x > 0.0f ? s->pad_x : g_frame_ctx->style.edit.padding.x) + gutter;
    }
    reaktor_note_mute(g_frame_app, 1);
    state = reaktor_field_text_as(g_frame_app, g_frame_ctx, s->style,
                                  s->multiline ? NK_EDIT_BOX : NK_EDIT_FIELD,
                                  buf, s->len, cap, s->hint, s->filter,
                                  pad_x, s->pad_y);
    reaktor_note_mute(g_frame_app, 0);
    if (gutter > 0.0f) field_numbers(r, gutter, lines, (state & NK_EDIT_ACTIVE) != 0);
    reaktor_note_value(g_frame_app, id, buf[0] ? buf : NULL);
}

int
reaktor_link(const reaktor_link_spec *s)
{
    reaktor_box box;
    unsigned    id = 0;
    int         hit, styled;

    if (!g_frame_app || !s || !s->text) return 0;
    box = s->box;
    decl_box_from_style(&box, s->text, s->style, "a", 0.0f, 0.0f);

    if (!decl_place(REAKTOR_A11Y_LINK, s->name ? s->name : s->text, NULL,
                    s->active ? REAKTOR_A11Y_SELECTED : 0u, NULL, &box, &id))
        return 0;

    styled = decl_push_font(s->style);
    reaktor_note_mute(g_frame_app, 1);
    hit = reaktor_link_label(g_frame_app, g_frame_ctx, s->text, s->active);
    reaktor_note_mute(g_frame_app, 0);
    if (styled) nk_style_pop_font(g_frame_ctx);

    if (reaktor_focus_activated(g_frame_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    else if (hit && s->url && !reaktor_url_allowed(s->url, s->schemes))
        SDL_Log("not opening %s: its scheme is not one this link allows", s->url);
    else if (hit && s->url && !SDL_OpenURL(s->url))
        SDL_Log("could not open %s: %s", s->url, SDL_GetError());
    return hit;
}

int
reaktor_swatch(const reaktor_swatch_spec *s)
{
    reaktor_box box;
    char        hex[10];
    unsigned    id = 0;
    int         hit;

    if (!g_frame_app || !s) return 0;
    box = s->box;
    decl_box_default(&box, 40.0f, 0.0f);
    decl_box_default(&box, 0.0f, box.w);

    reaktor_hex(s->fill, hex);
    if (!decl_place(REAKTOR_A11Y_BUTTON, s->name, hex, 0u, NULL, &box, &id))
        return 0;

    hit = reaktor_css_button_color(g_frame_app, g_frame_ctx, s->fill);

    if (reaktor_focus_activated(g_frame_app, id)) hit = 1;
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

    if (!g_frame_app || !s || (!s->on && !s->flags)) return 0;
    on = s->on ? *s->on : (nk_bool)((*s->flags & s->bit) != 0);
    was = on;

    box = s->box;
    decl_box_from_style(&box, s->label, s->style, "input", 0.0f, 0.0f);

    if (!decl_place(REAKTOR_A11Y_CHECKBOX, s->name ? s->name : s->label, NULL,
                    on ? REAKTOR_A11Y_CHECKED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_frame_app, id)) on = !on;

    styled = decl_push_font(s->style ? s->style : "input");

    reaktor_hot(g_frame_app, nk_widget_bounds(g_frame_ctx), 0, 1);
    reaktor_note_mute(g_frame_app, 1);
    if (s->box_right)
        nk_checkbox_label_align(g_frame_ctx, s->label, &on,
                                NK_WIDGET_RIGHT, NK_TEXT_LEFT);
    else
        nk_checkbox_label(g_frame_ctx, s->label, &on);
    reaktor_note_mute(g_frame_app, 0);
    if (styled) nk_style_pop_font(g_frame_ctx);

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

    if (!g_frame_app || !s || !s->choice) return 0;
    on = (*s->choice == s->value);

    box = s->box;
    decl_box_from_style(&box, s->label, s->style, "input", 0.0f, 0.0f);

    if (!decl_place(REAKTOR_A11Y_RADIO, s->name ? s->name : s->label, NULL,
                    on ? REAKTOR_A11Y_CHECKED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_frame_app, id)) { *s->choice = s->value; on = 1; }

    styled = decl_push_font(s->style ? s->style : "input");

    reaktor_note_mute(g_frame_app, 1);
    hit = reaktor_radio_label(g_frame_app, g_frame_ctx, s->label, on);
    reaktor_note_mute(g_frame_app, 0);
    if (styled) nk_style_pop_font(g_frame_ctx);

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

    if (!g_frame_app || !s || !s->on) return 0;
    was = *s->on;

    box = s->box;
    decl_box_from_style(&box, s->label, s->style, "select", 0.0f, 0.0f);

    if (!decl_place(REAKTOR_A11Y_LISTITEM, s->name ? s->name : s->label, NULL,
                    was ? REAKTOR_A11Y_SELECTED : 0u, NULL, &box, &id))
        return 0;

    if (reaktor_focus_activated(g_frame_app, id)) *s->on = !*s->on;

    styled = decl_push_font(s->style ? s->style : "select");

    {
        nk_flags align = s->centered ? NK_TEXT_CENTERED : NK_TEXT_LEFT;
        struct nk_rect b = nk_widget_bounds(g_frame_ctx);
        reaktor_style rs;

        reaktor_hot(g_frame_app, b, 0, 1);
        reaktor_note_mute(g_frame_app, 1);
        reaktor_style_get(decl_rule_or(s->style, "select"), &rs);
        reaktor_select_arm(b, s->on, rs.matched ? rs.rounding
                                                : g_frame_ctx->style.selectable.rounding);
        if (s->icon) {
            struct nk_color accent = g_frame_app->accent;
            struct nk_color ink = *s->on
                ? reaktor_on(accent)
                : reaktor_token("--text-muted", g_frame_ctx->style.text.color);

            hit = nk_selectable_image_label(g_frame_ctx,
                      reaktor_ionicon_col(g_frame_app, s->icon, 16, ink),
                      s->label, align, s->on);
        } else if (s->disc) {
            const struct nk_style_selectable *st = &g_frame_ctx->style.selectable;
            struct nk_rect icon;

            hit = nk_selectable_symbol_label(g_frame_ctx, NK_SYMBOL_NONE, s->label,
                                             align, s->on);
            icon.y = b.y + st->padding.y + st->image_padding.y;
            icon.x = b.x + 2.0f * st->padding.x + st->image_padding.x;
            icon.w = icon.h = b.h - 2.0f * st->padding.y;
            icon.w -= 2.0f * st->image_padding.x;
            icon.h -= 2.0f * st->image_padding.y;
            reaktor_glyph_at(g_frame_app, g_frame_ctx, icon, REAKTOR_DISC_ROUND,
                             *s->on ? st->text_pressed : st->text_normal,
                             (int)(icon.w < icon.h ? icon.w : icon.h), 0.0f);
        } else {
            hit = nk_selectable_label(g_frame_ctx, s->label, align, s->on);
        }
        reaktor_chrome_disarm();
        reaktor_note_mute(g_frame_app, 0);
    }
    if (styled) nk_style_pop_font(g_frame_ctx);
    return hit;
}

void
reaktor_slider(const reaktor_slider_spec *s)
{
    reaktor_box box;
    char        text[32];
    unsigned    id = 0;
    float       now;

    if (!g_frame_app || !s || (!s->value && !s->ivalue)) return;
    now = s->value ? *s->value : (float)*s->ivalue;

    if (s->text) SDL_strlcpy(text, s->text, sizeof(text));
    else if (s->value) SDL_snprintf(text, sizeof(text), "%.2f", (double)now);
    else SDL_snprintf(text, sizeof(text), "%d", *s->ivalue);

    box = s->box;
    decl_box_from_style(&box, NULL,
                        s->style ? s->style : "input.type-range",
                        "input", 0.0f, 7.0f);

    if (!decl_place(REAKTOR_A11Y_SLIDER, s->name, text, 0u, NULL, &box, &id))
        return;

    reaktor_note_mute(g_frame_app, 1);
    if (s->value)
        reaktor_slider_bar(g_frame_app, g_frame_ctx, id, s->value, s->lo, s->hi, s->step);
    else
        reaktor_slider_bar_int(g_frame_app, g_frame_ctx, id, s->ivalue, (int)s->lo,
                               (int)s->hi, (int)s->step);
    reaktor_note_mute(g_frame_app, 0);
}

void
reaktor_progress(const reaktor_progress_spec *s)
{
    reaktor_box box;
    char        text[32];
    unsigned    id = 0;

    if (!g_frame_app || !s || !s->value) return;
    SDL_snprintf(text, sizeof(text), "%d", (int)*s->value);

    box = s->box;
    decl_box_from_style(&box, NULL, s->style, "progress", 0.0f, 7.0f);

    if (!decl_place(REAKTOR_A11Y_PROGRESS, s->name, text, 0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_frame_app, id, (float)*s->value, 0.0f, (float)s->max, 1.0f);

    reaktor_note_mute(g_frame_app, 1);
    reaktor_progress_bar(g_frame_app, g_frame_ctx, s->value, s->max,
                         s->modifiable ? NK_MODIFIABLE : NK_FIXED);
    reaktor_note_mute(g_frame_app, 0);
}

void
reaktor_knob(const reaktor_knob_spec *s)
{
    reaktor_box box;
    char        text[32];
    unsigned    id = 0;

    if (!g_frame_app || !s || !s->value) return;
    SDL_snprintf(text, sizeof(text), "%.2f", (double)*s->value);

    box = s->box;
    decl_box_default(&box, 62.0f, 0.0f);
    decl_box_default(&box, 0.0f, box.w);

    if (!decl_place(REAKTOR_A11Y_SLIDER, s->name, text, 0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_frame_app, id, *s->value, s->lo, s->hi, 0.0f);

    reaktor_note_mute(g_frame_app, 1);
    reaktor_knob_dial(g_frame_app, g_frame_ctx, s->value, s->lo, s->hi, NK_DOWN);
    reaktor_note_mute(g_frame_app, 0);
}

void
reaktor_property(const reaktor_property_spec *s)
{
    reaktor_box    box;
    struct nk_rect r;
    char           text[32];
    unsigned       id = 0;
    double         now;

    if (!g_frame_app || !s || (!s->ivalue && !s->fvalue && !s->dvalue)) return;
    now = s->ivalue ? (double)*s->ivalue
        : s->fvalue ? (double)*s->fvalue : *s->dvalue;

    if (s->ivalue) SDL_snprintf(text, sizeof(text), "%d", *s->ivalue);
    else SDL_snprintf(text, sizeof(text), "%.2f", now);

    box = s->box;
    decl_box_from_style(&box, NULL, s->style ? s->style : "input.type-text", "input",
                        0.0f, 7.0f);

    if (!decl_place(REAKTOR_A11Y_SPINBUTTON, s->name ? s->name : s->label, text,
                    0u, NULL, &box, &id))
        return;
    reaktor_note_range(g_frame_app, id, (float)now, (float)s->lo, (float)s->hi,
                       (float)s->step);

    r = nk_widget_bounds(g_frame_ctx);

    reaktor_note_mute(g_frame_app, 1);
    reaktor_property_push(g_frame_ctx);
    if (s->ivalue)
        nk_property_int(g_frame_ctx, s->label, (int)s->lo, s->ivalue, (int)s->hi,
                        (int)s->step, s->grain);
    else if (s->fvalue)
        nk_property_float(g_frame_ctx, s->label, (float)s->lo, s->fvalue,
                          (float)s->hi, (float)s->step, s->grain);
    else
        nk_property_double(g_frame_ctx, s->label, s->lo, s->dvalue, s->hi,
                           s->step, s->grain);
    reaktor_property_pop(g_frame_ctx);
    reaktor_property_chrome(g_frame_app, g_frame_ctx, r, NULL);
    reaktor_note_mute(g_frame_app, 0);
}

int
reaktor_spinner(const reaktor_spinner_spec *s)
{
    struct nk_context *ctx = g_frame_ctx;
    const struct nk_style_property *st;
    const struct nk_user_font *f;
    struct nk_command_buffer *cv;
    struct nk_rect r, step[2];
    struct nk_style_item bg;
    const char *value;
    reaktor_box box;
    unsigned id = 0;
    float y, w;
    int d = 0, at;

    if (!g_frame_app || !s || !s->items || s->count <= 0 || !s->chosen) return 0;
    at    = *s->chosen < 0 ? 0 : *s->chosen >= s->count ? s->count - 1 : *s->chosen;
    value = s->items[at] ? s->items[at] : "";
    box   = s->box;
    decl_box_from_style(&box, NULL, s->style ? s->style : "input.type-text", "input",
                        0.0f, 7.0f);
    if (!decl_place(REAKTOR_A11Y_SPINBUTTON, s->name ? s->name : s->label, value, 0u, NULL,
                    &box, &id))
        return 0;
    st = &ctx->style.property;
    f  = ctx->style.font;
    cv = nk_window_get_canvas(ctx);
    nk_widget(&r, ctx);
    bg = nk_input_is_mouse_hovering_rect(&ctx->input, r) ? st->hover : st->normal;
    if (bg.type == NK_STYLE_ITEM_COLOR)
        reaktor_fill_round(g_frame_app, cv, r, st->rounding, bg.data.color);
    if (st->border > 0.0f)
        reaktor_edge_round(g_frame_app, cv, r, st->rounding, st->border, st->border_color);
    reaktor_property_chrome(g_frame_app, ctx, r, step);
    y = (float)(int)(r.y + (r.h - f->height) * 0.5f);
    if (s->label)
        nk_draw_text(cv, nk_rect(step[0].x + step[0].w + 8.0f, y, r.w * 0.5f, f->height),
                     s->label, (int)strlen(s->label), f, nk_rgba(0, 0, 0, 0), st->label_normal);
    w = reaktor_text_w(f, value);
    nk_draw_text(cv, nk_rect(step[1].x - 8.0f - w, y, w, f->height), value,
                 (int)strlen(value), f, nk_rgba(0, 0, 0, 0), st->label_normal);
    if (nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, step[0])) d = -1;
    if (nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, step[1])) d = 1;
    if (!d) {
        int k = reaktor_focus_step(g_frame_app, id);

        d = k > 0 ? 1 : k < 0 ? -1 : 0;
    }
    if (!d) return 0;
    at += d;
    if (s->wrap) at = (at + s->count) % s->count;
    else if (at < 0 || at >= s->count) return 0;
    *s->chosen = at;
    return 1;
}

int
reaktor_combo_open(const reaktor_combo_spec *s)
{
    reaktor_box    box;
    struct nk_rect h, clip;
    float          cw;
    int            open;

    if (!g_frame_app || !s) return 0;
    box = s->box;
    decl_box_from_style(&box, NULL, s->style, "select", 0.0f, 9.0f);
    if (s->box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y)) {
        reaktor_box field;

        memset(&field, 0, sizeof(field));
        decl_box_from_style(&field, NULL, "input.type-text", "input", 0.0f, 10.0f);
        if (field.h > box.h) box.h = field.h;
    }

    if (!decl_place(REAKTOR_A11Y_COMBOBOX, s->name ? s->name : s->label, s->label,
                    0u, NULL, &box, NULL))
        return 0;

    h    = nk_widget_bounds(g_frame_ctx);
    cw   = nk_widget_width(g_frame_ctx);
    clip = nk_window_get_canvas(g_frame_ctx)->clip;

    reaktor_hot(g_frame_app, h, 0, 1);
    reaktor_note_mute(g_frame_app, 1);
    if (s->disc)
        open = nk_combo_begin_symbol_label(g_frame_ctx, s->label, NK_SYMBOL_NONE,
                                           nk_vec2(cw, s->body_h));
    else
        open = nk_combo_begin_label(g_frame_ctx, s->swatch ? "" : s->label,
                                    nk_vec2(cw, s->body_h));
    reaktor_note_mute(g_frame_app, 0);

    if (open) nk_push_scissor(nk_window_get_canvas(g_frame_ctx), clip);
    if (s->disc) {
        struct nk_rect im;

        im.w = im.h = h.h - 2.0f * g_frame_ctx->style.combo.content_padding.y;
        im.x = h.x + g_frame_ctx->style.combo.content_padding.x;
        im.y = h.y + g_frame_ctx->style.combo.content_padding.y;
        reaktor_glyph_at(g_frame_app, g_frame_ctx, im, REAKTOR_DISC_ROUND,
                         g_frame_ctx->style.combo.symbol_normal, (int)im.w, 0.0f);
    } else if (s->swatch) {
        struct nk_rect sw = reaktor_combo_content(g_frame_ctx, reaktor_rect_trunc(h));
        float r = g_frame_ctx->style.combo.rounding;

        reaktor_fill_round(g_frame_app, nk_window_get_canvas(g_frame_ctx), sw,
                           r > sw.h * 0.5f ? sw.h * 0.5f : r, *s->swatch);
    }
    {
        reaktor_style sel;

        reaktor_style_get(decl_rule_or(s->style, "select"), &sel);
        reaktor_combo_chrome(g_frame_app, g_frame_ctx, h, sel.matched ? sel.border : 2.0f);
    }
    if (open) nk_push_scissor(nk_window_get_canvas(g_frame_ctx), g_frame_ctx->current->layout->clip);
    return open;
}

void
reaktor_combo_close(void)
{
    if (g_frame_ctx) nk_combo_end(g_frame_ctx);
}

int
reaktor_combo_item(const char *label, int chosen)
{
    struct nk_rect b;
    int            hit;

    if (!g_frame_app || !label) return 0;
    b = nk_widget_bounds(g_frame_ctx);
    reaktor_hot_top(g_frame_app, b, 1, 1);
    reaktor_note(g_frame_app, REAKTOR_A11Y_LISTITEM, label, NULL,
                 chosen ? REAKTOR_A11Y_SELECTED : 0u, b);
    hit = nk_combo_item_label(g_frame_ctx, label, NK_TEXT_LEFT);
    return hit;
}

void
reaktor_color_pick(const reaktor_color_spec *s)
{
    reaktor_box     box;
    struct nk_color c;
    char            hex[10];

    if (!g_frame_app || !s || !s->value) return;
    c = nk_rgb_cf(*s->value);
    reaktor_hex(c, hex);

    box = s->box;
    decl_box_default(&box, 210.0f, 132.0f);

    if (!decl_place(REAKTOR_A11Y_GROUP, s->name, hex, 0u, NULL, &box, NULL))
        return;

    reaktor_note_mute(g_frame_app, 1);
    nk_color_pick(g_frame_ctx, s->value, NK_RGB);
    reaktor_note_mute(g_frame_app, 0);
}
