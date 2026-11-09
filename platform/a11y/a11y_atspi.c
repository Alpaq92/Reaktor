#include "a11y.h"

#ifdef REAKTOR_HAVE_ATSPI

#include "a11y_snapshot.h"

#include "gibus.h"
#include <SDL3/SDL.h>

#define A11Y_BUS_NAME   "org.a11y.Bus"
#define A11Y_BUS_PATH   "/org/a11y/bus"
#define ATSPI_ROOT_PATH "/org/a11y/atspi/accessible/root"
#define ATSPI_PREFIX    "/org/a11y/atspi/accessible/"
#define ATSPI_REGISTRY  "org.a11y.atspi.Registry"

#define IF_ACCESSIBLE   "org.a11y.atspi.Accessible"
#define IF_COMPONENT    "org.a11y.atspi.Component"
#define IF_ACTION       "org.a11y.atspi.Action"
#define IF_VALUE        "org.a11y.atspi.Value"
#define IF_APPLICATION  "org.a11y.atspi.Application"
#define IF_SOCKET       "org.a11y.atspi.Socket"
#define IF_EVENT_OBJECT "org.a11y.atspi.Event.Object"
#define IF_PROPS        "org.freedesktop.DBus.Properties"

enum {
    ATSPI_ROLE_INVALID       = 0,
    ATSPI_ROLE_APPLICATION   = 75,
    ATSPI_ROLE_CHECK_BOX     = 8,
    ATSPI_ROLE_COMBO_BOX     = 11,
    ATSPI_ROLE_DIALOG        = 16,
    ATSPI_ROLE_FILLER        = 20,
    ATSPI_ROLE_FRAME         = 22,
    ATSPI_ROLE_LABEL         = 29,
    ATSPI_ROLE_LINK          = 88,
    ATSPI_ROLE_LIST_ITEM     = 33,
    ATSPI_ROLE_MENU          = 34,
    ATSPI_ROLE_MENU_BAR      = 35,
    ATSPI_ROLE_MENU_ITEM     = 36,
    ATSPI_ROLE_PAGE_TAB      = 38,
    ATSPI_ROLE_PAGE_TAB_LIST = 39,
    ATSPI_ROLE_PANEL         = 41,
    ATSPI_ROLE_PROGRESS_BAR  = 43,
    ATSPI_ROLE_PUSH_BUTTON   = 44,
    ATSPI_ROLE_RADIO_BUTTON  = 45,
    ATSPI_ROLE_SLIDER        = 51,
    ATSPI_ROLE_SPIN_BUTTON   = 52,
    ATSPI_ROLE_TEXT          = 60,
    ATSPI_ROLE_TREE_ITEM     = 68
};

enum {
    ATSPI_STATE_CHECKED    = 5,
    ATSPI_STATE_ENABLED    = 8,
    ATSPI_STATE_EXPANDED   = 12,
    ATSPI_STATE_FOCUSABLE  = 14,
    ATSPI_STATE_FOCUSED    = 15,
    ATSPI_STATE_SELECTABLE = 24,
    ATSPI_STATE_SELECTED   = 25,
    ATSPI_STATE_SENSITIVE  = 27,
    ATSPI_STATE_SHOWING    = 28,
    ATSPI_STATE_VISIBLE    = 30
};

static struct {
    gibus_conn     *bus;
    SDL_Thread     *pump;
    int             running;
    int             ready;

    char            self[128];
    char            desktop_name[128];
    char            desktop_path[256];

    unsigned        last_focus;
} g;

static void
path_of(unsigned id, char *out, size_t cap)
{
    SDL_assert(cap >= sizeof(ATSPI_ROOT_PATH) && cap >= sizeof(ATSPI_PREFIX) + 10);
    (void)cap;
    if (!id) {
        SDL_memcpy(out, ATSPI_ROOT_PATH, sizeof(ATSPI_ROOT_PATH));
        return;
    }
    SDL_memcpy(out, ATSPI_PREFIX, sizeof(ATSPI_PREFIX) - 1);
    SDL_uitoa(id, out + sizeof(ATSPI_PREFIX) - 1, 10);
}

