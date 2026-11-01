#include "css.h"
#include <plutovg.h>
#include "util.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH     8
#define MAX_NEG       8
#define MAX_NAMES     32
#define VAR_MAX       4096
#define VAR_DEPTH     32
#define VAR_STEPS     1024
#define CALC_DEPTH    32
#define BLOCK_DEPTH   16
#define IMPORT_DEPTH  8
#define IMPORT_LOADS  256
#define IMPORT_BUDGET (4u << 20)

enum { SPEC_ID = 0x10000, SPEC_CLASS = 0x100, SPEC_TYPE = 1 };

enum {
    P_BG, P_FG, P_BC,
    P_BW, P_BS = P_BW + 4, P_RAD = P_BS + 4, P_PAD = P_RAD + 4, P_MAR = P_PAD + 4,
    P_W = P_MAR + 4, P_H, P_MINW, P_MINH, P_MAXW, P_MAXH,
    P_FS, P_FW, P_LH, P_OP, P_GFROM, P_GTO, P_COUNT
};

enum { F_IMPORTANT = 1, F_INITIAL = 2, F_CURRENT = 4, F_NUMBER = 8 };

typedef struct buf { char *p; size_t len, cap; } buf;

typedef struct compound {
    unsigned      type, name, neg;
    unsigned char n, nn, child, enabled;
} compound;

typedef struct selector {
    compound c[MAX_DEPTH], neg[MAX_NEG];
    unsigned nm[MAX_NAMES];
    unsigned n, nneg, nnm, spec;
} selector;

/* Compounds whose name and negation offsets index nm and neg. */
typedef struct pool {
    const compound *c, *neg;
    const unsigned *nm;
} pool;

typedef struct decl {
    float         v;
    unsigned char rgba[4];
    unsigned char prop, flags;
} decl;

typedef struct rule {
    unsigned      c0, spec, next, blk;
    unsigned char n;
} rule;

/* Rules by the type or first name of their last compound, chained through next. */
typedef struct bucket { unsigned key, first; } bucket;

typedef struct var { char *name, *value; size_t n; unsigned h; } var;

/* A rule's selector list and body in the sheet's text; the body is read into
   declarations when a query first matches one of its selectors. */
typedef struct block {
    size_t        ss, se, bs, be;
    unsigned      d0, nd;
    unsigned char read;
} block;

struct reaktor_sheet {
    rule     *rule;  unsigned nrule, caprule;
    compound *cmp;   unsigned ncmp, capcmp;
    compound *neg;   unsigned nneg, capneg;
    unsigned *nm;    unsigned nnm, capnm;
    decl     *decl;  unsigned ndecl, capdecl;
    var      *var;   unsigned nvar, capvar;
    unsigned *slot;  unsigned capslot;
    bucket   *bucket; unsigned capbucket, any;
    block    *blk;   unsigned nblk, capblk;
    buf       src, tmp;
    char      theme[16];
    float     rem, em;
};

typedef struct ctx {
    reaktor_sheet *s;
    buf           *tmp;
    unsigned char  flags;
    uint64_t       key, best;
    float          rel, size;
} ctx;

static int g_reduce_motion, g_more_contrast;
static void (*g_seen)(const char *path);

void reaktor_css_media(int reduce_motion, int more_contrast)
{
    g_reduce_motion = reduce_motion;
    g_more_contrast = more_contrast;
}

void reaktor_css_track(void (*seen)(const char *path)) { g_seen = seen; }

static int grow(void **p, unsigned *cap, unsigned need, size_t size)
{
    unsigned n = *cap ? *cap : 16;
    void    *q;

    if (need <= *cap) return 1;
    while (n < need && n <= UINT_MAX / 2) n *= 2;
    if (n < need || n > SIZE_MAX / size || !(q = realloc(*p, n * size))) return 0;
    *p   = q;
    *cap = n;
    return 1;
}

#define ROOM(o, arr, n, cap, extra) grow((void **)&(o)->arr, &(o)->cap, (o)->n + (extra), sizeof *(o)->arr)

static int buf_add(buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 4096;
        char  *np;

        if (n > SIZE_MAX / 2 - b->len) return 0;
        while (c < b->len + n + 1) c *= 2;
        if (!(np = (char *)realloc(b->p, c))) return 0;
        b->p   = np;
        b->cap = c;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 1;
}

static int buf_str(buf *b, const char *s) { return buf_add(b, s, strlen(s)); }

static int space(char c) { return isspace((unsigned char)c); }

static void trim(const char *s, size_t *a, size_t *b)
{
    while (*a < *b && space(s[*a])) (*a)++;
    while (*b > *a && space(s[*b - 1])) (*b)--;
}

static int word(const char *s, size_t n, const char *w)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (!w[i] || tolower((unsigned char)s[i]) != w[i]) return 0;
    return !w[n];
}

static int holds(const char *s, size_t n, const char *w)
{
    size_t k = strlen(w), i;

    for (i = 0; i + k <= n; i++)
        if (memcmp(s + i, w, k) == 0) return 1;
    return 0;
}

static unsigned hash(char kind, const char *s, size_t n)
{
    unsigned h = reaktor_fnv_n(reaktor_fnv_n(REAKTOR_FNV, &kind, 1), s, n);

    return h ? h : 1;
}

static size_t skip_string(const char *s, size_t i, size_t n)
{
    char q = s[i++];

    while (i < n && s[i] != q) i += s[i] == '\\' ? 2 : 1;
    return i < n ? i + 1 : n;
}

/* The first of `stops` at bracket depth 0, or n. */
static size_t scan(const char *s, size_t i, size_t n, const char *stops)
{
    int depth = 0;

    while (i < n) {
        char c = s[i];

        if (c == '"' || c == '\'') { i = skip_string(s, i, n); continue; }
        if (!depth && strchr(stops, c)) return i;
        if (c == '(' || c == '[') depth++;
        else if ((c == ')' || c == ']') && depth) depth--;
        i++;
    }
    return n;
}

static size_t block_end(const char *s, size_t i, size_t n)
{
    int depth = 1;

    while (i < n) {
        if (s[i] == '"' || s[i] == '\'') { i = skip_string(s, i, n); continue; }
        if (s[i] == '{') depth++;
        else if (s[i] == '}' && !--depth) return i;
        i++;
    }
    return n;
}

/* The next comma-separated item of s[*a..e), trimmed. */
static int next_item(const char *s, size_t *a, size_t e, size_t *ps, size_t *pe)
{
    if (*a >= e) return 0;
    *ps = *a;
    *pe = *a = scan(s, *a, e, ",");
    if (*a < e) (*a)++;
    trim(s, ps, pe);
    return 1;
}

