/* widgets.c - what a page may ask of the shell.
 *
 * Declared in ui.h. Thin on purpose: a page is handed an App * and never sees
 * inside it, so everything it can do passes through here. The accessibility
 * notes and the file picker live here for the same reason.
 *
 * Lifted out of main.c unchanged.
 */
#include "internal.h"
#include "declare.h"

/* Non-zero while a widget that reports itself is being drawn by one that
 * has already reported it - see reaktor_note_mute. */
static int g_note_mute;

/* --- what a page may ask of the shell ------------------------------------
 * Declared in ui.h. Thin on purpose: the point of the indirection is only that
 * showcase.c never sees inside App. */

struct nk_color
reaktor_col(const unsigned char rgba[4])
{
    return col_of(rgba);
}

struct nk_color
reaktor_token(const char *name, struct nk_color def)
{
    unsigned char c[4];
    return reaktor_style_token(name, c) ? col_of(c) : def;
}

const struct nk_user_font *
reaktor_font(App *app, int px, int bold)
{
    return pick_font(app, px, bold);
}

/* Below about twenty pixels an Ionicon's stroke - a fixed 6.25% of the glyph -
 * falls under one device pixel and greys out. The window controls hit this
 * first; the rule lives here so every small icon comes out solid. */
#define ICON_HAIRLINE_BELOW 20

struct nk_image
reaktor_ionicon(App *app, const char *name, int px)
{
    char src[192];

    if (px < ICON_HAIRLINE_BELOW)
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                     name, app->icon_hex, (double)GLYPH_STROKE);
    else
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg?stroke=%s",
                     name, app->icon_hex);
    return icon(app, src, px);
}

/* Rasterised at exactly the size it is drawn, at an explicit stroke weight:
 * both matter to a rim a pixel wide. `sw` at zero takes the hairline rule. */
struct nk_image
reaktor_ionicon_exact(App *app, const char *name, int px,
                      struct nk_color stroke, float sw)
{
    char src[192];

    if (sw <= 0.0f) sw = px < ICON_HAIRLINE_BELOW ? GLYPH_STROKE : 1.0f;
    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=#%02x%02x%02x"
                 "&sw=%.2f", name, stroke.r, stroke.g, stroke.b, (double)sw);
    return icon_over(app, src, px, 1.0f);
}

struct nk_image
reaktor_ionicon_col(App *app, const char *name, int px, struct nk_color stroke)
{
    char src[192];

    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=#%02x%02x%02x"
                 "&sw=%.2f", name, stroke.r, stroke.g, stroke.b,
                 px < ICON_HAIRLINE_BELOW ? (double)GLYPH_STROKE : 1.0);
    return icon(app, src, px);
}

/* The label colour for something drawn on `bg`: the stylesheet's own unless it
 * fails the contrast floor, as for the accent button and the selection. */
struct nk_color
reaktor_on(struct nk_color bg)
{
    unsigned char c[4];

    c[0] = bg.r; c[1] = bg.g; c[2] = bg.b; c[3] = bg.a;
    return readable_on(c, "--text-bright", "--background-body");
}

void
reaktor_image(App *app, struct nk_context *ctx, struct nk_image im, int px)
{
    (void)app;
    image_centred(ctx, im, px);
}

void
reaktor_hot(App *app, struct nk_rect r, int cursor, int repaint)
{
    hot_push(app, r, cursor, repaint);
}

void
reaktor_hot_top(App *app, struct nk_rect r, int cursor, int repaint)
{
    hot_push_ex(app, r, cursor, repaint, 1, 0);
}

