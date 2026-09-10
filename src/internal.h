/* internal.h - what the shell's translation units share.
 *
 * main.c was one file; splitting it means the App struct and the handful of
 * macros above it have to be visible from more than one place. Nothing here is
 * public: ui.h is what a page may use, and this is what the shell's own files
 * use. It exists because of the split and for no other reason.
 */
#ifndef REAKTOR_INTERNAL_H
#define REAKTOR_INTERNAL_H

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nk_common.h"
#include "nk_sdl3_renderer.h"   /* the vendored backend nk_impl.c compiles */
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

#define WINDOW_WIDTH  960      /* logical px; scaled via reaktor_px() */
#define WINDOW_HEIGHT 680
#define FONT_SIZE     16

/* 16 is what tiny.css resolves to; the rest of the ladder is for headings and
 * small print. Nearest wins, so a size in between costs no fidelity. */
/* Only sizes something asks for: each step is a full glyph set in the atlas,
 * and 23 was a megapixel nothing ever drew from. */
#define FONT_STEPS 5
static const int g_font_px[FONT_STEPS] = { 12, 13, 14, 16, 19 };

/* One slot per icon per size per theme - the stroke colour is part of the key,
 * so a scheme change empties the cache rather than doubling it. 48 covers
 * the busiest page (Buttons: ~15 icons at 2-3 sizes) with headroom; on
 * overflow the cache returns a null image, which draws blank rather than
 * crashes, so this is a soft cap on memory not a hard cap on the app. */
#define IMG_CACHE_MAX 48

/* Height of the titlebar this app draws for itself when the native one is
 * turned off, and how wide a strip along each edge grabs for a resize. */
/* Nuklear adds 4px between columns and the group pads again, so the gap on
 * screen is ~10px wider than these numbers. */
#define TITLE_PX      16   /* the name's size; the one size the bold is baked at */

/* Both memory counters at each startup milestone (the list is in ui.h). File
 * statics because the first two samples land before App exists. Two counters
 * because they differ: resident says what a step maps in, mostly the driver's
 * shared pages; private says what it cost this process. */
static size_t g_rss[RSS_STEPS];
static size_t g_priv[RSS_STEPS];

/* How SDL should pace SDL_AppIterate, as the hint value. Asked before SDL_Init
 * and again once App exists, so it reads the environment rather than App. */
static const char *frame_rate_wanted(void)
{
    const char *rate = SDL_getenv("REAKTOR_FRAME_RATE");
    const char *redraw = SDL_getenv("REAKTOR_REDRAW");

    if (rate && *rate) return rate;
    return (redraw && SDL_strcmp(redraw, "always") == 0) ? "0" : "waitevent";
}

static void rss_mark(int step)
{
    reaktor_process_memory(&g_rss[step], &g_priv[step]);
}

/* Ionicons draw a 32-unit stroke on a 512 viewBox - 6.25% - so below 16px the
 * line falls under a pixel and greys out. Scaling the SVG's stroke widths puts
 * it back over one and lets the maximise sit a size below the other two. */
/* Even, so (CTL_SIZE - glyph) / 2 is a whole pixel: at 17 the minimise dash
 * straddled a boundary and came out one bright row between two dim. */

/* At this scale the artwork's line is about one device pixel and reads as a
 * hairline; doubling it puts two solid pixels down. */
#define GLYPH_STROKE  2.0f

/* tiny.css keeps palette and rules in separate files, so a theme is which
 * variables file loads in front of core.css. Read from the submodule. */
/* Three: tiny.css's palette, tiny.css's rules, and this application's own
 * layer on top - see assets/reaktor.css for why the third one exists. */
#define SHEET_COUNT 3
static const char *
theme_sheet(int dark)
{
    return dark ? "third_party/tinycss/src/variables-dark.css"
                : "third_party/tinycss/src/variables-light.css";
}
#define CORE_SHEET "third_party/tinycss/src/core.css"
#define APP_SHEET  "assets/reaktor.css"

/* Which scheme the user asked for, as opposed to which one is in force. */
#define THEME_SYSTEM 0
#define THEME_LIGHT  1
#define THEME_DARK   2
static const char *const g_theme_names[3] = { "system", "light", "dark" };

struct img_slot {
    char         src[192];
    int          px;        /* raster size this slot was built at */
    SDL_Texture *tex;
    int          w, h;
};

/* One disc mask per radius, for reaktor_fill_round. Eight is more radii than
 * any one screen asks for. */
#define ROUND_CACHE_MAX 8
struct round_slot {
    int          r;
    SDL_Texture *tex;
};

