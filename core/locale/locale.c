#include "locale_internal.h"

#include <stdlib.h>
#include <string.h>

#ifndef REAKTOR_LOCALE_EMBEDDED
#include <SDL3/SDL.h>
#include "reaktor.h"
#endif

#define LOCALE_MAX  32
#define LOCALE_CODE 16

typedef struct locale_entry {
    const char *key, *value;
    int         line;
} locale_entry;

typedef struct locale_catalog {
    char          code[LOCALE_CODE];
    char         *data;
    locale_entry *entry;
    int           count;
    const char   *name;
} locale_catalog;

static locale_catalog g_cat[LOCALE_MAX];
static int            g_count;
static int            g_current = -1;

static int
by_code(const void *a, const void *b)
{
    return strcmp(((const locale_catalog *)a)->code,
                  ((const locale_catalog *)b)->code);
}

static int
by_key(const void *a, const void *b)
{
    const locale_entry *x = a, *y = b;
    int c = strcmp(x->key, y->key);

    return c ? c : x->line - y->line;
}

static char *
trim(char *s, char *end)
{
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    *end = '\0';
    return s;
}

static void
unescape(char *s)
{
    char *w = s;

    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n')       { *w++ = '\n'; s++; }
        else if (s[0] == '\\' && s[1] == '\\') { *w++ = '\\'; s++; }
        else                                    *w++ = *s;
    }
    *w = '\0';
}

static int
parse(locale_catalog *c, char *data)
{
    char *s = data, *next;
    int   lines = 1, n = 0, i;

    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        s += 3;
    for (next = s; *next; next++) lines += *next == '\n';

    c->data  = data;
    c->entry = calloc((size_t)lines, sizeof *c->entry);
    if (!c->entry) return 0;

    for (i = 0; *s; i++, s = next) {
        char *eol = strchr(s, '\n');
        char *sep;

        next = eol ? eol + 1 : s + strlen(s);
        if (!eol) eol = next;
        *eol = '\0';

        sep = strstr(s, " = ");
        if (!sep) continue;
        c->entry[n].key   = trim(s, sep);
        c->entry[n].value = trim(sep + 3, eol);
        c->entry[n].line  = i;
        if (!*c->entry[n].key) continue;
        unescape((char *)c->entry[n].value);
        n++;
    }

    qsort(c->entry, (size_t)n, sizeof *c->entry, by_key);
    for (i = 0, c->count = 0; i < n; i++) {
        if (c->count && !strcmp(c->entry[c->count - 1].key, c->entry[i].key))
            continue;
        c->entry[c->count++] = c->entry[i];
    }
    return 1;
}

static const char *
lookup(const locale_catalog *c, const char *key)
{
    int lo = 0, hi = c->count - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(key, c->entry[mid].key);

        if (!cmp) return c->entry[mid].value;
        if (cmp < 0) hi = mid - 1;
        else         lo = mid + 1;
    }
    return NULL;
}

static void
add_catalog(const char *code, char *data)
{
    locale_catalog *c;

    if (g_count >= LOCALE_MAX || strlen(code) >= LOCALE_CODE) {
        free(data);
        return;
    }
    c = &g_cat[g_count];
    memset(c, 0, sizeof *c);
    strcpy(c->code, code);
    if (!parse(c, data)) {
        free(data);
        return;
    }
    c->name = lookup(c, "language");
    if (!c->name) c->name = c->code;
    g_count++;
}

static void
release(void)
{
    int i;

    for (i = 0; i < g_count; i++) {
        free(g_cat[i].entry);
        free(g_cat[i].data);
    }
    g_count   = 0;
    g_current = -1;
}

