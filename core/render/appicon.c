/* appicon.c - an SVG loaded, optionally recoloured, and rasterised, at runtime.
 *
 * Three callers, and only one of them is Ionicons. The app's own mark is
 * branding/reaktor-icon.svg: draw.c rasterises it for the window icon and
 * tools/mkicon.c derives the .ico the executable carries from the same file.
 * Both pass no colours and get the artwork as it was authored. The third
 * caller is the widget icon cache, which asks for Ionicons glyphs and does
 * recolour them, once per theme.
 *
 * That recolouring is the only reason this is not four lines of plutosvg.
 * Most Ionicons "-outline" glyphs carry style="fill:none;stroke:#000;...",
 * but not all of them: 35 of the 421 spell the same thing as the attributes
 * fill="none" stroke="#000", and 82 draw their solid parts with <circle> or
 * <rect> rather than <path>. calendar-outline does both, so nothing in it was
 * substituted and it rendered black on a dark background.
 *
 * So the fill is set on the <svg> element rather than on the shapes. fill is
 * an inherited property: every shape that does not name one of its own picks
 * it up, whichever element it is, and every shape that says fill:none or
 * fill="none" still overrides it. That is one rule instead of one per tag,
 * and it is what the per-<path> substitution was approximating.
 *
 * Applied to the file loaded from the submodule - no path data or colour
 * value is copied into this tree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reaktor.h"
#include "appicon.h"
#include "plutosvg.h"

/* Was MAX_PATH; kept local so this file needs no platform header. */
#define REAKTOR_PATH_MAX 1024

/* Appends src to dst, respecting cap. Returns 0 if it would overflow. */
static int str_append(char *dst, size_t cap, size_t *len, const char *src,
                      size_t n)
{
    if (*len + n + 1 > cap) return 0;
    memcpy(dst + *len, src, n);
    *len += n;
    dst[*len] = '\0';
    return 1;
}

/* Replaces every occurrence of find with repl. Returns 0 on overflow. */
static int str_replace_all(const char *in, char *out, size_t cap,
                           const char *find, const char *repl)
{
    size_t len = 0;
    size_t flen = strlen(find);
    if (flen == 0) return 0;   /* empty needle would never advance */
    size_t rlen = strlen(repl);
    const char *p = in;

    out[0] = '\0';
    for (;;) {
        const char *hit = strstr(p, find);
        if (!hit) break;
        if (!str_append(out, cap, &len, p, (size_t)(hit - p))) return 0;
        if (!str_append(out, cap, &len, repl, rlen)) return 0;
        p = hit + flen;
    }
    return str_append(out, cap, &len, p, strlen(p));
}

/* Multiplies every stroke-width the artwork declares by k. A multiplier, not
 * a width: an Ionicons glyph happens to carry a 32-unit stroke on a 512-unit
 * viewBox, but nothing here should know that, and scaling keeps the number
 * where it belongs - in the submodule. Returns 0 on overflow. */
static int str_scale_stroke(const char *in, char *out, size_t cap, float k)
{
    static const char key[] = "stroke-width:";
    const size_t klen = sizeof(key) - 1;
    size_t len = 0;
    const char *p = in;

    out[0] = '\0';
    for (;;) {
        const char *hit = strstr(p, key);
        const char *num;
        char *end;
        char repl[48];
        double w;

        if (!hit) break;
        if (!str_append(out, cap, &len, p, (size_t)(hit - p))) return 0;
        if (!str_append(out, cap, &len, key, klen)) return 0;
        num = hit + klen;
        w = strtod(num, &end);
        if (end == num) { p = num; continue; }   /* not a number after all */
        /* Rounded to whole viewBox units: the result is integral for every
         * width Ionicons uses, and printing no fraction sidesteps a locale
         * that would spell the decimal point with a comma. Any unit suffix
         * ("px") is left where it stands. */
        snprintf(repl, sizeof(repl), "%ld", (long)(w * (double)k + 0.5));
        if (!str_append(out, cap, &len, repl, strlen(repl))) return 0;
        p = end;
    }
    return str_append(out, cap, &len, p, strlen(p));
}

/* Un-premultiplies plutovg's ARGB32 output. Windows icon bitmaps are sampled
 * as straight alpha, so premultiplied pixels would render too dark. */
void reaktor_unpremultiply(unsigned char *px, int w, int h, int stride)
{
    int x, y;
    for (y = 0; y < h; y++) {
        unsigned char *row = px + (size_t)y * stride;
        for (x = 0; x < w; x++) {
            unsigned char *p = row + x * 4;   /* B, G, R, A */
            unsigned a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
            } else if (a < 255) {
                p[0] = (unsigned char)((p[0] * 255 + a / 2) / a);
                p[1] = (unsigned char)((p[1] * 255 + a / 2) / a);
                p[2] = (unsigned char)((p[2] * 255 + a / 2) / a);
            }
        }
    }
}

