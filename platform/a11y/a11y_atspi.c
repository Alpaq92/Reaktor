#include "a11y.h"

#ifdef REAKTOR_HAVE_ATSPI

#include "a11y_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus.h>
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
    DBusConnection *bus;
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
    if (!id) snprintf(out, cap, "%s", ATSPI_ROOT_PATH);
    else     snprintf(out, cap, "%s%u", ATSPI_PREFIX, id);
}

static long
id_of(const char *path)
{
    const char *tail;

    if (!path) return -1;
    if (strcmp(path, ATSPI_ROOT_PATH) == 0) return 0;
    if (strncmp(path, ATSPI_PREFIX, strlen(ATSPI_PREFIX)) != 0) return -1;
    tail = path + strlen(ATSPI_PREFIX);
    if (!*tail) return -1;
    return (long)strtoul(tail, NULL, 10);
}

static unsigned
role_of(unsigned char role)
{
    switch (role) {
    case REAKTOR_A11Y_WINDOW:     return ATSPI_ROLE_FRAME;
    case REAKTOR_A11Y_GROUP:      return ATSPI_ROLE_PANEL;
    case REAKTOR_A11Y_TABLIST:    return ATSPI_ROLE_PAGE_TAB_LIST;
    case REAKTOR_A11Y_TAB:        return ATSPI_ROLE_PAGE_TAB;
    case REAKTOR_A11Y_BUTTON:     return ATSPI_ROLE_PUSH_BUTTON;
    case REAKTOR_A11Y_LINK:       return ATSPI_ROLE_LINK;
    case REAKTOR_A11Y_CHECKBOX:   return ATSPI_ROLE_CHECK_BOX;
    case REAKTOR_A11Y_RADIO:      return ATSPI_ROLE_RADIO_BUTTON;
    case REAKTOR_A11Y_TEXTBOX:    return ATSPI_ROLE_TEXT;
    case REAKTOR_A11Y_SLIDER:     return ATSPI_ROLE_SLIDER;
    case REAKTOR_A11Y_SPINBUTTON: return ATSPI_ROLE_SPIN_BUTTON;
    case REAKTOR_A11Y_PROGRESS:   return ATSPI_ROLE_PROGRESS_BAR;
    case REAKTOR_A11Y_COMBOBOX:   return ATSPI_ROLE_COMBO_BOX;
    case REAKTOR_A11Y_LISTITEM:   return ATSPI_ROLE_LIST_ITEM;
    case REAKTOR_A11Y_TREEITEM:   return ATSPI_ROLE_TREE_ITEM;
    case REAKTOR_A11Y_MENUBAR:    return ATSPI_ROLE_MENU_BAR;
    case REAKTOR_A11Y_MENU:       return ATSPI_ROLE_MENU;
    case REAKTOR_A11Y_MENUITEM:   return ATSPI_ROLE_MENU_ITEM;
    case REAKTOR_A11Y_DIALOG:     return ATSPI_ROLE_DIALOG;
    case REAKTOR_A11Y_LABEL:      return ATSPI_ROLE_LABEL;
    default:                    return ATSPI_ROLE_FILLER;
    }
}

static const char *
role_name(unsigned char role)
{
    switch (role) {
    case REAKTOR_A11Y_WINDOW:     return "frame";
    case REAKTOR_A11Y_GROUP:      return "panel";
    case REAKTOR_A11Y_TABLIST:    return "page tab list";
    case REAKTOR_A11Y_TAB:        return "page tab";
    case REAKTOR_A11Y_BUTTON:     return "push button";
    case REAKTOR_A11Y_LINK:       return "link";
    case REAKTOR_A11Y_CHECKBOX:   return "check box";
    case REAKTOR_A11Y_RADIO:      return "radio button";
    case REAKTOR_A11Y_TEXTBOX:    return "text";
    case REAKTOR_A11Y_SLIDER:     return "slider";
    case REAKTOR_A11Y_SPINBUTTON: return "spin button";
    case REAKTOR_A11Y_PROGRESS:   return "progress bar";
    case REAKTOR_A11Y_COMBOBOX:   return "combo box";
    case REAKTOR_A11Y_LISTITEM:   return "list item";
    case REAKTOR_A11Y_TREEITEM:   return "tree item";
    case REAKTOR_A11Y_MENUBAR:    return "menu bar";
    case REAKTOR_A11Y_MENU:       return "menu";
    case REAKTOR_A11Y_MENUITEM:   return "menu item";
    case REAKTOR_A11Y_DIALOG:     return "dialog";
    case REAKTOR_A11Y_LABEL:      return "label";
    default:                    return "filler";
    }
}