/* Named, and declared as an incomplete type in ui.h, so a page can be handed
 * an App * without being handed the window, the renderer or the atlas. */
struct App {
    SDL_Window        *win;
    SDL_Renderer      *ren;
    struct nk_context *ctx;
    struct nk_color    clear;
    int                dark;

    /* Decoded SVG sources, keyed by their path. Small and linear: a screen
     * references a handful of icons, not hundreds. */
    struct img_slot img[IMG_CACHE_MAX];
    int img_count;

    struct round_slot round[ROUND_CACHE_MAX];
    int round_count;

    /* Nuklear bakes a glyph atlas per size, so a face is baked once per size
     * the stylesheet asks for and selected per draw. */
    struct nk_font *faces[FONT_STEPS];
    struct nk_font *face_bold;      /* Aileron-Bold at TITLE_PX, for the title */
    char            font_status[160];
    /* The backend's atlas, kept only so a rebake can free the bake before it -
     * see rebuild_font. */
    struct nk_font_atlas *atlas;
    /* The baked atlas, for the diagnostics. Bytes per pixel is read off the
     * texture, not assumed: the backend can fall back to RGBA32. */
    int             atlas_w, atlas_h, atlas_bpp;

    /* Nuklear owns the editing behaviour, but the state lives here because a
     * context menu has to act on it: nk_edit_buffer takes ours. */
    struct nk_text_edit edit;
    char                edit_buf[128];

    /* Decided while the frame is built - Nuklear knows what is under the
     * pointer only as each widget is emitted - and applied once at the end. */
    SDL_Cursor *cur_default, *cur_pointer, *cur_text;
    int         want_cursor, cur_shown;   /* 0 default, 1 pointer, 2 text */

    char render_mode[32];   /* wide enough for "auto (software: no GPU)" */
    int  vsync_on, aa, redraw_always;

    /* How SDL paces SDL_AppIterate: "waitevent", a frame rate, or 0 for
     * uncapped. See the note where it is applied. */
    char frame_rate[16];
    char drag_rate[16];        /* the display's refresh, as a rate string */
    int  first_frame_done;
    int  dragging;             /* a mouse button is held */

    /* Borderless: the titlebar is drawn like any widget, and
     * SDL_SetWindowHitTest tells the desktop what drags and what resizes. The
     * hit test runs on the OS thread and cannot ask Nuklear anything, so the
     * control rects are recorded as they are emitted. */
    /* Frames owed after a click before the rate returns to "waitevent". Two:
     * a click completes on release, which is where the rate was restored, so
     * the frame showing the result was never scheduled - and one is not enough
     * because a page lays out top to bottom, so a toggle halfway down is read
     * after the sizes above it were fixed. */
    int            restore_rate;
    int            borderless;
#ifdef __APPLE__
    /* On macOS the styleMask change inside SDL_SetWindowBordered arrives
     * back as a phantom mouse event that Nuklear reads as a second click
     * on the same toggle button in the next frame, so the flag flips
     * twice and the frame that macOS already added stays. Two pieces:
     * pending defers the real click to SDL_AppIterate (so it does not
     * race against a click that macOS is about to invent), and
     * lock_frames swallows the phantom click that arrives shortly after
     * the apply. Three frames is enough to cover the WindowServer round
     * trip and short enough that a real second click can never fit
     * inside it - a user cannot physically press the same button twice
     * within 50 ms. */
    int            borderless_pending;
    int            borderless_lock_frames;
#endif
    struct nk_rect ctl[3];     /* minimise, maximise, close */
    int            ctl_n;
    int            want_quit;

    /* Where the text field was on the last painted frame, and whether the
     * current drag started inside it. See the clamp in SDL_AppEvent. */
    struct nk_rect field_rect;
    int            field_rect_valid;
    int            drag_in_field;

    /* Where the *focused* field is, in window coordinates, for the IME to put
     * its candidate list beside rather than at the window's origin. Separate
     * from field_rect above, which follows the pointer. */
    SDL_Rect ime_rect;
    int      ime_cursor;       /* caret offset from ime_rect.x */
    int      ime_valid;

    /* The platform file picker. SDL runs it on its own thread on most
     * platforms and calls back from there, so the callback does the least it
     * can - fill in the answer, publish it, wake the loop - and the main
     * thread does everything else when it collects. */
    SDL_AtomicInt file_ready;      /* 0 nothing waiting, 1 answer in place */
    char          file_answer[SC_PATH_CAP];
    int           file_pending;    /* a picker is up; main thread only */
    Uint32        wake_event;      /* pushed from the callback's thread */

