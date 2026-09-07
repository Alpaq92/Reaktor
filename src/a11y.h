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
 * for now it is verified by curie_a11y_dump, which writes the tree as text.
 *
 * It runs unconditionally. Describing the busiest page costs about four
 * microseconds against a frame that takes 1.3 ms to build, which is below what
 * the app's own frame timer can even resolve, so there is nothing here worth
 * making optional. */
#ifndef CURIE_A11Y_H
#define CURIE_A11Y_H

#include "nk_common.h"
#include <stdio.h>

/* Roles, in the vocabulary every platform API shares. Deliberately short: a
 * role that cannot be mapped onto UIA, AT-SPI and ARIA alike is not worth
 * carrying. */
enum {
    CURIE_A11Y_NONE = 0,
    CURIE_A11Y_WINDOW,
    CURIE_A11Y_GROUP,
    CURIE_A11Y_TABLIST,
    CURIE_A11Y_TAB,
    CURIE_A11Y_BUTTON,
    CURIE_A11Y_LINK,
    CURIE_A11Y_CHECKBOX,
    CURIE_A11Y_RADIO,
    CURIE_A11Y_TEXTBOX,
    CURIE_A11Y_SLIDER,
    CURIE_A11Y_SPINBUTTON,
    CURIE_A11Y_PROGRESS,
    CURIE_A11Y_COMBOBOX,
    CURIE_A11Y_LISTITEM,
    CURIE_A11Y_TREEITEM,
    CURIE_A11Y_MENUBAR,
    CURIE_A11Y_MENU,
    CURIE_A11Y_MENUITEM,
    CURIE_A11Y_DIALOG,
    CURIE_A11Y_LABEL,
    CURIE_A11Y_ROLE_COUNT
};

/* State bits. Absent means the state does not apply, not that it is false -
 * a button is neither checked nor unchecked. */
enum {
    CURIE_A11Y_FOCUSED   = 1u << 0,
    CURIE_A11Y_CHECKED   = 1u << 1,
    CURIE_A11Y_EXPANDED  = 1u << 2,
    CURIE_A11Y_SELECTED  = 1u << 3,
    CURIE_A11Y_DISABLED  = 1u << 4,
    CURIE_A11Y_READONLY  = 1u << 5,
    CURIE_A11Y_OFFSCREEN = 1u << 6   /* scrolled or clipped out of reach */
};

/* One element. `name` and `value` point into the frame's string arena, so they
 * are valid until the frame after next - long enough for the diff, which is
 * the only thing that reads the previous frame's copy. */
typedef struct curie_a11y_node {
    unsigned       id;
    unsigned       parent;
    unsigned char  role;
    unsigned char  level;      /* depth, for the dump and for tree items */
    unsigned       state;
    const char    *name;
    const char    *value;
    struct nk_rect bounds;     /* window coordinates */
} curie_a11y_node;

enum {
    CURIE_A11Y_ADDED = 0,
    CURIE_A11Y_REMOVED,
    CURIE_A11Y_RENAMED,        /* name or value */
    CURIE_A11Y_RESTATED,       /* state bits */
    CURIE_A11Y_MOVED           /* bounds only */
};

typedef struct curie_a11y_change {
    unsigned char kind;
    unsigned      id;
    /* Into the tree the change is about: the new one for everything except
     * REMOVED, which points into the old. -1 when the node is gone. */
    int           index;
} curie_a11y_change;

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
#define CURIE_A11Y_MAX_NODES   256
#define CURIE_A11Y_MAX_CHANGES 256
/* One pool for both frames, so a name that survives a frame is not copied
 * again. Holds the whole app's vocabulary three times over. */
#define CURIE_A11Y_POOL        8192
#define CURIE_A11Y_MAX_DEPTH   32
/* Power of two, twice the node arena, so the open-addressed tables below never
 * pass half load and probing stays short. */
#define CURIE_A11Y_SLOTS       512

/* One slot of the scratch tables. Stamped with a generation rather than
 * cleared, so starting a frame costs nothing: a slot whose gen is stale is
 * empty by definition. */
typedef struct curie_a11y_slot {
    unsigned key;
    unsigned gen;
    int      val;
} curie_a11y_slot;

