/* mkicon.c - branding/reaktor-icon.ico, rasterised from
 * branding/reaktor-icon.svg.
 *
 * The .svg is the authored mark; the .ico is derived from it, and this is what
 * derives it. It is not part of the build: Explorer reads the icon from a
 * linked resource, so the file has to exist before the linker runs, and a
 * committed .ico is how that is guaranteed on a machine that has not built the
 * tool. Run it when the mark changes:
 *
 *     cmake --build build --target mkicon
 *     build/mkicon
 *
 * The file this replaced had every entry below 256 at 24 bits. A Windows icon
 * carries per-pixel alpha only at 32, so Explorer fell back to the 1-bit AND
 * mask and drew the mark with a hard stepped edge and no transparency - at
 * 16, 24, 32 and 48 pixels, which are the sizes it actually uses. Only the
 * 256 entry, a PNG, was right, which is why the icon looked correct large and
 * wrong everywhere else.
 *
 * So every entry here is 32-bit BGRA with a zeroed mask, except 256, which
 * stays a PNG because at four bytes a pixel it is a quarter-megabyte on its
 * own. plutovg renders premultiplied; Windows wants straight alpha, which is
 * what reaktor_unpremultiply is for. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reaktor.h"
#include "appicon.h"

#define ICON_SVG "branding/reaktor-icon.svg"
#define ICON_ICO "branding/reaktor-icon.ico"
#define ICON_TMP "branding/reaktor-icon.png.tmp"

/* Every size Explorer, the taskbar and the Alt-Tab switcher ask for. 20 and
 * 40 are the two fractional display scaling adds: at 125% and 250% the shell
 * asks for those rather than scaling 16 and 32, and an entry that is missing
 * is resampled from the next one up. They cost about 2.8 KB between them. */
static const int g_sizes[] = { 16, 20, 24, 32, 40, 48, 64, 128, 256 };
#define SIZE_N ((int)(sizeof(g_sizes) / sizeof(g_sizes[0])))

struct entry {
    int            size;
    int            png;      /* payload is a PNG rather than a BMP */
    unsigned char *data;
    long           len;
};

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
    p[2] = (unsigned char)((v >> 16) & 255u);
    p[3] = (unsigned char)((v >> 24) & 255u);
}

/* A BITMAPINFOHEADER, the pixels bottom-up, and an all-clear AND mask. The
 * height in the header is twice the image's: the format counts both planes,
 * even when the mask says nothing. */
static unsigned char *
bmp32(plutovg_surface_t *surf, int n, long *out_len)
{
    int stride = plutovg_surface_get_stride(surf);
    unsigned char *src = plutovg_surface_get_data(surf);
    int mask_row = ((n + 31) / 32) * 4;
    long len = 40 + (long)n * n * 4 + (long)mask_row * n;
    unsigned char *out = (unsigned char *)calloc((size_t)len, 1);
    int y;

    if (!out) return NULL;
    put32(out + 0,  40u);
    put32(out + 4,  (unsigned)n);
    put32(out + 8,  (unsigned)(2 * n));
    put16(out + 12, 1u);
    put16(out + 14, 32u);
    put32(out + 20, (unsigned)(n * n * 4));

    for (y = 0; y < n; y++)
        memcpy(out + 40 + (long)(n - 1 - y) * n * 4, src + (long)y * stride,
               (size_t)n * 4);
    *out_len = len;
    return out;
}

static unsigned char *
slurp(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long len;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)len);
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) *out_len = len;
    return buf;
}

int
main(void)
{
    struct entry e[SIZE_N];
    char tmp[1024], out_path[1024];
    FILE *f;
    long offset;
    int i, ok = 1;

    memset(e, 0, sizeof(e));
    if (!reaktor_path(tmp, sizeof(tmp), ICON_TMP) ||
        !reaktor_path(out_path, sizeof(out_path), ICON_ICO)) {
        fprintf(stderr, "mkicon: cannot resolve the repo root\n");
        return 1;
    }

    for (i = 0; i < SIZE_N && ok; i++) {
        int n = g_sizes[i];
        plutovg_surface_t *surf =
            reaktor_svg_surface_path(ICON_SVG, n, NULL, NULL, 0.0f);

        if (!surf) {
            fprintf(stderr, "mkicon: %s did not render at %d\n", ICON_SVG, n);
            ok = 0;
            break;
        }
        reaktor_unpremultiply(plutovg_surface_get_data(surf), n, n,
                              plutovg_surface_get_stride(surf));
        e[i].size = n;
        if (n >= 256) {
            /* plutovg writes PNG to a file and nothing else, so it goes out
             * and comes straight back in. */
            e[i].png = 1;
            if (plutovg_surface_write_to_png(surf, tmp))
                e[i].data = slurp(tmp, &e[i].len);
            remove(tmp);
        } else {
            e[i].data = bmp32(surf, n, &e[i].len);
        }
        plutovg_surface_destroy(surf);
        if (!e[i].data) {
            fprintf(stderr, "mkicon: no payload for %d\n", n);
            ok = 0;
        }
    }

    if (ok) {
        f = fopen(out_path, "wb");
        if (!f) {
            fprintf(stderr, "mkicon: cannot write %s\n", out_path);
            ok = 0;
        } else {
            unsigned char hdr[6];
            unsigned char dir[16];

            put16(hdr + 0, 0u);
            put16(hdr + 2, 1u);          /* 1 = icon, 2 = cursor */
            put16(hdr + 4, (unsigned)SIZE_N);
            fwrite(hdr, 1, sizeof(hdr), f);

            offset = 6 + 16L * SIZE_N;
            for (i = 0; i < SIZE_N; i++) {
                memset(dir, 0, sizeof(dir));
                /* 256 is written as zero: the field is one byte. */
                dir[0] = (unsigned char)(e[i].size >= 256 ? 0 : e[i].size);
                dir[1] = dir[0];
                put16(dir + 4, 1u);
                put16(dir + 6, 32u);
                put32(dir + 8, (unsigned)e[i].len);
                put32(dir + 12, (unsigned)offset);
                fwrite(dir, 1, sizeof(dir), f);
                offset += e[i].len;
            }
            for (i = 0; i < SIZE_N; i++)
                fwrite(e[i].data, 1, (size_t)e[i].len, f);
            fclose(f);
            printf("wrote %s\n", out_path);
            for (i = 0; i < SIZE_N; i++)
                printf("  %3dx%-3d %-3s %7ld bytes\n", e[i].size, e[i].size,
                       e[i].png ? "png" : "bmp", e[i].len);
        }
    }

    for (i = 0; i < SIZE_N; i++) free(e[i].data);
    return ok ? 0 : 1;
}