void
reaktor_hot_follow(App *app, struct nk_rect r, int cursor)
{
    hot_push_ex(app, r, cursor, 1, 0, 1);
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

/* One Ionicon centred in `slot`, at exactly px and no resampling - which is
 * what keeps a rim a line rather than a smear. `sw` multiplies the stroke the
 * artwork declares; at zero the hairline rule decides, which is what a
 * chevron wants and twice what a rim does. */
void
reaktor_glyph_at(App *app, struct nk_context *ctx, struct nk_rect slot,
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


/* Text that acts, reported as a link. Lifted out of the sample when the
 * declarative API needed one: a link is a widget, not a page's business. */
int
reaktor_link_label(App *app, struct nk_context *ctx, const char *label, int active)
{
    unsigned char c[4];
    struct nk_color col;
    int clicked = 0;

    /* A link does not change colour on hover, only the cursor. */
    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 0);
        reaktor_note(app, REAKTOR_A11Y_LINK, label, NULL,
                     active ? REAKTOR_A11Y_SELECTED : 0u, b);
        /* Released inside, not pressed - the same reason the buttons use
         * NK_BUTTON_TRIGGER_ON_RELEASE. Checked against clicked_pos, so
         * letting go elsewhere does not count. */
        if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, b))
            clicked = 1;
    }

    col = active && reaktor_style_token("--links", c)
        ? col_of(c)
        : (reaktor_style_token("--text-muted", c) ? col_of(c) : app->text);

    nk_style_push_color(ctx, &ctx->style.text.color, col);
    nk_label(ctx, label, NK_TEXT_CENTERED);
    nk_style_pop_color(ctx);
    return clicked;
}

/* Nuklear draws a radio as three filled circles - ring, hollow, dot - which
 * is three staircases here. So it draws none of them and the icons go in the
 * rects nk_do_toggle uses: the selector is a font-height square at the right
 * of the cell, the cursor that square inset by padding and border. A checkbox
 * is squares and needs none of this. */
int
reaktor_radio_label(App *app, struct nk_context *ctx, const char *label,
                    int on)
{
    struct nk_rect r = nk_widget_bounds(ctx);
    const struct nk_style_toggle *st = &ctx->style.option;
    float h = ctx->style.font->height;
    struct nk_rect ring, dot;

    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    struct nk_style_item bg = st->normal, cursor = st->cursor_normal;
    int side, dot_px;
    int clicked = 0;

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
    if (nk_option_label_align(ctx, label, on, NK_WIDGET_RIGHT, NK_TEXT_LEFT))
        clicked = 1;
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
        reaktor_glyph_at(app, ctx, ring, REAKTOR_DISC_ROUND, bg.data.color, side, 0.0f);
    reaktor_glyph_at(app, ctx, ring, REAKTOR_DISC_RING, st->border_color, side, 1.0f);
    if (on && cursor.type == NK_STYLE_ITEM_COLOR)
        reaktor_glyph_at(app, ctx, dot, REAKTOR_DISC_ROUND, cursor.data.color, dot_px, 0.0f);
    return clicked;
}


/* --- first-run values ---------------------------------------------------- */


/* Nuklear draws none of a slider either. Its bar and fill are rounded rects
 * whose caps it steps through in whole pixels, and its knob is one more
 * nk_fill_circle - so the styles go transparent for the call, which keeps the
 * geometry, the drag and the value, and all three are drawn afterwards, with
 * the value the drag has just produced rather than the previous frame's.
 *
 * The rects are nk_do_slider's: the bounds inset by padding, a bar of
 * bar_height centred in it, as much of it filled as the value, and the knob a
 * cursor_size square on the same centre line. */
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

void
reaktor_slider_bar(App *app, struct nk_context *ctx, unsigned id, float *val,
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
    reaktor_glyph_at(app, ctx, kn, REAKTOR_DISC_ROUND, knob,
             (int)(st->cursor_size.x < st->cursor_size.y ? st->cursor_size.x
                                                         : st->cursor_size.y),
             0.0f);
}

