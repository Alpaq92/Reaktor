/* cssflat.c - see cssflat.h for why this pass exists. */
#include "cssflat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- growable text ------------------------------------------------------- */

typedef struct buf {
    char  *p;
    size_t len, cap;
} buf;

static int buf_reserve(buf *b, size_t extra)
{
    size_t want = b->len + extra + 1;
    char *np;

    if (want <= b->cap) return 1;
    while (b->cap < want) b->cap = b->cap ? b->cap * 2 : 4096;
    np = (char *)realloc(b->p, b->cap);
    if (!np) return 0;
    b->p = np;
    return 1;
}

static int buf_add(buf *b, const char *s, size_t n)
{
    if (!buf_reserve(b, n)) return 0;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 1;
}

static int buf_str(buf *b, const char *s) { return buf_add(b, s, strlen(s)); }
static int buf_ch(buf *b, char c)         { return buf_add(b, &c, 1); }

/* --- custom property map -------------------------------------------------- */

typedef struct var_entry {
    char *name;                 /* including the leading "--" */
    char *value;
} var_entry;

struct curie_cssvars {
    var_entry *v;
    size_t     n, cap;
};

static var_entry *vars_find(curie_cssvars *m, const char *name, size_t nlen)
{
    size_t i;

    for (i = 0; i < m->n; i++)
        if (strlen(m->v[i].name) == nlen && strncmp(m->v[i].name, name, nlen) == 0)
            return &m->v[i];
    return NULL;
}

/* Later declarations replace earlier ones, which is what makes the theme block
 * override :root simply by appearing after it in the file. */
static int vars_set(curie_cssvars *m, const char *name, size_t nlen,
                    const char *value, size_t vlen)
{
    var_entry *e = vars_find(m, name, nlen);

    if (e) {
        char *nv = (char *)malloc(vlen + 1);
        if (!nv) return 0;
        memcpy(nv, value, vlen);
        nv[vlen] = 0;
        free(e->value);
        e->value = nv;
        return 1;
    }
    if (m->n == m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 64;
        var_entry *nv = (var_entry *)realloc(m->v, nc * sizeof(*nv));
        if (!nv) return 0;
        m->v = nv;
        m->cap = nc;
    }
    m->v[m->n].name  = (char *)malloc(nlen + 1);
    m->v[m->n].value = (char *)malloc(vlen + 1);
    if (!m->v[m->n].name || !m->v[m->n].value) {
        free(m->v[m->n].name); free(m->v[m->n].value);
        return 0;
    }
    memcpy(m->v[m->n].name, name, nlen);   m->v[m->n].name[nlen] = 0;
    memcpy(m->v[m->n].value, value, vlen); m->v[m->n].value[vlen] = 0;
    m->n++;
    return 1;
}

const char *curie_cssvars_get(const curie_cssvars *m, const char *name)
{
    size_t i;

    if (!m || !name) return NULL;
    for (i = 0; i < m->n; i++)
        if (strcmp(m->v[i].name, name) == 0) return m->v[i].value;
    return NULL;
}

