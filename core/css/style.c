#include "style.h"
#include "css.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct cache_entry {
    char         *selector;
    unsigned      hash;
    reaktor_style style;
} cache_entry;

static struct {
    reaktor_sheet *sheet;
    cache_entry   *cache;
    unsigned       cache_cap, cache_n;
    unsigned       generation;
} g;

static void cache_clear(int release)
{
    unsigned i;

    for (i = 0; i < g.cache_cap; i++) {
        free(g.cache[i].selector);
        g.cache[i].selector = NULL;
    }
    g.cache_n = 0;
    if (release) {
        free(g.cache);
        g.cache     = NULL;
        g.cache_cap = 0;
    }
}

static cache_entry *cache_slot(const char *selector, unsigned h)
{
    unsigned i = h & (g.cache_cap - 1);

    while (g.cache[i].selector && (g.cache[i].hash != h ||
                                   strcmp(g.cache[i].selector, selector)))
        i = (i + 1) & (g.cache_cap - 1);
    return &g.cache[i];
}

static void cache_grow(void)
{
    cache_entry *old = g.cache;
    unsigned     n = g.cache_cap, i;
    cache_entry *grown = calloc(n ? n * 2 : 128, sizeof *grown);

    if (!grown) return;
    g.cache     = grown;
    g.cache_cap = n ? n * 2 : 128;
    for (i = 0; i < n; i++)
        if (old[i].selector) *cache_slot(old[i].selector, old[i].hash) = old[i];
    free(old);
}

int reaktor_style_init(const char *const *css_paths, int count,
                       const char *theme)
{
    reaktor_sheet *sheet = reaktor_sheet_load(css_paths, count, theme);

    cache_clear(0);
    g.generation++;
    reaktor_sheet_free(g.sheet);
    g.sheet = sheet;
    return sheet != NULL;
}

void reaktor_style_shutdown(void)
{
    cache_clear(1);
    reaktor_sheet_free(g.sheet);
    g.sheet = NULL;
}

unsigned reaktor_style_loads(void)
{
    return g.generation;
}

void reaktor_style_darken(unsigned char rgba[4], float amount)
{
    int i;

    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    for (i = 0; i < 3; i++)
        rgba[i] = (unsigned char)(rgba[i] * (1.0f - amount) + 0.5f);
}

/* Selectors and "--" tokens share the cache; a token keeps its color in bg. */
static const reaktor_style *cached(const char *key)
{
    unsigned     h = reaktor_fnv(REAKTOR_FNV, key);
    size_t       len;
    cache_entry *e;

    if (g.cache_cap && (e = cache_slot(key, h))->selector) return &e->style;
    if (2 * (g.cache_n + 1) > g.cache_cap) cache_grow();
    if (g.cache_n + 1 >= g.cache_cap) return NULL;
    len = strlen(key) + 1;
    e   = cache_slot(key, h);
    if (!(e->selector = malloc(len))) return NULL;
    memcpy(e->selector, key, len);
    e->hash = h;
    if (key[0] == '-' && key[1] == '-') {
        memset(&e->style, 0, sizeof e->style);
        e->style.matched = reaktor_sheet_color(g.sheet, key, e->style.bg);
    } else {
        reaktor_sheet_style(g.sheet, key, &e->style);
    }
    g.cache_n++;
    return &e->style;
}

int reaktor_style_token(const char *name, unsigned char rgba[4])
{
    const reaktor_style *c;

    if (!name || name[0] != '-' || name[1] != '-') return 0;
    if (!(c = cached(name))) return reaktor_sheet_color(g.sheet, name, rgba);
    if (c->matched) memcpy(rgba, c->bg, 4);
    return c->matched;
}

void reaktor_style_get(const char *selector, reaktor_style *out)
{
    const reaktor_style *c;

    if (!out) return;
    if (selector && (c = cached(selector)) != NULL) *out = *c;
    else reaktor_sheet_style(g.sheet, selector, out);
}
