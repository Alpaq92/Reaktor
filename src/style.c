/* style.c - see style.h. */
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

/* A selector is resolved on every frame Nuklear draws, and the answer only
 * changes when the stylesheets are reloaded, so results are memoised. The set
 * of selectors a screen uses is a couple of dozen at most, which is why a
 * linear scan is the right shape here. */
#define CACHE_MAX 64

typedef struct cache_entry {
    char        selector[64];
    curie_style style;
} cache_entry;

static struct {
    int            ready;
    cache_entry    cache[CACHE_MAX];
    int            cache_n;
    css_metrics_t  metrics;
    curie_cssvars *vars;      /* the palette, kept for curie_style_token */
} g;

static void rgba_of(css_color_value_t c, unsigned char out[4])
{
    out[0] = (unsigned char)((c >> 16) & 0xff);   /* 0xAARRGGBB */
    out[1] = (unsigned char)((c >> 8) & 0xff);
    out[2] = (unsigned char)(c & 0xff);
    out[3] = (unsigned char)((c >> 24) & 0xff);
}

int curie_style_init(const char *const *css_paths, int count)
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
    }
    g.cache_n = 0;

    /* The custom properties are kept, not discarded: tiny.css puts its whole
     * palette in :root, and the surfaces Nuklear paints itself - the window
     * background, the card - read from the same declarations its rules do.
     *
     * No [data-theme] block to select: tiny.css ships light and dark as two
     * :root files, so the theme is chosen by which one the caller passes. */
    curie_cssvars_free(g.vars);
    g.vars = NULL;
    flat = curie_css_flatten(css_paths, count, NULL, &g.vars);
    if (!flat) return 0;

    parser = css_parser_create("curie");
    if (!parser) { free(flat); return 0; }
    /* css_parser_parse consumes at most its buffer per call and returns how
     * much it took, so it is driven to the end of the string. */
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
    return 1;
}

void curie_style_shutdown(void)
{
    if (!g.ready) return;
    curie_cssvars_free(g.vars);
    g.vars = NULL;
    css_destroy();
    g.ready = 0;
    g.cache_n = 0;
}

int curie_style_token(const char *name, unsigned char rgba[4])
{
    return curie_cssvars_color(g.vars, name, rgba);
}

void curie_style_darken(unsigned char rgba[4], float amount)
{
    int i;

    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    for (i = 0; i < 3; i++)
        rgba[i] = (unsigned char)(rgba[i] * (1.0f - amount) + 0.5f);
}

static void resolve(const char *selector, curie_style *out)
{
    css_selector_t *sel;
    css_style_decl_t *decl;
    css_computed_style_t computed;
    static const css_computed_style_t no_parent;

    memset(out, 0, sizeof(*out));
    if (!g.ready || !selector) return;

    sel = css_selector_create(selector);
    if (!sel) return;

    decl = css_select_style(sel);          /* caller owns the result */
    css_selector_destroy(sel);
    if (!decl) return;

    memset(&computed, 0, sizeof(computed));
    css_cascade_style(decl, &computed);
    /* Resolves percentages and any remaining relative lengths. There is no
     * parent here - each widget is styled on its own - so a zeroed one stands
     * in, which is also why cssflat resolves em before the engine sees it. */
    css_compute_absolute_values(&no_parent, &computed, &g.metrics);

    rgba_of(computed.background_color, out->bg);
    rgba_of(computed.color, out->fg);
    rgba_of(computed.border_top_color, out->border_col);
    out->border   = computed.border_top_width;
    out->rounding = computed.border_top_left_radius;
    out->pad_x    = computed.padding_left;
    out->pad_y    = computed.padding_top;
    out->font_px  = computed.font_size > 0.0f
                  ? (int)(computed.font_size + 0.5f) : 0;
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
    /* An empty declaration list means nothing in the stylesheets selected
     * this, which the caller needs to tell apart from "selected, and every
     * value happens to be zero". */
    out->matched = decl->length > 0;

    css_style_decl_destroy(decl);
    css_computed_style_destroy(&computed);
}

void curie_style_get(const char *selector, curie_style *out)
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
