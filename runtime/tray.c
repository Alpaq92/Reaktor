#include "internal.h"
#include "reaktor/launch.h"

#define TRAY_ITEMS 16

#ifdef REAKTOR_TRAY_DBUS
/* Linux and the BSDs: a StatusNotifierItem with a com.canonical.dbusmenu menu,
   spoken over libdbus. SDL's tray there builds its menu from GTK widgets, and
   loading GTK cost the showcase hundreds of libraries and half a second. */
#include <dbus/dbus.h>
#include <poll.h>
#include <unistd.h>

#define SNI_PATH    "/StatusNotifierItem"
#define SNI_IFACE   "org.kde.StatusNotifierItem"
#define MENU_PATH   "/MenuBar"
#define MENU_IFACE  "com.canonical.dbusmenu"
#define WATCHER     "org.kde.StatusNotifierWatcher"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"

static DBusConnection    *g_bus;
static reaktor_tray_item  g_item[TRAY_ITEMS];
static int                g_on[TRAY_ITEMS];
static char              *g_label[TRAY_ITEMS];
static char              *g_tip;
static char               g_name[64];
static int                g_n, g_hide_on_minimize, g_tag;
static dbus_uint32_t      g_revision = 1;
static Uint8             *g_icon[4];
static int                g_icon_px[4], g_icons;

/* SDL has no way to wait on another descriptor, so one thread sleeps in
   poll() on the bus and wakes the frame loop, which reads and answers. It
   never touches libdbus; it waits for the main thread to drain the bus. */
static SDL_Thread    *g_thread;
static SDL_Semaphore *g_drained;
static SDL_AtomicInt  g_running;
static int            g_stop[2] = { -1, -1 };

static int SDLCALL
watch_bus(void *fdp)
{
    struct pollfd p[2];

    p[0].fd = (int)(intptr_t)fdp;
    p[0].events = POLLIN;
    p[1].fd = g_stop[0];
    p[1].events = POLLIN;
    while (SDL_GetAtomicInt(&g_running)) {
        SDL_Event e;

        p[0].revents = p[1].revents = 0;
        if (poll(p, 2, -1) < 0) continue;
        if (p[1].revents || !SDL_GetAtomicInt(&g_running)) break;
        if (!p[0].revents) continue;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        e.user.data1 = &g_tag;
        if (!SDL_PushEvent(&e)) break;
        SDL_WaitSemaphore(g_drained);
        /* A hung-up bus would wake us forever. */
        if (p[0].revents & (POLLHUP | POLLERR)) break;
    }
    return 0;
}

static void
put_variant(DBusMessageIter *it, int type, const char *sig, const void *v)
{
    DBusMessageIter var;

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, sig, &var);
    dbus_message_iter_append_basic(&var, type, v);
    dbus_message_iter_close_container(it, &var);
}

static void
put_entry(DBusMessageIter *dict, const char *key, int type, const char *sig,
          const void *v)
{
    DBusMessageIter e;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    put_variant(&e, type, sig, v);
    dbus_message_iter_close_container(dict, &e);
}

/* a(iiay): each size, ARGB32 in network byte order. */
static void
put_pixmaps(DBusMessageIter *it, int all)
{
    DBusMessageIter arr, st, bytes;
    int i;

    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "(iiay)", &arr);
    for (i = 0; all && i < g_icons; i++) {
        dbus_int32_t px = g_icon_px[i];
        const Uint8 *b = g_icon[i];

        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &px);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &px);
        dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "y", &bytes);
        dbus_message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &b, px * px * 4);
        dbus_message_iter_close_container(&st, &bytes);
        dbus_message_iter_close_container(&arr, &st);
    }
    dbus_message_iter_close_container(it, &arr);
}

/* (sa(iiay)ss): icon name, pixmaps, title, text. */
static void
put_tooltip(DBusMessageIter *it)
{
    DBusMessageIter var, st;
    const char *none = "", *tip = g_tip ? g_tip : "";

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &none);
    put_pixmaps(&st, 0);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &tip);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &none);
    dbus_message_iter_close_container(&var, &st);
    dbus_message_iter_close_container(it, &var);
}

