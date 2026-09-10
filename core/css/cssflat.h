#ifndef REAKTOR_CSSFLAT_H
#define REAKTOR_CSSFLAT_H

#include <stddef.h>

typedef struct reaktor_cssvars reaktor_cssvars;

char *reaktor_css_flatten(const char *const *paths, int count,
                          const char *theme, reaktor_cssvars **out_vars);

const char *reaktor_cssvars_get(const reaktor_cssvars *vars, const char *name);

int reaktor_cssvars_color(const reaktor_cssvars *vars, const char *name,
                          unsigned char rgba[4]);

void reaktor_cssvars_free(reaktor_cssvars *vars);

#endif