/* nk_slider_int's own few lines, with the aligned call in the middle. */
void
reaktor_slider_bar_int(App *app, struct nk_context *ctx, unsigned id, int *val,
                int lo, int hi, int step)
{
    float f = (float)*val;

    reaktor_slider_bar(app, ctx, id, &f, (float)lo, (float)hi, (float)step);
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
void
reaktor_progress_bar(App *app, struct nk_context *ctx, nk_size *cur, nk_size max,
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

void
reaktor_knob_dial(App *app, struct nk_context *ctx, float *val, float lo, float hi,
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

    reaktor_glyph_at(app, ctx, b,   REAKTOR_DISC_ROUND,   face, px, 0.0f);
    reaktor_glyph_at(app, ctx, b,   REAKTOR_DISC_OUTLINE, rim,  px, 0.5f);
    reaktor_glyph_at(app, ctx, dot, REAKTOR_DISC_ROUND,   ink,  dot_px, 0.0f);
}

/* A chevron where Nuklear would have drawn one of its own. nk_draw_symbol's
 * chevron is two one-pixel lines corner to corner of whatever box it is
 * handed - thin, and half the size of the combo's - so tree headers and
 * property steppers hand Nuklear NK_SYMBOL_NONE and get the Ionicon here,
 * centred in the slot Nuklear sized. */
#define CHEVRON_PX 14
void
reaktor_chevron_at(App *app, struct nk_context *ctx, struct nk_rect slot,
           const char *name, struct nk_color col)
{
    struct nk_rect r;
    struct nk_image im = reaktor_ionicon_col(app, name, CHEVRON_PX, col);

    r.w = r.h = (float)CHEVRON_PX;
    r.x = slot.x + (slot.w - r.w) * 0.5f;
    r.y = slot.y + (slot.h - r.h) * 0.5f;
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
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
void
reaktor_property_chrome(App *app, struct nk_context *ctx, struct nk_rect b)
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
    reaktor_chevron_at(app, ctx, l, "chevron-back-outline",    st->dec_button.text_normal);
    reaktor_chevron_at(app, ctx, r, "chevron-forward-outline", st->inc_button.text_normal);
}

/* Nuklear's own stepper wash, off for the length of one property: it is a
 * square, and stepper_wash draws a circle over the same slot afterwards,
 * which would leave the square's corners standing. Not turned off in the
 * theme, because the colour picker's three properties have no chevrons and
 * so no wash drawn for them - there, Nuklear's is all there is. */
void
reaktor_property_push(struct nk_context *ctx)
{
    struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));

    nk_style_push_style_item(ctx, &ctx->style.property.dec_button.hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.property.dec_button.active, clear);
    nk_style_push_style_item(ctx, &ctx->style.property.inc_button.hover, clear);
    nk_style_push_style_item(ctx, &ctx->style.property.inc_button.active, clear);
}

void
reaktor_property_pop(struct nk_context *ctx)
{
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
}

int
reaktor_button_label(App *app, struct nk_context *ctx, const char *label)
{
    return css_button(app, ctx, "button", label);
}

int
reaktor_button_accent(App *app, struct nk_context *ctx, const char *label)
{
    return css_button_accent(app, ctx, "button", label, "--links");
}


/* Cut a button's vertical padding to what its row can actually hold, for the
 * one widget about to be drawn.
 *
 * nk_do_button insets the content rect by padding + border + rounding and
 * nk_widget_text then centres the label inside it. The centring is exact for
 * any padding, because content.y + content.h/2 comes back to the button's
 * middle - but only while content.h is what the arithmetic produced. tiny.css
 * asks for 9.6px of padding on top of a 2px border and an 8px radius, which
 * is 39px of inset; on a 30px row the height comes out negative, NK_MAX in
 * nk_widget_text clamps it to zero, and the origin is left below the middle.
 * The label is then centred on the wrong point and sits three to four pixels
 * low - which reads as a row of buttons whose text does not line up with
 * anything, and was reported as exactly that.
 *
 * It is the same arithmetic compact_push() in showcase.c already documents
 * for glyphs, where a content rect with no area shows up as artwork that
 * vanishes rather than as text that sags.
 *
 * Height-aware on purpose, and pushed per widget rather than clamped once in
 * the CSS mapping: a button tall enough for the stylesheet's padding keeps
 * it, and an image inside one is inset by exactly what it was before. Only
 * the rows that were already drawing a degenerate content rect change.
 * Answers whether it pushed; hand that to reaktor_unfit_label. */
int
reaktor_fit_label(App *app, struct nk_context *ctx, struct nk_rect b)
{
    float room = b.h * 0.5f
               - (ctx->style.button.border + ctx->style.button.rounding);

    (void)app;
    if (room < 0.0f) room = 0.0f;
    if (ctx->style.button.padding.y <= room) return 0;
    nk_style_push_vec2(ctx, &ctx->style.button.padding,
                       nk_vec2(ctx->style.button.padding.x, room));
    return 1;
}

void
reaktor_unfit_label(struct nk_context *ctx, int fitted)
{
    if (fitted) nk_style_pop_vec2(ctx);
}

/* A colour swatch: the button rule exactly as its neighbours draw it, with
 * the fill replaced and no label. Not nk_button_color, which draws its own
 * rect and so keeps none of the rule's geometry. */
