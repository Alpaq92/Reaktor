/* showcase.c - every Nuklear widget on screen, under the same stylesheet.
 *
 * The point is not a gallery for its own sake. tiny.css is classless: it has
 * rules for `button`, `input`, `select`, `textarea`, `table` and little else,
 * so a slider, a knob, a chart or a tree has no rule to read and must be
 * styled from the palette tokens instead. Drawing all of them is what shows
 * where that line falls - see curie_style_widgets() in main.c, which is the
 * whole of the token half of the seam.
 *
 * Layout is Nuklear's. Every page here is rows and groups; nothing computes a
 * position that Nuklear could compute itself, except where the point of the
 * section is precisely that it can't (nk_layout_space). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nk_common.h"
#include "ui.h"
#include "style.h"

/* A menu row, and the height of a popup holding n of them: the rows, the
 * 2px popup_style_push puts between them, and the panel's own padding. */
#define MENU_ROW  22.0f
#define MENU_H(n) ((n) * MENU_ROW + ((n) - 1) * 2.0f + 10.0f)

#define ROW       30.0f    /* one control */
#define ROW_TALL  34.0f    /* one control with a label inside it */
#define ROW_SMALL 22.0f

const char *const curie_rss_names[RSS_STEPS] = {
    "before any of it", "SDL_Init(VIDEO)", "window and renderer",
    "window icon", "Nuklear context", "font atlas", "stylesheets"
};

const char *const curie_tab_names[TAB_COUNT] = {
    "Login", "Buttons", "Inputs", "Display", "Layout", "Popups", "Diagnostics"
};

/* --- page furniture ------------------------------------------------------ */

static float
content_w(struct nk_context *ctx)
{
    return nk_window_get_content_region_size(ctx).x;
}

static void
heading(App *app, struct nk_context *ctx, const char *text)
{
    struct nk_color c = curie_token("--text-bright", ctx->style.text.color);

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    nk_style_push_font(ctx, curie_font(app, 19, 1));
    nk_label_colored(ctx, text, NK_TEXT_LEFT, c);
    nk_style_pop_font(ctx);
}

/* A wrapped note under a heading. The row has to be tall enough before the
 * text is emitted - Nuklear cannot grow it afterwards - so the line count is
 * measured from the font rather than guessed, with one line of slack because
 * breaking on words can always cost one more than the raw width implies. */
static void
caption(App *app, struct nk_context *ctx, const char *text)
{
    const struct nk_user_font *f = curie_font(app, 16, 0);
    struct nk_color c = curie_token("--text-muted", ctx->style.text.color);
    float avail = content_w(ctx) - 4.0f;
    float tw    = f->width(f->userdata, f->height, text, (int)strlen(text));
    int   lines = 1;

    if (avail > 1.0f && tw > avail) lines = (int)(tw / avail) + 2;

    nk_style_push_font(ctx, f);
    nk_layout_row_dynamic(ctx, (f->height + 3.0f) * (float)lines, 1);
    nk_label_colored_wrap(ctx, text, c);
    nk_style_pop_font(ctx);
    nk_layout_row_dynamic(ctx, 6.0f, 1);
    nk_spacer(ctx);
}

/* nk_rule_horizontal fills the whole widget rect rather than stroking a line
 * inside it, so the row height *is* the rule's thickness - at the 9px row it
 * was first given it came out as a bar. */
static void
rule(struct nk_context *ctx)
{
    nk_layout_row_dynamic(ctx, 14.0f, 1);
    nk_spacer(ctx);
    nk_layout_row_dynamic(ctx, 1.0f, 1);
    nk_rule_horizontal(ctx, curie_token("--background-hover",
                                        nk_rgba(128, 128, 128, 90)), nk_false);
    nk_layout_row_dynamic(ctx, 6.0f, 1);
    nk_spacer(ctx);
}

/* nk_do_button subtracts the padding, the border *and* the corner radius from
 * the content rect. tiny.css asks for 0.6rem of padding, a 2px border and an
 * 8px radius, which comes to 37px of inset - so on a 30px square button the
 * content rect is negative, and every symbol drawn with a fill rather than a
 * stroke vanishes while the stroked ones (X, the chevrons, the hamburger)
 * still appear. That is also what emptied the window controls earlier: not
 * the artwork and not nk_button_image, but a content rect with no area.
 *
 * So a row whose widget *is* the glyph gets its own geometry. The colours
 * still come from the stylesheet; only the inset is ours. */
static void
compact_push(struct nk_context *ctx)
{
    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(4.0f, 4.0f));
    nk_style_push_float(ctx, &ctx->style.button.rounding, 3.0f);
}

static void
compact_pop(struct nk_context *ctx)
{
    nk_style_pop_float(ctx);
    nk_style_pop_vec2(ctx);
}

/* A small line naming the call being demonstrated, so the page reads as
 * documentation rather than as decoration. Deliberately below the caption:
 * it is a reference, not part of the prose. */
static void
api(App *app, struct nk_context *ctx, const char *text)
{
    nk_style_push_font(ctx, curie_font(app, 12, 0));
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label_colored(ctx, text, NK_TEXT_LEFT,
                     curie_token("--links", ctx->style.text.color));
    nk_style_pop_font(ctx);
    nk_layout_row_dynamic(ctx, 4.0f, 1);
    nk_spacer(ctx);
}

static void
section(App *app, struct nk_context *ctx, const char *title, const char *note)
{
    rule(ctx);
    heading(app, ctx, title);
    if (note) caption(app, ctx, note);
}

/* Popups, tooltips and menus are panels like any other, so they read
 * window.fixed_background and window.rounding - and what is in force on a
 * page is the page's own colour and a square corner, which is right for the
 * groups a page nests inside itself and wrong for something that has to sit
 * above it. Pushed around those calls only, because both kinds read the same
 * two fields. */
static void
popup_style_push(struct nk_context *ctx)
{
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(
                                 curie_token("--background",
                                             nk_rgb(53, 53, 53))));
    nk_style_push_float(ctx, &ctx->style.window.rounding,
                        curie_popup_rounding());
    /* Nuklear's default leaves a dialog's text hard against its own frame. */
    nk_style_push_vec2(ctx, &ctx->style.window.popup_padding,
                       nk_vec2(16.0f, 12.0f));
}

static void
popup_style_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}

/* The page's 9px between rows is right for a page and wrong for a menu, where
 * the items are a list and belong against each other. Pushed *inside* the
 * popup: window.spacing is read when any rows are laid out, so pushing it
 * around the section instead pulled the page's own controls together. */
static void
menu_rows_push(struct nk_context *ctx)
{
    nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(4.0f, 2.0f));
}

static void
menu_rows_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
}

/* How far a combo's chevron sits from its right edge, and how much clear
 * space is left to its left. */
#define COMBO_ARROW    15.0f
#define COMBO_MARGIN   12.0f
#define COMBO_GAP      12.0f

/* Where a combo's swatch or content may go: from its left inset to the clear
 * space before the arrow. */
static struct nk_rect
combo_content(struct nk_context *ctx, struct nk_rect h)
{
    struct nk_rect r;

    /* Exactly the label's x: nk_combo_begin_text puts its text at
     * header.x + content_padding.x, and the swatch in the combo beside it
     * should start on the same line rather than two pixels off it. */
    r.x = h.x + ctx->style.combo.content_padding.x;
    r.y = h.y + ctx->style.combo.content_padding.y + 2.0f;
    r.h = h.h - 2.0f * (ctx->style.combo.content_padding.y + 2.0f);
    r.w = (h.x + h.w - COMBO_MARGIN - COMBO_ARROW - COMBO_GAP) - r.x;
    if (r.w < 0.0f) r.w = 0.0f;
    return r;
}

/* The combo's frame and drop arrow, both drawn here.
 *
 * The arrow because Nuklear builds its chevron from the corners of whatever
 * box it is handed, so the angle is that box's aspect and nothing else; this
 * is the Ionicon the rest of the app uses, at a fixed size, hard against the
 * right edge.
 *
 * The frame because nk_stroke_rect is biased - a 2px border measured one
 * pixel down the left edge and two down the right. Stroking it here, inset by
 * half its own width so the line lands wholly inside the widget, puts the
 * same number of pixels on every side. `h` is the bounds captured before the
 * combo was emitted. */
static void
combo_chrome(App *app, struct nk_context *ctx, struct nk_rect h, float border)
{
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    float px = COMBO_ARROW;
    struct nk_rect r;
    struct nk_image im;

    if (border > 0.0f) {
        struct nk_rect f = nk_rect(h.x + border * 0.5f, h.y + border * 0.5f,
                                   h.w - border, h.h - border);
        nk_stroke_rect(canvas, f, ctx->style.combo.rounding, border,
                       ctx->style.combo.border_color);
    }

    r.x = h.x + h.w - COMBO_MARGIN - px;
    r.y = h.y + (h.h - px) * 0.5f;
    r.w = r.h = px;
    im = curie_ionicon(app, "chevron-down-outline", (int)px);
    nk_draw_image(canvas, r, &im, nk_rgb(255, 255, 255));
}

