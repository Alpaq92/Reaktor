#ifndef REAKTOR_STYLE_H
#define REAKTOR_STYLE_H

#include "reaktor/style.h"

int  reaktor_style_init(const char *const *css_paths, int count,
                        const char *theme);
int  reaktor_style_add(const char *path);
void reaktor_style_shutdown(void);
unsigned reaktor_style_loads(void);
void reaktor_style_darken(unsigned char rgba[4], float amount);

#endif
