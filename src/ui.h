/* ui.h - the seam between the app shell and the pages it draws.
 *
 * main.c owns the window, the renderer, the font atlas, the SVG cache and the
 * CSS -> nk_style translation. A page needs none of those directly, only the
 * handful of calls below, so App stays an opaque pointer and a page stays
 * about layout. */
#ifndef CURIE_UI_H
#define CURIE_UI_H

#include "nk_common.h"

typedef struct App App;

/* Tab 0 is the login screen this app began as. The rest exist to put every
 * Nuklear widget on screen under the same stylesheet, which is the only
 * honest way to find out how far the CSS seam actually reaches: a control
 * that tiny.css has no rule for has to be styled from its palette tokens
 * instead, and doing that for all of them is what shows where the line is. */
enum {
    TAB_LOGIN = 0,
    TAB_BUTTONS,
    TAB_INPUTS,
    TAB_DISPLAY,
    TAB_LAYOUT,
    TAB_POPUPS,
    TAB_DIAG,
    TAB_COUNT
};
extern const char *const curie_tab_names[TAB_COUNT];

#define SC_TEXT_CAP  64
#define SC_BOX_CAP   512
#define SC_SERIES_N  32
#define SC_LIST_N    64

/* Everything the showcase remembers between frames. Immediate mode keeps no
 * widget state of its own for anything that holds a value, so it lives here -
 * one struct rather than scattered statics, so a page is a pure function of
 * it. */
typedef struct showcase_state {
    /* buttons and toggles */
    int          repeats, presses;
    nk_bool      check_wrap, check_spell;
    unsigned int flags;
    int          radio;
    nk_bool      sel_tile[4];
    nk_bool      sel_row;
    nk_bool      toggle;

    /* inputs */
    char   name[SC_TEXT_CAP];   int name_len;
    char   digits[SC_TEXT_CAP]; int digits_len;
    char   hex[SC_TEXT_CAP];    int hex_len;
    char   note[SC_BOX_CAP];    int note_len;
    float  slider_f;
    int    slider_i;
    float  knob;
    int    prop_i;
    float  prop_f;
    double prop_d;
    int    combo_size, combo_symbol;
    struct nk_colorf tint;
    nk_size          progress;

    /* display */
    float series[SC_SERIES_N];
    int   list_sel;
    nk_bool tree_leaf[3];

    /* popups */
    int  popup_open;
    char menu_pick[40];

    int seeded;
} showcase_state;

/* --- what a page may ask of the shell ---------------------------------- */

struct nk_color            curie_col(const unsigned char rgba[4]);
/* A palette token as a colour, falling back to `def` when the stylesheet has
 * no such custom property. */
struct nk_color            curie_token(const char *name, struct nk_color def);
/* A colour, unless it is indistinguishable from `behind` - then `fallback`.
 * tiny.css's dark palette gives thead and details the page's own background,
 * which makes them invisible. */
struct nk_color            curie_visible(struct nk_color want,
                                         struct nk_color behind,
                                         struct nk_color fallback);
const struct nk_user_font *curie_font(App *app, int px, int bold);

/* An SVG from the repo, and the common case: an Ionicon stroked in the
 * theme's muted text colour, so a page never spells out a path. */
struct nk_image curie_glyph(App *app, const char *rel_src, int px);
struct nk_image curie_ionicon(App *app, const char *name, int px);
/* The same, stroked in a colour of the caller's choosing - for an icon that
 * sits on something other than the page, such as a row filled with the
 * accent. curie_on gives the colour that reads on a given background. */
struct nk_image curie_ionicon_col(App *app, const char *name, int px,
                                  struct nk_color stroke);
struct nk_color curie_on(struct nk_color bg);

/* Records what the pointer is over: the cursor it wants, and whether hovering
 * it changes anything on screen. See the hot-region note in main.c. */
/* Draws an image at exactly px, centred in the current widget slot. nk_image
 * stretches to fill its slot instead, so the size asked for is ignored. */
void curie_image(App *app, struct nk_context *ctx, struct nk_image im, int px);

void curie_hot(App *app, struct nk_rect r, int cursor, int repaint);
/* The same, for something emitted inside a popup: it is drawn over the page,
 * so it outranks whatever it covers however the two were recorded. */
void curie_hot_top(App *app, struct nk_rect r, int cursor, int repaint);
/* The same, but redrawing on every move inside it rather than only on
 * crossing into it - for anything drawn at the pointer. */
void curie_hot_follow(App *app, struct nk_rect r, int cursor);

int curie_button(App *app, struct nk_context *ctx, const char *label);
int curie_button_accent(App *app, struct nk_context *ctx, const char *label);
int curie_button_icon(App *app, struct nk_context *ctx,
                      const char *ionicon, const char *label);
int curie_link(App *app, struct nk_context *ctx, const char *label, int active);

/* A text field styled from tiny.css's `input` rule, with `hint` painted into
 * it while it is empty. Nuklear has no placeholder of its own. */
nk_flags curie_field(App *app, struct nk_context *ctx, nk_flags flags,
                     char *buf, int *len, int cap, const char *hint,
                     nk_plugin_filter filter);

/* The radius for a popup, tooltip or menu. Nuklear has a single rounding for
 * every panel, so it cannot be set globally without rounding the window. */
float curie_popup_rounding(void);

/* What the shell knows about itself, for the page that reports it. Copied
 * out rather than reached for, so App stays opaque. */
typedef struct curie_diag {
    const char *renderer;      /* SDL's name for the backend in use */
    const char *mode;          /* what CURIE_RENDERER asked for */
    const char *frame_rate;    /* SDL_HINT_MAIN_CALLBACK_RATE at rest */
    const char *drag_rate;     /* and while a button is held */
    const char *font;
    int   vsync, aa, dark, sheets, tab;
    float scale, style_ms, fps, build_ms, render_ms, present_ms;
    float frame_gap_ms;        /* since the previous drawn frame */
    /* Where the memory is. nk_bytes is Nuklear's own command buffer, which
     * grows to fit the busiest frame drawn so far and is never given back;
     * icon_bytes is the rasterised SVG cache. */
    unsigned long nk_bytes, nk_used, icon_bytes, rss_bytes;
    int icons;
    int atlas_w, atlas_h;      /* the baked font atlas, RGBA32 */
    /* Resident set at each startup milestone: on entry, after SDL_Init,
     * after the window and renderer, after nk_sdl_init, after the font bake,
     * after the stylesheets. */
    unsigned long rss_at[6];
} curie_diag;

void curie_diagnostics(App *app, curie_diag *out);

showcase_state *curie_showcase(App *app);

/* Drawn by showcase.c, one page per tab above TAB_LOGIN. w and h are the
 * content region of the group it is being drawn into. */
void curie_showcase_page(App *app, struct nk_context *ctx, int tab,
                         float w, float h);

#endif /* CURIE_UI_H */
