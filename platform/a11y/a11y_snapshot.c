#include "a11y_snapshot.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "util.h"

typedef struct snap_node {
    unsigned      id, parent;
    unsigned char role, level;
    unsigned      state;
    int           name, value, keys;
    float         x, y, w, h;
    float         num, lo, hi, step;
    int           first, last, next, prev;
} snap_node;

typedef struct snap_slot {
    unsigned id;
    int      at;
} snap_slot;

#define SNAP_POOL  (REAKTOR_A11Y_POOL * 2)
#define SNAP_SLOTS (REAKTOR_A11Y_MAX_NODES * 2)

struct reaktor_snap {
    SDL_Mutex *lock;
    int        ready;

    snap_node  node[REAKTOR_A11Y_MAX_NODES];
    int        count, first, last;
    snap_slot  slot[SNAP_SLOTS], fresh[SNAP_SLOTS];
    char       str[SNAP_POOL];
    int        str_used;
    unsigned   focus;
    int        stale;

    unsigned   want_focus, want_activate;
    Uint32     window;

    reaktor_snap_diff diff[REAKTOR_SNAP_DIFFS];
    int               ndiff, overflow;

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
same_text(const reaktor_snap *s, int at, const char *t)
{
    if (at < 0) return !t || !*t;
    return t && strcmp(s->str + at, t) == 0;
}

static snap_slot *
slot_in(snap_slot *slot, unsigned id)
{
    unsigned i = (id * 2654435761u) & (SNAP_SLOTS - 1);

    while (slot[i].id && slot[i].id != id) i = (i + 1) & (SNAP_SLOTS - 1);
    return &slot[i];
}

static int
find(reaktor_snap *s, unsigned id)
{
    const snap_slot *at = id ? slot_in(s->slot, id) : NULL;

    return at && at->id && at->at < s->count ? at->at : -1;
}

/* Before the tree is replaced: what each node the core says changed in place
   had, against what it has now. */
static void
diff_tree(reaktor_snap *s, const reaktor_a11y *a, const reaktor_a11y_node *t, int n)
{
    const reaktor_a11y_change *c;
    int m, i, j;

    memset(s->fresh, 0, sizeof s->fresh);
    for (i = 0; i < n && i < REAKTOR_A11Y_MAX_NODES; i++) {
        snap_slot *at = t[i].id ? slot_in(s->fresh, t[i].id) : NULL;

        if (at && !at->id) { at->id = t[i].id; at->at = i; }
    }
    c = reaktor_a11y_changes(a, &m);
    for (i = 0; i < m; i++) {
        reaktor_snap_diff d;
        snap_slot *now;
        const snap_node *o;

        if (c[i].kind != REAKTOR_A11Y_RENAMED && c[i].kind != REAKTOR_A11Y_RESTATED) continue;
        if ((j = find(s, c[i].id)) < 0) continue;
        now = slot_in(s->fresh, c[i].id);
        if (!now->id || now->at < 0) continue;
        o = &s->node[j];
        j = now->at;
        now->at = -1;
        d.id      = c[i].id;
        d.flipped = o->state ^ t[j].state;
        d.value   = !same_text(s, o->value, t[j].value) || o->num != t[j].num;
        d.state   = t[j].state;
        d.num     = t[j].num;
        d.lo      = t[j].lo;
        d.hi      = t[j].hi;
        if (!d.value && !d.flipped) continue;
        if (s->ndiff == REAKTOR_SNAP_DIFFS) {
            s->overflow = 1;
            break;
        }
        s->diff[s->ndiff++] = d;
    }
}

static void
link_tree(reaktor_snap *s)
{
    int i;

    memset(s->slot, 0, sizeof s->slot);
    s->first = s->last = -1;
    for (i = 0; i < s->count; i++) {
        snap_slot *at = slot_in(s->slot, s->node[i].id);

        if (!at->id) { at->id = s->node[i].id; at->at = i; }
        s->node[i].first = s->node[i].last = s->node[i].next = -1;
    }
    for (i = 0; i < s->count; i++) {
        int  p    = s->node[i].parent ? find(s, s->node[i].parent) : -1;
        int *head = p >= 0 ? &s->node[p].first : &s->first;
        int *tail = p >= 0 ? &s->node[p].last  : &s->last;

        if (s->node[i].parent && p < 0) continue;
        s->node[i].prev = *tail;
        if (*tail >= 0) s->node[*tail].next = i;
        else            *head = i;
        *tail = i;
    }
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
    s->first        = s->last = -1;
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
    s->ndiff = s->overflow = 0;
    reaktor_a11y_changes(a, &m);
    t = reaktor_a11y_tree(a, &n);
    if (m == 0 && focus_id == s->focus && !s->stale) return 0;

    SDL_LockMutex(s->lock);
    if (!s->stale) diff_tree(s, a, t, n);
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
    link_tree(s);
    s->focus = focus_id;
    s->stale = 0;
    SDL_UnlockMutex(s->lock);
    return 1;
}

static const char *
copy_text(const reaktor_snap *s, int at, char **buf, size_t *left)
{
    size_t n;

    if (at < 0 || *left < 2) return NULL;
    /* D-Bus refuses a string that is not valid UTF-8. */
    n = reaktor_utf8_clean(*buf, s->str + at, *left);
    {
        const char *out = *buf;
        *buf  += n + 1;
        *left -= n + 1;
        return out;
    }
}

int
reaktor_snap_diffs_in(reaktor_snap *s, const reaktor_snap_diff **out)
{
    *out = s ? s->diff : NULL;
    return s ? s->ndiff : 0;
}

static void
fill(const reaktor_snap *s, int i, reaktor_snap_node *out, char *buf, size_t cap)
{
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
}

int
reaktor_snap_get_in(reaktor_snap *s, unsigned id, reaktor_snap_node *out,
                    char *buf, size_t cap)
{
    int i;

    memset(out, 0, sizeof(*out));
    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    i = find(s, id);
    if (i >= 0) fill(s, i, out, buf, cap);
    SDL_UnlockMutex(s->lock);
    return i >= 0;
}

static int
kids(const reaktor_snap *s, int first)
{
    int n = 0;

    for (; first >= 0; first = s->node[first].next) n++;
    return n;
}

static void
walk(const reaktor_snap *s, int first, reaktor_snap_node *n, char *buf,
     reaktor_snap_visit visit, void *user)
{
    int k, at = 0;

    for (k = first; k >= 0; k = s->node[k].next) {
        memset(n, 0, sizeof *n);
        fill(s, k, n, buf, REAKTOR_SNAP_TEXT);
        visit(user, n, at++, kids(s, s->node[k].first));
        walk(s, s->node[k].first, n, buf, visit, user);
    }
}

void
reaktor_snap_walk_in(reaktor_snap *s, reaktor_snap_visit visit, void *user)
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (!s || !s->ready) return;
    memset(&n, 0, sizeof n);
    SDL_LockMutex(s->lock);
    visit(user, &n, -1, kids(s, s->first));
    walk(s, s->first, &n, buf, visit, user);
    SDL_UnlockMutex(s->lock);
}