#ifdef REAKTOR_LOCALE_EMBEDDED
static void
load_all(void)
{
    int i;

    for (i = 0; i < reaktor_locale_catalog_count; i++) {
        const struct reaktor_locale_catalog *e = &reaktor_locale_catalogs[i];
        char *data = malloc(e->size + 1);

        if (!data) continue;
        memcpy(data, e->data, e->size);
        data[e->size] = '\0';
        add_catalog(e->code, data);
    }
}
#else
static void
load_all(void)
{
    char   dir[1024], path[1024];
    char **names;
    int    i, n = 0;

    if (!reaktor_path(dir, sizeof dir, "assets/locale")) return;
    names = SDL_GlobDirectory(dir, "*.txt", 0, &n);
    if (!names) return;

    for (i = 0; i < n; i++) {
        const char *dot = strrchr(names[i], '.');
        char        code[LOCALE_CODE];
        size_t      len = dot ? (size_t)(dot - names[i]) : 0;
        char       *data;

        if (len == 0 || len >= sizeof code) continue;
        memcpy(code, names[i], len);
        code[len] = '\0';

        SDL_snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        data = reaktor_read_file(path, NULL);
        if (data) add_catalog(code, data);
    }
    SDL_free(names);
}
#endif

int
reaktor_locale_init(void)
{
    release();
    load_all();
    qsort(g_cat, (size_t)g_count, sizeof *g_cat, by_code);
    return g_count;
}

int
reaktor_locale_count(void)
{
    return g_count;
}

int
reaktor_locale_find(const char *code)
{
    int i;

    for (i = 0; code && i < g_count; i++)
        if (!strcmp(g_cat[i].code, code)) return i;
    return -1;
}

const char *
reaktor_locale_code(int index)
{
    return index >= 0 && index < g_count ? g_cat[index].code : NULL;
}

const char *
reaktor_locale_name(int index)
{
    return index >= 0 && index < g_count ? g_cat[index].name : NULL;
}

int
reaktor_locale_set(const char *code)
{
    int i = reaktor_locale_find(code);

    if (i < 0) return 0;
    g_current = i;
    return 1;
}

const char *
reaktor_locale_current(void)
{
    return g_current >= 0 ? g_cat[g_current].code : "";
}

const char *
reaktor_tr(const char *key)
{
    const char *v;

    if (!key || g_current < 0) return key;
    v = lookup(&g_cat[g_current], key);
    return v ? v : key;
}

static int
utf8_next(const unsigned char *s, unsigned *cp)
{
    int len, i;

    if (s[0] < 0x80)              { *cp = s[0];        return 1; }
    else if ((s[0] & 0xE0) == 0xC0) { *cp = s[0] & 0x1F; len = 2; }
    else if ((s[0] & 0xF0) == 0xE0) { *cp = s[0] & 0x0F; len = 3; }
    else if ((s[0] & 0xF8) == 0xF0) { *cp = s[0] & 0x07; len = 4; }
    else return 0;

    for (i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        *cp = (*cp << 6) | (s[i] & 0x3F);
    }
    return len;
}

static int
by_codepoint(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;

    return x < y ? -1 : x > y;
}

static void
scan(const char *text, unsigned **set, int *n, int *cap)
{
    const unsigned char *s = (const unsigned char *)text;

    while (*s) {
        unsigned cp = 0;
        int      len = utf8_next(s, &cp);

        if (!len) { s++; continue; }
        s += len;
        if (cp <= 0xFF) continue;
        if (*n == *cap) {
            int       grow = *cap ? *cap * 2 : 256;
            unsigned *more = realloc(*set, (size_t)grow * sizeof **set);

            if (!more) return;
            *set = more;
            *cap = grow;
        }
        (*set)[(*n)++] = cp;
    }
}

int
reaktor_locale_codepoints(unsigned *out, int cap)
{
    unsigned *set = NULL;
    int       n = 0, room = 0, unique = 0, i, j;

    for (i = 0; i < g_count; i++)
        for (j = 0; j < g_cat[i].count; j++) {
            scan(g_cat[i].entry[j].key, &set, &n, &room);
            scan(g_cat[i].entry[j].value, &set, &n, &room);
        }

    if (n) qsort(set, (size_t)n, sizeof *set, by_codepoint);
    for (i = 0; i < n; i++) {
        if (unique && set[i] == set[i - 1]) continue;
        if (out && unique < cap) out[unique] = set[i];
        unique++;
    }
    free(set);
    return unique;
}