/* One property of the item into a variant; 0 when there is no such name. */
static int
sni_prop(DBusMessageIter *it, const char *name)
{
    const char *none = "", *menu = MENU_PATH, *id = reaktor_app_ident();
    const char *cat = "ApplicationStatus", *status = "Active";
    const char *title = g_tip ? g_tip : id;
    dbus_bool_t yes = 1;
    dbus_int32_t zero = 0;
    DBusMessageIter var;

    if (!SDL_strcmp(name, "Category"))      put_variant(it, DBUS_TYPE_STRING, "s", &cat);
    else if (!SDL_strcmp(name, "Id"))       put_variant(it, DBUS_TYPE_STRING, "s", &id);
    else if (!SDL_strcmp(name, "Title"))    put_variant(it, DBUS_TYPE_STRING, "s", &title);
    else if (!SDL_strcmp(name, "Status"))   put_variant(it, DBUS_TYPE_STRING, "s", &status);
    else if (!SDL_strcmp(name, "WindowId")) put_variant(it, DBUS_TYPE_INT32, "i", &zero);
    else if (!SDL_strcmp(name, "IconName") || !SDL_strcmp(name, "OverlayIconName") ||
             !SDL_strcmp(name, "AttentionIconName") ||
             !SDL_strcmp(name, "AttentionMovieName"))
        put_variant(it, DBUS_TYPE_STRING, "s", &none);
    else if (!SDL_strcmp(name, "IconPixmap") || !SDL_strcmp(name, "OverlayIconPixmap") ||
             !SDL_strcmp(name, "AttentionIconPixmap")) {
        dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "a(iiay)", &var);
        put_pixmaps(&var, name[0] == 'I');
        dbus_message_iter_close_container(it, &var);
    } else if (!SDL_strcmp(name, "ToolTip"))    put_tooltip(it);
    else if (!SDL_strcmp(name, "ItemIsMenu"))   put_variant(it, DBUS_TYPE_BOOLEAN, "b", &yes);
    else if (!SDL_strcmp(name, "Menu"))         put_variant(it, DBUS_TYPE_OBJECT_PATH, "o", &menu);
    else return 0;
    return 1;
}

static const char *const sni_props[] = {
    "Category", "Id", "Title", "Status", "WindowId", "IconName", "IconPixmap",
    "OverlayIconName", "OverlayIconPixmap", "AttentionIconName",
    "AttentionIconPixmap", "AttentionMovieName", "ToolTip", "ItemIsMenu", "Menu"
};

static int
menu_prop(DBusMessageIter *it, const char *name)
{
    const char *dir = "ltr", *status = "normal";
    dbus_uint32_t version = 3;
    DBusMessageIter var, arr;

    if (!SDL_strcmp(name, "Version"))            put_variant(it, DBUS_TYPE_UINT32, "u", &version);
    else if (!SDL_strcmp(name, "TextDirection")) put_variant(it, DBUS_TYPE_STRING, "s", &dir);
    else if (!SDL_strcmp(name, "Status"))        put_variant(it, DBUS_TYPE_STRING, "s", &status);
    else if (!SDL_strcmp(name, "IconThemePath")) {
        dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "as", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
        dbus_message_iter_close_container(&var, &arr);
        dbus_message_iter_close_container(it, &var);
    } else return 0;
    return 1;
}

static const char *const menu_props[] = { "Version", "TextDirection", "Status", "IconThemePath" };

/* An item's a{sv}; id 0 is the root, item i is id i + 1. */
static void
put_item_props(DBusMessageIter *it, int id)
{
    DBusMessageIter dict;
    const reaktor_tray_item *t = id > 0 ? &g_item[id - 1] : NULL;

    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    if (!t) {
        const char *sub = "submenu";

        put_entry(&dict, "children-display", DBUS_TYPE_STRING, "s", &sub);
    } else if (!t->label) {
        const char *sep = "separator";

        put_entry(&dict, "type", DBUS_TYPE_STRING, "s", &sep);
    } else {
        const char *label = g_label[id - 1] ? g_label[id - 1] : "";
        dbus_bool_t on = !t->disabled;

        put_entry(&dict, "label", DBUS_TYPE_STRING, "s", &label);
        put_entry(&dict, "enabled", DBUS_TYPE_BOOLEAN, "b", &on);
        if (t->checkbox) {
            const char *mark = "checkmark";
            dbus_int32_t state = g_on[id - 1];

            put_entry(&dict, "toggle-type", DBUS_TYPE_STRING, "s", &mark);
            put_entry(&dict, "toggle-state", DBUS_TYPE_INT32, "i", &state);
        }
    }
    dbus_message_iter_close_container(it, &dict);
}

