#include "cssflat.h"
#include "reaktor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

typedef struct var_entry {
    char *name;
    char *value;
} var_entry;

struct reaktor_cssvars {
    var_entry *v;
    size_t     n, cap;
};

static var_entry *vars_find(reaktor_cssvars *m, const char *name, size_t nlen)
{
    size_t i;

    for (i = 0; i < m->n; i++)
        if (strlen(m->v[i].name) == nlen && strncmp(m->v[i].name, name, nlen) == 0)
            return &m->v[i];
    return NULL;
}

static int vars_set(reaktor_cssvars *m, const char *name, size_t nlen,
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

const char *reaktor_cssvars_get(const reaktor_cssvars *m, const char *name)
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

int reaktor_cssvars_color(const reaktor_cssvars *m, const char *name,
                          unsigned char rgba[4])
{
    const char *v = reaktor_cssvars_get(m, name);
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

void reaktor_cssvars_free(reaktor_cssvars *m)
{
    size_t i;

    if (!m) return;
    for (i = 0; i < m->n; i++) { free(m->v[i].name); free(m->v[i].value); }
    free(m->v);
    free(m);
}

static int subst_vars(const reaktor_cssvars *m, const char *src, size_t len,
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

        j = i + 4;
        depth = 1;
        while (j < len && depth) {
            if (src[j] == '(') depth++;
            else if (src[j] == ')') depth--;
            if (depth) j++;
        }
        if (depth) return 0;

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
            const var_entry *e = vars_find((reaktor_cssvars *)m,
                                           src + name_s, name_e - name_s);
            if (e) val = e->value;
        }
        if (val) {
            if (!buf_str(out, val)) return 0;
        } else if (at) {
            if (!subst_vars(m, src + fb_s, fb_e - fb_s, out)) return 0;
        } else {
            return 0;
        }
        i = j + 1;
    }
    return 1;
}

static int num_start_ok(const char *s, size_t i)
{
    char c;

    if (i == 0) return 1;
    c = s[i - 1];
    return !(isalnum((unsigned char)c) || c == '#' || c == '_');
}

static void convert_units(const char *s, size_t len, float rem_px, float em_px,
                          buf *out)
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

        if (!unit_len) {
            buf_add(out, s + ns, j - ns);
            i = j;
            continue;
        }
        if (j - ns >= sizeof(num)) { buf_add(out, s + ns, j - ns); i = j; continue; }
        memcpy(num, s + ns, j - ns);
        num[j - ns] = 0;
        snprintf(px, sizeof(px), "%gpx",
                 atof(num) * (unit_len == 3 ? rem_px : em_px));
        buf_str(out, px);
        i = j + unit_len;
    }
}

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

static void trim(const char *s, size_t *start, size_t *end)
{
    while (*start < *end && isspace((unsigned char)s[*start])) (*start)++;
    while (*end > *start && isspace((unsigned char)s[*end - 1])) (*end)--;
}

/* [name="value"] -> .name-value, in place, for the one shape that carries
 * real styling in a document sheet. Anything else in brackets is still
 * unrepresentable and the rule still goes.
 *
 * Writes into `out` and answers its length, or 0 if the selector cannot be
 * rewritten. The result is never longer than the input: [x="y"] is six
 * characters of punctuation and .x-y is two. */
static size_t selector_rewrite(const char *s, size_t len, char *out,
                               size_t cap)
{
    size_t i = 0, n = 0;

    while (i < len) {
        if (s[i] != '[') {
            if (n + 1 >= cap) return 0;
            out[n++] = s[i++];
            continue;
        }
        {
            size_t j = i + 1, eq, ve, vs;

            while (j < len && s[j] != ']' && s[j] != '=') j++;
            if (j >= len || s[j] != '=') return 0;   /* [attr] alone */
            /* Only a plain equals. ^= $= *= |= ~= select on part of the
             * value, which a class cannot express - folding the operator
             * into the name produced a rule registered under something
             * nothing could ever match, which is worse than dropping it. */
            if (j > i + 1 && strchr("^$*|~", s[j - 1])) return 0;
            eq = j;
            vs = eq + 1;
            if (vs < len && (s[vs] == '"' || s[vs] == '\'')) vs++;
            ve = vs;
            while (ve < len && s[ve] != ']' && s[ve] != '"' && s[ve] != '\'')
                ve++;
            if (ve >= len) return 0;
            j = ve;
            while (j < len && s[j] != ']') j++;
            if (j >= len) return 0;

            if (n + 2 + (eq - i - 1) + (ve - vs) >= cap) return 0;
            out[n++] = '.';
            memcpy(out + n, s + i + 1, eq - i - 1);
            n += eq - i - 1;
            out[n++] = '-';
            memcpy(out + n, s + vs, ve - vs);
            n += ve - vs;
            i = j + 1;
        }
    }
    out[n] = 0;
    return n;
}

