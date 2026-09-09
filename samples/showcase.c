/* showcase.c - every Nuklear widget on screen, under the same stylesheet.
 *
 * The point is not a gallery for its own sake. tiny.css is classless: it has
 * rules for `button`, `input`, `select`, `textarea`, `table` and little else,
 * so a slider, a knob, a chart or a tree has no rule to read and must be
 * styled from the palette tokens instead. Drawing all of them is what shows
 * where that line falls - see reaktor_style_widgets() in main.c, which is the
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
#include "sample.h"
#include "style.h"

/* A menu row, and the height of a popup holding n of them: the rows, the
 * 2px popup_style_push puts between them, and the panel's own padding. */
#define MENU_ROW  22.0f
#define MENU_H(n) ((n) * MENU_ROW + ((n) - 1) * 2.0f + 10.0f)

#define ROW       30.0f    /* one control */
#define ROW_TALL  34.0f    /* one control with a label inside it */
#define ROW_SMALL 22.0f

const char *const reaktor_rss_names[RSS_STEPS] = {
    "before any of it", "SDL_Init(VIDEO)", "window and renderer",
    "window icon", "Nuklear context", "font atlas", "stylesheets"
};

const char *const reaktor_tab_names[TAB_COUNT] = {
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
    struct nk_color c = reaktor_token("--text-bright", ctx->style.text.color);

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    nk_style_push_font(ctx, reaktor_font(app, 19, 1));
    reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, text, 0);
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
    const struct nk_user_font *f = reaktor_font(app, 16, 0);
    struct nk_color c = reaktor_token("--text-muted", ctx->style.text.color);
    float avail = content_w(ctx) - 4.0f;
    float tw    = f->width(f->userdata, f->height, text, (int)strlen(text));
    int   lines = 1;

    if (avail > 1.0f && tw > avail) lines = (int)(tw / avail) + 2;

    nk_style_push_font(ctx, f);
    nk_layout_row_dynamic(ctx, (f->height + 3.0f) * (float)lines, 1);
    reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, text, 0);
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
    nk_rule_horizontal(ctx, reaktor_token("--background-hover",
                                          nk_rgba(128, 128, 128, 90)),
                       nk_false);
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
    /* The radius is left alone: an icon-only button should have the same
     * corner as a labelled one. Only the padding goes, because nk_do_button
     * insets its content by padding + border + rounding and hands what is
     * left to the glyph - so the room the larger radius takes has to come
     * from somewhere, and it is padding a glyph does not need. */
    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));
}

static void
compact_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
}

/* A small line naming the call being demonstrated, so the page reads as
 * documentation rather than as decoration. Deliberately below the caption:
 * it is a reference, not part of the prose. */
static void
api(App *app, struct nk_context *ctx, const char *text)
{
    nk_style_push_font(ctx, reaktor_font(app, 12, 0));
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, text, 0);
    nk_label_colored(ctx, text, NK_TEXT_LEFT,
                     reaktor_token("--links", ctx->style.text.color));
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
                                 reaktor_token("--background",
                                               nk_rgb(53, 53, 53))));
    nk_style_push_float(ctx, &ctx->style.window.rounding,
                        reaktor_popup_rounding());
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
    im = reaktor_ionicon(app, "chevron-down-outline", (int)px);
    nk_draw_image(canvas, r, &im, nk_rgb(255, 255, 255));
}

/* A chevron where Nuklear would have drawn one of its own. nk_draw_symbol's
 * chevron is two one-pixel lines corner to corner of whatever box it is
 * handed - thin, and half the size of the combo's - so tree headers and
 * property steppers hand Nuklear NK_SYMBOL_NONE and get the Ionicon here,
 * centred in the slot Nuklear sized. */
#define CHEVRON_PX 14

/* One Ionicon centred in `slot`, at exactly px and no resampling - which is
 * what keeps a rim a line rather than a smear. `sw` multiplies the stroke the
 * artwork declares; at zero the hairline rule decides, which is what a
 * chevron wants and twice what a rim does. */
static void
glyph_at(App *app, struct nk_context *ctx, struct nk_rect slot,
         const char *name, struct nk_color col, int px, float sw)
{
    struct nk_rect r;
    struct nk_image im;

    if (px < 1) return;
    im = reaktor_ionicon_exact(app, name, px, col, sw);
    /* A glyph the cache could not load has no handle, and nk_draw_image would
     * paint the null texture - a white quad, which is worse than nothing. */
    if (!im.handle.ptr) return;
    r.w = r.h = (float)px;
    r.x = slot.x + (slot.w - r.w) * 0.5f;
    r.y = slot.y + (slot.h - r.h) * 0.5f;
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
}

static void
chevron_at(App *app, struct nk_context *ctx, struct nk_rect slot,
           const char *name, struct nk_color col)
{
    struct nk_rect r;
    struct nk_image im = reaktor_ionicon_col(app, name, CHEVRON_PX, col);

    r.w = r.h = (float)CHEVRON_PX;
    r.x = slot.x + (slot.w - r.w) * 0.5f;
    r.y = slot.y + (slot.h - r.h) * 0.5f;
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
}

/* A circle is the one shape the software renderer cannot draw. Nuklear fills
 * one as a polygon and grades its rim with geometry a fraction of a pixel
 * wide; SDL's software rasteriser has no partial coverage, so that geometry
 * lands whole or not at all, and the pass that puts every vertex on the pixel
 * grid - right for a rect's edges, see nk_sdl_render_ex - quantises the arc
 * with it. A radio came out nineteen pixels across and seventeen high, with a
 * rim that jumped between five tones.
 *
 * A texture's alpha is blended per texel on both backends, so every circle on
 * a page is an Ionicon instead, drawn in the slot Nuklear sized. */
#define DISC_ROUND   "ellipse"
#define DISC_RING    "radio-button-off"
#define DISC_OUTLINE "ellipse-outline"

/* The knob is the accent and so is the fill it sits on the end of, so it
 * vanished into the bar. A shade off the fill tells them apart and keeps them
 * the same colour - and darker rather than lighter, which reads as the part
 * to take hold of on either scheme. */
#define KNOB_SHADE 0.16f

static struct nk_color
shaded(struct nk_color c, float amount)
{
    unsigned char rgba[4];

    rgba[0] = c.r; rgba[1] = c.g; rgba[2] = c.b; rgba[3] = c.a;
    reaktor_style_darken(rgba, amount);
    return nk_rgba(rgba[0], rgba[1], rgba[2], rgba[3]);
}

/* Nuklear draws none of a slider either. Its bar and fill are rounded rects
 * whose caps it steps through in whole pixels, and its knob is one more
 * nk_fill_circle - so the styles go transparent for the call, which keeps the
 * geometry, the drag and the value, and all three are drawn afterwards, with
 * the value the drag has just produced rather than the previous frame's.
 *
 * The rects are nk_do_slider's: the bounds inset by padding, a bar of
 * bar_height centred in it, as much of it filled as the value, and the knob a
 * cursor_size square on the same centre line. */
