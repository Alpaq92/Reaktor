/* style_map.c - the seam between the stylesheet and Nuklear.
 *
 * libcss answers what the stylesheet says about a selector; this answers what
 * Nuklear has to be told so it draws that. The two models disagree in ways
 * that cost real code: CSS describes one state at a time where nk_style wants
 * all of them at once, CSS has effects Nuklear cannot draw, and Nuklear's
 * style is a push/pop stack rather than a set of values.
 *
 * Lifted out of main.c unchanged.
 */
#include "internal.h"

/* --- CSS -> nk_style ----------------------------------------------------- */

struct nk_color
col_of(const unsigned char c[4])
{
    return nk_rgba(c[0], c[1], c[2], c[3]);
}

/* Records the rect a widget occupied, the cursor it wants, and whether hover
 * changes anything. Motion is tested against this instead of forcing a frame. */
void
hot_push_ex(App *app, struct nk_rect r, int cursor, int repaint, int top,
            int track)
{
    int n = app->hot_n;

    /* Clipped to the panel being built. nk_widget_bounds answers where a
     * widget *would* go with no clip test, so a page scrolled past the top
     * registered rects for widgets above the body - over the tab strip, where
     * they won the hit test, showed the wrong cursor, and on the Popups page
     * turned the strip into a pointer-following tooltip region. Found by the
     * redraw audit; measured as frames drawn for widgets nobody could see. */
    if (app->ctx && app->ctx->current && app->ctx->current->layout) {
        struct nk_rect c = app->ctx->current->layout->clip;
        float x0 = r.x > c.x ? r.x : c.x;
        float y0 = r.y > c.y ? r.y : c.y;
        float x1 = (r.x + r.w) < (c.x + c.w) ? (r.x + r.w) : (c.x + c.w);
        float y1 = (r.y + r.h) < (c.y + c.h) ? (r.y + r.h) : (c.y + c.h);
        if (x1 <= x0 || y1 <= y0) return;     /* entirely off screen */
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

/* Pushes the computed style behind `selector` onto Nuklear's button style and
 * returns how many entries, so the caller pops the same number. A state given
 * as a gradient or box-shadow becomes a shade of the background instead. */

static style_frame
push_button_style(App *app, struct nk_context *ctx, const char *selector)
{
    reaktor_style s, hov;
    unsigned char hover[4], active[4];
    style_frame f = { 0, 0, 0, 0, 0 };
    char hsel[80];

    reaktor_style_get(selector, &s);
    if (!s.matched) return f;

    /* tiny.css names the hover (--button-hover), so it is read. Only :active
     * is shaded: tiny.css states that as a transform. */
    snprintf(hsel, sizeof(hsel), "%s:hover", selector);
    reaktor_style_get(hsel, &hov);

    memcpy(hover, hov.matched && hov.bg[3] ? hov.bg : s.bg, 4);
    memcpy(active, hover, 4);
    reaktor_style_darken(active, 0.10f);

    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(col_of(s.bg)));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(col_of(hover)));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(col_of(active)));
    f.items = 3;

    nk_style_push_color(ctx, &ctx->style.button.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.text_hover,  col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.text_active, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.button.border_color,
                        col_of(s.border_col));
    f.colors = 4;

    nk_style_push_float(ctx, &ctx->style.button.rounding, s.rounding);
    nk_style_push_float(ctx, &ctx->style.button.border, s.border);
    f.floats = 2;

    /* The vertical padding a stylesheet asks for is a wish, not a promise.
     * Nuklear insets a button's content by padding + border + rounding on
     * each side, so a button shorter than twice that has a content rect with
     * no height left - and a label centred in a rect with no height is drawn
     * from its top edge, which is the button's own middle. The text then sits
     * in the bottom half and a row of buttons agrees with nothing.
     *
     * The clamp has to be here, and this is the whole of why it was not
     * working before. reaktor_fit_label computes the same number and pushes
     * it, but a caller pushes it *around* the draw, and this function then
     * pushes the stylesheet's value over the top of it - so the fit was
     * undone before Nuklear ever read the padding. Measured on a 30px button:
     * the label's x-height sat at rows 345..351 of 329..358, four and a half
     * rows below centre. Here is where the padding is decided, so here is
     * where it is bounded. */
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

    /* tiny.css gives the button a --button-hover fill, so this one does
     * need a frame when the pointer arrives. */
    hot_push(app, nk_widget_bounds(ctx), 1, 1);
    id = reaktor_note_here(app, ctx, REAKTOR_A11Y_BUTTON, label, 0);
    f = push_button_style(app, ctx, selector);
    clicked = nk_button_label(ctx, label);
    pop_style(ctx, f);
    if (reaktor_focus_activated(app, id)) clicked = 1;
    return clicked;
}

