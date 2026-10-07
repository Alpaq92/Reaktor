#ifndef REAKTOR_STYLE_H
#define REAKTOR_STYLE_H

#include "reaktor/style.h"

int  reaktor_style_init(const char *const *css_paths, int count,
                        const char *theme);
void reaktor_style_shutdown(void);
/* Counts loads, so a layout can tell its sizes come from an older sheet. */
unsigned reaktor_style_generation(void);

#endif
