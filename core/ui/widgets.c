#include "internal.h"
#include "declare.h"

static int g_note_mute;

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

#define ICON_HAIRLINE_BELOW 20

struct nk_image
reaktor_ionicon(App *app, const char *name, int px)
{
    char src[192];

    if (px < ICON_HAIRLINE_BELOW)
        SDL_snprintf(src, sizeof(src),
                     "external/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                     name, app->icon_hex, (double)GLYPH_STROKE);
    else
        SDL_snprintf(src, sizeof(src),
                     "external/ionicons/src/svg/%s.svg?stroke=%s",
                     name, app->icon_hex);
    return icon(app, src, px);
}

struct nk_image
reaktor_ionicon_exact(App *app, const char *name, int px,
                      struct nk_color stroke, float sw)
{
    char src[192];

    if (sw <= 0.0f) sw = px < ICON_HAIRLINE_BELOW ? GLYPH_STROKE : 1.0f;
    SDL_snprintf(src, sizeof(src),
                 "external/ionicons/src/svg/%s.svg?stroke=#%02x%02x%02x"
                 "&sw=%.2f", name, stroke.r, stroke.g, stroke.b, (double)sw);
    return icon_over(app, src, px, 1.0f);
}

struct nk_image
reaktor_ionicon_col(App *app, const char *name, int px, struct nk_color stroke)
{
    char src[192];

    SDL_snprintf(src, sizeof(src),
                 "external/ionicons/src/svg/%s.svg?stroke=#%02x%02x%02x"
                 "&sw=%.2f", name, stroke.r, stroke.g, stroke.b,
                 px < ICON_HAIRLINE_BELOW ? (double)GLYPH_STROKE : 1.0);
    return icon(app, src, px);
}

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


void
reaktor_glyph_at(App *app, struct nk_context *ctx, struct nk_rect slot,
         const char *name, struct nk_color col, int px, float sw)
{
    struct nk_rect r;
    struct nk_image im;

    if (px < 1) return;
    im = reaktor_ionicon_exact(app, name, px, col, sw);
    if (!im.handle.ptr) return;
    r.w = r.h = (float)px;
    r.x = slot.x + (slot.w - r.w) * 0.5f;
    r.y = slot.y + (slot.h - r.h) * 0.5f;
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
}

int
reaktor_link_label(App *app, struct nk_context *ctx, const char *label, int active)
{
    struct nk_color col;
    int clicked = 0;

    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 0);
        reaktor_note(app, REAKTOR_A11Y_LINK, label, NULL,
                     active ? REAKTOR_A11Y_SELECTED : 0u, b);
        if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, b))
            clicked = 1;
    }

    col = active ? reaktor_token("--links", app->text)
                 : reaktor_token("--text-muted", app->text);

    nk_style_push_color(ctx, &ctx->style.text.color, col);
    nk_label(ctx, label, NK_TEXT_CENTERED);
    nk_style_pop_color(ctx);
    return clicked;
}

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
    nk_style_push_float(ctx, &ctx->style.slider.border, 0.0f);
    nk_slider_float(ctx, lo, val, hi, step);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);

    {
        int steps = reaktor_focus_step(app, id);

        if (steps) {
            *val += step * (float)steps;
            if (*val < lo) *val = lo;
            if (*val > hi) *val = hi;
        }
    }
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

void
reaktor_slider_bar_int(App *app, struct nk_context *ctx, unsigned id, int *val,
                int lo, int hi, int step)
{
    float f = (float)*val;

    reaktor_slider_bar(app, ctx, id, &f, (float)lo, (float)hi, (float)step);
    *val = (int)(f + (f < 0.0f ? -0.5f : 0.5f));
}

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
    /* Both borders: Nuklear strokes the widget and the cursor separately,
     * and the cursor's is the one that showed as a hairline across the fill. */
    nk_style_push_float(ctx, &ctx->style.progress.border, 0.0f);
    nk_style_push_float(ctx, &ctx->style.progress.cursor_border, 0.0f);
    nk_progress(ctx, cur, max, modifiable);
    nk_style_pop_float(ctx);
    nk_style_pop_float(ctx);
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

#define KNOB_DOT   0.22f
#define KNOB_ORBIT 0.23f

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

#define COMBO_MARGIN   12.0f
#define COMBO_ARROW    15.0f
#define COMBO_GAP      12.0f
struct nk_rect
reaktor_combo_content(struct nk_context *ctx, struct nk_rect h)
{
    struct nk_rect r;

    r.x = h.x + ctx->style.combo.content_padding.x;
    r.y = h.y + ctx->style.combo.content_padding.y + 2.0f;
    r.h = h.h - 2.0f * (ctx->style.combo.content_padding.y + 2.0f);
    r.w = (h.x + h.w - COMBO_MARGIN - COMBO_ARROW - COMBO_GAP) - r.x;
    if (r.w < 0.0f) r.w = 0.0f;
    return r;
}

