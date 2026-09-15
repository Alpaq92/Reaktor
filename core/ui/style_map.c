#include "internal.h"

struct nk_color
col_of(const unsigned char c[4])
{
    return nk_rgba(c[0], c[1], c[2], c[3]);
}

void
hot_push_ex(App *app, struct nk_rect r, int cursor, int repaint, int top,
            int track)
{
    int n = app->hot_n;

    if (app->ctx && app->ctx->current && app->ctx->current->layout) {
        struct nk_rect c = app->ctx->current->layout->clip;
        float x0 = r.x > c.x ? r.x : c.x;
        float y0 = r.y > c.y ? r.y : c.y;
        float x1 = (r.x + r.w) < (c.x + c.w) ? (r.x + r.w) : (c.x + c.w);
        float y1 = (r.y + r.h) < (c.y + c.h) ? (r.y + r.h) : (c.y + c.h);
        if (x1 <= x0 || y1 <= y0) return;
        r = nk_rect(x0, y0, x1 - x0, y1 - y0);
    }

    if (n >= (int)(sizeof(app->hot) / sizeof(app->hot[0]))) return;
    app->hot[n].r       = r;
    app->hot[n].cursor  = (unsigned char)cursor;
    app->hot[n].repaint = (unsigned char)repaint;
    app->hot[n].top     = (unsigned char)top;
    app->hot[n].track   = (unsigned char)track;
    app->hot_n = n + 1;
}

void
hot_push(App *app, struct nk_rect r, int cursor, int repaint)
{
    hot_push_ex(app, r, cursor, repaint, 0, 0);
}

static style_frame
push_button_style(App *app, struct nk_context *ctx, const char *selector)
{
    reaktor_style s, hov, act;
    unsigned char hover[4], active[4];
    style_frame f = { 0, 0, 0, 0, 0 };
    char state[80];

    reaktor_style_get(selector, &s);
    if (!s.matched) return f;

    snprintf(state, sizeof(state), "%s:hover", selector);
    reaktor_style_get(state, &hov);
    snprintf(state, sizeof(state), "%s:active", selector);
    reaktor_style_get(state, &act);

    memcpy(hover, hov.matched && hov.bg[3] ? hov.bg : s.bg, 4);
    if (act.matched && act.bg[3]) {
        memcpy(active, act.bg, 4);
    } else {
        memcpy(active, hover, 4);
        reaktor_style_darken(active, 0.10f);
    }

    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(col_of(hover)));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(col_of(active)));
    f.items = 3;

    nk_style_push_color(ctx, &ctx->style.button.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.text_hover,
                        col_of(hov.matched && hov.fg[3] ? hov.fg : s.fg));
    nk_style_push_color(ctx, &ctx->style.button.text_active,
                        col_of(act.matched && act.fg[3] ? act.fg : s.fg));
    nk_style_push_color(ctx, &ctx->style.button.border_color,
                        col_of(s.border_col));
    f.colors = 4;

    nk_style_push_float(ctx, &ctx->style.button.rounding, s.rounding);
    nk_style_push_float(ctx, &ctx->style.button.border, s.border);
    f.floats = 2;

    {
        float room = nk_widget_bounds(ctx).h * 0.5f - (s.border + s.rounding);

        if (room < 0.0f) room = 0.0f;
        if (s.pad_y > room) s.pad_y = room;
    }

    nk_style_push_vec2(ctx, &ctx->style.button.padding,
                       nk_vec2(s.pad_x, s.pad_y));
    f.vec2s = 1;

    if (s.font_px > 0) {
        nk_style_push_font(ctx, pick_font(app, s.font_px, s.bold));
        f.fonts = 1;
    }
    return f;
}

void
pop_style(struct nk_context *ctx, style_frame f)
{
    int i;
    for (i = 0; i < f.fonts;  i++) nk_style_pop_font(ctx);
    for (i = 0; i < f.vec2s;  i++) nk_style_pop_vec2(ctx);
    for (i = 0; i < f.floats; i++) nk_style_pop_float(ctx);
    for (i = 0; i < f.colors; i++) nk_style_pop_color(ctx);
    for (i = 0; i < f.items;  i++) nk_style_pop_style_item(ctx);
}

