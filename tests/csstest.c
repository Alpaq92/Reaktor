#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "css.h"
#include "test.h"

#define ok test_ok
#define check test_eq

static const char g_sheet[] =
    "select:not([type=\"a\"]):not([type=\"b\"]) { color: #ff0000; }\n"
    "a.x.x { color: #00ff00; }\n"
    "a:hover:hover { color: #00ff00; }\n"
    "button.x:hover, input[type=\"range\"] { color: #0000ff; }\n"
    "button:enabled:hover { color: #123456; }\n"
    "button[disabled] { color: #654321; }\n"
    "p { padding: calc(1.5 * 8px) calc(8px / 2); "
    "margin: calc(2 * (3px + 1px)); width: calc(100% - 2 * 8px); }\n"
    "q { background-image: url(data:image/svg+xml;base64,AAAA); color: #010203; }\n"
    "q.v { content: \"a value longer than any token the old engine held\"; color: #040506; }\n"
    ".a-selector-longer-than-any-token-the-old-engine-held, u { color: #070809; }\n"
    "b { border: thin solid #010101; border-left: medium; border-top-width: thick; }\n"
    "i { border-style: thin-ish; }\n"
    "s { border: unset; }\n"
    "u.x { border: inherit; color: #0a0b0c; }\n"
    "a.y { border-top: 2px dotted #0d0e0f !important; }\n"
    "a.y.z { border-top: 1px solid #ffffff; }\n"
    "em { color: color-mix(in srgb, rgb(255 0 0 / 50%) 50%, red); "
    "background-color: color-mix(in srgb, hsl(120, 100%, 50%), black); "
    "border-top-color: color-mix(in srgb, #ff000080, rgba(0, 0, 255, 1)); }\n"
    ":root { --slash: /; --accent: #0a1b2c; --mixed: color-mix(in srgb, var(--accent), white 0%); }\n"
    "dl { background: url(https://x/a.png); color: #111111; }\n"
    "dt { b: x//y; color: #121212; }\n"
    "dd { b: {}x; color: #131313; }\n"
    "dfn { b: var(--slash)*x; color: #141414; }\n"
    "a[href^=\"//\"], kbd { color: #151515; }\n"
    "ol { color: #200000; }\n"
    "ol { color: #200001; }\n"
    "ol.k { color: #200002; }\n"
    "ol { color: #200003 !important; padding: 1px; padding: 2px; }\n"
    "nav .item { color: #300000; }\n"
    "#\\66oo, .a\\:b, [data-x=\"\\31 2\"] { color: #252525; }\n"
    "nav > .item { margin: 4px; }\n"
    "html { font-size: 20px; }\n"
    "body { font-size: 0.8rem; }\n"
    "h1 { font-size: 2em; padding: 1rem; font-weight: 600; opacity: 50%; }\n"
    "h2 { font-size: 150%; line-height: 24px; border: 2px solid; color: #405060; }\n"
    "h3 { background: var(--accent); border-top-color: currentcolor; color: #010101; }\n";

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
    } else if (strcmp(name, "open.css") == 0) {
        s = append(s, "p { color: #222222; q { color: \"x");
    } else if (strcmp(name, "after.css") == 0) {
        s = append(s, "kbd { color: #232323; }\n");
    } else if (strcmp(name, "late.css") == 0) {
        s = append(s, "h1 { content: \"@import 'nest.css';\"; color: #242424; }\n"
                      "@import \"nest.css\";\n");
    } else if (strcmp(name, "vars.css") == 0) {
        s = append(s, ":root {");
        for (i = 0; i < 20000; i++) {
            snprintf(piece, sizeof(piece), " --v%d: #%06x;", i, i);
            s = append(s, piece);
        }
        s = append(s, " }\nh1 { color: var(--v19999); }\n");
    } else if (strcmp(name, "empty.css") == 0) {
        s = append(s, ":root { --e0: var(--x,);");
        for (i = 1; i < 12; i++) {
            snprintf(piece, sizeof(piece), " --e%d:", i);
            s = append(s, piece);
            snprintf(piece, sizeof(piece), " var(--e%d)", i - 1);
            s = repeat(s, piece, 10);
            s = append(s, ";");
        }
        s = append(s, " }\nabbr { color: var(--e11) red; background-color: #1b1b1b; }\n");
    } else if (strcmp(name, "themes.css") == 0) {
        s = append(s, ":root { --c: #00ff00; } :root[data-theme=dark] { --c: #ff0000; }\n"
                      ":root:not([data-theme=\"dark\"]) { --d: #0000ff; }\n"
                      "h1 { color: var(--c); }\n");
    } else if (strcmp(name, "sizes.css") == 0) {
        s = append(s, "body { font-size: 0.5rem; } html { font-size: 20px; }\n"
                      "h1 { font-size: 1em; padding: 1rem; }\n");
    } else if (strcmp(name, "twice.css") == 0) {
        s = append(s, "body { font-size: 2em; } body { font-size: 2em; }\n"
                      ":root { font-size: 30px !important; } html { font-size: 10px; }\n"
                      "h1 { font-size: 1em; padding: 1rem; }\n");
    } else if (strcmp(name, "values.css") == 0) {
        s = append(s, "p { padding: medium thin; }\n"
                      "q { background: url(x) center / cover #ff0000; }\n"
                      "u { border-top-left-radius: 4px 8px; }\n"
                      "a { color: #ff0000; } a { color: var(--missing); }\n"
                      "s { color: color-mix(in oklch, red, blue); }\n");
    } else if (strcmp(name, "star.css") == 0) {
        s = append(s, "* { line-height: 1.5; } h1 { font-size: 30px; }\n");
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

static reaktor_sheet *g;
static long           g_seen_n;

static void
seen(const char *path)
{
    (void)path;
    g_seen_n++;
}

static reaktor_style
get(const char *selector)
{
    reaktor_style s;

    reaktor_sheet_style(g, selector, &s);
    return s;
}

static long
rgb(const unsigned char c[4])
{
    return (long)c[0] << 16 | (long)c[1] << 8 | c[2];
}

static long
fg(const char *selector)
{
    reaktor_style s = get(selector);

    return s.matched ? rgb(s.fg) : -1;
}

static reaktor_sheet *
load1(const char *name, const char *theme)
{
    return reaktor_sheet_load(&name, 1, theme);
}

int
main(void)
{
    reaktor_style s;
    unsigned char c[4];
    int           i;

    g = load1("test.css", "light");
    ok("a sheet loads", g != NULL);

    check("two :not() in one compound apply", fg("select"), 0xff0000);
    ok("  but not to what they exclude", fg("select.type-a") != 0xff0000 && fg("select.type-b") != 0xff0000);
    check("a class named twice matches once", fg("a.x"), 0x00ff00);
    check("a pseudo-class named twice matches once", fg("a:hover"), 0x00ff00);
    check("each selector of a list applies", fg("button.x:disabled:hover"), 0x0000ff);
    check("equal specificity: the later rule wins", fg("button.x:hover"), 0x123456);
    check("an attribute value reads as a class", fg("input.type-range"), 0x0000ff);
    check("a bare attribute reads as a class", fg("button.disabled"), 0x654321);
    check(":enabled holds unless :disabled is asked", fg("button:hover"), 0x123456);
    ok("  and :disabled skips it", fg("button:disabled:hover") != 0x123456);

    s = get("p");
    check("calc() over pixels", (long)s.pad[REAKTOR_SIDE_TOP], 12);
    check("  with a division", (long)s.pad[REAKTOR_SIDE_RIGHT], 4);
    check("  and brackets", (long)s.margin[REAKTOR_SIDE_LEFT], 8);
    check("calc() with a percentage is dropped", (long)s.width, 0);

    check("a ';' inside url() stays inside it", fg("q"), 0x010203);
    check("a long value is read", fg("q.v"), 0x040506);
    check("a long selector is read", fg(".a-selector-longer-than-any-token-the-old-engine-held"), 0x070809);
    check("  and the rest of its list", fg("u"), 0x070809);

    s = get("b");
    check("border keyword widths", (long)s.border_w[REAKTOR_SIDE_TOP], 5);
    check("  thin", (long)s.border_w[REAKTOR_SIDE_RIGHT], 1);
    check("  a side set without a style has none", (long)s.border_w[REAKTOR_SIDE_LEFT], 0);
    check("  the shorthand's color", rgb(s.border_col), 0x010101);
    ok("an invalid border-style is dropped", !get("i").matched);
    s = get("s");
    ok("border: unset is no border", s.matched && s.border == 0.0f);
    check("border: inherit keeps the rest of its rule", fg("u.x"), 0x0a0b0c);
    s = get("a.y.z");
    check("!important beats a more specific rule", (long)s.border, 2);
    check("  with its color", rgb(s.border_col), 0x0d0e0f);

    s = get("em");
    check("color-mix() with the space form and a weight", rgb(s.fg), 0xff0000);
    check("  and its alpha", s.fg[3], 191);
    check("color-mix() with hsl() and names", rgb(s.bg), 0x008000);
    check("color-mix() with a hex alpha", s.border_col[0], 85);

    check("a declaration of unknown text keeps its rule", fg("dl"), 0x111111);
    check("  '//' in a value", fg("dt"), 0x121212);
    check("  a block in a value", fg("dd"), 0x131313);
    check("  var() of a '/'", fg("dfn"), 0x141414);
    check("an operator attribute drops only its selector", fg("kbd"), 0x151515);
    ok("  which matches nothing", fg("a") != 0x151515);
    ok("background: url() is transparent", get("dl").bg[3] == 0);

    check("!important wins over later rules", fg("ol"), 0x200003);
    check("  and over more specific ones", fg("ol.k"), 0x200003);
    check("the later declaration wins in a rule", (long)get("ol").pad[REAKTOR_SIDE_TOP], 2);
    check("a hex escape in a name", fg("#foo"), 0x252525);
    check("an escaped character in a name", fg("a.a\\:b"), 0x252525);
    check("an escape in an attribute value", fg("p.data-x-12"), 0x252525);
    check("a descendant selector", fg("nav .item"), 0x300000);
    check("  through another level", fg("nav ul .item"), 0x300000);
    ok("  not without its ancestor", !get(".item").matched);
    check("a child selector", (long)get("nav .item").margin[0], 4);
    check("  not through another level", (long)get("nav ul .item").margin[0], 0);

    s = get("h1");
    check("em is the body's size", s.font_px, 32);
    check("rem is the root's size", (long)s.pad[0], 20);
    check("weight 600 is bold", s.bold, 1);
    ok("opacity takes a percentage", s.opacity == 0.5f);
    s = get("h2");
    check("a percentage font size", s.font_px, 24);
    check("a line height in pixels", (long)s.line_height, 24);
    check("a border with no color takes the text's", rgb(s.border_col), 0x405060);
    s = get("h3");
    check("background takes a var() color", rgb(s.bg), 0x0a1b2c);
    check("currentcolor", rgb(s.border_col), 0x010101);
    ok("a token reads as a color", reaktor_sheet_color(g, "--accent", c) && rgb(c) == 0x0a1b2c);
    ok("  through var() and color-mix()", reaktor_sheet_color(g, "--mixed", c) && rgb(c) == 0x0a1b2c);
    ok("  and not when it is no color", !reaktor_sheet_color(g, "--slash", c));
    ok("a selector that does not parse matches nothing", !get("a + b").matched && !get("").matched);
    reaktor_sheet_free(g);

    g = load1("media.css", "light");
    ok("with no preference, only no-preference queries apply",
       fg("h3") == 3 && fg("h1") < 0 && fg("h2") < 0 && fg("h4") < 0 && fg("h5") < 0 && fg("h6") < 0);
    reaktor_sheet_free(g);
    reaktor_css_media(1, 1);
    g = load1("media.css", "dark");
    ok("reduced motion and more contrast apply theirs",
       fg("h1") == 1 && fg("h2") == 2 && fg("h3") < 0 && fg("h4") == 4 && fg("h5") == 5 && fg("h6") == 6);
    ok("  but not with a feature not answered here, nor print", fg("ol") < 0 && fg("ul") < 0);
    reaktor_sheet_free(g);
    reaktor_css_media(0, 0);

    for (i = 0; i < 2; i++) {
        static const char *const simple[1] = { "external/simplecss/simple.css" };

        g = reaktor_sheet_load(simple, 1, i ? "dark" : "light");
        ok(i ? "Simple.css loads, dark" : "Simple.css loads, light", g && get("button").matched);
        reaktor_sheet_free(g);
    }

    g = load1("star.css", "light");
    check("* applies to everything", (long)(get("h1").line_height + 0.5f), 45);
    ok("  and matches any selector", get("anything .at all").matched);
    reaktor_sheet_free(g);
    g = load1("calc.css", "light");
    ok("calc() nested past its depth is dropped, the rest of its rule kept",
       get("dir").width == 0.0f && fg("dir") == 0x161616);
    reaktor_sheet_free(g);
    g = load1("self.css", "light");
    check("a sheet that imports itself loads", fg("kbd"), 0x171717);
    reaktor_sheet_free(g);
    g = load1("fan0.css", "light");
    check("imports that fan out stop at the budget", fg("samp"), 0x181818);
    reaktor_sheet_free(g);
    g = load1("nest.css", "light");
    s = get("var");
    ok("var() fallbacks nested past the limit are dropped", s.fg[0] == 0 && rgb(s.bg) == 0x191919);
    reaktor_sheet_free(g);
    g = load1("laughs.css", "light");
    s = get("abbr");
    ok("var() that would grow past the limit is dropped", rgb(s.fg) == 0 && rgb(s.bg) == 0x1a1a1a);
    reaktor_sheet_free(g);

    reaktor_css_track(seen);
    {
        static const char *const pair[2] = { "open.css", "after.css" };

        g = reaktor_sheet_load(pair, 2, "light");
        check("a sheet left open is closed at its end", fg("p"), 0x222222);
        check("  and the next sheet still reads", fg("kbd"), 0x232323);
        reaktor_sheet_free(g);
    }
    g_seen_n = 0;
    g = load1("late.css", "light");
    check("@import is read only at the top of a sheet", g_seen_n, 1);
    check("  and not inside a string", fg("h1"), 0x242424);
    reaktor_sheet_free(g);
    g_seen_n = 0;
    g = load1("fan0.css", "light");
    ok("imports that fan out stop at a count of loads", g_seen_n > 0 && g_seen_n <= 258);
    reaktor_sheet_free(g);
    reaktor_css_track(NULL);

    g = load1("vars.css", "light");
    check("many custom properties load", fg("h1"), 19999);
    reaktor_sheet_free(g);
    g = load1("empty.css", "light");
    s = get("abbr");
    ok("var() that would expand too often is dropped", rgb(s.fg) == 0 && rgb(s.bg) == 0x1b1b1b);
    reaktor_sheet_free(g);

    g = load1("themes.css", "light");
    check("the theme's own variables apply", fg("h1"), 0x00ff00);
    ok("  and :not() of the other theme", reaktor_sheet_color(g, "--d", c) && rgb(c) == 0x0000ff);
    reaktor_sheet_free(g);
    g = load1("themes.css", "dark");
    check("  the other theme's", fg("h1"), 0xff0000);
    ok("  without the excluded one", !reaktor_sheet_color(g, "--d", c));
    reaktor_sheet_free(g);

    g = load1("sizes.css", "light");
    check("em follows rem whatever the order", get("h1").font_px, 10);
    check("rem is the root's", (long)get("h1").pad[0], 20);
    reaktor_sheet_free(g);
    g = load1("twice.css", "light");
    check("a size repeated does not compound", get("h1").font_px, 60);
    check(":root counts, and !important wins", (long)get("h1").pad[0], 30);
    reaktor_sheet_free(g);

    g = load1("values.css", "light");
    ok("thin and medium are border widths only", !get("p").matched);
    check("background past a '/'", rgb(get("q").bg), 0xff0000);
    check("a corner radius with two values", (long)get("u").radius[0], 4);
    check("an undefined var() unsets, not skips", fg("a"), 0);
    ok("color-mix() outside srgb is dropped", !get("s").matched);
    reaktor_sheet_free(g);

    return test_done("css");
}