/* A tooltip, drawn straight onto the canvas at the pointer.
 *
 * Nuklear's own nk_tooltip opens a NK_POPUP_DYNAMIC panel, and a dynamic
 * panel does not fill its body when it begins - it patches the strips round
 * its content at nk_panel_end and strokes its border at bounds sized before
 * the content shrank them. The frame that leaves standing off the filled box
 * is not a style value and cannot be styled away. This is two draw calls and
 * it is exact.
 *
 * `bar` >= 0 appends a progress bar, which is what the laid-out example is
 * demonstrating: a tooltip can hold more than a line of text. */
static void
draw_tooltip(App *app, struct nk_context *ctx, const char *const *lines,
             int n, float bar)
{
    const struct nk_user_font *f = curie_font(app, 13, 0);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    /* --background-hover, not --background: a tooltip floats above whatever
     * it is describing, and against a panel of the very same colour it read
     * as though it were half transparent. This is the palette's next step up
     * and sits clear of both the page and a panel. */
    struct nk_color fill = curie_token("--background-hover",
                                       nk_rgb(69, 69, 69));
    struct nk_color ink  = curie_token("--text-main", nk_rgb(247, 247, 247));
    struct nk_color edge = curie_token("--text-muted", nk_rgb(192, 192, 192));
    struct nk_color acc  = curie_token("--links", nk_rgb(86, 199, 255));
    float pad = 10.0f, line = f->height + 6.0f;
    float w = 0.0f, h = 2.0f * pad + (float)n * line;
    struct nk_rect r;
    int i;

    for (i = 0; i < n; i++) {
        float lw = f->width(f->userdata, f->height, lines[i],
                            (int)strlen(lines[i]));
        if (lw > w) w = lw;
    }
    w += 2.0f * pad;
    if (bar >= 0.0f) h += 10.0f;

    /* Below and right of the pointer, the way every desktop places one. */
    r = nk_rect(ctx->input.mouse.pos.x + 14.0f,
                ctx->input.mouse.pos.y + 18.0f, w, h);

    nk_fill_rect(canvas, r, 6.0f, fill);
    nk_stroke_rect(canvas, nk_rect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f,
                                   r.h - 1.0f), 6.0f, 1.0f,
                   nk_rgba(edge.r, edge.g, edge.b, 140));

    for (i = 0; i < n; i++) {
        struct nk_rect t = nk_rect(r.x + pad, r.y + pad + (float)i * line,
                                   r.w - 2.0f * pad, line);
        nk_draw_text(canvas, t, lines[i], (int)strlen(lines[i]), f,
                     nk_rgba(0, 0, 0, 0), ink);
    }
    if (bar >= 0.0f) {
        struct nk_rect track = nk_rect(r.x + pad, r.y + h - pad - 6.0f,
                                       r.w - 2.0f * pad, 6.0f);
        nk_fill_rect(canvas, track, 3.0f,
                     curie_token("--background-body", nk_rgb(37, 37, 37)));
        track.w *= bar;
        nk_fill_rect(canvas, track, 3.0f, acc);
    }
}

/* A menu item with a trailing glyph, right-aligned.
 *
 * nk_menu_item_symbol_label would be the obvious call and cannot do this: it
 * centres its label whatever alignment it is passed, because
 * nk_draw_button_text_symbol calls nk_widget_text with NK_TEXT_CENTERED and
 * ignores the argument - the argument only picks which side the glyph goes
 * on. So the item is a plain left-aligned one and the glyph is painted over
 * its right edge, which is where a menu's accessory belongs and where the
 * combo's chevron already is.
 *
 * `contextual` selects which of the two item calls to make; they differ only
 * in which popup they close. */
static int
item_with_icon(App *app, struct nk_context *ctx, const char *label,
               const char *ionicon, int contextual)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    float px = 16.0f;
    struct nk_rect r;
    struct nk_image im;
    int hit;

    curie_hot_top(app, b, 1, 1);
    hit = contextual ? nk_contextual_item_label(ctx, label, NK_TEXT_LEFT)
                     : nk_menu_item_label(ctx, label, NK_TEXT_LEFT);

    r = nk_rect(b.x + b.w - px - 8.0f, b.y + (b.h - px) * 0.5f, px, px);
    im = curie_ionicon(app, ionicon, (int)px);
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
    return hit;
}

/* A menu item, registered so the pointer changes over it. */
static int
menu_item(App *app, struct nk_context *ctx, const char *label, int contextual)
{
    curie_hot_top(app, nk_widget_bounds(ctx), 1, 1);
    return contextual ? nk_contextual_item_label(ctx, label, NK_TEXT_LEFT)
                      : nk_menu_item_label(ctx, label, NK_TEXT_LEFT);
}

/* Marks the widget about to be emitted as wanting a pointer cursor. Buttons
 * and fields do this for themselves inside the shell; this is for the ones
 * drawn straight from Nuklear. */
static void
hot(App *app, struct nk_context *ctx, int cursor)
{
    curie_hot(app, nk_widget_bounds(ctx), cursor, 1);
}

/* Checkboxes and radios sit the way the Edit menu sits them: label at the
 * left of its cell, box at the right. nk_checkbox_label_align puts the box
 * flush against the edge of the widget it is given, with no padding of its
 * own, so the clear space between one cell and the next label has to come
 * from the row - hence a static gap column after every cell, which the three
 * cell helpers below fill so no call site has to remember it. */
#define CHECK_CELL 190.0f
#define CHECK_GAP   12.0f
#define CHECK_ALIGN NK_WIDGET_RIGHT, NK_TEXT_LEFT

static void
check_row(struct nk_context *ctx, int cells)
{
    int i;

    nk_layout_row_template_begin(ctx, ROW);
    for (i = 0; i < cells; i++) {
        nk_layout_row_template_push_static(ctx, CHECK_CELL);
        nk_layout_row_template_push_static(ctx, CHECK_GAP);
    }
    /* Soaks up the rest of the width so the cells stay the size they were
     * asked for rather than sharing the window between them. */
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
}

static void
check_cell(App *app, struct nk_context *ctx, const char *label, nk_bool *on)
{
    hot(app, ctx, 1);
    nk_checkbox_label_align(ctx, label, on, CHECK_ALIGN);
    nk_spacer(ctx);
}

/* Nuklear has nk_checkbox_flags_label but no aligned form of it, so this is
 * that wrapper's own few lines with the aligned call in the middle. */
static void
flags_cell(App *app, struct nk_context *ctx, const char *label,
           unsigned *flags, unsigned value)
{
    nk_bool on = (*flags & value) != 0;

    hot(app, ctx, 1);
    if (nk_checkbox_label_align(ctx, label, &on, CHECK_ALIGN)) {
        if (on) *flags |= value;
        else    *flags &= ~value;
    }
    nk_spacer(ctx);
}

static void
radio_cell(App *app, struct nk_context *ctx, const char *label, int *sel,
           int value)
{
    hot(app, ctx, 1);
    if (nk_option_label_align(ctx, label, *sel == value, CHECK_ALIGN))
        *sel = value;
    nk_spacer(ctx);
}

/* A button that exists only to be looked at. Same as nk_button_label, plus
 * the hover registration every interactive widget owes the shell now that the
 * page-wide backstop no longer forces a frame. */
static int
demo_button(App *app, struct nk_context *ctx, const char *label)
{
    hot(app, ctx, 1);
    return nk_button_label(ctx, label);
}

/* --- first-run values ---------------------------------------------------- */

static void
seed(showcase_state *s)
{
    int i;

    SDL_strlcpy(s->name, "Ada Lovelace", sizeof(s->name));
    s->name_len = (int)strlen(s->name);
    SDL_strlcpy(s->digits, "1815", sizeof(s->digits));
    s->digits_len = (int)strlen(s->digits);
    SDL_strlcpy(s->hex, "56c7ff", sizeof(s->hex));
    s->hex_len = (int)strlen(s->hex);
    /* Broken by hand, because Nuklear's editor breaks lines on newlines and
     * nothing else - there is no word wrap in nk_do_edit at any flag. */
    SDL_strlcpy(s->note,
                "A multiline editor: NK_EDIT_BOX.\n"
                "Tab is accepted, and the scrollbars\n"
                "appear when they are needed.\n"
                "\n"
                "Cut, copy and paste work because the\n"
                "shell pairs nk_input_begin with\n"
                "nk_input_end - without that Nuklear\n"
                "computes no key edges at all and\n"
                "Ctrl+V is silently dead.", sizeof(s->note));
    s->note_len = (int)strlen(s->note);

    s->slider_f = 0.65f;
    s->slider_i = 40;
    s->knob     = 0.3f;
    s->prop_i   = 12;
    s->prop_f   = 1.75f;
    s->prop_d   = 6.25;
    s->progress = 62;
    s->flags    = 1u | 4u;
    s->radio    = 1;
    s->check_wrap  = nk_true;
    s->sel_tile[1] = nk_true;
    s->list_sel = -1;
    s->tint.r = 0.34f; s->tint.g = 0.78f; s->tint.b = 1.0f; s->tint.a = 1.0f;
    SDL_strlcpy(s->menu_pick, "nothing yet", sizeof(s->menu_pick));

    /* A fixed waveform rather than random noise: the chart then looks the
     * same in every screenshot, which is what makes a visual change to the
     * styling visible at all. */
    for (i = 0; i < SC_SERIES_N; i++) {
        float t = (float)i / (float)(SC_SERIES_N - 1);
        s->series[i] = sinf(t * 6.2831853f) * 0.7f + sinf(t * 18.849556f) * 0.3f;
    }
    s->seeded = 1;
}