static int selector_unsupported(const char *s, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (s[i] == '*' || s[i] == '>' || s[i] == '+' || s[i] == '~')
            return 1;
        if (s[i] == ':' && i + 1 < len && s[i + 1] == ':')
            return 1;
    }
    return len == 0;
}

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

typedef struct collect_ctx { reaktor_cssvars *m; } collect_ctx;

static void collect_one(void *c, const char *p, size_t plen,
                        const char *v, size_t vlen)
{
    collect_ctx *ctx = (collect_ctx *)c;

    if (plen > 2 && p[0] == '-' && p[1] == '-')
        vars_set(ctx->m, p, plen, v, vlen);
}

/* px is the rem base - html's font-size, 16 unless the sheet says otherwise.
 * em is what an element inherits, which is body's if it sets one. */
typedef struct root_ctx { float px, em; } root_ctx;

/* A length in px, resolving a unit against the rem base. Used for the two
 * font-size declarations that establish the bases themselves, so it cannot
 * call convert_units without going in a circle. */
static float base_length(const char *v, size_t vlen, float rem_px)
{
    char   b[64];
    size_t i = 0;
    double n;

    if (vlen == 0 || vlen >= sizeof(b)) return 0.0f;
    for (i = 0; i < vlen; i++)
        b[i] = (char)tolower((unsigned char)v[i]);
    b[vlen] = 0;

    /* The number, and then exactly where it ended. atof would stop at the
     * first letter and never say so, which is how `1.15rem !important` came
     * back as 1.15 and was believed. */
    i = 0;
    if (b[i] == '-' || b[i] == '+') i++;
    while (i < vlen && (isdigit((unsigned char)b[i]) || b[i] == '.')) i++;
    if (i == 0) return 0.0f;
    n = atof(b);

    /* Only the units this can convert. A sheet may legitimately say pt or
     * ch or vw; answering 0 leaves the caller on its default, which is a
     * better wrong answer than treating the number as pixels. */
    if (strcmp(b + i, "%") == 0)   return (float)(n * rem_px / 100.0);
    if (strcmp(b + i, "rem") == 0) return (float)(n * rem_px);
    if (strcmp(b + i, "em") == 0)  return (float)(n * rem_px);
    if (strcmp(b + i, "px") == 0)  return (float)n;
    if (b[i] == 0)                 return (float)n;
    return 0.0f;
}

/* Whether a selector list names this element on its own - `body` or the
 * `html, body` a sheet is just as likely to write. An exact four-byte
 * compare matched only the first spelling and silently missed the second. */
static int selector_names(const char *src, size_t s, size_t e,
                          const char *want)
{
    size_t wl = strlen(want);

    while (s < e) {
        size_t ps = s, pe;

        while (s < e && src[s] != ',') s++;
        pe = s;
        if (s < e) s++;
        trim(src, &ps, &pe);
        if (pe - ps == wl && strncmp(src + ps, want, wl) == 0) return 1;
    }
    return 0;
}

static void root_one(void *c, const char *p, size_t plen,
                     const char *v, size_t vlen)
{
    root_ctx *ctx = (root_ctx *)c;
    float n;

    if (plen != 9 || strncmp(p, "font-size", 9) != 0) return;
    n = base_length(v, vlen, 16.0f);
    /* html is the rem base. It is the em base only until a body rule says
     * otherwise - setting both here meant an html rule appearing later in
     * the concatenated sheets discarded the body size. */
    if (n > 0.0f) {
        if (ctx->em == ctx->px) ctx->em = n;
        ctx->px = n;
    }
}

/* body's font-size, which is the one nearly every element inherits. */
static void body_one(void *c, const char *p, size_t plen,
                     const char *v, size_t vlen)
{
    root_ctx *ctx = (root_ctx *)c;
    float n;

    if (plen != 9 || strncmp(p, "font-size", 9) != 0) return;
    n = base_length(v, vlen, ctx->px);
    if (n > 0.0f) ctx->em = n;
}

typedef struct emit_ctx {
    const reaktor_cssvars *m;
    buf                 *out;
    int                  wrote;
    float                rem_px, em_px;
} emit_ctx;

