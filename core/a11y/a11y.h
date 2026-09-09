/* a11y.h - the retained model an immediate-mode frame does not have.
 *
 * A screen reader reads an accessibility tree and asks it questions between
 * frames: what are your children, what is your role, are you checked. Nothing
 * in Nuklear survives long enough to answer - there are no widget objects, only
 * calls that draw and return. So the frame reports itself as it is drawn, into
 * a flat array in draw order, which is also reading order. At end of frame that
 * array describes the whole screen; diffing it against the previous one gives
 * the change events a platform API wants.
 *
 * Phases 1 and 2 of docs/ACCESSIBILITY.md: the model, the diff, and every
 * widget reporting into it. No platform reads it yet - that is phase 4 - so
 * for now it is verified by reaktor_a11y_dump, which writes the tree as text.
 *
 * It runs unconditionally. Describing the busiest page costs about four
 * microseconds against a frame that takes 1.3 ms to build, which is below what
 * the app's own frame timer can even resolve, so there is nothing here worth
 * making optional. */
#ifndef REAKTOR_A11Y_H
#define REAKTOR_A11Y_H

#include "nk_common.h"
#include <stdio.h>

/* Roles, in the vocabulary every platform API shares. Deliberately short: a
 * role that cannot be mapped onto UIA, AT-SPI and ARIA alike is not worth
 * carrying. */
enum {
    REAKTOR_A11Y_NONE = 0,
    REAKTOR_A11Y_WINDOW,
    REAKTOR_A11Y_GROUP,
    REAKTOR_A11Y_TABLIST,
    REAKTOR_A11Y_TAB,
    REAKTOR_A11Y_BUTTON,
    REAKTOR_A11Y_LINK,
    REAKTOR_A11Y_CHECKBOX,
    REAKTOR_A11Y_RADIO,
    REAKTOR_A11Y_TEXTBOX,
    REAKTOR_A11Y_SLIDER,
    REAKTOR_A11Y_SPINBUTTON,
    REAKTOR_A11Y_PROGRESS,
    REAKTOR_A11Y_COMBOBOX,
    REAKTOR_A11Y_LISTITEM,
    REAKTOR_A11Y_TREEITEM,
    REAKTOR_A11Y_MENUBAR,
    REAKTOR_A11Y_MENU,
    REAKTOR_A11Y_MENUITEM,
    REAKTOR_A11Y_DIALOG,
    REAKTOR_A11Y_LABEL,
    REAKTOR_A11Y_ROLE_COUNT
};

/* State bits. Absent means the state does not apply, not that it is false -
 * a button is neither checked nor unchecked. */
enum {
    REAKTOR_A11Y_FOCUSED   = 1u << 0,
    REAKTOR_A11Y_CHECKED   = 1u << 1,
    REAKTOR_A11Y_EXPANDED  = 1u << 2,
    REAKTOR_A11Y_SELECTED  = 1u << 3,
    REAKTOR_A11Y_DISABLED  = 1u << 4,
    REAKTOR_A11Y_READONLY  = 1u << 5,
    REAKTOR_A11Y_OFFSCREEN = 1u << 6,  /* scrolled or clipped out of reach */
    /* Changes on its own, and a reader must not read the change out. The
     * Diagnostics page is a page of these: every reading moves as the pointer
     * does, and a platform that turned each into an announcement would talk
     * over everything else in the app. The node is still in the tree and
     * still readable on demand - this says only that its changing is not
     * news. ARIA spells it aria-live="off"; UIA is the reason it exists,
     * since a provider raises a property-changed event per change unless
     * something says not to. */
    REAKTOR_A11Y_VOLATILE  = 1u << 7
};

/* One element. `name` and `value` point into the frame's string arena, so they
 * are valid until the frame after next - long enough for the diff, which is
 * the only thing that reads the previous frame's copy. */
typedef struct reaktor_a11y_node {
    unsigned       id;
    unsigned       parent;
    unsigned char  role;
    unsigned char  level;      /* depth, for the dump and for tree items */
    unsigned       state;
    const char    *name;
    const char    *value;
    /* The chords that reach this node, as ARIA writes them - and as a list,
     * "Control+1 Meta+1", because an action commonly answers to more than one.
     * Announced by every bridge and bound by none of them: the key itself is
     * the application's, and this only says it exists. NULL for a node with
     * none, which is nearly all of them. */
    const char    *keys;
    struct nk_rect bounds;     /* window coordinates */
    /* A range in numbers, for a client that computes rather than reads: ARIA
     * asks for valuemin and valuemax, UIA for IRangeValueProvider, and
     * neither can get them out of the text a reader hears. `lo == hi` means
     * the node is not a range, which is every node nothing reports one for. */
    float          num, lo, hi, step;
} reaktor_a11y_node;