/* Loads any SVG under the repo root, optionally recolours it from
 * Open-Color, and rasterises to a surface the caller owns. A NULL family
 * leaves that channel as the artwork has it. */
plutovg_surface_t *reaktor_svg_surface_path(const char *rel_path, int size,
                                            const char *outline_colour,
                                            const char *inside_colour,
                                            float stroke_scale)
{
    char path[REAKTOR_PATH_MAX];
    char outline_hex[16] = "#000000", inside_hex[16] = "none";
    char fill_decl[32], stroke_decl[32], stroke_attr[32], svg_open[48];
    char *svg;
    const char *src;
    char *stage0 = NULL, *stage1 = NULL, *stage2 = NULL, *stage3 = NULL;
    size_t cap;
    plutosvg_document_t *doc = NULL;
    plutovg_surface_t *surf = NULL;

    if (!reaktor_path(path, sizeof(path), rel_path)) {
        fprintf(stderr, "svg: reaktor_path failed for %s\n", rel_path);
        return NULL;
    }

    svg = reaktor_read_file(path, NULL);
    if (!svg) { fprintf(stderr, "svg: cannot read %s\n", path); return NULL; }

    /* A channel is a literal "#rrggbb", which is what lets an icon follow the
     * active stylesheet - the caller passes whatever --links or --text-muted
     * resolved to this frame. This used to accept an Open-Color family and
     * shade as well, and that was exactly the problem: a fixed palette shade
     * cannot track a theme, and it left the card's icons near-black against a
     * dark background. */
    if (outline_colour) {
        snprintf(outline_hex, sizeof(outline_hex), "%s", outline_colour);
    }
    if (inside_colour) {
        snprintf(inside_hex, sizeof(inside_hex), "%s", inside_colour);
    }

    snprintf(fill_decl,   sizeof(fill_decl),   "fill:%s", inside_hex);
    snprintf(stroke_decl, sizeof(stroke_decl), "stroke:%s", outline_hex);
    snprintf(stroke_attr, sizeof(stroke_attr), "stroke=\"%s\"", outline_hex);
    snprintf(svg_open,    sizeof(svg_open),    "<svg fill=\"%s\" ",
              outline_hex);

    cap = strlen(svg) * 4 + 1024;
    stage0 = (char *)malloc(cap);
    stage1 = (char *)malloc(cap);
    stage2 = (char *)malloc(cap);
    stage3 = (char *)malloc(cap);
    if (!stage0 || !stage1 || !stage2 || !stage3) { fprintf(stderr, "svg: alloc failed\n"); goto done; }

    /* Each requested channel is one substitution pass; an unrequested one is
     * skipped outright rather than matched against a sentinel. */
    src = svg;
    if (stroke_scale > 0.0f && stroke_scale != 1.0f) {
        if (!str_scale_stroke(src, stage0, cap, stroke_scale)) goto done;
        src = stage0;
    }
    if (inside_colour) {
        if (!str_replace_all(src, stage1, cap, "fill:none", fill_decl))
            goto done;
        src = stage1;
    }
    if (outline_colour) {
        /* Both spellings of the same thing, because the set uses both. */
        if (!str_replace_all(src, stage2, cap, "stroke:#000", stroke_decl))
            goto done;
        if (!str_replace_all(stage2, stage3, cap, "stroke=\"#000\"",
                             stroke_attr))
            goto done;
        /* And one inherited fill for every shape that names none. */
        if (!str_replace_all(stage3, stage2, cap, "<svg ", svg_open)) {
            fprintf(stderr, "svg: replace overflow\n");
            goto done;
        }
        src = stage2;
    }

    doc = plutosvg_document_load_from_data(src, (int)strlen(src),
                                           -1.0f, -1.0f, NULL, NULL);
    if (!doc) { fprintf(stderr, "svg: plutosvg parse failed\n"); goto done; }

    surf = plutosvg_document_render_to_surface(doc, NULL, size, size,
                                               NULL, NULL, NULL);
    if (!surf) fprintf(stderr, "svg: render_to_surface failed at %dpx\n", size);

done:
    /* The surface is self-contained, so the document and the staged SVG text
     * can be released here. */
    if (doc) plutosvg_document_destroy(doc);
    free(stage3);
    free(stage2);
    free(stage1);
    free(stage0);
    reaktor_free(svg);
    return surf;
}