static void
slider_cell(App *app, struct nk_context *ctx, unsigned id, float *val,
            float lo, float hi, float step)
{
    const struct nk_style_slider *st = &ctx->style.slider;
    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_rect b = nk_widget_bounds(ctx);
    int hot = nk_input_is_mouse_hovering_rect(&ctx->input, b);
    const struct nk_style_item *ci = hot ? &st->cursor_hover
                                         : &st->cursor_normal;
    struct nk_color track = hot ? st->bar_hover : st->bar_normal;
    struct nk_color filled = st->bar_filled;
    struct nk_color knob = shaded(ci->type == NK_STYLE_ITEM_COLOR
                                 ? ci->data.color : filled, KNOB_SHADE);
    float cap = st->bar_height * 0.5f;
    struct nk_rect in, bar, fl, kn;
    float t;

    nk_style_push_color(ctx, &ctx->style.slider.bar_normal, clear.data.color);
    nk_style_push_color(ctx, &ctx->style.slider.bar_hover, clear.data.color);
    nk_style_push_color(ctx, &ctx->style.slider.bar_active, clear.data.color);
    nk_style_push_color(ctx, &ctx->style.slider.bar_filled, clear.data.color);
    nk_style_push_style_item(ctx, &ctx->style.slider.cursor_normal, clear);
    nk_style_push_style_item(ctx, &ctx->style.slider.cursor_hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.slider.cursor_active, clear);
    nk_slider_float(ctx, lo, val, hi, step);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);

    /* The arrows, if this is what has focus: applied here rather than in the
     * shell, which knows neither the bounds nor the grain of the value. */
    {
        int steps = reaktor_focus_step(app, id);

        if (steps) {
            *val += step * (float)steps;
            if (*val < lo) *val = lo;
            if (*val > hi) *val = hi;
        }
    }
    /* And the same three numbers to the tree, after the arrows rather than
     * before, so a client reads the value it has just been given. */
    reaktor_note_range(app, id, *val, lo, hi, step);

    in  = nk_rect(b.x + st->padding.x, b.y + st->padding.y,
                  b.w - 2.0f * st->padding.x, b.h - 2.0f * st->padding.y);
    bar = nk_rect(in.x, in.y + in.h * 0.5f - cap, in.w, st->bar_height);
    t   = (hi > lo) ? (*val - lo) / (hi - lo) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    fl  = nk_rect(bar.x, bar.y, bar.w * t, bar.h);
    kn  = nk_rect(in.x + in.w * t - st->cursor_size.x * 0.5f,
                  in.y + in.h * 0.5f - st->cursor_size.y * 0.5f,
                  st->cursor_size.x, st->cursor_size.y);

    reaktor_fill_round(app, cv, bar, cap, track);
    if (fl.w >= 1.0f) reaktor_fill_round(app, cv, fl, cap, filled);
    glyph_at(app, ctx, kn, DISC_ROUND, knob,
             (int)(st->cursor_size.x < st->cursor_size.y ? st->cursor_size.x
                                                         : st->cursor_size.y),
             0.0f);
}

/* nk_slider_int's own few lines, with the aligned call in the middle. */
static void
slider_cell_int(App *app, struct nk_context *ctx, unsigned id, int *val,
                int lo, int hi, int step)
{
    float f = (float)*val;

    slider_cell(app, ctx, id, &f, (float)lo, (float)hi, (float)step);
    /* Rounded, not truncated: a step that arrives as 39.999999 is 40. */
    *val = (int)(f + (f < 0.0f ? -0.5f : 0.5f));
}

/* Nuklear draws neither of the bar's two rounded rects: they are the shape
 * reaktor_fill_round exists for, and its corners are the only ones on the page
 * that are actually curves rather than stairs. So the style items go
 * transparent for the call - which keeps the geometry, the drag and the
 * value - and the track and the fill are drawn here.
 *
 * The rects are nk_do_progress's: the bounds padded by padding plus border,
 * the fill scaled by the value. The fill's corner is the track's, clamped to
 * half its own width, or a bar in its first few per cent would draw a corner
 * wider than the bar. */
static void
progress_cell(App *app, struct nk_context *ctx, nk_size *cur, nk_size max,
              int modifiable)
{
    const struct nk_style_progress *st = &ctx->style.progress;
    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_rect b = nk_widget_bounds(ctx);
    struct nk_vec2 pad = nk_vec2(st->padding.x + st->border,
                                 st->padding.y + st->border);
    struct nk_rect fill = nk_rect(b.x + pad.x, b.y + pad.y,
                                  b.w - 2.0f * pad.x, b.h - 2.0f * pad.y);
    int hot = nk_input_is_mouse_hovering_rect(&ctx->input, b);
    struct nk_style_item track_it = hot ? st->hover : st->normal;
    struct nk_style_item fill_it = hot ? st->cursor_hover : st->cursor_normal;
    float track_r = st->rounding, r;

    fill.w *= max ? (float)*cur / (float)max : 0.0f;
    r = track_r;
    if (r > fill.w * 0.5f) r = fill.w * 0.5f;
    if (r < 0.0f) r = 0.0f;

    nk_style_push_style_item(ctx, &ctx->style.progress.normal, clear);
    nk_style_push_style_item(ctx, &ctx->style.progress.hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.progress.active, clear);
    nk_style_push_style_item(ctx, &ctx->style.progress.cursor_normal, clear);
    nk_style_push_style_item(ctx, &ctx->style.progress.cursor_hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.progress.cursor_active, clear);
    nk_progress(ctx, cur, max, modifiable);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);

    if (track_it.type == NK_STYLE_ITEM_COLOR)
        reaktor_fill_round(app, cv, b, track_r, track_it.data.color);
    if (fill.w >= 1.0f && fill_it.type == NK_STYLE_ITEM_COLOR)
        reaktor_fill_round(app, cv, fill, r, fill_it.data.color);
}

/* The knob is the same again, and the widget the stylesheet reaches least:
 * CSS has no such control, so Nuklear's own greys stand, and what it draws is
 * a filled circle with a hairline spoke from the middle out. It draws none of
 * it here - every colour is cleared for the call, which keeps the geometry,
 * the drag and the value - and the widget is three Ionicons instead: a face
 * in the surface colour, its rim, and a dot at the value's angle in the link
 * colour, which is what the rest of the page uses to mean "this one".
 *
 * The angle is nk_draw_knob's own, the value's fraction of the range as a
 * full turn zeroed at the given heading, reproduced because Nuklear keeps
 * none of it. The two fractions are placement, picked to sit the dot clear of
 * the rim; the artwork's margin inside its own box is the same for face and
 * dot, so the pair stays in proportion at any size. */
#define KNOB_DOT   0.22f   /* the dot's box, against the face's */
#define KNOB_ORBIT 0.23f   /* how far its centre sits from the middle */

static void
knob_cell(App *app, struct nk_context *ctx, float *val, float lo, float hi,
          enum nk_heading zero)
{
    static const float zero_rads[4] = { NK_PI * 1.5f, 0.0f, NK_PI * 0.5f,
                                        NK_PI };
    struct nk_style_knob *st = &ctx->style.knob;
    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    struct nk_rect b = nk_widget_bounds(ctx);
    struct nk_color face = st->knob_normal;
    struct nk_color rim  = st->knob_border_color;
    struct nk_color ink  = st->cursor_normal;
    struct nk_rect dot;
    float a, orbit;
    int i, px = (int)(b.w < b.h ? b.w : b.h);
    int dot_px = (int)((float)px * KNOB_DOT + 0.5f);
    struct nk_color *cols[7];

    cols[0] = &st->border_color;    cols[1] = &st->knob_normal;
    cols[2] = &st->knob_hover;      cols[3] = &st->knob_active;
    cols[4] = &st->cursor_normal;   cols[5] = &st->cursor_hover;
    cols[6] = &st->cursor_active;