static void strip_comments(char *s)
{
    char *r = s, *w = s;

    while (*r) {
        if (*r == '"' || *r == '\'') {
            char q = *r;

            *w++ = *r++;
            while (*r && *r != q) {
                if (*r == '\\' && r[1]) *w++ = *r++;
                *w++ = *r++;
            }
            if (*r) *w++ = *r++;
        } else if (r[0] == '/' && r[1] == '*') {
            r += 2;
            while (*r && !(r[0] == '*' && r[1] == '/')) r++;
            if (*r) r += 2;
            *w++ = ' ';
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

/* Not strtod, which reads the locale's decimal mark. */
static double number(const char *p, const char **end)
{
    double v = 0.0, scale = 1.0;
    const char *q = p;
    int neg = 0, digits = 0, ex = 0, eneg = 0;

    if (*q == '+' || *q == '-') neg = *q++ == '-';
    for (; *q >= '0' && *q <= '9'; q++, digits++) v = v * 10.0 + (*q - '0');
    if (*q == '.')
        for (q++; *q >= '0' && *q <= '9'; q++, digits++)
            v += (*q - '0') * (scale *= 0.1);
    if (!digits) { *end = p; return 0.0; }
    if ((*q == 'e' || *q == 'E') &&
        (isdigit((unsigned char)q[1]) ||
         ((q[1] == '+' || q[1] == '-') && isdigit((unsigned char)q[2])))) {
        q++;
        if (*q == '+' || *q == '-') eneg = *q++ == '-';
        for (; *q >= '0' && *q <= '9'; q++)
            if (ex < 400) ex = ex * 10 + (*q - '0');
        while (ex-- > 0) v = eneg ? v / 10.0 : v * 10.0;
    }
    *end = q;
    return neg ? -v : v;
}

/* A plain number, or one with '%' when pct is set. */
static int number_of(const char *v, size_t n, float *f, int *pct)
{
    const char *end;

    *f   = (float)number(v, &end);
    *pct = end == v + n - 1 && n > 1 && *end == '%';
    return end > v && (end == v + n || *pct);
}

static const var *var_find(const reaktor_sheet *s, const char *name, size_t n)
{
    unsigned h = hash('-', name, n), i;

    if (!s->capslot) return NULL;
    for (i = h & (s->capslot - 1); s->slot[i]; i = (i + 1) & (s->capslot - 1)) {
        const var *v = &s->var[s->slot[i] - 1];

        if (v->h == h && v->n == n && memcmp(v->name, name, n) == 0) return v;
    }
    return NULL;
}

static void var_index(reaktor_sheet *s)
{
    unsigned i, k;

    memset(s->slot, 0, s->capslot * sizeof *s->slot);
    for (k = 0; k < s->nvar; k++) {
        for (i = s->var[k].h & (s->capslot - 1); s->slot[i]; i = (i + 1) & (s->capslot - 1)) {}
        s->slot[i] = k + 1;
    }
}

static void var_set(reaktor_sheet *s, const char *name, size_t nn, const char *v, size_t vn)
{
    char *value = (char *)malloc(vn + 1);
    var  *e = (var *)var_find(s, name, nn);

    if (!value) return;
    memcpy(value, v, vn);
    value[vn] = 0;
    if (e) {
        free(e->value);
        e->value = value;
        return;
    }
    if (!ROOM(s, var, nvar, capvar, 1) || !(s->var[s->nvar].name = (char *)malloc(nn))) {
        free(value);
        return;
    }
    e = &s->var[s->nvar++];
    memcpy(e->name, name, nn);
    e->n     = nn;
    e->h     = hash('-', name, nn);
    e->value = value;
    if (2 * s->nvar > s->capslot) {
        unsigned  cap = s->capslot ? s->capslot * 2 : 64;
        unsigned *slot = (unsigned *)malloc(cap * sizeof *slot);

        if (!slot) return;
        free(s->slot);
        s->slot    = slot;
        s->capslot = cap;
        var_index(s);
    } else {
        unsigned i;

        for (i = e->h & (s->capslot - 1); s->slot[i]; i = (i + 1) & (s->capslot - 1)) {}
        s->slot[i] = s->nvar;
    }
}

static int expand(const reaktor_sheet *s, const char *v, size_t n, buf *out, int depth, int *steps)
{
    size_t i = 0;

    if (depth > VAR_DEPTH) return 0;
    while (i < n) {
        size_t      end, ns, ne;
        const var  *val;
        const char *comma;

        if (n - i < 4 || memcmp(v + i, "var(", 4) != 0 ||
            (i && (isalnum((unsigned char)v[i - 1]) || v[i - 1] == '-'))) {
            if (!buf_add(out, v + i, 1)) return 0;
            i++;
            continue;
        }
        if (--*steps < 0 || (end = scan(v, i + 4, n, ")")) >= n) return 0;
        ns = i + 4;
        while (ns < end && space(v[ns])) ns++;
        ne = ns;
        while (ne < end && v[ne] != ',' && !space(v[ne])) ne++;
        comma = (const char *)memchr(v + ne, ',', end - ne);
        if ((val = var_find(s, v + ns, ne - ns)) != NULL) {
            if (!expand(s, val->value, strlen(val->value), out, depth + 1, steps)) return 0;
        } else if (comma) {
            size_t fs = (size_t)(comma - v) + 1, fe = end;

            trim(v, &fs, &fe);
            if (!expand(s, v + fs, fe - fs, out, depth + 1, steps)) return 0;
        } else {
            return 0;
        }
        if (out->len > VAR_MAX) return 0;
        i = end + 1;
    }
    return out->len <= VAR_MAX;
}

/* v with every var() replaced, trimmed; *b holds what it points into. */
static int resolve_vars(const reaktor_sheet *s, const char **v, size_t *n, buf *b)
{
    int    steps = VAR_STEPS;
    size_t a = 0, e;

    if (!holds(*v, *n, "var(")) return 1;
    b->len = 0;
    if (!expand(s, *v, *n, b, 0, &steps) || !b->len) return 0;
    e = b->len;
    trim(b->p, &a, &e);
    *v = b->p + a;
    *n = e - a;
    return 1;
}

/* plutovg reads only the comma form of rgb() and hsl(), so a space or a '/'
   between arguments becomes a comma first. */
static int plain_color(const char *s, size_t n, float c[4])
{
    char tmp[64];
    const char *open;
    plutovg_color_t col;
    size_t i, k = 0;
    int depth = 0, gap = 0;

    if (n == 0 || n >= sizeof(tmp)) return 0;
    open = (const char *)memchr(s, '(', n);
    for (i = 0; i < n; i++) {
        char ch = s[i];

        if (open && depth == 1 && (space(ch) || ch == ',' || ch == '/')) {
            gap = 1;
            continue;
        }
        if (ch == '(') depth++;
        if (ch == ')') { depth--; gap = 0; }
        if (gap && tmp[k - 1] != '(') tmp[k++] = ',';
        gap = 0;
        tmp[k++] = ch;
    }
    tmp[k] = 0;
    if (plutovg_color_parse(&col, tmp, (int)k) != (int)k) return 0;
    c[0] = col.r * 255.0f;
    c[1] = col.g * 255.0f;
    c[2] = col.b * 255.0f;
    c[3] = col.a;
    return 1;
}

static int color_of(const char *s, size_t n, float c[4], int depth);

static int mix_arg(const char *s, size_t n, float c[4], float *pct, int depth)
{
    size_t a = 0, e = n;

    trim(s, &a, &e);
    *pct = -1.0f;
    if (e > a && s[e - 1] == '%') {
        size_t      b = e - 1;
        const char *end;

        while (b > a && !space(s[b - 1]) && s[b - 1] != ')') b--;
        *pct = (float)number(s + b, &end) / 100.0f;
        if (end != s + e - 1 || !(*pct >= 0.0f && *pct <= 1.0f)) return 0;
        e = b;
    }
    return color_of(s + a, e - a, c, depth + 1);
}

/* color-mix(in srgb, a [p%], b [q%]) */
static int mix(const char *s, size_t n, float out[4], int depth)
{
    size_t i = 0, ps, pe, part[2][2];
    float  a[4], b[4], pa, pb, wa, wb, sum, alpha;
    int    k;

    if (!next_item(s, &i, n, &ps, &pe) || !word(s + ps, pe - ps, "in srgb")) return 0;
    for (k = 0; k < 2; k++) {
        if (!next_item(s, &i, n, &part[k][0], &part[k][1])) return 0;
    }
    if (i < n || !mix_arg(s + part[0][0], part[0][1] - part[0][0], a, &pa, depth) ||
        !mix_arg(s + part[1][0], part[1][1] - part[1][0], b, &pb, depth))
        return 0;
    if (pa < 0.0f) pa = pb < 0.0f ? 0.5f : 1.0f - pb;
    if (pb < 0.0f) pb = 1.0f - pa;
    if ((sum = pa + pb) <= 0.0f) return 0;
    wa = pa * a[3];
    wb = pb * b[3];
    alpha = wa + wb;
    for (k = 0; k < 3; k++) out[k] = alpha > 0.0f ? (a[k] * wa + b[k] * wb) / alpha : 0.0f;
    out[3] = sum > 1.0f ? alpha / sum : alpha;
    return 1;
}

static int color_of(const char *s, size_t n, float c[4], int depth)
{
    size_t a = 0, e = n;

    trim(s, &a, &e);
    if (depth > CALC_DEPTH) return 0;
    if (e - a > 11 && word(s + a, 10, "color-mix(") && s[e - 1] == ')')
        return mix(s + a + 10, e - a - 11, c, depth);
    return plain_color(s + a, e - a, c);
}

static void to_bytes(const float c[4], unsigned char rgba[4])
{
    int i;

    for (i = 0; i < 4; i++) {
        float v = i < 3 ? c[i] : c[i] * 255.0f;

        rgba[i] = (unsigned char)(v < 0.0f ? 0 : v > 255.0f ? 255 : (int)(v + 0.5f));
    }
}

typedef struct calc_in { const reaktor_sheet *sh; const char *s; size_t i, n; int depth; float em; } calc_in;
typedef struct calc_val { double v; int px; } calc_val;

static void calc_space(calc_in *c) { while (c->i < c->n && space(c->s[c->i])) c->i++; }

static int calc_sum(calc_in *c, calc_val *out);

static int calc_atom(calc_in *c, calc_val *out)
{
    const char *end, *u;
    int neg = 0, ok;

    calc_space(c);
    while (c->i < c->n && c->s[c->i] == '-') {
        neg = !neg;
        c->i++;
        calc_space(c);
    }
    if (c->n - c->i >= 5 && memcmp(c->s + c->i, "calc(", 5) == 0) c->i += 4;
    if (c->i < c->n && c->s[c->i] == '(') {
        if (c->depth >= CALC_DEPTH) return 0;
        c->i++;
        c->depth++;
        ok = calc_sum(c, out);
        c->depth--;
        calc_space(c);
        if (!ok || c->i >= c->n || c->s[c->i] != ')') return 0;
        c->i++;
    } else {
        if (c->i >= c->n || (!isdigit((unsigned char)c->s[c->i]) && c->s[c->i] != '.')) return 0;
        out->v = number(c->s + c->i, &end);
        for (u = end; u < c->s + c->n && isalpha((unsigned char)*u); u++) {}
        out->px = u > end;
        if (word(end, (size_t)(u - end), "em")) out->v *= c->em;
        else if (word(end, (size_t)(u - end), "rem")) out->v *= c->sh->rem;
        else if (u > end && !word(end, (size_t)(u - end), "px")) return 0;
        if (u < c->s + c->n && *u == '%') return 0;
        c->i = (size_t)(u - c->s);
    }
    if (neg) out->v = -out->v;
    return 1;
}

static int calc_product(calc_in *c, calc_val *out)
{
    calc_val r;
    char     op;

    if (!calc_atom(c, out)) return 0;
    for (;;) {
        calc_space(c);
        if (c->i >= c->n || (c->s[c->i] != '*' && c->s[c->i] != '/')) return 1;
        op = c->s[c->i++];
        if (!calc_atom(c, &r)) return 0;
        if (op == '*') {
            if (out->px && r.px) return 0;
            out->v *= r.v;
            out->px |= r.px;
        } else {
            if (r.px || r.v == 0.0) return 0;
            out->v /= r.v;
        }
    }
}

static int calc_sum(calc_in *c, calc_val *out)
{
    calc_val r;
    char     op;

    if (!calc_product(c, out)) return 0;
    for (;;) {
        calc_space(c);
        if (c->i >= c->n || (c->s[c->i] != '+' && c->s[c->i] != '-')) return 1;
        op = c->s[c->i++];
        if (!calc_product(c, &r) || r.px != out->px) return 0;
        out->v += op == '+' ? r.v : -r.v;
    }
}

/* A length in pixels, em counted against `em`; a percentage is not one. */
static int length_em(const reaktor_sheet *sh, const char *s, size_t n, float em, float *px)
{
    const char *end;
    double      v;

    if (word(s, n, "auto") || word(s, n, "none")) { *px = 0.0f; return 1; }
    if (n > 5 && word(s, 5, "calc(")) {
        calc_in  c = { sh, s, 0, n, 0, em };
        calc_val r;

        if (!calc_atom(&c, &r) || c.i != n || (!r.px && r.v != 0.0)) return 0;
        *px = (float)r.v;
        return 1;
    }
    v = number(s, &end);
    if (end == s) return 0;
    n -= (size_t)(end - s);
    if (n == 0 && v == 0.0) *px = 0.0f;
    else if (word(end, n, "px")) *px = (float)v;
    else if (word(end, n, "em")) *px = (float)v * em;
    else if (word(end, n, "rem")) *px = (float)v * sh->rem;
    else return 0;
    return 1;
}

static int length(const ctx *x, const char *s, size_t n, float *px)
{
    return length_em(x->s, s, n, x->s->em, px);
}

static int border_width(const ctx *x, const char *s, size_t n, float *px)
{
    if (word(s, n, "thin"))   { *px = 1.0f; return 1; }
    if (word(s, n, "medium")) { *px = 3.0f; return 1; }
    if (word(s, n, "thick"))  { *px = 5.0f; return 1; }
    return length(x, s, n, px);
}

static int color(const char *s, size_t n, unsigned char rgba[4], unsigned char *flags)
{
    float c[4];

    if (word(s, n, "currentcolor")) { *flags |= F_CURRENT; return 1; }
    if (word(s, n, "transparent")) { memset(rgba, 0, 4); return 1; }
    if (!color_of(s, n, c, 0)) return 0;
    to_bytes(c, rgba);
    return 1;
}

static int direction(const char *s, size_t n, int *axis, int *flip)
{
    const char *end;
    double      deg;
    size_t      a = 2, e = n;

    if (n > 3 && word(s, 2, "to") && space(s[2])) {
        trim(s, &a, &e);
        *flip = word(s + a, e - a, "left") || word(s + a, e - a, "top");
        if (word(s + a, e - a, "left") || word(s + a, e - a, "right"))
            *axis = REAKTOR_GRADIENT_ACROSS;
        else if (word(s + a, e - a, "top") || word(s + a, e - a, "bottom"))
            *axis = REAKTOR_GRADIENT_DOWN;
        else
            return 0;
        return 1;
    }
    deg = number(s, &end);
    if (end == s || !word(end, n - (size_t)(end - s), "deg") || deg != (double)(long)deg)
        return 0;
    switch ((((long)deg % 360) + 360) % 360) {
    case 0:   *axis = REAKTOR_GRADIENT_DOWN;   *flip = 1; return 1;
    case 90:  *axis = REAKTOR_GRADIENT_ACROSS; *flip = 0; return 1;
    case 180: *axis = REAKTOR_GRADIENT_DOWN;   *flip = 0; return 1;
    case 270: *axis = REAKTOR_GRADIENT_ACROSS; *flip = 1; return 1;
    default:  return 0;
    }
}

/* linear-gradient([to <side> | <n>deg,] a, b), turned to run left to right or
   top to bottom. Two stops and the four sides are all that is drawn. */
static int gradient(const char *s, size_t n, unsigned char from[4], unsigned char to[4],
                    int *axis)
{
    size_t        i = 16, item[3][2];
    unsigned char flags = 0;
    int           k = 0, flip = 0, way = REAKTOR_GRADIENT_DOWN;

    if (n < 18 || !word(s, 16, "linear-gradient(") || s[n - 1] != ')') return 0;
    n--;
    while (k < 3 && next_item(s, &i, n, &item[k][0], &item[k][1])) k++;
    if (i < n || k < 2) return 0;
    if (k == 3 && !direction(s + item[0][0], item[0][1] - item[0][0], &way, &flip))
        return 0;
    if (!color(s + item[k - 2][0], item[k - 2][1] - item[k - 2][0], flip ? to : from, &flags) ||
        !color(s + item[k - 1][0], item[k - 1][1] - item[k - 1][0], flip ? from : to, &flags) ||
        flags)
        return 0;
    *axis = way;
    return 1;
}

static int line_style(const char *s, size_t n, float *v)
{
    static const char *const styles[] = { "none", "hidden", "dotted", "dashed", "solid",
                                          "double", "groove", "ridge", "inset", "outset" };
    unsigned k;

    for (k = 0; k < sizeof styles / sizeof *styles; k++)
        if (word(s, n, styles[k])) { *v = k > 1; return 1; }
    return 0;
}

/* The next space-separated part of s[*i..n), or 0 at the end or at a '/'. */
static int part(const char *s, size_t *i, size_t n, size_t *a, size_t *b)
{
    while (*i < n && space(s[*i])) (*i)++;
    if (*i >= n || s[*i] == '/') return 0;
    *a = *i;
    while (*i < n && !space(s[*i]) && s[*i] != '/') {
        if (s[*i] == '(') *i = scan(s, *i + 1, n, ")");
        if (*i < n) (*i)++;
    }
    *b = *i;
    return 1;
}

static void emit(ctx *x, int prop, float v, const unsigned char *rgba, unsigned char flags)
{
    reaktor_sheet *s = x->s;
    decl          *d;

    if (!ROOM(s, decl, ndecl, capdecl, 1)) return;
    d = &s->decl[s->ndecl++];
    d->v     = v;
    d->prop  = (unsigned char)prop;
    d->flags = (unsigned char)(x->flags | flags);
    if (rgba) memcpy(d->rgba, rgba, 4);
    else memset(d->rgba, 0, 4);
}

enum { K_COLOR, K_BG, K_BGIMG, K_LEN, K_FS, K_FW, K_LH, K_OP, K_BOX, K_RADIUS, K_BSTYLE,
       K_BCOLOR, K_BORDER };

static const struct prop {
    const char   *name;
    unsigned char kind, base;
    signed char   side;
} g_props[] = {
    { "color", K_COLOR, P_FG, 0 },             { "background-color", K_COLOR, P_BG, 0 },
    { "background", K_BG, P_BG, 0 },           { "width", K_LEN, P_W, 0 },
    { "height", K_LEN, P_H, 0 },               { "min-width", K_LEN, P_MINW, 0 },
    { "min-height", K_LEN, P_MINH, 0 },        { "max-width", K_LEN, P_MAXW, 0 },
    { "max-height", K_LEN, P_MAXH, 0 },        { "font-size", K_FS, P_FS, 0 },
    { "font-weight", K_FW, P_FW, 0 },          { "line-height", K_LH, P_LH, 0 },
    { "opacity", K_OP, P_OP, 0 },              { "background-image", K_BGIMG, P_GTO, 0 },
    { "padding", K_BOX, P_PAD, -1 },           { "padding-top", K_BOX, P_PAD, 0 },
    { "padding-right", K_BOX, P_PAD, 1 },      { "padding-bottom", K_BOX, P_PAD, 2 },
    { "padding-left", K_BOX, P_PAD, 3 },       { "margin", K_BOX, P_MAR, -1 },
    { "margin-top", K_BOX, P_MAR, 0 },         { "margin-right", K_BOX, P_MAR, 1 },
    { "margin-bottom", K_BOX, P_MAR, 2 },      { "margin-left", K_BOX, P_MAR, 3 },
    { "border-width", K_BOX, P_BW, -1 },       { "border-top-width", K_BOX, P_BW, 0 },
    { "border-right-width", K_BOX, P_BW, 1 },  { "border-bottom-width", K_BOX, P_BW, 2 },
    { "border-left-width", K_BOX, P_BW, 3 },   { "border-radius", K_RADIUS, P_RAD, -1 },
    { "border-top-left-radius", K_RADIUS, P_RAD, 0 },
    { "border-top-right-radius", K_RADIUS, P_RAD, 1 },
    { "border-bottom-right-radius", K_RADIUS, P_RAD, 2 },
    { "border-bottom-left-radius", K_RADIUS, P_RAD, 3 },
    { "border-style", K_BSTYLE, P_BS, -1 },    { "border-top-style", K_BSTYLE, P_BS, 0 },
    { "border-right-style", K_BSTYLE, P_BS, 1 }, { "border-bottom-style", K_BSTYLE, P_BS, 2 },
    { "border-left-style", K_BSTYLE, P_BS, 3 },  { "border-color", K_BCOLOR, P_BC, -1 },
    { "border-top-color", K_BCOLOR, P_BC, 0 }, { "border", K_BORDER, 0, -1 },
    { "border-top", K_BORDER, 0, 0 },          { "border-right", K_BORDER, 0, 1 },
    { "border-bottom", K_BORDER, 0, 2 },       { "border-left", K_BORDER, 0, 3 },
};

static void border(ctx *x, int side, const char *v, size_t n, int reset)
{
    unsigned char rgba[4] = { 0, 0, 0, 0 }, cflags = F_CURRENT;
    float         w = 3.0f, st = 0.0f, f;
    int           have = 0, k;
    size_t        i = 0, a, b;

    while (!reset && part(v, &i, n, &a, &b)) {
        if (line_style(v + a, b - a, &f) && !(have & 1)) { st = f; have |= 1; }
        else if (border_width(x, v + a, b - a, &f) && !(have & 2)) { w = f; have |= 2; }
        else if (!(have & 4) && (cflags = 0, color(v + a, b - a, rgba, &cflags))) have |= 4;
        else return;
    }
    if (!reset && (!have || i < n)) return;
    for (k = 0; k < 4; k++) {
        if (side >= 0 && k != side) continue;
        emit(x, P_BW + k, w, NULL, reset ? F_INITIAL : 0);
        emit(x, P_BS + k, st, NULL, reset ? F_INITIAL : 0);
        if (k == 0) emit(x, P_BC, 0.0f, rgba, reset ? F_INITIAL : cflags);
    }
}

static void box(ctx *x, const struct prop *p, const char *v, size_t n, int reset)
{
    static const int spread[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
    float  val[4];
    size_t i = 0, a, b;
    int    k = 0, ok;

    if (reset) {
        for (k = 0; k < 4; k++)
            if (p->side < 0 || k == p->side) emit(x, p->base + k, 0.0f, NULL, F_INITIAL);
        return;
    }
    while (part(v, &i, n, &a, &b)) {
        if (k == 4) return;
        ok = p->kind == K_BSTYLE ? line_style(v + a, b - a, &val[k])
           : p->base == P_BW     ? border_width(x, v + a, b - a, &val[k])
                                 : length(x, v + a, b - a, &val[k]);
        if (!ok) return;
        k++;
    }
    if (!k || (i < n && p->kind != K_RADIUS) ||
        (p->side >= 0 && k > (p->kind == K_RADIUS ? 2 : 1)))
        return;
    if (p->side >= 0) {
        emit(x, p->base + p->side, val[0], NULL, 0);
        return;
    }
    for (b = 0; b < 4; b++) emit(x, p->base + (int)b, val[spread[k - 1][b]], NULL, 0);
}

static void apply(ctx *x, const struct prop *p, const char *v, size_t n)
{
    unsigned char rgba[4] = { 0, 0, 0, 0 }, flags = 0;
    float         f;
    size_t        i = 0, a, b;
    int           pct, reset = word(v, n, "inherit") || word(v, n, "initial") ||
                               word(v, n, "unset") || word(v, n, "revert") ||
                               word(v, n, "revert-layer");

    switch (p->kind) {
    case K_BORDER: border(x, p->side, v, n, reset); return;
    case K_BOX: case K_RADIUS: case K_BSTYLE: box(x, p, v, n, reset); return;
    case K_BCOLOR:
        if (!reset && (!part(v, &i, n, &a, &b) || !color(v + a, b - a, rgba, &flags))) return;
        if (p->side == 0 && !reset && i < n) return;
        emit(x, P_BC, 0.0f, rgba, reset ? F_INITIAL : flags);
        return;
    default: break;
    }
    if (reset) {
        if (p->kind == K_BG) emit(x, P_GTO, 0.0f, NULL, F_INITIAL);
        emit(x, p->base, 0.0f, NULL, F_INITIAL);
        return;
    }
    switch (p->kind) {
    case K_COLOR:
        if (color(v, n, rgba, &flags)) emit(x, p->base, 0.0f, rgba, flags);
        break;
    case K_BG: {
        unsigned char from[4] = { 0, 0, 0, 0 }, to[4] = { 0, 0, 0, 0 };
        int           axis = REAKTOR_GRADIENT_NONE, painted = 0;

        for (;;) {
            while (i < n && (space(v[i]) || v[i] == '/')) i++;
            if (!part(v, &i, n, &a, &b)) break;
            if (!axis && gradient(v + a, b - a, from, to, &axis)) continue;
            if (color(v + a, b - a, rgba, &flags)) { painted = 1; break; }
        }
        /* Where the gradient is not drawn, its first stop stands for it. */
        if (axis && !painted) memcpy(rgba, from, 4);
        emit(x, P_BG, 0.0f, rgba, flags);
        emit(x, P_GFROM, 0.0f, from, 0);
        emit(x, P_GTO, (float)axis, to, 0);
        break;
    }
    case K_BGIMG: {
        unsigned char from[4] = { 0, 0, 0, 0 }, to[4] = { 0, 0, 0, 0 };
        int           axis = REAKTOR_GRADIENT_NONE;

        if (word(v, n, "none") || gradient(v, n, from, to, &axis)) {
            emit(x, P_GFROM, 0.0f, from, 0);
            emit(x, P_GTO, (float)axis, to, 0);
        }
        break;
    }
    case K_LEN:
        if (length(x, v, n, &f)) emit(x, p->base, f, NULL, 0);
        break;
    case K_FS:
        if (number_of(v, n, &f, &pct) && pct) emit(x, P_FS, f * x->s->em / 100.0f, NULL, 0);
        else if (length(x, v, n, &f) && f > 0.0f) emit(x, P_FS, f, NULL, 0);
        break;
    case K_FW:
        if (word(v, n, "bold") || word(v, n, "bolder")) emit(x, P_FW, 700.0f, NULL, 0);
        else if (word(v, n, "normal") || word(v, n, "lighter")) emit(x, P_FW, 400.0f, NULL, 0);
        else if (number_of(v, n, &f, &pct) && !pct) emit(x, P_FW, f, NULL, 0);
        break;
    case K_LH:
        if (word(v, n, "normal")) emit(x, P_LH, 0.0f, NULL, 0);
        else if (number_of(v, n, &f, &pct)) emit(x, P_LH, pct ? f / 100.0f : f, NULL, F_NUMBER);
        else if (length(x, v, n, &f)) emit(x, P_LH, f, NULL, 0);
        break;
    case K_OP:
        if (number_of(v, n, &f, &pct)) emit(x, P_OP, pct ? f / 100.0f : f, NULL, 0);
        break;
    }
}

static const struct prop *prop_of(const char *s, size_t n)
{
    static unsigned char slot[128];
    static int           ready;
    char                 low[32];
    unsigned             i, h;

    if (!ready) {
        ready = 1;
        for (i = 0; i < sizeof g_props / sizeof *g_props; i++) {
            for (h = hash('p', g_props[i].name, strlen(g_props[i].name)) & 127; slot[h]; h = (h + 1) & 127) {}
            slot[h] = (unsigned char)(i + 1);
        }
    }
    if (n >= sizeof low) return NULL;
    for (i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)s[i]);
    for (h = hash('p', low, n) & 127; slot[h]; h = (h + 1) & 127) {
        const struct prop *p = &g_props[slot[h] - 1];

        if (strlen(p->name) == n && memcmp(p->name, low, n) == 0) return p;
    }
    return NULL;
}

/* Calls fn for each declaration in s[i..n), its value with !important cut. */
static void each_decl(ctx *x, const char *s, size_t i, size_t n,
                      void (*fn)(ctx *, const char *, size_t, const char *, size_t, int))
{
    while (i < n) {
        size_t e = scan(s, i, n, ";{"), ps = i, pe, vs, ve, bang;
        const char *colon;
        int important;

        if (e < n && s[e] == '{') {
            i = block_end(s, e + 1, n) + 1;
            continue;
        }
        i = e + 1;
        if (!(colon = (const char *)memchr(s + ps, ':', e - ps))) continue;
        pe = (size_t)(colon - s);
        vs = pe + 1;
        ve = e;
        trim(s, &ps, &pe);
        trim(s, &vs, &ve);
        bang = scan(s, vs, ve, "!");
        if ((important = bang < ve) != 0) {
            size_t ws = bang + 1, we = ve;

            trim(s, &ws, &we);
            if (!word(s + ws, we - ws, "important")) continue;
            ve = bang;
            trim(s, &vs, &ve);
        }
        if (pe > ps && ve > vs) fn(x, s + ps, pe - ps, s + vs, ve - vs, important);
    }
}

static void declare(ctx *x, const char *p, size_t pn, const char *v, size_t vn, int important)
{
    const struct prop *prop;

    if (!(prop = prop_of(p, pn))) return;
    x->flags = important ? F_IMPORTANT : 0;
    if (resolve_vars(x->s, &v, &vn, x->tmp)) apply(x, prop, v, vn);
    else apply(x, prop, "unset", 5);
    x->flags = 0;
}

static void collect_var(ctx *x, const char *p, size_t pn, const char *v, size_t vn, int important)
{
    (void)important;
    if (pn > 2 && p[0] == '-' && p[1] == '-') var_set(x->s, p, pn, v, vn);
}

/* The winning font-size against x->rel, for the root and the body. */
static void font_size(ctx *x, const char *p, size_t pn, const char *v, size_t vn, int important)
{
    uint64_t key = (uint64_t)!!important << 63 | x->key;
    float    f;
    int      pct;

    if (!word(p, pn, "font-size") || key < x->best) return;
    if (resolve_vars(x->s, &v, &vn, x->tmp) &&
        ((number_of(v, vn, &f, &pct) && pct && (f *= x->rel / 100.0f, 1)) ||
         length_em(x->s, v, vn, x->rel, &f)) &&
        f > 0.0f) {
        x->size = f;
        x->best = key;
    }
}

static int ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '\\' || (unsigned char)c >= 0x80;
}

/* Past the escape at s[i]: up to six hex digits and one space, or one character. */
static size_t escape_end(const char *s, size_t i, size_t n, unsigned *cp)
{
    size_t k = 0;

    *cp = 0;
    for (i++; i < n && k < 6 && isxdigit((unsigned char)s[i]); i++, k++)
        *cp = *cp * 16 + (unsigned)(isdigit((unsigned char)s[i]) ? s[i] - '0' : (s[i] | 0x20) - 'a' + 10);
    if (k) return i < n && space(s[i]) ? i + 1 : i;
    *cp = i < n ? (unsigned char)s[i] : 0xFFFD;
    return i < n ? i + 1 : i;
}

static size_t ident_end(const char *s, size_t i, size_t n)
{
    unsigned cp;

    while (i < n && ident_char(s[i])) i = s[i] == '\\' ? escape_end(s, i, n, &cp) : i + 1;
    return i;
}

/* s[0..n) with its escapes decoded into out as UTF-8; the length, or cap when it does not fit. */
static size_t unescape(const char *s, size_t n, char *out, size_t cap)
{
    size_t   i = 0, k = 0;
    unsigned cp;

    while (i < n && k + 4 < cap) {
        if (s[i] != '\\') {
            out[k++] = s[i++];
            continue;
        }
        i = escape_end(s, i, n, &cp);
        if (!cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (cp < 0x80) {
            out[k++] = (char)cp;
        } else if (cp < 0x800) {
            out[k++] = (char)(0xC0 | cp >> 6);
            out[k++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[k++] = (char)(0xE0 | cp >> 12);
            out[k++] = (char)(0x80 | (cp >> 6 & 63));
            out[k++] = (char)(0x80 | (cp & 63));
        } else {
            out[k++] = (char)(0xF0 | cp >> 18);
            out[k++] = (char)(0x80 | (cp >> 12 & 63));
            out[k++] = (char)(0x80 | (cp >> 6 & 63));
            out[k++] = (char)(0x80 | (cp & 63));
        }
    }
    return i < n ? cap : k;
}

static unsigned name_hash(char kind, const char *s, size_t n)
{
    char   b[256];
    size_t k;

    if (!memchr(s, '\\', n)) return hash(kind, s, n);
    k = unescape(s, n, b, sizeof b);
    return hash(kind, b, k < sizeof b ? k : 0);
}

/* [name] as the class "name", [name=value] as "name-value". */
static int attribute(const char *s, size_t *pi, size_t n, unsigned *h)
{
    size_t i = *pi + 1, close = scan(s, i, n, "]"), ne, vs, ve, k;
    char   cls[128];

    if (close >= n) return 0;
    while (i < close && space(s[i])) i++;
    ne = ident_end(s, i, close);
    if (ne == i || (k = unescape(s + i, ne - i, cls, sizeof cls)) >= sizeof cls) return 0;
    vs = ne;
    while (vs < close && space(s[vs])) vs++;
    if (vs < close) {
        if (s[vs] != '=') return 0;
        vs++;
        ve = close;
        trim(s, &vs, &ve);
        if (ve > vs && (s[vs] == '"' || s[vs] == '\'')) {
            if (ve - vs < 2 || s[ve - 1] != s[vs]) return 0;
            vs++;
            ve--;
        }
        cls[k++] = '-';
        if ((ve = unescape(s + vs, ve - vs, cls + k, sizeof cls - k)) >= sizeof cls - k) return 0;
        k += ve;
    }
    *h  = hash('.', cls, k);
    *pi = close + 1;
    return 1;
}

static int parse_compound(selector *q, const char *s, size_t *pi, size_t n, compound *c,
                          int inner, unsigned *spec);

static int negation(selector *q, const char *s, size_t a, size_t e, compound *c, unsigned *spec)
{
    unsigned best = 0;
    size_t   ps, pe;

    if (!c->nn) c->neg = q->nneg;
    while (next_item(s, &a, e, &ps, &pe)) {
        unsigned sp = 0;

        if (q->nneg >= MAX_NEG || !parse_compound(q, s, &ps, pe, &q->neg[q->nneg], 1, &sp) ||
            ps != pe)
            return 0;
        q->nneg++;
        c->nn++;
        if (sp > best) best = sp;
    }
    *spec += best;
    return c->nn > 0;
}

static int parse_compound(selector *q, const char *s, size_t *pi, size_t n, compound *c,
                          int inner, unsigned *spec)
{
    unsigned names[MAX_NAMES];
    unsigned k = 0;
    size_t   i = *pi, e;

    memset(c, 0, sizeof *c);
    if (i < n && s[i] == '*') {
        i++;
    } else if (i < n && ident_char(s[i])) {
        e = ident_end(s, i, n);
        c->type = name_hash('t', s + i, e - i);
        *spec += SPEC_TYPE;
        i = e;
    }
    while (i < n && k < MAX_NAMES) {
        if (s[i] == '.' || s[i] == '#') {
            e = ident_end(s, i + 1, n);
            if (e == i + 1) return 0;
            names[k++] = name_hash(s[i], s + i + 1, e - i - 1);
            *spec += s[i] == '#' ? SPEC_ID : SPEC_CLASS;
            i = e;
        } else if (s[i] == '[') {
            if (!attribute(s, &i, n, &names[k++])) return 0;
            *spec += SPEC_CLASS;
        } else if (s[i] == ':') {
            if (i + 1 < n && s[i + 1] == ':') return 0;
            e = ident_end(s, i + 1, n);
            if (e == i + 1) return 0;
            if (e < n && s[e] == '(') {
                size_t close = scan(s, e + 1, n, ")");

                if (close >= n) return 0;
                if (word(s + i + 1, e - i - 1, "not")) {
                    if (inner || !negation(q, s, e + 1, close, c, spec)) return 0;
                } else {
                    names[k++] = hash(':', s + i + 1, close - i);
                    *spec += SPEC_CLASS;
                }
                i = close + 1;
            } else {
                if (word(s + i + 1, e - i - 1, "enabled")) c->enabled = 1;
                else names[k++] = hash(':', s + i + 1, e - i - 1);
                *spec += SPEC_CLASS;
                i = e;
            }
        } else {
            break;
        }
    }
    if (i == *pi || k >= MAX_NAMES || q->nnm + k > MAX_NAMES) return 0;
    c->name = q->nnm;
    c->n    = (unsigned char)k;
    memcpy(q->nm + q->nnm, names, k * sizeof *names);
    q->nnm += k;
    *pi = i;
    return 1;
}

static int parse_selector(selector *q, const char *s, size_t n)
{
    size_t i = 0;
    int    child = 0;

    q->n = q->nneg = q->nnm = q->spec = 0;
    for (;;) {
        while (i < n && space(s[i])) i++;
        if (i >= n) break;
        if (s[i] == '>') {
            if (child || !q->n) return 0;
            child = 1;
            i++;
            continue;
        }
        if (q->n >= MAX_DEPTH || !parse_compound(q, s, &i, n, &q->c[q->n], 0, &q->spec)) return 0;
        q->c[q->n++].child = (unsigned char)child;
        child = 0;
        if (i < n && !space(s[i]) && s[i] != '>') return 0;
    }
    return q->n > 0 && !child;
}

static int has(const unsigned *a, unsigned n, unsigned h)
{
    while (n--)
        if (*a++ == h) return 1;
    return 0;
}

static int simple(const pool *r, const compound *rc, const selector *q, const compound *c)
{
    static unsigned disabled;
    unsigned        i;

    if (!disabled) disabled = hash(':', "disabled", 8);
    if (rc->type && rc->type != c->type) return 0;
    for (i = 0; i < rc->n; i++)
        if (!has(q->nm + c->name, c->n, r->nm[rc->name + i])) return 0;
    if (rc->enabled && has(q->nm + c->name, c->n, disabled)) return 0;
    for (i = 0; i < rc->nn; i++)
        if (simple(r, &r->neg[rc->neg + i], q, c)) return 0;
    return 1;
}

static int chain(const pool *r, const compound *rc0, int ri, const selector *q, int qi)
{
    const compound *rc = &rc0[ri];
    int             j;

    if (!simple(r, rc, q, &q->c[qi])) return 0;
    if (ri == 0) return 1;
    if (rc->child) return qi > 0 && chain(r, rc0, ri - 1, q, qi - 1);
    for (j = qi - 1; j >= 0; j--)
        if (chain(r, rc0, ri - 1, q, j)) return 1;
    return 0;
}

static int matches(const selector *r, const selector *q)
{
    pool p = { r->c, r->neg, r->nm };

    return chain(&p, r->c, (int)r->n - 1, q, (int)q->n - 1);
}

static void add_rule(reaktor_sheet *s, const selector *q, unsigned blk)
{
    unsigned k;
    rule          *r;

    if (!ROOM(s, rule, nrule, caprule, 1) || !ROOM(s, cmp, ncmp, capcmp, q->n) ||
        !ROOM(s, neg, nneg, capneg, q->nneg) || !ROOM(s, nm, nnm, capnm, q->nnm))
        return;
    r = &s->rule[s->nrule++];
    r->c0   = s->ncmp;
    r->n    = (unsigned char)q->n;
    r->blk  = blk;
    r->spec = q->spec;
    for (k = 0; k < q->n; k++) {
        compound c = q->c[k];

        c.name += s->nnm;
        c.neg  += s->nneg;
        s->cmp[s->ncmp++] = c;
    }
    for (k = 0; k < q->nneg; k++) {
        compound c = q->neg[k];

        c.name += s->nnm;
        s->neg[s->nneg++] = c;
    }
    memcpy(s->nm + s->nnm, q->nm, q->nnm * sizeof *q->nm);
    s->nnm += q->nnm;
}

/* 1 or 0 for a feature this runtime answers, and 0 for any other. */
static int media_feature(const char *s, size_t n, const char *theme)
{
    static const struct { const char *name, *on; int *flag; } prefs[] = {
        { "prefers-reduced-motion", "reduce", &g_reduce_motion },
        { "prefers-contrast", "more", &g_more_contrast },
    };
    const char *colon = (const char *)memchr(s, ':', n);
    size_t      a = 0, nl = colon ? (size_t)(colon - s) : n, vs = nl + 1, ve = n;
    unsigned    k;

    trim(s, &a, &nl);
    if (colon) trim(s, &vs, &ve);
    if (word(s + a, nl - a, "prefers-color-scheme"))
        return colon && word(s + vs, ve - vs, theme);
    for (k = 0; k < sizeof prefs / sizeof *prefs; k++) {
        if (!word(s + a, nl - a, prefs[k].name)) continue;
        if (!colon || word(s + vs, ve - vs, prefs[k].on)) return *prefs[k].flag;
        return word(s + vs, ve - vs, "no-preference") && !*prefs[k].flag;
    }
    return 0;
}

/* Whether a @media prelude holds: one of its comma-separated queries names
   only features answered here, each true. */
static int media_matches(const char *q, size_t n, const char *theme)
{
    size_t i = 0;

    while (i < n) {
        int ok = 1, features = 0;

        while (i < n && q[i] != ',') {
            size_t s;

            if (space(q[i])) { i++; continue; }
            if (q[i] == '(') {
                s = ++i;
                while (i < n && q[i] != ')') i++;
                ok &= media_feature(q + s, i - s, theme);
                features++;
                if (i < n) i++;
                continue;
            }
            s = i;
            while (i < n && !space(q[i]) && q[i] != '(' && q[i] != ',') i++;
            if (!word(q + s, i - s, "only") && !word(q + s, i - s, "and") &&
                !word(q + s, i - s, "screen") && !word(q + s, i - s, "all"))
                ok = 0;
        }
        if (ok && features) return 1;
        i++;
    }
    return 0;
}

static void blocks(reaktor_sheet *sh, const char *s, size_t i, size_t n, int depth)
{
    while (i < n) {
        size_t a, open, close;

        while (i < n && (space(s[i]) || s[i] == ';' || s[i] == '}')) i++;
        if (i >= n) return;
        a = i;
        open = scan(s, i, n, "{;");
        if (open >= n) return;
        if (s[open] == ';') { i = open + 1; continue; }
        close = block_end(s, open + 1, n);
        if (s[a] == '@') {
            if (depth < BLOCK_DEPTH && open - a > 6 && word(s + a, 6, "@media") &&
                media_matches(s + a + 6, open - a - 6, sh->theme))
                blocks(sh, s, open + 1, close, depth + 1);
        } else if (ROOM(sh, blk, nblk, capblk, 1)) {
            block *b = &sh->blk[sh->nblk++];

            memset(b, 0, sizeof *b);
            b->ss = a;
            b->se = open;
            b->bs = open + 1;
            b->be = close;
            trim(s, &b->ss, &b->se);
        }
        i = close + 1;
    }
}

/* `to` resolved against the folder of `from`, with "." and ".." folded away. */
static char *import_path(const char *from, const char *to, size_t n)
{
    const char *slash = strrchr(from, '/');
    size_t      dir = slash ? (size_t)(slash - from) + 1 : 0;
    char       *p = (char *)malloc(dir + n + 1), *w, *r;

    if (!p) return NULL;
    memcpy(p, from, dir);
    memcpy(p + dir, to, n);
    p[dir + n] = 0;
    for (w = r = p; *r;) {
        if (strncmp(r, "./", 2) == 0) { r += 2; continue; }
        if (strncmp(r, "../", 3) == 0) {
            if (w > p) {
                w--;
                while (w > p && w[-1] != '/') w--;
            }
            r += 3;
            continue;
        }
        while (*r && *r != '/') *w++ = *r++;
        if (*r) *w++ = *r++;
    }
    *w = 0;
    return p;
}

typedef struct loader {
    const char *chain[IMPORT_DEPTH + 1];
    int         loads;
    buf        *all;
} loader;

static int in_chain(const loader *l, int depth, const char *path)
{
    int i;

    for (i = 0; i <= depth; i++)
        if (strcmp(l->chain[i], path) == 0) return 1;
    return 0;
}

/* s[i..n) with every string, bracket and block it leaves open closed, as
   the end of a sheet closes them. */
static int add_closed(buf *all, const char *s, size_t i, size_t n)
{
    buf    stack = { 0 };
    size_t k;
    char   q = 0;
    int    ok;

    for (k = i; k < n; k++) {
        char c = s[k];

        if (q) {
            if (c == '\\') k++;
            else if (c == q) q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '{' || c == '(' || c == '[') {
            char close = c == '{' ? '}' : c == '(' ? ')' : ']';

            if (!buf_add(&stack, &close, 1)) { free(stack.p); return 0; }
        } else if (stack.len && c == stack.p[stack.len - 1]) {
            stack.len--;
        }
    }
    ok = buf_add(all, s + i, n - i) && (!q || buf_add(all, &q, 1));
    while (ok && stack.len) ok = buf_add(all, &stack.p[--stack.len], 1);
    free(stack.p);
    return ok && buf_str(all, "\n");
}

/* The sheet, its leading @imports read in place, a conditional one as @media. */
static int load_sheet(loader *l, const char *path, int depth)
{
    char  *t, *to;
    size_t i = 0, n;
    int    ok = 1;

    l->chain[depth] = path;
    if (g_seen) g_seen(path);
    if (!(t = reaktor_asset_load(path, NULL))) {
        fprintf(stderr, "css: cannot read %s; skipped\n", path);
        return 1;
    }
    if ((n = strlen(t)) > IMPORT_BUDGET) {
        fprintf(stderr, "css: %s is past %u bytes; skipped\n", path, IMPORT_BUDGET);
        reaktor_free(t);
        return 1;
    }
    strip_comments(t);
    n = strlen(t);
    for (;;) {
        size_t s, e, q_s, q_e;
        char   end = 0;

        while (i < n && (space(t[i]) || t[i] == ';')) i++;
        if (n - i > 8 && word(t + i, 8, "@charset")) {
            i = scan(t, i, n, ";");
            continue;
        }
        if (n - i <= 7 || !word(t + i, 7, "@import")) break;
        s = i + 7;
        while (s < n && space(t[s])) s++;
        if (strncmp(t + s, "url(", 4) == 0) { s += 4; end = ')'; }
        while (s < n && space(t[s])) s++;
        if (t[s] == '\'' || t[s] == '"') end = t[s++];
        e = s;
        while (e < n && (end ? t[e] != end : !space(t[e]) && t[e] != ';')) e++;
        q_s = e < n && end ? e + 1 : e;
        if (end == '\'' || end == '"') {
            while (q_s < n && space(t[q_s])) q_s++;
            if (t[q_s] == ')') q_s++;
        }
        q_e = i = scan(t, q_s, n, ";");
        if (depth >= IMPORT_DEPTH || e == s || l->all->len > IMPORT_BUDGET ||
            ++l->loads > IMPORT_LOADS)
            continue;
        if (!(to = import_path(path, t + s, e - s))) { ok = 0; break; }
        if (!in_chain(l, depth, to)) {
            trim(t, &q_s, &q_e);
            if (q_e > q_s)
                ok = buf_str(l->all, "@media ") && buf_add(l->all, t + q_s, q_e - q_s) &&
                     buf_str(l->all, " {\n") && load_sheet(l, to, depth + 1) &&
                     buf_str(l->all, "}\n");
            else
                ok = load_sheet(l, to, depth + 1);
        }
        free(to);
        if (!ok) break;
    }
    ok = ok && add_closed(l->all, t, i, n);
    reaktor_free(t);
    return ok;
}

static ctx context(reaktor_sheet *s)
{
    ctx x;

    memset(&x, 0, sizeof x);
    x.s   = s;
    x.tmp = &s->tmp;
    return x;
}

/* The font size of the element `who` names, against `rel`, or `fallback`. */
static float root_font(reaktor_sheet *s, const char *who, float rel, float fallback)
{
    ctx      x = context(s);
    pool     p = { s->cmp, s->neg, s->nm };
    selector q;
    unsigned k;

    parse_selector(&q, who, strlen(who));
    x.size = fallback;
    x.rel  = rel;
    for (k = 0; k < s->nrule; k++) {
        const rule  *r = &s->rule[k];
        const block *b = &s->blk[r->blk];

        if (!chain(&p, s->cmp + r->c0, r->n - 1, &q, (int)q.n - 1)) continue;
        x.key = (uint64_t)r->spec << 32 | r->blk;
        each_decl(&x, s->src.p, b->bs, b->be, font_size);
    }
    return x.size;
}

static bucket *bucket_of(const reaktor_sheet *s, unsigned key)
{
    unsigned i;

    for (i = key & (s->capbucket - 1); s->bucket[i].key && s->bucket[i].key != key;
         i = (i + 1) & (s->capbucket - 1)) {}
    return &s->bucket[i];
}

static void index_rules(reaktor_sheet *s)
{
    unsigned cap = 16, i;

    free(s->bucket);
    s->bucket    = NULL;
    s->capbucket = s->any = 0;
    while (cap < 2 * s->nrule) cap *= 2;
    if ((s->bucket = (bucket *)calloc(cap, sizeof *s->bucket)) != NULL) s->capbucket = cap;
    for (i = s->nrule; i-- > 0;) {
        rule           *r = &s->rule[i];
        const compound *c = &s->cmp[r->c0 + r->n - 1];
        unsigned        key = c->type ? c->type : c->n ? s->nm[c->name] : 0, *head = &s->any;

        if (key && s->capbucket) {
            bucket *b = bucket_of(s, key);

            b->key = key;
            head   = &b->first;
        }
        r->next = *head;
        *head   = i + 1;
    }
}

/* One more sheet read into s: its blocks, its rules, and the custom
   properties of the root. */
static int ingest(reaktor_sheet *s, const char *path)
{
    loader   l;
    ctx      x = context(s);
    selector q, root;
    char     who[96];
    unsigned k, first = s->nblk;
    size_t   a, ps, pe, from = s->src.len;

    memset(&l, 0, sizeof l);
    l.all = &s->src;
    if (!load_sheet(&l, path, 0)) return 0;
    if (!s->src.p) return 1;
    blocks(s, s->src.p, from, s->src.len, 0);
    snprintf(who, sizeof who, "html:root.data-theme-%s", s->theme);
    parse_selector(&root, who, strlen(who));
    for (k = first; k < s->nblk; k++) {
        int vars = 0;

        a = s->blk[k].ss;
        while (next_item(s->src.p, &a, s->blk[k].se, &ps, &pe)) {
            if (holds(s->src.p + ps, pe - ps, "::backdrop")) vars = 1;
            if (!parse_selector(&q, s->src.p + ps, pe - ps)) continue;
            if (matches(&q, &root)) vars = 1;
            add_rule(s, &q, k);
        }
        if (vars) each_decl(&x, s->src.p, s->blk[k].bs, s->blk[k].be, collect_var);
    }
    return 1;
}

/* After a sheet is read: the root's sizes, the index, and every body to be
   read again, since a custom property may have changed under it. */
static void settle(reaktor_sheet *s)
{
    char     who[96];
    unsigned k;

    for (k = 0; k < s->nblk; k++) s->blk[k].read = 0;
    s->ndecl = 0;
    s->rem = s->em = 16.0f;
    snprintf(who, sizeof who, "html:root.data-theme-%s", s->theme);
    s->rem = root_font(s, who, 16.0f, 16.0f);
    snprintf(who, sizeof who, "html:root.data-theme-%s body", s->theme);
    s->em = root_font(s, who, s->rem, s->rem);
    index_rules(s);
}

reaktor_sheet *reaktor_sheet_load(const char *const *paths, int count, const char *theme)
{
    reaktor_sheet *s = (reaktor_sheet *)calloc(1, sizeof *s);
    int            i;

    if (!s) return NULL;
    snprintf(s->theme, sizeof s->theme, "%s", theme && *theme ? theme : "light");
    for (i = 0; i < count; i++)
        if (!ingest(s, paths[i])) {
            reaktor_sheet_free(s);
            return NULL;
        }
    settle(s);
    return s;
}

int reaktor_sheet_add(reaktor_sheet *s, const char *path)
{
    if (!s || !path || !ingest(s, path)) return 0;
    settle(s);
    return 1;
}

void reaktor_sheet_free(reaktor_sheet *s)
{
    unsigned i;

    if (!s) return;
    for (i = 0; i < s->nvar; i++) {
        free(s->var[i].name);
        free(s->var[i].value);
    }
    free(s->var);
    free(s->slot);
    free(s->bucket);
    free(s->rule);
    free(s->cmp);
    free(s->neg);
    free(s->nm);
    free(s->decl);
    free(s->blk);
    free(s->src.p);
    free(s->tmp.p);
    free(s);
}

static int set(const decl *d) { return d && !(d->flags & F_INITIAL); }

static float len_of(const decl *const *w, int prop) { return set(w[prop]) ? w[prop]->v : 0.0f; }

static void paint(const decl *d, const unsigned char *current, unsigned char out[4])
{
    if (set(d)) memcpy(out, d->flags & F_CURRENT ? current : d->rgba, 4);
}

static void resolve(const reaktor_sheet *s, const decl *const *w, reaktor_style *o)
{
    float fs = len_of(w, P_FS);
    int   k;

    o->fg[3] = 255;
    paint(w[P_FG], o->fg, o->fg);
    paint(w[P_BG], o->fg, o->bg);
    if (set(w[P_GTO]) && set(w[P_GFROM]) && w[P_GTO]->v > 0.0f) {
        o->gradient = (int)w[P_GTO]->v;
        memcpy(o->gradient_from, w[P_GFROM]->rgba, 4);
        memcpy(o->gradient_to, w[P_GTO]->rgba, 4);
    }
    memcpy(o->border_col, o->fg, 4);
    paint(w[P_BC], o->fg, o->border_col);
    for (k = 0; k < 4; k++) {
        int styled = set(w[P_BS + k]) && w[P_BS + k]->v != 0.0f;

        o->border_w[k] = styled ? (set(w[P_BW + k]) ? w[P_BW + k]->v : 3.0f) : 0.0f;
        o->radius[k]   = len_of(w, P_RAD + k);
        o->pad[k]      = len_of(w, P_PAD + k);
        o->margin[k]   = len_of(w, P_MAR + k);
    }
    o->border     = o->border_w[REAKTOR_SIDE_TOP];
    o->rounding   = o->radius[REAKTOR_CORNER_TL];
    o->pad_x      = o->pad[REAKTOR_SIDE_LEFT];
    o->pad_y      = o->pad[REAKTOR_SIDE_TOP];
    o->width      = len_of(w, P_W);
    o->height     = len_of(w, P_H);
    o->min_width  = len_of(w, P_MINW);
    o->min_height = len_of(w, P_MINH);
    o->max_width  = len_of(w, P_MAXW);
    o->max_height = len_of(w, P_MAXH);
    o->font_px    = fs > 0.0f ? (int)(fs + 0.5f) : 0;
    o->bold       = len_of(w, P_FW) >= 600.0f;
    o->opacity    = set(w[P_OP]) ? w[P_OP]->v : 1.0f;
    if (o->opacity < 0.0f) o->opacity = 0.0f;
    if (o->opacity > 1.0f) o->opacity = 1.0f;
    o->line_height = len_of(w, P_LH);
    if (set(w[P_LH]) && (w[P_LH]->flags & F_NUMBER))
        o->line_height *= fs > 0.0f ? fs : s->em;
}

static const block *read_block(reaktor_sheet *s, unsigned k)
{
    block *b = &s->blk[k];

    if (!b->read) {
        ctx x = context(s);

        b->read = 1;
        b->d0   = s->ndecl;
        each_decl(&x, s->src.p, b->bs, b->be, declare);
        b->nd = s->ndecl - b->d0;
    }
    return b;
}

/* The rules chained from `first` that match q, cascaded into win as indices,
   since reading a body may move the declarations. */
static void cascade(reaktor_sheet *s, unsigned first, const selector *q,
                    unsigned *win, uint64_t *key, reaktor_style *out)
{
    pool     p = { s->cmp, s->neg, s->nm };
    unsigned i, k;

    for (i = first; i; i = s->rule[i - 1].next) {
        const rule  *r = &s->rule[i - 1];
        const block *b;

        if (!chain(&p, s->cmp + r->c0, r->n - 1, q, (int)q->n - 1)) continue;
        b = read_block(s, r->blk);
        if (b->nd) out->matched = 1;
        for (k = b->d0; k < b->d0 + b->nd; k++) {
            const decl *d = &s->decl[k];
            uint64_t    w = (uint64_t)(d->flags & F_IMPORTANT) << 63 |
                            (uint64_t)r->spec << 32 | r->blk;

            if (!win[d->prop] || w >= key[d->prop]) {
                win[d->prop] = k + 1;
                key[d->prop] = w;
            }
        }
    }
}

void reaktor_sheet_style(reaktor_sheet *s, const char *sel, reaktor_style *out)
{
    unsigned        win[P_COUNT] = { 0 }, k;
    uint64_t        key[P_COUNT];
    const decl     *w[P_COUNT];
    const compound *c;
    selector        q;

    memset(out, 0, sizeof *out);
    out->opacity = 1.0f;
    if (!s || !sel || !parse_selector(&q, sel, strlen(sel))) return;
    c = &q.c[q.n - 1];
    cascade(s, s->any, &q, win, key, out);
    if (s->capbucket) {
        if (c->type) cascade(s, bucket_of(s, c->type)->first, &q, win, key, out);
        for (k = 0; k < c->n; k++)
            cascade(s, bucket_of(s, q.nm[c->name + k])->first, &q, win, key, out);
    }
    for (k = 0; k < P_COUNT; k++) w[k] = win[k] ? &s->decl[win[k] - 1] : NULL;
    resolve(s, w, out);
}

int reaktor_sheet_color(const reaktor_sheet *s, const char *name, unsigned char rgba[4])
{
    const var  *v = s && name ? var_find(s, name, strlen(name)) : NULL;
    const char *t;
    size_t      n;
    buf         b = { 0 };
    float       c[4];
    int         ok;

    if (!v) return 0;
    t  = v->value;
    n  = strlen(t);
    ok = resolve_vars(s, &t, &n, &b) && color_of(t, n, c, 0);
    free(b.p);
    if (ok) to_bytes(c, rgba);
    return ok;
}