/* WCAG relative luminance and contrast ratio. The accent button's label cannot
 * be named here: --links is #0070E0 in light, where white reads cleanly, and
 * #56c7ff in dark, where it does not. */
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

/* The stylesheet's own label colour, unless unreadable on `bg`. Not "whichever
 * measures higher": on the light accent the candidates are 4.39 and 4.46, and
 * taking the maximum flipped the label to near-white for 0.07. On the dark
 * accent white measures 1.91, and the fallback is worth taking at 8.04. */
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

/* A colour, unless it cannot be told from what is behind it. tiny.css's dark
 * palette sets --table to --background-body, so `thead` came out exactly the
 * surface it is drawn on; `details` too. */
struct nk_color
reaktor_visible(struct nk_color want, struct nk_color behind,
                struct nk_color fallback)
{
    unsigned char a[4], b[4];

    a[0] = want.r;   a[1] = want.g;   a[2] = want.b;   a[3] = want.a;
    b[0] = behind.r; b[1] = behind.g; b[2] = behind.b; b[3] = behind.a;
    return contrast_ratio(a, b) < 1.12f ? fallback : want;
}

/* The same button filled from a palette token: tiny.css is classless and has
 * one button style, so a primary action comes from --links. Base first, accent
 * on top - the other way round the base rule was pushed last and won. */
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

/* The text field. tiny.css has `input`, `input:focus` and `input::placeholder`;
 * the pseudo-element is beyond libcss, but the value behind it is
 * --text-muted. `hint` is painted into the empty field, never in the buffer. */

/* Copies the selection, as nk_edit_buffer does internally for Ctrl+C: the
 * bounds are glyph indices, so the text comes from nk_str_at_const. */
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
        /* Glyph count, not byte length: the same Nuklear bug the SDL backend
         * works around in its own paste hook. */
        nk_textedit_paste(edit, text, (int)SDL_utf8strlen(text));
    }
    SDL_free(text);
}