/* (ia{sv}av), the root holding every item when depth allows. */
static void
put_layout(DBusMessageIter *it, int id, int depth)
{
    DBusMessageIter st, kids, var;
    dbus_int32_t i32 = id;
    int i;

    dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &i32);
    put_item_props(&st, id);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &kids);
    for (i = 0; id == 0 && depth != 0 && i < g_n; i++) {
        dbus_message_iter_open_container(&kids, DBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
        put_layout(&var, i + 1, depth - 1);
        dbus_message_iter_close_container(&kids, &var);
    }
    dbus_message_iter_close_container(&st, &kids);
    dbus_message_iter_close_container(it, &st);
}

static void
send_and_free(DBusMessage *m)
{
    if (!m) return;
    dbus_connection_send(g_bus, m, NULL);
    dbus_message_unref(m);
}

static void
items_updated(int id)
{
    DBusMessage *m = dbus_message_new_signal(MENU_PATH, MENU_IFACE, "ItemsPropertiesUpdated");
    DBusMessageIter it, arr, st;
    dbus_int32_t i32 = id;

    if (!m) return;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &i32);
    put_item_props(&st, id);
    dbus_message_iter_close_container(&arr, &st);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ias)", &arr);
    dbus_message_iter_close_container(&it, &arr);
    send_and_free(m);
}

static void
clicked(int id)
{
    const reaktor_tray_item *it;
    App *app = reaktor_main_app();

    if (id < 1 || id > g_n) return;
    it = &g_item[id - 1];
    if (!it->label || it->disabled) return;
    /* A check item flips as it is chosen, as GTK's did under SDL's tray. */
    if (it->checkbox) {
        g_on[id - 1] = !g_on[id - 1];
        items_updated(id);
    }
    if (app && it->chosen) {
        it->chosen(app, it->checkbox && g_on[id - 1], it->user);
        reaktor_wake(app);
    }
}

static const char sni_xml[] =
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
    "<node><interface name=\"" SNI_IFACE "\">"
    "<method name=\"Activate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"SecondaryActivate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"ContextMenu\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>"
    "<method name=\"Scroll\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>"
    "<property name=\"Category\" type=\"s\" access=\"read\"/>"
    "<property name=\"Id\" type=\"s\" access=\"read\"/>"
    "<property name=\"Title\" type=\"s\" access=\"read\"/>"
    "<property name=\"Status\" type=\"s\" access=\"read\"/>"
    "<property name=\"WindowId\" type=\"i\" access=\"read\"/>"
    "<property name=\"IconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
    "<property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
    "<property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>"
    "<property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
    "<property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>"
    "<property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>"
    "<property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>"
    "<property name=\"Menu\" type=\"o\" access=\"read\"/>"
    "<signal name=\"NewTitle\"/><signal name=\"NewIcon\"/><signal name=\"NewToolTip\"/>"
    "<signal name=\"NewStatus\"><arg type=\"s\"/></signal>"
    "</interface></node>";

static const char menu_xml[] =
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
    "<node><interface name=\"" MENU_IFACE "\">"
    "<method name=\"GetLayout\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/>"
    "<arg type=\"(ia{sv}av)\" direction=\"out\"/></method>"
    "<method name=\"GetGroupProperties\"><arg type=\"ai\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/><arg type=\"a(ia{sv})\" direction=\"out\"/></method>"
    "<method name=\"GetProperty\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"out\"/></method>"
    "<method name=\"Event\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/></method>"
    "<method name=\"EventGroup\"><arg type=\"a(isvu)\" direction=\"in\"/>"
    "<arg type=\"ai\" direction=\"out\"/></method>"
    "<method name=\"AboutToShow\"><arg type=\"i\" direction=\"in\"/><arg type=\"b\" direction=\"out\"/></method>"
    "<method name=\"AboutToShowGroup\"><arg type=\"ai\" direction=\"in\"/>"
    "<arg type=\"ai\" direction=\"out\"/><arg type=\"ai\" direction=\"out\"/></method>"
    "<property name=\"Version\" type=\"u\" access=\"read\"/>"
    "<property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
    "<property name=\"Status\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconThemePath\" type=\"as\" access=\"read\"/>"
    "<signal name=\"ItemsPropertiesUpdated\"><arg type=\"a(ia{sv})\"/><arg type=\"a(ias)\"/></signal>"
    "<signal name=\"LayoutUpdated\"><arg type=\"u\"/><arg type=\"i\"/></signal>"
    "<signal name=\"ItemActivationRequested\"><arg type=\"i\"/><arg type=\"u\"/></signal>"
    "</interface></node>";