static int
has_action(unsigned char role)
{
    return role == REAKTOR_A11Y_BUTTON || role == REAKTOR_A11Y_LINK ||
           role == REAKTOR_A11Y_TAB    || role == REAKTOR_A11Y_MENUITEM ||
           role == REAKTOR_A11Y_CHECKBOX || role == REAKTOR_A11Y_RADIO ||
           role == REAKTOR_A11Y_LISTITEM;
}

static void
append_ref(DBusMessageIter *it, const char *name, const char *path)
{
    DBusMessageIter sub;

    dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &sub);
    dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &name);
    dbus_message_iter_append_basic(&sub, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_close_container(it, &sub);
}

static void
append_node_ref(DBusMessageIter *it, unsigned id)
{
    char path[256];
    const char *name = g.self;

    path_of(id, path, sizeof(path));
    append_ref(it, name, path);
}

static void
append_null_ref(DBusMessageIter *it)
{
    append_ref(it, "org.a11y.atspi.Registry", "/org/a11y/atspi/null");
}

static void
append_attribute(DBusMessageIter *arr, const char *key, const char *val)
{
    DBusMessageIter e;

    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(arr, &e);
}

static void
append_variant_string(DBusMessageIter *it, const char *s)
{
    DBusMessageIter v;

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s);
    dbus_message_iter_close_container(it, &v);
}

static void
append_variant_int(DBusMessageIter *it, dbus_int32_t n)
{
    DBusMessageIter v;

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "i", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &n);
    dbus_message_iter_close_container(it, &v);
}

static void
append_variant_double(DBusMessageIter *it, double d)
{
    DBusMessageIter v;

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "d", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_DOUBLE, &d);
    dbus_message_iter_close_container(it, &v);
}

static void
append_variant_ref(DBusMessageIter *it, unsigned id)
{
    DBusMessageIter v;

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "(so)", &v);
    append_node_ref(&v, id);
    dbus_message_iter_close_container(it, &v);
}

static void
append_state(DBusMessageIter *it, const reaktor_snap_node *n)
{
    DBusMessageIter arr;
    dbus_uint32_t w[2];
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

    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "u", &arr);
    for (i = 0; i < 2; i++)
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_UINT32, &w[i]);
    dbus_message_iter_close_container(it, &arr);
}

static void
append_interfaces(DBusMessageIter *it, const reaktor_snap_node *n, int is_root)
{
    DBusMessageIter arr;
    const char *s;

    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "s", &arr);
    s = IF_ACCESSIBLE;
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
    if (is_root) {
        s = IF_APPLICATION;
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
    } else {
        s = IF_COMPONENT;
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
        if (has_action(n->role)) {
            s = IF_ACTION;
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
        }
        if (n->hi > n->lo) {
            s = IF_VALUE;
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
        }
    }
    dbus_message_iter_close_container(it, &arr);
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

static DBusHandlerResult
send_reply(DBusConnection *c, DBusMessage *m, DBusMessage *r)
{
    if (r) {
        dbus_connection_send(c, r, NULL);
        dbus_message_unref(r);
    }
    (void)m;
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
reply_unknown(DBusConnection *c, DBusMessage *m)
{
    DBusMessage *r = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD,
                                            "no such member here");
    return send_reply(c, m, r);
}

