/* sample.h - the numbers this particular application is built to.
 *
 * Card widths, titlebar heights, control sizes: none of it is library, so none
 * of it belongs in a header the library shares.
 */
#ifndef REAKTOR_SAMPLE_H
#define REAKTOR_SAMPLE_H

/* The pages, in strip order. Tab 0 is the login screen this app began as. The
 * rest exist to put every Nuklear widget on screen under the same stylesheet,
 * which is the only honest way to find out how far the CSS seam actually
 * reaches: a control that tiny.css has no rule for has to be styled from its
 * palette tokens instead, and doing that for all of them is what shows where
 * the line is. */
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
extern const char *const reaktor_tab_names[TAB_COUNT];

#define CARD_W        420
#define TITLEBAR_H    36
#define TAB_H         34      /* the strip under it */
#define TAB_PAD_X     14      /* either side of a tab's label */
#define RESIZE_EDGE    6
#define CTL_SIZE      28   /* the control's square hit area */
#define TITLE_PAD      1   /* before the app mark */
#define MARK_SIZE     18   /* the app mark, drawn size */
#define GLYPH_MINIMISE 18
#define GLYPH_MAXIMISE 14
#define GLYPH_CLOSE    18

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

/* The showcase's own state, owned by samples/showcase.c. */
showcase_state *sample_state(void);

/* A file the platform picker returned, delivered into that state. Called from
 * the frame rather than from the page that shows it, so a pick answered while
 * another tab is on screen still clears. */
void sample_file_taken(App *app);

/* This application's keyboard bindings; see samples/shortcuts.c. Answers
 * non-zero when the key was one of them and has been acted on. */
int sample_key(App *app, const SDL_Event *e);

/* The same bindings, written out for the accessibility tree: what reaches one
 * page, and what moves between them. Empty when nothing does. */
void sample_tab_keys(int tab, char *out, int cap);
void sample_tablist_keys(char *out, int cap);

/* Which page is on screen, and moving to another. A library that draws
 * widgets has no notion of a page, so the sample keeps this itself. */
int  sample_tab(void);
void set_tab(App *app, int tab);

/* One showcase page, any tab above TAB_LOGIN. w and h are the content region
 * of the group it is being drawn into. */
void reaktor_showcase_page(App *app, struct nk_context *ctx, int tab,
                           float w, float h);

#endif /* REAKTOR_SAMPLE_H */
