#ifndef REAKTOR_UI_H
#define REAKTOR_UI_H

#include "nk_common.h"
#include "a11y.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

enum {
    RSS_ENTRY,
    RSS_SDL,
    RSS_WINDOW,
    RSS_ICON,
    RSS_NUKLEAR,
    RSS_FONT,
    RSS_STYLE,
    RSS_STEPS
};
extern const char *const reaktor_rss_names[RSS_STEPS];

#define SC_TEXT_CAP  64
#define SC_BOX_CAP   512
#define SC_SERIES_N  32
#define SC_LIST_N    64
#define SC_PATH_CAP  520

struct nk_color            reaktor_col(const unsigned char rgba[4]);
struct nk_color            reaktor_token(const char *name,
                                         struct nk_color def);
struct nk_color            reaktor_visible(struct nk_color want,
                                           struct nk_color behind,
                                           struct nk_color fallback);
const struct nk_user_font *reaktor_font(App *app, int px, int bold);

struct nk_image reaktor_ionicon(App *app, const char *name, int px);

void reaktor_fill_round(App *app, struct nk_command_buffer *cv,
                        struct nk_rect b, float rounding, struct nk_color col);
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

void reaktor_note_keys(App *app, unsigned id, const char *keys);
void reaktor_note_bounds(App *app, unsigned id, struct nk_rect r);
unsigned reaktor_note_here(App *app, struct nk_context *ctx,
                           unsigned char role, const char *name,
                           unsigned state);

int reaktor_file_open(App *app);
int reaktor_file_taken(App *app, char *out, int cap);

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
    unsigned long rss_at[RSS_STEPS];
    unsigned long priv_at[RSS_STEPS];
} reaktor_diag;

void reaktor_diagnostics(App *app, reaktor_diag *out);

int reaktor_css_override(App *app, int on);
int reaktor_css_override_on(App *app);

#endif