/* --- buttons and toggles ------------------------------------------------- */

static const enum nk_symbol_type g_symbols[] = {
    NK_SYMBOL_X, NK_SYMBOL_UNDERSCORE,
    NK_SYMBOL_CIRCLE_SOLID, NK_SYMBOL_CIRCLE_OUTLINE,
    NK_SYMBOL_RECT_SOLID, NK_SYMBOL_RECT_OUTLINE,
    NK_SYMBOL_TRIANGLE_UP, NK_SYMBOL_TRIANGLE_DOWN,
    NK_SYMBOL_TRIANGLE_LEFT, NK_SYMBOL_TRIANGLE_RIGHT,
    NK_SYMBOL_TRIANGLE_UP_OUTLINE, NK_SYMBOL_TRIANGLE_DOWN_OUTLINE,
    NK_SYMBOL_TRIANGLE_LEFT_OUTLINE, NK_SYMBOL_TRIANGLE_RIGHT_OUTLINE,
    NK_SYMBOL_PLUS, NK_SYMBOL_MINUS,
    NK_SYMBOL_CHEVRON_UP, NK_SYMBOL_CHEVRON_RIGHT,
    NK_SYMBOL_CHEVRON_DOWN, NK_SYMBOL_CHEVRON_LEFT,
    NK_SYMBOL_HAMBURGER
};
#define SYMBOL_N ((int)(sizeof(g_symbols) / sizeof(g_symbols[0])))

