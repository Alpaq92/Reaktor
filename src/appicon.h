/* appicon.h - SVG -> raster, at runtime.
 *
 * Portable: no windows.h, no HICON. The window icon is applied through
 * SDL_SetWindowIcon; the Windows executable resource is a committed .ico that
 * the build links, not something rendered here. */
#ifndef CURIE_APPICON_H
#define CURIE_APPICON_H

#include "plutovg.h"

/* Loads an SVG by path relative to the repo root, substitutes the two
 * colours into it and rasterises it at size x size. Caller owns the surface.
 *
 * outline_colour and inside_colour are "#rrggbb" literals, or NULL to leave
 * that channel as the artwork has it. They are literals because the callers
 * pass whatever the stylesheet's tokens resolved to this frame, which is the
 * only way an icon can follow a theme.
 *
 * stroke_scale multiplies every stroke-width the artwork declares; 0 or 1
 * leaves it alone. Ionicons set a stroke of 6.25% of the glyph, which below
 * about 20px is a hairline that anti-aliases to grey, so a glyph drawn small
 * needs a heavier line to read as solid. */
plutovg_surface_t *curie_svg_surface_path(const char *rel_path, int size,
                                          const char *outline_colour,
                                          const char *inside_colour,
                                          float stroke_scale);

/* The same, for a glyph named inside the Ionicons submodule. */
plutovg_surface_t *curie_svg_surface(const char *name, int size,
                                     const char *outline_colour,
                                     const char *inside_colour);

/* Converts plutovg's premultiplied ARGB32 to straight alpha in place.
 * Both SDL surfaces and Windows icon bitmaps sample straight alpha;
 * premultiplied pixels would render too dark. */
void curie_unpremultiply(unsigned char *px, int w, int h, int stride);

#endif /* CURIE_APPICON_H */