static DBusHandlerResult
handle_properties(DBusConnection *c, DBusMessage *m, unsigned id, int is_root,
                  const reaktor_snap_node *n)
{
    const char *iface = NULL, *prop = NULL;
    DBusMessage *r;
    DBusMessageIter it;

    if (!dbus_message_has_member(m, "Get")) {
        if (dbus_message_has_member(m, "GetAll")) {
            DBusMessageIter arr;

            r = dbus_message_new_method_return(m);
            if (!r) return DBUS_HANDLER_RESULT_HANDLED;
            dbus_message_iter_init_append(r, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}",
                                             &arr);
            dbus_message_iter_close_container(&it, &arr);
            return send_reply(c, m, r);
        }
        return reply_unknown(c, m);
    }

    if (!dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
        return reply_unknown(c, m);

    r = dbus_message_new_method_return(m);
    if (!r) return DBUS_HANDLER_RESULT_HANDLED;
    dbus_message_iter_init_append(r, &it);

    if (strcmp(iface, IF_ACCESSIBLE) == 0) {
        if (strcmp(prop, "Name") == 0)
            append_variant_string(&it, is_root ? "Reaktor"
                                               : (n->name ? n->name : ""));
        else if (strcmp(prop, "Description") == 0)
            append_variant_string(&it, "");
        else if (strcmp(prop, "Locale") == 0)
            append_variant_string(&it, "C");
        else if (strcmp(prop, "AccessibleId") == 0) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%u", id);
            append_variant_string(&it, buf);
        } else if (strcmp(prop, "ChildCount") == 0)
            append_variant_int(&it, (dbus_int32_t)child_count(id));
        else if (strcmp(prop, "Parent") == 0) {
            if (is_root) {
                DBusMessageIter v;
                dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT,
                                                 "(so)", &v);
                if (g.desktop_path[0])
                    append_ref(&v, g.desktop_name, g.desktop_path);
                else
                    append_null_ref(&v);
                dbus_message_iter_close_container(&it, &v);
            } else {
                append_variant_ref(&it, reaktor_snap_parent(id));
            }
        } else {
            dbus_message_unref(r);
            return reply_unknown(c, m);
        }
    } else if (strcmp(iface, IF_APPLICATION) == 0) {
        if (strcmp(prop, "ToolkitName") == 0)
            append_variant_string(&it, "Reaktor");
        else if (strcmp(prop, "Version") == 0)
            append_variant_string(&it, "1");
        else if (strcmp(prop, "AtspiVersion") == 0)
            append_variant_string(&it, "2.1");
        else if (strcmp(prop, "Id") == 0)
            append_variant_int(&it, 0);
        else {
            dbus_message_unref(r);
            return reply_unknown(c, m);
        }
    } else if (strcmp(iface, IF_VALUE) == 0) {
        if (strcmp(prop, "CurrentValue") == 0)
            append_variant_double(&it, n->num);
        else if (strcmp(prop, "MinimumValue") == 0)
            append_variant_double(&it, n->lo);
        else if (strcmp(prop, "MaximumValue") == 0)
            append_variant_double(&it, n->hi);
        else if (strcmp(prop, "MinimumIncrement") == 0)
            append_variant_double(&it, n->step);
        else {
            dbus_message_unref(r);
            return reply_unknown(c, m);
        }
    } else if (strcmp(iface, IF_ACTION) == 0) {
        if (strcmp(prop, "NActions") == 0)
            append_variant_int(&it, has_action(n->role) ? 1 : 0);
        else {
            dbus_message_unref(r);
            return reply_unknown(c, m);
        }
    } else {
        dbus_message_unref(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static DBusHandlerResult
handle_accessible(DBusConnection *c, DBusMessage *m, unsigned id, int is_root,
                  const reaktor_snap_node *n)
{
    DBusMessage *r = dbus_message_new_method_return(m);
    DBusMessageIter it;

    if (!r) return DBUS_HANDLER_RESULT_HANDLED;
    dbus_message_iter_init_append(r, &it);

    if (dbus_message_has_member(m, "GetRole")) {
        dbus_uint32_t v = is_root ? ATSPI_ROLE_APPLICATION : role_of(n->role);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &v);
    } else if (dbus_message_has_member(m, "GetRoleName") ||
               dbus_message_has_member(m, "GetLocalizedRoleName")) {
        const char *s = is_root ? "application" : role_name(n->role);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &s);
    } else if (dbus_message_has_member(m, "GetState")) {
        if (is_root) {
            DBusMessageIter arr;
            dbus_uint32_t w[2];
            int i;

            w[0] = (1u << ATSPI_STATE_VISIBLE) | (1u << ATSPI_STATE_SHOWING) |
                   (1u << ATSPI_STATE_ENABLED) | (1u << ATSPI_STATE_SENSITIVE);
            w[1] = 0;
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "u", &arr);
            for (i = 0; i < 2; i++)
                dbus_message_iter_append_basic(&arr, DBUS_TYPE_UINT32, &w[i]);
            dbus_message_iter_close_container(&it, &arr);
        } else {
            append_state(&it, n);
        }
    } else if (dbus_message_has_member(m, "GetChildAtIndex")) {
        dbus_int32_t idx = 0;

        dbus_message_get_args(m, NULL, DBUS_TYPE_INT32, &idx,
                              DBUS_TYPE_INVALID);
        {
            unsigned kid = child_at(id, (int)idx);
            if (kid) append_node_ref(&it, kid);
            else     append_null_ref(&it);
        }
    } else if (dbus_message_has_member(m, "GetChildren")) {
        DBusMessageIter arr;
        unsigned kid = reaktor_snap_child(id, 0);

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(so)", &arr);
        while (kid) {
            append_node_ref(&arr, kid);
            kid = reaktor_snap_sibling(kid, 0);
        }
        dbus_message_iter_close_container(&it, &arr);
    } else if (dbus_message_has_member(m, "GetIndexInParent")) {
        dbus_int32_t v = is_root ? 0 : (dbus_int32_t)index_in_parent(id);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &v);
    } else if (dbus_message_has_member(m, "GetApplication")) {
        append_node_ref(&it, 0);
    } else if (dbus_message_has_member(m, "GetRelationSet")) {
        DBusMessageIter arr;
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ua(so))",
                                         &arr);
        dbus_message_iter_close_container(&it, &arr);
    } else if (dbus_message_has_member(m, "GetAttributes")) {
        DBusMessageIter arr;
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ss}", &arr);
        if (!is_root && n && n->keys)
            append_attribute(&arr, "keyshortcuts", n->keys);
        dbus_message_iter_close_container(&it, &arr);
    } else if (dbus_message_has_member(m, "GetInterfaces")) {
        append_interfaces(&it, n, is_root);
    } else {
        dbus_message_unref(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static DBusHandlerResult
handle_component(DBusConnection *c, DBusMessage *m, unsigned id,
                 const reaktor_snap_node *n)
{
    DBusMessage *r = dbus_message_new_method_return(m);
    DBusMessageIter it;

    if (!r) return DBUS_HANDLER_RESULT_HANDLED;
    dbus_message_iter_init_append(r, &it);

    if (dbus_message_has_member(m, "GetExtents")) {
        DBusMessageIter sub;
        dbus_int32_t v[4];
        int i;

        v[0] = (dbus_int32_t)n->x; v[1] = (dbus_int32_t)n->y;
        v[2] = (dbus_int32_t)n->w; v[3] = (dbus_int32_t)n->h;
        dbus_message_iter_open_container(&it, DBUS_TYPE_STRUCT, NULL, &sub);
        for (i = 0; i < 4; i++)
            dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &v[i]);
        dbus_message_iter_close_container(&it, &sub);
    } else if (dbus_message_has_member(m, "GetPosition")) {
        dbus_int32_t x = (dbus_int32_t)n->x, y = (dbus_int32_t)n->y;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &x);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &y);
    } else if (dbus_message_has_member(m, "GetSize")) {
        dbus_int32_t w = (dbus_int32_t)n->w, h = (dbus_int32_t)n->h;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &w);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &h);
    } else if (dbus_message_has_member(m, "Contains")) {
        dbus_int32_t x = 0, y = 0;
        dbus_uint32_t coord = 0;
        dbus_bool_t in;

        dbus_message_get_args(m, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32,
                              &y, DBUS_TYPE_UINT32, &coord, DBUS_TYPE_INVALID);
        in = (x >= n->x && x < n->x + n->w &&
              y >= n->y && y < n->y + n->h) ? TRUE : FALSE;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &in);
    } else if (dbus_message_has_member(m, "GetAccessibleAtPoint")) {
        dbus_int32_t x = 0, y = 0;
        dbus_uint32_t coord = 0;
        unsigned hit;

        dbus_message_get_args(m, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32,
                              &y, DBUS_TYPE_UINT32, &coord, DBUS_TYPE_INVALID);
        hit = reaktor_snap_hit((float)x, (float)y);
        if (hit) append_node_ref(&it, hit);
        else     append_null_ref(&it);
    } else if (dbus_message_has_member(m, "GrabFocus")) {
        dbus_bool_t ok = TRUE;

        reaktor_snap_request_focus(id);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &ok);
    } else if (dbus_message_has_member(m, "GetLayer")) {
        dbus_uint32_t layer = 3;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &layer);
    } else if (dbus_message_has_member(m, "GetMDIZOrder")) {
        dbus_int16_t z = 0;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT16, &z);
    } else if (dbus_message_has_member(m, "GetAlpha")) {
        double a = 1.0;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_DOUBLE, &a);
    } else {
        dbus_message_unref(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static DBusHandlerResult
handle_action(DBusConnection *c, DBusMessage *m, unsigned id,
              const reaktor_snap_node *n)
{
    DBusMessage *r = dbus_message_new_method_return(m);
    DBusMessageIter it;

    if (!r) return DBUS_HANDLER_RESULT_HANDLED;
    dbus_message_iter_init_append(r, &it);

    if (dbus_message_has_member(m, "GetNActions")) {
        dbus_int32_t v = has_action(n->role) ? 1 : 0;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &v);
    } else if (dbus_message_has_member(m, "DoAction")) {
        dbus_bool_t ok = has_action(n->role) ? TRUE : FALSE;

        if (ok) reaktor_snap_request_activate(id);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &ok);
    } else if (dbus_message_has_member(m, "GetName") ||
               dbus_message_has_member(m, "GetLocalizedName")) {
        const char *s = "activate";
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &s);
    } else if (dbus_message_has_member(m, "GetDescription") ||
               dbus_message_has_member(m, "GetKeyBinding")) {
        const char *s = "";
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &s);
    } else if (dbus_message_has_member(m, "GetActions")) {
        DBusMessageIter arr;
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(sss)", &arr);
        if (has_action(n->role)) {
            DBusMessageIter sub;
            const char *a = "activate", *b = "", *k = "";

            dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL,
                                             &sub);
            dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &a);
            dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &b);
            dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &k);
            dbus_message_iter_close_container(&arr, &sub);
        }
        dbus_message_iter_close_container(&it, &arr);
    } else {
        dbus_message_unref(r);
        return reply_unknown(c, m);
    }
    return send_reply(c, m, r);
}