    nk_style_push_style_item(ctx, &st->normal, clear);
    nk_style_push_style_item(ctx, &st->hover, clear);
    nk_style_push_style_item(ctx, &st->active, clear);
    for (i = 0; i < 7; i++)
        nk_style_push_color(ctx, cols[i], clear.data.color);
    nk_style_push_float(ctx, &st->knob_border, 0.0f);
    nk_knob_float(ctx, lo, val, hi, 0.01f, zero, 0.0f);
    nk_style_pop_float(ctx);
    for (i = 0; i < 7; i++)
        nk_style_pop_color(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);

    a = (hi > lo) ? (*val - lo) / (hi - lo) : 0.0f;
    a = a * NK_PI * 2.0f + zero_rads[zero];
    orbit = (float)px * KNOB_ORBIT;
    dot = nk_rect(b.x + b.w * 0.5f + orbit * (float)cos((double)a)
                      - (float)dot_px * 0.5f,
                  b.y + b.h * 0.5f + orbit * (float)sin((double)a)
                      - (float)dot_px * 0.5f,
                  (float)dot_px, (float)dot_px);

    glyph_at(app, ctx, b,   DISC_ROUND,   face, px, 0.0f);
    glyph_at(app, ctx, b,   DISC_OUTLINE, rim,  px, 0.5f);
    glyph_at(app, ctx, dot, DISC_ROUND,   ink,  dot_px, 0.0f);
}

/* A stepper's hover wash, round rather than the square Nuklear draws: the
 * slot is a square the height of the font, so half of it is a circle, and
 * reaktor_fill_round makes it one that is actually round. Nuklear draws none -
 * see stepper_push. */
static void
stepper_wash(App *app, struct nk_context *ctx, struct nk_rect sq)
{
    struct nk_color wash;

    if (!nk_input_is_mouse_hovering_rect(&ctx->input, sq)) return;
    wash = reaktor_token("--background-hover", ctx->style.property.hover.type
                         == NK_STYLE_ITEM_COLOR
                         ? ctx->style.property.hover.data.color
                         : ctx->style.text.color);
    reaktor_fill_round(app, nk_window_get_canvas(ctx), sq, sq.w * 0.5f, wash);
}

/* The two steppers of a property, placed the way nk_do_property places
 * them: a font-height square inside the border and padding at each end.
 * `b` is the bounds captured before the property was emitted. */
static void
property_chevrons(App *app, struct nk_context *ctx, struct nk_rect b)
{
    const struct nk_style_property *st = &ctx->style.property;
    float h = ctx->style.font->height;
    struct nk_rect l, r;

    l.w = l.h = r.w = r.h = h;
    l.x = b.x + st->border + st->padding.x;
    l.y = b.y + st->border + b.h * 0.5f - h * 0.5f;
    r.x = b.x + b.w - (h + st->padding.x);
    r.y = l.y;
    stepper_wash(app, ctx, l);
    stepper_wash(app, ctx, r);
    chevron_at(app, ctx, l, "chevron-back-outline",    st->dec_button.text_normal);
    chevron_at(app, ctx, r, "chevron-forward-outline", st->inc_button.text_normal);
}

/* Nuklear's own stepper wash, off for the length of one property: it is a
 * square, and stepper_wash draws a circle over the same slot afterwards,
 * which would leave the square's corners standing. Not turned off in the
 * theme, because the colour picker's three properties have no chevrons and
 * so no wash drawn for them - there, Nuklear's is all there is. */
static void
stepper_push(struct nk_context *ctx)
{
    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));

    nk_style_push_style_item(ctx, &ctx->style.property.dec_button.hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.property.dec_button.active, clear);
    nk_style_push_style_item(ctx, &ctx->style.property.inc_button.hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.property.inc_button.active, clear);
}

static void
stepper_pop(struct nk_context *ctx)
{
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
}

/* The slot nk_tree_push is about to lay out for its header.
 *
 * nk_widget_bounds cannot be asked for it directly: it peeks the *current*
 * row, and the current row is whatever the last layout call set - after api()
 * that is a 4px spacer. The push then resets the row to Nuklear's own header
 * height before it draws. So the peek gets x, y and width right and the
 * height wrong, and the node went into the tree 4px tall: the focus ring came
 * out as a thin bar above the label instead of a box around it, and every
 * platform reading bounds - the ring, AT-SPI, UIA - was told the same wrong
 * thing. The expression below is Nuklear's own, from nk_tree_state_base, and
 * it is the one tree_chevron already assumes when it centres the chevron. */
static struct nk_rect
tree_header_bounds(struct nk_context *ctx)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    b.h = ctx->style.font->height + 2.0f * ctx->style.tab.padding.y;
    return b;
}

/* A tree header's chevron, in the slot nk_tree_state_base gives its own: a
 * font-height square at the header's padding. `b` is the header's bounds,
 * captured before the push; `open` is what the push answered. */
static void
tree_chevron(App *app, struct nk_context *ctx, struct nk_rect b, int open)
{
    float h = ctx->style.font->height;
    struct nk_rect s = nk_rect(b.x + ctx->style.tab.padding.x,
                               b.y + ctx->style.tab.padding.y, h, h);

    chevron_at(app, ctx, s,
               open ? "chevron-down-outline" : "chevron-forward-outline",
               ctx->style.tab.text);
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
    const struct nk_user_font *f = reaktor_font(app, 13, 0);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    /* --background-hover, not --background: a tooltip floats above whatever
     * it is describing, and against a panel of the very same colour it read
     * as though it were half transparent. This is the palette's next step up
     * and sits clear of both the page and a panel. */
    struct nk_color fill = reaktor_token("--background-hover",
                                         nk_rgb(69, 69, 69));
    struct nk_color ink  = reaktor_token("--text-main", nk_rgb(247, 247, 247));
    struct nk_color edge = reaktor_token("--text-muted",
                                         nk_rgb(192, 192, 192));
    struct nk_color acc  = reaktor_token("--links", nk_rgb(86, 199, 255));
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
                     reaktor_token("--background-body", nk_rgb(37, 37, 37)));
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

    reaktor_hot_top(app, b, 1, 1);
    reaktor_note(app, REAKTOR_A11Y_MENUITEM, label, NULL, 0, b);
    hit = contextual ? nk_contextual_item_label(ctx, label, NK_TEXT_LEFT)
                     : nk_menu_item_label(ctx, label, NK_TEXT_LEFT);

    r = nk_rect(b.x + b.w - px - 8.0f, b.y + (b.h - px) * 0.5f, px, px);
    im = reaktor_ionicon(app, ionicon, (int)px);
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
    return hit;
}

/* A menu item, registered so the pointer changes over it. */
static int
menu_item(App *app, struct nk_context *ctx, const char *label, int contextual)
{
    {
        struct nk_rect b = nk_widget_bounds(ctx);
        reaktor_hot_top(app, b, 1, 1);
        reaktor_note(app, REAKTOR_A11Y_MENUITEM, label, NULL, 0, b);
    }
    return contextual ? nk_contextual_item_label(ctx, label, NK_TEXT_LEFT)
                      : nk_menu_item_label(ctx, label, NK_TEXT_LEFT);
}