int
css_button(App *app, struct nk_context *ctx, const char *selector,
           const char *label)
{
    style_frame f;
    unsigned id;
    int clicked;

    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    id = reaktor_note_here(app, ctx, REAKTOR_A11Y_BUTTON, label, 0);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_label(ctx, label);
    pop_style(ctx, f);
    if (reaktor_focus_activated(app, id)) clicked = 1;
    return clicked;
}

static float
luminance(const unsigned char c[4])
{
    float ch[3];
    int i;

    for (i = 0; i < 3; i++) {
        float v = c[i] / 255.0f;
        ch[i] = v <= 0.04045f ? v / 12.92f
                              : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    return 0.2126f * ch[0] + 0.7152f * ch[1] + 0.0722f * ch[2];
}

static float
contrast_ratio(const unsigned char a[4], const unsigned char b[4])
{
    float la = luminance(a), lb = luminance(b);
    float hi = la > lb ? la : lb, lo = la > lb ? lb : la;

    return (hi + 0.05f) / (lo + 0.05f);
}

#define READABLE_MIN 3.0f

struct nk_color
readable_on(const unsigned char bg[4], const char *preferred,
            const char *fallback)
{
    unsigned char a[4], b[4];

    if (reaktor_style_token(preferred, a)) {
        if (contrast_ratio(bg, a) >= READABLE_MIN) return col_of(a);
        if (reaktor_style_token(fallback, b) &&
            contrast_ratio(bg, b) > contrast_ratio(bg, a))
            return col_of(b);
        return col_of(a);
    }
    if (reaktor_style_token(fallback, b)) return col_of(b);
    return nk_rgb(255, 255, 255);
}

struct nk_color
reaktor_visible(struct nk_color want, struct nk_color behind,
                struct nk_color fallback)
{
    unsigned char a[4], b[4];

    a[0] = want.r;   a[1] = want.g;   a[2] = want.b;   a[3] = want.a;
    b[0] = behind.r; b[1] = behind.g; b[2] = behind.b; b[3] = behind.a;
    return contrast_ratio(a, b) < 1.12f ? fallback : want;
}

int
css_button_accent(App *app, struct nk_context *ctx, const char *selector,
                  const char *label, const char *token)
{
    style_frame f, a = { 0, 0, 0, 0, 0 };
    unsigned char c[4];
    int clicked;

    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    reaktor_note_here(app, ctx, REAKTOR_A11Y_BUTTON, label, 0);

    f = push_button_style(app, ctx, selector);

    if (reaktor_style_token(token, c)) {
        unsigned char hov[4];
        memcpy(hov, c, 4);
        reaktor_style_darken(hov, 0.12f);

        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(col_of(c)));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(col_of(hov)));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(col_of(hov)));
        a.items = 3;
        nk_style_push_color(ctx, &ctx->style.button.border_color, col_of(c));

        {
            struct nk_color label_col =
                readable_on(c, "--text-bright", "--background-body");
            nk_style_push_color(ctx, &ctx->style.button.text_normal, label_col);
            nk_style_push_color(ctx, &ctx->style.button.text_hover, label_col);
            nk_style_push_color(ctx, &ctx->style.button.text_active, label_col);
        }
        a.colors = 4;
    }

    clicked = nk_button_label(ctx, label);
    pop_style(ctx, a);
    pop_style(ctx, f);
    return clicked;
}

int
css_button_icon(App *app, struct nk_context *ctx, const char *selector,
                const char *icon_src, const char *label)
{
    style_frame f;
    struct nk_image im = icon(app, icon_src, 18);
    int clicked;

    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    reaktor_note_here(app, ctx, REAKTOR_A11Y_BUTTON, label, 0);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_image_label(ctx, im, label, NK_TEXT_CENTERED);
    pop_style(ctx, f);
    return clicked;
}

static void
edit_copy_selection(struct nk_context *ctx, struct nk_text_edit *edit)
{
    int b = edit->select_start, e = edit->select_end;
    int begin = NK_MIN(b, e), end = NK_MAX(b, e);
    int glyph_len;
    nk_rune unicode;
    const char *text;

    if (begin == end || !ctx->clip.copy) return;
    text = nk_str_at_const(&edit->string, begin, &unicode, &glyph_len);
    if (text) ctx->clip.copy(ctx->clip.userdata, text, end - begin);
}