    int   theme_mode;          /* THEME_SYSTEM | THEME_LIGHT | THEME_DARK */
    /* A scheme picked mid-frame, as mode + 1, for the next frame to apply
     * where no style is pushed. Zero when there is nothing waiting. */
    int   theme_pending;
    struct nk_color page, card_bg, text;
    /* --text-muted and --links as "#rrggbb" - strings, because that is what
     * goes into the SVG before it is parsed. */
    char  icon_hex[10];
    char  accent_hex[10];

    /* Where the interactive widgets landed last frame, and what hovering each
     * costs. Two questions: every control wants a cursor, but only some change
     * appearance on hover. So the cursor is read straight off this list with
     * no frame at all, and a repaint asked for only when the pointer crosses
     * something whose looks depend on hover. */
    struct hot_region {
        struct nk_rect r;
        unsigned char  cursor;    /* 0 default, 1 pointer, 2 text */
        unsigned char  repaint;   /* does hover change what is drawn? */
        /* Emitted inside a popup, which is drawn over the page - so it wins
         * against anything it covers however the two were recorded. */
        unsigned char  top;
        /* Repaint on every move *inside* it, not only on crossing into it.
         * A tooltip is drawn at the pointer, so it has to follow one. */
        unsigned char  track;
    } hot[192];
    int hot_n, hot_last;
    /* What hot_last referred to: hot[] is rebuilt every frame, so an index is
     * only meaningful within the frame that filled it. */
    struct nk_rect hot_last_r;
    unsigned char  hot_last_repaint;

    int   dirty;
    int   show_contact;

    /* Pointer-driven redraws are coalesced: see hover_redraw, and
     * calibrate_hover_gap for how the gap is chosen. */
    Uint64 last_draw_ms;
    int    hover_pending;
    int    renderer_is_sw;      /* SDL's software rasteriser - see the
                                * antialiasing note in the frame body */
    int    sw_noaa;             /* REAKTOR_SW_NOAA: no feathering at all
                                * there - see the frame body */
    int    drag_moved;          /* pointer moved since the last drawn frame */
    int    cal_frames, cal_done;
    double cal_cpu0;
    float  cpu_ms_per_frame;    /* 0 until calibrated */

    /* The retained description of the frame - see a11y.h. Rebuilt every frame
     * from the widgets as they are drawn, and diffed against the previous one.
     * Large (arenas, not pointers), so it lives here rather than on a stack. */
    reaktor_a11y a11y;
    /* Where the declared boxes went. One frame behind by construction - see
     * core/ui/layout.h - and empty until a page declares something. */
    reaktor_layout lay;

    /* Keyboard focus - phase 3 of docs/ACCESSIBILITY.md. The tree above is in
     * reading order, so its focusable subset is the tab order and nothing
     * separate is kept. focus_id names a node in it, 0 for none. */
    unsigned       focus_id;
    int            focus_visible;  /* the ring: a key shows it, a click hides it */
    struct nk_rect focus_rect;     /* where the focused node landed this frame */
    /* Arrows pressed while a range had focus, in steps, taken by the widget
     * on the next frame and cleared there whether or not it was drawn. They
     * accumulate: key repeat outruns the frame rate. */
    int            focus_step;
    /* A node Enter or a platform client asked to activate, for the widget to
     * take on the next frame. A widget that takes it acts on itself, which is
     * what makes activation reliable; the frame that ends with it untaken
     * falls back to the synthetic click below, which is what everything that
     * has not opted in still gets. */
    unsigned       activate_id;
    int            focus_seen;
    /* Enter or Space on the focused node, delivered to Nuklear as a press at
     * its centre and released on the frame after. */
    int            key_click;
    float          key_click_x, key_click_y;
    /* Focus landing outside the page's visible band scrolls the page to it
     * on the next frame. body_rect is that band, in window coordinates, as
     * of the last frame; page_node is the tree id of the page's group, so
     * only nodes inside the page ask for a scroll - the tab strip is
     * outside the band too, and must not. */
    struct nk_rect body_rect;
    unsigned       page_node;
    int            focus_scroll;
    struct nk_rect focus_scroll_rect;

    int   laid_w, laid_h;
    float fps;
    int   fps_frames;
    Uint64 fps_t0;
    /* The gap to the previous drawn frame. fps counts frames over a second and
     * that second only ticks on when one is drawn, so with frames arriving
     * rarely it reads stale; the interval says the same thing at once. */
    Uint64 last_frame_ms;
    float  frame_gap_ms;
    int   style_ms_x100;   /* time the last stylesheet load took */
    /* Where a frame goes, in hundredths of a millisecond. Split three ways
     * because guessing which one dominates has been wrong twice. */
    int   build_ms_x100, render_ms_x100, present_ms_x100;
};

