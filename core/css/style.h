#ifndef REAKTOR_STYLE_H
#define REAKTOR_STYLE_H

typedef struct reaktor_style {
    unsigned char bg[4];
    unsigned char fg[4];
    unsigned char border_col[4];
    float         border;
    float         rounding;
    float         pad_x, pad_y;
    int           font_px;
    int           bold;
    int           matched;
} reaktor_style;

int  reaktor_style_init(const char *const *css_paths, int count,
                        const char *theme);
void reaktor_style_shutdown(void);

void reaktor_style_get(const char *selector, reaktor_style *out);

void reaktor_style_darken(unsigned char rgba[4], float amount);

int reaktor_style_token(const char *name, unsigned char rgba[4]);

#endif