static void
edit_paste_clipboard(struct nk_text_edit *edit)
{
    char *text = SDL_GetClipboardText();

    if (!text) return;
    if (*text) {
        nk_textedit_paste(edit, text, (int)SDL_utf8strlen(text));
    }
    SDL_free(text);
}

style_frame
push_edit_style(struct nk_context *ctx, reaktor_style *out, int inset)
{
    reaktor_style s, foc, btn;
    style_frame f = { 0, 0, 0, 0, 0 };
    unsigned char body[4];

    reaktor_style_get("input", &s);
    reaktor_style_get("input:focus", &foc);
    reaktor_style_get("button", &btn);
    if (btn.matched) s.rounding = btn.rounding;
    s.pad_x += s.border;
    if (btn.matched && btn.pad_x > s.pad_x) s.pad_x = btn.pad_x;
    if (s.pad_x < 16.0f) s.pad_x = 16.0f;
    if (inset && reaktor_style_token("--background-body", body)) {
        int k;
        for (k = 0; k < 3; k++)
            s.bg[k] = (unsigned char)((s.bg[k] * 3 + body[k] * 2) / 5);
    }
    *out = s;
    if (!s.matched) return f;

    nk_style_push_style_item(ctx, &ctx->style.edit.normal,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.edit.hover,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.edit.active,
                             nk_style_item_color(col_of(s.bg)));
    f.items = 3;

    {
        const struct nk_window *win = ctx->current;
        int focused = win && win->edit.active &&
                      win->edit.seq == win->edit.name;
        struct nk_color line;

        if (focused && foc.matched) {
            line = col_of(foc.border_col);
        } else {
            unsigned char c[4];
            line = col_of(s.border_col);
            if (line.a == 0)
                line = reaktor_style_token("--background-hover", c)
                     ? col_of(c) : nk_rgba(128, 128, 128, 90);
        }
        nk_style_push_color(ctx, &ctx->style.edit.border_color, line);
        out->border_col[0] = line.r; out->border_col[1] = line.g;
        out->border_col[2] = line.b; out->border_col[3] = line.a;
    }
    nk_style_push_color(ctx, &ctx->style.edit.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_hover, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_active, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.cursor_normal, col_of(s.fg));

    {
        unsigned char sel[4];
        struct nk_color selbg, seltx;

        if (reaktor_style_token("--focus", sel)) {
            selbg = col_of(sel);
            seltx = readable_on(sel, "--text-bright", "--background-body");
        } else {
            selbg = nk_rgb(0x7a, 0xa3, 0xfc);
            seltx = nk_rgb(0x20, 0x20, 0x20);
        }
        nk_style_push_color(ctx, &ctx->style.edit.selected_normal, selbg);
        nk_style_push_color(ctx, &ctx->style.edit.selected_hover, selbg);
        nk_style_push_color(ctx, &ctx->style.edit.selected_text_normal, seltx);
        nk_style_push_color(ctx, &ctx->style.edit.selected_text_hover, seltx);
    }
    f.colors = 9;

    nk_style_push_float(ctx, &ctx->style.edit.rounding, s.rounding);
    nk_style_push_float(ctx, &ctx->style.edit.border, 0.0f);
    f.floats = 2;

    nk_style_push_vec2(ctx, &ctx->style.edit.padding,
                       nk_vec2(s.pad_x, s.pad_y));
    f.vec2s = 1;
    return f;
}

void
stroke_edit_edge(struct nk_context *ctx, struct nk_rect b, const reaktor_style *s)
{
    float t = s->border, r;

    if (!s->matched || t <= 0.0f) return;
    r = s->rounding - t * 0.5f;
    nk_stroke_rect(nk_window_get_canvas(ctx),
                   nk_rect(b.x + t * 0.5f, b.y + t * 0.5f, b.w - t, b.h - t),
                   r > 0.0f ? r : 0.0f, t, col_of(s->border_col));
}