typedef struct curie_a11y {
    /* Front is the frame just described, back is the one before it. They swap
     * at curie_a11y_end, so the diff has both. */
    curie_a11y_node node[2][CURIE_A11Y_MAX_NODES];
    int             count[2];
    int             front;          /* which half is being filled */

    /* Interned strings, one pool across frames rather than an arena each.
     * Two equal names are therefore one pointer, which is what lets the diff
     * compare them with == instead of strcmp, and a name that was already
     * here is not copied at all. */
    char            pool[CURIE_A11Y_POOL];
    int             pool_used;
    int             pool_entries;   /* to keep the index below half load */
    unsigned        pool_gen;

    unsigned parent[CURIE_A11Y_MAX_DEPTH];
    int      depth;

    curie_a11y_change change[CURIE_A11Y_MAX_CHANGES];
    int               change_count;

    /* Both are counters rather than flags, because a page that overflows once
     * and then fits should stop reporting. */
    int overflow_nodes, overflow_strings;

    /* Scratch. `bucket` counts how many siblings share a name and role, which
     * is what makes an id stable; `index` maps id to array position for the
     * diff; `strings` maps a name's hash to its offset in the pool. All three
     * were linear scans once, which made a frame quadratic in widgets. */
    curie_a11y_slot bucket[CURIE_A11Y_SLOTS];
    curie_a11y_slot index[CURIE_A11Y_SLOTS];
    curie_a11y_slot strings[CURIE_A11Y_SLOTS * 2];
    unsigned        bucket_gen, index_gen;
    /* Which old node the forward pass matched, so the removal pass is a scan
     * of a byte array rather than a second table. */
    unsigned char   matched[CURIE_A11Y_MAX_NODES];

    int building;

    /* Keyboard focus, phase 3: the id the shell says has it, 0 for none.
     * Stamped CURIE_A11Y_FOCUSED onto that node as it is emitted, so focus
     * moving reaches the diff as two state changes like anything else. */
    unsigned focus_id;
} curie_a11y;

/* --- building, once per frame ------------------------------------------ */

/* Starts a frame. The window node is emitted here, so every later node has a
 * parent without the caller pushing one. */
void curie_a11y_begin(curie_a11y *a, const char *window_name,
                      struct nk_rect bounds);

/* Reports one element. `name` may be NULL for something unlabelled - a group,
 * an icon-only button whose meaning comes from its position. `value` may be
 * NULL for anything that does not hold one. Answers the node's id, which is
 * what a caller passes to curie_a11y_push to make it a container.
 *
 * The id is a hash of the parent's id, the role, the name and an occurrence
 * counter, so it names the same element on consecutive frames: adding a widget
 * above does not renumber everything below it. */
unsigned curie_a11y_add(curie_a11y *a, unsigned char role, const char *name,
                        const char *value, unsigned state,
                        struct nk_rect bounds);

/* The same, and then makes it the parent of everything up to the matching pop.
 * Unbalanced pushes are clamped rather than trapped: an accessibility tree that
 * is slightly wrong is better than a frame that does not draw. */
unsigned curie_a11y_push(curie_a11y *a, unsigned char role, const char *name,
                         const char *value, unsigned state,
                         struct nk_rect bounds);
void     curie_a11y_pop(curie_a11y *a);

/* Ends the frame: diffs against the previous one and swaps. Answers how many
 * changes it found, which is 0 for a frame that drew the same thing. */
int curie_a11y_end(curie_a11y *a);

/* Names the node with keyboard focus, by id, 0 for none. Takes effect on the
 * next frame built. */
void curie_a11y_set_focus(curie_a11y *a, unsigned id);

/* --- reading it back --------------------------------------------------- */

const curie_a11y_node *curie_a11y_tree(const curie_a11y *a, int *count);
const curie_a11y_change *curie_a11y_changes(const curie_a11y *a, int *count);
const char *curie_a11y_role_name(unsigned char role);

/* The tree as text, one line per node, indented by depth. This is what the
 * golden-file test compares, so the format is deliberately dull and stable:
 *
 *     window "Curie" 0,0 960x680
 *       tablist 0,40 960x34
 *         tab "Login" [selected] 8,40 68x34
 */
void curie_a11y_dump(const curie_a11y *a, FILE *out);

/* --- a platform bridge (phase 4) ---------------------------------------- */

/* Called back when a reader presses a node, or moves to one. */
typedef void (*curie_a11y_action)(void *user, unsigned id);

/* Hands the tree to the platform after each drawn frame. On the web it is
 * mirrored into hidden DOM beside the canvas (a11y_web.c); elsewhere it is a
 * no-op until phases 4b-d. push is cheap when nothing changed: it looks at the
 * change count and returns. */
void curie_a11y_platform_init(curie_a11y_action activate,
                              curie_a11y_action focus, void *user);
void curie_a11y_platform_push(const curie_a11y *a, unsigned focus_id);

#endif /* CURIE_A11Y_H */
