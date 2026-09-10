#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reaktor.h"
#include "appicon.h"
#include "plutosvg.h"

#define REAKTOR_PATH_MAX 1024

static int str_append(char *dst, size_t cap, size_t *len, const char *src,
                      size_t n)
{
    if (*len + n + 1 > cap) return 0;
    memcpy(dst + *len, src, n);
    *len += n;
    dst[*len] = '\0';
    return 1;
}

static int str_replace_all(const char *in, char *out, size_t cap,
                           const char *find, const char *repl)
{
    size_t len = 0;
    size_t flen = strlen(find);
    if (flen == 0) return 0;
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
        if (end == num) { p = num; continue; }
        snprintf(repl, sizeof(repl), "%ld", (long)(w * (double)k + 0.5));
        if (!str_append(out, cap, &len, repl, strlen(repl))) return 0;
        p = end;
    }
    return str_append(out, cap, &len, p, strlen(p));
}

void reaktor_unpremultiply(unsigned char *px, int w, int h, int stride)
{
    int x, y;
    for (y = 0; y < h; y++) {
        unsigned char *row = px + (size_t)y * stride;
        for (x = 0; x < w; x++) {
            unsigned char *p = row + x * 4;
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
        if (!str_replace_all(src, stage2, cap, "stroke:#000", stroke_decl))
            goto done;
        if (!str_replace_all(stage2, stage3, cap, "stroke=\"#000\"",
                             stroke_attr))
            goto done;
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
    if (doc) plutosvg_document_destroy(doc);
    free(stage3);
    free(stage2);
    free(stage1);
    free(stage0);
    reaktor_free(svg);
    return surf;
}