int
reaktor_button_color(App *app, struct nk_context *ctx, const char *name,
                     struct nk_color fill)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    unsigned char hov[4];
    char hex[10];
    int clicked;

    hot_push(app, b, 1, 1);
    SDL_snprintf(hex, sizeof(hex), "#%02x%02x%02x", fill.r, fill.g, fill.b);
    reaktor_note(app, REAKTOR_A11Y_BUTTON, name, hex, 0, b);

    hov[0] = fill.r; hov[1] = fill.g; hov[2] = fill.b; hov[3] = fill.a;
    reaktor_style_darken(hov, 0.12f);

    /* Only the colours are pushed. Size, radius and padding stay whatever
     * the caller has in force, which is what makes this the same button as
     * the nk_button_image beside it rather than a lookalike - pushing the
     * `button` rule here instead put the CSS radius and padding over the
     * caller's, and the two stopped matching.
     *
     * The border takes the fill colour too, which is the one deviation and
     * the reason the corner matches. nk_draw_button fills the bounds in the
     * border colour and then fills the inset rect in the background, so the
     * border is a band and the fill's own arc sits at rounding - border.
     * With a grey rim the outer arc is there and measures identical to the
     * neighbour's, but grey on the page is a two-step difference nobody
     * sees: the eye reads the blue, and the blue is the tighter corner.
     * Colouring the band blue puts the visible edge back on the button's
     * outer arc, where its neighbour's is. */
    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(fill));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(col_of(hov)));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(col_of(hov)));
    nk_style_push_color(ctx, &ctx->style.button.border_color, fill);
    clicked = nk_button_label(ctx, "");
    nk_style_pop_color(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);

    return clicked;
}

int
reaktor_button_icon(App *app, struct nk_context *ctx, const char *ionicon,
                    const char *label)
{
    char src[192];

    SDL_snprintf(src, sizeof(src),
                 "third_party/ionicons/src/svg/%s.svg?stroke=%s",
                 ionicon, app->icon_hex);
    return css_button_icon(app, ctx, "button", src, label);
}

/* The showcase's own fields. nk_edit_string keeps the caret and selection
 * inside Nuklear, so a page can have several; the login field uses
 * nk_edit_buffer because its context menu has to reach that state. */
nk_flags
reaktor_field_text(App *app, struct nk_context *ctx, nk_flags flags,
              char *buf, int *len, int cap, const char *hint,
              nk_plugin_filter filter)
{
    struct nk_rect bounds = nk_widget_bounds(ctx);
    reaktor_style s;
    style_frame f;
    nk_flags state;

    note_field_rect(app, ctx, bounds);
    hot_push(app, bounds, 2, 0);

    f = push_edit_style(ctx, &s, 0);
    state = nk_edit_string(ctx, flags, buf, len, cap, filter);
    pop_style(ctx, f);
    stroke_edit_edge(ctx, bounds, &s);
    note_ime_caret(app, ctx, bounds, state, &ctx->text_edit);
    /* The hint doubles as the label: it is the only text the field carries,
     * and an unnamed field is unusable to a reader. A box with no hint gets
     * its shape instead, which is at least a description. */
    reaktor_note(app, REAKTOR_A11Y_TEXTBOX,
                 hint ? hint : ((flags & NK_EDIT_BOX) ? "Notes" : "Text"), buf,
                 state & NK_EDIT_ACTIVE ? REAKTOR_A11Y_FOCUSED : 0u, bounds);

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
    return state;
}

/* --- the platform file picker ------------------------------------------
 * SDL_ShowOpenFileDialog returns at once and calls back later, possibly from
 * another thread. So the callback touches nothing but this struct: it fills in
 * the answer, publishes it with an atomic store, and pushes an event to wake a
 * main loop that may be parked in SDL_WaitEvent. Nuklear, the renderer and the
 * style are main-thread only and are left to reaktor_file_taken. */

static const SDL_DialogFileFilter g_file_filters[] = {
    { "Stylesheets", "css" },
    { "All files",   "*"   }
};

