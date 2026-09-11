#ifndef REAKTOR_DECLARE_H
#define REAKTOR_DECLARE_H

#include "layout.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

void reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area);
void reaktor_frame_end(void);

int reaktor_frame_settled(void);

void reaktor_box_open(unsigned char dir, const reaktor_box *b);
void reaktor_box_close(void);

#define REAKTOR_BOX_SCOPE_(axis, ...)                                        \
    for (int reaktor_box_scope_ =                                            \
             (reaktor_box_open((unsigned char)(axis),                        \
                               &(reaktor_box){ 0, __VA_ARGS__ }), 1);        \
         reaktor_box_scope_;                                                 \
         reaktor_box_scope_ = (reaktor_box_close(), 0))

int reaktor_box_rect(struct nk_rect *out);

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

enum { REAKTOR_LEFT = 0, REAKTOR_CENTRE, REAKTOR_RIGHT };

typedef struct reaktor_button_spec {
    const char     *label;
    const char     *icon;
    const char     *name;
    const char     *style;
    const char     *keys;
    reaktor_box     box;
    reaktor_handler on_press;
    unsigned char   accent;
    unsigned char   disabled;
    unsigned char   repeat;
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
    struct nk_color fill;
    const char *style;
    reaktor_box     box;
    reaktor_handler on_press;
} reaktor_swatch_spec;

int reaktor_swatch(const reaktor_swatch_spec *s);

typedef struct reaktor_icon_spec {
    const char   *name;
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
    nk_plugin_filter filter;
    reaktor_box box;
    unsigned char multiline;
} reaktor_field_spec;

void reaktor_field(const reaktor_field_spec *s);

typedef struct reaktor_check_spec {
    const char   *label;
    const char   *name;
    nk_bool      *on;
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
    nk_bool      *on;
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
    nk_size    *value;
    nk_size     max;
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

typedef struct reaktor_combo_spec {
    const char     *label;
    const char     *name;
    float           body_h;
    const char *style;
    reaktor_box     box;
    unsigned char   disc;
    const struct nk_color *swatch;
} reaktor_combo_spec;

int  reaktor_combo_open(const reaktor_combo_spec *s);
void reaktor_combo_close(void);

#define REAKTOR_COMBO(...)                                                       for (int reaktor_combo_scope_ =                                                       reaktor_combo_open(&(reaktor_combo_spec){ __VA_ARGS__ });                reaktor_combo_scope_;                                                        reaktor_combo_scope_ = (reaktor_combo_close(), 0))

int reaktor_combo_item(const char *label, int chosen);

typedef struct reaktor_colour_spec {
    const char       *name;
    struct nk_colorf *value;
    const char *style;
    reaktor_box       box;
} reaktor_colour_spec;

void reaktor_colour_pick(const reaktor_colour_spec *s);

typedef struct reaktor_link_spec {
    const char     *text;
    const char     *name;
    const char     *style;
    reaktor_box     box;
    reaktor_handler on_press;
    unsigned char   active;
} reaktor_link_spec;

int reaktor_link(const reaktor_link_spec *s);

#endif
