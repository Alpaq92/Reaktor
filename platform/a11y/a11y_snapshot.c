#include "a11y_snapshot.h"

#include <string.h>
#include <SDL3/SDL.h>

typedef struct snap_node {
    unsigned      id, parent;
    unsigned char role, level;
    unsigned      state;
    int           name, value, keys;
    float         x, y, w, h;
    float         num, lo, hi, step;
} snap_node;

#define SNAP_POOL (REAKTOR_A11Y_POOL * 2)

struct reaktor_snap {
    SDL_Mutex *lock;
    int        ready;

    snap_node  node[REAKTOR_A11Y_MAX_NODES];
    int        count;
    char       str[SNAP_POOL];
    int        str_used;
    unsigned   focus;

    unsigned   want_focus, want_activate;
    Uint32     window;

    reaktor_a11y_action activate, focus_action;
    void             *user;
};

static reaktor_snap g;
static Uint32       g_wake;

static int
intern(reaktor_snap *s, const char *t)
{
    int at, n;

    if (!t || !*t) return -1;
    n = (int)strlen(t) + 1;
    if (s->str_used + n > SNAP_POOL) return -1;
    at = s->str_used;
    memcpy(s->str + at, t, (size_t)n);
    s->str_used += n;
    return at;
}

static int
find(const reaktor_snap *s, unsigned id)
{
    int i;

    for (i = 0; i < s->count; i++)
        if (s->node[i].id == id) return i;
    return -1;
}

static int
snap_start(reaktor_snap *s, reaktor_a11y_action activate,
           reaktor_a11y_action focus, void *user, Uint32 window)
{
    s->activate     = activate;
    s->focus_action = focus;
    s->user         = user;
    s->window       = window;
    s->lock         = SDL_CreateMutex();
    s->ready        = s->lock != NULL;
    return s->ready;
}

int
reaktor_snap_init(reaktor_a11y_action activate, reaktor_a11y_action focus,
                  void *user)
{
    g_wake = SDL_RegisterEvents(1);
    if (!snap_start(&g, activate, focus, user, 0))
        SDL_Log("a11y: no mutex (%s); the platform bridge stands down",
                SDL_GetError());
    return g.ready;
}

reaktor_snap *
reaktor_snap_main(void)
{
    return &g;
}

reaktor_snap *
reaktor_snap_new(reaktor_a11y_action activate, reaktor_a11y_action focus,
                 void *user, Uint32 window)
{
    reaktor_snap *s = (reaktor_snap *)SDL_calloc(1, sizeof *s);

    if (s && !snap_start(s, activate, focus, user, window)) {
        SDL_free(s);
        s = NULL;
    }
    return s;
}

void
reaktor_snap_bind(reaktor_snap *s, reaktor_a11y_action activate,
                  reaktor_a11y_action focus, void *user, Uint32 window)
{
    if (!s || !s->ready) return;
    SDL_LockMutex(s->lock);
    s->activate     = activate;
    s->focus_action = focus;
    s->user         = user;
    s->window       = window;
    s->count        = 0;
    s->str_used     = 0;
    s->focus        = s->want_focus = s->want_activate = 0;
    SDL_UnlockMutex(s->lock);
}

int
reaktor_snap_update_in(reaktor_snap *s, const reaktor_a11y *a, unsigned focus_id)
{
    int n, m, i;
    const reaktor_a11y_node *t;

    if (!s || !s->ready) return 0;
    reaktor_a11y_changes(a, &m);
    t = reaktor_a11y_tree(a, &n);
    if (m == 0 && focus_id == s->focus) return 0;

    SDL_LockMutex(s->lock);
    s->str_used = 0;
    s->count    = 0;
    for (i = 0; i < n && i < REAKTOR_A11Y_MAX_NODES; i++) {
        snap_node *d = &s->node[s->count++];

        d->id     = t[i].id;
        d->parent = t[i].parent;
        d->role   = t[i].role;
        d->level  = t[i].level;
        d->state  = t[i].state;
        d->name   = intern(s, t[i].name);
        d->value  = intern(s, t[i].value);
        d->keys   = intern(s, t[i].keys);
        d->x = t[i].bounds.x; d->y = t[i].bounds.y;
        d->w = t[i].bounds.w; d->h = t[i].bounds.h;
        d->num = t[i].num; d->lo = t[i].lo;
        d->hi  = t[i].hi;  d->step = t[i].step;
    }
    s->focus = focus_id;
    SDL_UnlockMutex(s->lock);
    return 1;
}

static const char *
copy_text(const reaktor_snap *s, int at, char **buf, size_t *left)
{
    size_t n;

    if (at < 0 || *left < 2) return NULL;
    /* libdbus aborts on invalid UTF-8. */
    n = SDL_utf8strlcpy(*buf, s->str + at, *left);
    {
        const char *out = *buf;
        *buf  += n + 1;
        *left -= n + 1;
        return out;
    }
}

int
reaktor_snap_get_in(reaktor_snap *s, unsigned id, reaktor_snap_node *out,
                    char *buf, size_t cap)
{
    int i, ok = 0;

    memset(out, 0, sizeof(*out));
    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    i = find(s, id);
    if (i >= 0) {
        const snap_node *d = &s->node[i];
        size_t left = cap;
        char  *at   = buf;

        out->id = d->id; out->parent = d->parent;
        out->role = d->role; out->level = d->level;
        out->state = d->state;
        out->x = d->x; out->y = d->y; out->w = d->w; out->h = d->h;
        out->num = d->num; out->lo = d->lo;
        out->hi = d->hi;   out->step = d->step;
        out->name  = copy_text(s, d->name, &at, &left);
        out->value = copy_text(s, d->value, &at, &left);
        out->keys  = copy_text(s, d->keys, &at, &left);
        ok = 1;
    }
    SDL_UnlockMutex(s->lock);
    return ok;
}