/* Defined in core/draw.c. These were static in main.c until that file was
 * split; the file boundary is the only reason they are declared here. */
void img_cache_clear(App *app);
struct nk_image icon_over(App *app, const char *src, int px, float over);
struct nk_image icon(App *app, const char *src, int px);
void image_centred(struct nk_context *ctx, struct nk_image im, int px);
const struct nk_user_font *pick_font(App *app, int px, int bold);
void rebuild_font(App *app);
void apply_render_scale(App *app);
void set_window_icon(SDL_Window *win);

/* Defined in src/main.c; core/focus.c draws the ring with it. */
struct nk_color col_of(const unsigned char c[4]);

/* Defined in core/focus.c. main.c hands these to the accessibility layer as
 * the callbacks a screen reader's press and focus move arrive through. */
void reader_focus(void *user, unsigned id);
void reader_activate(void *user, unsigned id);

int  focus_key(App *app, const SDL_Event *event);
void focus_ring(App *app, struct nk_context *ctx);
void focus_resolve(App *app);

/* Defined in runtime/app.c: the hover-frame gap in force, which
 * reaktor_diagnostics reports. */
extern Uint64 g_hover_gap_ms;

/* Defined in samples/shell.c; runtime/app.c installs it on the window. */
SDL_HitTestResult SDLCALL window_hit_test(SDL_Window *win,
                                          const SDL_Point *pt, void *data);

/* Crossing files since main.c was split; each was static in it. */
int  env_int(const char *name, int fallback);
int  renderer_is_software(SDL_Renderer *ren);
void load_theme(App *app);
void page_shell(App *app, struct nk_context *ctx, int win_w, int win_h);
void a11y_dump_once(App *app);

/* How many entries push_button_style pushed, so a caller pops the same
 * number. Shared because the sample's titlebar pushes styles of its own. */
typedef struct style_frame { int items, colors, floats, vec2s, fonts; } style_frame;

/* The CSS-to-Nuklear helpers, reached by the sample's own shell. Stage 5
 * replaces this with a public API; until then the file boundary needs them. */
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
void note_field_rect(App *app, struct nk_context *ctx, struct nk_rect bounds);
void stroke_edit_edge(struct nk_context *ctx, struct nk_rect b,
                      const reaktor_style *s);
void note_ime_caret(App *app, struct nk_context *ctx, struct nk_rect bounds,
                    nk_flags state, const struct nk_text_edit *edit);
void draw_hint(struct nk_context *ctx, struct nk_rect bounds, const char *hint,
               const reaktor_style *s);

/* --- the imperative widget surface --------------------------------------
 *
 * These were in ui.h, which is what a page includes. They are not there any
 * more, because no page calls them: every one is what a declared widget draws
 * through, and `core/ui/declare.c` is the only caller left.
 *
 * That is the whole of stage 08, and it is a move rather than the deletion the
 * plan expected. Deleting them would mean reimplementing each widget inside
 * declare.c, and the reason the declarative layer could be trusted from the
 * first page was precisely that it did not: behaviour is inherited from code
 * that already worked, and a bug fixed here is fixed for both.
 *
 * What did happen is the part that mattered. A page can no longer reach them,
 * so a page can no longer drift back into drawing imperatively by accident,
 * and the header a page reads is 47 declarations shorter than it was - the
 * public surface is the declarative one.
 *
 * The exceptions, and why: what stays in ui.h is what a page still legitimately
 * asks the shell for - the palette and the fonts, the file dialog, the
 * diagnostics - plus the accessibility calls and the hand-drawn chrome that
 * the widgets with no spec still need. Those widgets are groups, trees, list
 * views, charts and the insides of popups, and they are stage 10's problem,
 * not something to pretend away here. */

/* The same as reaktor_ionicon, stroked in a colour of the caller's choosing -
 * for an icon that sits on something other than the page, such as a row
 * filled with the accent. reaktor_on gives the colour that reads on a given
 * background. */
struct nk_image reaktor_ionicon_exact(App *app, const char *name, int px,
                                      struct nk_color stroke, float sw);
struct nk_image reaktor_ionicon_col(App *app, const char *name, int px,
                                    struct nk_color stroke);

/* Draws an image at exactly px, centred in the current widget slot. nk_image
 * stretches to fill its slot instead, so the size asked for is ignored. */