static DBusHandlerResult
handle_value(DBusConnection *c, DBusMessage *m)
{
    DBusMessage *r = dbus_message_new_error(m, DBUS_ERROR_NOT_SUPPORTED,
                                            "value is read-only");
    return send_reply(c, m, r);
}

static DBusHandlerResult
on_message(DBusConnection *c, DBusMessage *m, void *user)
{
    const char *iface = dbus_message_get_interface(m);
    long id = id_of(dbus_message_get_path(m));
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];
    int is_root;

    (void)user;
    if (id < 0 || !iface) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    is_root = id == 0;

    memset(&n, 0, sizeof(n));
    if (!is_root && !reaktor_snap_get((unsigned)id, &n, buf, sizeof(buf))) {
        DBusMessage *r = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_OBJECT,
                                                "that element is gone");
        return send_reply(c, m, r);
    }

    if (strcmp(iface, IF_PROPS) == 0)
        return handle_properties(c, m, (unsigned)id, is_root, &n);
    if (strcmp(iface, IF_ACCESSIBLE) == 0)
        return handle_accessible(c, m, (unsigned)id, is_root, &n);
    if (strcmp(iface, IF_COMPONENT) == 0 && !is_root)
        return handle_component(c, m, (unsigned)id, &n);
    if (strcmp(iface, IF_ACTION) == 0 && !is_root)
        return handle_action(c, m, (unsigned)id, &n);
    if (strcmp(iface, IF_VALUE) == 0 && !is_root)
        return handle_value(c, m);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
