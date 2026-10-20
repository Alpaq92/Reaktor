#ifndef REAKTOR_WIDGETS_H
#define REAKTOR_WIDGETS_H

#include "reaktor/nuklear.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

enum {
    REAKTOR_LAY_ROW = 0,
    REAKTOR_LAY_COLUMN,
    REAKTOR_LAY_FREE
};

enum {
    REAKTOR_LAY_FILL_X = 1u << 0,
    REAKTOR_LAY_FILL_Y = 1u << 1,
    REAKTOR_LAY_WRAP   = 1u << 2,
    REAKTOR_LAY_CENTER_X = 1u << 3,
    REAKTOR_LAY_CENTER_Y = 1u << 4,
    REAKTOR_LAY_PACK_CENTER = 1u << 5,
    REAKTOR_LAY_PACK_END    = 1u << 6,
    REAKTOR_LAY_PACK_SPREAD = 1u << 7
};

typedef struct reaktor_box {
    unsigned char dir;
    const char   *name;
    float         w, h;
    float         weight;
    float         gap;
    float         ml, mt, mr, mb;
    unsigned      flags;
} reaktor_box;

void reaktor_box_open(unsigned char dir, const reaktor_box *b);
void reaktor_box_close(void);

#define REAKTOR_BOX_SCOPE_(axis, ...)                                        \
    for (int reaktor_box_scope_ =                                            \
             (reaktor_box_open((unsigned char)(axis),                        \
                               &(reaktor_box){ 0, __VA_ARGS__ }), 1);        \
         reaktor_box_scope_;                                                 \
         reaktor_box_scope_ = (reaktor_box_close(), 0))

int reaktor_box_rect(struct reaktor_rect *out);

unsigned reaktor_box_id(void);

#define REAKTOR_ROW(...)    REAKTOR_BOX_SCOPE_(REAKTOR_LAY_ROW, __VA_ARGS__)
#define REAKTOR_COLUMN(...) REAKTOR_BOX_SCOPE_(REAKTOR_LAY_COLUMN, __VA_ARGS__)
#define REAKTOR_FREE(...)   REAKTOR_BOX_SCOPE_(REAKTOR_LAY_FREE, __VA_ARGS__)

void reaktor_gap(float w, float h);

void reaktor_soak(void);

typedef void (*reaktor_action)(void *user);

typedef struct reaktor_handler {
    reaktor_action fn;
    void          *user;
} reaktor_handler;

enum { REAKTOR_LEFT = 0, REAKTOR_CENTER, REAKTOR_RIGHT };

typedef void (*reaktor_button_paint)(App *app, struct reaktor_command_buffer *cv,
                                     struct reaktor_rect content,
                                     struct reaktor_color ink, void *user);

typedef struct reaktor_button_spec {
    const char     *label;
    const char     *icon;
    const char     *name;
    const char     *style;
    const char     *keys;
    reaktor_box     box;
    const char     *tooltip;
    reaktor_handler on_press;
    unsigned char   accent;
    unsigned char   disabled;
    unsigned char   repeat;
    unsigned char   fit_content;
    unsigned char   flat;
    reaktor_button_paint paint;
    void           *paint_user;
} reaktor_button_spec;

int reaktor_button(const reaktor_button_spec *s);

typedef struct reaktor_label_spec {
    const char   *text;
    const char   *name;
    const char   *value;
    const char   *style;
    const char   *color;
    reaktor_box   box;
    unsigned char align;
    unsigned char wrap;
    unsigned char silent;
} reaktor_label_spec;

void reaktor_label(const reaktor_label_spec *s);

typedef struct reaktor_swatch_spec {
    const char     *name;
    struct reaktor_color fill;
    const char *style;
    reaktor_box     box;
    reaktor_handler on_press;
} reaktor_swatch_spec;

int reaktor_swatch(const reaktor_swatch_spec *s);

typedef struct reaktor_icon_spec {
    const char   *name;
    const char   *src;
    const char   *tooltip;
    const char *style;
    reaktor_box   box;
    unsigned char accent;
} reaktor_icon_spec;

void reaktor_icon(const reaktor_icon_spec *s);

typedef struct reaktor_field_spec {
    char       *buf;
    int        *len;
    int         cap;
    const char *hint;
    const char *name;
    const char *style;
    float pad_x, pad_y;
    reaktor_plugin_filter filter;
    reaktor_box box;
    unsigned char multiline;
    unsigned char line_numbers;
    char        **heap;
    int          *heap_cap;
} reaktor_field_spec;

void reaktor_field(const reaktor_field_spec *s);
void reaktor_field_select(App *app, const char *buf, int from, int to);

typedef struct reaktor_check_spec {
    const char   *label;
    const char   *name;
    reaktor_bool *on;
    unsigned     *flags;
    unsigned      bit;
    const char *style;
    reaktor_box   box;
    unsigned char box_right;
} reaktor_check_spec;

int reaktor_check(const reaktor_check_spec *s);

typedef struct reaktor_radio_spec {
    const char   *label;
    const char   *name;
    int          *choice;
    int           value;
    const char *style;
    reaktor_box   box;
} reaktor_radio_spec;

int reaktor_radio(const reaktor_radio_spec *s);

typedef struct reaktor_select_spec {
    const char   *label;
    const char   *name;
    reaktor_bool *on;
    const char   *icon;
    unsigned char disc;
    const char *style;
    reaktor_box   box;
    unsigned char centered;
} reaktor_select_spec;

int reaktor_select(const reaktor_select_spec *s);

typedef struct reaktor_slider_spec {
    const char *name;
    const char *text;
    float      *value;
    int        *ivalue;
    float       lo, hi, step;
    const char *style;
    reaktor_box box;
} reaktor_slider_spec;

