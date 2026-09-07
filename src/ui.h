/* ui.h - the seam between the app shell and the pages it draws.
 *
 * main.c owns the window, the renderer, the font atlas, the SVG cache and the
 * CSS -> nk_style translation. A page needs none of those directly, only the
 * handful of calls below, so App stays an opaque pointer and a page stays
 * about layout. */
#ifndef CURIE_UI_H
#define CURIE_UI_H

#include "nk_common.h"
#include "a11y.h"

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

/* Startup milestones, sampled into curie_diag::rss_at so the diagnostics page
 * can report what each step of the startup cost. They live here rather than
 * in the shell because the page indexes the same list: adding a milestone is
 * one edit, and the array below sizes itself from it. */
enum {
    RSS_ENTRY,      /* before SDL_Init - the CRT, the loader, the image */
    RSS_SDL,
    RSS_WINDOW,
    RSS_ICON,
    RSS_NUKLEAR,
    RSS_FONT,
    RSS_STYLE,
    RSS_STEPS
};
extern const char *const curie_rss_names[RSS_STEPS];

#define SC_TEXT_CAP  64
#define SC_BOX_CAP   512
#define SC_SERIES_N  32
#define SC_LIST_N    64
/* Long enough for a Windows extended-length path, which is where the file
 * picker can legitimately land. */
#define SC_PATH_CAP  520

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
    char file_pick[SC_PATH_CAP];   /* what the platform picker last answered */

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

/* --- describing the frame ----------------------------------------------
 * What a screen reader will eventually be handed. A page reports each widget
 * as it draws it; the shell collects the reports into a tree and diffs them.
 * See a11y.h for the roles and states, docs/ACCESSIBILITY.md for the rest.
 *
 * These are the only calls a page needs. Nothing is served to a platform yet -
 * phase 4 - so an unreported widget is not a visible bug today, which is
 * exactly why the widgets go through helpers that report for you. */
void curie_note(App *app, unsigned char role, const char *name,
                const char *value, unsigned state, struct nk_rect bounds);
/* The same, and everything until curie_note_pop is a child of it. */
void curie_note_push(App *app, unsigned char role, const char *name,
                     const char *value, unsigned state, struct nk_rect bounds);
void curie_note_pop(App *app);
/* Convenience for the common case: the widget just drawn, at the bounds the
 * layout gave it, with no value. */
void curie_note_here(App *app, struct nk_context *ctx, unsigned char role,
                     const char *name, unsigned state);

/* The platform's own Open dialog. Answers 0 if one is already up. The choice
 * arrives on SDL's thread, so it is not a return value: call curie_file_taken
 * on a later frame, which answers non-zero once - when there is a fresh
 * result - and writes it into `out`. A cancel and a platform with no picker
 * both come back as text, because a page that shows the answer should show
 * those too. */
int curie_file_open(App *app);
int curie_file_taken(App *app, char *out, int cap);

/* What the shell knows about itself, for the page that reports it. Copied
 * out rather than reached for, so App stays opaque. */
typedef struct curie_diag {
    const char *renderer;      /* SDL's name for the backend in use */
    const char *mode;          /* what CURIE_RENDERER asked for */
    const char *frame_rate;    /* SDL_HINT_MAIN_CALLBACK_RATE at rest */
    const char *drag_rate;     /* and while a button is held */
    const char *font;
    int   vsync, dark, sheets, tab;
    const char *aa;            /* what the frame is drawn with - not always
                                * what was asked for on the software path */
    float scale, style_ms, build_ms, render_ms, present_ms;
    float frame_gap_ms;        /* since the previous drawn frame */
    /* What a drawn frame costs this process in CPU, every thread, measured
     * over the first frames after startup; and the pointer-redraw gap chosen
     * from it. 0 until measured. */
    float cpu_ms_per_frame;
    int   hover_gap_ms;
    /* Where the memory is. nk_bytes is Nuklear's own command buffer, which
     * grows to fit the busiest frame drawn so far and is never given back;
     * icon_bytes is the rasterised SVG cache. */
    unsigned long nk_bytes, nk_used, icon_bytes;
    /* Two different questions. rss_bytes is the working set - what is resident,
     * shared driver and system pages included. private_bytes is commit, which
     * is the process's own. Every rss_at milestone below is working set. */
    unsigned long rss_bytes, private_bytes;
    int icons;
    /* The baked font atlas. atlas_bpp is read off the texture, so it says
     * what the backend actually produced rather than what it prefers. */
    int atlas_w, atlas_h, atlas_bpp;
    unsigned long rss_at[RSS_STEPS];   /* working set, one per milestone */
    unsigned long priv_at[RSS_STEPS];  /* private commit, the same milestones */
} curie_diag;

void curie_diagnostics(App *app, curie_diag *out);

showcase_state *curie_showcase(App *app);

/* Drawn by showcase.c, one page per tab above TAB_LOGIN. w and h are the
 * content region of the group it is being drawn into. */
void curie_showcase_page(App *app, struct nk_context *ctx, int tab,
                         float w, float h);

#endif /* CURIE_UI_H */
