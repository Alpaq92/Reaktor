#ifndef REAKTOR_SVG_H
#define REAKTOR_SVG_H

#include "plutovg.h"

plutovg_surface_t *reaktor_svg_surface_path(const char *rel_path, int size,
                                            const char *outline_color,
                                            const char *inside_color,
                                            float stroke_scale);

void reaktor_unpremultiply(unsigned char *px, int w, int h, int stride);

#endif