void reaktor_slider(const reaktor_slider_spec *s);

typedef struct reaktor_progress_spec {
    const char *name;
    reaktor_size *value;
    reaktor_size max;
    const char *style;
    reaktor_box box;
    unsigned char modifiable;
} reaktor_progress_spec;

void reaktor_progress(const reaktor_progress_spec *s);

typedef struct reaktor_knob_spec {
    const char *name;
    float      *value;
    float       lo, hi;
    const char *style;
    reaktor_box box;
} reaktor_knob_spec;

void reaktor_knob(const reaktor_knob_spec *s);

typedef struct reaktor_property_spec {
    const char *label;
    const char *name;
    int        *ivalue;
    float      *fvalue;
    double     *dvalue;
    double      lo, hi, step;
    float       grain;
    const char *style;
    reaktor_box box;
} reaktor_property_spec;

void reaktor_property(const reaktor_property_spec *s);

typedef struct reaktor_spinner_spec {
    const char        *label;
    const char        *name;
    const char *const *items;
    int                count;
    int               *chosen;
    unsigned char      wrap;
    const char        *style;
    reaktor_box        box;
} reaktor_spinner_spec;

int reaktor_spinner(const reaktor_spinner_spec *s);

typedef struct reaktor_date_spec {
    const char *name;
    int        *year, *month, *day;
    int         min_year, max_year;
    reaktor_box box;
} reaktor_date_spec;

int reaktor_date_pick(const reaktor_date_spec *s);

typedef struct reaktor_time_spec {
    const char   *name;
    int          *hour, *minute;
    unsigned char clock12;
    reaktor_box   box;
} reaktor_time_spec;

int reaktor_time_pick(const reaktor_time_spec *s);

typedef struct reaktor_combo_spec {
    const char     *label;
    const char     *name;
    float           body_h;
    const char *style;
    reaktor_box     box;
    unsigned char   disc;
    const struct reaktor_color *swatch;
} reaktor_combo_spec;

int  reaktor_combo_open(const reaktor_combo_spec *s);
void reaktor_combo_close(void);

#define REAKTOR_COMBO(...)                                                   \
    for (int reaktor_combo_scope_ =                                          \
             reaktor_combo_open(&(reaktor_combo_spec){ __VA_ARGS__ });       \
         reaktor_combo_scope_;                                               \
         reaktor_combo_scope_ = (reaktor_combo_close(), 0))

int reaktor_combo_item(const char *label, int chosen);

typedef struct reaktor_color_spec {
    const char       *name;
    struct reaktor_colorf *value;
    const char *style;
    reaktor_box       box;
} reaktor_color_spec;

void reaktor_color_pick(const reaktor_color_spec *s);

typedef struct reaktor_link_spec {
    const char     *text;
    const char     *url;
    const char     *name;
    const char     *style;
    reaktor_box     box;
    reaktor_handler on_press;
    unsigned char   active;
} reaktor_link_spec;

int reaktor_link(const reaktor_link_spec *s);

#define REAKTOR_LIST_COLUMNS 16

typedef struct reaktor_list_spec {
    const char        *name;
    int                count;
    const char *const *columns;
    const float       *widths;
    int                column_count;
    const char      *(*cell)(int row, int column, void *user);
    void              *user;
    int               *selected;
    float              row_h;
    reaktor_box        box;
    const char        *drag;
    void             (*dropped)(const char *from, int item, int at, void *user);
} reaktor_list_spec;

int reaktor_list(const reaktor_list_spec *s);

typedef struct reaktor_tree_spec {
    const char    *name;
    int            count;
    int          (*parent)(int node, void *user);
    const char  *(*label)(int node, void *user);
    const char  *(*icon)(int node, void *user);
    void          *user;
    unsigned char *open;
    int           *selected;
    float          row_h;
    reaktor_box    box;
} reaktor_tree_spec;

int reaktor_tree(const reaktor_tree_spec *s);

typedef struct reaktor_menu_entry {
    const char     *label;
    const char     *icon;
    const char     *keys;
    reaktor_handler on_press;
    reaktor_bool   *checked;
    unsigned char   disabled;
    const struct reaktor_menu_entry *items;
    int             count;
    void          (*custom)(App *app, struct reaktor_context *ctx, void *user);
    float           height, width;
} reaktor_menu_entry;

typedef struct reaktor_menu {
    const char               *label;
    const reaktor_menu_entry *items;
    int                       count;
} reaktor_menu;

typedef struct reaktor_menubar_spec {
    const reaktor_menu        *menus;
    int                        count;
    const char                *name;
    reaktor_box                box;
    int                       *picked_menu;
    int                       *picked_item;
    const reaktor_menu_entry **picked;
} reaktor_menubar_spec;

int reaktor_menubar(const reaktor_menubar_spec *s);

typedef struct reaktor_splitter_spec {
    float        *value;
    float         lo, hi;
    const char   *name;
    unsigned char vertical;
} reaktor_splitter_spec;

int reaktor_splitter(const reaktor_splitter_spec *s);

enum { REAKTOR_NAV_PAGES, REAKTOR_NAV_CHOICE, REAKTOR_NAV_ACTIONS };

typedef struct reaktor_nav_spec {
    const char *const *items;
    const char *const *icons;
    const char *const *keys;
    int                count;
    int               *chosen;
    const char        *name;
    const char        *list_keys;
    const char        *style;
    reaktor_box        box;
    unsigned char      kind;
    unsigned char      narrow;
} reaktor_nav_spec;

typedef reaktor_nav_spec reaktor_sidebar_spec;
typedef reaktor_nav_spec reaktor_tabs_spec;

int reaktor_sidebar(const reaktor_sidebar_spec *s);
int reaktor_tabs(const reaktor_tabs_spec *s);

#endif