static long
id_of(const char *path)
{
    const char *tail;

    if (!path) return -1;
    if (SDL_strcmp(path, ATSPI_ROOT_PATH) == 0) return 0;
    if (SDL_strncmp(path, ATSPI_PREFIX, SDL_strlen(ATSPI_PREFIX)) != 0) return -1;
    tail = path + SDL_strlen(ATSPI_PREFIX);
    if (!*tail) return -1;
    return (long)SDL_strtoul(tail, NULL, 10);
}

static const struct {
    unsigned    code;
    const char *name;
} g_roles[REAKTOR_A11Y_ROLE_COUNT] = {
    [REAKTOR_A11Y_WINDOW]     = { ATSPI_ROLE_FRAME, "frame" },
    [REAKTOR_A11Y_GROUP]      = { ATSPI_ROLE_PANEL, "panel" },
    [REAKTOR_A11Y_TABLIST]    = { ATSPI_ROLE_PAGE_TAB_LIST, "page tab list" },
    [REAKTOR_A11Y_TAB]        = { ATSPI_ROLE_PAGE_TAB, "page tab" },
    [REAKTOR_A11Y_BUTTON]     = { ATSPI_ROLE_PUSH_BUTTON, "push button" },
    [REAKTOR_A11Y_LINK]       = { ATSPI_ROLE_LINK, "link" },
    [REAKTOR_A11Y_CHECKBOX]   = { ATSPI_ROLE_CHECK_BOX, "check box" },
    [REAKTOR_A11Y_RADIO]      = { ATSPI_ROLE_RADIO_BUTTON, "radio button" },
    [REAKTOR_A11Y_TEXTBOX]    = { ATSPI_ROLE_TEXT, "text" },
    [REAKTOR_A11Y_SLIDER]     = { ATSPI_ROLE_SLIDER, "slider" },
    [REAKTOR_A11Y_SPINBUTTON] = { ATSPI_ROLE_SPIN_BUTTON, "spin button" },
    [REAKTOR_A11Y_PROGRESS]   = { ATSPI_ROLE_PROGRESS_BAR, "progress bar" },
    [REAKTOR_A11Y_COMBOBOX]   = { ATSPI_ROLE_COMBO_BOX, "combo box" },
    [REAKTOR_A11Y_LISTITEM]   = { ATSPI_ROLE_LIST_ITEM, "list item" },
    [REAKTOR_A11Y_TREEITEM]   = { ATSPI_ROLE_TREE_ITEM, "tree item" },
    [REAKTOR_A11Y_MENUBAR]    = { ATSPI_ROLE_MENU_BAR, "menu bar" },
    [REAKTOR_A11Y_MENU]       = { ATSPI_ROLE_MENU, "menu" },
    [REAKTOR_A11Y_MENUITEM]   = { ATSPI_ROLE_MENU_ITEM, "menu item" },
    [REAKTOR_A11Y_DIALOG]     = { ATSPI_ROLE_DIALOG, "dialog" },
    [REAKTOR_A11Y_LABEL]      = { ATSPI_ROLE_LABEL, "label" },
};

static unsigned
role_of(unsigned char role)
{
    return role < REAKTOR_A11Y_ROLE_COUNT && g_roles[role].name ? g_roles[role].code
                                                                : ATSPI_ROLE_FILLER;
}

static const char *
role_name(unsigned char role)
{
    return role < REAKTOR_A11Y_ROLE_COUNT && g_roles[role].name ? g_roles[role].name : "filler";
}

static void
append_ref(gibus_iter *it, const char *name, const char *path)
{
    gibus_iter sub;

    gibus_iter_open(it, GIBUS_TYPE_STRUCT, NULL, &sub);
    gibus_iter_append_basic(&sub, GIBUS_TYPE_STRING, &name);
    gibus_iter_append_basic(&sub, GIBUS_TYPE_OBJECT_PATH, &path);
    gibus_iter_close(it, &sub);
}

static void
append_node_ref(gibus_iter *it, unsigned id)
{
    char path[256];
    const char *name = g.self;

    path_of(id, path, sizeof(path));
    append_ref(it, name, path);
}

static void
append_null_ref(gibus_iter *it)
{
    append_ref(it, "org.a11y.atspi.Registry", "/org/a11y/atspi/null");
}

