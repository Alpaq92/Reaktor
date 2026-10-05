#ifndef REAKTOR_UI_PUBLIC_H
#define REAKTOR_UI_PUBLIC_H

#include "reaktor/nuklear.h"
#include "reaktor/a11y.h"

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
    REAKTOR_RSS_STYLE,
    REAKTOR_RSS_STEPS
};

struct nk_color            reaktor_col(const unsigned char rgba[4]);
struct nk_color            reaktor_token(const char *name,
                                         struct nk_color def);
struct nk_color            reaktor_visible(struct nk_color want,
                                           struct nk_color behind,
                                           struct nk_color fallback);
const struct nk_user_font *reaktor_font(App *app, int px, int bold);

struct nk_image reaktor_ionicon(App *app, const char *name, int px);

#define REAKTOR_MARK "assets/icons/reaktor-icon.svg"
/* An SVG by asset name; ?stroke=#rrggbb, &fill=, &sw= recolor it. */
struct nk_image reaktor_svg(App *app, const char *src, int px);

void reaktor_fill_round(App *app, struct nk_command_buffer *cv,
                        struct nk_rect b, float rounding, struct nk_color col);
void reaktor_edge_round(App *app, struct nk_command_buffer *cv,
                        struct nk_rect b, float rounding, float width,
                        struct nk_color col);
struct nk_color reaktor_on(struct nk_color bg);

void reaktor_hot(App *app, struct nk_rect r, int cursor, int repaint);
void reaktor_hot_top(App *app, struct nk_rect r, int cursor, int repaint);
void reaktor_hot_follow(App *app, struct nk_rect r, int cursor);

int reaktor_button_label(App *app, struct nk_context *ctx, const char *label);
void reaktor_slider_bar(App *app, struct nk_context *ctx, unsigned id,
                        float *val, float lo, float hi, float step);

void reaktor_progress_bar(App *app, struct nk_context *ctx, nk_size *cur,
                          nk_size max, int modifiable);

void reaktor_chevron_at(App *app, struct nk_context *ctx, struct nk_rect slot,
                        const char *name, struct nk_color col);

int reaktor_button_accent(App *app, struct nk_context *ctx, const char *label);
const struct nk_user_font *reaktor_style_font(App *app,
                                              const char *selector,
                                              int px, int bold);
float reaktor_popup_rounding(void);

#define REAKTOR_MENU_ROW 22.0f
#define REAKTOR_MENU_GAP  2.0f
void  reaktor_menu_style_push(struct nk_context *ctx);
void  reaktor_menu_style_pop(struct nk_context *ctx);
float reaktor_menu_height(int rows);
void  reaktor_menu_edge(App *app, struct nk_context *ctx);
typedef struct reaktor_toast_spec {
    const char *text;
    const char *icon;
    const char *action;
    void      (*on_action)(App *app, void *user);
    void      (*content)(App *app, struct nk_context *ctx, void *user);
    float       content_w;
    void       *user;
    int         timeout_ms;
    int         no_close;
} reaktor_toast_spec;

/* Main thread; hooks get the main window's App. */
void  reaktor_toast(App *app, const reaktor_toast_spec *spec);

int   reaktor_menu_item(App *app, struct nk_context *ctx, const char *label,
                        const char *accel, int contextual);

unsigned reaktor_note(App *app, unsigned char role, const char *name,
                      const char *value, unsigned state,
                      struct nk_rect bounds);
unsigned reaktor_note_push(App *app, unsigned char role, const char *name,
                           const char *value, unsigned state,
                           struct nk_rect bounds);
void reaktor_note_pop(App *app);

int reaktor_focus_activated(App *app, unsigned id);
void reaktor_focus_area(App *app, struct nk_rect area, unsigned node);
int  reaktor_focus_scroll(App *app, struct nk_rect *reveal);

void reaktor_note_keys(App *app, unsigned id, const char *keys);
void reaktor_note_value(App *app, unsigned id, const char *value);
void reaktor_note_bounds(App *app, unsigned id, struct nk_rect r);
unsigned reaktor_note_here(App *app, struct nk_context *ctx,
                           unsigned char role, const char *name,
                           unsigned state);

int reaktor_file_open(App *app);

typedef struct reaktor_diag {
    const char *renderer;
    const char *mode;
    const char *frame_rate;
    const char *drag_rate;
    const char *font;
    int   vsync, dark, sheets;
    const char *aa;
    float scale, style_ms, build_ms, render_ms, present_ms;
    float frame_gap_ms;
    float cpu_ms_per_frame;
    int   hover_gap_ms;
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