/* The `input` rule pushed onto nk_style.edit, shared by the login field and
 * by the showcase's, which differ only in where the buffer lives.
 *
 * Two departures from the rule as written. A field takes the button's radius
 * rather than its own: tiny.css rounds an input tighter than a button, and
 * beside the card's buttons that read as a different kind of thing. And with
 * `inset`, the fill leans toward the page body - tiny.css gives an input the
 * same colour as the card it sits on, so on the login card the field was a
 * hairline rectangle and nothing else. Leaning toward the body puts it a
 * step lighter than the card on the light scheme and a step darker on the
 * dark one, which is how a field is expected to sit. The showcase's fields
 * are on the page itself and keep the rule's fill. */
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
    /* Nuklear insets the text by border + padding, and the border is drawn
     * here as a stroke with Nuklear's own set to 0 - so the border's width
     * goes back into the padding, or the text would sit two pixels further
     * left than the rule says. Then the button's horizontal padding, if it
     * is the larger: the text starts where a button's label would. */
    s.pad_x += s.border;
    if (btn.matched && btn.pad_x > s.pad_x) s.pad_x = btn.pad_x;
    /* And never tighter than a 40px field wants: the rule's 0.6rem was
     * written for a field on a page, and next to the card's rounding the
     * text sat against the edge. */
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

    /* Nuklear carries one border colour for every edit state, so the choice
     * is made here, before the widget draws. Which field is focused is not a
     * guess: nk_edit_buffer takes `hash = win->edit.seq++` and is active when
     * that matches win->edit.name, so seq == name identifies the widget about
     * to be emitted. tiny.css gives an unfocused input `2px solid transparent`
     * and only :focus colours it - bordering every field in --focus made four
     * fields look like four focused ones. */
    {
        const struct nk_window *win = ctx->current;
        int focused = win && win->edit.active &&
                      win->edit.seq == win->edit.name;
        struct nk_color line;

        if (focused && foc.matched) {
            line = col_of(foc.border_col);
        } else {
            /* Transparent works on a page, where the fill alone separates the
             * field from --background-body. On the login card both are
             * --background, so it leaves nothing to see. */
            unsigned char c[4];
            line = col_of(s.border_col);
            if (line.a == 0)
                line = reaktor_style_token("--background-hover", c)
                     ? col_of(c) : nk_rgba(128, 128, 128, 90);
        }
        nk_style_push_color(ctx, &ctx->style.edit.border_color, line);
        /* Handed back for stroke_edit_edge, which draws the ring. */
        out->border_col[0] = line.r; out->border_col[1] = line.g;
        out->border_col[2] = line.b; out->border_col[3] = line.a;
    }
    nk_style_push_color(ctx, &ctx->style.edit.text_normal, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_hover, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.text_active, col_of(s.fg));
    nk_style_push_color(ctx, &ctx->style.edit.cursor_normal, col_of(s.fg));

    /* Selection in --focus - the colour tiny.css already puts on a focused
     * input's border - with whichever palette end stays readable on it. */
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
    /* No border from Nuklear: it draws one as two fills, and the ring is
     * stroked instead - see stroke_edit_edge. */
    nk_style_push_float(ctx, &ctx->style.edit.border, 0.0f);
    f.floats = 2;

    nk_style_push_vec2(ctx, &ctx->style.edit.padding,
                       nk_vec2(s.pad_x, s.pad_y));
    f.vec2s = 1;
    return f;
}

/* Nuklear draws an edit's border as two fills - the border colour, then the
 * background shrunk by the width. On the software renderer fills are not
 * feathered (see the note at nk_sdl_render_ex in the frame body), so that
 * ring came out as a staircase at a 6px radius, which reads as a square
 * corner. Strokes are feathered there, so the ring is drawn as one, on the
 * same footprint: a stroke of the border's width, centred half a width in,
 * covers exactly the band the two fills did. Buttons already worked this
 * way, which is why they kept their corners and the fields lost theirs. */
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
    struct nk_color grey = reaktor_style_token("--text-muted", muted)
                         ? col_of(muted) : nk_rgb(0x9a, 0x9a, 0x9a);
    /* pad_x already carries the border - see push_edit_style - so this is
     * exactly where Nuklear starts the typed text. */
    float pad = s->matched ? s->pad_x : 9.0f;
    struct nk_rect r = nk_rect(bounds.x + pad,
                               bounds.y + (bounds.h - font->height) * 0.5f,
                               bounds.w - pad * 2.0f, font->height + 2.0f);

    nk_draw_text(canvas, r, hint, (int)strlen(hint), font,
                 nk_rgba(0, 0, 0, 0), grey);
}

/* Records the field under the pointer for the drag clamp in SDL_AppEvent. Only
 * the hovered one: the clamp has to pin the gesture to where it started. */
void
note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
        app->field_rect = bounds;
        app->field_rect_valid = 1;
    }
}

/* Where the IME should put its candidate list: beside the caret of the field
 * that has focus, in window coordinates.
 *
 * The SDL3 backend cannot do this itself - its own FIXME says so - because
 * Nuklear exposes no way to ask which edit widget is active or where it is.
 * The app can, though: it is the one laying the widget out, so it knows the
 * rect, and ctx->text_edit is the state nk_edit_string was just working on.
 * Without it every IME candidate window opens at the window's origin, which
 * on a full-screen app is nowhere near what is being typed. */
