#ifndef REAKTOR_CSS_H
#define REAKTOR_CSS_H

#include "reaktor/style.h"

typedef struct reaktor_sheet reaktor_sheet;

reaktor_sheet *reaktor_sheet_load(const char *const *paths, int count, const char *theme);
void reaktor_sheet_free(reaktor_sheet *s);
void reaktor_sheet_style(const reaktor_sheet *s, const char *selector, reaktor_style *out);
int  reaktor_sheet_color(const reaktor_sheet *s, const char *name, unsigned char rgba[4]);

/* What @media (prefers-reduced-motion) and (prefers-contrast) answer. */
void reaktor_css_media(int reduce_motion, int more_contrast);

/* Told each sheet a load reads, @imports too, as the sheet names it. */
void reaktor_css_track(void (*seen)(const char *path));

#endif