unsigned
reaktor_snap_child_in(reaktor_snap *s, unsigned parent, int last)
{
    unsigned found = 0;
    int p, k;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    p = parent ? find(s, parent) : -1;
    k = p >= 0 ? (last ? s->node[p].last : s->node[p].first)
      : parent ? -1 : (last ? s->last : s->first);
    if (k >= 0) found = s->node[k].id;
    SDL_UnlockMutex(s->lock);
    return found;
}

unsigned
reaktor_snap_sibling_in(reaktor_snap *s, unsigned id, int back)
{
    unsigned found = 0;
    int i, k;

    if (!s || !s->ready) return 0;
    SDL_LockMutex(s->lock);
    i = find(s, id);
    k = i < 0 ? -1 : back ? s->node[i].prev : s->node[i].next;
    if (k >= 0) found = s->node[k].id;
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
reaktor_snap_pressable(unsigned char role)
{
    return role == REAKTOR_A11Y_BUTTON   || role == REAKTOR_A11Y_LINK ||
           role == REAKTOR_A11Y_TAB      || role == REAKTOR_A11Y_MENUITEM ||
           role == REAKTOR_A11Y_CHECKBOX || role == REAKTOR_A11Y_RADIO ||
           role == REAKTOR_A11Y_LISTITEM;
}

int
reaktor_snap_structural_in(reaktor_snap *s, const reaktor_a11y *a)
{
    int m, i;
    const reaktor_a11y_change *c = reaktor_a11y_changes(a, &m);

    if (s && s->overflow) return 1;
    for (i = 0; i < m; i++)
        if (c[i].kind == REAKTOR_A11Y_ADDED || c[i].kind == REAKTOR_A11Y_REMOVED) return 1;
    return 0;
}

void
reaktor_a11y_platform_drain(void)
{
    reaktor_snap_drain();
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

void
reaktor_snap_skip_in(reaktor_snap *s)
{
    if (s) s->stale = 1;
}

static void
request(reaktor_snap *s, unsigned *slot, unsigned id)
{
    SDL_Event e;

    if (!s || !s->ready) return;
    SDL_zero(e);
    e.type = g_wake;
    SDL_LockMutex(s->lock);
    if (slot) *slot = id;
    e.user.windowID = s->window;
    SDL_UnlockMutex(s->lock);
    SDL_PushEvent(&e);
}

void
reaktor_snap_wake_in(reaktor_snap *s)
{
    request(s, NULL, 0);
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
int reaktor_snap_diffs(const reaktor_snap_diff **out) { return reaktor_snap_diffs_in(&g, out); }
int reaktor_snap_get(unsigned id, reaktor_snap_node *out, char *buf, size_t cap)
{ return reaktor_snap_get_in(&g, id, out, buf, cap); }
unsigned reaktor_snap_child(unsigned parent, int last)
{ return reaktor_snap_child_in(&g, parent, last); }
unsigned reaktor_snap_sibling(unsigned id, int back)
{ return reaktor_snap_sibling_in(&g, id, back); }
unsigned reaktor_snap_parent(unsigned id) { return reaktor_snap_parent_in(&g, id); }
void reaktor_snap_walk(reaktor_snap_visit visit, void *user)
{ reaktor_snap_walk_in(&g, visit, user); }
int reaktor_snap_structural(const reaktor_a11y *a)
{ return reaktor_snap_structural_in(&g, a); }
unsigned reaktor_snap_hit(float x, float y) { return reaktor_snap_hit_in(&g, x, y); }
unsigned reaktor_snap_focus(void) { return reaktor_snap_focus_in(&g); }
void reaktor_snap_request_focus(unsigned id)
{ reaktor_snap_request_focus_in(&g, id); }
void reaktor_snap_request_activate(unsigned id)
{ reaktor_snap_request_activate_in(&g, id); }
void reaktor_snap_drain(void) { reaktor_snap_drain_in(&g); }