void
note_ime_caret(App *app, struct nk_context *ctx, struct nk_rect bounds,
               nk_flags state, const struct nk_text_edit *edit)
{
    const struct nk_user_font *font = ctx->style.font;
    float caret = 0.0f;

    if (!(state & NK_EDIT_ACTIVE)) return;

    /* Bytes up to the caret, which is a rune index. A NULL answer means the
     * caret is past the end, and the whole string is the right measure. */
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

    /* The field's hover background is its resting colour, so hovering it
     * changes only the cursor - which needs no frame. */
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

    /* Right-click menu. Nuklear places and dismisses it; the items act on the
     * edit state directly, which is why it is ours to hold. The theme leaves
     * window.rounding at 0 - the page is square - so the popup's radius is
     * pushed here for as long as the popup can draw, as the showcase's menus
     * do; without it this one menu came out square-cornered. NK_WINDOW_BORDER
     * because a contextual popup is dynamic - it fills its body at
     * nk_panel_end, sized to its rows - and that is also where Nuklear
     * strokes the border, at the final height and at the rounding; without
     * the flag the fill's corners were a staircase on the software renderer,
     * where a fill is not feathered and only a stroke is. */
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

/* --- the palette half of the seam ----------------------------------------
 *
 * tiny.css is classless: it has rules for `button`, `input`, `select`,
 * `textarea` and `table`, and nothing for a slider, knob, chart, tree,
 * scrollbar, menu or popup. Those are styled from the palette instead - the
 * same custom properties the rules are written in terms of - so the whole UI
 * still comes out of the stylesheet. Written into ctx->style once per theme
 * rather than pushed per widget. src/style.c is the rule half. */

/* `pad` is not decoration: nk_do_button subtracts it, the border and the corner
 * radius from the content rect, and a symbol fills what is left - so it is the
 * only control over the glyph's size. */
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
    /* nk_do_toggle grows its row to font->height + 2 * padding.y, so anything
     * generous makes a checkbox taller than the menu items above it. */
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
    unsigned char c[4], ac[4];

    if (!app->ctx) return;
    ctx = app->ctx;
    st  = &ctx->style;

    body   = reaktor_style_token("--background-body", c) ? col_of(c)
                                                         : app->page;
    base   = reaktor_style_token("--background", c)      ? col_of(c)
                                                         : app->card_bg;
    hover  = reaktor_style_token("--background-hover", c) ? col_of(c) : base;
    text   = reaktor_style_token("--text-main", c)   ? col_of(c) : app->text;
    muted  = reaktor_style_token("--text-muted", c)  ? col_of(c) : text;
    bright = reaktor_style_token("--text-bright", c) ? col_of(c) : text;
    accent = reaktor_style_token("--links", c)       ? col_of(c) : text;
    focus  = reaktor_style_token("--focus", c)       ? col_of(c) : accent;

    /* tiny.css draws its borders in --background-hover; there is no --border
     * token to read, so the same value serves both. */
    edge = hover;

    /* A label on a filled accent, by measured contrast: the accent is a mid
     * blue in light and a pale one in dark, and no single choice reads on
     * both. */
    on_accent = reaktor_style_token("--links", ac)
              ? readable_on(ac, "--text-bright", "--background-body") : bright;

    /* A scrollbar thumb has no colour of its own in any stylesheet. It is the
     * muted text colour at partial alpha, which is what every desktop's is. */
    thumb    = nk_rgba(muted.r, muted.g, muted.b, 110);
    thumb_hi = nk_rgba(muted.r, muted.g, muted.b, 175);

    i_none   = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    i_base   = nk_style_item_color(base);
    i_hover  = nk_style_item_color(hover);
    i_accent = nk_style_item_color(accent);

    st->text.color = text;

    /* button and input are the two widgets tiny.css names, so they come from
     * their rules. Set globally as well as pushed per widget, so a page can
     * call nk_button_label with no ceremony. */
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

    /* Outlined in the muted text colour, not `edge`: --background-hover
     * against --background is sixteen levels, which disappears outright on a
     * panel of that colour - a checkbox in a menu had no visible box. */
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
    /* The glyph square is the row height less the padding, so without an inset
     * a 32px row gives a 28px icon. */
    st->selectable.image_padding = nk_vec2(7.0f, 7.0f);

    st->slider.normal        = i_none;
    st->slider.hover         = i_none;
    st->slider.active        = i_none;
    st->slider.bar_normal    = edge;
    st->slider.bar_hover     = edge;
    st->slider.bar_active    = edge;
    st->slider.bar_filled    = accent;
    st->slider.cursor_normal = i_accent;
    st->slider.cursor_hover  = nk_style_item_color(focus);
    st->slider.cursor_active = nk_style_item_color(focus);
    st->slider.border_color  = edge;
    /* Six pixels and a radius of three, rather than Nuklear's four and two.
     * A two-pixel arc does not survive the software renderer: every vertex of
     * it lands on the same pixel once the backend puts them on the grid, and
     * the cap came out square, which on a bar flush with the column edge read
     * as one running off it. Three is enough of an arc to stay an arc. */
    st->slider.bar_height    = 6.0f;
    st->slider.rounding      = 3.0f;
    st->slider.show_buttons  = nk_false;

    st->knob.normal            = i_none;
    st->knob.hover             = i_none;
    st->knob.active            = i_none;
    st->knob.knob_normal       = base;
    st->knob.knob_hover        = hover;
    st->knob.knob_active       = hover;
    st->knob.knob_border_color = edge;
    st->knob.cursor_normal     = accent;
    st->knob.cursor_hover      = focus;
    st->knob.cursor_active     = focus;
    st->knob.border_color      = edge;

    st->progress.normal              = i_base;
    st->progress.hover               = i_base;
    st->progress.active              = i_base;
    st->progress.cursor_normal       = i_accent;
    st->progress.cursor_hover        = nk_style_item_color(focus);
    st->progress.cursor_active       = nk_style_item_color(focus);
    st->progress.border_color        = edge;
    st->progress.cursor_border_color = accent;
    /* No outline on the track either: at the button's corner it is a dark
     * ring round a bright fill, and the ring's own arc is a fill's arc, so it
     * steps where the fill under it does. The track's tone is enough to say
     * where the bar ends. */
    st->progress.border              = 0.0f;
    /* No outline on the fill: Nuklear strokes it in cursor_border_color on top,
     * which on a solid accent is a lighter line inside the bar. */
    st->progress.cursor_border       = 0.0f;
    /* tiny.css has no `progress` rule, so the track and the fill borrow the
     * button's corner. A rounded rect whose radius exceeds half its width
     * self-intersects and Nuklear does not clamp it, so a bar in its first
     * few per cent would draw as a knot; progress_cell clamps the fill's
     * radius to its own width before each call. */
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
    /* The value is a real edit widget, so it inherits `input` - whose 0.6rem
     * padding plus 2px border is more than a 30px row has, leaving the number
     * no height at all. It keeps the colours and takes its own geometry. */
    st->property.edit = st->edit;
    st->property.edit.normal = st->property.edit.hover =
        st->property.edit.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    st->property.edit.padding  = nk_vec2(2.0f, 2.0f);
    st->property.edit.border   = 0.0f;
    st->property.edit.rounding = 0.0f;
    /* A property's steppers are half the font's height square, not the row's,
     * so they take almost no inset before the arrow vanishes. */
    style_flat_button(&st->property.inc_button, hover, muted, 1.0f);
    style_flat_button(&st->property.dec_button, hover, muted, 1.0f);

    /* A combo is a select, and tiny.css has a rule for one - its own fill,
     * border, radius and padding rather than the card's background. */
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
    /* No symbol: nk_draw_symbol builds its chevron from the corners of the box,
     * so the angle is the aspect ratio - a wide, flat V. The page paints the
     * Ionicon over the button instead. */
    st->combo.sym_normal = st->combo.sym_hover = st->combo.sym_active =
        NK_SYMBOL_NONE;
    if (sel.matched) {
        /* Unmodified. Nuklear's antialiased stroke lands unevenly when the
         * rect is off whole pixels - 1px left against 2px right - and
         * widening only moved it; the fix is on the layout side. */
        /* Zero; the page strokes it - see combo_chrome in showcase.c. Same
         * stroke bias, and no style value evens it out. */
        st->combo.border   = 0.0f;
        st->combo.rounding = sel.rounding;

        /* CSS measures padding inside the border, Nuklear from the outer edge,
         * and the border is the page's to draw - so the two are added. Floored
         * at `input`'s inset: select's 0.25rem against input's 0.55rem read as
         * an oversight side by side. */
        {
            float inset = sel.pad_x + sel.border;
            if (inp.matched && inp.pad_x + inp.border > inset)
                inset = inp.pad_x + inp.border;
            st->combo.content_padding = nk_vec2(inset, sel.pad_y);
        }
    }
    style_flat_button(&st->combo.button, hover, muted, 7.0f);

    /* A tree is a <details> and its header a <summary>; both have rules. The
     * arrow comes from summary::before, which libcss cannot reach. */
    reaktor_style_get("details", &det);
    reaktor_style_get("summary", &sum);
    st->tab.background   = nk_style_item_color(
        det.matched ? reaktor_visible(col_of(det.bg), body, base) : base);
    /* Nuklear fills a tab header twice: the border rect at a literal 0
     * rounding, then the background at tab.rounding. The first is square
     * whatever the style says, so painting it in the surface behind, with no
     * border, leaves the rounded fill alone. */
    st->tab.border_color = body;
    /* One text colour for both kinds of tree row. `summary` resolves dimmer
     * and reaches only the plain nodes - element rows draw through
     * nk_style_selectable - so honouring it split the tree's colours. */
    st->tab.text         = text;
    st->tab.border = 0.0f;
    if (det.matched) {
        st->tab.rounding = det.rounding;
        st->tab.padding  = nk_vec2(det.pad_x, det.pad_y);
    }
    /* No symbol from Nuklear on tree headers or property steppers. Its
     * chevron is two one-pixel lines corner to corner of the box it is
     * handed - thin, and small beside the combo's - so the slots are left
     * empty and the showcase draws the Ionicon into them: chevron_at in
     * showcase.c. */
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
    /* Inset from both ends of its track, so the thumb clears the tab strip and
     * the window edge. Only useful with the zeroed footer below. */
    st->scrollv.padding = nk_vec2(0.0f, 8.0f);

    style_flat_button(&st->contextual_button, hover, text, 4.0f);
    style_flat_button(&st->menu_button, hover, text, 4.0f);
    /* A 2px radius on a 26px row is square in all but name, and the wash sat
     * in a rounded menu with hard corners of its own. */
    st->contextual_button.rounding = 5.0f;
    st->menu_button.rounding       = 5.0f;

    st->window.background              = base;
    st->window.fixed_background        = i_base;
    /* A popup is a <dialog>. Only its frame is reachable - the backdrop is
     * ::backdrop, which is a pseudo-element. */
    reaktor_style_get("dialog", &dlg);
    st->window.border_color            = edge;
    st->window.popup_border_color      = dlg.matched ? col_of(dlg.border_col)
                                                     : edge;
    if (dlg.matched) st->window.popup_border = dlg.border;

    /* Nuklear has one rounding for every panel, and the page, titlebar, tab
     * strip and body are all panels - taking it from `dialog` rounded the
     * window and then the titlebar inside it. The transient panels push their
     * own; see reaktor_popup_rounding. */
    st->window.rounding = 0.0f;
    st->window.combo_border_color      = edge;
    st->window.contextual_border_color = edge;
    /* 2px, not Nuklear's 1: a contextual popup is dynamic and fills its body
     * at nk_panel_end, unfeathered on the software renderer, so its corners
     * are a staircase up to half a pixel either side of the arc. A 1px rim
     * centred on that arc covers half of it; a 2px rim covers all of it and
     * the corner reads as the stroke's own feathered curve. */
    st->window.contextual_border       = 2.0f;
    st->window.menu_border_color       = edge;
    st->window.group_border_color      = edge;
    /* Not `edge`: --background-hover is both border and hover fill, so the
     * frame vanished into the button under it. */
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

