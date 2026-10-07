#ifndef REAKTOR_INTERNAL_H
#define REAKTOR_INTERNAL_H

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nk_common.h"
#include "nk_sdl3_renderer.h"
#include "appicon.h"
#include "theme.h"
#include "metrics.h"
#include "reaktor.h"
#include "style.h"
#include "layout.h"
#include "ui.h"
#include "text.h"
#include "reaktor/launch.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#define WINDOW_WIDTH  1100
#define WINDOW_HEIGHT 680
#define FONT_SIZE     16

#define FONT_STEPS 8

#define IMG_CACHE_MAX 48

#define TITLE_PX      16

#define REAKTOR_NO_CLIP nk_rect(-8192.0f, -8192.0f, 16384.0f, 16384.0f)

enum {
    KEY_CLICK_NONE, KEY_CLICK_ASKED, KEY_CLICK_PRESS, KEY_CLICK_RELEASE,
    KEY_CLICK_AGAIN
};

extern size_t reaktor_rss[REAKTOR_RSS_STEPS];
extern size_t reaktor_priv[REAKTOR_RSS_STEPS];
void reaktor_rss_mark(int step);

#define GLYPH_STROKE  2.0f

#define SHEET_MAX 12
#define CORE_SHEET "external/tinycss/src/core.css"
#define APP_SHEET  "assets/rest/reaktor.css"
#define FONT_FILE      "assets/fonts/Aileron-Regular.otf"
#define FONT_BOLD_FILE "assets/fonts/Aileron-Bold.otf"

struct tex_slot {
    SDL_Texture *tex;
    unsigned     seen;
};

struct img_slot {
    struct tex_slot s;
    char            src[192];
    int             px, w, h;
};

#define ROUND_CACHE_MAX 32
struct round_slot {
    struct tex_slot s;
    int             r, t;
};

#define FIELD_UNDO_STEPS 100

struct field_step {
    int   at, cut, put;
    char *bytes;
};

struct field_undo {
    const struct nk_window *win;
    const char             *buf;
    char                   *text;
    int                     len, room;
    struct field_step       step[FIELD_UNDO_STEPS];
    int                     n, top;
};

struct App {
    SDL_Window        *win;
    SDL_Renderer      *ren;
    struct nk_context *ctx;
    struct nk_color    clear;
    int                dark;

    struct img_slot *img;
    int img_count, img_cap;
    SDL_Texture *blank;

    struct round_slot round[ROUND_CACHE_MAX];
    int round_count;

    struct nk_font *faces[FONT_STEPS];
    struct nk_font *bolds[FONT_STEPS];
    struct nk_font *face_bold;
    char            font_status[160];
    struct nk_font_atlas *atlas;
    int             atlas_w, atlas_h, atlas_bpp;
    /* Nuklear reads these until the next bake. */
    unsigned       *glyphs;

    struct nk_text_edit edit;
    char                edit_buf[128];

    SDL_Cursor *cur_default, *cur_pointer, *cur_text;
    int         want_cursor, cur_shown;

    char render_mode[32];
    int  vsync_on, aa, redraw_always;

    char frame_rate[16];
    char drag_rate[16];
    int  first_frame_done;
    int  dragging;
    int  drag_mouse;

    int    touch, touch_in_field, touch_n;
    float  touch_x0, touch_y0, touch_last, touch_unit;
    float  touch_ys[4];
    Uint64 touch_ns[4];
    float  fling_v;
    Uint64 fling_ns;

    int            restore_rate;
    int            borderless;
#ifdef __APPLE__
    int            borderless_pending;
    int            borderless_lock_frames;
#endif

    const char    *shot_path;
    const char    *dump_path;
    const char    *renderer_pref;
    const char    *video_pref;
    const char    *lang_pref;

    struct nk_rect field_rect;
    int            field_rect_valid;
    struct field_undo field_undo;
    int            drag_in_field;
    int            editing;
    int            stop_editing;

    SDL_Rect ime_rect;
    int      ime_cursor;
    int      ime_valid;

    SDL_AtomicInt file_ready, file_done;
    char          file_answer[520];
    int           file_pending;

    int   theme_mode;
    int   theme_pending;
    struct nk_color page, card_bg, text, accent;
    char  icon_hex[10];
    char  accent_hex[10];

    struct hot_region {
        struct nk_rect r;
        unsigned char  cursor;
        unsigned char  repaint;
        unsigned char  top;
        unsigned char  track;
    } hot[192];
    int hot_n, hot_last;
    struct nk_rect hot_last_r;
    unsigned char  hot_last_repaint;

    int   dirty;

    struct input_queue {
        SDL_Event     *ev;
        int            n, cap;
        int            changed, moved, moved_ok, hook_ate;
        SDL_Keycode    key;
        size_t         text;
        struct nk_vec2 pointer;
        int            pointer_back;
    } queue;
    SDL_AtomicInt  wake;
    SDL_WindowID   win_id;
    int            page_held;

