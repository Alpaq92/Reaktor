#ifndef REAKTOR_UI_PUBLIC_H
#define REAKTOR_UI_PUBLIC_H

#include <SDL3/SDL.h>

#include "reaktor/nuklear.h"
#include "reaktor/a11y.h"
#include "reaktor/widgets.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

enum {
    REAKTOR_RSS_ENTRY,
    REAKTOR_RSS_SDL,
    REAKTOR_RSS_WINDOW,
    REAKTOR_RSS_ICON,
    REAKTOR_RSS_NUKLEAR,
    REAKTOR_RSS_FONT,
    REAKTOR_RSS_START,
    REAKTOR_RSS_STYLE,
    REAKTOR_RSS_STEPS
};

struct reaktor_color       reaktor_col(const unsigned char rgba[4]);
struct reaktor_color       reaktor_token(const char *name,
                                         struct reaktor_color def);
struct reaktor_color       reaktor_visible(struct reaktor_color want,
                                           struct reaktor_color behind,
                                           struct reaktor_color fallback);
struct reaktor_color       reaktor_page_color(App *app);
struct reaktor_color       reaktor_accent_color(App *app);
const struct reaktor_user_font *reaktor_font(App *app, int px, int bold);

struct reaktor_image reaktor_ionicon(App *app, const char *name, int px);
struct reaktor_image reaktor_ionicon_exact(App *app, const char *name, int px,
                                           struct reaktor_color stroke,
                                           float sw);
void reaktor_glyph_at(App *app, struct reaktor_context *ctx,
                      struct reaktor_rect slot,
                      const char *name, struct reaktor_color col, int px,
                      float sw);

#define REAKTOR_MARK "assets/icons/reaktor-icon.svg"
struct reaktor_image reaktor_image_file(App *app, const char *src, int px);
void reaktor_image_centered(struct reaktor_context *ctx, struct reaktor_image im,
                            int px);

typedef struct reaktor_style_frame {
    int items, colors, floats, vec2s, fonts;
} reaktor_style_frame;
void reaktor_style_frame_pop(struct reaktor_context *ctx, reaktor_style_frame f);

void reaktor_fill_round(App *app, struct reaktor_command_buffer *cv,
                        struct reaktor_rect b, float rounding,
                        struct reaktor_color col);
void reaktor_edge_round(App *app, struct reaktor_command_buffer *cv,
                        struct reaktor_rect b, float rounding, float width,
                        struct reaktor_color col);
struct reaktor_color reaktor_on(struct reaktor_color bg);

void reaktor_hot(App *app, struct reaktor_rect r, int cursor, int repaint);
void reaktor_hot_top(App *app, struct reaktor_rect r, int cursor, int repaint);
void reaktor_hot_follow(App *app, struct reaktor_rect r, int cursor);
void reaktor_tip(App *app, struct reaktor_rect r, const char *text);

int reaktor_button_label(App *app, struct reaktor_context *ctx,
                         const char *label);
int reaktor_button_icon(App *app, struct reaktor_context *ctx,
                        const char *ionicon, const char *label);
int reaktor_button_accent_icon(App *app, struct reaktor_context *ctx,
                               const char *ionicon, const char *label);
float reaktor_button_width(App *app, struct reaktor_context *ctx,
                           const char *ionicon, const char *label);
int reaktor_button_custom(App *app, struct reaktor_context *ctx, const char *name,
                          reaktor_button_paint paint, void *user);
reaktor_flags reaktor_field_text(App *app, struct reaktor_context *ctx,
                                 reaktor_flags flags,
                                 char *buf, int *len, int cap, const char *hint,
                                 reaktor_plugin_filter filter, float pad_x,
                                 float pad_y);
void reaktor_slider_bar(App *app, struct reaktor_context *ctx, unsigned id,
                        float *val, float lo, float hi, float step);

void reaktor_progress_bar(App *app, struct reaktor_context *ctx,
                          reaktor_size *cur,
                          reaktor_size max, int modifiable);

void reaktor_chevron_at(App *app, struct reaktor_context *ctx,
                        struct reaktor_rect slot,
                        const char *name, struct reaktor_color col);

int reaktor_button_accent(App *app, struct reaktor_context *ctx,
                          const char *label);
const struct reaktor_user_font *reaktor_style_font(App *app,
                                                   const char *selector,
                                                   int px, int bold);
float reaktor_popup_rounding(void);

#define REAKTOR_MENU_ROW 22.0f
#define REAKTOR_MENU_GAP  2.0f
void  reaktor_menu_style_push(struct reaktor_context *ctx);
void  reaktor_menu_style_pop(struct reaktor_context *ctx);
float reaktor_menu_height(int rows);
void  reaktor_menu_edge(App *app, struct reaktor_context *ctx);
typedef struct reaktor_toast_spec {
    const char *text;
    const char *icon;
    const char *action;
    void      (*on_action)(App *app, void *user);
    void      (*content)(App *app, struct reaktor_context *ctx, void *user);
    float       content_w;
    void       *user;
    int         timeout_ms;
    int         no_close;
} reaktor_toast_spec;

