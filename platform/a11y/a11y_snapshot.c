/* a11y_snapshot.c - see a11y_snapshot.h.
 *
 * Portable: SDL for the lock and the wake, and nothing else. The platform
 * headers live next door in the bridges that include this. */
#include "a11y_snapshot.h"

#include <string.h>
#include <SDL3/SDL.h>

typedef struct snap_node {
    unsigned      id, parent;
    unsigned char role, level;
    unsigned      state;
    int           name, value, keys; /* offsets into `str`, -1 for none */
    float         x, y, w, h;
    float         num, lo, hi, step;
} snap_node;

/* Twice the model's own arena: the same strings, and the model interns while
 * this does not - two nodes with the same name cost it twice. Overflowing
 * loses a name rather than a node, which is the right way round. */
#define SNAP_POOL (REAKTOR_A11Y_POOL * 2)

static struct {
    SDL_Mutex *lock;
    int        ready;

    snap_node  node[REAKTOR_A11Y_MAX_NODES];
    int        count;
    char       str[SNAP_POOL];
    int        str_used;
    unsigned   focus;

    unsigned   want_focus, want_activate;
    Uint32     wake;

    reaktor_a11y_action activate, focus_action;
    void             *user;
} g;

static int
intern(const char *s)
{
    int at, n;

    if (!s || !*s) return -1;
    n = (int)strlen(s) + 1;
    if (g.str_used + n > SNAP_POOL) return -1;
    at = g.str_used;
    memcpy(g.str + at, s, (size_t)n);
    g.str_used += n;
    return at;
}

/* Under the lock. Linear, because a client asks about one element at a time
 * and the tree is a few hundred entries; the model's own hash exists for the
 * per-frame diff, which is quadratic without it. */
static int
find(unsigned id)
{
    int i;

    for (i = 0; i < g.count; i++)
        if (g.node[i].id == id) return i;
    return -1;
}

int
reaktor_snap_init(reaktor_a11y_action activate, reaktor_a11y_action focus,
                  void *user)
{
    g.activate     = activate;
    g.focus_action = focus;
    g.user         = user;
    g.lock         = SDL_CreateMutex();
    g.wake         = SDL_RegisterEvents(1);
    g.ready        = g.lock != NULL;
    if (!g.ready)
        SDL_Log("a11y: no mutex (%s); the platform bridge stands down",
                SDL_GetError());
    return g.ready;
}

int
reaktor_snap_update(const reaktor_a11y *a, unsigned focus_id)
{
    int n, m, i;
    const reaktor_a11y_node *t;

    if (!g.ready) return 0;
    reaktor_a11y_changes(a, &m);
    t = reaktor_a11y_tree(a, &n);
    if (m == 0 && focus_id == g.focus) return 0;

    SDL_LockMutex(g.lock);
    g.str_used = 0;
    g.count    = 0;
    for (i = 0; i < n && i < REAKTOR_A11Y_MAX_NODES; i++) {
        snap_node *s = &g.node[g.count++];

        s->id     = t[i].id;
        s->parent = t[i].parent;
        s->role   = t[i].role;
        s->level  = t[i].level;
        s->state  = t[i].state;
        s->name   = intern(t[i].name);
        s->value  = intern(t[i].value);
        s->keys   = intern(t[i].keys);
        s->x = t[i].bounds.x; s->y = t[i].bounds.y;
        s->w = t[i].bounds.w; s->h = t[i].bounds.h;
        s->num = t[i].num; s->lo = t[i].lo;
        s->hi  = t[i].hi;  s->step = t[i].step;
    }
    g.focus = focus_id;
    SDL_UnlockMutex(g.lock);
    return 1;
}

/* The one place a string leaves the arena, and it leaves by copy. */
static const char *
copy_text(int at, char **buf, size_t *left)
{
    const char *src;
    size_t n;

    if (at < 0 || *left < 2) return NULL;
    src = g.str + at;
    n = strlen(src);
    if (n > *left - 1) n = *left - 1;
    memcpy(*buf, src, n);
    (*buf)[n] = '\0';
    {
        const char *out = *buf;
        *buf  += n + 1;
        *left -= n + 1;
        return out;
    }
}

int
reaktor_snap_get(unsigned id, reaktor_snap_node *out, char *buf, size_t cap)
{
    int i, ok = 0;

    memset(out, 0, sizeof(*out));
    if (!g.ready) return 0;
    SDL_LockMutex(g.lock);
    i = find(id);
    if (i >= 0) {
        const snap_node *s = &g.node[i];
        size_t left = cap;
        char  *at   = buf;

        out->id = s->id; out->parent = s->parent;
        out->role = s->role; out->level = s->level;
        out->state = s->state;
        out->x = s->x; out->y = s->y; out->w = s->w; out->h = s->h;
        out->num = s->num; out->lo = s->lo;
        out->hi = s->hi;   out->step = s->step;
        out->name  = copy_text(s->name, &at, &left);
        out->value = copy_text(s->value, &at, &left);
        out->keys  = copy_text(s->keys, &at, &left);
        ok = 1;
    }
    SDL_UnlockMutex(g.lock);
    return ok;
}

