/* appicon.h - Ionicons SVG + Open-Color palette -> raster, at runtime.
 *
 * Portable: no windows.h, no HICON. The window icon is applied through
 * SDL_SetWindowIcon, and tools/mkicon.c turns the same surface into a .ico
 * for the Windows executable resource. */
#ifndef CURIE_APPICON_H
#define CURIE_APPICON_H

#include "plutovg.h"

/* Loads third_party/ionicons/src/svg/<name>.svg, recolours it from Open-Color
 * (strokes -> outline_family/idx, fills -> inside_family/idx) and rasterises
 * it at size x size. Caller owns the surface. NULL on failure.
 * Shared by the app and by tools/mkicon.c so both use one code path. */
/* Loads an arbitrary SVG by path relative to the repo root and rasterises it
 * at size x size. Pass NULL for either family to leave those colours alone,
 * which is what a plain <img src="..."> wants. Caller owns the surface. */
plutovg_surface_t *curie_svg_surface_path(const char *rel_path, int size,
                                          const char *outline_family, int outline_idx,
                                          const char *inside_family, int inside_idx);

plutovg_surface_t *curie_svg_surface(const char *name, int size,
                                     const char *outline_family, int outline_idx,
                                     const char *inside_family, int inside_idx);

/* Converts plutovg's premultiplied ARGB32 to straight alpha in place.
 * Both SDL surfaces and Windows icon bitmaps sample straight alpha;
 * premultiplied pixels would render too dark. */
void curie_unpremultiply(unsigned char *px, int w, int h, int stride);

/* Same pipeline, written to a PNG instead. Used by `curie --dump-icon`
 * to eyeball the recolour without launching the UI. Returns 0 on failure. */
int curie_svg_icon_dump(const char *name, int size,
                        const char *outline_family, int outline_idx,
                        const char *inside_family, int inside_idx,
                        const char *png_path);

#endif /* CURIE_APPICON_H */
