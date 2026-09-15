#include "locale_internal.h"

#include <ctype.h>
#include <string.h>

#define PLURAL_DEPTH 64

typedef struct plural_parser {
    const char   *s;
    unsigned long n;
    int           depth;
    int           bad;
} plural_parser;

static unsigned long expr(plural_parser *p);

static void
skip(plural_parser *p)
{
    while (isspace((unsigned char)*p->s)) p->s++;
}

static int
take(plural_parser *p, const char *op)
{
    size_t len = strlen(op);

    skip(p);
    if (strncmp(p->s, op, len) != 0) return 0;
    p->s += len;
    return 1;
}

static unsigned long
primary(plural_parser *p)
{
    unsigned long v = 0;

    skip(p);
    if (*p->s == 'n') {
        p->s++;
        return p->n;
    }
    if (isdigit((unsigned char)*p->s)) {
        while (isdigit((unsigned char)*p->s))
            v = v * 10 + (unsigned long)(*p->s++ - '0');
        return v;
    }
    if (take(p, "(")) {
        v = expr(p);
        if (!take(p, ")")) p->bad = 1;
        return v;
    }
    p->bad = 1;
    return 0;
}

static unsigned long
unary(plural_parser *p)
{
    unsigned long v;

    skip(p);
    if (*p->s != '!' || p->s[1] == '=') return primary(p);
    if (++p->depth > PLURAL_DEPTH) {
        p->bad = 1;
        return 0;
    }
    p->s++;
    v = !unary(p);
    p->depth--;
    return v;
}

static unsigned long
mul(plural_parser *p)
{
    unsigned long v = unary(p), r;

    for (;;) {
        skip(p);
        if (*p->s == '*') {
            p->s++;
            v *= unary(p);
        } else if (*p->s == '/' || *p->s == '%') {
            char op = *p->s++;

            r = unary(p);
            if (r == 0) {
                p->bad = 1;
                return 0;
            }
            v = op == '/' ? v / r : v % r;
        } else {
            return v;
        }
    }
}

static unsigned long
add(plural_parser *p)
{
    unsigned long v = mul(p);

    for (;;) {
        skip(p);
        if (*p->s == '+') {
            p->s++;
            v += mul(p);
        } else if (*p->s == '-') {
            p->s++;
            v -= mul(p);
        } else {
            return v;
        }
    }
}

static unsigned long
rel(plural_parser *p)
{
    unsigned long v = add(p), r;

    for (;;) {
        if (take(p, "<="))      { r = add(p); v = v <= r; }
        else if (take(p, ">=")) { r = add(p); v = v >= r; }
        else if (take(p, "<"))  { r = add(p); v = v <  r; }
        else if (take(p, ">"))  { r = add(p); v = v >  r; }
        else return v;
    }
}

static unsigned long
eq(plural_parser *p)
{
    unsigned long v = rel(p), r;

    for (;;) {
        if (take(p, "=="))      { r = rel(p); v = v == r; }
        else if (take(p, "!=")) { r = rel(p); v = v != r; }
        else return v;
    }
}

static unsigned long
and_(plural_parser *p)
{
    unsigned long v = eq(p), r;

    while (take(p, "&&")) {
        r = eq(p);
        v = v && r;
    }
    return v;
}

static unsigned long
or_(plural_parser *p)
{
    unsigned long v = and_(p), r;

    while (take(p, "||")) {
        r = and_(p);
        v = v || r;
    }
    return v;
}

static unsigned long
expr(plural_parser *p)
{
    unsigned long c, a, b;

    if (++p->depth > PLURAL_DEPTH) {
        p->bad = 1;
        return 0;
    }
    c = or_(p);
    if (take(p, "?")) {
        a = expr(p);
        if (!take(p, ":")) p->bad = 1;
        b = expr(p);
        c = c ? a : b;
    }
    p->depth--;
    return c;
}

long
reaktor_plural_eval(const char *text, unsigned long n)
{
    plural_parser p;
    const char   *at;
    unsigned long v;

    if (!text) return -1;
    at = strstr(text, "plural=");
    p.s     = at ? at + 7 : text;
    p.n     = n;
    p.depth = 0;
    p.bad   = 0;

    v = expr(&p);
    if (take(&p, ";")) skip(&p);
    if (p.bad || *p.s) return -1;
    return (long)v;
}