/* Properties.Get and GetAll, for either object. */
static DBusMessage *
properties(DBusMessage *m, int menu)
{
    int (*prop)(DBusMessageIter *, const char *) = menu ? menu_prop : sni_prop;
    const char *const *names = menu ? menu_props : sni_props;
    int count = menu ? (int)SDL_arraysize(menu_props) : (int)SDL_arraysize(sni_props);
    const char *iface = NULL, *name = NULL;
    DBusMessage *r;
    DBusMessageIter it, dict, e;
    int i;

    if (dbus_message_is_method_call(m, PROPS_IFACE, "GetAll")) {
        if (!dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID))
            return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS, "GetAll(s)");
        if (!(r = dbus_message_new_method_return(m))) return NULL;
        dbus_message_iter_init_append(r, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
        for (i = 0; i < count; i++) {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
            dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &names[i]);
            prop(&e, names[i]);
            dbus_message_iter_close_container(&dict, &e);
        }
        dbus_message_iter_close_container(&it, &dict);
        return r;
    }
    if (dbus_message_is_method_call(m, PROPS_IFACE, "Get")) {
        if (!dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &iface,
                                   DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
            return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS, "Get(ss)");
        if (!(r = dbus_message_new_method_return(m))) return NULL;
        dbus_message_iter_init_append(r, &it);
        if (prop(&it, name)) return r;
        dbus_message_unref(r);
        return dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_PROPERTY, name);
    }
    return dbus_message_new_error(m, DBUS_ERROR_PROPERTY_READ_ONLY, "read only");
}