enum {
    REAKTOR_A11Y_ADDED = 0,
    REAKTOR_A11Y_REMOVED,
    REAKTOR_A11Y_RENAMED,        /* name, value or keys */
    REAKTOR_A11Y_RESTATED,       /* state bits */
    REAKTOR_A11Y_MOVED           /* bounds only */
};

typedef struct reaktor_a11y_change {
    unsigned char kind;
    unsigned      id;
    /* Into the tree the change is about: the new one for everything except
     * REMOVED, which points into the old. -1 when the node is gone. */
    int           index;
} reaktor_a11y_change;

/* Sized once, from measurement rather than from a round number: the busiest
 * page in the app (Buttons) reports 80 nodes and interns 77 strings taking
 * 2385 bytes, so these are roughly three times what is used. They are the
 * whole allocation - the model never calls malloc, because it runs inside the
 * frame - and overflowing any of them is counted and survived rather than
 * trapped, so the cost of being wrong is a slightly short tree.
 *
 * They were four times larger. That put the struct at 135 KB, which was enough
 * to push the CRT heap over a segment boundary and cost the process most of a
 * megabyte of commit for arenas that were 84% empty. */
#define REAKTOR_A11Y_MAX_NODES   256
#define REAKTOR_A11Y_MAX_CHANGES 256
/* One pool for both frames, so a name that survives a frame is not copied
 * again. Holds the whole app's vocabulary three times over. */
#define REAKTOR_A11Y_POOL        8192
#define REAKTOR_A11Y_MAX_DEPTH   32
/* Power of two, twice the node arena, so the open-addressed tables below never
 * pass half load and probing stays short. */
#define REAKTOR_A11Y_SLOTS       512

/* One slot of the scratch tables. Stamped with a generation rather than
 * cleared, so starting a frame costs nothing: a slot whose gen is stale is
 * empty by definition. */
typedef struct reaktor_a11y_slot {
    unsigned key;
    unsigned gen;
    int      val;
} reaktor_a11y_slot;

typedef struct reaktor_a11y {
    /* Front is the frame just described, back is the one before it. They swap
     * at reaktor_a11y_end, so the diff has both. */
    reaktor_a11y_node node[2][REAKTOR_A11Y_MAX_NODES];
    int             count[2];
    int             front;          /* which half is being filled */

    /* Interned strings, one pool across frames rather than an arena each.
     * Two equal names are therefore one pointer, which is what lets the diff
     * compare them with == instead of strcmp, and a name that was already
     * here is not copied at all. */
    char            pool[REAKTOR_A11Y_POOL];
    int             pool_used;
    int             pool_entries;   /* to keep the index below half load */
    unsigned        pool_gen;

    unsigned parent[REAKTOR_A11Y_MAX_DEPTH];
    int      depth;

    reaktor_a11y_change change[REAKTOR_A11Y_MAX_CHANGES];
    int               change_count;

    /* Both are counters rather than flags, because a page that overflows once
     * and then fits should stop reporting. */
    int overflow_nodes, overflow_strings;

    /* Scratch. `bucket` counts how many siblings share a name and role, which
     * is what makes an id stable; `index` maps id to array position for the
     * diff; `strings` maps a name's hash to its offset in the pool. All three
     * were linear scans once, which made a frame quadratic in widgets. */
    reaktor_a11y_slot bucket[REAKTOR_A11Y_SLOTS];
    reaktor_a11y_slot index[REAKTOR_A11Y_SLOTS];
    reaktor_a11y_slot strings[REAKTOR_A11Y_SLOTS * 2];
    unsigned        bucket_gen, index_gen;
    /* Which old node the forward pass matched, so the removal pass is a scan
     * of a byte array rather than a second table. */
    unsigned char   matched[REAKTOR_A11Y_MAX_NODES];

    int building;

    /* Keyboard focus, phase 3: the id the shell says has it, 0 for none.
     * Stamped REAKTOR_A11Y_FOCUSED onto that node as it is emitted, so focus
     * moving reaches the diff as two state changes like anything else. */
    unsigned focus_id;
} reaktor_a11y;

