#include "style.h"
#include "cssflat.h"

#include <css.h>
#include <css/parser.h>
#include <css/library.h>
#include <css/selector.h>
#include <css/computed.h>
#include <css/style_decl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_MAX 64

typedef struct cache_entry {
    char        selector[64];
    reaktor_style style;
} cache_entry;

static struct {
    int            ready;
    cache_entry    cache[CACHE_MAX];
    int            cache_n;
    css_metrics_t  metrics;
    reaktor_cssvars *vars;
    /* The sheet's own base, resolved once from `body`, and the parent every
     * other lookup is computed against. Without it `em` and `rem` fall back
     * to the 16px initial value: simple.css says `body { font-size: 1.15rem }`,
     * so its 0.5em padding is 9.2px and we were computing 8.0 - a 15% error
     * on every length in the sheet, compounding into every widget's size. */
    css_computed_style_t root;
    int            root_ready;
} g;

static void rgba_of(css_color_value_t c, unsigned char out[4])
{
    out[0] = (unsigned char)((c >> 16) & 0xff);
    out[1] = (unsigned char)((c >> 8) & 0xff);
    out[2] = (unsigned char)(c & 0xff);
    out[3] = (unsigned char)((c >> 24) & 0xff);
}

/* Drop the base style. Called before every cascade into it and on the two
 * failure paths, so a half-initialized sheet cannot leave the previous
 * sheet's font size in place as the parent for the next one. */
static void base_reset(void)
{
    if (g.root_ready) css_computed_style_destroy(&g.root);
    memset(&g.root, 0, sizeof(g.root));
    g.root_ready = 0;
}

int reaktor_style_init(const char *const *css_paths, int count,
                       const char *theme)
{
    char *flat;
    css_parser_t *parser;

    if (!g.ready) {
        css_init();
        g.metrics.dpi = 96.0f;
        g.metrics.density = 1.0f;
        g.metrics.scaled_density = 1.0f;
        g.metrics.scale = 1.0f;
        g.ready = 1;
    } else {
        css_destroy_library();
        css_init_library();
    }
    g.cache_n = 0;
    base_reset();

    reaktor_cssvars_free(g.vars);
    g.vars = NULL;
    flat = reaktor_css_flatten(css_paths, count, theme, &g.vars);
    if (!flat) return 0;

    parser = css_parser_create("reaktor");
    if (!parser) { free(flat); return 0; }
    {
        const char *cur = flat;
        size_t len = 1;
        while (len > 0) {
            len = css_parser_parse(parser, cur);
            cur += len;
        }
    }
    css_parser_destroy(parser);
    free(flat);

    /* Now that the rules are in, establish the base every other lookup is
     * computed against.
     *
     * One cascade, not one per element. css_cascade_style writes the initial
     * value for every property the declaration leaves out, so cascading
     * `html` and then `body` into the same computed style erased the first
     * pass rather than layering on it - and it allocates font_family without
     * freeing what was there, which leaked on every theme change. `body` is
     * the one that matters: it is what a document's elements inherit, and it
     * is where a sheet puts its size. */
    base_reset();
    {
        static const css_computed_style_t none;
        css_selector_t *bs = css_selector_create("body");

        if (bs) {
            css_style_decl_t *bd = css_select_style(bs);

            css_selector_destroy(bs);
            if (bd) {
                css_cascade_style(bd, &g.root);
                css_style_decl_destroy(bd);
            }
        }
        css_compute_absolute_values(&none, &g.root, &g.metrics);
    }
    g.root_ready = 1;
    return 1;
}

void reaktor_style_shutdown(void)
{
    if (!g.ready) return;
    reaktor_cssvars_free(g.vars);
    g.vars = NULL;
    if (g.root_ready) {
        css_computed_style_destroy(&g.root);
        g.root_ready = 0;
    }
    css_destroy();
    g.ready = 0;
    g.cache_n = 0;
}

int reaktor_style_token(const char *name, unsigned char rgba[4])
{
    return reaktor_cssvars_color(g.vars, name, rgba);
}

void reaktor_style_darken(unsigned char rgba[4], float amount)
{
    int i;

    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    for (i = 0; i < 3; i++)
        rgba[i] = (unsigned char)(rgba[i] * (1.0f - amount) + 0.5f);
}

/* A length libcss resolved to pixels, or 0.
 *
 * It cannot resolve a percentage without a parent of known width, and when it
 * gives up it leaves the number alone with the unit still set - so `100%`
 * arrives as the float 100. Anything not in px is not a length we can use. */
static float px_of(css_numeric_value_t v, css_unit_t unit)
{
    return unit == CSS_UNIT_PX ? v : 0.0f;
}