static DBusMessage *
menu_call(DBusMessage *m, const char *member)
{
    DBusMessage *r = NULL;
    DBusMessageIter in, out, arr, st;
    dbus_int32_t id = 0, depth = -1;
    int i;

    if (!SDL_strcmp(member, "GetLayout")) {
        if (!dbus_message_get_args(m, NULL, DBUS_TYPE_INT32, &id, DBUS_TYPE_INT32, &depth,
                                   DBUS_TYPE_INVALID) || id < 0 || id > g_n)
            return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS, "no such item");
        if (!(r = dbus_message_new_method_return(m))) return NULL;
        dbus_message_iter_init_append(r, &out);
        dbus_message_iter_append_basic(&out, DBUS_TYPE_UINT32, &g_revision);
        put_layout(&out, id, depth);
    } else if (!SDL_strcmp(member, "GetGroupProperties")) {
        DBusMessageIter ids;
        int any = 0;

        if (!(r = dbus_message_new_method_return(m))) return NULL;
        dbus_message_iter_init_append(r, &out);
        dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
        if (dbus_message_iter_init(m, &in) &&
            dbus_message_iter_get_arg_type(&in) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&in, &ids);
            while (dbus_message_iter_get_arg_type(&ids) == DBUS_TYPE_INT32) {
                dbus_message_iter_get_basic(&ids, &id);
                any = 1;
                if (id >= 0 && id <= g_n) {
                    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st);
                    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &id);
                    put_item_props(&st, id);
                    dbus_message_iter_close_container(&arr, &st);
                }
                dbus_message_iter_next(&ids);
            }
        }
        /* No ids asks for every item. */
        for (i = 0; !any && i <= g_n; i++) {
            dbus_int32_t each = i;

            dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &each);
            put_item_props(&st, i);
            dbus_message_iter_close_container(&arr, &st);
        }
        dbus_message_iter_close_container(&out, &arr);
    } else if (!SDL_strcmp(member, "GetProperty")) {
        const char *name = NULL;
        DBusMessageIter props, e;

        if (!dbus_message_get_args(m, NULL, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &name,
                                   DBUS_TYPE_INVALID) || id < 0 || id > g_n)
            return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS, "no such item");
        /* Built as a dict, then the one value copied out. */
        {
            DBusMessage *tmp = dbus_message_new_method_return(m);
            DBusMessageIter t;

            if (!tmp) return NULL;
            dbus_message_iter_init_append(tmp, &t);
            put_item_props(&t, id);
            dbus_message_iter_init(tmp, &t);
            dbus_message_iter_recurse(&t, &props);
            while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                const char *key;

                dbus_message_iter_recurse(&props, &e);
                dbus_message_iter_get_basic(&e, &key);
                if (!SDL_strcmp(key, name)) {
                    DBusMessageIter v, inner;
                    int type;
                    union { const char *s; dbus_int32_t i; dbus_bool_t b; } val;
                    char sig[2];

                    dbus_message_iter_next(&e);
                    dbus_message_iter_recurse(&e, &v);
                    type = dbus_message_iter_get_arg_type(&v);
                    dbus_message_iter_get_basic(&v, &val);
                    sig[0] = (char)type;
                    sig[1] = 0;
                    if ((r = dbus_message_new_method_return(m))) {
                        dbus_message_iter_init_append(r, &out);
                        dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, sig, &inner);
                        dbus_message_iter_append_basic(&inner, type, &val);
                        dbus_message_iter_close_container(&out, &inner);
                    }
                    break;
                }
                dbus_message_iter_next(&props);
            }
            dbus_message_unref(tmp);
        }
        if (!r) return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS, "no such property");
    } else if (!SDL_strcmp(member, "Event")) {
        const char *what = NULL;

        if (dbus_message_iter_init(m, &in) &&
            dbus_message_iter_get_arg_type(&in) == DBUS_TYPE_INT32) {
            dbus_message_iter_get_basic(&in, &id);
            dbus_message_iter_next(&in);
            if (dbus_message_iter_get_arg_type(&in) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&in, &what);
        }
        r = dbus_message_new_method_return(m);
        if (what && !SDL_strcmp(what, "clicked")) {
            send_and_free(r);
            r = NULL;
            clicked(id);
            return NULL;
        }
    } else if (!SDL_strcmp(member, "EventGroup")) {
        DBusMessageIter evs, ev;
        int n = 0, chosen[TRAY_ITEMS];

        if (dbus_message_iter_init(m, &in) &&
            dbus_message_iter_get_arg_type(&in) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&in, &evs);
            while (dbus_message_iter_get_arg_type(&evs) == DBUS_TYPE_STRUCT) {
                const char *what = NULL;

                dbus_message_iter_recurse(&evs, &ev);
                dbus_message_iter_get_basic(&ev, &id);
                dbus_message_iter_next(&ev);
                if (dbus_message_iter_get_arg_type(&ev) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&ev, &what);
                if (what && !SDL_strcmp(what, "clicked") && n < TRAY_ITEMS) chosen[n++] = id;
                dbus_message_iter_next(&evs);
            }
        }
        if ((r = dbus_message_new_method_return(m))) {
            dbus_message_iter_init_append(r, &out);
            dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "i", &arr);
            dbus_message_iter_close_container(&out, &arr);
            send_and_free(r);
        }
        for (i = 0; i < n; i++) clicked(chosen[i]);
        return NULL;
    } else if (!SDL_strcmp(member, "AboutToShow")) {
        dbus_bool_t no = 0;

        if ((r = dbus_message_new_method_return(m)))
            dbus_message_append_args(r, DBUS_TYPE_BOOLEAN, &no, DBUS_TYPE_INVALID);
    } else if (!SDL_strcmp(member, "AboutToShowGroup")) {
        if ((r = dbus_message_new_method_return(m))) {
            dbus_message_iter_init_append(r, &out);
            dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "i", &arr);
            dbus_message_iter_close_container(&out, &arr);
            dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "i", &arr);
            dbus_message_iter_close_container(&out, &arr);
        }
    } else {
        r = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD, member);
    }
    return r;
}