void  reaktor_toast(App *app, const reaktor_toast_spec *spec);

int   reaktor_menu_item(App *app, struct reaktor_context *ctx,
                        const char *label,
                        const char *accel, int contextual);

unsigned reaktor_note(App *app, unsigned char role, const char *name,
                      const char *value, unsigned state,
                      struct reaktor_rect bounds);
unsigned reaktor_note_push(App *app, unsigned char role, const char *name,
                           const char *value, unsigned state,
                           struct reaktor_rect bounds);
void reaktor_note_pop(App *app);

int reaktor_focus_activated(App *app, unsigned id);
void reaktor_focus_area(App *app, struct reaktor_rect area, unsigned node);
int  reaktor_focus_scroll(App *app, struct reaktor_rect *reveal);

void reaktor_note_keys(App *app, unsigned id, const char *keys);
void reaktor_note_value(App *app, unsigned id, const char *value);
void reaktor_note_bounds(App *app, unsigned id, struct reaktor_rect r);
unsigned reaktor_note_here(App *app, struct reaktor_context *ctx,
                           unsigned char role, const char *name,
                           unsigned state);

enum {
    REAKTOR_CURSOR_DEFAULT = 0,
    REAKTOR_CURSOR_POINTER,
    REAKTOR_CURSOR_TEXT,
    REAKTOR_CURSOR_RESIZE_X,
    REAKTOR_CURSOR_RESIZE_Y,
    REAKTOR_CURSORS
};

enum { REAKTOR_FILE_OPEN = 0, REAKTOR_FILE_SAVE, REAKTOR_FOLDER_OPEN };

typedef SDL_DialogFileFilter reaktor_file_filter;

typedef struct reaktor_file_dialog {
    int                        kind;
    const reaktor_file_filter *filters;
    int                        filter_count;
    const char                *location;
    int                        many;
    void (*chosen)(App *app, const char *const *paths, int count, void *user);
    void                      *user;
} reaktor_file_dialog;

int reaktor_file_dialog_open(App *app, const reaktor_file_dialog *dialog);

typedef struct reaktor_question {
    const char        *text;
    const char *const *buttons;
    int                count;
    void (*answered)(App *app, int button, void *user);
    void              *user;
} reaktor_question;

int reaktor_ask(App *app, const reaktor_question *question);

typedef void (*reaktor_task)(App *app, void *user);

unsigned reaktor_timer(App *app, int ms, int repeat, reaktor_task fn,
                       void *user);
void     reaktor_timer_stop(unsigned id);
void     reaktor_post(App *app, reaktor_task fn, void *user);

const char *reaktor_setting(const char *key);
void        reaktor_setting_set(const char *key, const char *value);
void        reaktor_settings_save(void);
void        reaktor_settings_clear(void);

typedef struct reaktor_notification {
    const char *title;
    const char *text;
} reaktor_notification;

int reaktor_notify(App *app, const reaktor_notification *n);

typedef struct reaktor_print_spec {
    const char *title;
    int         pages;
    float       w, h;
    int         dpi;
    void      (*page)(App *app, struct reaktor_context *ctx, int index, int w, int h,
                      void *user);
    void      (*done)(App *app, int ok, void *user);
    void       *user;
} reaktor_print_spec;

int reaktor_print(App *app, const reaktor_print_spec *s);
int reaktor_print_pdf(App *app, const char *path, const reaktor_print_spec *s);

typedef struct reaktor_undo_step {
    const char *label;
    void      (*undo)(void *data);
    void      (*redo)(void *data);
    void      (*free)(void *data);
    void       *data;
    unsigned    merge;
} reaktor_undo_step;

void        reaktor_undo_push(App *app, const reaktor_undo_step *s);
int         reaktor_undo(App *app);
int         reaktor_redo(App *app);
const char *reaktor_undo_label(App *app);
const char *reaktor_redo_label(App *app);
void        reaktor_undo_group(App *app, int begin);
void        reaktor_undo_saved(App *app);
int         reaktor_undo_dirty(App *app);
void        reaktor_undo_clear(App *app);

typedef struct reaktor_diag {
    const char *renderer;
    const char *mode;
    const char *frame_rate;
    const char *drag_rate;
    const char *font;
    int   vsync, dark, sheets;
    const char *aa;
    float scale, style_ms, build_ms, render_ms, present_ms;
    float window_ms, atlas_ms, first_frame_ms;
    float frame_gap_ms;
    float cpu_ms_per_frame;
    int   hover_gap_ms;
    unsigned long frames_drawn, frames_skipped;
    unsigned long nk_bytes, nk_used, icon_bytes;
    unsigned long rss_bytes, private_bytes;
    int icons;
    int atlas_w, atlas_h, atlas_bpp;
    unsigned long rss_at[REAKTOR_RSS_STEPS];
    unsigned long priv_at[REAKTOR_RSS_STEPS];
} reaktor_diag;

void reaktor_diagnostics(App *app, reaktor_diag *out);
void reaktor_frame_ms(App *app, double *build, double *render, double *present);

void   reaktor_process_memory(size_t *rss, size_t *priv);
double reaktor_process_cpu_ms(void);

#endif