static void
append_attribute(gibus_iter *arr, const char *key, const char *val)
{
    gibus_iter e;

    gibus_iter_open(arr, GIBUS_TYPE_DICT_ENTRY, NULL, &e);
    gibus_iter_append_basic(&e, GIBUS_TYPE_STRING, &key);
    gibus_iter_append_basic(&e, GIBUS_TYPE_STRING, &val);
    gibus_iter_close(arr, &e);
}

static void
append_variant_string(gibus_iter *it, const char *s)
{
    gibus_iter v;

    gibus_iter_open(it, GIBUS_TYPE_VARIANT, "s", &v);
    gibus_iter_append_basic(&v, GIBUS_TYPE_STRING, &s);
    gibus_iter_close(it, &v);
}

static void
append_variant_int(gibus_iter *it, int32_t n)
{
    gibus_iter v;

    gibus_iter_open(it, GIBUS_TYPE_VARIANT, "i", &v);
    gibus_iter_append_basic(&v, GIBUS_TYPE_INT32, &n);
    gibus_iter_close(it, &v);
}

static void
append_variant_double(gibus_iter *it, double d)
{
    gibus_iter v;

    gibus_iter_open(it, GIBUS_TYPE_VARIANT, "d", &v);
    gibus_iter_append_basic(&v, GIBUS_TYPE_DOUBLE, &d);
    gibus_iter_close(it, &v);
}

static void
append_variant_ref(gibus_iter *it, unsigned id)
{
    gibus_iter v;

    gibus_iter_open(it, GIBUS_TYPE_VARIANT, "(so)", &v);
    append_node_ref(&v, id);
    gibus_iter_close(it, &v);
}

static void
append_state(gibus_iter *it, const reaktor_snap_node *n)
{
    gibus_iter arr;
    uint32_t w[2];
    int i;

    w[0] = w[1] = 0;
#define SET(bit) (w[(bit) / 32] |= 1u << ((bit) % 32))
    SET(ATSPI_STATE_VISIBLE);
    if (!(n->state & REAKTOR_A11Y_OFFSCREEN)) SET(ATSPI_STATE_SHOWING);
    if (!(n->state & REAKTOR_A11Y_DISABLED)) {
        SET(ATSPI_STATE_ENABLED);
        SET(ATSPI_STATE_SENSITIVE);
    }
    if (reaktor_snap_focusable(n->role, n->state)) SET(ATSPI_STATE_FOCUSABLE);
    if (n->state & REAKTOR_A11Y_FOCUSED)  SET(ATSPI_STATE_FOCUSED);
    if (n->state & REAKTOR_A11Y_CHECKED)  SET(ATSPI_STATE_CHECKED);
    if (n->state & REAKTOR_A11Y_EXPANDED) SET(ATSPI_STATE_EXPANDED);
    if (n->role == REAKTOR_A11Y_TAB || n->role == REAKTOR_A11Y_LISTITEM) {
        SET(ATSPI_STATE_SELECTABLE);
        if (n->state & REAKTOR_A11Y_SELECTED) SET(ATSPI_STATE_SELECTED);
    }
#undef SET

    gibus_iter_open(it, GIBUS_TYPE_ARRAY, "u", &arr);
    for (i = 0; i < 2; i++)
        gibus_iter_append_basic(&arr, GIBUS_TYPE_UINT32, &w[i]);
    gibus_iter_close(it, &arr);
}

static void
append_interfaces(gibus_iter *it, const reaktor_snap_node *n, int is_root)
{
    gibus_iter arr;
    const char *s;

    gibus_iter_open(it, GIBUS_TYPE_ARRAY, "s", &arr);
    s = IF_ACCESSIBLE;
    gibus_iter_append_basic(&arr, GIBUS_TYPE_STRING, &s);
    if (is_root) {
        s = IF_APPLICATION;
        gibus_iter_append_basic(&arr, GIBUS_TYPE_STRING, &s);
    } else {
        s = IF_COMPONENT;
        gibus_iter_append_basic(&arr, GIBUS_TYPE_STRING, &s);
        if (reaktor_snap_pressable(n->role)) {
            s = IF_ACTION;
            gibus_iter_append_basic(&arr, GIBUS_TYPE_STRING, &s);
        }
        if (n->hi > n->lo) {
            s = IF_VALUE;
            gibus_iter_append_basic(&arr, GIBUS_TYPE_STRING, &s);
        }
    }
    gibus_iter_close(it, &arr);
}

