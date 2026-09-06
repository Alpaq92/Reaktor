/* appicon.c - Ionicons SVG + Open-Color palette -> raster, entirely at runtime.
 *
 * Ionicons "-outline" glyphs share one idiom: shapes carry
 * style="fill:none;stroke:#000;..." and any solid shape is a <path> with no
 * fill attribute (so it defaults to black). Recolouring therefore needs three
 * substitutions, applied to the file loaded from the submodule - no path data
 * or colour value is copied into this tree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "curie.h"
#include "appicon.h"
#include "plutosvg.h"

/* Was MAX_PATH; kept local so this file needs no platform header. */
#define CURIE_PATH_MAX 1024

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
void curie_unpremultiply(unsigned char *px, int w, int h, int stride)
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

/* Convenience wrapper: turns an Ionicons glyph name into a path. */
plutovg_surface_t *curie_svg_surface(const char *name, int size,
                                     const char *outline_family, int outline_idx,
                                     const char *inside_family, int inside_idx)
{
    char rel[CURIE_PATH_MAX];

    if (snprintf(rel, sizeof(rel),
                 "third_party/ionicons/src/svg/%s.svg", name) < 0)
        return NULL;
    rel[sizeof(rel) - 1] = '\0';
    return curie_svg_surface_path(rel, size, outline_family, outline_idx,
                                  inside_family, inside_idx, 0.0f);
}

/* Loads any SVG under the repo root, optionally recolours it from
 * Open-Color, and rasterises to a surface the caller owns. A NULL family
 * leaves that channel as the artwork has it. */
plutovg_surface_t *curie_svg_surface_path(const char *rel_path, int size,
                                          const char *outline_family, int outline_idx,
                                          const char *inside_family, int inside_idx,
                                          float stroke_scale)
{
    char path[CURIE_PATH_MAX];
    char outline_hex[16] = "#000000", inside_hex[16] = "none";
    char fill_decl[32], stroke_decl[32], path_open[48];
    unsigned char r, g, b;
    char *svg;
    const char *src;
    char *stage0 = NULL, *stage1 = NULL, *stage2 = NULL, *stage3 = NULL;
    size_t cap;
    plutosvg_document_t *doc = NULL;
    plutovg_surface_t *surf = NULL;

    if (!curie_path(path, sizeof(path), rel_path)) {
        fprintf(stderr, "svg: curie_path failed for %s\n", rel_path);
        return NULL;
    }

    svg = curie_read_file(path, NULL);
    if (!svg) { fprintf(stderr, "svg: cannot read %s\n", path); return NULL; }

    /* A channel is either an Open-Color name, resolved from open-color.json,
     * or a literal "#rrggbb". The literal form is what lets an icon follow the
     * active stylesheet: a fixed palette shade cannot, and the icons were left
     * near-black against a dark card because of it. Either way nothing is
     * baked in here. */
    if (outline_family) {
        if (outline_family[0] == '#') {
            snprintf(outline_hex, sizeof(outline_hex), "%s", outline_family);
        } else if (curie_oc_color(outline_family, outline_idx, &r, &g, &b)) {
            snprintf(outline_hex, sizeof(outline_hex), "#%02x%02x%02x", r, g, b);
        } else {
            fprintf(stderr, "svg: no colour %s-%d\n", outline_family, outline_idx);
            goto done;
        }
    }
    if (inside_family) {
        if (inside_family[0] == '#') {
            snprintf(inside_hex, sizeof(inside_hex), "%s", inside_family);
        } else if (curie_oc_color(inside_family, inside_idx, &r, &g, &b)) {
            snprintf(inside_hex, sizeof(inside_hex), "#%02x%02x%02x", r, g, b);
        } else {
            fprintf(stderr, "svg: no colour %s-%d\n", inside_family, inside_idx);
            goto done;
        }
    }

    snprintf(fill_decl,   sizeof(fill_decl),   "fill:%s", inside_hex);
    snprintf(stroke_decl, sizeof(stroke_decl), "stroke:%s", outline_hex);
    snprintf(path_open,   sizeof(path_open),   "<path fill=\"%s\" ",
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
    if (inside_family) {
        if (!str_replace_all(src, stage1, cap, "fill:none", fill_decl))
            goto done;
        src = stage1;
    }
    if (outline_family) {
        if (!str_replace_all(src, stage2, cap, "stroke:#000", stroke_decl))
            goto done;
        if (!str_replace_all(stage2, stage3, cap, "<path ", path_open)) {
            fprintf(stderr, "svg: replace overflow\n");
            goto done;
        }
        src = stage3;
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
    curie_free(svg);
    return surf;
}

int curie_svg_icon_dump(const char *name, int size,
                        const char *outline_family, int outline_idx,
                        const char *inside_family, int inside_idx,
                        const char *png_path)
{
    plutovg_surface_t *surf;
    int ok;

    surf = curie_svg_surface(name, size, outline_family, outline_idx,
                             inside_family, inside_idx);
    if (!surf) return 0;
    ok = plutovg_surface_write_to_png(surf, png_path) ? 1 : 0;
    plutovg_surface_destroy(surf);
    return ok;
}