static int hex1(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int curie_cssvars_color(const curie_cssvars *m, const char *name,
                        unsigned char rgba[4])
{
    const char *v = curie_cssvars_get(m, name);
    int h[8], i, n;

    if (!v) return 0;
    while (*v && isspace((unsigned char)*v)) v++;
    if (*v != '#') return 0;
    v++;
    for (n = 0; n < 8 && v[n]; n++) {
        h[n] = hex1((unsigned char)v[n]);
        if (h[n] < 0) break;
    }
    if (n == 3 || n == 4) {
        for (i = 0; i < 3; i++) rgba[i] = (unsigned char)(h[i] * 17);
        rgba[3] = (unsigned char)(n == 4 ? h[3] * 17 : 255);
        return 1;
    }
    if (n == 6 || n == 8) {
        for (i = 0; i < 3; i++)
            rgba[i] = (unsigned char)(h[i * 2] * 16 + h[i * 2 + 1]);
        rgba[3] = (unsigned char)(n == 8 ? h[6] * 16 + h[7] : 255);
        return 1;
    }
    return 0;
}

void curie_cssvars_free(curie_cssvars *m)
{
    size_t i;

    if (!m) return;
    for (i = 0; i < m->n; i++) { free(m->v[i].name); free(m->v[i].value); }
    free(m->v);
    free(m);
}

/* --- var() substitution --------------------------------------------------- */

/* Writes `src` into `out` with every var(--name[, fallback]) replaced.
 * Returns 0 if a reference resolves to nothing and has no fallback, which is
 * the caller's cue to drop the declaration entirely - the same thing a browser
 * does with an unresolvable var, and better than emitting a broken value. */
static int subst_vars(const curie_cssvars *m, const char *src, size_t len,
                      buf *out)
{
    size_t i = 0;

    while (i < len) {
        const char *at;
        size_t j, depth, name_s, name_e, fb_s, fb_e;
        const char *val;

        if (!(src[i] == 'v' && i + 4 <= len && strncmp(src + i, "var(", 4) == 0)) {
            if (!buf_ch(out, src[i])) return 0;
            i++;
            continue;
        }

        /* Find this var()'s closing parenthesis, allowing nesting. */
        j = i + 4;
        depth = 1;
        while (j < len && depth) {
            if (src[j] == '(') depth++;
            else if (src[j] == ')') depth--;
            if (depth) j++;
        }
        if (depth) return 0;                       /* unbalanced */

        name_s = i + 4;
        while (name_s < j && isspace((unsigned char)src[name_s])) name_s++;
        name_e = name_s;
        while (name_e < j && src[name_e] != ',' && !isspace((unsigned char)src[name_e]))
            name_e++;

        fb_s = fb_e = 0;
        at = (const char *)memchr(src + name_e, ',', j - name_e);
        if (at) {
            fb_s = (size_t)(at - src) + 1;
            while (fb_s < j && isspace((unsigned char)src[fb_s])) fb_s++;
            fb_e = j;
            while (fb_e > fb_s && isspace((unsigned char)src[fb_e - 1])) fb_e--;
        }

        val = NULL;
        if (name_e > name_s) {
            const var_entry *e = vars_find((curie_cssvars *)m,
                                           src + name_s, name_e - name_s);
            if (e) val = e->value;
        }
        if (val) {
            if (!buf_str(out, val)) return 0;
        } else if (at) {
            /* The fallback may itself contain var(). */
            if (!subst_vars(m, src + fb_s, fb_e - fb_s, out)) return 0;
        } else {
            return 0;
        }
        i = j + 1;
    }
    return 1;
}

/* --- unit conversion ------------------------------------------------------ */

/* LCUI's length parser accepts px, %, dp, sp and pt, and nothing else
 * (lib/css/src/data_types.c) - there is no rem and no em. milligram is written
 * almost entirely in rem, on the html { font-size: 62.5% } that makes 1rem
 * exactly 10px, so a stylesheet handed over untouched loses every length it
 * has. They are resolved here, in the same pass that resolves var().
 *
 * em is resolved against the root as well. The only em in these stylesheets is
 * body's font-size, and body's parent *is* the root element, so the two agree;
 * anything nested deeper would need real inheritance, which LCUI does not
 * implement either. */
static int num_start_ok(const char *s, size_t i)
{
    char c;

    if (i == 0) return 1;
    c = s[i - 1];
    /* Inside an identifier or a hex colour, digits are not a length. */
    return !(isalnum((unsigned char)c) || c == '#' || c == '_');
}

static void convert_units(const char *s, size_t len, float root_px, buf *out)
{
    size_t i = 0;

    while (i < len) {
        size_t j = i, ns = i;
        int digits = 0, unit_len = 0;
        char num[64], px[64];

        if (!num_start_ok(s, i)) { buf_ch(out, s[i]); i++; continue; }

        if (s[j] == '-' || s[j] == '+') j++;
        while (j < len && (isdigit((unsigned char)s[j]) || s[j] == '.')) {
            if (isdigit((unsigned char)s[j])) digits = 1;
            j++;
        }
        if (!digits) { buf_ch(out, s[i]); i++; continue; }

        if (j + 3 <= len && strncmp(s + j, "rem", 3) == 0 &&
            (j + 3 == len || !isalpha((unsigned char)s[j + 3])))
            unit_len = 3;
        else if (j + 2 <= len && strncmp(s + j, "em", 2) == 0 &&
                 (j + 2 == len || !isalpha((unsigned char)s[j + 2])))
            unit_len = 2;

        if (!unit_len) {                     /* a plain number - leave it */
            buf_add(out, s + ns, j - ns);
            i = j;
            continue;
        }
        if (j - ns >= sizeof(num)) { buf_add(out, s + ns, j - ns); i = j; continue; }
        memcpy(num, s + ns, j - ns);
        num[j - ns] = 0;
        snprintf(px, sizeof(px), "%gpx", atof(num) * root_px);
        buf_str(out, px);
        i = j + unit_len;
    }
}

/* --- source scanning ------------------------------------------------------ */

/* Comments are removed up front so nothing downstream has to consider them.
 * Open-Color's headings contain box-drawing characters, which LCUI's parser
 * reports as property names when it meets them. */
static void strip_comments(char *s)
{
    char *r = s, *w = s;

    while (*r) {
        if (r[0] == '/' && r[1] == '*') {
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

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    size_t got;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    got = fread(b, 1, (size_t)n, f);
    b[got] = 0;
    fclose(f);
    return b;
}

static void trim(const char *s, size_t *start, size_t *end)
{
    while (*start < *end && isspace((unsigned char)s[*start])) (*start)++;
    while (*end > *start && isspace((unsigned char)s[*end - 1])) (*end)--;
}

/* True for the selectors LCUI's parser cannot represent: attribute selectors,
 * pseudo-elements, the universal selector and child/sibling combinators. A
 * selector list is filtered part by part, so `.button, input[type='submit']`
 * keeps the half that works. */
static int selector_unsupported(const char *s, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (s[i] == '[' || s[i] == '*' || s[i] == '>' || s[i] == '+' || s[i] == '~')
            return 1;
        if (s[i] == ':' && i + 1 < len && s[i + 1] == ':')
            return 1;
    }
    return len == 0;
}

/* Does this selector list contain the theme block we are resolving? Matching
 * is textual because the attribute selector never reaches the engine. */
static int is_theme_selector(const char *s, size_t len, const char *theme)
{
    char want[64];
    size_t i, wl;

    snprintf(want, sizeof(want), "[data-theme=\"%s\"]", theme ? theme : "");
    wl = strlen(want);
    if (len < wl) return 0;
    for (i = 0; i + wl <= len; i++)
        if (strncmp(s + i, want, wl) == 0) return 1;
    return 0;
}

static int is_root_selector(const char *s, size_t len)
{
    size_t i;

    for (i = 0; i + 5 <= len; i++)
        if (strncmp(s + i, ":root", 5) == 0) return 1;
    return 0;
}

/* Walks one declaration block, calling back per `prop: value` pair. */
typedef void (*decl_fn)(void *ctx, const char *p, size_t plen,
                        const char *v, size_t vlen);

static void each_decl(const char *s, size_t start, size_t end,
                      decl_fn fn, void *ctx)
{
    size_t i = start;

    while (i < end) {
        size_t ps = i, pe, vs, ve, depth = 0;

        while (i < end && s[i] != ':' && s[i] != ';') i++;
        if (i >= end || s[i] == ';') { i++; continue; }
        pe = i++;
        vs = i;
        while (i < end && (depth > 0 || s[i] != ';')) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')' && depth) depth--;
            i++;
        }
        ve = i;
        if (i < end) i++;
        trim(s, &ps, &pe);
        trim(s, &vs, &ve);
        if (pe > ps && ve > vs) fn(ctx, s + ps, pe - ps, s + vs, ve - vs);
    }
}

