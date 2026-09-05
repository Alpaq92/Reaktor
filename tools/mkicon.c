/* mkicon.c - build-time generator for the executable's Explorer icon.
 *
 * The window icon can be built at runtime (see src/appicon.c), but the icon
 * Explorer shows for curie.exe must be a linked Win32 resource, which means it
 * has to exist before the linker runs. This host tool renders the same
 * Ionicons glyph with the same Open-Color palette and emits a multi-resolution
 * .ico plus the .rc that references it.
 *
 * Both inputs are read from third_party/ at generation time, so upstream stays
 * the single source of truth - nothing is hand-transcribed, and bumping a
 * submodule changes the icon on the next build.
 *
 * Usage: mkicon <out.ico> <out.rc> <svg-name>
 *               <outline_family> <outline_idx> <inside_family> <inside_idx>
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "curie.h"
#include "appicon.h"

/* Explorer, the task bar, Alt-Tab and the file dialogs all pick different
 * resolutions; shipping the whole ladder avoids blurry downscales. */
static const int SIZES[] = { 16, 24, 32, 48, 64, 128, 256 };
#define NSIZES ((int)(sizeof(SIZES) / sizeof(SIZES[0])))

#pragma pack(push, 1)
typedef struct {
    unsigned short reserved;    /* 0 */
    unsigned short type;        /* 1 = icon */
    unsigned short count;
} ICONDIR;

typedef struct {
    unsigned char  width;       /* 0 means 256 */
    unsigned char  height;
    unsigned char  colors;      /* 0 for true colour */
    unsigned char  reserved;
    unsigned short planes;
    unsigned short bpp;
    unsigned int   bytes_in_res;
    unsigned int   image_offset;
} ICONDIRENTRY;
#pragma pack(pop)

/* AND-mask rows are 1bpp padded to a 4-byte boundary. */
static int mask_stride(int w) { return ((w + 31) / 32) * 4; }

static int write_dib(FILE *f, const plutovg_surface_t *surf)
{
    BITMAPINFOHEADER bih;
    const unsigned char *px = plutovg_surface_get_data(surf);
    int w = plutovg_surface_get_width(surf);
    int h = plutovg_surface_get_height(surf);
    int stride = plutovg_surface_get_stride(surf);
    int ms = mask_stride(w);
    unsigned char *zero;
    int y;

    memset(&bih, 0, sizeof(bih));
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = w;
    bih.biHeight      = h * 2;      /* XOR image + AND mask, per the format */
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage   = (DWORD)(w * h * 4 + h * ms);

    if (fwrite(&bih, sizeof(bih), 1, f) != 1) return 0;

    /* DIB rows run bottom-up. */
    for (y = h - 1; y >= 0; y--)
        if (fwrite(px + (size_t)y * stride, 1, (size_t)w * 4, f) != (size_t)w * 4)
            return 0;

    /* 32bpp icons are keyed off the alpha channel, but the mask must be
     * present and correctly sized; all-zero means "opaque everywhere". */
    zero = (unsigned char *)calloc(1, (size_t)ms);
    if (!zero) return 0;
    for (y = 0; y < h; y++) {
        if (fwrite(zero, 1, (size_t)ms, f) != (size_t)ms) {
            free(zero);
            return 0;
        }
    }
    free(zero);
    return 1;
}

int main(int argc, char **argv)
{
    const char *ico_path, *rc_path, *svg_name;
    const char *outline_family, *inside_family;
    int outline_idx, inside_idx;
    plutovg_surface_t *surf[NSIZES];
    ICONDIR dir;
    ICONDIRENTRY ent[NSIZES];
    FILE *f;
    unsigned int offset;
    int i, n = 0;

    if (argc < 8) {
        fprintf(stderr, "usage: %s <out.ico> <out.rc> <svg-name>"
                        " <outline_family> <outline_idx>"
                        " <inside_family> <inside_idx>\n", argv[0]);
        return 2;
    }
    ico_path       = argv[1];
    rc_path        = argv[2];
    svg_name       = argv[3];
    outline_family = argv[4];
    outline_idx    = atoi(argv[5]);
    inside_family  = argv[6];
    inside_idx     = atoi(argv[7]);

    for (i = 0; i < NSIZES; i++) {
        surf[n] = curie_svg_surface(svg_name, SIZES[i],
                                    outline_family, outline_idx,
                                    inside_family, inside_idx);
        if (!surf[n]) {
            fprintf(stderr, "mkicon: failed to render %s at %dpx\n",
                    svg_name, SIZES[i]);
            return 1;
        }
        curie_unpremultiply(plutovg_surface_get_data(surf[n]),
                            plutovg_surface_get_width(surf[n]),
                            plutovg_surface_get_height(surf[n]),
                            plutovg_surface_get_stride(surf[n]));
        n++;
    }

    f = fopen(ico_path, "wb");
    if (!f) {
        fprintf(stderr, "mkicon: cannot write %s\n", ico_path);
        return 1;
    }

    memset(&dir, 0, sizeof(dir));
    dir.type  = 1;
    dir.count = (unsigned short)n;

    offset = (unsigned int)(sizeof(ICONDIR) + sizeof(ICONDIRENTRY) * n);
    for (i = 0; i < n; i++) {
        int w = SIZES[i], h = SIZES[i];
        unsigned int bytes = (unsigned int)(sizeof(BITMAPINFOHEADER) +
                             w * h * 4 + h * mask_stride(w));
        memset(&ent[i], 0, sizeof(ent[i]));
        ent[i].width        = (unsigned char)(w >= 256 ? 0 : w);
        ent[i].height       = (unsigned char)(h >= 256 ? 0 : h);
        ent[i].planes       = 1;
        ent[i].bpp          = 32;
        ent[i].bytes_in_res = bytes;
        ent[i].image_offset = offset;
        offset += bytes;
    }

    if (fwrite(&dir, sizeof(dir), 1, f) != 1) goto fail;
    if (fwrite(ent, sizeof(ent[0]), (size_t)n, f) != (size_t)n) goto fail;
    for (i = 0; i < n; i++)
        if (!write_dib(f, surf[i])) goto fail;
    fclose(f);

    /* Resource id 1: the lowest-numbered ICON is what Explorer displays. */
    f = fopen(rc_path, "wb");
    if (!f) {
        fprintf(stderr, "mkicon: cannot write %s\n", rc_path);
        return 1;
    }
    fprintf(f, "/* generated by tools/mkicon.c - do not edit */\n");
    fprintf(f, "1 ICON \"curie.ico\"\n");
    fclose(f);

    for (i = 0; i < n; i++) plutovg_surface_destroy(surf[i]);
    printf("mkicon: wrote %s (%d sizes) and %s\n", ico_path, n, rc_path);
    return 0;

fail:
    fprintf(stderr, "mkicon: write failed\n");
    fclose(f);
    return 1;
}
