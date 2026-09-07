/* style.h - stylesheet lookup for Nuklear widgets.
 *
 * Nuklear draws the widgets and lays them out; the look comes from CSS. This
 * is the seam between the two: parse the stylesheets once, then ask for the
 * computed values behind a selector - "button", or the same with
 * ":hover" - and push those into nk_style around the widget call.
 *
 * The engine behind it is LCUI's libcss (parser, selector matching, cascade)
 * and nothing else. libui and pandagl are not built: their layout and
 * rasteriser have no job here.
 *
 * Deliberately free of nuklear.h. Nothing below names a Nuklear type, so the
 * mapping into nk_style lives with the widget calls in main.c and this file
 * stays a plain CSS query. */
#ifndef CURIE_STYLE_H
#define CURIE_STYLE_H

/* The properties this UI actually styles. These are exactly the ones LCUI
 * implements well - colour, border, radius, padding, font - which is the point
 * of the arrangement: layout and typography CSS, where its gaps are, never
 * comes up because Nuklear owns layout. */
typedef struct curie_style {
    unsigned char bg[4];         /* background-color */
    unsigned char fg[4];         /* color */
    unsigned char border_col[4]; /* border-*-color */
    float         border;        /* border-*-width, px */
    float         rounding;      /* border-radius, px */
    float         pad_x, pad_y;  /* padding, px */
    int           font_px;       /* font-size, px */
    int           bold;          /* font-weight >= 700 */
    int           matched;       /* 0 when the selector matched no rule */
} curie_style;

/* Reads and parses `count` stylesheets, in order. Returns 0 on failure. The
 * text goes through cssflat.c first: LCUI's length parser accepts px, %, dp,
 * sp and pt only, and no custom properties. */
int  curie_style_init(const char *const *css_paths, int count);
void curie_style_shutdown(void);

/* Computed values behind a CSS selector. `selector` is what a stylesheet
 * would write - "button", "input:focus" - matched by LCUI against everything
 * loaded. Never fails: an unmatched selector yields zeroed values with
 * `matched` clear, so a caller can fall back to Nuklear's defaults. */
void curie_style_get(const char *selector, curie_style *out);

/* Darkens an already-resolved colour by `amount` (0..1). Kept for stylesheets
 * that express a hover as an overlay rather than a colour; tiny.css states
 * --button-hover outright, so nothing on this screen needs it. */
void curie_style_darken(unsigned char rgba[4], float amount);

/* A resolved custom property, as an RGBA quad. Returns 0 if the stylesheets
 * define no such name or its value is not a colour.
 *
 * This is how the window and card get their colours: tiny.css keeps its whole
 * palette in :root (--background-body, --background, --text-main), and those
 * are the same declarations its own rules are written against. Reading them
 * here means the surfaces Nuklear paints directly come from the stylesheet
 * rather than from constants that would have to be kept in step with it. */
int curie_style_token(const char *name, unsigned char rgba[4]);

#endif /* CURIE_STYLE_H */
