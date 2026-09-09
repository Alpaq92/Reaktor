/* ui.h - the seam between the app shell and the pages it draws.
 *
 * main.c owns the window, the renderer, the font atlas, the SVG cache and the
 * CSS -> nk_style translation. A page needs none of those directly, only the
 * handful of calls below, so App stays an opaque pointer and a page stays
 * about layout. */
#ifndef REAKTOR_UI_H
#define REAKTOR_UI_H

#include "nk_common.h"
#include "a11y.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

/* Startup milestones, sampled into reaktor_diag::rss_at so the diagnostics
 * page can report what each step of the startup cost. They live here rather
 * than in the shell because the page indexes the same list: adding a milestone
 * is one edit, and the array below sizes itself from it. */
enum {
    RSS_ENTRY,      /* before SDL_Init - the CRT, the loader, the image */
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
/* Long enough for a Windows extended-length path, which is where the file
 * picker can legitimately land. */
#define SC_PATH_CAP  520


/* --- what a page may ask of the shell ---------------------------------- */

struct nk_color            reaktor_col(const unsigned char rgba[4]);
/* A palette token as a colour, falling back to `def` when the stylesheet has
 * no such custom property. */
struct nk_color            reaktor_token(const char *name,
                                         struct nk_color def);
/* A colour, unless it is indistinguishable from `behind` - then `fallback`.
 * tiny.css's dark palette gives thead and details the page's own background,
 * which makes them invisible. */
struct nk_color            reaktor_visible(struct nk_color want,
                                           struct nk_color behind,
                                           struct nk_color fallback);
const struct nk_user_font *reaktor_font(App *app, int px, int bold);

/* The common case: an Ionicon stroked in the theme's muted text colour, so a
 * page never spells out a path. */
struct nk_image reaktor_ionicon(App *app, const char *name, int px);
/* The same, stroked in a colour of the caller's choosing - for an icon that
 * sits on something other than the page, such as a row filled with the
 * accent. reaktor_on gives the colour that reads on a given background. */
struct nk_image reaktor_ionicon_exact(App *app, const char *name, int px,
                                      struct nk_color stroke, float sw);

/* A filled rounded rect whose corners are anti-aliased on every backend -
 * nk_fill_rect's are not, on the software one. See the definition. */
void reaktor_fill_round(App *app, struct nk_command_buffer *cv,
                        struct nk_rect b, float rounding, struct nk_color col);
struct nk_image reaktor_ionicon_col(App *app, const char *name, int px,
                                    struct nk_color stroke);
struct nk_color reaktor_on(struct nk_color bg);

/* Records what the pointer is over: the cursor it wants, and whether hovering
 * it changes anything on screen. See the hot-region note in main.c. */
/* Draws an image at exactly px, centred in the current widget slot. nk_image
 * stretches to fill its slot instead, so the size asked for is ignored. */
void reaktor_image(App *app, struct nk_context *ctx, struct nk_image im,
                   int px);

void reaktor_hot(App *app, struct nk_rect r, int cursor, int repaint);
/* The same, for something emitted inside a popup: it is drawn over the page,
 * so it outranks whatever it covers however the two were recorded. */
void reaktor_hot_top(App *app, struct nk_rect r, int cursor, int repaint);
/* The same, but redrawing on every move inside it rather than only on
 * crossing into it - for anything drawn at the pointer. */
void reaktor_hot_follow(App *app, struct nk_rect r, int cursor);

/* Cuts a button's vertical padding to what its row can hold, for the one
 * widget about to be drawn, and puts it back. Without it a label on a row
 * shorter than padding + border + rounding is centred on a clamped content
 * rect and rides low - see the note on the definition in main.c. */
int  reaktor_fit_label(App *app, struct nk_context *ctx, struct nk_rect b);
void reaktor_unfit_label(struct nk_context *ctx, int fitted);

int reaktor_button_label(App *app, struct nk_context *ctx, const char *label);
/* Text that acts. `active` draws it in the link colour rather than muted. */
int reaktor_link_label(App *app, struct nk_context *ctx, const char *label,
                       int active);
int reaktor_button_accent(App *app, struct nk_context *ctx, const char *label);
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

/* The radius for a popup, tooltip or menu. Nuklear has a single rounding for
 * every panel, so it cannot be set globally without rounding the window. */
float reaktor_popup_rounding(void);

/* --- describing the frame ----------------------------------------------
 * What a screen reader will eventually be handed. A page reports each widget
 * as it draws it; the shell collects the reports into a tree and diffs them.
 * See a11y.h for the roles and states, docs/ACCESSIBILITY.md for the rest.
 *
 * These are the only calls a page needs. Nothing is served to a platform yet -
 * phase 4 - so an unreported widget is not a visible bug today, which is
 * exactly why the widgets go through helpers that report for you. */
/* Answers the node's id, which a widget that takes the keyboard needs -
 * see reaktor_focus_step. Most callers ignore it. */
unsigned reaktor_note(App *app, unsigned char role, const char *name,
                      const char *value, unsigned state,
                      struct nk_rect bounds);
/* The same, and everything until reaktor_note_pop is a child of it. */
unsigned reaktor_note_push(App *app, unsigned char role, const char *name,
                           const char *value, unsigned state,
                           struct nk_rect bounds);
void reaktor_note_pop(App *app);

/* How many steps the arrows asked this node for, taken once and only while it
 * has focus. A range answers its own arrows: the shell knows a node's value
 * only as the text a reader would hear, and nothing of its bounds or its
 * grain. Every other role lets the arrows move focus instead. */
int reaktor_focus_step(App *app, unsigned id);

/* Whether this node was asked to activate - by Enter, or by a screen reader
 * pressing it - taken once. A widget that answers it acts on itself, which is
 * the reliable path; one that does not still gets the synthetic click the
 * shell falls back to. See the definition. */
int reaktor_focus_activated(App *app, unsigned id);

/* The numbers behind a range's value text, for the node `id` names - what a
 * platform needs to offer a slider as something to set rather than only to
 * read. See reaktor_a11y_set_range. */
void reaktor_note_range(App *app, unsigned id, float num, float lo, float hi,
                        float step);
/* The chords that reach the node `id` names, as ARIA writes them - see
 * reaktor_shortcut_text in core/ui/keys.h. Announced by every bridge, bound
 * by none: the binding stays where the application declared it. */
void reaktor_note_keys(App *app, unsigned id, const char *keys);
/* Where the node `id` was placed, once the layout engine has said. */
void reaktor_note_bounds(App *app, unsigned id, struct nk_rect r);
/* Stops reports being recorded while a widget that already reports itself is
 * drawn by one that has reported it. Nested, so it is safe to bracket a call
 * that brackets another. Goes away with the imperative surface in stage 08. */
void reaktor_note_mute(App *app, int on);
/* Convenience for the common case: the widget just drawn, at the bounds the
 * layout gave it, with no value. */
unsigned reaktor_note_here(App *app, struct nk_context *ctx,
                           unsigned char role, const char *name,
                           unsigned state);

/* The platform's own Open dialog. Answers 0 if one is already up. The choice
 * arrives on SDL's thread, so it is not a return value: call
 * reaktor_file_taken on a later frame, which answers non-zero once - when
 * there is a fresh result - and writes it into `out`. A cancel and a platform
 * with no picker both come back as text, because a page that shows the answer
 * should show those too. */
int reaktor_file_open(App *app);
int reaktor_file_taken(App *app, char *out, int cap);

/* What the shell knows about itself, for the page that reports it. Copied
 * out rather than reached for, so App stays opaque. */
typedef struct reaktor_diag {
    const char *renderer;      /* SDL's name for the backend in use */
    const char *mode;          /* what REAKTOR_RENDERER asked for */
    const char *frame_rate;    /* SDL_HINT_MAIN_CALLBACK_RATE at rest */
    const char *drag_rate;     /* and while a button is held */
    const char *font;
    int   vsync, dark, sheets;
    const char *aa;            /* what the frame is drawn with - not always
                                * what was asked for on the software path */
    float scale, style_ms, build_ms, render_ms, present_ms;
    float frame_gap_ms;        /* since the previous drawn frame */
    /* What a drawn frame costs this process in CPU, every thread, measured
     * over the first frames after startup; and the pointer-redraw gap chosen
     * from it. 0 until measured. */
    float cpu_ms_per_frame;
    int   hover_gap_ms;
    /* Where the memory is. nk_bytes is Nuklear's own command buffer, which
     * grows to fit the busiest frame drawn so far and is never given back;
     * icon_bytes is the rasterised SVG cache. */
    unsigned long nk_bytes, nk_used, icon_bytes;
    /* Two different questions. rss_bytes is the working set - what is resident,
     * shared driver and system pages included. private_bytes is commit, which
     * is the process's own. Every rss_at milestone below is working set. */
    unsigned long rss_bytes, private_bytes;
    int icons;
    /* The baked font atlas. atlas_bpp is read off the texture, so it says
     * what the backend actually produced rather than what it prefers. */
    int atlas_w, atlas_h, atlas_bpp;
    unsigned long rss_at[RSS_STEPS];   /* working set, one per milestone */
    unsigned long priv_at[RSS_STEPS];  /* private commit, the same milestones */
} reaktor_diag;

void reaktor_diagnostics(App *app, reaktor_diag *out);

#endif /* REAKTOR_UI_H */