static void
page_buttons(App *app, struct nk_context *ctx, showcase_state *s)
{
    char line[96];
    int i;

    section(app, ctx, "Buttons",
            "The fill, border, radius, padding and font all come from "
            "tiny.css's `button` rule; the hover colour is its "
            "--button-hover. The accent is --links, which is the only thing "
            "in a classless stylesheet that means \"this is the one to "
            "press\".");
    api(app, ctx, "nk_button_label  /  nk_button_image_label");

    nk_layout_row_dynamic(ctx, 38.0f, 3);
    if (curie_button(app, ctx, "Default")) s->presses++;
    if (curie_button_accent(app, ctx, "Primary")) s->presses++;
    if (curie_button_icon(app, ctx, "key-outline", "With icon")) s->presses++;

    section(app, ctx, "Symbols",
            "nk_button_symbol draws Nuklear's own vector glyphs from the "
            "button's text colour - no atlas, no SVG, no font. All "
            "twenty-one of them.");
    api(app, ctx, "nk_button_symbol  /  nk_button_symbol_label");

    compact_push(ctx);
    nk_layout_row_static(ctx, 34.0f, 34, 21);
    for (i = 0; i < SYMBOL_N; i++) {
        hot(app, ctx, 1);
        if (nk_button_symbol(ctx, g_symbols[i])) s->presses++;
    }
    compact_pop(ctx);

    nk_layout_row_dynamic(ctx, 34.0f, 3);
    hot(app, ctx, 1);
    if (nk_button_symbol_label(ctx, NK_SYMBOL_TRIANGLE_LEFT, "Back",
                               NK_TEXT_RIGHT)) s->presses++;
    hot(app, ctx, 1);
    if (nk_button_symbol_label(ctx, NK_SYMBOL_TRIANGLE_RIGHT, "Forward",
                               NK_TEXT_LEFT)) s->presses++;
    hot(app, ctx, 1);
    if (nk_button_symbol_label(ctx, NK_SYMBOL_HAMBURGER, "Menu",
                               NK_TEXT_LEFT)) s->presses++;

    section(app, ctx, "Colour, image, repeat and disabled",
            "nk_button_color paints a swatch and nothing else. A repeater "
            "fires for as long as it is held rather than once on release. "
            "nk_widget_disable_begin swallows the click and multiplies "
            "every colour by a factor - which reads as \"greyed out\" on a "
            "dark palette and, since multiplying can only darken, as a "
            "*darker* button on a light one. Worth knowing before "
            "relying on it to mean unavailable.");
    api(app, ctx, "nk_button_color  /  nk_button_image  /  "
                  "nk_button_set_behavior  /  nk_widget_disable_begin");

    /* nk_button_image stretches the image across the whole content rect
     * rather than centring it at its natural size, so the slot has to be
     * square or the glyph is smeared. nk_button_image_label - which is what
     * the window controls use - insets it by image_padding instead. */
    nk_layout_row_template_begin(ctx, 40.0f);
    nk_layout_row_template_push_static(ctx, 60.0f);
    nk_layout_row_template_push_static(ctx, 40.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    compact_push(ctx);
    hot(app, ctx, 1);
    if (nk_button_color(ctx, nk_rgb_cf(s->tint))) s->presses++;
    hot(app, ctx, 1);
    if (nk_button_image(ctx, curie_ionicon(app, "cloud-download-outline", 24)))
        s->presses++;
    compact_pop(ctx);

    nk_button_set_behavior(ctx, NK_BUTTON_REPEATER);
    hot(app, ctx, 1);
    if (nk_button_label(ctx, "Hold me")) s->repeats++;
    nk_button_set_behavior(ctx, NK_BUTTON_DEFAULT);

    nk_widget_disable_begin(ctx);
    nk_button_label(ctx, "Disabled");
    nk_widget_disable_end(ctx);

    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "%d presses, %d repeat ticks",
                 s->presses, s->repeats);
    nk_style_push_font(ctx, curie_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     curie_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);

    section(app, ctx, "Checkboxes and radios",
            "A checkbox writes a bool. nk_checkbox_flags_label writes one bit "
            "of an unsigned instead, which is how a set of independent "
            "options is held in a single value. Radios are a group only "
            "because the code makes them one: nk_option_label takes the "
            "answer to \"am I the active one\" and returns whether it was "
            "clicked. Both sit their box at the right of the cell, the way "
            "the Edit menu does; the flags form has no aligned variant, so "
            "this page wraps the aligned call in its own four lines.");
    api(app, ctx, "nk_checkbox_label_align  /  nk_option_label_align");

    /* A fixed grid, not dynamic columns: two dynamic columns put the second
     * checkbox at the halfway mark of a 960px window, which reads as two
     * unrelated controls rather than a pair. */
    check_row(ctx, 2);
    check_cell(app, ctx, "Wrap long lines", &s->check_wrap);
    check_cell(app, ctx, "Check spelling", &s->check_spell);

    check_row(ctx, 3);
    flags_cell(app, ctx, "read", &s->flags, 1u);
    flags_cell(app, ctx, "write", &s->flags, 2u);
    flags_cell(app, ctx, "execute", &s->flags, 4u);

    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "flags = 0x%02x", s->flags);
    nk_style_push_font(ctx, curie_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     curie_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);

    check_row(ctx, 3);
    for (i = 0; i < 3; i++) {
        static const char *const names[3] = { "Never", "On Wi-Fi", "Always" };
        radio_cell(app, ctx, names[i], &s->radio, i);
    }

    section(app, ctx, "Selectables",
            "A selectable is a label that holds a pressed state - the widget "
            "a list or a tile grid is built out of. It also takes a symbol "
            "or an image, which is how a toolbar is made without a single "
            "custom draw call.");
    api(app, ctx, "nk_selectable_label  /  nk_selectable_symbol_label  /  "
                  "nk_selectable_image_label");

    nk_layout_row_static(ctx, 32.0f, 140, 4);
    for (i = 0; i < 4; i++) {
        char lab[16];
        SDL_snprintf(lab, sizeof(lab), "Tile %d", i + 1);
        hot(app, ctx, 1);
        nk_selectable_label(ctx, lab, NK_TEXT_CENTERED, &s->sel_tile[i]);
    }

    /* Sized to their contents rather than stretched across half the window,
     * and centred.
     *
     * nk_do_selectable_image pins the glyph to the *right* edge when the
     * alignment is NK_TEXT_LEFT and to the left edge otherwise, and then lays
     * the label across the full width either way - it never reserves room for
     * the glyph. So "glyph, then label beside it" is not expressible; glyph
     * left with the label centred is the arrangement that reads as one
     * control rather than two things at opposite ends. */
    nk_layout_row_static(ctx, 32.0f, 190, 2);
    hot(app, ctx, 1);
    nk_selectable_symbol_label(ctx, NK_SYMBOL_CIRCLE_SOLID, "With a symbol",
                               NK_TEXT_CENTERED, &s->sel_row);
    hot(app, ctx, 1);
    {
        /* Selected, the row is filled with the accent and the label switches
         * to whatever reads on it; the icon has to make the same move or it
         * is the one thing on the row that does not. */
        struct nk_color accent = curie_token("--links", ctx->style.text.color);
        struct nk_color ink = s->toggle
            ? curie_on(accent)
            : curie_token("--text-muted", ctx->style.text.color);

        nk_selectable_image_label(ctx,
            curie_ionicon_col(app, "star-outline", 16, ink),
            "With an image", NK_TEXT_CENTERED, &s->toggle);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- inputs -------------------------------------------------------------- */

static void
page_inputs(App *app, struct nk_context *ctx, showcase_state *s)
{
    static const char *const sizes[] = { "Compact", "Comfortable", "Spacious" };
    char line[96];

    section(app, ctx, "Text",
            "One rule - tiny.css's `input` - supplies the fill, the radius, "
            "the padding and the border; `input:focus` supplies the focus "
            "colour, which Nuklear can only apply to every state at once "
            "because its edit style carries a single border colour. A filter "
            "rejects a keystroke before it reaches the buffer, so the field "
            "cannot hold a value it would have to validate later.");
    api(app, ctx, "nk_edit_string with nk_filter_default / _decimal / _hex");

    nk_layout_row_dynamic(ctx, 36.0f, 1);
    curie_field(app, ctx, NK_EDIT_FIELD, s->name, &s->name_len,
                SC_TEXT_CAP, "Full name", nk_filter_default);

    nk_layout_row_dynamic(ctx, 36.0f, 2);
    curie_field(app, ctx, NK_EDIT_FIELD, s->digits, &s->digits_len,
                SC_TEXT_CAP, "Digits only", nk_filter_decimal);
    curie_field(app, ctx, NK_EDIT_FIELD, s->hex, &s->hex_len,
                SC_TEXT_CAP, "Hex only", nk_filter_hex);

    api(app, ctx, "nk_edit_string with NK_EDIT_BOX "
                  "(Nuklear breaks lines on newlines only - there is no wrap)");
    nk_layout_row_dynamic(ctx, 92.0f, 1);
    curie_field(app, ctx, NK_EDIT_BOX, s->note, &s->note_len,
                SC_BOX_CAP, NULL, nk_filter_default);

    section(app, ctx, "Ranges",
            "A slider steps a value between two bounds; a progress bar is the "
            "same shape but reports rather than accepts - unless it is made "
            "modifiable, which is the fourth argument. A knob is a slider "
            "wrapped around a circle, and is the one widget here with no "
            "counterpart in CSS at all.");
    api(app, ctx, "nk_slider_float  /  nk_slider_int  /  nk_progress  /  "
                  "nk_knob_float");

    nk_layout_row_dynamic(ctx, ROW, 2);
    hot(app, ctx, 1);
    nk_slider_float(ctx, 0.0f, &s->slider_f, 1.0f, 0.01f);
    SDL_snprintf(line, sizeof(line), "%.2f", (double)s->slider_f);
    nk_label(ctx, line, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, ROW, 2);
    hot(app, ctx, 1);
    nk_slider_int(ctx, 0, &s->slider_i, 100, 1);
    SDL_snprintf(line, sizeof(line), "%d", s->slider_i);
    nk_label(ctx, line, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, ROW, 2);
    hot(app, ctx, 1);
    nk_progress(ctx, &s->progress, 100, NK_MODIFIABLE);
    nk_label(ctx, "modifiable - drag it", NK_TEXT_LEFT);

    nk_layout_row_static(ctx, 62.0f, 62, 2);
    hot(app, ctx, 1);
    nk_knob_float(ctx, 0.0f, &s->knob, 1.0f, 0.01f, NK_DOWN, 0.0f);
    nk_spacer(ctx);

    section(app, ctx, "Properties",
            "A property is a labelled number that can be dragged, clicked "
            "through its two steppers, or typed into - the three ways a "
            "person expects to change a number, in one widget.");
    api(app, ctx, "nk_property_int  /  nk_property_float  /  "
                  "nk_property_double");

    nk_layout_row_dynamic(ctx, ROW, 3);
    hot(app, ctx, 1);
    nk_property_int(ctx, "Columns:", 1, &s->prop_i, 24, 1, 0.25f);
    hot(app, ctx, 1);
    nk_property_float(ctx, "Stroke:", 0.25f, &s->prop_f, 8.0f, 0.05f, 0.01f);
    hot(app, ctx, 1);
    nk_property_double(ctx, "Ratio:", 0.0, &s->prop_d, 100.0, 0.25, 0.05f);

    section(app, ctx, "Combo boxes",
            "nk_combo is the whole widget in one call, for the common case of "
            "picking a string out of an array. The begin/end form opens an "
            "empty popup instead and lets any layout go inside it, which is "
            "how a combo grows a colour picker or a set of sliders.");
    api(app, ctx, "nk_combo  /  nk_combo_begin_label  /  "
                  "nk_combo_begin_symbol_label");

    /* Static, not dynamic: two dynamic columns split the content width in
     * half and land the widgets on fractional x, where Nuklear's antialiased
     * rect stroke puts 1px down one edge and 2px down the other. A whole
     * number of pixels makes the frame even. */
    popup_style_push(ctx);
    nk_layout_row_static(ctx, ROW_TALL, 440, 2);
    hot(app, ctx, 1);
    {
        /* The popup takes an explicit size, so "as wide as the box" has to
         * be asked for - nk_widget_width is the box that is about to be
         * emitted. */
        struct nk_rect h = nk_widget_bounds(ctx);
        float cw = nk_widget_width(ctx);
        s->combo_size = nk_combo(ctx, sizes, 3, s->combo_size, 26,
                                 nk_vec2(cw, 130.0f));
        combo_chrome(app, ctx, h, 2.0f);
    }

    /* nk_combo_begin_symbol_text sizes the leading glyph as a square of the
     * row's height less twice combo.content_padding.y - so at the 4px the
     * `select` rule asks for, a 34px row gives a 26px circle. The padding is
     * the only handle on it. */
    nk_style_push_vec2(ctx, &ctx->style.combo.content_padding,
                       nk_vec2(ctx->style.combo.content_padding.x, 9.0f));
    hot(app, ctx, 1);
    {
        struct nk_rect h = nk_widget_bounds(ctx);

        if (nk_combo_begin_symbol_label(ctx, sizes[s->combo_size],
                                        NK_SYMBOL_CIRCLE_SOLID,
                                        nk_vec2(nk_widget_width(ctx),
                                                130.0f))) {
            int i;
            nk_layout_row_dynamic(ctx, 26.0f, 1);
            for (i = 0; i < 3; i++) {
                curie_hot_top(app, nk_widget_bounds(ctx), 1, 1);
                if (nk_combo_item_label(ctx, sizes[i], NK_TEXT_LEFT))
                    s->combo_size = i;
            }
            nk_combo_end(ctx);
        }
        combo_chrome(app, ctx, h, 2.0f);
    }
    nk_style_pop_vec2(ctx);

    nk_layout_row_static(ctx, ROW_TALL, 440, 2);
    hot(app, ctx, 1);
    {
        /* nk_combo_begin_label with an empty label, and the swatch painted
         * here.
         *
         * nk_combo_begin_color would be the obvious call, and it draws its
         * swatch with nk_fill_rect(..., 0, ...) - the rounding is a literal
         * zero in Nuklear, not a style field - so a square block sits inside
         * a box with an 8px radius. There is no way to ask it for anything
         * else, so the swatch is ours. */
        struct nk_rect h = nk_widget_bounds(ctx);
        float r = ctx->style.combo.rounding;
        struct nk_rect sw = combo_content(ctx, h);

        if (nk_combo_begin_label(ctx, "",
                                 nk_vec2(nk_widget_width(ctx), 150.0f))) {
            nk_layout_row_dynamic(ctx, 26.0f, 1);
            nk_property_float(ctx, "R:", 0.0f, &s->tint.r, 1.0f, 0.01f, 0.005f);
            nk_property_float(ctx, "G:", 0.0f, &s->tint.g, 1.0f, 0.01f, 0.005f);
            nk_property_float(ctx, "B:", 0.0f, &s->tint.b, 1.0f, 0.01f, 0.005f);
            nk_combo_end(ctx);
        }
        nk_fill_rect(nk_window_get_canvas(ctx), sw,
                     r > sw.h * 0.5f ? sw.h * 0.5f : r, nk_rgb_cf(s->tint));
        combo_chrome(app, ctx, h, 2.0f);
    }

    hot(app, ctx, 1);
    {
        struct nk_rect h = nk_widget_bounds(ctx);

        if (nk_combo_begin_label(ctx, "Anything at all",
                                 nk_vec2(nk_widget_width(ctx), 130.0f))) {
            nk_layout_row_dynamic(ctx, 26.0f, 1);
            nk_label(ctx, "A combo is just a popup", NK_TEXT_LEFT);
            nk_slider_float(ctx, 0.0f, &s->slider_f, 1.0f, 0.01f);
            nk_checkbox_label(ctx, "with a layout in it", &s->check_spell);
            nk_combo_end(ctx);
        }
        combo_chrome(app, ctx, h, 2.0f);
    }
    popup_style_pop(ctx);

    section(app, ctx, "Colour picker",
            "The one widget Nuklear draws as a continuous field rather than "
            "from the style: a saturation-value square with a hue bar. Its "
            "value is an nk_colorf, which is what the swatch and the combo "
            "above are reading.");
    api(app, ctx, "nk_color_pick");

    nk_layout_row_static(ctx, 132.0f, 210, 2);
    hot(app, ctx, 1);
    nk_color_pick(ctx, &s->tint, NK_RGB);
    if (nk_group_begin(ctx, "swatch", NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_color c = nk_rgb_cf(s->tint);
        nk_layout_row_dynamic(ctx, 44.0f, 1);
        nk_button_color(ctx, c);
        nk_layout_row_dynamic(ctx, 22.0f, 1);
        SDL_snprintf(line, sizeof(line), "#%02x%02x%02x", c.r, c.g, c.b);
        nk_label(ctx, line, NK_TEXT_CENTERED);
        nk_group_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- a data grid ---------------------------------------------------------
 *
 * Nuklear has no table widget at all, and tiny.css has a full set of table
 * rules - so this is the one place where the missing widget is assembled out
 * of rows and *every* colour in it still comes from a selector rather than a
 * palette token: `thead` for the header, `tbody tr` and its :nth-child(2n)
 * sibling for the zebra, `th, td` for the cell padding.
 *
 * The row background is painted onto the canvas before the cells are emitted,
 * because a row is not a widget and has no bounds of its own. The first
 * cell's bounds give the top and the height; the width is the content region,
 * which is exactly what the row spans. */
static void
grid_row(App *app, struct nk_context *ctx, const char *const *cells, int n,
         const float *weights, struct nk_color bg, struct nk_color fg,
         int header, float pad)
{
    struct nk_rect first;
    int i;

    nk_layout_row_begin(ctx, NK_DYNAMIC, 26.0f, n);
    nk_layout_row_push(ctx, weights[0]);
    first = nk_widget_bounds(ctx);

    if (bg.a) {
        nk_fill_rect(nk_window_get_canvas(ctx),
                     nk_rect(first.x, first.y, content_w(ctx), first.h),
                     0.0f, bg);
    }

    nk_style_push_font(ctx, curie_font(app, 13, header));
    nk_style_push_color(ctx, &ctx->style.text.color, fg);
    nk_style_push_vec2(ctx, &ctx->style.text.padding, nk_vec2(pad, 0.0f));
    for (i = 0; i < n; i++) {
        if (i) nk_layout_row_push(ctx, weights[i]);
        nk_label(ctx, cells[i], NK_TEXT_LEFT);
    }
    nk_style_pop_vec2(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_font(ctx);
    nk_layout_row_end(ctx);
}

static void
section_grid(App *app, struct nk_context *ctx)
{
    /* What the app is actually built out of, which makes the table worth
     * reading as well as worth looking at. */
    static const char *const head[3] = { "Submodule", "Licence", "What it does" };
    static const char *const rows[7][3] = {
        { "SDL3",      "Zlib",   "window, input, renderer, main loop" },
        { "Nuklear",   "MIT/PD", "the widgets and their layout" },
        { "LCUI css",  "MIT",    "parses the stylesheet, computes values" },
        { "tiny.css",  "MIT",    "the stylesheet itself" },
        { "plutosvg",  "MIT",    "rasterises the icons" },
        { "Ionicons",  "MIT",    "the icons" },
        { "Aileron",   "CC0",    "the typeface" }
    };
    static const float weights[3] = { 0.24f, 0.16f, 0.60f };

    curie_style tbl, head_s, cell;
    struct nk_color odd, even, head_bg, head_fg;
    float pad;
    int i;

    section(app, ctx, "Tables",
            "The only widget on these pages that Nuklear does not have - and "
            "the only one whose every colour comes from a rule rather than a "
            "token, because a stylesheet does have tables. thead, tbody tr, "
            "its :nth-child(2n) sibling and the padding on th, td are all "
            "read straight out of tiny.css.");
    api(app, ctx, "nk_layout_row_begin(NK_DYNAMIC) + nk_fill_rect per row");

    curie_style_get("table", &tbl);
    curie_style_get("thead", &head_s);
    curie_style_get("th", &cell);

    odd     = curie_token("--table-bg", nk_rgb(53, 53, 53));
    even    = curie_token("--table-bg-alt", nk_rgb(37, 37, 37));

    {
        /* thead asks for --table, and in the light palette that is #0070E0 -
         * the same value as --links. The dark palette aliases it through
         * --table-bg-alt all the way back to --background-body, which is both
         * the page's colour *and*, via --table-bg, the odd rows' colour: the
         * header came out identical to half the table.
         *
         * So the fallback is --links rather than a grey step. It is the
         * colour the stylesheet itself uses for this in the theme where the
         * chain resolves, which makes it the sheet's own intent rather than
         * ours. */
        struct nk_color page = curie_token("--background-body", nk_rgb(37, 37, 37));
        struct nk_color accent = curie_token("--links", nk_rgb(0, 112, 224));
        struct nk_color want = head_s.matched && head_s.bg[3]
                             ? curie_col(head_s.bg)
                             : curie_token("--table", accent);

        head_bg = curie_visible(want, page, accent);
        head_bg = curie_visible(head_bg, odd, accent);
        head_fg = curie_on(head_bg);
    }
    pad     = cell.matched && cell.pad_x > 0.0f ? cell.pad_x : 8.0f;

    grid_row(app, ctx, head, 3, weights, head_bg, head_fg, 1, pad);
    for (i = 0; i < 7; i++) {
        grid_row(app, ctx, rows[i], 3, weights,
                 (i & 1) ? even : odd, ctx->style.text.color, 0, pad);
    }
    (void)tbl;
}

/* --- display ------------------------------------------------------------- */

static void
page_display(App *app, struct nk_context *ctx, showcase_state *s)
{
    struct nk_list_view view;
    int i;

    section(app, ctx, "Text",
            "Alignment is a flag, not a layout: the label fills the row it "
            "was given and puts the text where the flag says. Wrapping is a "
            "separate call, because a wrapped label has to be given its "
            "height in advance - Nuklear cannot grow the row after the fact.");
    api(app, ctx, "nk_label  /  nk_label_colored  /  nk_label_wrap  /  nk_text");

    nk_layout_row_dynamic(ctx, ROW_SMALL, 3);
    nk_label(ctx, "left", NK_TEXT_LEFT);
    nk_label(ctx, "centered", NK_TEXT_CENTERED);
    nk_label(ctx, "right", NK_TEXT_RIGHT);

    nk_layout_row_dynamic(ctx, ROW_SMALL, 3);
    nk_label_colored(ctx, "--text-main", NK_TEXT_LEFT,
                     curie_token("--text-main", ctx->style.text.color));
    nk_label_colored(ctx, "--text-muted", NK_TEXT_CENTERED,
                     curie_token("--text-muted", ctx->style.text.color));
    nk_label_colored(ctx, "--links", NK_TEXT_RIGHT,
                     curie_token("--links", ctx->style.text.color));

    nk_layout_row_dynamic(ctx, 40.0f, 1);
    nk_label_wrap(ctx, "nk_label_wrap breaks on words inside the row it was "
                       "given, which is why the paragraphs on these pages "
                       "measure their own height first.");

    section(app, ctx, "Images",
            "Every icon here is an SVG read out of the Ionicons submodule and "
            "rasterised at the size it is drawn, twice over for the "
            "downscale. The stroke colour is substituted into the file before "
            "it is parsed, which is how an icon follows the stylesheet - CSS "
            "cannot reach inside an SVG.");
    api(app, ctx, "nk_image");

    {
        static const char *const names[8] = {
            "home-outline", "search-outline", "settings-outline",
            "heart-outline", "cloud-outline", "calendar-outline",
            "mail-outline", "lock-closed-outline"
        };
        int k;

        nk_layout_row_static(ctx, 30.0f, 30, 8);
        for (k = 0; k < 8; k++)
            curie_image(app, ctx, curie_ionicon(app, names[k], 22), 22);
    }

    section(app, ctx, "Charts",
            "A chart is pushed one value at a time and reports back whether "
            "each was hovered or clicked, so the data never has to be copied "
            "anywhere first. Slots stack several series in one frame; nk_plot "
            "is the same thing for an array you already have.");
    api(app, ctx, "nk_chart_begin / nk_chart_push  /  nk_chart_add_slot  /  "
                  "nk_plot");

    nk_layout_row_dynamic(ctx, 100.0f, 1);
    if (nk_chart_begin(ctx, NK_CHART_LINES, SC_SERIES_N, -1.1f, 1.1f)) {
        for (i = 0; i < SC_SERIES_N; i++) nk_chart_push(ctx, s->series[i]);
        nk_chart_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 100.0f, 1);
    if (nk_chart_begin(ctx, NK_CHART_COLUMN, SC_SERIES_N, -1.1f, 1.1f)) {
        nk_chart_add_slot(ctx, NK_CHART_LINES, SC_SERIES_N, -1.1f, 1.1f);
        for (i = 0; i < SC_SERIES_N; i++) {
            nk_chart_push_slot(ctx, s->series[i], 0);
            nk_chart_push_slot(ctx, s->series[SC_SERIES_N - 1 - i], 1);
        }
        nk_chart_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 70.0f, 1);
    nk_plot(ctx, NK_CHART_LINES, s->series, SC_SERIES_N, 0);

    section(app, ctx, "Progress and rules",
            "A fixed progress bar reports; nk_rule_horizontal is the "
            "separator every page on this tab is divided by, and takes its "
            "colour as an argument rather than from the style.");
    api(app, ctx, "nk_progress with NK_FIXED  /  nk_rule_horizontal");

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    {
        nk_size fixed = s->progress;
        nk_progress(ctx, &fixed, 100, NK_FIXED);
    }

    section_grid(app, ctx);

    section(app, ctx, "Trees and lists",
            "A tree remembers whether it is open between frames without the "
            "caller holding a bool, by hashing the source line it was written "
            "on. nk_tree_element makes the header itself selectable, drawn "
            "through nk_style_selectable - so an unselected one shows nothing "
            "but its label, and a selected one fills with the accent. A list "
            "view emits only the rows actually on screen, so its cost does "
            "not grow with the data.");
    api(app, ctx, "nk_tree_push  /  nk_tree_element_push  /  "
                  "nk_list_view_begin");

    hot(app, ctx, 1);
    if (nk_tree_push(ctx, NK_TREE_TAB, "A tab-style tree", NK_MAXIMIZED)) {
        nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
        nk_label(ctx, "Its children are indented under it.", NK_TEXT_LEFT);
        hot(app, ctx, 1);
        if (nk_tree_push(ctx, NK_TREE_NODE, "A node inside it", NK_MINIMIZED)) {
            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            nk_label(ctx, "Nesting is unlimited.", NK_TEXT_LEFT);
            nk_tree_pop(ctx);
        }
        for (i = 0; i < 3; i++) {
            char lab[32];
            SDL_snprintf(lab, sizeof(lab), "Selectable branch %d", i + 1);
            hot(app, ctx, 1);
            if (nk_tree_element_push(ctx, NK_TREE_NODE, lab, NK_MINIMIZED,
                                     &s->tree_leaf[i])) {
                nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
                nk_label(ctx, "with a checkbox in the header",
                         NK_TEXT_LEFT);
                nk_tree_pop(ctx);
            }
        }
        nk_tree_pop(ctx);
    }

    nk_layout_row_dynamic(ctx, 10.0f, 1);
    nk_spacer(ctx);

    /* The row height handed to nk_list_view_begin is what it uses to decide
     * which rows are on screen and how far the content scrolls - so the rows
     * emitted inside have to occupy exactly that, spacing included. They were
     * 22px rows with 9px of spacing against a declared 24, which is why they
     * drifted apart and the last one was sliced in half. */
    nk_layout_row_dynamic(ctx, 152.0f, 1);
    nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(0.0f, 0.0f));
    if (nk_list_view_begin(ctx, &view, "rows", NK_WINDOW_BORDER, 24,
                           SC_LIST_N)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        for (i = view.begin; i < view.end; i++) {
            char lab[40];
            nk_bool on = (s->list_sel == i);
            SDL_snprintf(lab, sizeof(lab), "Row %d of %d", i + 1, SC_LIST_N);
            hot(app, ctx, 1);
            if (nk_selectable_label(ctx, lab, NK_TEXT_LEFT, &on) && on)
                s->list_sel = i;
        }
        nk_list_view_end(&view);
    }
    nk_style_pop_vec2(ctx);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- layout -------------------------------------------------------------- */

static void
page_layout(App *app, struct nk_context *ctx, showcase_state *s)
{
    int i;

    (void)s;

    section(app, ctx, "Rows",
            "Dynamic rows split the width into equal columns and follow a "
            "resize. Static rows are given a pixel width per item and do "
            "not. Both are one call for the whole row, which is what keeps "
            "immediate mode from turning into layout bookkeeping.");
    api(app, ctx, "nk_layout_row_dynamic  /  nk_layout_row_static");

    nk_layout_row_dynamic(ctx, ROW, 1);
    demo_button(app, ctx, "one column, dynamic");
    nk_layout_row_dynamic(ctx, ROW, 3);
    for (i = 0; i < 3; i++) demo_button(app, ctx, "third");
    nk_layout_row_static(ctx, ROW, 90, 4);
    for (i = 0; i < 4; i++) demo_button(app, ctx, "90px");

    section(app, ctx, "Grids",
            "A grid is a static row repeated: the row API wraps to the next "
            "line once the items no longer fit, so one call and a loop give a "
            "grid of any size without any arithmetic here.");
    api(app, ctx, "nk_layout_row_static, repeated");

    nk_layout_row_static(ctx, 46.0f, 110, 6);
    for (i = 0; i < 12; i++) {
        char lab[16];
        SDL_snprintf(lab, sizeof(lab), "%d,%d", i / 6 + 1, i % 6 + 1);
        demo_button(app, ctx, lab);
    }

    section(app, ctx, "Per-column widths",
            "The push form gives each column its own width, either in pixels "
            "(NK_STATIC) or as a fraction of the row (NK_DYNAMIC). It is what "
            "the titlebar above uses to put the controls flush against the "
            "right edge.");
    api(app, ctx, "nk_layout_row_begin / nk_layout_row_push");

    nk_layout_row_begin(ctx, NK_STATIC, ROW, 3);
    nk_layout_row_push(ctx, 60.0f);  demo_button(app, ctx, "60");
    nk_layout_row_push(ctx, 140.0f); demo_button(app, ctx, "140");
    nk_layout_row_push(ctx, 220.0f); demo_button(app, ctx, "220");
    nk_layout_row_end(ctx);

    nk_layout_row_begin(ctx, NK_DYNAMIC, ROW, 3);
    nk_layout_row_push(ctx, 0.2f); demo_button(app, ctx, "20%");
    nk_layout_row_push(ctx, 0.5f); demo_button(app, ctx, "50%");
    nk_layout_row_push(ctx, 0.3f); demo_button(app, ctx, "30%");
    nk_layout_row_end(ctx);

    section(app, ctx, "Templates",
            "A template mixes the two: a fixed column keeps its width, a "
            "variable one has a floor and grows, and a dynamic one takes "
            "whatever is left. Resize the window and only the last two move.");
    api(app, ctx, "nk_layout_row_template_push_static / _variable / _dynamic");

    nk_layout_row_template_begin(ctx, ROW);
    nk_layout_row_template_push_static(ctx, 80.0f);
    nk_layout_row_template_push_variable(ctx, 80.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    demo_button(app, ctx, "static 80");
    demo_button(app, ctx, "variable");
    demo_button(app, ctx, "dynamic");

    section(app, ctx, "Absolute placement",
            "nk_layout_space takes a rect per widget and stacks them in the "
            "order they are emitted - the escape hatch for the cases a row "
            "cannot express, such as the login card being centred in the "
            "window on the first tab.");
    api(app, ctx, "nk_layout_space_begin / nk_layout_space_push");

    nk_layout_space_begin(ctx, NK_STATIC, 110.0f, 4);
    nk_layout_space_push(ctx, nk_rect(0.0f, 0.0f, 130.0f, 44.0f));
    demo_button(app, ctx, "0, 0");
    nk_layout_space_push(ctx, nk_rect(60.0f, 26.0f, 130.0f, 44.0f));
    demo_button(app, ctx, "60, 26");
    nk_layout_space_push(ctx, nk_rect(120.0f, 52.0f, 130.0f, 44.0f));
    demo_button(app, ctx, "120, 52");
    nk_layout_space_push(ctx, nk_rect(300.0f, 10.0f, 160.0f, 86.0f));
    demo_button(app, ctx, "and anywhere");
    nk_layout_space_end(ctx);

    section(app, ctx, "Groups",
            "A group is a window inside a window: it clips, it scrolls, and "
            "it starts a fresh layout. The card on the login tab, this page's "
            "body and the titlebar are all groups.");
    api(app, ctx, "nk_group_begin  /  nk_group_begin_titled");

    nk_layout_row_dynamic(ctx, 150.0f, 2);
    if (nk_group_begin(ctx, "plain", NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        for (i = 0; i < 12; i++) {
            char lab[24];
            SDL_snprintf(lab, sizeof(lab), "scrolls: item %d", i + 1);
            nk_label(ctx, lab, NK_TEXT_LEFT);
        }
        nk_group_end(ctx);
    }
    if (nk_group_begin_titled(ctx, "titled", "With a title",
                              NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_label(ctx, "NK_WINDOW_TITLE", NK_TEXT_LEFT);
        nk_label(ctx, "draws the header", NK_TEXT_LEFT);
        nk_group_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- popups -------------------------------------------------------------- */

static void
page_popups(App *app, struct nk_context *ctx, showcase_state *s)
{
    char line[96];

    /* Before anything else on this page. nk_menubar_begin pins the rows
     * emitted inside it to the top of the group and moves the content region
     * down past them, and it can only do that while the region is still
     * whole - called after a heading, the bar drew straight over everything
     * above it. */
    /* In a group of its own rather than as the page's menu bar.
     *
     * nk_menubar_begin pins its row to the top of the panel it is in and
     * keeps it out of the scroll - which is exactly right for an application
     * window and wrong here, because this page scrolls: the bar stayed put
     * and the headings slid underneath it. A group is a panel, so the bar
     * still behaves like a menu bar, and it travels with the section it
     * belongs to. */
    popup_style_push(ctx);
    nk_layout_row_dynamic(ctx, 40.0f, 1);
    /* nk_group_begin answers 0 when the group is scrolled out of view, which
     * is a reason to skip the bar and nothing else - it was bailing out of
     * the whole page, so scrolling past the bar blanked everything below. */
    if (nk_group_begin(ctx, "menubar", NK_WINDOW_BORDER |
                                       NK_WINDOW_NO_SCROLLBAR)) {
    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, 26.0f, 3);
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, 1);
    if (nk_menu_begin_label(ctx, "File", NK_TEXT_LEFT,
                            nk_vec2(150.0f, MENU_H(3)))) {
        menu_rows_push(ctx);
        nk_layout_row_dynamic(ctx, MENU_ROW, 1);
        if (item_with_icon(app, ctx, "New", "add-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > New", sizeof(s->menu_pick));
        if (item_with_icon(app, ctx, "Open", "folder-open-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > Open", sizeof(s->menu_pick));
        if (item_with_icon(app, ctx, "Close", "close-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > Close", sizeof(s->menu_pick));
        menu_rows_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, 1);
    if (nk_menu_begin_label(ctx, "Edit", NK_TEXT_LEFT,
                            nk_vec2(190.0f, MENU_H(4)))) {
        menu_rows_push(ctx);
        nk_layout_row_dynamic(ctx, MENU_ROW, 1);
        if (menu_item(app, ctx, "Cut", 0))
            SDL_strlcpy(s->menu_pick, "Edit > Cut", sizeof(s->menu_pick));
        if (menu_item(app, ctx, "Copy", 0))
            SDL_strlcpy(s->menu_pick, "Edit > Copy", sizeof(s->menu_pick));
        /* NK_WIDGET_RIGHT puts the box at the right of the row with the
         * label at the left - the arrangement the icons above use. It sits
         * flush against the edge of the widget it is given (select.x is
         * r.x + r.w - font->height, with no padding of its own), so the clear
         * space has to come from the row: a trailing spacer column. */
        /* A menu item's text starts at padding + border + rounding inside
         * its row - nk_do_button subtracts all three - so a checkbox laid out
         * against the raw row would sit that much further left than the items
         * above it. The leading spacer puts its label on the same line. */
        nk_layout_row_template_begin(ctx, MENU_ROW);
        nk_layout_row_template_push_static(ctx, 5.0f);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 6.0f);
        nk_layout_row_template_end(ctx);
        nk_spacer(ctx);
        nk_checkbox_label_align(ctx, "Overwrite", &s->check_spell,
                                NK_WIDGET_RIGHT, NK_TEXT_LEFT);
        nk_spacer(ctx);
        nk_layout_row_dynamic(ctx, MENU_ROW, 1);
        nk_slider_float(ctx, 0.0f, &s->slider_f, 1.0f, 0.01f);
        menu_rows_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, 1);
    if (nk_menu_begin_label(ctx, "View", NK_TEXT_LEFT,
                            nk_vec2(170.0f, MENU_H(2)))) {
        menu_rows_push(ctx);
        nk_layout_row_dynamic(ctx, MENU_ROW, 1);
        nk_progress(ctx, &s->progress, 100, NK_MODIFIABLE);
        if (menu_item(app, ctx, "Reset", 0))
            s->progress = 50;
        menu_rows_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
    nk_group_end(ctx);
    }
    popup_style_pop(ctx);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
    heading(app, ctx, "Menus");
    caption(app, ctx,
            "The bar above. A menu is a popup anchored to the item that "
            "opened it, and its items are ordinary widgets - the View menu "
            "has a draggable progress bar in it. An item's glyph and the "
            "Edit menu's checkbox both sit at the right edge: the checkbox "
            "through nk_checkbox_label_align, the glyph painted there, "
            "because nk_menu_item_symbol_label centres its label whatever "
            "alignment it is given.");
    api(app, ctx, "nk_menubar_begin  /  nk_menu_begin_label  /  "
                  "nk_menu_item_label  /  nk_checkbox_label_align");

    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "last chosen: %s", s->menu_pick);
    nk_style_push_font(ctx, curie_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     curie_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);

    section(app, ctx, "Context menu",
            "The same popup, opened by a right-click inside a rect the caller "
            "names. The login field on the first tab uses one for cut, copy "
            "and paste.");
    api(app, ctx, "nk_contextual_begin");

    popup_style_push(ctx);
    nk_layout_row_dynamic(ctx, 54.0f, 1);
    {
        struct nk_rect trigger = nk_widget_bounds(ctx);
        hot(app, ctx, 1);
        nk_button_label(ctx, "Right-click anywhere on this button");
        /* The same width and the same three-row height as the File menu
         * above it: a context menu is the same widget, and two popups of
         * different sizes on one page read as an oversight. */
        if (nk_contextual_begin(ctx, 0, nk_vec2(150.0f, MENU_H(3)),
                                trigger)) {
            menu_rows_push(ctx);
            nk_layout_row_dynamic(ctx, MENU_ROW, 1);
            if (item_with_icon(app, ctx, "First", "star-outline", 1))
                SDL_strlcpy(s->menu_pick, "context: First",
                            sizeof(s->menu_pick));
            if (item_with_icon(app, ctx, "Second", "bookmark-outline", 1))
                SDL_strlcpy(s->menu_pick, "context: Second",
                            sizeof(s->menu_pick));
            if (item_with_icon(app, ctx, "Third", "refresh-outline",
                               1))
                SDL_strlcpy(s->menu_pick, "context: Third",
                            sizeof(s->menu_pick));
            menu_rows_pop(ctx);
            nk_contextual_end(ctx);
        }
    }
    popup_style_pop(ctx);

    section(app, ctx, "Tooltips",
            "Positioned against the pointer rather than against a widget, "
            "and emitted only while the thing it describes is hovered - so it "
            "costs nothing on the frames where it is not showing. Drawn here "
            "rather than with nk_tooltip: that opens a NK_POPUP_DYNAMIC "
            "panel, which does not fill its body when it begins and strokes "
            "its border at bounds sized before the content shrank them, "
            "leaving a frame standing off the box that no style value "
            "moves.");
    api(app, ctx, "drawn onto the canvas - see draw_tooltip");

    /* Tested against the button's own rect rather than with
     * nk_widget_is_hovered, which peeks at whatever the layout is about to
     * emit next - and emitting the tooltip first changed what that was, so
     * the rect being tested was not the button's. The whole button raises the
     * tooltip now, not the words on it. */
    popup_style_push(ctx);
    nk_layout_row_dynamic(ctx, 34.0f, 2);
    {
        static const char *const one[1] = { "A one-line tooltip." };
        struct nk_rect b = nk_widget_bounds(ctx);

        curie_hot_follow(app, b, 1);
        nk_button_label(ctx, "Hover for a plain tooltip");
        if (nk_input_is_mouse_hovering_rect(&ctx->input, b))
            draw_tooltip(app, ctx, one, 1, -1.0f);
    }

    {
        static const char *const many[2] = {
            "A tooltip is not limited to a line:",
            "this one carries a progress bar."
        };
        struct nk_rect b = nk_widget_bounds(ctx);

        curie_hot_follow(app, b, 1);
        nk_button_label(ctx, "Hover for a laid-out one");
        if (nk_input_is_mouse_hovering_rect(&ctx->input, b))
            draw_tooltip(app, ctx, many, 2, (float)s->progress / 100.0f);
    }
    popup_style_pop(ctx);

    section(app, ctx, "Popups",
            "A static popup is a fixed rect that stays until it is closed - a "
            "dialog. A dynamic one is sized by its contents. Both hold the "
            "input while they are open, which is what makes the first modal "
            "without any modality machinery.");
    api(app, ctx, "nk_popup_begin(NK_POPUP_STATIC)  /  nk_popup_close");

    nk_layout_row_template_begin(ctx, 34.0f);
    nk_layout_row_template_push_static(ctx, 240.0f);
    nk_layout_row_template_push_static(ctx, 14.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    if (curie_button(app, ctx, "Open a dialog")) s->popup_open = 1;
    nk_spacer(ctx);
    nk_label(ctx, s->popup_open ? "open" : "closed", NK_TEXT_LEFT);

    popup_style_push(ctx);
    if (s->popup_open) {
        /* A popup's rect is relative to the parent panel's clip, so centring
         * it means halving the visible region rather than the page - which
         * on a scrolled page are not the same thing. */
        /* Sized to what is in it. A fixed rect with two lines of text in it
         * leaves a bare half-panel underneath, which is what a dialog looks
         * like when nobody counted the rows. */
        struct nk_vec2 vis = nk_window_get_content_region_size(ctx);
        float pw = 360.0f;
        /* Five rows, the four gaps between them, and the panel's own
         * padding either side. Counted rather than guessed - the first guess
         * left the buttons under the bottom edge with a scrollbar beside
         * them. */
        float ph = (22.0f + 8.0f + 44.0f + 12.0f + 36.0f)   /* rows  */
                 + 4.0f * 2.0f                              /* gaps  */
                 + 2.0f * 12.0f;                            /* frame */
        struct nk_rect at = nk_rect((vis.x - pw) * 0.5f, (vis.y - ph) * 0.5f,
                                    pw, ph);

        /* NO_SCROLLBAR: it is sized to its contents, so a scrollbar there
         * can only ever be a sliver down the edge from a rounding error. */
        if (nk_popup_begin(ctx, NK_POPUP_STATIC, "Confirm",
                           NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, at)) {
            menu_rows_push(ctx);
            nk_layout_row_dynamic(ctx, 22.0f, 1);
            nk_style_push_font(ctx, curie_font(app, 16, 1));
            nk_label_colored(ctx, "Close without saving?", NK_TEXT_LEFT,
                             curie_token("--text-bright",
                                         ctx->style.text.color));
            nk_style_pop_font(ctx);

            nk_layout_row_dynamic(ctx, 8.0f, 1);
            nk_spacer(ctx);

            nk_layout_row_dynamic(ctx, 44.0f, 1);
            nk_style_push_font(ctx, curie_font(app, 13, 0));
            nk_label_colored_wrap(ctx,
                "Everything behind this is inert until the popup is closed - "
                "which is what makes it modal, with no modality machinery.",
                curie_token("--text-muted", ctx->style.text.color));
            nk_style_pop_font(ctx);

            nk_layout_row_dynamic(ctx, 12.0f, 1);
            nk_spacer(ctx);

            /* Cancel then OK, right-aligned, which is where a dialog's
             * buttons live on every desktop this runs on. */
            nk_layout_row_template_begin(ctx, 36.0f);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 96.0f);
            nk_layout_row_template_push_static(ctx, 96.0f);
            nk_layout_row_template_end(ctx);
            nk_spacer(ctx);
            if (curie_button(app, ctx, "Cancel")) {
                s->popup_open = 0;
                nk_popup_close(ctx);
            }
            if (curie_button_accent(app, ctx, "OK")) {
                s->popup_open = 0;
                nk_popup_close(ctx);
            }
            menu_rows_pop(ctx);
            nk_popup_end(ctx);
        } else {
            s->popup_open = 0;
        }
    }
    popup_style_pop(ctx);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- diagnostics --------------------------------------------------------- */

/* One labelled reading. Two columns, so the values line up down the page
 * rather than wherever their labels happen to end. */
static void
diag_row(App *app, struct nk_context *ctx, const char *name, const char *value)
{
    nk_layout_row_template_begin(ctx, 24.0f);
    nk_layout_row_template_push_static(ctx, 190.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_style_push_font(ctx, curie_font(app, 13, 0));
    nk_label_colored(ctx, name, NK_TEXT_LEFT,
                     curie_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);
    nk_label(ctx, value, NK_TEXT_LEFT);
}

static void
page_diagnostics(App *app, struct nk_context *ctx, showcase_state *st)
{
    curie_diag d;
    char v[192];

    (void)st;
    curie_diagnostics(app, &d);

    section(app, ctx, "Rendering",
            "Which backend SDL settled on, and what the app asked for. "
            "CURIE_RENDERER picks between auto, gpu and software; "
            "CURIE_VSYNC and CURIE_AA turn the other two off.");

    diag_row(app, ctx, "backend", d.renderer);
    SDL_snprintf(v, sizeof(v), "%s", d.mode);
    diag_row(app, ctx, "requested", v);
    diag_row(app, ctx, "vsync", d.vsync ? "on" : "off");
    diag_row(app, ctx, "anti-aliasing", d.aa ? "on" : "off");

    section(app, ctx, "Pacing",
            "How often SDL calls the app back. At rest that is \"waitevent\" - "
            "nothing is drawn until something happens - and for the duration "
            "of a drag it becomes the display's refresh, so the frames come "
            "at an even cadence rather than one per mouse event. This page "
            "forces no frames of its own, so the readings below hold still "
            "while nothing is happening - which is the measurement. They come "
            "from the gap between the last two frames rather than from a "
            "count over a second, because a second only advances when a frame "
            "is drawn: counted that way the figure would read stale for as "
            "long as the app was quiet. Move the pointer across the page.");

    diag_row(app, ctx, "at rest", d.frame_rate);
    diag_row(app, ctx, "while dragging", d.drag_rate);
    /* Milliseconds are the natural unit for a frame, but a quiet app can sit
     * for minutes: past a second the number stops reading as a duration. */
    if (d.frame_gap_ms >= 1000.0f)
        SDL_snprintf(v, sizeof(v), "%.2f s since the last one",
                     (double)d.frame_gap_ms / 1000.0);
    else if (d.frame_gap_ms > 0.0f)
        SDL_snprintf(v, sizeof(v), "%.0f ms since the last one",
                     (double)d.frame_gap_ms);
    else SDL_strlcpy(v, "the first", sizeof(v));
    diag_row(app, ctx, "this frame", v);

    section(app, ctx, "Where a frame goes",
            "Split three ways because guessing which one dominates has been "
            "wrong more than once. Building the UI and converting it to "
            "geometry are both fractions of a millisecond; the wait on "
            "present is the whole frame.");

    SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.build_ms);
    diag_row(app, ctx, "build", v);
    SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.render_ms);
    diag_row(app, ctx, "render", v);
    SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.present_ms);
    diag_row(app, ctx, "present", v);

    section(app, ctx, "Style and text",
            "The stylesheets are parsed by LCUI's libcss on every theme "
            "change, and the font atlas is baked per size at the display's "
            "scale.");

    SDL_snprintf(v, sizeof(v), "%s", d.dark ? "dark" : "light");
    diag_row(app, ctx, "scheme", v);
    SDL_snprintf(v, sizeof(v), "%d, parsed in %.2f ms", d.sheets,
                 (double)d.style_ms);
    diag_row(app, ctx, "stylesheets", v);
    SDL_snprintf(v, sizeof(v), "%.2fx", (double)d.scale);
    diag_row(app, ctx, "display scale", v);
    diag_row(app, ctx, "font", d.font);

    section(app, ctx, "Memory",
            "Where the process's memory has gone. Nuklear's command buffer "
            "grows to fit the busiest frame it has been asked to draw and is "
            "never handed back, so it records the high-water mark rather than "
            "the current page; the icon cache holds every SVG rasterised so "
            "far, at twice the size it is drawn. The font atlas is one RGBA32 "
            "texture holding every baked size at once, and is the largest "
            "single allocation the app makes.");

    SDL_snprintf(v, sizeof(v), "%.0f KB reserved, %.0f KB used by this frame",
                 d.nk_bytes / 1024.0, d.nk_used / 1024.0);
    diag_row(app, ctx, "Nuklear buffer", v);
    SDL_snprintf(v, sizeof(v), "%.0f KB in %d rasters", d.icon_bytes / 1024.0,
                 d.icons);
    diag_row(app, ctx, "icon cache", v);
    SDL_snprintf(v, sizeof(v), "%d x %d, %.1f MB", d.atlas_w, d.atlas_h,
                 d.atlas_w * (double)d.atlas_h * 4.0 / 1048576.0);
    diag_row(app, ctx, "font atlas", v);
    SDL_snprintf(v, sizeof(v), "%.1f MB", d.rss_bytes / 1048576.0);
    diag_row(app, ctx, "process, resident", v);

    section(app, ctx, "What startup costs",
            "The resident set sampled at each step, and the difference each "
            "one made. Anything left over is what the C runtime, the graphics "
            "driver and the loader wanted before main ran.");

    {
        int i;

        SDL_snprintf(v, sizeof(v), "%.1f MB",
                     d.rss_at[RSS_ENTRY] / 1048576.0);
        diag_row(app, ctx, curie_rss_names[RSS_ENTRY], v);
        for (i = RSS_ENTRY + 1; i < RSS_STEPS; i++) {
            double delta = (d.rss_at[i] - (double)d.rss_at[i - 1]) / 1048576.0;
            SDL_snprintf(v, sizeof(v), "%+.1f MB   (%.1f MB total)", delta,
                         d.rss_at[i] / 1048576.0);
            diag_row(app, ctx, curie_rss_names[i], v);
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- dispatch ------------------------------------------------------------ */

void
curie_showcase_page(App *app, struct nk_context *ctx, int tab,
                    float w, float h)
{
    showcase_state *s = curie_showcase(app);

    (void)w; (void)h;
    if (!s->seeded) seed(s);

    /* Nuklear puts 4px between rows by default, which on a page this dense
     * ran the controls straight into the paragraph above them. */
    nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(8.0f, 9.0f));

    switch (tab) {
    case TAB_BUTTONS: page_buttons(app, ctx, s); break;
    case TAB_INPUTS:  page_inputs(app, ctx, s);  break;
    case TAB_DISPLAY: page_display(app, ctx, s); break;
    case TAB_LAYOUT:  page_layout(app, ctx, s);  break;
    case TAB_POPUPS:  page_popups(app, ctx, s);  break;
    case TAB_DIAG:    page_diagnostics(app, ctx, s); break;
    default: break;
    }

    nk_style_pop_vec2(ctx);
}
