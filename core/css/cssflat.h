#ifndef REAKTOR_CSSFLAT_H
#define REAKTOR_CSSFLAT_H

#include <stddef.h>

typedef struct reaktor_cssvars reaktor_cssvars;

/* A declaration or selector of token_max bytes or more is dropped; 0 keeps all. */
char *reaktor_css_flatten(const char *const *paths, int count, const char *theme,
                          size_t token_max, reaktor_cssvars **out_vars);

/* What @media (prefers-reduced-motion) and (prefers-contrast) answer. */
void reaktor_css_media(int reduce_motion, int more_contrast);

/* Told each sheet a flatten reads, @imports too, as the sheet names it. */
void reaktor_css_track(void (*seen)(const char *path));

int reaktor_cssvars_color(const reaktor_cssvars *vars, const char *name,
                          unsigned char rgba[4]);

void reaktor_cssvars_free(reaktor_cssvars *vars);

#endif
