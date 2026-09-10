#ifndef REAKTOR_APPICON_H
#define REAKTOR_APPICON_H

#include "plutovg.h"

plutovg_surface_t *reaktor_svg_surface_path(const char *rel_path, int size,
                                            const char *outline_colour,
                                            const char *inside_colour,
                                            float stroke_scale);

void reaktor_unpremultiply(unsigned char *px, int w, int h, int stride);

#endif