/* --- building, once per frame ------------------------------------------ */

/* Starts a frame. The window node is emitted here, so every later node has a
 * parent without the caller pushing one. */
void reaktor_a11y_begin(reaktor_a11y *a, const char *window_name,
                        struct nk_rect bounds);

/* Reports one element. `name` may be NULL for something unlabelled - a group,
 * an icon-only button whose meaning comes from its position. `value` may be
 * NULL for anything that does not hold one. Answers the node's id, which is
 * what a caller passes to reaktor_a11y_push to make it a container.
 *
 * The id is a hash of the parent's id, the role, the name and an occurrence
 * counter, so it names the same element on consecutive frames: adding a widget
 * above does not renumber everything below it. */
unsigned reaktor_a11y_add(reaktor_a11y *a, unsigned char role,
                          const char *name, const char *value, unsigned state,
                          struct nk_rect bounds);

/* The same, and then makes it the parent of everything up to the matching pop.
 * Unbalanced pushes are clamped rather than trapped: an accessibility tree that
 * is slightly wrong is better than a frame that does not draw. */
unsigned reaktor_a11y_push(reaktor_a11y *a, unsigned char role,
                           const char *name, const char *value, unsigned state,
                           struct nk_rect bounds);
void     reaktor_a11y_pop(reaktor_a11y *a);

/* Ends the frame: diffs against the previous one and swaps. Answers how many
 * changes it found, which is 0 for a frame that drew the same thing. */
int reaktor_a11y_end(reaktor_a11y *a);

/* Names the node with keyboard focus, by id, 0 for none. Takes effect on the
 * next frame built. */
void reaktor_a11y_set_focus(reaktor_a11y *a, unsigned id);

/* Puts numbers on a node already emitted this frame, named by the id its own
 * report answered with. Separate from the report because three roles want it
 * and the rest would carry four arguments they have no use for. */
void reaktor_a11y_set_range(reaktor_a11y *a, unsigned id, float num, float lo,
                            float hi, float step);

/* The keyboard chords that reach the node `id` names - same shape as the range
 * above, and for the same reason: a handful of nodes have one. NULL or empty
 * means none, so a chord that stops applying can be taken away. See
 * reaktor_shortcut_text, which is what produces the string. */
void reaktor_a11y_set_keys(reaktor_a11y *a, unsigned id, const char *keys);

/* --- reading it back --------------------------------------------------- */

const reaktor_a11y_node *reaktor_a11y_tree(const reaktor_a11y *a, int *count);
const reaktor_a11y_change *reaktor_a11y_changes(const reaktor_a11y *a,
                                                int *count);
const char *reaktor_a11y_role_name(unsigned char role);

/* The tree as text, one line per node, indented by depth. This is what the
 * golden-file test compares, so the format is deliberately dull and stable:
 *
 *     window "Reaktor" 0,0 960x680
 *       tablist 0,40 960x34
 *         tab "Login" [selected] 8,40 68x34
 */
void reaktor_a11y_dump(const reaktor_a11y *a, FILE *out);

/* --- a platform bridge (phase 4) ---------------------------------------- */

/* Called back when a reader presses a node, or moves to one. */
typedef void (*reaktor_a11y_action)(void *user, unsigned id);

/* Hands the tree to the platform after each drawn frame. On the web it is
 * mirrored into hidden DOM beside the canvas (a11y_web.c); on Windows it is
 * served to UI Automation and on the free desktops to AT-SPI (src/sys/); on
 * macOS the bridge is written and has never run. Anywhere else this is a
 * no-op. push is cheap when nothing changed: it looks at the change count and
 * returns. */
void reaktor_a11y_platform_init(reaktor_a11y_action activate,
                                reaktor_a11y_action focus, void *user);
void reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id);

/* Runs whatever a client asked for since the last call, on the thread that
 * owns the tree. A bridge whose client calls it on another thread - UIA does,
 * from an RPC thread, while this one may be asleep in SDL_WaitEvent - cannot
 * run the action there: it would read the tree the frame is rewriting. So it
 * records the request, wakes the loop with an event, and the shell calls this
 * before it builds the next frame. On the web it is a no-op; there is one
 * thread there and the request arrives on it. */
void reaktor_a11y_platform_drain(void);

#endif /* REAKTOR_A11Y_H */