void
draw_hint(struct nk_context *ctx, struct nk_rect bounds, const char *hint,
          const reaktor_style *s)
{
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    const struct nk_user_font *font = ctx->style.font;
    unsigned char muted[4];
    struct nk_color gray = reaktor_style_token("--text-muted", muted)
                         ? col_of(muted) : nk_rgb(0x9a, 0x9a, 0x9a);
    float pad = s->matched ? s->pad_x : 9.0f;
    struct nk_rect r = nk_rect(bounds.x + pad,
                               bounds.y + (bounds.h - font->height) * 0.5f,
                               bounds.w - pad * 2.0f, font->height + 2.0f);

    nk_draw_text(canvas, r, hint, (int)strlen(hint), font,
                 nk_rgba(0, 0, 0, 0), gray);
}

void
note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
        app->field_rect = bounds;
        app->field_rect_valid = 1;
    }
}

void
note_ime_caret(App *app, struct nk_context *ctx, struct nk_rect bounds,
               nk_flags state, const struct nk_text_edit *edit)
{
    const struct nk_user_font *font = ctx->style.font;
    float caret = 0.0f;

    if (!(state & NK_EDIT_ACTIVE)) return;

    if (font && font->width) {
        const char *txt = nk_str_get_const(&edit->string);
        nk_rune unicode;
        int glyph_len;
        char *at = nk_str_at_rune((struct nk_str *)&edit->string,
                                  edit->cursor, &unicode, &glyph_len);
        int bytes = at ? (int)(at - txt) : nk_str_len_char(&edit->string);
        if (bytes > 0)
            caret = font->width(font->userdata, font->height, txt, bytes);
    }

    app->ime_rect.x = (int)bounds.x;
    app->ime_rect.y = (int)bounds.y;
    app->ime_rect.w = (int)bounds.w;
    app->ime_rect.h = (int)bounds.h;
    app->ime_cursor = (int)caret;
    app->ime_valid  = 1;
}

void
css_field(App *app, struct nk_context *ctx, char *buf, int *len, int cap,
          const char *hint)
{
    struct nk_rect bounds = nk_widget_bounds(ctx);
    reaktor_style s;
    style_frame f;

    (void)buf; (void)cap;
    note_field_rect(app, ctx, bounds);

    hot_push(app, bounds, 2, 0);

    f = push_edit_style(ctx, &s, 1);
    {
        nk_flags st = nk_edit_buffer(ctx, NK_EDIT_FIELD, &app->edit,
                                     nk_filter_default);
        note_ime_caret(app, ctx, bounds, st, &app->edit);
        reaktor_note(app, REAKTOR_A11Y_TEXTBOX, hint,
                     nk_str_get_const(&app->edit.string),
                     st & NK_EDIT_ACTIVE ? REAKTOR_A11Y_FOCUSED : 0u, bounds);
    }
    pop_style(ctx, f);
    stroke_edit_edge(ctx, bounds, &s);

    nk_style_push_float(ctx, &ctx->style.window.rounding,
                        reaktor_popup_rounding());
    if (nk_contextual_begin(ctx, NK_WINDOW_BORDER, nk_vec2(160, 172), bounds)) {
        int has_sel = app->edit.select_start != app->edit.select_end;

        nk_layout_row_dynamic(ctx, 26, 1);
        if (has_sel && nk_contextual_item_label(ctx, "Cut", NK_TEXT_LEFT)) {
            edit_copy_selection(ctx, &app->edit);
            nk_textedit_cut(&app->edit);
        }
        if (has_sel && nk_contextual_item_label(ctx, "Copy", NK_TEXT_LEFT))
            edit_copy_selection(ctx, &app->edit);
        if (nk_contextual_item_label(ctx, "Paste", NK_TEXT_LEFT))
            edit_paste_clipboard(&app->edit);
        if (nk_contextual_item_label(ctx, "Select all", NK_TEXT_LEFT))
            nk_textedit_select_all(&app->edit);
        nk_contextual_end(ctx);
    }
    nk_style_pop_float(ctx);

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
}

static void
style_flat_button(struct nk_style_button *b, struct nk_color hover,
                  struct nk_color text, float pad)
{
    b->normal      = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    b->hover       = nk_style_item_color(hover);
    b->active      = nk_style_item_color(hover);
    b->text_normal = b->text_hover = b->text_active = text;
    b->text_background = hover;
    b->border      = 0.0f;
    b->rounding    = 2.0f;
    b->padding     = nk_vec2(pad, pad);
}

