#ifndef REAKTOR_STYLE_H
#define REAKTOR_STYLE_H

enum { REAKTOR_SIDE_TOP = 0, REAKTOR_SIDE_RIGHT, REAKTOR_SIDE_BOTTOM, REAKTOR_SIDE_LEFT };
enum { REAKTOR_CORNER_TL = 0, REAKTOR_CORNER_TR, REAKTOR_CORNER_BR, REAKTOR_CORNER_BL };

typedef struct reaktor_style {
    unsigned char bg[4];
    unsigned char fg[4];
    unsigned char border_col[4];

    float         border;
    float         rounding;
    float         pad_x, pad_y;

    float         pad[4];
    float         border_w[4];
    float         radius[4];
    float         margin[4];

    float         width, height;
    float         min_width, min_height;
    float         max_width, max_height;
    float         line_height;

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
