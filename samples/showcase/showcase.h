#ifndef REAKTOR_SHOWCASE_H
#define REAKTOR_SHOWCASE_H

#include "sample.h"

enum {
    TAB_LOGIN = 0,
    TAB_BUTTONS,
    TAB_INPUTS,
    TAB_DISPLAY,
    TAB_LAYOUT,
    TAB_POPUPS,
    TAB_ANIM,
    TAB_STYLING,
    TAB_DIAG,
    TAB_COUNT
};
extern const char *const reaktor_tab_names[TAB_COUNT];

#define CARD_W        420
#define TITLEBAR_H    36
#define TAB_H         34
#define TAB_PAD_X     14
#define RESIZE_EDGE    6
#define CTL_SIZE      28
#define TITLE_PAD      1
#define MARK_SIZE     18
#define GLYPH_MINIMISE 18
#define GLYPH_MAXIMISE 14
#define GLYPH_CLOSE    18

typedef struct showcase_state {
    int          repeats, presses;
    nk_bool      check_wrap, check_spell;
    unsigned int flags;
    int          radio;
    nk_bool      sel_tile[4];
    nk_bool      sel_row;
    nk_bool      toggle;

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

    float series[SC_SERIES_N];
    int   list_sel;
    nk_bool tree_leaf[3];

    int   anim_slot;
    int   anim_curve;
    int   anim_ms;
    float anim_head;
    nk_bool anim_play;

    int  popup_open;
    char menu_pick[40];
    char file_pick[SC_PATH_CAP];

    int seeded;
} showcase_state;

showcase_state *sample_state(void);

void sample_tab_keys(int tab, char *out, int cap);
void sample_tablist_keys(char *out, int cap);

int  sample_tab(void);
void set_tab(App *app, int tab);

void reaktor_showcase_page(App *app, struct nk_context *ctx, int tab,
                           float w, float h);

#endif
