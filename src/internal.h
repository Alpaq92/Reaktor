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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#define WINDOW_WIDTH  1100
#define WINDOW_HEIGHT 680
#define FONT_SIZE     16

/* The ladder pick_font snaps to: a fifth of a step apart, from body text to
 * a page heading. It used to stop at 19, so a stylesheet asking for a heading
 * got 19px however large it asked and a document's whole type scale collapsed
 * into one size. Geometric rather than a list of sizes read off some
 * particular sheet, so no size is privileged and nothing goes stale.
 *
 * Every step is baked twice, regular and bold - a sheet that can set
 * font-size but not font-weight is only half reading the sheet. */
#define FONT_STEPS 8

#define IMG_CACHE_MAX 48

#define TITLE_PX      16

/* One definition, in runtime/app.c. As statics in this header they were a
 * private copy per translation unit: app.c filled its own and the Diagnostics
 * page read widgets.c's, which was zero and always would be. */
extern size_t reaktor_rss[RSS_STEPS];
extern size_t reaktor_priv[RSS_STEPS];
void reaktor_rss_mark(int step);

#define GLYPH_STROKE  2.0f

#define SHEET_MAX 4
#define CORE_SHEET "external/tinycss/src/core.css"
#define APP_SHEET  "assets/rest/reaktor.css"

#define USER_SHEET "external/simplecss/simple.css"

#define THEME_SYSTEM 0
#define THEME_LIGHT  1
#define THEME_DARK   2

struct img_slot {
    char         src[192];
    int          px;
    SDL_Texture *tex;
    int          w, h;
};

#define ROUND_CACHE_MAX 8
struct round_slot {
    int          r;
    SDL_Texture *tex;
};

struct App {
    SDL_Window        *win;
    SDL_Renderer      *ren;
    struct nk_context *ctx;
    struct nk_color    clear;
    int                dark;

    struct img_slot img[IMG_CACHE_MAX];
    int img_count;

    struct round_slot round[ROUND_CACHE_MAX];
    int round_count;

    struct nk_font *faces[FONT_STEPS];
    struct nk_font *bolds[FONT_STEPS];
    struct nk_font *face_bold;
    char            font_status[160];
    struct nk_font_atlas *atlas;
    int             atlas_w, atlas_h, atlas_bpp;

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

    int            restore_rate;
    int            borderless;
#ifdef __APPLE__
    int            borderless_pending;
    int            borderless_lock_frames;
#endif
    struct nk_rect ctl[3];
    int            ctl_n;
    int            want_quit;

    /* --shot and --a11y-dump: a run that can be compared with the last one.
     * NULL unless the flag was given. */
    const char    *shot_path;
    const char    *dump_path;
    const char    *renderer_pref;

    struct nk_rect field_rect;
    int            field_rect_valid;
    int            drag_in_field;

    SDL_Rect ime_rect;
    int      ime_cursor;
    int      ime_valid;

    SDL_AtomicInt file_ready;
    char          file_answer[SC_PATH_CAP];
    int           file_pending;
    Uint32        wake_event;

    int   theme_mode;
    int   theme_pending;
    struct nk_color page, card_bg, text;
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
    int   show_contact;

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
    float          key_click_x, key_click_y;
    struct nk_rect body_rect;
    unsigned       page_node;
    int            focus_scroll;
    struct nk_rect focus_scroll_rect;

    int   laid_w, laid_h;
    float fps;
    int   fps_frames;
    Uint64 fps_t0;
    Uint64 last_frame_ms;
    float  frame_gap_ms;
    int   style_ms_x100;
    int   sheets;
    int   css_override_off;
    int   build_ms_x100, render_ms_x100, present_ms_x100;
};

void img_cache_clear(App *app);
struct nk_image icon_over(App *app, const char *src, int px, float over);
struct nk_image icon(App *app, const char *src, int px);
void image_centred(struct nk_context *ctx, struct nk_image im, int px);
const struct nk_user_font *pick_font(App *app, int px, int bold);
void rebuild_font(App *app);
void apply_render_scale(App *app);
void set_window_icon(SDL_Window *win);

struct nk_color col_of(const unsigned char c[4]);

void reader_focus(void *user, unsigned id);
void reader_activate(void *user, unsigned id);

int  focus_key(App *app, const SDL_Event *event);
void focus_ring(App *app, struct nk_context *ctx);
void focus_resolve(App *app);

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
                       const char *label, const char *token);
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
int  reaktor_button_label_as(App *app, struct nk_context *ctx,
                             const char *sel, const char *label);
int  reaktor_button_accent_as(App *app, struct nk_context *ctx,
                              const char *sel, const char *label);
int  reaktor_button_icon_as(App *app, struct nk_context *ctx, const char *sel,
                            const char *ionicon, const char *label);
void note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds);
void stroke_edit_edge(struct nk_context *ctx, struct nk_rect b,
                      const reaktor_style *s);
void note_ime_caret(App *app, struct nk_context *ctx, struct nk_rect bounds,
                    nk_flags state, const struct nk_text_edit *edit);
void draw_hint(struct nk_context *ctx, struct nk_rect bounds, const char *hint,
               const reaktor_style *s);

struct nk_image reaktor_ionicon_exact(App *app, const char *name, int px,
                                      struct nk_color stroke, float sw);
struct nk_image reaktor_ionicon_col(App *app, const char *name, int px,
                                    struct nk_color stroke);

void reaktor_image(App *app, struct nk_context *ctx, struct nk_image im,
                   int px);

int  reaktor_fit_label(App *app, struct nk_context *ctx, struct nk_rect b);
void reaktor_unfit_label(struct nk_context *ctx, int fitted);

void reaktor_glyph_at(App *app, struct nk_context *ctx, struct nk_rect slot,
                      const char *name, struct nk_color col, int px, float sw);
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
int reaktor_button_icon(App *app, struct nk_context *ctx,
                        const char *ionicon, const char *label);
int reaktor_button_color(App *app, struct nk_context *ctx, const char *name,
                         struct nk_color fill);

nk_flags reaktor_field_text(App *app, struct nk_context *ctx, nk_flags flags,
                       char *buf, int *len, int cap, const char *hint,
                       nk_plugin_filter filter, float pad_x, float pad_y);

int reaktor_focus_step(App *app, unsigned id);

void reaktor_note_range(App *app, unsigned id, float num, float lo, float hi,
                        float step);

void reaktor_note_mute(App *app, int on);

#endif