static void emit_one(void *c, const char *p, size_t plen,
                     const char *v, size_t vlen)
{
    emit_ctx *ctx = (emit_ctx *)c;
    buf tmp, conv;

    if (plen > 2 && p[0] == '-' && p[1] == '-') return;

    memset(&tmp, 0, sizeof(tmp));
    if (!subst_vars(ctx->m, v, vlen, &tmp)) { free(tmp.p); return; }

    memset(&conv, 0, sizeof(conv));
    if (plen == 9 && strncmp(p, "font-size", 9) == 0 &&
        tmp.len > 1 && tmp.p[tmp.len - 1] == '%') {
        char px[64];
        snprintf(px, sizeof(px), "%gpx",
                 atof(tmp.p) * (double)ctx->em_px / 100.0);
        buf_str(&conv, px);
    } else {
        convert_units(tmp.p ? tmp.p : "", tmp.len,
                      ctx->rem_px, ctx->em_px, &conv);
    }

    buf_add(ctx->out, p, plen);
    buf_str(ctx->out, ": ");
    buf_add(ctx->out, conv.p ? conv.p : "", conv.len);
    buf_str(ctx->out, ";\n");
    ctx->wrote = 1;
    free(tmp.p);
    free(conv.p);
}

static void
unwrap_color_scheme(char *src, const char *theme)
{
    const char *want = (theme && *theme) ? theme : "light";
    size_t i = 0, len = strlen(src);

    while (i < len) {
        size_t at, prelude_e, body_s, depth;
        int    keep;

        if (src[i] != '@' || strncmp(src + i, "@media", 6) != 0) { i++; continue; }
        at = i;

        prelude_e = at;
        while (prelude_e < len && src[prelude_e] != '{' && src[prelude_e] != '}')
            prelude_e++;
        if (prelude_e >= len || src[prelude_e] != '{') break;

        {
            size_t n = prelude_e - at;
            char   low[256];
            size_t k;

            if (n >= sizeof(low)) n = sizeof(low) - 1;
            for (k = 0; k < n; k++) {
                char c = src[at + k];
                low[k] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            }
            low[n] = '\0';
            keep = strstr(low, "prefers-color-scheme") != NULL &&
                   strstr(low, want) != NULL;
        }

        body_s = prelude_e + 1;
        depth = 1;
        i = body_s;
        while (i < len && depth) {
            if (src[i] == '{') depth++;
            else if (src[i] == '}') depth--;
            if (depth) i++;
        }
        if (i >= len) break;

        if (keep) {
            memset(src + at, ' ', prelude_e - at + 1);
            src[i] = ' ';
        } else {
            memset(src + at, ' ', i - at + 1);
        }
        i++;
    }
}

static char *flatten(char *src, const char *theme, reaktor_cssvars **out_vars)
{
    reaktor_cssvars *m = (reaktor_cssvars *)calloc(1, sizeof(*m));
    buf out;
    size_t i, len, pass;
    root_ctx root;

    root.px = 16.0f;
    root.em = 16.0f;

    if (!m) { free(src); return NULL; }
    strip_comments(src);
    unwrap_color_scheme(src, theme);
    len = strlen(src);

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
        if (selector_names(src, sel_s, sel_e, "html"))
            each_decl(src, body_s, body_e, root_one, &root);
        if (selector_names(src, sel_s, sel_e, "body"))
            each_decl(src, body_s, body_e, body_one, &root);
    }

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

        if (src[sel_s] == '@') continue;
        if (is_root_selector(src + sel_s, sel_e - sel_s)) continue;

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
                if (memchr(src + ps, '[', pe - ps)) {
                    char   rw[256];
                    size_t rn = selector_rewrite(src + ps, pe - ps,
                                                 rw, sizeof(rw));

                    if (!rn) continue;
                    if (n_emitted) buf_str(&sel, ", ");
                    buf_add(&sel, rw, rn);
                } else {
                    if (n_emitted) buf_str(&sel, ", ");
                    buf_add(&sel, src + ps, pe - ps);
                }
                n_emitted++;
            }
            if (!n_emitted) { free(sel.p); continue; }

            ctx.m = m;
            ctx.out = &out;
            ctx.wrote = 0;
            ctx.rem_px = root.px;
            ctx.em_px  = root.em;
            {
                buf body;
                emit_ctx bctx;
                memset(&body, 0, sizeof(body));
                bctx.m = m; bctx.out = &body; bctx.wrote = 0;
                bctx.rem_px = root.px;
                bctx.em_px  = root.em;
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
    if (out_vars) *out_vars = m; else reaktor_cssvars_free(m);
    return out.p;
}

char *reaktor_css_flatten(const char *const *paths, int count,
                          const char *theme, reaktor_cssvars **out_vars)
{
    buf all;
    int i;

    memset(&all, 0, sizeof(all));
    for (i = 0; i < count; i++) {
        char *t = reaktor_read_file(paths[i], NULL);
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