/* Marks the widget about to be emitted: the pointer cursor over it, and its
 * entry in the accessibility tree. Buttons and fields do both for themselves
 * inside the shell; this is for the ones drawn straight from Nuklear.
 *
 * One call for both on purpose. A widget worth a cursor is worth a name, and
 * keeping them together is what stops the tree quietly falling behind the
 * screen - see docs/ACCESSIBILITY.md. `state` carries what the tree needs and
 * the cursor does not: checked, selected, expanded. */
static unsigned
hot(App *app, struct nk_context *ctx, unsigned char role, const char *name,
    unsigned state)
{
    struct nk_rect b = nk_widget_bounds(ctx);

    reaktor_hot(app, b, 1, 1);
    /* REAKTOR_A11Y_NONE means the caller has already reported it - a widget
     * whose value has to be formatted first reports itself, then asks for the
     * cursor. Reporting it twice would give a reader two of it. Answers with
     * the node's id, which a widget that takes its own activation needs and
     * every other caller ignores. */
    if (role == REAKTOR_A11Y_NONE) return 0;
    return reaktor_note(app, role, name, NULL, state, b);
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
    unsigned id = hot(app, ctx, REAKTOR_A11Y_CHECKBOX, label,
                      *on ? REAKTOR_A11Y_CHECKED : 0u);

    /* Enter, or a screen reader's press, taken here rather than delivered as
     * a click on the box - see reaktor_focus_activated. */
    if (reaktor_focus_activated(app, id)) *on = !*on;
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
    unsigned id = hot(app, ctx, REAKTOR_A11Y_CHECKBOX, label,
                      on ? REAKTOR_A11Y_CHECKED : 0u);

    if (reaktor_focus_activated(app, id)) on = !on;
    if (nk_checkbox_label_align(ctx, label, &on, CHECK_ALIGN) ||
        (*flags & value) != (on ? value : 0u)) {
        if (on) *flags |= value;
        else    *flags &= ~value;
    }
    nk_spacer(ctx);
}

/* Nuklear draws a radio as three filled circles - ring, hollow, dot - which
 * is three staircases here. So it draws none of them and the icons go in the
 * rects nk_do_toggle uses: the selector is a font-height square at the right
 * of the cell, the cursor that square inset by padding and border. A checkbox
 * is squares and needs none of this. */
static void
radio_cell(App *app, struct nk_context *ctx, const char *label, int *sel,
           int value)
{
    struct nk_rect r = nk_widget_bounds(ctx);
    const struct nk_style_toggle *st = &ctx->style.option;
    float h = ctx->style.font->height;
    struct nk_rect ring, dot;

    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    struct nk_style_item bg = st->normal, cursor = st->cursor_normal;
    int side, dot_px;
    int on = *sel == value;

    {
        unsigned id = hot(app, ctx, REAKTOR_A11Y_RADIO, label,
                          on ? REAKTOR_A11Y_CHECKED : 0u);

        if (reaktor_focus_activated(app, id)) { *sel = value; on = 1; }
    }

    /* Nuklear draws nothing for the circle: its fills are pushed transparent
     * for the call, and the widget keeps its geometry, its label and its
     * click. border_color has to go with them - nk_draw_option fills the
     * whole selector with it before the background and does not ask whether
     * there is a border, so leaving it stood a disc under everything here,
     * which read as a second rim a pixel outside the real one. */
    nk_style_push_style_item(ctx, &ctx->style.option.normal, clear);
    nk_style_push_style_item(ctx, &ctx->style.option.hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.option.active, clear);
    nk_style_push_style_item(ctx, &ctx->style.option.cursor_normal, clear);
    nk_style_push_style_item(ctx, &ctx->style.option.cursor_hover, clear);
    nk_style_push_color(ctx, &ctx->style.option.border_color, clear.data.color);
    nk_style_push_float(ctx, &ctx->style.option.border, 0.0f);
    if (nk_option_label_align(ctx, label, on, CHECK_ALIGN))
        *sel = value;
    nk_style_pop_float(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);

    /* Then the circle: the hollow, the rim over it, and the dot inside. */
    ring   = nk_rect(r.x + r.w - h, r.y + r.h * 0.5f - h * 0.5f, h, h);
    dot    = nk_rect(ring.x + st->padding.x + st->border,
                     ring.y + st->padding.y + st->border,
                     h - 2.0f * (st->padding.x + st->border),
                     h - 2.0f * (st->padding.y + st->border));
    side   = (int)h;
    dot_px = (int)dot.w;
    if (bg.type == NK_STYLE_ITEM_COLOR)
        glyph_at(app, ctx, ring, DISC_ROUND, bg.data.color, side, 0.0f);
    glyph_at(app, ctx, ring, DISC_RING, st->border_color, side, 1.0f);
    if (on && cursor.type == NK_STYLE_ITEM_COLOR)
        glyph_at(app, ctx, dot, DISC_ROUND, cursor.data.color, dot_px, 0.0f);
    nk_spacer(ctx);
}

/* A button that exists only to be looked at. Same as nk_button_label, plus
 * the hover registration every interactive widget owes the shell now that the
 * page-wide backstop no longer forces a frame. */