static void resolve(const char *selector, reaktor_style *out)
{
    css_selector_t *sel;
    css_style_decl_t *decl;
    css_computed_style_t computed;

    memset(out, 0, sizeof(*out));
    if (!g.ready || !selector) return;

    sel = css_selector_create(selector);
    if (!sel) return;

    decl = css_select_style(sel);
    css_selector_destroy(sel);
    if (!decl) return;

    memset(&computed, 0, sizeof(computed));
    css_cascade_style(decl, &computed);
    css_compute_absolute_values(&g.root, &computed, &g.metrics);

    rgba_of(computed.background_color, out->bg);
    rgba_of(computed.color, out->fg);
    rgba_of(computed.border_top_color, out->border_col);
    out->border   = px_of(computed.border_top_width,
                          computed.unit_bits.border_top_width);
    out->rounding = px_of(computed.border_top_left_radius,
                          computed.unit_bits.border_top_left_radius);
    out->pad_x    = px_of(computed.padding_left,
                          computed.unit_bits.padding_left);
    out->pad_y    = px_of(computed.padding_top,
                          computed.unit_bits.padding_top);

    out->pad[REAKTOR_SIDE_TOP]    = out->pad_y;
    out->pad[REAKTOR_SIDE_RIGHT]  = px_of(computed.padding_right,
                                          computed.unit_bits.padding_right);
    out->pad[REAKTOR_SIDE_BOTTOM] = px_of(computed.padding_bottom,
                                          computed.unit_bits.padding_bottom);
    out->pad[REAKTOR_SIDE_LEFT]   = out->pad_x;

    out->border_w[REAKTOR_SIDE_TOP]    = out->border;
    out->border_w[REAKTOR_SIDE_RIGHT]  =
        px_of(computed.border_right_width,
              computed.unit_bits.border_right_width);
    out->border_w[REAKTOR_SIDE_BOTTOM] =
        px_of(computed.border_bottom_width,
              computed.unit_bits.border_bottom_width);
    out->border_w[REAKTOR_SIDE_LEFT]   =
        px_of(computed.border_left_width,
              computed.unit_bits.border_left_width);

    out->radius[REAKTOR_CORNER_TL] = out->rounding;
    out->radius[REAKTOR_CORNER_TR] =
        px_of(computed.border_top_right_radius,
              computed.unit_bits.border_top_right_radius);
    out->radius[REAKTOR_CORNER_BR] =
        px_of(computed.border_bottom_right_radius,
              computed.unit_bits.border_bottom_right_radius);
    out->radius[REAKTOR_CORNER_BL] =
        px_of(computed.border_bottom_left_radius,
              computed.unit_bits.border_bottom_left_radius);

    out->margin[REAKTOR_SIDE_TOP]    = px_of(computed.margin_top,
                                             computed.unit_bits.margin_top);
    out->margin[REAKTOR_SIDE_RIGHT]  = px_of(computed.margin_right,
                                             computed.unit_bits.margin_right);
    out->margin[REAKTOR_SIDE_BOTTOM] =
        px_of(computed.margin_bottom, computed.unit_bits.margin_bottom);
    out->margin[REAKTOR_SIDE_LEFT]   = px_of(computed.margin_left,
                                             computed.unit_bits.margin_left);

    out->width       = px_of(computed.width, computed.unit_bits.width);
    out->height      = px_of(computed.height, computed.unit_bits.height);
    out->min_width   = px_of(computed.min_width,
                             computed.unit_bits.min_width);
    out->min_height  = px_of(computed.min_height,
                             computed.unit_bits.min_height);
    out->font_px     = computed.font_size > 0.0f
                     ? (int)(computed.font_size + 0.5f) : 0;
    out->max_width   = px_of(computed.max_width,
                             computed.unit_bits.max_width);
    out->max_height  = px_of(computed.max_height,
                             computed.unit_bits.max_height);

    /* Only when this rule states it. line-height inherits in CSS, but libcss
     * reports an explicit `line-height: normal` and an absent declaration
     * identically - and simple.css sets `normal` on every control, which is
     * why the browser computes 18.4px/normal for a button while body is 1.5.
     * Inheriting body's ratio made every control taller than the browser
     * draws it.
     *
     * The kind comes from libcss's own tag, not from the size of the number:
     * `line-height: 150%` is the same thing as 1.5 and would otherwise have
     * been taken for a 150px line box. line-height is not in the property
     * list css_compute_absolute_values walks, so a length is only usable if
     * it was already written in px. */
    out->line_height = 0.0f;
    switch (computed.type_bits.line_height) {
    case CSS_LINE_HEIGHT_NUMBER: {
        float fs = computed.font_size > 0.0f ? computed.font_size
                                             : g.root.font_size;

        out->line_height = computed.line_height * (fs > 0.0f ? fs : 16.0f);
        break;
    }
    case CSS_LINE_HEIGHT_DIMENSION:
        out->line_height = px_of(computed.line_height,
                                 computed.unit_bits.line_height);
        break;
    default:
        break;
    }

    switch (computed.type_bits.font_weight) {
    case CSS_FONT_WEIGHT_BOLD:
    case CSS_FONT_WEIGHT_700:
    case CSS_FONT_WEIGHT_800:
    case CSS_FONT_WEIGHT_900:
        out->bold = 1;
        break;
    default:
        out->bold = 0;
        break;
    }
    out->matched = decl->length > 0;

    css_style_decl_destroy(decl);
    css_computed_style_destroy(&computed);
}

void reaktor_style_get(const char *selector, reaktor_style *out)
{
    int i;

    if (!out) return;
    for (i = 0; i < g.cache_n; i++) {
        if (strcmp(g.cache[i].selector, selector) == 0) {
            *out = g.cache[i].style;
            return;
        }
    }
    resolve(selector, out);
    if (g.cache_n < CACHE_MAX && strlen(selector) < sizeof(g.cache[0].selector)) {
        snprintf(g.cache[g.cache_n].selector,
                 sizeof(g.cache[0].selector), "%s", selector);
        g.cache[g.cache_n].style = *out;
        g.cache_n++;
    }
}