unsigned
reaktor_snap_child_in(reaktor_snap *s, unsigned parent, int last)
{
    unsigned found = 0;
    int i;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    for (i = 0; i < s->count; i++)
        if (s->node[i].parent == parent) {
            found = s->node[i].id;
            if (!last) break;
        }
    SDL_UnlockMutex(s->lock);
    return found;
}

unsigned
reaktor_snap_sibling_in(reaktor_snap *s, unsigned id, int back)
{
    unsigned parent, prev = 0, found = 0;
    int i, k, seen = 0;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    i = find(s, id);
    if (i >= 0) {
        parent = s->node[i].parent;
        for (k = 0; k < s->count; k++) {
            if (s->node[k].parent != parent) continue;
            if (s->node[k].id == id) {
                if (back) { found = prev; break; }
                seen = 1;
                continue;
            }
            if (seen) { found = s->node[k].id; break; }
            prev = s->node[k].id;
        }
    }
    SDL_UnlockMutex(s->lock);
    return found;
}

unsigned
reaktor_snap_parent_in(reaktor_snap *s, unsigned id)
{
    unsigned found = 0;
    int i;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    i = find(s, id);
    if (i >= 0) found = s->node[i].parent;
    SDL_UnlockMutex(s->lock);
    return found;
}

unsigned
reaktor_snap_hit_in(reaktor_snap *s, float x, float y)
{
    unsigned hit = 0;
    unsigned char best = 0;
    int i;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    for (i = 0; i < s->count; i++) {
        const snap_node *n = &s->node[i];

        if (n->state & REAKTOR_A11Y_OFFSCREEN) continue;
        if (x < n->x || x >= n->x + n->w) continue;
        if (y < n->y || y >= n->y + n->h) continue;
        if (!hit || n->level >= best) { hit = n->id; best = n->level; }
    }
    SDL_UnlockMutex(s->lock);
    return hit;
}

unsigned
reaktor_snap_focus_in(reaktor_snap *s)
{
    unsigned id = 0;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    if (s->focus && find(s, s->focus) >= 0) id = s->focus;
    SDL_UnlockMutex(s->lock);
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

static void
request(reaktor_snap *s, unsigned *slot, unsigned id)
{
    SDL_Event e;

    if (!s || !s->ready) return;
    SDL_zero(e);
    e.type = g_wake;
    SDL_LockMutex(s->lock);
    *slot = id;
    e.user.windowID = s->window;
    SDL_UnlockMutex(s->lock);
    SDL_PushEvent(&e);
}

void
reaktor_snap_request_focus_in(reaktor_snap *s, unsigned id)
{
    if (s) request(s, &s->want_focus, id);
}

void
reaktor_snap_request_activate_in(reaktor_snap *s, unsigned id)
{
    if (s) request(s, &s->want_activate, id);
}

void
reaktor_snap_drain_in(reaktor_snap *s)
{
    unsigned focus, activate;

    if (!s || !s->ready) return;
    SDL_LockMutex(s->lock);
    focus    = s->want_focus;
    activate = s->want_activate;
    s->want_focus = s->want_activate = 0;
    SDL_UnlockMutex(s->lock);

    if (focus && s->focus_action) s->focus_action(s->user, focus);
    if (activate && s->activate)  s->activate(s->user, activate);
}

int reaktor_snap_update(const reaktor_a11y *a, unsigned focus_id)
{ return reaktor_snap_update_in(&g, a, focus_id); }
int reaktor_snap_get(unsigned id, reaktor_snap_node *out, char *buf, size_t cap)
{ return reaktor_snap_get_in(&g, id, out, buf, cap); }
unsigned reaktor_snap_child(unsigned parent, int last)
{ return reaktor_snap_child_in(&g, parent, last); }
unsigned reaktor_snap_sibling(unsigned id, int back)
{ return reaktor_snap_sibling_in(&g, id, back); }
unsigned reaktor_snap_parent(unsigned id) { return reaktor_snap_parent_in(&g, id); }
unsigned reaktor_snap_hit(float x, float y) { return reaktor_snap_hit_in(&g, x, y); }
unsigned reaktor_snap_focus(void) { return reaktor_snap_focus_in(&g); }
void reaktor_snap_request_focus(unsigned id)
{ reaktor_snap_request_focus_in(&g, id); }
void reaktor_snap_request_activate(unsigned id)
{ reaktor_snap_request_activate_in(&g, id); }
void reaktor_snap_drain(void) { reaktor_snap_drain_in(&g); }

#ifndef _WIN32
void
reaktor_a11y_platform_window_push(struct SDL_Window *win, const reaktor_a11y *a,
                                  unsigned focus_id, reaktor_a11y_action activate,
                                  reaktor_a11y_action focus, void *user)
{
    (void)win; (void)a; (void)focus_id; (void)activate; (void)focus; (void)user;
}

void
reaktor_a11y_platform_window_drain(struct SDL_Window *win)
{
    (void)win;
}

void
reaktor_a11y_platform_window_gone(struct SDL_Window *win)
{
    (void)win;
}
#endif