static void SDLCALL
file_chosen(void *userdata, const char * const *filelist, int filter)
{
    App *app = (App *)userdata;
    SDL_Event wake;

    (void)filter;
    if (!filelist)
        SDL_snprintf(app->file_answer, sizeof(app->file_answer),
                     "unavailable: %s", SDL_GetError());
    else if (!filelist[0])
        SDL_strlcpy(app->file_answer, "cancelled", sizeof(app->file_answer));
    else
        SDL_strlcpy(app->file_answer, filelist[0], sizeof(app->file_answer));

    /* Release: everything written above is visible to the thread that sees
     * this. SDL's atomics are full barriers. */
    SDL_SetAtomicInt(&app->file_ready, 1);

    SDL_zero(wake);
    wake.type = app->wake_event;
    SDL_PushEvent(&wake);
}

int
reaktor_file_open(App *app)
{
    if (app->file_pending) return 0;
    app->file_pending = 1;
    SDL_ShowOpenFileDialog(file_chosen, app, app->win, g_file_filters,
                           (int)NK_LEN(g_file_filters), NULL, false);
    return 1;
}

int
reaktor_file_taken(App *app, char *out, int cap)
{
    if (!SDL_GetAtomicInt(&app->file_ready)) return 0;
    SDL_SetAtomicInt(&app->file_ready, 0);
    app->file_pending = 0;
    SDL_strlcpy(out, app->file_answer, (size_t)cap);
    return 1;
}

/* REAKTOR_A11Y_DUMP=<path> writes the tree once, early, and is how phase 2 is
 * checked: the instrumentation is invisible on screen, so the only way to see
 * whether a widget reported itself is to read the tree. */

/* Frames to let pass before the tree is written out.
 *
 * It was one. A declared box is placed by the layout engine at the end of the
 * frame that declares it - see core/ui/layout.h - so on the very first frame
 * a declared page reports two empty containers and nothing inside them. That
 * is correct behaviour and a useless snapshot. Two frames is what it takes for
 * the first one to have somewhere to be, and every page that draws itself
 * immediately is identical on both. */
#define A11Y_DUMP_FRAME 2

void
a11y_dump_once(App *app)
{
    static int done, frames;
    const char *path;
    FILE *f;

    if (done) return;
    path = SDL_getenv("REAKTOR_A11Y_DUMP");
    if (!path) { done = 1; return; }
    /* Not just a frame count: a wrapped paragraph needs the frame after the
     * one that learned its width, and a page with none is settled at once. So
     * the dump waits for the tree to stop moving rather than for a number. */
    if (++frames < A11Y_DUMP_FRAME || !reaktor_frame_settled()) {
        /* Dirty alone is not enough: at rest the loop is parked in
         * SDL_WaitEvent, and nothing here is an event. An empty user event is
         * what the activation fallback already uses to ask for a frame. */
        SDL_Event e;

        app->dirty = 1;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
        return;
    }
    done = 1;
    f = fopen(path, "w");
    if (!f) return;
    reaktor_a11y_dump(&app->a11y, f);
    {
        int n = 0;
        reaktor_a11y_tree(&app->a11y, &n);
        fprintf(f, "\n# %d of %d nodes, %d of %d string bytes, "
                   "%d interned, struct %d bytes\n",
                n, REAKTOR_A11Y_MAX_NODES, app->a11y.pool_used,
                REAKTOR_A11Y_POOL, app->a11y.pool_entries,
                (int)sizeof(reaktor_a11y));
    }
    fclose(f);
}

/* --- describing the frame ----------------------------------------------- */
/* Thin forwards onto app->a11y, so App stays opaque to the pages and a11y.h
 * stays free of it. Every report passes through focus_saw, which is how the
 * frame learns where the focused node was drawn without any widget knowing
 * that focus exists. */

static void
focus_saw(App *app, unsigned id, struct nk_rect b)
{
    if (id && id == app->focus_id) {
        app->focus_rect = b;
        app->focus_seen = 1;
    }
}

unsigned
reaktor_note(App *app, unsigned char role, const char *name, const char *value,
             unsigned state, struct nk_rect bounds)
{
    unsigned id;

    if (g_note_mute) return 0;
    id = reaktor_a11y_add(&app->a11y, role, name, value, state, bounds);
    focus_saw(app, id, bounds);
    return id;
}

unsigned
reaktor_note_push(App *app, unsigned char role, const char *name,
                  const char *value, unsigned state, struct nk_rect bounds)
{
    unsigned id;

    if (g_note_mute) return 0;
    id = reaktor_a11y_push(&app->a11y, role, name, value, state, bounds);
    focus_saw(app, id, bounds);
    return id;
}

