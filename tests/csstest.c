#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cssflat.h"
#include "test.h"

#define ok test_ok

static const char g_sheet[] =
    "input:not([type=\"a\"]):not([type=\"b\"]) { color: #ff0000; }\n"
    "a.x.x { color: #00ff00; }\n"
    "a:hover:hover { color: #00ff00; }\n"
    "button.x:hover, input[type=\"range\"] { color: #0000ff; }\n"
    "button:enabled:hover { color: #123456; }\n"
    "button[disabled] { color: #654321; }\n"
    "p { padding: calc(1.5 * 8px) calc(8px / 2); top: calc(0px - 8px); "
    "margin: calc(2 * (3px + 1px)); width: calc(100% - 2 * 8px); }\n"
    "q { background-image: url(data:image/svg+xml;base64,AAAA); color: #010203; }\n"
    "q.v { content: \"a value longer than the engine token\"; color: #040506; }\n"
    ".a-selector-longer-than-the-engines-token, u { color: #070809; }\n"
    "b { border: thin solid #010101; border-left: medium; border-top-width: thick; }\n"
    "i { border-style: thin-ish; }\n"
    "s { border: unset; }\n"
    "u.x { border: inherit; color: #0a0b0c; }\n"
    "a.y { border-top: 2px dotted #0d0e0f !important; }\n"
    "em { color: color-mix(in srgb, rgb(255 0 0 / 50%) 50%, red); "
    "background-color: color-mix(in srgb, hsl(120, 100%, 50%), black); "
    "border-top-color: color-mix(in srgb, #ff000080, rgba(0, 0, 255, 1)); }\n"
    ":root { --slash: /; }\n"
    "dl { background: url(https://x/a.png); color: #111111; }\n"
    "dt { b: x//y; color: #121212; }\n"
    "dd { b: {}x; color: #131313; }\n"
    "dfn { b: var(--slash)*x; color: #141414; }\n"
    "a[href^=\"//\"], kbd { color: #151515; }\n";

static const char g_media[] =
    "@media (prefers-color-scheme: dark) { h1 { color: #000001; } }\n"
    "@media (prefers-reduced-motion: reduce) { h2 { color: #000002; } }\n"
    "@media (prefers-reduced-motion: no-preference) { h3 { color: #000003; } }\n"
    "@media (prefers-contrast: more) { h4 { color: #000004; } }\n"
    "@media screen and (prefers-contrast) { h5 { color: #000005; } }\n"
    "@media print, (prefers-reduced-motion) { h6 { color: #000006; } }\n"
    "@media (prefers-contrast: more) and (max-width: 720px) { ol { color: #000007; } }\n"
    "@media print { ul { color: #000008; } }\n";

static char *
repeat(char *s, const char *piece, int times)
{
    size_t n = strlen(s), k = strlen(piece);
    char  *r = realloc(s, n + k * (size_t)times + 1);
    int    i;

    if (!r) return s;
    for (i = 0; i < times; i++) memcpy(r + n + k * (size_t)i, piece, k);
    r[n + k * (size_t)times] = 0;
    return r;
}

static char *
append(char *s, const char *piece)
{
    return repeat(s, piece, 1);
}

static char *
generated(const char *name)
{
    char *s = calloc(1, 1);
    char  piece[64];
    int   i;

    if (!s) return NULL;
    if (strcmp(name, "calc.css") == 0) {
        s = append(s, "dir { width: calc(");
        s = repeat(s, "(", 40);
        s = append(s, "1px");
        s = repeat(s, ")", 40);
        s = append(s, "); color: #161616; }\n");
    } else if (strcmp(name, "self.css") == 0) {
        s = repeat(s, "@import \"self.css\";\n", 20);
        s = append(s, "kbd { color: #171717; }\n");
    } else if (strncmp(name, "fan", 3) == 0 && name[3] >= '0' && name[3] <= '8') {
        if (name[3] < '8') {
            snprintf(piece, sizeof(piece), "@import \"fan%c.css\";\n", name[3] + 1);
            s = repeat(s, piece, 20);
        }
        s = append(s, "samp { color: #181818; }\n");
    } else if (strcmp(name, "nest.css") == 0) {
        s = append(s, "var { color: ");
        s = repeat(s, "var(--u, ", 1000);
        s = append(s, "red");
        s = repeat(s, ")", 1000);
        s = append(s, "; background-color: var(--u, var(--v, #191919)); }\n");
    } else if (strcmp(name, "laughs.css") == 0) {
        s = append(s, ":root { --a0: xxxxxxxxxx;");
        for (i = 1; i < 10; i++) {
            snprintf(piece, sizeof(piece), " --a%d:", i);
            s = append(s, piece);
            snprintf(piece, sizeof(piece), " var(--a%d)", i - 1);
            s = repeat(s, piece, 10);
            s = append(s, ";");
        }
        s = append(s, " }\nabbr { color: var(--a9); background-color: #1a1a1a; }\n");
    } else {
        free(s);
        return NULL;
    }
    return s;
}