static DBusHandlerResult
on_message(DBusConnection *c, DBusMessage *m, void *menup)
{
    int menu = menup != NULL;
    const char *iface = dbus_message_get_interface(m);
    const char *member = dbus_message_get_member(m);
    DBusMessage *r = NULL;

    (void)c;
    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_CALL || !member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (iface && !SDL_strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE)) {
        const char *xml = menu ? menu_xml : sni_xml;

        if ((r = dbus_message_new_method_return(m)))
            dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    } else if (iface && !SDL_strcmp(iface, PROPS_IFACE)) {
        r = properties(m, menu);
    } else if (menu) {
        r = menu_call(m, member);
        if (!r) {
            dbus_connection_flush(g_bus);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    } else if (!SDL_strcmp(member, "Activate") || !SDL_strcmp(member, "SecondaryActivate") ||
               !SDL_strcmp(member, "ContextMenu") || !SDL_strcmp(member, "Scroll")) {
        /* ItemIsMenu: the host opens the menu itself on a click. */
        r = dbus_message_new_method_return(m);
    } else {
        r = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD, member);
    }
    send_and_free(r);
    dbus_connection_flush(g_bus);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static int
register_item(int block)
{
    DBusMessage *m = dbus_message_new_method_call(WATCHER, "/StatusNotifierWatcher", WATCHER,
                                                  "RegisterStatusNotifierItem");
    const char *name = g_name;
    DBusMessage *r;
    DBusError err;

    if (!m) return 0;
    dbus_message_append_args(m, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    if (!block) {
        send_and_free(m);
        dbus_connection_flush(g_bus);
        return 1;
    }
    dbus_error_init(&err);
    r = dbus_connection_send_with_reply_and_block(g_bus, m, 2000, &err);
    dbus_message_unref(m);
    if (!r) {
        SDL_Log("tray: no StatusNotifierWatcher (%s)", err.message ? err.message : "");
        dbus_error_free(&err);
        return 0;
    }
    dbus_message_unref(r);
    return 1;
}

/* A watcher that starts after us, or restarts, is told about the item again. */
static DBusHandlerResult
on_signal(DBusConnection *c, DBusMessage *m, void *user)
{
    const char *name = NULL, *was = NULL, *now = NULL;

    (void)c; (void)user;
    if (dbus_message_is_signal(m, DBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
        dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &was,
                              DBUS_TYPE_STRING, &now, DBUS_TYPE_INVALID) &&
        !SDL_strcmp(name, WATCHER) && now && now[0])
        register_item(0);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
pump(void)
{
    if (!g_bus) return;
    dbus_connection_read_write(g_bus, 0);
    while (dbus_connection_dispatch(g_bus) == DBUS_DISPATCH_DATA_REMAINS)
        ;
}

static void
icons_make(void)
{
    static const int sizes[] = { 22, 32, 48, 64 };
    int i, x, y;

    for (i = g_icons = 0; i < (int)SDL_arraysize(sizes); i++) {
        SDL_Surface *s = reaktor_icon_surface(reaktor_launch_icon(), sizes[i]);
        Uint8 *out;

        if (!s) continue;
        if (s->w != sizes[i] || s->h != sizes[i] ||
            !(out = (Uint8 *)SDL_malloc((size_t)s->w * (size_t)s->h * 4))) {
            SDL_DestroySurface(s);
            continue;
        }
        for (y = 0; y < s->h; y++) {
            const Uint32 *row = (const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch);

            for (x = 0; x < s->w; x++) {
                Uint32 p = row[x];
                Uint8 *o = out + (y * s->w + x) * 4;

                o[0] = (Uint8)(p >> 24);
                o[1] = (Uint8)(p >> 16);
                o[2] = (Uint8)(p >> 8);
                o[3] = (Uint8)p;
            }
        }
        g_icon[g_icons] = out;
        g_icon_px[g_icons++] = sizes[i];
        SDL_DestroySurface(s);
    }
}

static char *
clean_dup(const char *s, int mnemonic)
{
    size_t n = SDL_strlen(s) * 3 + 1, i, j;
    char *c = (char *)SDL_malloc(n), *out;

    if (!c) return NULL;
    reaktor_utf8_clean(c, s, n);
    if (!mnemonic || !(out = (char *)SDL_malloc(SDL_strlen(c) * 2 + 1))) return c;
    /* dbusmenu reads an underscore as a mnemonic; GTK's label under SDL did not. */
    for (i = j = 0; c[i]; i++) {
        if (c[i] == '_') out[j++] = '_';
        out[j++] = c[i];
    }
    out[j] = 0;
    SDL_free(c);
    return out;
}

int
reaktor_tray_open(App *app, const reaktor_tray *spec)
{
    static const DBusObjectPathVTable vt = { NULL, on_message, NULL, NULL, NULL, NULL };
    const char *drv = SDL_GetCurrentVideoDriver();
    DBusError err;
    int i, fd = -1;

    reaktor_tray_close(app);
    if (!spec) return 0;
    if (!drv || SDL_strcmp(drv, "dummy") == 0 || SDL_strcmp(drv, "offscreen") == 0)
        return 0;
    dbus_error_init(&err);
    if (!(g_bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err))) {
        SDL_Log("tray: %s", err.message ? err.message : "no session bus");
        dbus_error_free(&err);
        return 0;
    }
    dbus_connection_set_exit_on_disconnect(g_bus, 0);
    g_n = spec->count < 0 ? 0 : spec->count < TRAY_ITEMS ? spec->count : TRAY_ITEMS;
    for (i = 0; i < g_n; i++) {
        g_item[i]  = spec->items[i];
        g_on[i]    = spec->items[i].checkbox && spec->items[i].checked;
        g_label[i] = spec->items[i].label ? clean_dup(spec->items[i].label, 1) : NULL;
    }
    g_tip = clean_dup(spec->tooltip ? spec->tooltip : "", 0);
    icons_make();
    SDL_snprintf(g_name, sizeof g_name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());
    if (!dbus_connection_register_object_path(g_bus, SNI_PATH, &vt, NULL) ||
        !dbus_connection_register_object_path(g_bus, MENU_PATH, &vt, (void *)1) ||
        dbus_bus_request_name(g_bus, g_name, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) !=
            DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
        !register_item(1)) {
        reaktor_tray_close(app);
        return 0;
    }
    dbus_connection_add_filter(g_bus, on_signal, NULL, NULL);
    dbus_bus_add_match(g_bus, "type='signal',sender='" DBUS_SERVICE_DBUS "',"
                       "interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',"
                       "arg0='" WATCHER "'", NULL);
    dbus_connection_flush(g_bus);
    if (dbus_connection_get_unix_fd(g_bus, &fd) && pipe(g_stop) == 0 &&
        (g_drained = SDL_CreateSemaphore(0))) {
        SDL_SetAtomicInt(&g_running, 1);
        g_thread = SDL_CreateThread(watch_bus, "reaktor-tray", (void *)(intptr_t)fd);
    }
    if (!g_thread) {
        reaktor_tray_close(app);
        return 0;
    }
    /* Whatever arrived during setup is read now; the thread sees only what is new. */
    pump();
    return 1;
}

void
reaktor_tray_check(App *app, int item, int checked)
{
    (void)app;
    if (!g_bus || item < 0 || item >= g_n || !g_item[item].checkbox) return;
    if (g_on[item] == (checked != 0)) return;
    g_on[item] = checked != 0;
    items_updated(item + 1);
    dbus_connection_flush(g_bus);
}

void
reaktor_tray_close(App *app)
{
    int i;

    (void)app;
    if (g_thread) {
        SDL_SetAtomicInt(&g_running, 0);
        if (write(g_stop[1], "", 1) < 0) { /* the thread also checks the flag */ }
        SDL_SignalSemaphore(g_drained);
        SDL_WaitThread(g_thread, NULL);
        g_thread = NULL;
    }
    for (i = 0; i < 2; i++) {
        if (g_stop[i] >= 0) close(g_stop[i]);
        g_stop[i] = -1;
    }
    if (g_drained) SDL_DestroySemaphore(g_drained);
    g_drained = NULL;
    if (g_bus) {
        dbus_connection_close(g_bus);
        dbus_connection_unref(g_bus);
    }
    g_bus = NULL;
    for (i = 0; i < g_n; i++) {
        SDL_free(g_label[i]);
        g_label[i] = NULL;
    }
    for (i = 0; i < g_icons; i++) SDL_free(g_icon[i]);
    g_icons = 0;
    SDL_free(g_tip);
    g_tip = NULL;
    g_n   = 0;
}

void
reaktor_tray_hide_on_minimize(App *app, int on)
{
    (void)app;
    g_hide_on_minimize = on != 0;
}

void
reaktor_tray_event(App *app, const SDL_Event *e)
{
    if (e->type == SDL_EVENT_USER && e->user.data1 == &g_tag) {
        if (g_bus) {
            pump();
            SDL_SignalSemaphore(g_drained);
        }
        return;
    }
    if (e->type == SDL_EVENT_WINDOW_MINIMIZED && g_hide_on_minimize && g_bus)
        reaktor_set_visible(app, 0);
}

#else

static SDL_Tray          *g_tray;
static SDL_TrayEntry     *g_entry[TRAY_ITEMS];
static reaktor_tray_item  g_item[TRAY_ITEMS];
static int                g_n, g_hide_on_minimize;

static void SDLCALL
chosen(void *userdata, SDL_TrayEntry *entry)
{
    const reaktor_tray_item *it = &g_item[(intptr_t)userdata];
    App *app = reaktor_main_app();

    if (!app || !it->chosen) return;
    it->chosen(app, it->checkbox && SDL_GetTrayEntryChecked(entry), it->user);
    reaktor_wake(app);
}

int
reaktor_tray_open(App *app, const reaktor_tray *spec)
{
#if defined(__EMSCRIPTEN__) || (defined(__unix__) && !defined(__APPLE__) && !defined(__ANDROID__))
    /* Linux and the BSDs built without dbus-1: no tray, rather than SDL's and GTK. */
    (void)app; (void)spec;
    return 0;
#else
    SDL_TrayMenu *menu;
    SDL_Surface  *icon;
    const char   *drv = SDL_GetCurrentVideoDriver();
    int i;

    reaktor_tray_close(app);
    if (!spec) return 0;
    /* No display, no tray: macOS aborts creating one under dummy. */
    if (!drv || SDL_strcmp(drv, "dummy") == 0 || SDL_strcmp(drv, "offscreen") == 0)
        return 0;
    icon   = reaktor_icon_surface(reaktor_launch_icon(), 32);
    g_tray = SDL_CreateTray(icon, spec->tooltip);
    if (icon) SDL_DestroySurface(icon);
    if (!g_tray) {
        SDL_Log("tray: %s", SDL_GetError());
        return 0;
    }
    menu = SDL_CreateTrayMenu(g_tray);
    g_n  = spec->count < 0 ? 0 : spec->count < TRAY_ITEMS ? spec->count : TRAY_ITEMS;
    for (i = 0; i < g_n; i++) {
        const reaktor_tray_item *it = &spec->items[i];
        SDL_TrayEntryFlags f = it->checkbox ? SDL_TRAYENTRY_CHECKBOX : SDL_TRAYENTRY_BUTTON;

        if (it->checkbox && it->checked) f |= SDL_TRAYENTRY_CHECKED;
        if (it->disabled) f |= SDL_TRAYENTRY_DISABLED;
        g_item[i]  = *it;
        g_entry[i] = menu ? SDL_InsertTrayEntryAt(menu, -1, it->label, f) : NULL;
        if (g_entry[i] && it->label)
            SDL_SetTrayEntryCallback(g_entry[i], chosen, (void *)(intptr_t)i);
    }
    return 1;
#endif
}

void
reaktor_tray_check(App *app, int item, int checked)
{
    (void)app;
    if (item < 0 || item >= g_n || !g_entry[item] || !g_item[item].checkbox) return;
    if (SDL_GetTrayEntryChecked(g_entry[item]) != (checked != 0))
        SDL_SetTrayEntryChecked(g_entry[item], checked != 0);
}

void
reaktor_tray_close(App *app)
{
    (void)app;
    if (g_tray) SDL_DestroyTray(g_tray);
    g_tray = NULL;
    g_n    = 0;
}

void
reaktor_tray_hide_on_minimize(App *app, int on)
{
    (void)app;
    g_hide_on_minimize = on != 0;
}

void
reaktor_tray_event(App *app, const SDL_Event *e)
{
    if (e->type == SDL_EVENT_WINDOW_MINIMIZED && g_hide_on_minimize && g_tray)
        reaktor_set_visible(app, 0);
}
#endif