emit_event(const char *member, const char *detail, dbus_int32_t d1,
           dbus_int32_t d2, unsigned id)
{
    DBusMessage *sig;
    DBusMessageIter it, v;
    char path[256];
    dbus_int32_t zero = 0;

    if (!g.bus) return;
    path_of(id, path, sizeof(path));
    sig = dbus_message_new_signal(path, IF_EVENT_OBJECT, member);
    if (!sig) return;

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &detail);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &d1);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &d2);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "i", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &zero);
    dbus_message_iter_close_container(&it, &v);
    append_node_ref(&it, 0);

    dbus_connection_send(g.bus, sig, NULL);
    dbus_message_unref(sig);
}

static int SDLCALL
pump(void *user)
{
    (void)user;
    while (g.running && dbus_connection_read_write_dispatch(g.bus, 200))
        ;
    return 0;
}

static char *
a11y_bus_address(void)
{
    DBusConnection *session;
    DBusMessage *msg, *reply;
    DBusError err;
    const char *addr = NULL;
    char *out = NULL;

    dbus_error_init(&err);
    session = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!session) { dbus_error_free(&err); return NULL; }

    msg = dbus_message_new_method_call(A11Y_BUS_NAME, A11Y_BUS_PATH,
                                       A11Y_BUS_NAME, "GetAddress");
    if (!msg) return NULL;
    reply = dbus_connection_send_with_reply_and_block(session, msg, 1000, &err);
    dbus_message_unref(msg);
    if (!reply) {
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return NULL;
    }
    if (dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &addr,
                              DBUS_TYPE_INVALID) && addr)
        out = SDL_strdup(addr);
    dbus_message_unref(reply);
    return out;
}