static char *
flat1(const char *name)
{
    return reaktor_css_flatten(&name, 1, "light", 511, NULL);
}

static int
count(const char *s, const char *what)
{
    int n = 0;

    while ((s = strstr(s, what)) != NULL) { n++; s++; }
    return n;
}

char *
reaktor_asset_load(const char *name, size_t *len)
{
    char *out = generated(name);
    long  n = 0;

    if (out) {
        if (len) *len = strlen(out);
        return out;
    }
    if (strcmp(name, "test.css") == 0 || strcmp(name, "media.css") == 0) {
        const char *s = name[0] == 't' ? g_sheet : g_media;

        n = (long)strlen(s);
        out = malloc((size_t)n + 1);
        if (out) memcpy(out, s, (size_t)n + 1);
    } else {
        char  path[1024];
        FILE *f;

        snprintf(path, sizeof path, "%s%s", CSSTEST_SOURCE, name);
        if (!(f = fopen(path, "rb"))) return NULL;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        out = n >= 0 ? malloc((size_t)n + 1) : NULL;
        if (out && fread(out, 1, (size_t)n, f) == (size_t)n) {
            out[n] = 0;
        } else {
            free(out);
            out = NULL;
        }
        fclose(f);
    }
    if (out && len) *len = (size_t)n;
    return out;
}

void
reaktor_free(void *p)
{
    free(p);
}

static int
not_twice(const char *css)
{
    const char *p = css, *q;

    while ((p = strstr(p, ":not(")) != NULL) {
        for (q = p + 5; *q && !strchr(" \t\r\n,{", *q); q++)
            if (strncmp(q, ":not(", 5) == 0) return 1;
        p += 5;
    }
    return 0;
}

