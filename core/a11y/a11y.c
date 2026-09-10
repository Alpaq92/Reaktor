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

static const char *
intern(reaktor_a11y *a, const char *s, unsigned h)
{
    const unsigned mask = REAKTOR_A11Y_SLOTS * 2 - 1;
    unsigned i = h & mask;

    if (!s) return NULL;
    for (;;) {
        reaktor_a11y_slot *sl = &a->strings[i];

        if (sl->gen != a->pool_gen) {
            size_t n = strlen(s) + 1;
            if (a->pool_entries >= REAKTOR_A11Y_SLOTS ||
                a->pool_used + (int)n > REAKTOR_A11Y_POOL) {
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
        if (sl->key == h && strcmp(a->pool + sl->val, s) == 0)
            return a->pool + sl->val;
        i = (i + 1) & mask;
    }
}

#define same_str(x, y) ((x) == (y))

static reaktor_a11y_slot *
slot_of(reaktor_a11y_slot *tab, unsigned gen, unsigned key)
{
    unsigned i = key & (REAKTOR_A11Y_SLOTS - 1);

    for (;;) {
        reaktor_a11y_slot *sl = &tab[i];
        if (sl->gen != gen) {
            sl->gen = gen;
            sl->key = key;
            sl->val = 0;
            return sl;
        }
        if (sl->key == key) return sl;
        i = (i + 1) & (REAKTOR_A11Y_SLOTS - 1);
    }
}

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

    nh   = hash_str(2166136261u, name);
    base = nh ^ ((unsigned)role * 0x9e3779b9u);
    base ^= parent + 0x9e3779b9u + (base << 6) + (base >> 2);

    sl = slot_of(a->bucket, a->bucket_gen, base);
    id = base ^ ((unsigned)sl->val * 0x85ebca6bu);
    sl->val++;
    if (id == 0) id = 1;

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
    if (a->pool_gen == 0) a->pool_gen = 1;

    if (a->pool_used > REAKTOR_A11Y_POOL - REAKTOR_A11Y_POOL / 4 ||
        a->pool_entries >= REAKTOR_A11Y_SLOTS) {
        a->pool_used    = 0;
        a->pool_entries = 0;
        a->pool_gen++;
        a->count[0] = a->count[1] = 0;
    }

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

    a->index_gen++;
    for (i = 0; i < ncur; i++)
        slot_of(a->index, a->index_gen, cur[i].id)->val = i + 1;

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
            if (!up) break;
            up = cur[k].parent;
        }
    }

    a->index_gen++;
    for (i = 0; i < nold; i++)
        slot_of(a->index, a->index_gen, old[i].id)->val = i + 1;
    if (nold > 0) memset(a->matched, 0, (size_t)nold);

    for (i = 0; i < ncur; i++) {
        int j = slot_get(a->index, a->index_gen, cur[i].id) - 1;
        if (j < 0) {
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
            change(a, REAKTOR_A11Y_MOVED, cur[i].id, i);
    }
    for (i = 0; i < nold; i++) {
        if (a->matched[i]) continue;
        if (old[i].parent) {
            int up = slot_get(a->index, a->index_gen, old[i].parent) - 1;
            if (up >= 0 && !a->matched[up]) continue;
        }
        change(a, REAKTOR_A11Y_REMOVED, old[i].id, i);
    }

    a->front = b;
    return a->change_count;
}

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