    Uint64 last_draw_ms;
    int    hover_pending;
    int    renderer_is_sw;
    int    sw_noaa;
    int    drag_moved;
    int    cal_frames, cal_done;
    double cal_cpu0;
    float  cpu_ms_per_frame;

    reaktor_a11y a11y;
    reaktor_layout lay;

    unsigned       focus_id;
    int            focus_visible;
    struct nk_rect focus_rect;
    int            focus_step;
    unsigned       activate_id;
    int            focus_seen;
    int            key_click;
    struct nk_rect body_rect;
    unsigned       page_node;
    int            focus_scroll;
    struct nk_rect focus_scroll_rect;
    int            focus_layer;
    const struct nk_window *focus_win;
    int            layer, layer_trap, layer_held, input_held, focus_held;
    int            popup_lo, popup_hi, trap_lo, trap_hi;
    struct nk_input held_input;

    int   laid_w, laid_h;
    float fps;
    int   fps_frames;
    Uint64 fps_t0;
    Uint64 last_frame_ms;
    float  frame_gap_ms;
    struct nka_context *anim;
    int   style_ms_x100;
    int   sheets;
    const char *font_face, *font_face_bold;
    const char *icon_dirs[8];
    int         icon_dir_count;
    int         file_ok;
    int         shot_failed, dump_failed;
    int         secondary;
    int         settled_run, settle_tries;
    unsigned    style_gen;
    int   build_ms_x100, render_ms_x100, present_ms_x100;
};

void img_cache_clear(App *app);
void img_cache_free(App *app);
struct nk_image icon_over(App *app, const char *src, int px, float over);
struct nk_image icon(App *app, const char *src, int px);
void image_centred(struct nk_context *ctx, struct nk_image im, int px);
const struct nk_user_font *pick_font(App *app, int px, int bold);
void rebuild_font(App *app);
void apply_render_scale(App *app);
void reaktor_set_window_icon(SDL_Window *win, const char *name);

struct nk_color col_of(const unsigned char c[4]);

void reader_focus(void *user, unsigned id);
void reader_activate(void *user, unsigned id);

enum { FOCUS_PAGE, FOCUS_POPUP, FOCUS_TOAST, FOCUS_FLOATER };

enum { HOLD_NONE, HOLD_ALL, HOLD_FRESH };

int  focus_key(App *app, const SDL_Event *event);
void focus_ring(App *app, struct nk_context *ctx, struct nk_command_buffer *cv);
void reaktor_focus_ring_overlay(App *app, struct nk_context *ctx);
void focus_resolve(App *app);
void reaktor_key_click_ask(App *app);
int  reaktor_focus_covered(App *app);

extern Uint64 g_hover_gap_ms;

int  renderer_is_software(SDL_Renderer *ren);
void load_theme(App *app);
void a11y_dump_once(App *app);

typedef struct style_frame { int items, colors, floats, vec2s, fonts; } style_frame;

void hot_push(App *app, struct nk_rect r, int cursor, int repaint);
void pop_style(struct nk_context *ctx, style_frame f);
void css_field(App *app, struct nk_context *ctx, char *buf, int *len, int cap,
               const char *hint);
int  css_button_accent(App *app, struct nk_context *ctx, const char *selector,
                       const char *label, struct nk_color accent);
int  reaktor_css_button_image(App *app, struct nk_context *ctx, const char *selector,
                      struct nk_image im, float px, const char *name);
void reaktor_toasts_draw(App *app, struct nk_context *ctx, int win_w,
                         int win_h, int blocked);
void reaktor_toast_clear(void);
void reaktor_floaters_draw(App *app, struct nk_context *ctx, int win_w, int win_h);
int  reaktor_floaters_modal(void);
void reaktor_floater_frame(App *app, struct nk_context *ctx, struct nk_rect window,
                         int modal);
void reaktor_floaters_clear(App *app);
int  reaktor_popup_open(const struct nk_window *w);
void reaktor_layer_focus(struct nk_context *ctx, struct nk_window *to);
int  reaktor_layer_begin(App *app, struct nk_context *ctx, const char *name,
                         struct nk_rect r, nk_flags flags, int layer, int trap,
                         int hold, int *held);
void reaktor_layer_end(App *app, struct nk_context *ctx, int shown);

typedef struct reaktor_surface {
    struct nk_color fill, edge;
    float           border, radius;
} reaktor_surface;

reaktor_surface reaktor_rule_surface(const reaktor_style *s, float radius);
void reaktor_paint_surface(App *app, struct nk_command_buffer *cv, struct nk_rect r,
                           const reaktor_surface *s);
struct nk_rect reaktor_rect_trunc(struct nk_rect b);
nk_size reaktor_fade_mark(const struct nk_command_buffer *cv);
void reaktor_fade_since(struct nk_context *ctx, const struct nk_command_buffer *cv,
                        nk_size mark, float opacity);