int
main(void)
{
    static const char *const test[1] = { "test.css" };
    static const char *const media[1] = { "media.css" };
    static const char *const simple[1] = { "external/simplecss/simple.css" };
    char *out;
    int   i;

    out = reaktor_css_flatten(test, 1, "light", 32, NULL);
    ok("a sheet flattens", out != NULL);
    if (out) {
        ok("two :not() in one compound are dropped", !strstr(out, "not("));
        ok("a class named twice is dropped", !strstr(out, ".x.x"));
        ok("a pseudo-class named twice is dropped", !strstr(out, ":hover:hover"));
        ok("the rest of a selector list stays", strstr(out, "button.x:hover") != NULL);
        ok("an attribute value becomes a class", strstr(out, "input.type-range") != NULL);
        ok("a bare attribute becomes a class", strstr(out, "button.disabled") != NULL);
        ok(":enabled is dropped", strstr(out, "button:hover") != NULL && !strstr(out, ":enabled"));
        ok("calc() over pixels is worked out", strstr(out, "padding: 12px 4px;") != NULL);
        ok("  with a subtraction", strstr(out, "top: -8px;") != NULL);
        ok("  and brackets", strstr(out, "margin: 8px;") != NULL);
        ok("calc() with a percentage is left", strstr(out, "width: calc(100% - 2 * 8px);") != NULL);
        ok("a value with a ';' is dropped", !strstr(out, "base64") && strstr(out, "#010203"));
        ok("a value with '//', '/*' or '}' is dropped",
           strstr(out, "dl {") && !strstr(strstr(out, "dl {"), "/*") &&
           !strstr(out, "//") && !strstr(out, "{}") &&
           strstr(out, "#111111") && strstr(out, "#121212") &&
           strstr(out, "#131313") && strstr(out, "#141414"));
        ok("a selector with '//' is dropped, the rest of its list kept",
           strstr(out, "kbd {") && strstr(out, "#151515"));
        ok("a value past the token is dropped", !strstr(out, "longer") && strstr(out, "#040506"));
        ok("a selector past the token is dropped", !strstr(out, "a-selector") && strstr(out, "u {"));
        ok("border keyword widths become lengths",
           strstr(out, "border-right-width: 1px;") && strstr(out, "border-left-width: 3px;") &&
           strstr(out, "border-top-width: 5px;"));
        ok("a border shorthand is spelled out as longhands",
           !strstr(out, "border: ") && !strstr(out, "border-left: ") &&
           strstr(out, "border-bottom-style: solid;") &&
           strstr(out, "border-bottom-color: #010101;"));
        ok("  with what it leaves out at libcss's own values",
           strstr(out, "border-left-style: none;") &&
           strstr(out, "border-left-color: transparent;"));
        ok("border: unset is no border", strstr(out, "border-top-width: 0px;") != NULL);
        ok("border: inherit is dropped, the rest of its rule kept",
           !strstr(out, "inherit") && strstr(out, "#0a0b0c"));
        ok("!important reaches every longhand",
           strstr(out, "border-top-style: dotted !important;") &&
           strstr(out, "border-top-color: #0d0e0f !important;"));
        ok("  but not inside another word", strstr(out, "thin-ish") != NULL);
        ok("color-mix() takes the space form, hsl() and names",
           !strstr(out, "color-mix") && !strstr(out, "hsl("));
        ok("  and the comma form with a hex alpha", strstr(out, "border-top-color: rgba(") != NULL);
        free(out);
    }

    out = reaktor_css_flatten(media, 1, "light", 0, NULL);
    ok("with no preference, only no-preference queries apply",
       out && strstr(out, "#000003") && !strstr(out, "#000001") && !strstr(out, "#000002") &&
       !strstr(out, "#000004") && !strstr(out, "#000005") && !strstr(out, "#000006"));
    free(out);
    reaktor_css_media(1, 1);
    out = reaktor_css_flatten(media, 1, "dark", 0, NULL);
    ok("reduced motion and more contrast apply theirs",
       out && strstr(out, "#000001") && strstr(out, "#000002") && !strstr(out, "#000003") &&
       strstr(out, "#000004") && strstr(out, "#000005") && strstr(out, "#000006"));
    ok("  but not with a feature not answered here, nor print",
       out && !strstr(out, "#000007") && !strstr(out, "#000008"));
    free(out);
    reaktor_css_media(0, 0);

    for (i = 0; i < 2; i++) {
        out = reaktor_css_flatten(simple, 1, i ? "dark" : "light", 0, NULL);
        ok(i ? "Simple.css flattens, dark" : "Simple.css flattens, light",
           out != NULL && strstr(out, "button") != NULL);
        ok("  and no compound in it holds two :not()", out != NULL && !not_twice(out));
        free(out);
    }

    out = flat1("calc.css");
    ok("calc() nested past its depth is dropped, the rest of its rule kept",
       out && !strstr(out, "width") && strstr(out, "#161616"));
    free(out);
    out = flat1("self.css");
    ok("a sheet that imports itself is read once", out && count(out, "#171717") == 1);
    free(out);
    out = flat1("fan0.css");
    ok("imports that fan out stop at the budget", out && strlen(out) < (16u << 20));
    free(out);
    out = flat1("nest.css");
    ok("var() fallbacks nested past the limit are dropped",
       out && !strstr(out, "red") && strstr(out, "background-color: #191919"));
    free(out);
    out = flat1("laughs.css");
    ok("var() that would grow past the limit is dropped",
       out && strlen(out) < (1u << 20) && !strstr(out, "var(") &&
       strstr(out, "#1a1a1a"));
    free(out);

    return test_done("css");
}