unsigned
reaktor_snap_child(unsigned parent, int last)
{
    unsigned found = 0;
    int i;

    if (!g.ready) return 0;
    SDL_LockMutex(g.lock);
    for (i = 0; i < g.count; i++)
        if (g.node[i].parent == parent) {
            found = g.node[i].id;
            if (!last) break;
        }
    SDL_UnlockMutex(g.lock);
    return found;
}

unsigned
reaktor_snap_sibling(unsigned id, int back)
{
    unsigned parent, prev = 0, found = 0;
    int i, k, seen = 0;

    if (!g.ready) return 0;
    SDL_LockMutex(g.lock);
    i = find(id);
    if (i >= 0) {
        parent = g.node[i].parent;
        /* One pass, both directions: the node before it in the parent's run,
         * or the one after. */
        for (k = 0; k < g.count; k++) {
            if (g.node[k].parent != parent) continue;
            if (g.node[k].id == id) {
                if (back) { found = prev; break; }
                seen = 1;
                continue;
            }
            if (seen) { found = g.node[k].id; break; }
            prev = g.node[k].id;
        }
    }
    SDL_UnlockMutex(g.lock);
    return found;
}

unsigned
reaktor_snap_parent(unsigned id)
{
    unsigned found = 0;
    int i;

    if (!g.ready) return 0;
    SDL_LockMutex(g.lock);
    i = find(id);
    if (i >= 0) found = g.node[i].parent;
    SDL_UnlockMutex(g.lock);
    return found;
}

unsigned
reaktor_snap_hit(float x, float y)
{
    unsigned hit = 0;
    unsigned char best = 0;
    int i;

    if (!g.ready) return 0;
    SDL_LockMutex(g.lock);
    for (i = 0; i < g.count; i++) {
        const snap_node *n = &g.node[i];

        if (n->state & REAKTOR_A11Y_OFFSCREEN) continue;
        if (x < n->x || x >= n->x + n->w) continue;
        if (y < n->y || y >= n->y + n->h) continue;
        if (!hit || n->level >= best) { hit = n->id; best = n->level; }
    }
    SDL_UnlockMutex(g.lock);
    return hit;
}

unsigned
reaktor_snap_focus(void)
{
    unsigned id = 0;

    if (!g.ready) return 0;
    SDL_LockMutex(g.lock);
    if (g.focus && find(g.focus) >= 0) id = g.focus;
    SDL_UnlockMutex(g.lock);
    return id;
}

int
reaktor_snap_focusable(unsigned char role, unsigned state)
{
    if (state & REAKTOR_A11Y_DISABLED) return 0;
    switch (role) {
    case REAKTOR_A11Y_TAB:       case REAKTOR_A11Y_BUTTON:
    case REAKTOR_A11Y_LINK:      case REAKTOR_A11Y_CHECKBOX:
    case REAKTOR_A11Y_RADIO:     case REAKTOR_A11Y_TEXTBOX:
    case REAKTOR_A11Y_SLIDER:    case REAKTOR_A11Y_SPINBUTTON:
    case REAKTOR_A11Y_COMBOBOX:  case REAKTOR_A11Y_LISTITEM:
    case REAKTOR_A11Y_TREEITEM:  case REAKTOR_A11Y_MENUITEM:
        return 1;
    default:
        return 0;
    }
}

/* Waking the loop is the other half of recording the request: with nothing
 * else happening the app is asleep in SDL_WaitEvent, and an id nobody looks
 * at is a press that does nothing. */
static void
request(unsigned *slot, unsigned id)
{
    SDL_Event e;

    if (!g.ready) return;
    SDL_LockMutex(g.lock);
    *slot = id;
    SDL_UnlockMutex(g.lock);

    SDL_zero(e);
    e.type = g.wake;
    SDL_PushEvent(&e);
}

void reaktor_snap_request_focus(unsigned id)    { request(&g.want_focus, id); }
void reaktor_snap_request_activate(unsigned id) { request(&g.want_activate, id); }

void
reaktor_snap_drain(void)
{
    unsigned focus, activate;

    if (!g.ready) return;
    SDL_LockMutex(g.lock);
    focus    = g.want_focus;
    activate = g.want_activate;
    g.want_focus = g.want_activate = 0;
    SDL_UnlockMutex(g.lock);

    /* The same two calls a reader's move and press make on the web, which are
     * the same ones Tab and Enter make - see reader_focus in main.c. */
    if (focus && g.focus_action) g.focus_action(g.user, focus);
    if (activate && g.activate)  g.activate(g.user, activate);
}