static void
style_toggle(struct nk_style_toggle *t, struct nk_style_item bg,
             struct nk_style_item hov, struct nk_style_item cursor,
             struct nk_color border, struct nk_color text,
             struct nk_color back)
{
    t->normal        = bg;
    t->hover         = hov;
    t->active        = hov;
    t->cursor_normal = cursor;
    t->cursor_hover  = cursor;
    t->border_color  = border;
    t->border        = 1.0f;
    t->padding       = nk_vec2(2.0f, 2.0f);
    t->text_normal   = t->text_hover = t->text_active = text;
    t->text_background = back;
}

void
apply_widget_style(App *app)
{
    struct nk_context *ctx;
    struct nk_style *st;
    struct nk_color body, base, hover, text, muted, bright, accent, focus, edge;
    struct nk_color on_accent, thumb, thumb_hi;
    struct nk_style_item i_none, i_base, i_hover, i_accent;
    reaktor_style btn, btn_hov, inp, sel, det, sum, dlg;
    reaktor_style rng, acc;
    unsigned char c[4], ac[4];

    if (!app->ctx) return;
    ctx = app->ctx;
    st  = &ctx->style;

    body   = reaktor_token("--background-body", app->page);
    base   = reaktor_token("--background", app->card_bg);
    hover  = reaktor_token("--background-hover", base);
    text   = reaktor_token("--text-main", app->text);
    muted  = reaktor_token("--text-muted", text);
    bright = reaktor_token("--text-bright", text);
    accent = reaktor_token("--links", text);
    focus  = reaktor_token("--focus", accent);

    edge = hover;

    on_accent = reaktor_style_token("--links", ac)
              ? readable_on(ac, "--text-bright", "--background-body") : bright;

    thumb    = nk_rgba(muted.r, muted.g, muted.b, 110);
    thumb_hi = nk_rgba(muted.r, muted.g, muted.b, 175);

    i_none   = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    i_base   = nk_style_item_color(base);
    i_hover  = nk_style_item_color(hover);
    i_accent = nk_style_item_color(accent);

    st->text.color = text;

    reaktor_style_get("button", &btn);
    reaktor_style_get("button:hover", &btn_hov);
    if (btn.matched) {
        unsigned char act[4];

        memcpy(act, btn_hov.matched && btn_hov.bg[3] ? btn_hov.bg : btn.bg, 4);
        reaktor_style_darken(act, 0.10f);

        st->button.normal = nk_style_item_color(col_of(btn.bg));
        st->button.hover  = nk_style_item_color(
            btn_hov.matched && btn_hov.bg[3] ? col_of(btn_hov.bg)
                                             : col_of(btn.bg));
        st->button.active = nk_style_item_color(col_of(act));
        st->button.text_normal = st->button.text_hover =
            st->button.text_active = col_of(btn.fg);
        st->button.border_color = col_of(btn.border_col);
        st->button.border       = btn.border;
        st->button.rounding     = btn.rounding;
        st->button.padding      = nk_vec2(btn.pad_x, btn.pad_y);
    }

    reaktor_style_get("input", &inp);
    if (inp.matched) {
        struct nk_color sel_text = reaktor_style_token("--focus", c)
                                 ? readable_on(c, "--text-bright",
                                               "--background-body") : bright;

        st->edit.normal = st->edit.hover = st->edit.active =
            nk_style_item_color(col_of(inp.bg));
        st->edit.border_color = col_of(inp.border_col);
        st->edit.text_normal  = st->edit.text_hover = st->edit.text_active =
            col_of(inp.fg);
        st->edit.cursor_normal = col_of(inp.fg);
        st->edit.selected_normal = st->edit.selected_hover = focus;
        st->edit.selected_text_normal = st->edit.selected_text_hover =
            sel_text;
        st->edit.border   = inp.border;
        st->edit.rounding = inp.rounding;
        st->edit.padding  = nk_vec2(inp.pad_x, inp.pad_y);
    }

    {
        struct nk_color box = nk_rgba(muted.r, muted.g, muted.b, 150);

        style_toggle(&st->checkbox, i_base, i_hover, i_accent, box, text, body);
        style_toggle(&st->option,   i_base, i_hover, i_accent, box, text, body);
    }

    st->selectable.normal         = i_none;
    st->selectable.hover          = i_hover;
    st->selectable.pressed        = i_hover;
    st->selectable.normal_active  = i_accent;
    st->selectable.hover_active   = i_accent;
    st->selectable.pressed_active = i_accent;
    st->selectable.text_normal    = st->selectable.text_hover =
        st->selectable.text_pressed = text;
    st->selectable.text_normal_active = st->selectable.text_hover_active =
        st->selectable.text_pressed_active = on_accent;
    st->selectable.text_background = body;
    st->selectable.rounding = 3.0f;
    st->selectable.image_padding = nk_vec2(7.0f, 7.0f);

    reaktor_style_get("input", &rng);
    reaktor_style_get("a", &acc);

    st->slider.normal        = i_none;
    st->slider.hover         = i_none;
    st->slider.active        = i_none;
    st->slider.bar_normal    = rng.matched ? col_of(rng.bg) : edge;
    st->slider.bar_hover     = st->slider.bar_normal;
    st->slider.bar_active    = st->slider.bar_normal;
    st->slider.bar_filled    = acc.matched ? col_of(acc.fg) : accent;
    st->slider.cursor_normal = acc.matched
                             ? nk_style_item_color(col_of(acc.fg)) : i_accent;
    st->slider.cursor_hover  = nk_style_item_color(focus);
    st->slider.cursor_active = nk_style_item_color(focus);
    st->slider.border_color  = rng.matched ? col_of(rng.border_col) : edge;
    st->slider.bar_height    = 6.0f;
    st->slider.rounding      = rng.matched && rng.rounding > 0.0f
                             && rng.rounding < st->slider.bar_height * 0.5f
                             ? rng.rounding
                             : st->slider.bar_height * 0.5f;
    st->slider.show_buttons  = nk_false;

    st->knob.normal            = i_none;
    st->knob.hover             = i_none;
    st->knob.active            = i_none;
    st->knob.knob_normal       = btn.matched ? col_of(btn.bg) : base;
    st->knob.knob_hover        = hover;
    st->knob.knob_active       = hover;
    st->knob.knob_border_color = btn.matched ? col_of(btn.border_col) : edge;
    st->knob.cursor_normal     = acc.matched ? col_of(acc.fg) : accent;
    st->knob.cursor_hover      = focus;
    st->knob.cursor_active     = focus;
    st->knob.border_color      = btn.matched ? col_of(btn.border_col) : edge;

    st->progress.normal              = rng.matched
                                     ? nk_style_item_color(col_of(rng.bg))
                                     : i_base;
    st->progress.hover               = st->progress.normal;
    st->progress.active              = st->progress.normal;
    st->progress.cursor_normal       = acc.matched
                                     ? nk_style_item_color(col_of(acc.fg))
                                     : i_accent;
    st->progress.cursor_hover        = nk_style_item_color(focus);
    st->progress.cursor_active       = nk_style_item_color(focus);
    st->progress.border_color        = rng.matched ? col_of(rng.border_col)
                                                   : edge;
    st->progress.cursor_border_color = acc.matched ? col_of(acc.fg) : accent;
    st->progress.border              = 0.0f;
    st->progress.cursor_border       = 0.0f;
    {
        reaktor_style bs;

        reaktor_style_get("button", &bs);
        st->progress.rounding        = bs.matched ? bs.rounding : 3.0f;
        st->progress.cursor_rounding = st->progress.rounding;
    }

    st->property.normal       = i_base;
    st->property.hover        = i_hover;
    st->property.active       = i_hover;
    st->property.border_color = edge;
    st->property.label_normal = st->property.label_hover =
        st->property.label_active = text;
    st->property.edit = st->edit;
    st->property.edit.normal = st->property.edit.hover =
        st->property.edit.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    st->property.edit.padding  = nk_vec2(2.0f, 2.0f);
    st->property.edit.border   = 0.0f;
    st->property.edit.rounding = 0.0f;
    style_flat_button(&st->property.inc_button, hover, muted, 1.0f);
    style_flat_button(&st->property.dec_button, hover, muted, 1.0f);

    reaktor_style_get("select", &sel);
    st->combo.normal        = sel.matched ? nk_style_item_color(col_of(sel.bg))
                                          : i_base;
    st->combo.hover         = i_hover;
    st->combo.active        = i_hover;
    st->combo.border_color  = sel.matched ? col_of(sel.border_col) : edge;
    st->combo.label_normal  = st->combo.label_hover =
        st->combo.label_active = sel.matched ? col_of(sel.fg) : text;
    st->combo.symbol_normal = st->combo.symbol_hover =
        st->combo.symbol_active = muted;
    st->combo.sym_normal = st->combo.sym_hover = st->combo.sym_active =
        NK_SYMBOL_NONE;
    if (sel.matched) {
        st->combo.border   = 0.0f;
        st->combo.rounding = sel.rounding;

        {
            float inset = sel.pad_x + sel.border;
            if (inp.matched && inp.pad_x + inp.border > inset)
                inset = inp.pad_x + inp.border;
            st->combo.content_padding = nk_vec2(inset, sel.pad_y);
        }
    }
    style_flat_button(&st->combo.button, hover, muted, 7.0f);

    reaktor_style_get("details", &det);
    reaktor_style_get("summary", &sum);
    st->tab.background   = nk_style_item_color(
        det.matched ? reaktor_visible(col_of(det.bg), body, base) : base);
    st->tab.border_color = body;
    st->tab.text         = text;
    st->tab.border = 0.0f;
    if (det.matched) {
        st->tab.rounding = det.rounding;
        st->tab.padding  = nk_vec2(det.pad_x, det.pad_y);
    }
    st->tab.sym_minimize = NK_SYMBOL_NONE;
    st->tab.sym_maximize = NK_SYMBOL_NONE;
    st->property.sym_left  = NK_SYMBOL_NONE;
    st->property.sym_right = NK_SYMBOL_NONE;

    style_flat_button(&st->tab.tab_maximize_button, hover, muted, 3.0f);
    style_flat_button(&st->tab.tab_minimize_button, hover, muted, 3.0f);
    style_flat_button(&st->tab.node_maximize_button, hover, muted, 3.0f);
    style_flat_button(&st->tab.node_minimize_button, hover, muted, 3.0f);

    st->chart.background     = nk_style_item_color(base);
    st->chart.border_color   = edge;
    st->chart.color          = accent;
    st->chart.selected_color = focus;
    st->chart.border         = 1.0f;

    st->scrollh.normal        = nk_style_item_color(body);
    st->scrollh.hover         = nk_style_item_color(body);
    st->scrollh.active        = nk_style_item_color(body);
    st->scrollh.cursor_normal = nk_style_item_color(thumb);
    st->scrollh.cursor_hover  = nk_style_item_color(thumb_hi);
    st->scrollh.cursor_active = nk_style_item_color(thumb_hi);
    st->scrollh.border_color  = body;
    st->scrollh.rounding      = 3.0f;
    st->scrollh.rounding_cursor = 3.0f;
    st->scrollh.show_buttons  = nk_false;
    st->scrollv = st->scrollh;
    st->scrollv.padding = nk_vec2(0.0f, 8.0f);

    style_flat_button(&st->contextual_button, hover, text, 4.0f);
    style_flat_button(&st->menu_button, hover, text, 4.0f);
    st->contextual_button.rounding = 5.0f;
    st->menu_button.rounding       = 5.0f;

    st->window.background              = base;
    st->window.fixed_background        = i_base;
    reaktor_style_get("dialog", &dlg);
    st->window.border_color            = edge;
    st->window.popup_border_color      = dlg.matched ? col_of(dlg.border_col)
                                                     : edge;
    if (dlg.matched) st->window.popup_border = dlg.border;

    st->window.rounding = 0.0f;
    st->window.combo_border_color      = edge;
    st->window.contextual_border_color = edge;
    st->window.contextual_border       = 2.0f;
    st->window.menu_border_color       = edge;
    st->window.group_border_color      = edge;
    st->window.tooltip_border_color    = nk_rgba(muted.r, muted.g, muted.b, 90);
    st->window.scaler                  = i_hover;
    st->window.header.normal           = i_base;
    st->window.header.hover            = i_base;
    st->window.header.active           = i_base;
    st->window.header.label_normal     = st->window.header.label_hover =
        st->window.header.label_active = text;
    style_flat_button(&st->window.header.close_button, hover, muted, 5.0f);
    style_flat_button(&st->window.header.minimize_button, hover, muted, 5.0f);
}