static unsigned
child_at(unsigned parent, int index)
{
    unsigned id = reaktor_snap_child(parent, 0);
    int i = 0;

    while (id && i < index) {
        id = reaktor_snap_sibling(id, 0);
        i++;
    }
    return i == index ? id : 0;
}

static int
child_count(unsigned parent)
{
    unsigned id = reaktor_snap_child(parent, 0);
    int n = 0;

    while (id) {
        n++;
        id = reaktor_snap_sibling(id, 0);
    }
    return n;
}

static int
index_in_parent(unsigned id)
{
    unsigned parent = reaktor_snap_parent(id);
    unsigned it = reaktor_snap_child(parent, 0);
    int i = 0;

    while (it) {
        if (it == id) return i;
        it = reaktor_snap_sibling(it, 0);
        i++;
    }
    return -1;
}

static int
send_reply(gibus_conn *c, gibus_msg *m, gibus_msg *r)
{
    if (r) {
        gibus_send(c, r, NULL);
        gibus_msg_free(r);
    }
    (void)m;
    return GIBUS_HANDLED;
}

static int
reply_unknown(gibus_conn *c, gibus_msg *m)
{
    gibus_msg *r = gibus_msg_error(m, GIBUS_ERROR_UNKNOWN_METHOD, "no such member here");
    return send_reply(c, m, r);
}