typedef struct collect_ctx { curie_cssvars *m; } collect_ctx;

static void collect_one(void *c, const char *p, size_t plen,
                        const char *v, size_t vlen)
{
    collect_ctx *ctx = (collect_ctx *)c;

    if (plen > 2 && p[0] == '-' && p[1] == '-')
        vars_set(ctx->m, p, plen, v, vlen);
}

/* Picks up html { font-size: ... }, which is what one rem is worth. */
typedef struct root_ctx { float px; } root_ctx;

static void root_one(void *c, const char *p, size_t plen,
                     const char *v, size_t vlen)
{
    root_ctx *ctx = (root_ctx *)c;
    char buf[64];

    if (plen != 9 || strncmp(p, "font-size", 9) != 0) return;
    if (vlen >= sizeof(buf)) return;
    memcpy(buf, v, vlen);
    buf[vlen] = 0;
    if (vlen && buf[vlen - 1] == '%') ctx->px = (float)(atof(buf) * 16.0 / 100.0);
    else                              ctx->px = (float)atof(buf);
    if (ctx->px <= 0.0f) ctx->px = 16.0f;
}

typedef struct emit_ctx {
    const curie_cssvars *m;
    buf                 *out;
    int                  wrote;
    float                root_px;
} emit_ctx;

static void emit_one(void *c, const char *p, size_t plen,
                     const char *v, size_t vlen)
{
    emit_ctx *ctx = (emit_ctx *)c;
    buf tmp, conv;

    if (plen > 2 && p[0] == '-' && p[1] == '-') return;   /* resolved already */

    memset(&tmp, 0, sizeof(tmp));
    if (!subst_vars(ctx->m, v, vlen, &tmp)) { free(tmp.p); return; }

    memset(&conv, 0, sizeof(conv));
    /* A percentage font-size is the root's own declaration - the 62.5% that
     * sets up the rem scale - and LCUI would resolve it against nothing, so
     * it becomes the pixel size it stands for. Percentages on every other
     * property are real and stay. */
    if (plen == 9 && strncmp(p, "font-size", 9) == 0 &&
        tmp.len > 1 && tmp.p[tmp.len - 1] == '%') {
        char px[64];
        snprintf(px, sizeof(px), "%gpx", atof(tmp.p) * 16.0 / 100.0);
        buf_str(&conv, px);
    } else {
        convert_units(tmp.p ? tmp.p : "", tmp.len, ctx->root_px, &conv);
    }

    buf_add(ctx->out, p, plen);
    buf_str(ctx->out, ": ");
    buf_add(ctx->out, conv.p ? conv.p : "", conv.len);
    buf_str(ctx->out, ";\n");
    ctx->wrote = 1;
    free(tmp.p);
    free(conv.p);
}

