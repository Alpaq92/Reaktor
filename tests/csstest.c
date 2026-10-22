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

char *
reaktor_asset_load(const char *name, size_t *len)
{
    char *out = NULL;
    long  n = 0;

    if (strcmp(name, "test.css") == 0 || strcmp(name, "media.css") == 0) {
        const char *s = name[0] == 't' ? g_sheet : g_media;

        n = (long)strlen(s);
        out = malloc((size_t)n + 1);
        if (out) memcpy(out, s, (size_t)n + 1);
    } else {
        FILE *f = fopen(name, "rb");

        if (!f) return NULL;
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

    return test_done("css");
}