static int
handle_properties(gibus_conn *c, gibus_msg *m, unsigned id, int is_root,
                  const reaktor_snap_node *n)
{
    const char *iface = NULL, *prop = NULL;
    gibus_msg *r;
    gibus_iter it;

    if (!gibus_msg_has_member(m, "Get")) {
        if (gibus_msg_has_member(m, "GetAll")) {
            gibus_iter arr;

            r = gibus_msg_return(m);
            if (!r) return GIBUS_HANDLED;
            gibus_iter_init_append(r, &it);
            gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "{sv}", &arr);
            gibus_iter_close(&it, &arr);
            return send_reply(c, m, r);
        }
        return reply_unknown(c, m);
    }

    if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_STRING, &iface, GIBUS_TYPE_STRING, &prop,
                            GIBUS_TYPE_INVALID))
        return reply_unknown(c, m);

    r = gibus_msg_return(m);
    if (!r) return GIBUS_HANDLED;
    gibus_iter_init_append(r, &it);

    if (SDL_strcmp(iface, IF_ACCESSIBLE) == 0) {
        if (SDL_strcmp(prop, "Name") == 0)
            append_variant_string(&it, is_root ? "Reaktor"
                                               : (n->name ? n->name : ""));
        else if (SDL_strcmp(prop, "Description") == 0)
            append_variant_string(&it, "");
        else if (SDL_strcmp(prop, "Locale") == 0)
            append_variant_string(&it, "C");
        else if (SDL_strcmp(prop, "AccessibleId") == 0) {
            char buf[24];
            SDL_snprintf(buf, sizeof(buf), "%u", id);
            append_variant_string(&it, buf);
        } else if (SDL_strcmp(prop, "ChildCount") == 0)
            append_variant_int(&it, (int32_t)child_count(id));
        else if (SDL_strcmp(prop, "Parent") == 0) {
            if (is_root) {
                gibus_iter v;
                gibus_iter_open(&it, GIBUS_TYPE_VARIANT, "(so)", &v);
                if (g.desktop_path[0])
                    append_ref(&v, g.desktop_name, g.desktop_path);
                else
                    append_null_ref(&v);
                gibus_iter_close(&it, &v);
            } else {
                append_variant_ref(&it, reaktor_snap_parent(id));
            }
        } else {
            gibus_msg_free(r);
            return reply_unknown(c, m);
        }
    } else if (SDL_strcmp(iface, IF_APPLICATION) == 0) {
        if (SDL_strcmp(prop, "ToolkitName") == 0)
            append_variant_string(&it, "Reaktor");
        else if (SDL_strcmp(prop, "Version") == 0)
            append_variant_string(&it, "1");
        else if (SDL_strcmp(prop, "AtspiVersion") == 0)
            append_variant_string(&it, "2.1");
        else if (SDL_strcmp(prop, "Id") == 0)
            append_variant_int(&it, 0);
        else {
            gibus_msg_free(r);
            return reply_unknown(c, m);
        }
    } else if (SDL_strcmp(iface, IF_VALUE) == 0) {
        if (SDL_strcmp(prop, "CurrentValue") == 0)
            append_variant_double(&it, n->num);
        else if (SDL_strcmp(prop, "MinimumValue") == 0)
            append_variant_double(&it, n->lo);
        else if (SDL_strcmp(prop, "MaximumValue") == 0)
            append_variant_double(&it, n->hi);
        else if (SDL_strcmp(prop, "MinimumIncrement") == 0)
            append_variant_double(&it, n->step);
        else {
            gibus_msg_free(r);
            return reply_unknown(c, m);
        }
    } else if (SDL_strcmp(iface, IF_ACTION) == 0) {
        if (SDL_strcmp(prop, "NActions") == 0)
            append_variant_int(&it, reaktor_snap_pressable(n->role) ? 1 : 0);
        else {
            gibus_msg_free(r);
            return reply_unknown(c, m);
        }
    } else {
        gibus_msg_free(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static int
handle_accessible(gibus_conn *c, gibus_msg *m, unsigned id, int is_root,
                  const reaktor_snap_node *n)
{
    gibus_msg *r = gibus_msg_return(m);
    gibus_iter it;

    if (!r) return GIBUS_HANDLED;
    gibus_iter_init_append(r, &it);

    if (gibus_msg_has_member(m, "GetRole")) {
        uint32_t v = is_root ? ATSPI_ROLE_APPLICATION : role_of(n->role);
        gibus_iter_append_basic(&it, GIBUS_TYPE_UINT32, &v);
    } else if (gibus_msg_has_member(m, "GetRoleName") ||
               gibus_msg_has_member(m, "GetLocalizedRoleName")) {
        const char *s = is_root ? "application" : role_name(n->role);
        gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &s);
    } else if (gibus_msg_has_member(m, "GetState")) {
        if (is_root) {
            gibus_iter arr;
            uint32_t w[2];
            int i;

            w[0] = (1u << ATSPI_STATE_VISIBLE) | (1u << ATSPI_STATE_SHOWING) |
                   (1u << ATSPI_STATE_ENABLED) | (1u << ATSPI_STATE_SENSITIVE);
            w[1] = 0;
            gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "u", &arr);
            for (i = 0; i < 2; i++)
                gibus_iter_append_basic(&arr, GIBUS_TYPE_UINT32, &w[i]);
            gibus_iter_close(&it, &arr);
        } else {
            append_state(&it, n);
        }
    } else if (gibus_msg_has_member(m, "GetChildAtIndex")) {
        int32_t idx = 0;

        gibus_msg_get_args(m, NULL, GIBUS_TYPE_INT32, &idx, GIBUS_TYPE_INVALID);
        {
            unsigned kid = child_at(id, (int)idx);
            if (kid) append_node_ref(&it, kid);
            else     append_null_ref(&it);
        }
    } else if (gibus_msg_has_member(m, "GetChildren")) {
        gibus_iter arr;
        unsigned kid = reaktor_snap_child(id, 0);

        gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "(so)", &arr);
        while (kid) {
            append_node_ref(&arr, kid);
            kid = reaktor_snap_sibling(kid, 0);
        }
        gibus_iter_close(&it, &arr);
    } else if (gibus_msg_has_member(m, "GetIndexInParent")) {
        int32_t v = is_root ? 0 : (int32_t)index_in_parent(id);
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &v);
    } else if (gibus_msg_has_member(m, "GetApplication")) {
        append_node_ref(&it, 0);
    } else if (gibus_msg_has_member(m, "GetRelationSet")) {
        gibus_iter arr;
        gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "(ua(so))", &arr);
        gibus_iter_close(&it, &arr);
    } else if (gibus_msg_has_member(m, "GetAttributes")) {
        gibus_iter arr;
        gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "{ss}", &arr);
        if (!is_root && n && n->keys)
            append_attribute(&arr, "keyshortcuts", n->keys);
        gibus_iter_close(&it, &arr);
    } else if (gibus_msg_has_member(m, "GetInterfaces")) {
        append_interfaces(&it, n, is_root);
    } else {
        gibus_msg_free(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static int
handle_component(gibus_conn *c, gibus_msg *m, unsigned id,
                 const reaktor_snap_node *n)
{
    gibus_msg *r = gibus_msg_return(m);
    gibus_iter it;

    if (!r) return GIBUS_HANDLED;
    gibus_iter_init_append(r, &it);

    if (gibus_msg_has_member(m, "GetExtents")) {
        gibus_iter sub;
        int32_t v[4];
        int i;

        v[0] = (int32_t)n->x; v[1] = (int32_t)n->y;
        v[2] = (int32_t)n->w; v[3] = (int32_t)n->h;
        gibus_iter_open(&it, GIBUS_TYPE_STRUCT, NULL, &sub);
        for (i = 0; i < 4; i++)
            gibus_iter_append_basic(&sub, GIBUS_TYPE_INT32, &v[i]);
        gibus_iter_close(&it, &sub);
    } else if (gibus_msg_has_member(m, "GetPosition")) {
        int32_t x = (int32_t)n->x, y = (int32_t)n->y;
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &x);
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &y);
    } else if (gibus_msg_has_member(m, "GetSize")) {
        int32_t w = (int32_t)n->w, h = (int32_t)n->h;
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &w);
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &h);
    } else if (gibus_msg_has_member(m, "Contains")) {
        int32_t x = 0, y = 0;
        uint32_t coord = 0;
        gibus_bool in;

        gibus_msg_get_args(m, NULL, GIBUS_TYPE_INT32, &x, GIBUS_TYPE_INT32, &y,
                           GIBUS_TYPE_UINT32, &coord, GIBUS_TYPE_INVALID);
        in = (x >= n->x && x < n->x + n->w &&
              y >= n->y && y < n->y + n->h) ? 1 : 0;
        gibus_iter_append_basic(&it, GIBUS_TYPE_BOOLEAN, &in);
    } else if (gibus_msg_has_member(m, "GetAccessibleAtPoint")) {
        int32_t x = 0, y = 0;
        uint32_t coord = 0;
        unsigned hit;

        gibus_msg_get_args(m, NULL, GIBUS_TYPE_INT32, &x, GIBUS_TYPE_INT32, &y,
                           GIBUS_TYPE_UINT32, &coord, GIBUS_TYPE_INVALID);
        hit = reaktor_snap_hit((float)x, (float)y);
        if (hit) append_node_ref(&it, hit);
        else     append_null_ref(&it);
    } else if (gibus_msg_has_member(m, "GrabFocus")) {
        gibus_bool ok = 1;

        reaktor_snap_request_focus(id);
        gibus_iter_append_basic(&it, GIBUS_TYPE_BOOLEAN, &ok);
    } else if (gibus_msg_has_member(m, "GetLayer")) {
        uint32_t layer = 3;
        gibus_iter_append_basic(&it, GIBUS_TYPE_UINT32, &layer);
    } else if (gibus_msg_has_member(m, "GetMDIZOrder")) {
        int16_t z = 0;
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT16, &z);
    } else if (gibus_msg_has_member(m, "GetAlpha")) {
        double a = 1.0;
        gibus_iter_append_basic(&it, GIBUS_TYPE_DOUBLE, &a);
    } else {
        gibus_msg_free(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static int
handle_action(gibus_conn *c, gibus_msg *m, unsigned id,
              const reaktor_snap_node *n)
{
    gibus_msg *r = gibus_msg_return(m);
    gibus_iter it;

    if (!r) return GIBUS_HANDLED;
    gibus_iter_init_append(r, &it);

    if (gibus_msg_has_member(m, "GetNActions")) {
        int32_t v = reaktor_snap_pressable(n->role) ? 1 : 0;
        gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &v);
    } else if (gibus_msg_has_member(m, "DoAction")) {
        gibus_bool ok = reaktor_snap_pressable(n->role) ? 1 : 0;

        if (ok) reaktor_snap_request_activate(id);
        gibus_iter_append_basic(&it, GIBUS_TYPE_BOOLEAN, &ok);
    } else if (gibus_msg_has_member(m, "GetName") ||
               gibus_msg_has_member(m, "GetLocalizedName")) {
        const char *s = "activate";
        gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &s);
    } else if (gibus_msg_has_member(m, "GetDescription") ||
               gibus_msg_has_member(m, "GetKeyBinding")) {
        const char *s = "";
        gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &s);
    } else if (gibus_msg_has_member(m, "GetActions")) {
        gibus_iter arr;
        gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "(sss)", &arr);
        if (reaktor_snap_pressable(n->role)) {
            gibus_iter sub;
            const char *a = "activate", *b = "", *k = "";

            gibus_iter_open(&arr, GIBUS_TYPE_STRUCT, NULL, &sub);
            gibus_iter_append_basic(&sub, GIBUS_TYPE_STRING, &a);
            gibus_iter_append_basic(&sub, GIBUS_TYPE_STRING, &b);
            gibus_iter_append_basic(&sub, GIBUS_TYPE_STRING, &k);
            gibus_iter_close(&arr, &sub);
        }
        gibus_iter_close(&it, &arr);
    } else {
        gibus_msg_free(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static int
handle_value(gibus_conn *c, gibus_msg *m)
{
    gibus_msg *r = gibus_msg_error(m, GIBUS_ERROR_NOT_SUPPORTED, "value is read-only");
    return send_reply(c, m, r);
}

static int
on_message(gibus_conn *c, gibus_msg *m, void *user)
{
    const char *iface = gibus_msg_interface(m);
    long id = id_of(gibus_msg_path(m));
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];
    int is_root;

    (void)user;
    if (id < 0 || !iface) return GIBUS_NOT_YET_HANDLED;
    is_root = id == 0;

    SDL_memset(&n, 0, sizeof(n));
    if (!is_root && !reaktor_snap_get((unsigned)id, &n, buf, sizeof(buf))) {
        gibus_msg *r = gibus_msg_error(m, GIBUS_ERROR_UNKNOWN_OBJECT, "that element is gone");
        return send_reply(c, m, r);
    }

    if (SDL_strcmp(iface, IF_PROPS) == 0)
        return handle_properties(c, m, (unsigned)id, is_root, &n);
    if (SDL_strcmp(iface, IF_ACCESSIBLE) == 0)
        return handle_accessible(c, m, (unsigned)id, is_root, &n);
    if (SDL_strcmp(iface, IF_COMPONENT) == 0 && !is_root)
        return handle_component(c, m, (unsigned)id, &n);
    if (SDL_strcmp(iface, IF_ACTION) == 0 && !is_root)
        return handle_action(c, m, (unsigned)id, &n);
    if (SDL_strcmp(iface, IF_VALUE) == 0 && !is_root)
        return handle_value(c, m);

    return GIBUS_NOT_YET_HANDLED;
}

static void
emit_event(const char *member, const char *detail, int32_t d1,
           int32_t d2, unsigned id)
{
    gibus_msg *sig;
    gibus_iter it, v;
    char path[256];
    int32_t zero = 0;

    if (!g.bus) return;
    path_of(id, path, sizeof(path));
    sig = gibus_msg_signal(path, IF_EVENT_OBJECT, member);
    if (!sig) return;

    gibus_iter_init_append(sig, &it);
    gibus_iter_append_basic(&it, GIBUS_TYPE_STRING, &detail);
    gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &d1);
    gibus_iter_append_basic(&it, GIBUS_TYPE_INT32, &d2);
    gibus_iter_open(&it, GIBUS_TYPE_VARIANT, "i", &v);
    gibus_iter_append_basic(&v, GIBUS_TYPE_INT32, &zero);
    gibus_iter_close(&it, &v);
    append_node_ref(&it, 0);

    gibus_send(g.bus, sig, NULL);
    gibus_msg_free(sig);
}

