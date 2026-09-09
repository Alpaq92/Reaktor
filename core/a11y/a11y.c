/* a11y.c - see a11y.h, and docs/ACCESSIBILITY.md for where this is going.
 *
 * Two fixed arenas, front and back. The frame fills the front, the diff reads
 * both, they swap. No allocation, no strdup, no tree pointers: a node names its
 * parent by id and the array is already in draw order, which is reading order,
 * so the "tree" is a flat list that happens to be sorted correctly. */
#include <string.h>

#include "a11y.h"

static const char *const g_role_names[REAKTOR_A11Y_ROLE_COUNT] = {
    "none", "window", "group", "tablist", "tab", "button", "link",
    "checkbox", "radio", "textbox", "slider", "spinbutton", "progress",
    "combobox", "listitem", "treeitem", "menubar", "menu", "menuitem",
    "dialog", "label"
};

const char *
reaktor_a11y_role_name(unsigned char role)
{
    return role < REAKTOR_A11Y_ROLE_COUNT ? g_role_names[role] : "?";
}

/* FNV-1a. The id has to be stable across frames and cheap enough to run per
 * widget; it does not have to be cryptographic. */
static unsigned
hash_str(unsigned h, const char *s)
{
    if (!s) return h * 16777619u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* Interns into the pool and answers the stored copy, or NULL when it is full -
 * a node with no name still describes its role and bounds, which is more
 * useful than dropping it.
 *
 * `h` is the hash the caller already computed for the id, so a name is walked
 * once per frame rather than four times: it used to be hashed, strlen'd,
 * copied, and then strcmp'd again by the diff. Now the pool spans frames, so a
 * name that was here last frame is not copied at all, and - the part that
 * matters - two equal strings are one pointer, which is what lets the diff
 * compare names with == instead of strcmp. */
static const char *
intern(reaktor_a11y *a, const char *s, unsigned h)
{
    const unsigned mask = REAKTOR_A11Y_SLOTS * 2 - 1;
    unsigned i = h & mask;

    if (!s) return NULL;
    for (;;) {
        reaktor_a11y_slot *sl = &a->strings[i];

        if (sl->gen != a->pool_gen) {       /* stale means empty */
            size_t n = strlen(s) + 1;
            /* Refuse rather than probe forever. The reset at the top of the
             * next frame is what actually recovers the room; this is the
             * guard for a single frame that floods it. */
            if (a->pool_entries >= REAKTOR_A11Y_SLOTS ||
                a->pool_used + (int)n > REAKTOR_A11Y_POOL) {
                a->overflow_strings++;
                return NULL;
            }
            memcpy(a->pool + a->pool_used, s, n);
            sl->gen = a->pool_gen;
            sl->key = h;
            sl->val = a->pool_used;
            a->pool_used += (int)n;
            a->pool_entries++;
            return a->pool + sl->val;
        }
        /* The strcmp only runs on a hash hit, which is either the same string
         * or a collision - so it is one pass over a string that is about to be
         * reused, not a pass over every string. */
        if (sl->key == h && strcmp(a->pool + sl->val, s) == 0)
            return a->pool + sl->val;
        i = (i + 1) & mask;
    }
}

/* Pointer equality, because interning guarantees it: equal strings share a
 * slot, distinct strings get distinct ones even when their hashes collide. */
#define same_str(x, y) ((x) == (y))

/* Open addressing, linear probing, stamped by generation so a frame starts
 * without clearing anything. Load never passes half - the tables are twice the
 * node arena - so a probe is one or two slots. Answers the slot for `key`,
 * fresh if it was not there. Never NULL: the arena cannot fill. */
static reaktor_a11y_slot *
slot_of(reaktor_a11y_slot *tab, unsigned gen, unsigned key)
{
    unsigned i = key & (REAKTOR_A11Y_SLOTS - 1);

    for (;;) {
        reaktor_a11y_slot *sl = &tab[i];
        if (sl->gen != gen) {          /* stale means empty */
            sl->gen = gen;
            sl->key = key;
            sl->val = 0;
            return sl;
        }
        if (sl->key == key) return sl;
        i = (i + 1) & (REAKTOR_A11Y_SLOTS - 1);
    }
}

/* Lookup that does not insert. The diff asks about ids that are not there -
 * that is what "added" and "removed" mean - and inserting on a miss would let
 * two passes over 512 nodes each fill a 1024-slot table and probe forever.
 * Answers 0 for absent, which is why the index stores i + 1. */
static int
slot_get(const reaktor_a11y_slot *tab, unsigned gen, unsigned key)
{
    unsigned i = key & (REAKTOR_A11Y_SLOTS - 1);

    for (;;) {
        const reaktor_a11y_slot *sl = &tab[i];
        if (sl->gen != gen) return 0;
        if (sl->key == key) return sl->val;
        i = (i + 1) & (REAKTOR_A11Y_SLOTS - 1);
    }
}

static int
same_rect(struct nk_rect x, struct nk_rect y)
{
    return x.x == y.x && x.y == y.y && x.w == y.w && x.h == y.h;
}

static unsigned
emit(reaktor_a11y *a, unsigned char role, const char *name, const char *value,
     unsigned state, struct nk_rect bounds)
{
    int f = a->front;
    reaktor_a11y_node *n;
    reaktor_a11y_slot *sl;
    unsigned parent, id, base, nh;

    if (!a->building) return 0;
    if (a->count[f] >= REAKTOR_A11Y_MAX_NODES) {
        a->overflow_nodes++;
        return 0;
    }

    parent = a->depth > 0 ? a->parent[a->depth - 1] : 0u;

    /* Identity: parent, role and name, plus how many siblings with the same
     * three have already been emitted this frame. That last term is what lets
     * a row of unlabelled buttons keep their identities, and what stops an
     * inserted widget from renaming everything after it.
     *
     * The count used to come from scanning every node emitted so far, with a
     * strcmp each - quadratic in widgets, and the strcmp made the constant
     * large. It is a table now, keyed by the same hash the id is built from. */
    nh   = hash_str(2166136261u, name);
    /* The role mixes in as a number. It used to hash its own name, which
     * walked a string per widget for no information a small integer did not
     * already carry. */
    base = nh ^ ((unsigned)role * 0x9e3779b9u);
    base ^= parent + 0x9e3779b9u + (base << 6) + (base >> 2);

    sl = slot_of(a->bucket, a->bucket_gen, base);
    id = base ^ ((unsigned)sl->val * 0x85ebca6bu);
    sl->val++;
    if (id == 0) id = 1;   /* 0 is the window's parent, so it cannot be a node */

    /* Decorative: an icon that repeats its label, a box that is only spacing.
     * The id is still computed and the occurrence counter still advances, so
     * ids stay stable either way - but nothing is added, because a reader has
     * no use for a node with no role and no name, and every bridge would have
     * to filter it out again. */
    if (role == REAKTOR_A11Y_NONE) return id;

    if (id == a->focus_id) state |= REAKTOR_A11Y_FOCUSED;

    n = &a->node[f][a->count[f]++];
    n->id     = id;
    n->parent = parent;
    n->role   = role;
    n->level  = (unsigned char)a->depth;
    n->state  = state;
    n->name   = intern(a, name, nh);
    n->value  = intern(a, value, hash_str(2166136261u, value));
    n->keys   = NULL;
    n->bounds = bounds;
    n->num = n->lo = n->hi = n->step = 0.0f;
    return id;
}

void
reaktor_a11y_set_range(reaktor_a11y *a, unsigned id, float num, float lo,
                       float hi, float step)
{
    int f = a->front, i;

    if (!a->building || !id) return;
    /* Backwards: the caller is decorating the node it has just reported, so
     * this is the last entry or close to it. */
    for (i = a->count[f] - 1; i >= 0; i--) {
        reaktor_a11y_node *n = &a->node[f][i];

        if (n->id != id) continue;
        n->num = num; n->lo = lo; n->hi = hi; n->step = step;
        return;
    }
}

void
reaktor_a11y_set_keys(reaktor_a11y *a, unsigned id, const char *keys)
{
    int f = a->front, i;

    if (!a->building || !id) return;
    for (i = a->count[f] - 1; i >= 0; i--) {
        reaktor_a11y_node *n = &a->node[f][i];

        if (n->id != id) continue;
        n->keys = (keys && *keys)
                ? intern(a, keys, hash_str(2166136261u, keys)) : NULL;
        return;
    }
}

void
reaktor_a11y_set_bounds(reaktor_a11y *a, unsigned id, struct nk_rect r)
{
    int f = a->front, i;

    if (!a->building || !id) return;
    for (i = a->count[f] - 1; i >= 0; i--) {
        reaktor_a11y_node *n = &a->node[f][i];

        if (n->id != id) continue;
        n->bounds = r;
        return;
    }
}

void
reaktor_a11y_set_focus(reaktor_a11y *a, unsigned id)
{
    a->focus_id = id;
}

void
reaktor_a11y_begin(reaktor_a11y *a, const char *window_name,
                   struct nk_rect bounds)
{
    int f = a->front;

    a->count[f]    = 0;
    a->depth       = 0;
    a->change_count = 0;
    a->building    = 1;
    a->bucket_gen++;
    /* Generation 0 is what a zeroed struct already holds, so every slot would
     * read as occupied with key 0 and the first probe would walk a full table
     * forever. The other two tables step their generation before use and so
     * are never at 0; this one does not, so it starts here. */
    if (a->pool_gen == 0) a->pool_gen = 1;

    /* Start the pool over when it is nearly full. Everything pointing into it
     * goes with it, which is the previous frame's tree, so the next diff
     * reports the whole screen once - the price of never copying a name twice,
     * paid only by a page that churns thousands of distinct strings. Done here
     * rather than mid-frame, where nodes already emitted would be left holding
     * offsets about to be overwritten. */
    if (a->pool_used > REAKTOR_A11Y_POOL - REAKTOR_A11Y_POOL / 4 ||
        a->pool_entries >= REAKTOR_A11Y_SLOTS) {
        a->pool_used    = 0;
        a->pool_entries = 0;
        a->pool_gen++;
        a->count[0] = a->count[1] = 0;
    }

    /* The window is the root, emitted here so no caller has to remember to,
     * and pushed so everything after it has a parent. */
    reaktor_a11y_push(a, REAKTOR_A11Y_WINDOW, window_name, NULL, 0, bounds);
}

unsigned
reaktor_a11y_add(reaktor_a11y *a, unsigned char role, const char *name,
                 const char *value, unsigned state, struct nk_rect bounds)
{
    return emit(a, role, name, value, state, bounds);
}

unsigned
reaktor_a11y_push(reaktor_a11y *a, unsigned char role, const char *name,
                  const char *value, unsigned state, struct nk_rect bounds)
{
    unsigned id = emit(a, role, name, value, state, bounds);

    /* Past the limit the node is still emitted flat rather than dropped, and
     * the pop that follows is absorbed below. Nesting deeper than this means a
     * bug, not a page. */
    if (a->depth < REAKTOR_A11Y_MAX_DEPTH)
        a->parent[a->depth] = id;
    a->depth++;
    return id;
}

void
reaktor_a11y_pop(reaktor_a11y *a)
{
    if (a->depth > 0) a->depth--;
}

/* --- the diff ----------------------------------------------------------- */

static void
change(reaktor_a11y *a, unsigned char kind, unsigned id, int index)
{
    if (a->change_count >= REAKTOR_A11Y_MAX_CHANGES) return;
    a->change[a->change_count].kind  = kind;
    a->change[a->change_count].id    = id;
    a->change[a->change_count].index = index;
    a->change_count++;
}

int
reaktor_a11y_end(reaktor_a11y *a)
{
    int f = a->front, b = 1 - a->front;
    const reaktor_a11y_node *cur = a->node[f], *old = a->node[b];
    int ncur = a->count[f], nold = a->count[b];
    int i;

    if (!a->building) return 0;
    a->building = 0;
    a->depth    = 0;

    /* An id to index table for the frame just described. The ancestor walk
     * below needs it, and so does the suppression in the diff; the diff
     * builds the same table for the previous frame afterwards, on the next
     * generation. */
    a->index_gen++;
    for (i = 0; i < ncur; i++)
        slot_of(a->index, a->index_gen, cur[i].id)->val = i + 1;

    /* A page scrolls, so plenty of what was reported is out of sight. Those
     * nodes stay in the tree - a reader should be able to find them and
     * scroll to them - but they are marked, because a client that draws a
     * highlight or a magnifier needs to know the bounds are not on screen.
     *
     * Against every ancestor, not only the window. A container's bounds are
     * what it clips its children to - the page's band, a popup's rect - so a
     * node scrolled out of a list inside a popup is out of sight even though
     * it is well inside the window, which is what measuring against the root
     * alone used to miss. */
    for (i = 1; i < ncur; i++) {
        struct nk_rect b = cur[i].bounds;
        unsigned up = cur[i].parent;
        int guard = 0;

        while (guard++ <= REAKTOR_A11Y_MAX_DEPTH) {
            int k = up ? slot_get(a->index, a->index_gen, up) - 1 : 0;
            struct nk_rect v;

            if (k < 0) break;
            v = cur[k].bounds;
            if (b.x + b.w <= v.x || b.x >= v.x + v.w ||
                b.y + b.h <= v.y || b.y >= v.y + v.h) {
                a->node[f][i].state |= REAKTOR_A11Y_OFFSCREEN;
                break;
            }
            if (!up) break;              /* reached the window */
            up = cur[k].parent;
        }
    }

    /* One table build, then a linear pass. Old nodes the pass matched are
     * ticked off in `matched`, so finding the removals afterwards is a scan of
     * a byte array rather than a second table. */
    a->index_gen++;
    for (i = 0; i < nold; i++)
        slot_of(a->index, a->index_gen, old[i].id)->val = i + 1;
    if (nold > 0) memset(a->matched, 0, (size_t)nold);

    for (i = 0; i < ncur; i++) {
        int j = slot_get(a->index, a->index_gen, cur[i].id) - 1;
        if (j < 0) {
            /* Only the root of an arrival is reported. A node whose parent is
             * arriving too is part of that subtree, and a client re-reads a
             * subtree when its root appears - switching tabs used to be a
             * hundred unrelated additions where it is one. */
            if (!cur[i].parent ||
                slot_get(a->index, a->index_gen, cur[i].parent) > 0)
                change(a, REAKTOR_A11Y_ADDED, cur[i].id, i);
            continue;
        }
        a->matched[j] = 1;
        if (!same_str(cur[i].name, old[j].name) ||
            !same_str(cur[i].value, old[j].value) ||
            !same_str(cur[i].keys, old[j].keys))
            change(a, REAKTOR_A11Y_RENAMED, cur[i].id, i);
        if (cur[i].state != old[j].state)
            change(a, REAKTOR_A11Y_RESTATED, cur[i].id, i);
        else if (!same_rect(cur[i].bounds, old[j].bounds))
            /* Only when nothing more interesting happened: a client that has
             * to re-read a node for its state will re-read its bounds too. */
            change(a, REAKTOR_A11Y_MOVED, cur[i].id, i);
    }
    for (i = 0; i < nold; i++) {
        if (a->matched[i]) continue;
        /* And only the root of a departure, by the same argument: the parent
         * is in the old table, so a parent that is also going is one lookup
         * away. */
        if (old[i].parent) {
            int up = slot_get(a->index, a->index_gen, old[i].parent) - 1;
            if (up >= 0 && !a->matched[up]) continue;
        }
        change(a, REAKTOR_A11Y_REMOVED, old[i].id, i);
    }

    a->front = b;
    return a->change_count;
}

/* --- reading it back ---------------------------------------------------- */

/* After the swap in reaktor_a11y_end the frame just described is the back
 * half, so both of these read 1 - front. Between begin and end they read the
 * half being filled, which is what a caller mid-frame means. */
const reaktor_a11y_node *
reaktor_a11y_tree(const reaktor_a11y *a, int *count)
{
    int f = a->building ? a->front : 1 - a->front;
    if (count) *count = a->count[f];
    return a->node[f];
}

const reaktor_a11y_change *
reaktor_a11y_changes(const reaktor_a11y *a, int *count)
{
    if (count) *count = a->change_count;
    return a->change;
}

static void
state_text(unsigned state, char *out, size_t cap)
{
    static const char *const names[] = {
        "focused", "checked", "expanded", "selected",
        "disabled", "readonly", "offscreen", "volatile"
    };
    size_t len = 0;
    int i;

    /* Each name goes in with a leading space, so the caller prints out + 1 and
     * gets them separated with none in front. */
    out[0] = '\0';
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        size_t n;
        if (!(state & (1u << i))) continue;
        n = strlen(names[i]);
        if (len + n + 2 > cap) return;
        out[len++] = ' ';
        memcpy(out + len, names[i], n);
        len += n;
        out[len] = '\0';
    }
}

void
reaktor_a11y_dump(const reaktor_a11y *a, FILE *out)
{
    int n = 0, i;
    const reaktor_a11y_node *t = reaktor_a11y_tree(a, &n);

    for (i = 0; i < n; i++) {
        char st[128];
        int pad = t[i].level * 2;

        state_text(t[i].state, st, sizeof(st));
        fprintf(out, "%*s%s", pad, "", reaktor_a11y_role_name(t[i].role));
        if (t[i].name)  fprintf(out, " \"%s\"", t[i].name);
        if (t[i].value) fprintf(out, " = \"%s\"", t[i].value);
        if (t[i].keys)  fprintf(out, " keys=\"%s\"", t[i].keys);
        if (st[0])      fprintf(out, " [%s]", st + 1);
        fprintf(out, " %d,%d %dx%d\n",
                (int)t[i].bounds.x, (int)t[i].bounds.y,
                (int)t[i].bounds.w, (int)t[i].bounds.h);
    }
}