void
reaktor_combo_chrome(App *app, struct nk_context *ctx, struct nk_rect h, float border)
{
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    float px = COMBO_ARROW;
    struct nk_rect r;
    struct nk_image im;

    if (border > 0.0f)
        nk_stroke_rect(canvas, h, ctx->style.combo.rounding, border,
                       ctx->style.combo.border_color);

    r.x = h.x + h.w - COMBO_MARGIN - px;
    r.y = h.y + (h.h - px) * 0.5f;
    r.w = r.h = px;
    im = reaktor_ionicon(app, "chevron-down-outline", (int)px);
    nk_draw_image(canvas, r, &im, nk_rgb(255, 255, 255));
}

int
reaktor_button_label(App *app, struct nk_context *ctx, const char *label)
{
    return css_button(app, ctx, "button", label);
}

/* The same three buttons, painted from a rule the page named. A class the
 * sheet does not define falls back to `button`, so a selector that is only
 * meaningful to one stylesheet costs nothing under another. */
int
reaktor_button_label_as(App *app, struct nk_context *ctx, const char *sel,
                        const char *label)
{
    return css_button(app, ctx, sel, label);
}

int
reaktor_button_accent_as(App *app, struct nk_context *ctx, const char *sel,
                         const char *label)
{
    return css_button_accent(app, ctx, sel, label, "--links");
}

int
reaktor_button_icon_as(App *app, struct nk_context *ctx, const char *sel,
                       const char *ionicon, const char *label)
{
    char src[192];

    SDL_snprintf(src, sizeof(src),
                 "external/ionicons/src/svg/%s.svg?stroke=%s",
                 ionicon, app->icon_hex);
    return css_button_icon(app, ctx, sel, src, label);
}

int
reaktor_button_accent(App *app, struct nk_context *ctx, const char *label)
{
    return css_button_accent(app, ctx, "button", label, "--links");
}

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
                 "external/ionicons/src/svg/%s.svg?stroke=%s",
                 ionicon, app->icon_hex);
    return css_button_icon(app, ctx, "button", src, label);
}

nk_flags
reaktor_field_text(App *app, struct nk_context *ctx, nk_flags flags,
              char *buf, int *len, int cap, const char *hint,
              nk_plugin_filter filter, float pad_x, float pad_y)
{
    struct nk_rect bounds = nk_widget_bounds(ctx);
    reaktor_style s;
    style_frame f;
    nk_flags state;

    note_field_rect(app, ctx, bounds);
    hot_push(app, bounds, 2, 0);

    f = push_edit_style(ctx, &s, 0);
    if (pad_x > 0.0f) ctx->style.edit.padding.x = pad_x;
    if (pad_y > 0.0f) ctx->style.edit.padding.y = pad_y;
    state = nk_edit_string(ctx, flags, buf, len, cap, filter);
    pop_style(ctx, f);
    stroke_edit_edge(ctx, bounds, &s);
    note_ime_caret(app, ctx, bounds, state, &ctx->text_edit);
    reaktor_note(app, REAKTOR_A11Y_TEXTBOX,
                 hint ? hint : ((flags & NK_EDIT_BOX) ? "Notes" : "Text"), buf,
                 state & NK_EDIT_ACTIVE ? REAKTOR_A11Y_FOCUSED : 0u, bounds);

    if (hint && *len == 0) draw_hint(ctx, bounds, hint, &s);
    return state;
}

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
        SDL_strlcpy(app->file_answer, "canceled", sizeof(app->file_answer));
    else
        SDL_strlcpy(app->file_answer, filelist[0], sizeof(app->file_answer));

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

#define A11Y_DUMP_FRAME 2