static void
embed(void)
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, sub;
    DBusError err;
    const char *name, *path;

    dbus_error_init(&err);
    msg = dbus_message_new_method_call(ATSPI_REGISTRY, ATSPI_ROOT_PATH,
                                       IF_SOCKET, "Embed");
    if (!msg) return;
    dbus_message_iter_init_append(msg, &it);
    append_node_ref(&it, 0);

    reply = dbus_connection_send_with_reply_and_block(g.bus, msg, 2000, &err);
    dbus_message_unref(msg);
    if (!reply) {
        if (dbus_error_is_set(&err)) {
            SDL_Log("a11y: Embed refused (%s); the tree is not registered",
                    err.message);
            dbus_error_free(&err);
        }
        return;
    }
    if (dbus_message_iter_init(reply, &it) &&
        dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRUCT) {
        dbus_message_iter_recurse(&it, &sub);
        dbus_message_iter_get_basic(&sub, &name);
        dbus_message_iter_next(&sub);
        dbus_message_iter_get_basic(&sub, &path);
        SDL_strlcpy(g.desktop_name, name ? name : "",
                    sizeof(g.desktop_name));
        SDL_strlcpy(g.desktop_path, path ? path : "",
                    sizeof(g.desktop_path));
    }
    dbus_message_unref(reply);
}

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    static const DBusObjectPathVTable vtable = { NULL, on_message,
                                                 NULL, NULL, NULL, NULL };
    DBusError err;
    char *addr;
    const char *unique;

    if (!reaktor_snap_init(activate, focus, user)) return;

    dbus_threads_init_default();

    addr = a11y_bus_address();
    if (!addr) {
        SDL_Log("a11y: no accessibility bus; nothing is served");
        return;
    }

    dbus_error_init(&err);
    g.bus = dbus_connection_open_private(addr, &err);
    SDL_free(addr);
    if (!g.bus) {
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return;
    }
    if (!dbus_bus_register(g.bus, &err)) {
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        dbus_connection_close(g.bus);
        dbus_connection_unref(g.bus);
        g.bus = NULL;
        return;
    }
    dbus_connection_set_exit_on_disconnect(g.bus, FALSE);

    unique = dbus_bus_get_unique_name(g.bus);
    SDL_strlcpy(g.self, unique ? unique : "", sizeof(g.self));

    if (!dbus_connection_register_fallback(g.bus, "/org/a11y/atspi/accessible",
                                           &vtable, NULL)) {
        dbus_connection_close(g.bus);
        dbus_connection_unref(g.bus);
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
reaktor_a11y_platform_drain(void)
{
    reaktor_snap_drain();
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    int m;

    if (!reaktor_snap_update(a, focus_id)) return;
    if (!g.ready) return;

    reaktor_a11y_changes(a, &m);
    {
        int i, structural = 0;
        const reaktor_a11y_change *c = reaktor_a11y_changes(a, &m);

        for (i = 0; i < m; i++)
            if (c[i].kind == REAKTOR_A11Y_ADDED ||
                c[i].kind == REAKTOR_A11Y_REMOVED) {
                structural = 1;
                break;
            }
        if (structural) emit_event("ChildrenChanged", "add", 0, 0, 0);
    }

    if (focus_id != g.last_focus) {
        if (g.last_focus)
            emit_event("StateChanged", "focused", 0, 0, g.last_focus);
        if (focus_id)
            emit_event("StateChanged", "focused", 1, 0, focus_id);
        g.last_focus = focus_id;
    }
}

#endif