void
reaktor_note_pop(App *app)
{
    reaktor_a11y_pop(&app->a11y);
}

void
reaktor_note_range(App *app, unsigned id, float num, float lo, float hi,
                   float step)
{
    reaktor_a11y_set_range(&app->a11y, id, num, lo, hi, step);
}

void
reaktor_note_keys(App *app, unsigned id, const char *keys)
{
    reaktor_a11y_set_keys(&app->a11y, id, keys);
}

void
reaktor_note_bounds(App *app, unsigned id, struct nk_rect r)
{
    reaktor_a11y_set_bounds(&app->a11y, id, r);
}

void
reaktor_note_mute(App *app, int on)
{
    (void)app;
    g_note_mute += on ? 1 : -1;
    if (g_note_mute < 0) g_note_mute = 0;
}

/* nk_widget_bounds answers where the *next* widget goes, so this is called
 * before drawing, not after - which is also when the caller still knows what
 * it is about to draw. */
unsigned
reaktor_note_here(App *app, struct nk_context *ctx, unsigned char role,
                  const char *name, unsigned state)
{
    struct nk_rect b;
    unsigned       id;

    if (g_note_mute) return 0;
    b  = nk_widget_bounds(ctx);
    id = reaktor_a11y_add(&app->a11y, role, name, NULL, state, b);
    focus_saw(app, id, b);
    return id;
}

/* The radius for a popup, tooltip or menu: `dialog`'s, capped, because 1rem is
 * a pill at tooltip height. Pushed around those calls, not set globally. */
float
reaktor_popup_rounding(void)
{
    reaktor_style dlg;
    reaktor_style_get("dialog", &dlg);
    if (!dlg.matched) return 6.0f;
    return dlg.rounding > 8.0f ? 8.0f : dlg.rounding;
}
void
reaktor_diagnostics(App *app, reaktor_diag *out)
{
    out->renderer   = SDL_GetRendererName(app->ren);
    out->mode       = app->render_mode;
    out->frame_rate = app->frame_rate;
    out->drag_rate  = app->drag_rate;
    out->font       = app->font_status;
    out->vsync      = app->vsync_on;
    /* what the frame is drawn with, not what was asked for - see the note at
     * nk_sdl_render_ex in the frame body */
    out->aa = !app->aa ? "off"
            : !app->renderer_is_sw ? "on"
            : app->sw_noaa ? "off (software renderer)"
            : "strokes only (software renderer)";
    out->dark       = app->dark;
    out->sheets     = SHEET_COUNT;
    out->scale      = reaktor_scale();
    out->style_ms   = app->style_ms_x100 / 100.0f;
    out->frame_gap_ms = app->frame_gap_ms;
    out->cpu_ms_per_frame = app->cpu_ms_per_frame;
    out->hover_gap_ms     = (int)g_hover_gap_ms;

    /* Nuklear grows this to fit the busiest frame it has been asked to draw
     * and keeps it; `used` is what the last frame actually needed. */
    out->nk_bytes = (unsigned long)app->ctx->memory.size;
    out->nk_used  = (unsigned long)app->ctx->memory.allocated;

    {
        int i;
        unsigned long b = 0;
        for (i = 0; i < app->img_count; i++)
            b += (unsigned long)app->img[i].w * (unsigned long)app->img[i].h * 4u;
        out->icon_bytes = b;
        out->icons      = app->img_count;
    }

    {
        size_t rss = 0, priv = 0;
        reaktor_process_memory(&rss, &priv);
        out->rss_bytes     = (unsigned long)rss;
        out->private_bytes = (unsigned long)priv;
    }
    out->atlas_w    = app->atlas_w;
    out->atlas_h    = app->atlas_h;
    out->atlas_bpp  = app->atlas_bpp;
    {
        int i;
        for (i = 0; i < RSS_STEPS; i++) {
            out->rss_at[i]  = (unsigned long)g_rss[i];
            out->priv_at[i] = (unsigned long)g_priv[i];
        }
    }
    out->build_ms   = app->build_ms_x100 / 100.0f;
    out->render_ms  = app->render_ms_x100 / 100.0f;
    out->present_ms = app->present_ms_x100 / 100.0f;
}
