#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cssflat.h"

static int g_fail;

static const char g_sheet[] =
    "input:not([type=\"a\"]):not([type=\"b\"]) { color: #ff0000; }\n"
    "a.x.x { color: #00ff00; }\n"
    "a:hover:hover { color: #00ff00; }\n"
    "button.x:hover, input[type=\"range\"] { color: #0000ff; }\n"
    "button:enabled:hover { color: #123456; }\n"
    "button[disabled] { color: #654321; }\n"
    "p { padding: calc(1.5 * 8px) calc(8px / 2); top: calc(0px - 8px); "
    "margin: calc(2 * (3px + 1px)); width: calc(100% - 2 * 8px); }\n";

char *
reaktor_asset_load(const char *name, size_t *len)
{
    char *out = NULL;
    long  n = 0;

    if (strcmp(name, "test.css") == 0) {
        n = (long)strlen(g_sheet);
        out = malloc((size_t)n + 1);
        if (out) memcpy(out, g_sheet, (size_t)n + 1);
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

static void
ok(const char *what, int got)
{
    if (!got) g_fail++;
    printf("  %-52s %s\n", what, got ? "ok" : "FAILED");
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
    static const char *const simple[1] = { "external/simplecss/simple.css" };
    char *out;
    int   i;

    out = reaktor_css_flatten(test, 1, "light", NULL);
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
        free(out);
    }

    for (i = 0; i < 2; i++) {
        out = reaktor_css_flatten(simple, 1, i ? "dark" : "light", NULL);
        ok(i ? "simple.css flattens, dark" : "simple.css flattens, light",
           out != NULL && strstr(out, "button") != NULL);
        ok("  and no compound in it holds two :not()", out != NULL && !not_twice(out));
        free(out);
    }

    printf("\n%s\n", g_fail ? "FAILED" : "css: all checks passed");
    return g_fail ? 1 : 0;
}
