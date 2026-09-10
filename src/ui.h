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

/* A filled rounded rect whose corners are anti-aliased on every backend -
 * nk_fill_rect's are not, on the software one. See the definition. */
void reaktor_fill_round(App *app, struct nk_command_buffer *cv,
                        struct nk_rect b, float rounding, struct nk_color col);
struct nk_color reaktor_on(struct nk_color bg);

/* Records what the pointer is over: the cursor it wants, and whether hovering
 * it changes anything on screen. See the hot-region note in main.c. */
void reaktor_hot(App *app, struct nk_rect r, int cursor, int repaint);
/* The same, for something emitted inside a popup: it is drawn over the page,
 * so it outranks whatever it covers however the two were recorded. */
void reaktor_hot_top(App *app, struct nk_rect r, int cursor, int repaint);
/* The same, but redrawing on every move inside it rather than only on
 * crossing into it - for anything drawn at the pointer. */
void reaktor_hot_follow(App *app, struct nk_rect r, int cursor);

int reaktor_button_label(App *app, struct nk_context *ctx, const char *label);
/* A slider that draws its own track and knob. Nuklear's is a filled bar with
 * a square cursor; the stylesheet asks for a rounded rail with a round grip,
 * and a circle is the one shape the software rasteriser cannot draw. `id` is
 * the node it reports as, so the arrow keys can move it. The int-valued one
 * is in internal.h, with the rest of what only a declared widget draws
 * through. */
void reaktor_slider_bar(App *app, struct nk_context *ctx, unsigned id,
                        float *val, float lo, float hi, float step);

/* A progress bar. It draws its own geometry for the reason the slider does:
 * the stylesheet's shape is not Nuklear's, and a circle is the one thing the
 * software rasteriser cannot draw. With NK_MODIFIABLE it can be dragged like
 * a slider. The knob is in internal.h. */
void reaktor_progress_bar(App *app, struct nk_context *ctx, nk_size *cur,
                          nk_size max, int modifiable);

/* A chevron centred in `slot`, at the one size they are drawn. */
void reaktor_chevron_at(App *app, struct nk_context *ctx, struct nk_rect slot,
                        const char *name, struct nk_color col);

int reaktor_button_accent(App *app, struct nk_context *ctx, const char *label);
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

/* Whether this node was asked to activate - by Enter, or by a screen reader
 * pressing it - taken once. A widget that answers it acts on itself, which is
 * the reliable path; one that does not still gets the synthetic click the
 * shell falls back to. See the definition. */
int reaktor_focus_activated(App *app, unsigned id);

/* The chords that reach the node `id` names, as ARIA writes them - see
 * reaktor_shortcut_text in core/ui/keys.h. Announced by every bridge, bound
 * by none: the binding stays where the application declared it. */
void reaktor_note_keys(App *app, unsigned id, const char *keys);
/* Where the node `id` was placed, once the layout engine has said. */
void reaktor_note_bounds(App *app, unsigned id, struct nk_rect r);
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