static int SDLCALL
pump(void *user)
{
    (void)user;
    while (g.running && gibus_read_write_dispatch(g.bus, -1))
        ;
    return 0;
}

static char *
a11y_bus_address(void)
{
    gibus_conn *session;
    gibus_msg *msg, *reply;
    gibus_error err;
    const char *addr = NULL;
    char *out = NULL;

    gibus_error_init(&err);
    session = gibus_session(&err);
    if (!session) { gibus_error_free(&err); return NULL; }

    msg = gibus_msg_call(A11Y_BUS_NAME, A11Y_BUS_PATH, A11Y_BUS_NAME, "GetAddress");
    if (!msg) {
        gibus_unref(session);
        return NULL;
    }
    reply = gibus_call(session, msg, 1000, &err);
    gibus_msg_free(msg);
    if (!reply) {
        gibus_error_free(&err);
        gibus_unref(session);
        return NULL;
    }
    if (gibus_msg_get_args(reply, NULL, GIBUS_TYPE_STRING, &addr, GIBUS_TYPE_INVALID) && addr)
        out = SDL_strdup(addr);
    gibus_msg_free(reply);
    gibus_unref(session);
    return out;
}

static void
embed(void)
{
    gibus_msg *msg, *reply;
    gibus_iter it, sub;
    gibus_error err;
    const char *name = NULL, *path = NULL;

    gibus_error_init(&err);
    msg = gibus_msg_call(ATSPI_REGISTRY, ATSPI_ROOT_PATH, IF_SOCKET, "Embed");
    if (!msg) return;
    gibus_iter_init_append(msg, &it);
    append_node_ref(&it, 0);

    reply = gibus_call(g.bus, msg, 2000, &err);
    gibus_msg_free(msg);
    if (!reply) {
        if (gibus_error_is_set(&err))
            SDL_Log("a11y: Embed refused (%s); the tree is not registered", err.message);
        gibus_error_free(&err);
        return;
    }
    if (gibus_iter_init(reply, &it) && gibus_iter_type(&it) == GIBUS_TYPE_STRUCT) {
        gibus_iter_recurse(&it, &sub);
        if (gibus_iter_type(&sub) == GIBUS_TYPE_STRING) {
            gibus_iter_get_basic(&sub, &name);
            gibus_iter_next(&sub);
            if (gibus_iter_type(&sub) == GIBUS_TYPE_OBJECT_PATH)
                gibus_iter_get_basic(&sub, &path);
        }
        SDL_strlcpy(g.desktop_name, name ? name : "", sizeof(g.desktop_name));
        SDL_strlcpy(g.desktop_path, path ? path : "", sizeof(g.desktop_path));
    }
    gibus_msg_free(reply);
}

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    gibus_error err;
    char *addr;
    const char *unique;

    if (!reaktor_snap_init(activate, focus, user)) return;

    addr = a11y_bus_address();
    if (!addr) {
        SDL_Log("a11y: no accessibility bus; nothing is served");
        return;
    }

    gibus_error_init(&err);
    g.bus = gibus_open(addr, &err);
    SDL_free(addr);
    gibus_error_free(&err);
    if (!g.bus) return;

    unique = gibus_unique_name(g.bus);
    SDL_strlcpy(g.self, unique ? unique : "", sizeof(g.self));

    if (!gibus_register_fallback(g.bus, "/org/a11y/atspi/accessible", on_message, NULL)) {
        gibus_unref(g.bus);
        g.bus = NULL;
        return;
    }

    embed();

    g.running = 1;
    g.ready   = 1;
    g.pump    = SDL_CreateThread(pump, "reaktor-atspi", NULL);
    if (!g.pump) {
        g.running = 0;
        g.ready   = 0;
        SDL_Log("a11y: no pump thread (%s); nothing is served", SDL_GetError());
    }
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    if (!reaktor_snap_update(a, focus_id)) return;
    if (!g.ready) return;

    if (reaktor_snap_structural(a)) emit_event("ChildrenChanged", "add", 0, 0, 0);

    if (focus_id != g.last_focus) {
        if (g.last_focus)
            emit_event("StateChanged", "focused", 0, 0, g.last_focus);
        if (focus_id)
            emit_event("StateChanged", "focused", 1, 0, focus_id);
        g.last_focus = focus_id;
    }
}

#endif