static int
demo_button(App *app, struct nk_context *ctx, const char *label)
{
    unsigned id = hot(app, ctx, REAKTOR_A11Y_BUTTON, label, 0);
    int fitted = reaktor_fit_label(app, ctx, nk_widget_bounds(ctx));
    int hit = nk_button_label(ctx, label);

    reaktor_unfit_label(ctx, fitted);

    /* Both, always: `||` would short-circuit and leave the activation for the
     * frame to turn into a second press. */
    if (reaktor_focus_activated(app, id)) hit = 1;
    return hit;
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
                "Tab moves focus on, as it does\n"
                "everywhere; the scrollbars appear\n"
                "when they are needed.\n"
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
    SDL_strlcpy(s->file_pick, "nothing yet", sizeof(s->file_pick));

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

/* The icons a page reaches for: each is an Ionicon read out of the submodule
 * and rasterised at the size it is drawn, its stroke colour substituted in
 * before it is parsed so it follows the stylesheet. Nuklear's own vector
 * symbols (nk_button_symbol) are still there, but they are two hairlines
 * corner to corner of a box, and beside these they looked like it. */
static const struct { const char *icon, *label; } g_icons[] = {
    { "add-outline",          "Add"      }, { "remove-outline",   "Remove"   },
    { "close-outline",        "Close"    }, { "search-outline",   "Search"   },
    { "settings-outline",     "Settings" }, { "save-outline",     "Save"     },
    { "trash-outline",        "Delete"   }, { "share-social-outline", "Share" },
    { "refresh-outline",      "Refresh"  }, { "download-outline", "Download" },
    { "star-outline",         "Star"     }, { "heart-outline",    "Favourite" }
};
#define ICON_N ((int)(sizeof(g_icons) / sizeof(g_icons[0])))

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
    if (reaktor_button(app, ctx, "Default")) s->presses++;
    if (reaktor_button_accent(app, ctx, "Primary")) s->presses++;
    if (reaktor_button_icon(app, ctx, "key-outline", "With icon"))
        s->presses++;

    section(app, ctx, "Icons",
            "Every icon is an Ionicon, read out of the submodule and "
            "rasterised at the size it is drawn - twice over, for the "
            "downscale. The stroke colour is substituted into the file "
            "before it is parsed, which is how an icon follows the "
            "stylesheet: CSS cannot reach inside an SVG. The label is the "
            "icon's name to a reader as well as to the eye.");
    api(app, ctx, "nk_button_image_label  /  reaktor_button_icon");

    nk_layout_row_dynamic(ctx, 38.0f, 4);
    for (i = 0; i < ICON_N; i++)
        if (reaktor_button_icon(app, ctx, g_icons[i].icon, g_icons[i].label))
            s->presses++;

    nk_layout_row_dynamic(ctx, 38.0f, 3);
    if (reaktor_button_icon(app, ctx, "chevron-back-outline", "Back"))
        s->presses++;
    if (reaktor_button_icon(app, ctx, "chevron-forward-outline", "Forward"))
        s->presses++;
    if (reaktor_button_icon(app, ctx, "menu-outline", "Menu"))
        s->presses++;

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
    nk_layout_row_template_push_static(ctx, 40.0f);   /* the swatch, square like its neighbour */
    nk_layout_row_template_push_static(ctx, 40.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    compact_push(ctx);
    if (reaktor_button_color(app, ctx, "Tint swatch", nk_rgb_cf(s->tint)))
        s->presses++;
    hot(app, ctx, REAKTOR_A11Y_BUTTON, "Download", 0);
    if (nk_button_image(ctx,
                        reaktor_ionicon(app, "cloud-download-outline", 24)))
        s->presses++;
    compact_pop(ctx);

    nk_button_set_behavior(ctx, NK_BUTTON_REPEATER);
    hot(app, ctx, REAKTOR_A11Y_BUTTON, "Hold me", 0);
    if (nk_button_label(ctx, "Hold me")) s->repeats++;
    nk_button_set_behavior(ctx, NK_BUTTON_DEFAULT);

    nk_widget_disable_begin(ctx);
    /* Not through hot(): a disabled control takes no pointer, but a reader
     * should still find it and be told why it cannot be used. */
    reaktor_note_here(app, ctx, REAKTOR_A11Y_BUTTON, "Disabled",
                      REAKTOR_A11Y_DISABLED);
    nk_button_label(ctx, "Disabled");
    nk_widget_disable_end(ctx);

    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "%d presses, %d repeat ticks",
                 s->presses, s->repeats);
    nk_style_push_font(ctx, reaktor_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));
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
    nk_style_push_font(ctx, reaktor_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));
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
        hot(app, ctx, REAKTOR_A11Y_LISTITEM, lab,
            s->sel_tile[i] ? REAKTOR_A11Y_SELECTED : 0u);
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
    hot(app, ctx, REAKTOR_A11Y_LISTITEM, "With a symbol",
        s->sel_row ? REAKTOR_A11Y_SELECTED : 0u);
    {
        /* NK_SYMBOL_NONE and the disc drawn into the slot Nuklear sized for
         * it - nk_do_selectable_symbol's icon rect, which for any alignment
         * but NK_TEXT_LEFT sits at twice the style's padding from the left
         * edge and is as tall as the row less that padding. */
        const struct nk_style_selectable *st = &ctx->style.selectable;
        struct nk_rect b = nk_widget_bounds(ctx);
        struct nk_rect icon;

        nk_selectable_symbol_label(ctx, NK_SYMBOL_NONE, "With a symbol",
                                   NK_TEXT_CENTERED, &s->sel_row);

        icon.y = b.y + st->padding.y + st->image_padding.y;
        icon.x = b.x + 2.0f * st->padding.x + st->image_padding.x;
        icon.w = icon.h = b.h - 2.0f * st->padding.y;
        icon.w -= 2.0f * st->image_padding.x;
        icon.h -= 2.0f * st->image_padding.y;
        glyph_at(app, ctx, icon, DISC_ROUND,
                 s->sel_row ? st->text_pressed : st->text_normal,
                 (int)(icon.w < icon.h ? icon.w : icon.h), 0.0f);
    }
    hot(app, ctx, REAKTOR_A11Y_LISTITEM, "With an image",
        s->toggle ? REAKTOR_A11Y_SELECTED : 0u);
    {
        /* Selected, the row is filled with the accent and the label switches
         * to whatever reads on it; the icon has to make the same move or it
         * is the one thing on the row that does not. */
            struct nk_color accent = reaktor_token("--links",
                                                   ctx->style.text.color);
        struct nk_color ink = s->toggle
            ? reaktor_on(accent)
            : reaktor_token("--text-muted", ctx->style.text.color);

        nk_selectable_image_label(ctx,
            reaktor_ionicon_col(app, "star-outline", 16, ink),
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
    unsigned id;

    section(app, ctx, "Text",
            "One rule - tiny.css's `input` - supplies the fill, the radius, "
            "the padding and the border - 2px of transparent until "
            "`input:focus` colours it. Only the focused field takes that "
            "colour; the rest get the neutral edge, because the rule assumes "
            "a field sits on the page and on the login card the field and the "
            "card are the same colour. A filter rejects a keystroke before it "
            "reaches the buffer, so the field cannot hold a value it would "
            "have to validate later.");
    api(app, ctx, "nk_edit_string with nk_filter_default / _decimal / _hex");

    nk_layout_row_dynamic(ctx, 36.0f, 1);
    reaktor_field(app, ctx, NK_EDIT_FIELD, s->name, &s->name_len,
                  SC_TEXT_CAP, "Full name", nk_filter_default);

    nk_layout_row_dynamic(ctx, 36.0f, 2);
    reaktor_field(app, ctx, NK_EDIT_FIELD, s->digits, &s->digits_len,
                  SC_TEXT_CAP, "Digits only", nk_filter_decimal);
    reaktor_field(app, ctx, NK_EDIT_FIELD, s->hex, &s->hex_len,
                  SC_TEXT_CAP, "Hex only", nk_filter_hex);

    api(app, ctx, "nk_edit_string with NK_EDIT_BOX "
                  "(Nuklear breaks lines on newlines only - there is no wrap)");
    nk_layout_row_dynamic(ctx, 92.0f, 1);
    reaktor_field(app, ctx, NK_EDIT_BOX, s->note, &s->note_len,
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
    SDL_snprintf(line, sizeof(line), "%.2f", (double)s->slider_f);
    id = reaktor_note(app, REAKTOR_A11Y_SLIDER, "Float", line, 0,
                      nk_widget_bounds(ctx));
    hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
    slider_cell(app, ctx, id, &s->slider_f, 0.0f, 1.0f, 0.01f);
    nk_label(ctx, line, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, ROW, 2);
    SDL_snprintf(line, sizeof(line), "%d", s->slider_i);
    id = reaktor_note(app, REAKTOR_A11Y_SLIDER, "Integer", line, 0,
                      nk_widget_bounds(ctx));
    hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
    slider_cell_int(app, ctx, id, &s->slider_i, 0, 100, 1);
    nk_label(ctx, line, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, ROW, 2);
    SDL_snprintf(line, sizeof(line), "%d", (int)s->progress);
    reaktor_note(app, REAKTOR_A11Y_PROGRESS, "Progress", line, 0,
                 nk_widget_bounds(ctx));
    hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
    progress_cell(app, ctx, &s->progress, 100, NK_MODIFIABLE);
    nk_label(ctx, "modifiable - drag it", NK_TEXT_LEFT);

    nk_layout_row_static(ctx, 62.0f, 62, 2);
    SDL_snprintf(line, sizeof(line), "%.2f", (double)s->knob);
    reaktor_note(app, REAKTOR_A11Y_SLIDER, "Knob", line, 0,
                 nk_widget_bounds(ctx));
    hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
    knob_cell(app, ctx, &s->knob, 0.0f, 1.0f, NK_DOWN);
    nk_spacer(ctx);

    section(app, ctx, "Properties",
            "A property is a labelled number that can be dragged, clicked "
            "through its two steppers, or typed into - the three ways a "
            "person expects to change a number, in one widget.");
    api(app, ctx, "nk_property_int  /  nk_property_float  /  "
                  "nk_property_double");

    nk_layout_row_dynamic(ctx, ROW, 3);
    {
        struct nk_rect pb;

        pb = nk_widget_bounds(ctx);
        SDL_snprintf(line, sizeof(line), "%d", s->prop_i);
        reaktor_note(app, REAKTOR_A11Y_SPINBUTTON, "Columns:", line, 0, pb);
        hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
        stepper_push(ctx);
        nk_property_int(ctx, "Columns:", 1, &s->prop_i, 24, 1, 0.25f);
        stepper_pop(ctx);
        property_chevrons(app, ctx, pb);

        pb = nk_widget_bounds(ctx);
        SDL_snprintf(line, sizeof(line), "%.2f", (double)s->prop_f);
        reaktor_note(app, REAKTOR_A11Y_SPINBUTTON, "Stroke:", line, 0, pb);
        hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
        stepper_push(ctx);
        nk_property_float(ctx, "Stroke:", 0.25f, &s->prop_f, 8.0f, 0.05f,
                          0.01f);
        stepper_pop(ctx);
        property_chevrons(app, ctx, pb);

        pb = nk_widget_bounds(ctx);
        SDL_snprintf(line, sizeof(line), "%.2f", s->prop_d);
        reaktor_note(app, REAKTOR_A11Y_SPINBUTTON, "Ratio:", line, 0, pb);
        hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
        stepper_push(ctx);
        nk_property_double(ctx, "Ratio:", 0.0, &s->prop_d, 100.0, 0.25,
                           0.05f);
        stepper_pop(ctx);
        property_chevrons(app, ctx, pb);
    }

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
    hot(app, ctx, REAKTOR_A11Y_COMBOBOX, "Size", 0);
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
    hot(app, ctx, REAKTOR_A11Y_COMBOBOX, "Size, with a symbol", 0);
    {
        struct nk_rect h = nk_widget_bounds(ctx);

        /* The disc goes in nk_combo_begin_symbol_text's own slot: the header
         * inset by content_padding, square on its height. */
        struct nk_rect im =
            nk_rect(h.x + ctx->style.combo.content_padding.x,
                    h.y + ctx->style.combo.content_padding.y,
                    h.h - 2.0f * ctx->style.combo.content_padding.y,
                    h.h - 2.0f * ctx->style.combo.content_padding.y);

        if (nk_combo_begin_symbol_label(ctx, sizes[s->combo_size],
                                        NK_SYMBOL_NONE,
                                        nk_vec2(nk_widget_width(ctx),
                                                130.0f))) {
            int i;
            nk_layout_row_dynamic(ctx, 26.0f, 1);
            for (i = 0; i < 3; i++) {
                struct nk_rect ib = nk_widget_bounds(ctx);
                reaktor_hot_top(app, ib, 1, 1);
                reaktor_note(app, REAKTOR_A11Y_LISTITEM, sizes[i], NULL,
                             i == s->combo_size ? REAKTOR_A11Y_SELECTED : 0u,
                             ib);
                if (nk_combo_item_label(ctx, sizes[i], NK_TEXT_LEFT))
                    s->combo_size = i;
            }
            nk_combo_end(ctx);
        }
        glyph_at(app, ctx, im, DISC_ROUND, ctx->style.combo.symbol_normal,
                 (int)im.w, 0.0f);
        combo_chrome(app, ctx, h, 2.0f);
    }
    nk_style_pop_vec2(ctx);

    nk_layout_row_static(ctx, ROW_TALL, 440, 2);
    hot(app, ctx, REAKTOR_A11Y_COMBOBOX, "Tint", 0);
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

    hot(app, ctx, REAKTOR_A11Y_COMBOBOX, "Anything at all", 0);
    {
        struct nk_rect h = nk_widget_bounds(ctx);

        if (nk_combo_begin_label(ctx, "Anything at all",
                                 nk_vec2(nk_widget_width(ctx), 130.0f))) {
            nk_layout_row_dynamic(ctx, 26.0f, 1);
            nk_label(ctx, "A combo is just a popup", NK_TEXT_LEFT);
            slider_cell(app, ctx, 0, &s->slider_f, 0.0f, 1.0f, 0.01f);
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
    hot(app, ctx, REAKTOR_A11Y_GROUP, "Colour picker", 0);
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

    nk_style_push_font(ctx, reaktor_font(app, 13, header));
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

    reaktor_style tbl, head_s, cell;
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

    reaktor_style_get("table", &tbl);
    reaktor_style_get("thead", &head_s);
    reaktor_style_get("th", &cell);

    odd     = reaktor_token("--table-bg", nk_rgb(53, 53, 53));
    even    = reaktor_token("--table-bg-alt", nk_rgb(37, 37, 37));

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
        struct nk_color page = reaktor_token("--background-body", nk_rgb(37, 37, 37));
        struct nk_color accent = reaktor_token("--links", nk_rgb(0, 112, 224));
        struct nk_color want = head_s.matched && head_s.bg[3]
                             ? reaktor_col(head_s.bg)
                             : reaktor_token("--table", accent);

        head_bg = reaktor_visible(want, page, accent);
        head_bg = reaktor_visible(head_bg, odd, accent);
        head_fg = reaktor_on(head_bg);
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
                     reaktor_token("--text-main", ctx->style.text.color));
    nk_label_colored(ctx, "--text-muted", NK_TEXT_CENTERED,
                     reaktor_token("--text-muted", ctx->style.text.color));
    nk_label_colored(ctx, "--links", NK_TEXT_RIGHT,
                     reaktor_token("--links", ctx->style.text.color));

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
            reaktor_image(app, ctx, reaktor_ionicon(app, names[k], 22), 22);
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
        progress_cell(app, ctx, &fixed, 100, NK_FIXED);
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

    /* A tree node is a container, so it is pushed rather than added: what is
     * drawn between the push and the pop is its children, and a reader walks
     * them that way. Nuklear tells us it is open by returning true. */
    /* Each header's bounds are taken before the push - the push lays out
     * its own row, so afterwards nk_widget_bounds is the first child's - and
     * serve both the tree node and the chevron drawn into the slot Nuklear
     * left empty. Closed nodes still report, and still get their chevron:
     * a reader can reach a collapsed branch, and it points somewhere. */
    {
        struct nk_rect hb = tree_header_bounds(ctx);
        int open;

        hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
        open = nk_tree_push(ctx, NK_TREE_TAB, "A tab-style tree", NK_MAXIMIZED);
        tree_chevron(app, ctx, hb, open);
        reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, "A tab-style tree", NULL,
                          open ? REAKTOR_A11Y_EXPANDED : 0u, hb);
        if (open) {
            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            nk_label(ctx, "Its children are indented under it.", NK_TEXT_LEFT);

            hb = tree_header_bounds(ctx);
            hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
            open = nk_tree_push(ctx, NK_TREE_NODE, "A node inside it",
                                NK_MINIMIZED);
            tree_chevron(app, ctx, hb, open);
            reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, "A node inside it",
                              NULL, open ? REAKTOR_A11Y_EXPANDED : 0u, hb);
            if (open) {
                nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
                nk_label(ctx, "Nesting is unlimited.", NK_TEXT_LEFT);
                nk_tree_pop(ctx);
            }
            reaktor_note_pop(app);

            for (i = 0; i < 3; i++) {
                char lab[32];
                SDL_snprintf(lab, sizeof(lab), "Selectable branch %d", i + 1);
                hb = tree_header_bounds(ctx);
                hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
                open = nk_tree_element_push(ctx, NK_TREE_NODE, lab,
                                            NK_MINIMIZED, &s->tree_leaf[i]);
                tree_chevron(app, ctx, hb, open);
                reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, lab, NULL,
                                  (open ? REAKTOR_A11Y_EXPANDED : 0u) |
                                  (s->tree_leaf[i] ? REAKTOR_A11Y_CHECKED
                                                   : 0u),
                                  hb);
                if (open) {
                    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
                    nk_label(ctx, "with a checkbox in the header",
                             NK_TEXT_LEFT);
                    nk_tree_pop(ctx);
                }
                reaktor_note_pop(app);
            }
            nk_tree_pop(ctx);
        }
        reaktor_note_pop(app);
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
            hot(app, ctx, REAKTOR_A11Y_LISTITEM, lab,
                on ? REAKTOR_A11Y_SELECTED : 0u);
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

    /* They overlap on purpose - that is the point being made, that these are
     * placed rather than laid out and the later one is drawn over the earlier.
     * The step is a third of the height rather than a quarter because at a
     * quarter each box cut the bottom off the label of the one behind it,
     * which reads as a widget drawn wrong rather than as a demonstration. */
    nk_layout_space_begin(ctx, NK_STATIC, 110.0f, 4);
    nk_layout_space_push(ctx, nk_rect(0.0f, 0.0f, 130.0f, 44.0f));
    demo_button(app, ctx, "0, 0");
    nk_layout_space_push(ctx, nk_rect(60.0f, 33.0f, 130.0f, 44.0f));
    demo_button(app, ctx, "60, 33");
    nk_layout_space_push(ctx, nk_rect(120.0f, 66.0f, 130.0f, 44.0f));
    demo_button(app, ctx, "120, 66");
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
    /* Wide enough for a whole path: the picker's answer is reported here, and
     * a label clipped at the widget edge is better than one truncated before
     * it gets there. */
    char line[SC_PATH_CAP + 32];
    struct nk_rect bar;
    int ok;

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
    bar = nk_widget_bounds(ctx);   /* before the group: see titlebar() */
    /* nk_group_begin answers 0 when the group is scrolled out of view, which
     * is a reason to skip the bar and nothing else - it was bailing out of
     * the whole page, so scrolling past the bar blanked everything below. */
    /* No NK_WINDOW_BORDER: Nuklear strokes a group's border at a literal 0
     * rounding around a fill drawn at window.rounding, so the corners of the
     * frame poked out past the rounded fill - crisp on the software renderer,
     * where nothing feathers it away. The border is stroked below instead, on
     * the fill's own footprint. The padding centres the 26px row in the 40px
     * bar: (40 - 26) / 2, read by the panel as it begins, so it is popped the
     * moment nk_group_begin has answered, whichever way it answered. */
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(12.0f, 7.0f));
    ok = nk_group_begin(ctx, "menubar", NK_WINDOW_NO_SCROLLBAR);
    nk_style_pop_vec2(ctx);
    if (ok) {
    reaktor_note_push(app, REAKTOR_A11Y_MENUBAR, "Menu bar", NULL, 0, bar);
    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, 26.0f, 3);
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, REAKTOR_A11Y_MENU, "File", 0);
    if (nk_menu_begin_label(ctx, "File", NK_TEXT_LEFT,
                            nk_vec2(150.0f, MENU_H(3)))) {
        menu_rows_push(ctx);
        nk_layout_row_dynamic(ctx, MENU_ROW, 1);
        if (item_with_icon(app, ctx, "New", "add-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > New", sizeof(s->menu_pick));
        /* The one item here that does something outside the window. SDL runs
         * the platform's own picker, so this is the OS dialog, not a drawn
         * imitation - and the answer comes back on a later frame. */
        if (item_with_icon(app, ctx, "Open...", "folder-open-outline", 0)) {
            SDL_strlcpy(s->menu_pick, "File > Open", sizeof(s->menu_pick));
            if (reaktor_file_open(app))
                SDL_strlcpy(s->file_pick, "waiting for the picker...",
                            sizeof(s->file_pick));
        }
        if (item_with_icon(app, ctx, "Close", "close-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > Close", sizeof(s->menu_pick));
        menu_rows_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, REAKTOR_A11Y_MENU, "Edit", 0);
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
        slider_cell(app, ctx, 0, &s->slider_f, 0.0f, 1.0f, 0.01f);
        menu_rows_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, REAKTOR_A11Y_MENU, "View", 0);
    if (nk_menu_begin_label(ctx, "View", NK_TEXT_LEFT,
                            nk_vec2(170.0f, MENU_H(2)))) {
        menu_rows_push(ctx);
        nk_layout_row_dynamic(ctx, MENU_ROW, 1);
        progress_cell(app, ctx, &s->progress, 100, NK_MODIFIABLE);
        if (menu_item(app, ctx, "Reset", 0))
            s->progress = 50;
        menu_rows_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
    reaktor_note_pop(app);
    nk_group_end(ctx);
    }
    /* The bar's frame, at the rounding its fill was drawn with - still
     * pushed here - inset half a pixel so the line lands inside the bar. */
    nk_stroke_rect(nk_window_get_canvas(ctx),
                   nk_rect(bar.x + 0.5f, bar.y + 0.5f, bar.w - 1.0f, bar.h - 1.0f),
                   ctx->style.window.rounding, 1.0f,
                   ctx->style.window.border_color);
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
    nk_style_push_font(ctx, reaktor_font(app, 13, 0));
    reaktor_note(app, REAKTOR_A11Y_LABEL, "Last chosen", s->menu_pick, 0,
                 nk_widget_bounds(ctx));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));

    /* File > Open opens the platform's picker, which answers on its own
     * schedule; the shell drains the answer into file_pick each frame. */
    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "file picker: %s", s->file_pick);
    reaktor_note(app, REAKTOR_A11Y_LABEL, "File picker", s->file_pick, 0,
                 nk_widget_bounds(ctx));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));
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
        hot(app, ctx, REAKTOR_A11Y_BUTTON,
            "Right-click anywhere on this button", 0);
        nk_button_label(ctx, "Right-click anywhere on this button");
        /* The same width and the same three-row height as the File menu
         * above it: a context menu is the same widget, and two popups of
         * different sizes on one page read as an oversight. */
        /* NK_WINDOW_BORDER: the rim a dynamic popup strokes for itself at
         * nk_panel_end, at its final height - see css_field in main.c. */
        if (nk_contextual_begin(ctx, NK_WINDOW_BORDER, nk_vec2(150.0f, MENU_H(3)),
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

        reaktor_hot_follow(app, b, 1);
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

        reaktor_hot_follow(app, b, 1);
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
    if (reaktor_button(app, ctx, "Open a dialog")) s->popup_open = 1;
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
            nk_style_push_font(ctx, reaktor_font(app, 16, 1));
            nk_label_colored(ctx, "Close without saving?", NK_TEXT_LEFT,
                             reaktor_token("--text-bright",
                                           ctx->style.text.color));
            nk_style_pop_font(ctx);

            nk_layout_row_dynamic(ctx, 8.0f, 1);
            nk_spacer(ctx);

            nk_layout_row_dynamic(ctx, 44.0f, 1);
            nk_style_push_font(ctx, reaktor_font(app, 13, 0));
            nk_label_colored_wrap(ctx,
                "Everything behind this is inert until the popup is closed - "
                "which is what makes it modal, with no modality machinery.",
                reaktor_token("--text-muted", ctx->style.text.color));
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
            if (reaktor_button(app, ctx, "Cancel")) {
                s->popup_open = 0;
                nk_popup_close(ctx);
            }
            if (reaktor_button_accent(app, ctx, "OK")) {
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
 * rather than wherever their labels happen to end.
 *
 * Reported as one node carrying both halves, rather than the two labels a
 * reader would otherwise meet as unrelated runs of text: a reading is a name
 * and a value, which is what the tree is shaped to say. Marked volatile,
 * because every one of them moves as the pointer does and none of that is
 * worth announcing - see REAKTOR_A11Y_VOLATILE. */
static void
diag_row(App *app, struct nk_context *ctx, const char *name, const char *value)
{
    struct nk_rect a, b;

    nk_layout_row_template_begin(ctx, 24.0f);
    nk_layout_row_template_push_static(ctx, 190.0f);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    a = nk_widget_bounds(ctx);
    nk_style_push_font(ctx, reaktor_font(app, 13, 0));
    nk_label_colored(ctx, name, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);
    b = nk_widget_bounds(ctx);
    nk_label(ctx, value, NK_TEXT_LEFT);
    /* Both columns, so a magnifier lands on the whole reading. Reported
     * after them because the second column's bounds are only known once the
     * first is drawn; the node still falls between its neighbours, which is
     * what reading order is. */
    reaktor_note(app, REAKTOR_A11Y_LABEL, name, value, REAKTOR_A11Y_VOLATILE,
                 nk_rect(a.x, a.y, (b.x + b.w) - a.x, a.h));
}

static void
page_diagnostics(App *app, struct nk_context *ctx, showcase_state *st)
{
    reaktor_diag d;
    char v[192];

    (void)st;
    reaktor_diagnostics(app, &d);

    section(app, ctx, "Rendering",
            "Which backend SDL settled on, and what the app asked for. "
            "REAKTOR_RENDERER picks between auto, gpu and software; "
            "REAKTOR_VSYNC and REAKTOR_AA turn the other two off. "
            "Anti-aliasing reads what the frame is drawn with: the software "
            "rasteriser has no partial coverage, so it feathers strokes, "
            "which grades a curve, and not fills, which would only draw a "
            "hairline. REAKTOR_SW_NOAA=1 drops both.");

    diag_row(app, ctx, "backend", d.renderer);
    SDL_snprintf(v, sizeof(v), "%s", d.mode);
    diag_row(app, ctx, "requested", v);
    diag_row(app, ctx, "vsync", d.vsync ? "on" : "off");
    diag_row(app, ctx, "anti-aliasing", d.aa);

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
    /* The number that decides how pointer motion is paced. Measured across
     * every thread of the process, because on a software renderer the frame
     * is rasterised on threads this app never created and the main thread's
     * clock sees almost none of it. */
    if (d.cpu_ms_per_frame > 0.0f)
        SDL_snprintf(v, sizeof(v), "%.1f ms of CPU, all threads",
                     (double)d.cpu_ms_per_frame);
    else SDL_strlcpy(v, "not yet measured", sizeof(v));
    diag_row(app, ctx, "a drawn frame costs", v);
    SDL_snprintf(v, sizeof(v), "one per %d ms while the pointer moves",
                 d.hover_gap_ms);
    diag_row(app, ctx, "hover redraws", v);

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
            "far, at twice the size it is drawn. The font atlas holds every "
            "baked size in one texture, normally 8-bit indexed - a glyph is "
            "coverage, so a byte a pixel and a palette say what four bytes "
            "did; the row below reports what this run got. Private is what "
            "this process has committed, resident is what is in memory now "
            "including every mapped DLL and driver. Only the first is ours - "
            "and with no GPU, as here, textures are in it too.");

    SDL_snprintf(v, sizeof(v), "%.0f KB reserved, %.0f KB used by this frame",
                 d.nk_bytes / 1024.0, d.nk_used / 1024.0);
    diag_row(app, ctx, "Nuklear buffer", v);
    SDL_snprintf(v, sizeof(v), "%.0f KB in %d rasters", d.icon_bytes / 1024.0,
                 d.icons);
    diag_row(app, ctx, "icon cache", v);
    SDL_snprintf(v, sizeof(v), "%d x %d %s, %.0f KB", d.atlas_w, d.atlas_h,
                 !d.atlas_bpp ? "none" : d.atlas_bpp == 1 ? "indexed"
                                                          : "RGBA32",
                 d.atlas_w * (double)d.atlas_h * d.atlas_bpp / 1024.0);
    diag_row(app, ctx, "font atlas", v);
    SDL_snprintf(v, sizeof(v), "%.1f MB", d.private_bytes / 1048576.0);
    diag_row(app, ctx, "process, private", v);
    SDL_snprintf(v, sizeof(v), "%.1f MB", d.rss_bytes / 1048576.0);
    diag_row(app, ctx, "process, resident", v);

    section(app, ctx, "What startup costs",
            "Both counters at each step, and the difference each one made. "
            "Private is what the step cost this process; resident is what it "
            "mapped in, the graphics driver's shared pages included. The gap "
            "between the two columns is the point: a step can map in ten "
            "megabytes and commit almost none of it. Anything before the "
            "first line is what the C runtime and the loader wanted before "
            "main ran.");

    {
        int i;

        SDL_snprintf(v, sizeof(v), "%.1f MB private, %.1f MB resident",
                     d.priv_at[RSS_ENTRY] / 1048576.0,
                     d.rss_at[RSS_ENTRY] / 1048576.0);
        diag_row(app, ctx, reaktor_rss_names[RSS_ENTRY], v);
        for (i = RSS_ENTRY + 1; i < RSS_STEPS; i++) {
            double dp = (d.priv_at[i] - (double)d.priv_at[i - 1]) / 1048576.0;
            double dr = (d.rss_at[i] - (double)d.rss_at[i - 1]) / 1048576.0;
            SDL_snprintf(v, sizeof(v), "%+.1f MB private   %+.1f MB resident",
                         dp, dr);
            diag_row(app, ctx, reaktor_rss_names[i], v);
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

/* --- dispatch ------------------------------------------------------------ */

/* Owned here, not by the library: what this demo remembers between frames is
 * the demo's business. */
static showcase_state g_show;

showcase_state *
sample_state(void)
{
    return &g_show;
}

void
sample_file_taken(App *app)
{
    reaktor_file_taken(app, g_show.file_pick, (int)sizeof(g_show.file_pick));
}

void
reaktor_showcase_page(App *app, struct nk_context *ctx, int tab,
                      float w, float h)
{
    showcase_state *s = sample_state();

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