void
a11y_dump_once(App *app)
{
    static int done, frames;
    const char *path;
    FILE *f;

    if (done) return;
    path = app->dump_path;
    if (!path) { done = 1; return; }
    if (++frames < A11Y_DUMP_FRAME || !reaktor_frame_settled()) {
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

/* The face a rule asks for, falling back to a size named in the code.
 *
 * Every caller of this used to be a bare reaktor_font(app, 13, 0) - a number
 * the stylesheet could not reach, on a page claiming the look lives in CSS. */
const struct nk_user_font *
reaktor_style_font(App *app, const char *selector, int px, int bold)
{
    reaktor_style st;

    if (selector) {
        reaktor_style_get(selector, &st);
        if (st.matched && st.font_px > 0)
            return reaktor_font(app, st.font_px, st.bold ? 1 : bold);
    }
    return reaktor_font(app, px, bold);
}

float
reaktor_popup_rounding(void)
{
    reaktor_style dlg;
    reaktor_style_get("dialog", &dlg);
    if (!dlg.matched) return 6.0f;
    return dlg.rounding > 8.0f ? 8.0f : dlg.rounding;
}

/* A menu popup's chrome. The row spacing here is not cosmetic: it is the 2
 * that reaktor_menu_height counts, and a popup opened without it is a row
 * short of the height it asked for, which leaves the last item on the
 * clipped edge with nothing to click. */
void
reaktor_menu_style_push(struct nk_context *ctx)
{
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(
                                 reaktor_token("--background",
                                               nk_rgb(53, 53, 53))));
    nk_style_push_float(ctx, &ctx->style.window.rounding,
                        reaktor_popup_rounding());
    nk_style_push_vec2(ctx, &ctx->style.window.spacing,
                       nk_vec2(4.0f, REAKTOR_MENU_GAP));
}

void
reaktor_menu_style_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}

float
reaktor_menu_height(int rows)
{
    return rows * REAKTOR_MENU_ROW + (rows - 1) * REAKTOR_MENU_GAP + 10.0f;
}

/* One menu row: the pointer cursor, the accessibility node and the item
 * itself, plus an accelerator drawn at the right edge when there is one. */
int
reaktor_menu_item(App *app, struct nk_context *ctx, const char *label,
                  const char *accel, int contextual)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    int hit;

    reaktor_hot_top(app, b, 1, 1);
    reaktor_note(app, REAKTOR_A11Y_MENUITEM, label, NULL, 0u, b);

    hit = contextual ? nk_contextual_item_label(ctx, label, NK_TEXT_LEFT)
                     : nk_menu_item_label(ctx, label, NK_TEXT_LEFT);

    if (accel && *accel) {
        const struct nk_user_font *f = ctx->style.font;
        int n = (int)SDL_strlen(accel);
        float w = f->width(f->userdata, f->height, accel, n);
        struct nk_rect r = nk_rect(b.x + b.w - w - 6.0f,
                                   b.y + (b.h - f->height) * 0.5f,
                                   w, f->height);

        nk_draw_text(nk_window_get_canvas(ctx), r, accel, n, f,
                     nk_rgba(0, 0, 0, 0),
                     reaktor_token("--text-muted", nk_rgb(160, 160, 160)));
    }
    return hit;
}
int
reaktor_css_override(App *app, int on)
{
    int off = !on;

    if (off != app->css_override_off) {
        app->css_override_off = off;
        load_theme(app);
        apply_widget_style(app);
        app->dirty = 1;
    }
    return !app->css_override_off;
}

int
reaktor_css_override_on(App *app)
{
    return !app->css_override_off;
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
    out->aa = !app->aa ? "off"
            : !app->renderer_is_sw ? "on"
            : app->sw_noaa ? "off (software renderer)"
            : "strokes only (software renderer)";
    out->dark       = app->dark;
    out->sheets     = app->sheets;
    out->scale      = reaktor_scale();
    out->style_ms   = app->style_ms_x100 / 100.0f;
    out->frame_gap_ms = app->frame_gap_ms;
    out->cpu_ms_per_frame = app->cpu_ms_per_frame;
    out->hover_gap_ms     = (int)g_hover_gap_ms;

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
        /* Sampled twice a second, not per frame. On Linux this parses
         * /proc/self/statm and /proc/self/smaps_rollup, and the second makes
         * the kernel walk every mapping - on the one page whose job is to
         * report honest frame timings, which it would then be inflating. */
        static size_t   rss, priv;
        static unsigned taken;
        unsigned        now = SDL_GetTicks();

        if (!taken || now - taken >= 500) {
            reaktor_process_memory(&rss, &priv);
            taken = now ? now : 1u;
        }
        out->rss_bytes     = (unsigned long)rss;
        out->private_bytes = (unsigned long)priv;
    }
    out->atlas_w    = app->atlas_w;
    out->atlas_h    = app->atlas_h;
    out->atlas_bpp  = app->atlas_bpp;
    {
        int i;
        for (i = 0; i < RSS_STEPS; i++) {
            out->rss_at[i]  = (unsigned long)reaktor_rss[i];
            out->priv_at[i] = (unsigned long)reaktor_priv[i];
        }
    }
    out->build_ms   = app->build_ms_x100 / 100.0f;
    out->render_ms  = app->render_ms_x100 / 100.0f;
    out->present_ms = app->present_ms_x100 / 100.0f;
}