/* --- the pass ------------------------------------------------------------- */

static char *flatten(char *src, const char *theme, curie_cssvars **out_vars)
{
    curie_cssvars *m = (curie_cssvars *)calloc(1, sizeof(*m));
    buf out;
    size_t i, len, pass;
    root_ctx root;

    root.px = 16.0f;

    if (!m) { free(src); return NULL; }
    strip_comments(src);
    len = strlen(src);

    /* Pass 1: gather custom properties from :root and the active theme. */
    i = 0;
    while (i < len) {
        size_t sel_s, sel_e, body_s, body_e, depth;

        sel_s = i;
        while (i < len && src[i] != '{' && src[i] != '}') i++;
        if (i >= len) break;
        if (src[i] == '}') { i++; continue; }
        sel_e = i++;
        body_s = i;
        depth = 1;
        while (i < len && depth) {
            if (src[i] == '{') depth++;
            else if (src[i] == '}') depth--;
            if (depth) i++;
        }
        body_e = i;
        if (i < len) i++;

        trim(src, &sel_s, &sel_e);
        if (is_root_selector(src + sel_s, sel_e - sel_s) ||
            is_theme_selector(src + sel_s, sel_e - sel_s, theme)) {
            collect_ctx ctx;
            ctx.m = m;
            each_decl(src, body_s, body_e, collect_one, &ctx);
        }
        if (sel_e - sel_s == 4 && strncmp(src + sel_s, "html", 4) == 0)
            each_decl(src, body_s, body_e, root_one, &root);
    }

    /* Resolve references between custom properties. --app-surface names
     * --oc-violet-0, so one substitution pass is not enough; four is well
     * clear of the depth this project uses and terminates on a cycle. */
    for (pass = 0; pass < 4; pass++) {
        size_t k;
        int changed = 0;

        for (k = 0; k < m->n; k++) {
            buf t;
            if (!strstr(m->v[k].value, "var(")) continue;
            memset(&t, 0, sizeof(t));
            if (subst_vars(m, m->v[k].value, strlen(m->v[k].value), &t) && t.p) {
                free(m->v[k].value);
                m->v[k].value = t.p;
                changed = 1;
            } else {
                free(t.p);
            }
        }
        if (!changed) break;
    }

    /* Pass 2: emit everything else, with var() resolved. */
    memset(&out, 0, sizeof(out));
    buf_str(&out, "/* Flattened by cssflat.c: custom properties resolved,\n"
                  "   @media and unsupported selectors removed. */\n");

    i = 0;
    while (i < len) {
        size_t sel_s, sel_e, body_s, body_e, depth, part, n_emitted;
        emit_ctx ctx;

        sel_s = i;
        while (i < len && src[i] != '{' && src[i] != '}') i++;
        if (i >= len) break;
        if (src[i] == '}') { i++; continue; }
        sel_e = i++;
        body_s = i;
        depth = 1;
        while (i < len && depth) {
            if (src[i] == '{') depth++;
            else if (src[i] == '}') depth--;
            if (depth) i++;
        }
        body_e = i;
        if (i < len) i++;

        trim(src, &sel_s, &sel_e);
        if (sel_e <= sel_s) continue;

        /* @media, @supports and friends: LCUI implements none of them, and
         * their bodies are nested blocks this emitter does not descend into. */
        if (src[sel_s] == '@') continue;
        if (is_root_selector(src + sel_s, sel_e - sel_s)) continue;

        /* Keep only the parts of the selector list LCUI can parse. */
        {
            buf sel;
            memset(&sel, 0, sizeof(sel));
            n_emitted = 0;
            part = sel_s;
            while (part < sel_e) {
                size_t ps = part, pe;
                while (part < sel_e && src[part] != ',') part++;
                pe = part;
                if (part < sel_e) part++;
                trim(src, &ps, &pe);
                if (selector_unsupported(src + ps, pe - ps)) continue;
                if (n_emitted) buf_str(&sel, ", ");
                buf_add(&sel, src + ps, pe - ps);
                n_emitted++;
            }
            if (!n_emitted) { free(sel.p); continue; }

            ctx.m = m;
            ctx.out = &out;
            ctx.wrote = 0;
            ctx.root_px = root.px;
            {
                /* Emit into a scratch buffer first: a block whose every
                 * declaration was a custom property must not leave an empty
                 * rule behind. */
                buf body;
                emit_ctx bctx;
                memset(&body, 0, sizeof(body));
                bctx.m = m; bctx.out = &body; bctx.wrote = 0;
                bctx.root_px = root.px;
                each_decl(src, body_s, body_e, emit_one, &bctx);
                if (bctx.wrote) {
                    buf_add(&out, sel.p, sel.len);
                    buf_str(&out, " {\n");
                    buf_add(&out, body.p ? body.p : "", body.len);
                    buf_str(&out, "}\n");
                }
                free(body.p);
            }
            free(sel.p);
        }
    }

    free(src);
    if (out_vars) *out_vars = m; else curie_cssvars_free(m);
    return out.p;
}

char *curie_css_flatten_text(const char *const *texts, int count,
                             const char *theme, curie_cssvars **out_vars)
{
    buf all;
    int i;

    memset(&all, 0, sizeof(all));
    for (i = 0; i < count; i++) {
        if (!texts[i]) continue;
        if (!buf_str(&all, texts[i]) || !buf_str(&all, "\n")) {
            free(all.p);
            return NULL;
        }
    }
    if (!all.p) all.p = (char *)calloc(1, 1);
    return flatten(all.p, theme, out_vars);
}

char *curie_css_flatten(const char *const *paths, int count,
                        const char *theme, curie_cssvars **out_vars)
{
    buf all;
    int i;

    memset(&all, 0, sizeof(all));
    for (i = 0; i < count; i++) {
        char *t = read_file(paths[i]);
        if (!t) { free(all.p); return NULL; }
        if (!buf_str(&all, t) || !buf_str(&all, "\n")) {
            free(t); free(all.p);
            return NULL;
        }
        free(t);
    }
    if (!all.p) all.p = (char *)calloc(1, 1);
    return flatten(all.p, theme, out_vars);
}