App *reaktor_main_app(void);
const char *reaktor_asset_name(const reaktor_asset *a, const char *fallback);
int  reaktor_hot_motion(App *app, float mx, float my);
void reaktor_show_cursor(App *app);
void reaktor_place_ime(App *app);
void reaktor_render(App *app);

typedef void (*reaktor_page_fn)(App *app, struct nk_context *ctx, int w, int h,
                                void *user);
void reaktor_app_size(const App *app, int *w, int *h);
void reaktor_app_due(App *app, int w, int h);
int  reaktor_app_frame(App *app, int w, int h, const char *title,
                       reaktor_page_fn page, void *user);
void reaktor_app_input_free(App *app);
App *reaktor_windows_app(const SDL_Event *event);
void reaktor_windows_take(App *app, const SDL_Event *event);
int  reaktor_windows_retitle(App *app, const char *title);
void reaktor_windows_draw(void);
void reaktor_windows_restyle(void);
void reaktor_windows_dirty(void);
void reaktor_windows_rescale(void);
void reaktor_windows_close_all(void);
const char *reaktor_launch_icon(void);
int  css_button_icon(App *app, struct nk_context *ctx, const char *selector,
                     const char *icon_src, const char *label);

struct nk_color readable_on(const unsigned char bg[4], const char *preferred,
                            const char *fallback);
style_frame push_edit_style(struct nk_context *ctx, reaktor_style *out, int inset);

void apply_widget_style(App *app);

void hot_push_ex(App *app, struct nk_rect r, int cursor, int repaint,
                 int top, int track);
int  css_button(App *app, struct nk_context *ctx, const char *selector,
                const char *label);

typedef struct reaktor_button_look {
    reaktor_style s, hov, act;
} reaktor_button_look;
void reaktor_button_look_of(const char *selector, reaktor_button_look *k);
int  reaktor_css_button_look(App *app, struct nk_context *ctx,
                             const reaktor_button_look *k, const char *label);
int  reaktor_button_label_as(App *app, struct nk_context *ctx,
                             const char *sel, const char *label);
int  reaktor_button_accent_as(App *app, struct nk_context *ctx,
                              const char *sel, const char *label);
int  reaktor_button_icon_as(App *app, struct nk_context *ctx, const char *sel,
                            const char *ionicon, const char *label);
void note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds);
void reaktor_edit_edge(App *app, struct nk_context *ctx, struct nk_rect b,
               const reaktor_style *s);
void reaktor_button_arm(struct nk_context *ctx);
void reaktor_select_arm(struct nk_rect b, const nk_bool *on, float rounding);
void reaktor_chrome_disarm(void);
void note_edit_active(App *app, struct nk_context *ctx, nk_flags state);
void note_ime_caret(App *app, struct nk_context *ctx, struct nk_rect bounds,
                    nk_flags state, const struct nk_text_edit *edit);
void draw_hint(struct nk_context *ctx, struct nk_rect bounds, const char *hint,
               const reaktor_style *s);

struct nk_image reaktor_ionicon_col(App *app, const char *name, int px,
                                    struct nk_color stroke);

void reaktor_image(App *app, struct nk_context *ctx, struct nk_image im,
                   int px);

int  reaktor_fit_label(App *app, struct nk_context *ctx, struct nk_rect b);
void reaktor_unfit_label(struct nk_context *ctx, int fitted);

#define REAKTOR_DISC_ROUND   "ellipse"
#define REAKTOR_DISC_RING    "radio-button-off"
#define REAKTOR_DISC_OUTLINE "ellipse-outline"

int reaktor_radio_label(App *app, struct nk_context *ctx, const char *label,
                        int on);

void reaktor_slider_bar_int(App *app, struct nk_context *ctx, unsigned id,
                            int *val, int lo, int hi, int step);
void reaktor_knob_dial(App *app, struct nk_context *ctx, float *val,
                       float lo, float hi, enum nk_heading zero);

void reaktor_property_push(struct nk_context *ctx);
void reaktor_property_pop(struct nk_context *ctx);
void reaktor_property_chrome(App *app, struct nk_context *ctx,
                             struct nk_rect b);

struct nk_rect reaktor_combo_content(struct nk_context *ctx, struct nk_rect h);
void reaktor_combo_chrome(App *app, struct nk_context *ctx, struct nk_rect h,
                          float border);

int reaktor_link_label(App *app, struct nk_context *ctx, const char *label,
                       int active);
int reaktor_button_color(App *app, struct nk_context *ctx, const char *name,
                         struct nk_color fill);

void field_undo_clear(App *app);

int reaktor_focus_step(App *app, unsigned id);

void reaktor_note_range(App *app, unsigned id, float num, float lo, float hi,
                        float step);

void reaktor_note_mute(App *app, int on);

#endif