void reaktor_image(App *app, struct nk_context *ctx, struct nk_image im,
                   int px);

/* Cuts a button's vertical padding to what its row can hold, for the one
 * widget about to be drawn, and puts it back. Only the icon-only button needs
 * it now: everything else draws through css_button, and push_button_style
 * bounds the padding itself - it has to, because it pushes the stylesheet's
 * value afterwards and would otherwise undo this. */
int  reaktor_fit_label(App *app, struct nk_context *ctx, struct nk_rect b);
void reaktor_unfit_label(struct nk_context *ctx, int fitted);

/* One Ionicon centred in `slot` at exactly px, no resampling - which is what
 * keeps a rim a line rather than a smear. `sw` multiplies the artwork's own
 * stroke; 0 takes the hairline rule. The disc names below go with it: a
 * circle is the one shape the software rasteriser cannot draw, so every
 * circle in this tree is a glyph. */
void reaktor_glyph_at(App *app, struct nk_context *ctx, struct nk_rect slot,
                      const char *name, struct nk_color col, int px, float sw);
#define REAKTOR_DISC_ROUND   "ellipse"
#define REAKTOR_DISC_RING    "radio-button-off"
#define REAKTOR_DISC_OUTLINE "ellipse-outline"

/* A radio, drawn as a glyph rather than as Nuklear's three filled circles -
 * see the note on the definition. `on` is whether this one is the chosen one;
 * answers whether it was clicked. */
int reaktor_radio_label(App *app, struct nk_context *ctx, const char *label,
                        int on);

/* The int-valued slider, and the knob. See ui.h on reaktor_slider_bar and
 * reaktor_progress_bar for why either draws its own geometry. */
void reaktor_slider_bar_int(App *app, struct nk_context *ctx, unsigned id,
                            int *val, int lo, int hi, int step);
void reaktor_knob_dial(App *app, struct nk_context *ctx, float *val,
                       float lo, float hi, enum nk_heading zero);

/* The chrome around a property stepper. Nuklear draws its increment and
 * decrement as square washes with a text arrow; the stylesheet asks for a
 * round wash and a chevron glyph, and neither is something nk_property can
 * be told. Push, draw the property, pop, then lay the chrome over it. */
void reaktor_property_push(struct nk_context *ctx);
void reaktor_property_pop(struct nk_context *ctx);
void reaktor_property_chrome(App *app, struct nk_context *ctx,
                             struct nk_rect b);

/* A combo box's own drawing. Nuklear's header has a text arrow and a square
 * swatch with a literal zero rounding; the stylesheet asks for a chevron and
 * the same radius as everything else, and neither is a style field. So the
 * header is drawn, and then this is laid over it. `content` is the region
 * inside the header a swatch or a glyph belongs in. */
struct nk_rect reaktor_combo_content(struct nk_context *ctx, struct nk_rect h);
void reaktor_combo_chrome(App *app, struct nk_context *ctx, struct nk_rect h,
                          float border);

/* Text that acts. `active` draws it in the link colour rather than muted. */
int reaktor_link_label(App *app, struct nk_context *ctx, const char *label,
                       int active);
int reaktor_button_icon(App *app, struct nk_context *ctx,
                        const char *ionicon, const char *label);
/* The button rule with no label and the given fill: a colour swatch that is
 * otherwise a button. `name` is what a reader is told it is. */
int reaktor_button_color(App *app, struct nk_context *ctx, const char *name,
                         struct nk_color fill);

/* A text field styled from tiny.css's `input` rule, with `hint` painted into
 * it while it is empty. Nuklear has no placeholder of its own. */
nk_flags reaktor_field_text(App *app, struct nk_context *ctx, nk_flags flags,
                       char *buf, int *len, int cap, const char *hint,
                       nk_plugin_filter filter);

/* How many steps the arrows asked this node for, taken once and only while it
 * has focus. A range answers its own arrows: the shell knows a node's value
 * only as the text a reader would hear, and nothing of its bounds or its
 * grain. Every other role lets the arrows move focus instead. */
int reaktor_focus_step(App *app, unsigned id);

/* The numbers behind a range's value text, for the node `id` names - what a
 * platform needs to offer a slider as something to set rather than only to
 * read. See reaktor_a11y_set_range. */
void reaktor_note_range(App *app, unsigned id, float num, float lo, float hi,
                        float step);

/* Stops reports being recorded while a widget that already reports itself is
 * drawn by one that has reported it. Nested, so it is safe to bracket a call
 * that brackets another. */
void reaktor_note_mute(App *app, int on);

#endif /* REAKTOR_INTERNAL_H */
