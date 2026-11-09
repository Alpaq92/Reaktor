#include "internal.h"
#include "reaktor/launch.h"

#define TRAY_ITEMS 16

#ifdef REAKTOR_TRAY_DBUS
/* SDL's tray here builds its menu from GTK widgets, and loading GTK cost the
   showcase hundreds of libraries and half a second. */
#include "gibus.h"
#include <poll.h>
#include <unistd.h>

#define SNI_PATH    "/StatusNotifierItem"
#define SNI_IFACE   "org.kde.StatusNotifierItem"
#define MENU_PATH   "/MenuBar"
#define MENU_IFACE  "com.canonical.dbusmenu"
#define WATCHER     "org.kde.StatusNotifierWatcher"
#define PROPS_IFACE GIBUS_INTERFACE_PROPERTIES

static gibus_conn        *g_bus;
static reaktor_tray_item  g_item[TRAY_ITEMS];
static int                g_on[TRAY_ITEMS];
static char              *g_label[TRAY_ITEMS];
static char              *g_tip;
static char               g_name[64];
static int                g_n, g_hide_on_minimize, g_tag;
static Uint8             *g_icon[4];
static int                g_icon_px[4], g_icons;

/* SDL has no way to wait on another descriptor, so one thread sleeps in
   poll() on the bus and wakes the frame loop, which reads and answers. It
   never touches the connection; it waits for the main thread to drain it. */
static SDL_Thread    *g_thread;
static SDL_Semaphore *g_drained;
static SDL_AtomicInt  g_running;
static int            g_stop[2] = { -1, -1 };
static Sint32         g_gen;

static reaktor_tray_item g_chosen[TRAY_ITEMS];
static int               g_chosen_on[TRAY_ITEMS], g_chosen_n;

static int SDLCALL
watch_bus(void *fdp)
{
    struct pollfd p[2];
    Sint32 gen = g_gen;

    p[0].fd = (int)(intptr_t)fdp;
    p[0].events = POLLIN;
    p[1].fd = g_stop[0];
    p[1].events = POLLIN;
    while (SDL_GetAtomicInt(&g_running)) {
        SDL_Event e;

        if (poll(p, 2, -1) < 0) continue;
        if (p[1].revents || !SDL_GetAtomicInt(&g_running)) break;
        if (!p[0].revents) continue;
        SDL_zero(e);
        e.type = SDL_EVENT_USER;
        e.user.data1 = &g_tag;
        e.user.code = gen;
        if (!SDL_PushEvent(&e)) break;
        SDL_WaitSemaphore(g_drained);
        if (p[0].revents & (POLLHUP | POLLERR)) break;
    }
    return 0;
}

static void
put_variant(gibus_iter *it, int type, const char *sig, const void *v)
{
    gibus_iter var;

    gibus_iter_open(it, GIBUS_TYPE_VARIANT, sig, &var);
    gibus_iter_append_basic(&var, type, v);
    gibus_iter_close(it, &var);
}

static void
put_entry(gibus_iter *dict, const char *key, int type, const char *sig, const void *v)
{
    gibus_iter e;

    gibus_iter_open(dict, GIBUS_TYPE_DICT_ENTRY, NULL, &e);
    gibus_iter_append_basic(&e, GIBUS_TYPE_STRING, &key);
    put_variant(&e, type, sig, v);
    gibus_iter_close(dict, &e);
}

static void
put_empty(gibus_iter *it, const char *sig)
{
    gibus_iter arr;

    gibus_iter_open(it, GIBUS_TYPE_ARRAY, sig, &arr);
    gibus_iter_close(it, &arr);
}

static void
put_pixmaps(gibus_iter *it, int all)
{
    gibus_iter arr, st, bytes;
    int i;

    gibus_iter_open(it, GIBUS_TYPE_ARRAY, "(iiay)", &arr);
    for (i = 0; all && i < g_icons; i++) {
        int32_t px = g_icon_px[i];

        gibus_iter_open(&arr, GIBUS_TYPE_STRUCT, NULL, &st);
        gibus_iter_append_basic(&st, GIBUS_TYPE_INT32, &px);
        gibus_iter_append_basic(&st, GIBUS_TYPE_INT32, &px);
        gibus_iter_open(&st, GIBUS_TYPE_ARRAY, "y", &bytes);
        gibus_iter_append_fixed_array(&bytes, GIBUS_TYPE_BYTE, g_icon[i], px * px * 4);
        gibus_iter_close(&st, &bytes);
        gibus_iter_close(&arr, &st);
    }
    gibus_iter_close(it, &arr);
}

static void
put_tooltip(gibus_iter *it)
{
    gibus_iter var, st;
    const char *none = "", *tip = g_tip ? g_tip : "";

    gibus_iter_open(it, GIBUS_TYPE_VARIANT, "(sa(iiay)ss)", &var);
    gibus_iter_open(&var, GIBUS_TYPE_STRUCT, NULL, &st);
    gibus_iter_append_basic(&st, GIBUS_TYPE_STRING, &none);
    put_pixmaps(&st, 0);
    gibus_iter_append_basic(&st, GIBUS_TYPE_STRING, &tip);
    gibus_iter_append_basic(&st, GIBUS_TYPE_STRING, &none);
    gibus_iter_close(&var, &st);
    gibus_iter_close(it, &var);
}

static int
sni_prop(gibus_iter *it, const char *name)
{
    const char *none = "", *menu = MENU_PATH, *id = reaktor_app_ident();
    const char *cat = "ApplicationStatus", *status = "Active";
    const char *title = g_tip ? g_tip : id;
    gibus_bool yes = 1;
    int32_t zero = 0;
    gibus_iter var;

    if (!SDL_strcmp(name, "Category"))      put_variant(it, GIBUS_TYPE_STRING, "s", &cat);
    else if (!SDL_strcmp(name, "Id"))       put_variant(it, GIBUS_TYPE_STRING, "s", &id);
    else if (!SDL_strcmp(name, "Title"))    put_variant(it, GIBUS_TYPE_STRING, "s", &title);
    else if (!SDL_strcmp(name, "Status"))   put_variant(it, GIBUS_TYPE_STRING, "s", &status);
    else if (!SDL_strcmp(name, "WindowId")) put_variant(it, GIBUS_TYPE_INT32, "i", &zero);
    else if (!SDL_strcmp(name, "IconName") || !SDL_strcmp(name, "OverlayIconName") ||
             !SDL_strcmp(name, "AttentionIconName") ||
             !SDL_strcmp(name, "AttentionMovieName"))
        put_variant(it, GIBUS_TYPE_STRING, "s", &none);
    else if (!SDL_strcmp(name, "IconPixmap") || !SDL_strcmp(name, "OverlayIconPixmap") ||
             !SDL_strcmp(name, "AttentionIconPixmap")) {
        gibus_iter_open(it, GIBUS_TYPE_VARIANT, "a(iiay)", &var);
        put_pixmaps(&var, name[0] == 'I');
        gibus_iter_close(it, &var);
    } else if (!SDL_strcmp(name, "ToolTip"))    put_tooltip(it);
    else if (!SDL_strcmp(name, "ItemIsMenu"))   put_variant(it, GIBUS_TYPE_BOOLEAN, "b", &yes);
    else if (!SDL_strcmp(name, "Menu"))         put_variant(it, GIBUS_TYPE_OBJECT_PATH, "o", &menu);
    else return 0;
    return 1;
}

static const char *const sni_props[] = {
    "Category", "Id", "Title", "Status", "WindowId", "IconName", "IconPixmap",
    "OverlayIconName", "OverlayIconPixmap", "AttentionIconName",
    "AttentionIconPixmap", "AttentionMovieName", "ToolTip", "ItemIsMenu", "Menu"
};

static int
menu_prop(gibus_iter *it, const char *name)
{
    const char *dir = "ltr", *status = "normal";
    uint32_t version = 3;
    gibus_iter var;

    if (!SDL_strcmp(name, "Version"))            put_variant(it, GIBUS_TYPE_UINT32, "u", &version);
    else if (!SDL_strcmp(name, "TextDirection")) put_variant(it, GIBUS_TYPE_STRING, "s", &dir);
    else if (!SDL_strcmp(name, "Status"))        put_variant(it, GIBUS_TYPE_STRING, "s", &status);
    else if (!SDL_strcmp(name, "IconThemePath")) {
        gibus_iter_open(it, GIBUS_TYPE_VARIANT, "as", &var);
        put_empty(&var, "s");
        gibus_iter_close(it, &var);
    } else return 0;
    return 1;
}

static const char *const menu_props[] = { "Version", "TextDirection", "Status", "IconThemePath" };

typedef struct item_out {
    gibus_iter *it;
    const char *want;
    int         found;
} item_out;

static void
put_prop(item_out *o, const char *key, int type, const char *sig, const void *v)
{
    if (!o->want) {
        put_entry(o->it, key, type, sig, v);
    } else if (!o->found && !SDL_strcmp(key, o->want)) {
        put_variant(o->it, type, sig, v);
        o->found = 1;
    }
}

static void
item_props(item_out *o, int id)
{
    const reaktor_tray_item *t = id > 0 ? &g_item[id - 1] : NULL;

    if (!t) {
        const char *sub = "submenu";

        put_prop(o, "children-display", GIBUS_TYPE_STRING, "s", &sub);
    } else if (!t->label) {
        const char *sep = "separator";

        put_prop(o, "type", GIBUS_TYPE_STRING, "s", &sep);
    } else {
        const char *label = g_label[id - 1] ? g_label[id - 1] : "";
        gibus_bool on = !t->disabled;

        put_prop(o, "label", GIBUS_TYPE_STRING, "s", &label);
        put_prop(o, "enabled", GIBUS_TYPE_BOOLEAN, "b", &on);
        if (t->checkbox) {
            const char *mark = "checkmark";
            int32_t state = g_on[id - 1];

            put_prop(o, "toggle-type", GIBUS_TYPE_STRING, "s", &mark);
            put_prop(o, "toggle-state", GIBUS_TYPE_INT32, "i", &state);
        }
    }
}

static void
put_item_props(gibus_iter *it, int id)
{
    gibus_iter dict;
    item_out o;

    gibus_iter_open(it, GIBUS_TYPE_ARRAY, "{sv}", &dict);
    o.it = &dict;
    o.want = NULL;
    o.found = 0;
    item_props(&o, id);
    gibus_iter_close(it, &dict);
}

static void
put_layout(gibus_iter *it, int id, int depth)
{
    gibus_iter st, kids, var;
    int32_t i32 = id;
    int i;

    gibus_iter_open(it, GIBUS_TYPE_STRUCT, NULL, &st);
    gibus_iter_append_basic(&st, GIBUS_TYPE_INT32, &i32);
    put_item_props(&st, id);
    gibus_iter_open(&st, GIBUS_TYPE_ARRAY, "v", &kids);
    for (i = 0; id == 0 && depth != 0 && i < g_n; i++) {
        gibus_iter_open(&kids, GIBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
        put_layout(&var, i + 1, depth - 1);
        gibus_iter_close(&kids, &var);
    }
    gibus_iter_close(&st, &kids);
    gibus_iter_close(it, &st);
}

static void
put_item(gibus_iter *arr, int32_t id)
{
    gibus_iter st;

    gibus_iter_open(arr, GIBUS_TYPE_STRUCT, NULL, &st);
    gibus_iter_append_basic(&st, GIBUS_TYPE_INT32, &id);
    put_item_props(&st, id);
    gibus_iter_close(arr, &st);
}

static void
send_and_free(gibus_msg *m)
{
    if (!m) return;
    gibus_send(g_bus, m, NULL);
    gibus_msg_free(m);
    gibus_flush(g_bus);
}

static void
items_updated(int id)
{
    gibus_msg *m = gibus_msg_signal(MENU_PATH, MENU_IFACE, "ItemsPropertiesUpdated");
    gibus_iter it, arr;

    if (!m) return;
    gibus_iter_init_append(m, &it);
    gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "(ia{sv})", &arr);
    put_item(&arr, id);
    gibus_iter_close(&it, &arr);
    put_empty(&it, "(ias)");
    send_and_free(m);
}

static void
clicked(int id)
{
    const reaktor_tray_item *it;

    if (id < 1 || id > g_n) return;
    it = &g_item[id - 1];
    if (!it->label || it->disabled) return;
    if (it->checkbox) {
        g_on[id - 1] = !g_on[id - 1];
        items_updated(id);
    }
    if (it->chosen && g_chosen_n < TRAY_ITEMS) {
        g_chosen[g_chosen_n]      = *it;
        g_chosen_on[g_chosen_n++] = it->checkbox && g_on[id - 1];
    }
}

static const char sni_xml[] =
    GIBUS_INTROSPECT_DOCTYPE
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
    GIBUS_INTROSPECT_DOCTYPE
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

static gibus_msg *
properties(gibus_msg *m, int menu)
{
    int (*prop)(gibus_iter *, const char *) = menu ? menu_prop : sni_prop;
    const char *const *names = menu ? menu_props : sni_props;
    int count = menu ? (int)SDL_arraysize(menu_props) : (int)SDL_arraysize(sni_props);
    const char *iface = NULL, *name = NULL;
    gibus_msg *r;
    gibus_iter it, dict, e;
    int i;

    if (gibus_msg_is_call(m, PROPS_IFACE, "GetAll")) {
        if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_STRING, &iface, GIBUS_TYPE_INVALID))
            return gibus_msg_error(m, GIBUS_ERROR_INVALID_ARGS, "GetAll(s)");
        if (!(r = gibus_msg_return(m))) return NULL;
        gibus_iter_init_append(r, &it);
        gibus_iter_open(&it, GIBUS_TYPE_ARRAY, "{sv}", &dict);
        for (i = 0; i < count; i++) {
            gibus_iter_open(&dict, GIBUS_TYPE_DICT_ENTRY, NULL, &e);
            gibus_iter_append_basic(&e, GIBUS_TYPE_STRING, &names[i]);
            prop(&e, names[i]);
            gibus_iter_close(&dict, &e);
        }
        gibus_iter_close(&it, &dict);
        return r;
    }
    if (gibus_msg_is_call(m, PROPS_IFACE, "Get")) {
        if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_STRING, &iface,
                                GIBUS_TYPE_STRING, &name, GIBUS_TYPE_INVALID))
            return gibus_msg_error(m, GIBUS_ERROR_INVALID_ARGS, "Get(ss)");
        if (!(r = gibus_msg_return(m))) return NULL;
        gibus_iter_init_append(r, &it);
        if (prop(&it, name)) return r;
        gibus_msg_free(r);
        return gibus_msg_error(m, GIBUS_ERROR_UNKNOWN_PROPERTY, name);
    }
    return gibus_msg_error(m, GIBUS_ERROR_PROPERTY_READ_ONLY, "read only");
}

static gibus_msg *
menu_call(gibus_msg *m, const char *member)
{
    gibus_msg *r = NULL;
    gibus_iter in, out, arr;
    int32_t id = 0, depth = -1;
    int i;

    if (!SDL_strcmp(member, "GetLayout")) {
        if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_INT32, &id, GIBUS_TYPE_INT32, &depth,
                                GIBUS_TYPE_INVALID) || id < 0 || id > g_n)
            return gibus_msg_error(m, GIBUS_ERROR_INVALID_ARGS, "no such item");
        if (!(r = gibus_msg_return(m))) return NULL;
        uint32_t revision = 1;

        gibus_iter_init_append(r, &out);
        gibus_iter_append_basic(&out, GIBUS_TYPE_UINT32, &revision);
        put_layout(&out, id, depth);
    } else if (!SDL_strcmp(member, "GetGroupProperties")) {
        const int32_t *ids = NULL;
        int n = 0;

        if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_ARRAY, GIBUS_TYPE_INT32, &ids, &n,
                                GIBUS_TYPE_INVALID))
            n = 0;
        if (!(r = gibus_msg_return(m))) return NULL;
        gibus_iter_init_append(r, &out);
        gibus_iter_open(&out, GIBUS_TYPE_ARRAY, "(ia{sv})", &arr);
        for (i = 0; i < (n ? n : g_n + 1); i++) {
            id = n ? ids[i] : i;
            if (id >= 0 && id <= g_n) put_item(&arr, id);
        }
        gibus_iter_close(&out, &arr);
    } else if (!SDL_strcmp(member, "GetProperty")) {
        const char *name = NULL;
        item_out o;

        if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_INT32, &id, GIBUS_TYPE_STRING, &name,
                                GIBUS_TYPE_INVALID) || id < 0 || id > g_n)
            return gibus_msg_error(m, GIBUS_ERROR_INVALID_ARGS, "no such item");
        if (!(r = gibus_msg_return(m))) return NULL;
        gibus_iter_init_append(r, &out);
        o.it = &out;
        o.want = name;
        o.found = 0;
        item_props(&o, id);
        if (!o.found) {
            gibus_msg_free(r);
            return gibus_msg_error(m, GIBUS_ERROR_INVALID_ARGS, "no such property");
        }
    } else if (!SDL_strcmp(member, "Event")) {
        const char *what = NULL;

        if (!gibus_msg_get_args(m, NULL, GIBUS_TYPE_INT32, &id, GIBUS_TYPE_STRING, &what,
                                GIBUS_TYPE_INVALID))
            what = NULL;
        r = gibus_msg_return(m);
        if (what && !SDL_strcmp(what, "clicked")) {
            send_and_free(r);
            clicked(id);
            return NULL;
        }
    } else if (!SDL_strcmp(member, "EventGroup")) {
        gibus_iter evs, ev;
        int n = 0, chosen[TRAY_ITEMS];

        if (gibus_msg_has_signature(m, "a(isvu)") && gibus_iter_init(m, &in)) {
            gibus_iter_recurse(&in, &evs);
            while (gibus_iter_type(&evs) == GIBUS_TYPE_STRUCT) {
                const char *what = NULL;

                gibus_iter_recurse(&evs, &ev);
                gibus_iter_get_basic(&ev, &id);
                gibus_iter_next(&ev);
                if (gibus_iter_type(&ev) == GIBUS_TYPE_STRING)
                    gibus_iter_get_basic(&ev, &what);
                if (what && !SDL_strcmp(what, "clicked") && n < TRAY_ITEMS) chosen[n++] = id;
                gibus_iter_next(&evs);
            }
        }
        if ((r = gibus_msg_return(m))) {
            gibus_iter_init_append(r, &out);
            put_empty(&out, "i");
            send_and_free(r);
        }
        for (i = 0; i < n; i++) clicked(chosen[i]);
        return NULL;
    } else if (!SDL_strcmp(member, "AboutToShow")) {
        gibus_bool no = 0;

        if ((r = gibus_msg_return(m)))
            gibus_msg_append_args(r, GIBUS_TYPE_BOOLEAN, &no, GIBUS_TYPE_INVALID);
    } else if (!SDL_strcmp(member, "AboutToShowGroup")) {
        if ((r = gibus_msg_return(m))) {
            gibus_iter_init_append(r, &out);
            put_empty(&out, "i");
            put_empty(&out, "i");
        }
    } else {
        r = gibus_msg_error(m, GIBUS_ERROR_UNKNOWN_METHOD, member);
    }
    return r;
}

static int
on_message(gibus_conn *c, gibus_msg *m, void *menup)
{
    int menu = menup != NULL;
    const char *iface = gibus_msg_interface(m);
    const char *member = gibus_msg_member(m);
    gibus_msg *r = NULL;

    (void)c;
    if (gibus_msg_type(m) != GIBUS_MESSAGE_METHOD_CALL || !member)
        return GIBUS_NOT_YET_HANDLED;
    if (iface && !SDL_strcmp(iface, GIBUS_INTERFACE_INTROSPECTABLE)) {
        const char *xml = menu ? menu_xml : sni_xml;

        if ((r = gibus_msg_return(m)))
            gibus_msg_append_args(r, GIBUS_TYPE_STRING, &xml, GIBUS_TYPE_INVALID);
    } else if (iface && !SDL_strcmp(iface, PROPS_IFACE)) {
        r = properties(m, menu);
    } else if (menu) {
        r = menu_call(m, member);
    } else if (!SDL_strcmp(member, "Activate") || !SDL_strcmp(member, "SecondaryActivate") ||
               !SDL_strcmp(member, "ContextMenu") || !SDL_strcmp(member, "Scroll")) {
        r = gibus_msg_return(m);
    } else {
        r = gibus_msg_error(m, GIBUS_ERROR_UNKNOWN_METHOD, member);
    }
    send_and_free(r);
    return GIBUS_HANDLED;
}

static int
register_item(int block)
{
    gibus_msg *m = gibus_msg_call(WATCHER, "/StatusNotifierWatcher", WATCHER,
                                  "RegisterStatusNotifierItem");
    const char *name = g_name;
    gibus_msg *r;
    gibus_error err;

    if (!m) return 0;
    gibus_msg_append_args(m, GIBUS_TYPE_STRING, &name, GIBUS_TYPE_INVALID);
    if (!block) {
        send_and_free(m);
        return 1;
    }
    gibus_error_init(&err);
    r = gibus_call(g_bus, m, 2000, &err);
    gibus_msg_free(m);
    if (!r) {
        SDL_Log("tray: no StatusNotifierWatcher (%s)", err.message);
        gibus_error_free(&err);
        return 0;
    }
    gibus_msg_free(r);
    return 1;
}

static int
on_signal(gibus_conn *c, gibus_msg *m, void *user)
{
    const char *name = NULL, *was = NULL, *now = NULL;

    (void)c; (void)user;
    if (gibus_msg_is_signal(m, GIBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
        gibus_msg_get_args(m, NULL, GIBUS_TYPE_STRING, &name, GIBUS_TYPE_STRING, &was,
                           GIBUS_TYPE_STRING, &now, GIBUS_TYPE_INVALID) &&
        !SDL_strcmp(name, WATCHER) && now && now[0])
        register_item(0);
    return GIBUS_NOT_YET_HANDLED;
}

/* Menu callbacks run after dispatch, where closing the tray or pumping
   again is safe; one that closes it drops the rest. */
static void
pump(void)
{
    reaktor_tray_item chosen[TRAY_ITEMS];
    int on[TRAY_ITEMS], i, n;
    App *app = reaktor_main_app();
    Sint32 gen = g_gen;

    if (!g_bus) return;
    gibus_read_write(g_bus, 0);
    while (gibus_dispatch(g_bus))
        ;
    n = g_chosen_n;
    g_chosen_n = 0;
    SDL_memcpy(chosen, g_chosen, sizeof chosen[0] * (size_t)n);
    SDL_memcpy(on, g_chosen_on, sizeof on[0] * (size_t)n);
    for (i = 0; app && i < n && g_bus && g_gen == gen; i++) {
        chosen[i].chosen(app, on[i], chosen[i].user);
        reaktor_wake(app);
    }
}

static void
icons_make(void)
{
    static const int sizes[] = { 22, 32, 48, 64 };
    int i;

    for (i = g_icons = 0; i < (int)SDL_arraysize(sizes); i++) {
        SDL_Surface *s = reaktor_icon_surface(reaktor_launch_icon(), sizes[i]);
        Uint8 *out;

        if (!s) continue;
        if (s->w != sizes[i] || s->h != sizes[i] ||
            !(out = (Uint8 *)SDL_malloc((size_t)s->w * (size_t)s->h * 4))) {
            SDL_DestroySurface(s);
            continue;
        }
        if (!SDL_ConvertPixels(s->w, s->h, s->format, s->pixels, s->pitch,
                               SDL_PIXELFORMAT_ARGB32, out, s->w * 4)) {
            SDL_free(out);
            SDL_DestroySurface(s);
            continue;
        }
        g_icon[g_icons] = out;
        g_icon_px[g_icons++] = sizes[i];
        SDL_DestroySurface(s);
    }
}

static char *
label_dup(const char *s)
{
    char *c = reaktor_utf8_clean_dup(s), *out;
    size_t i, j;

    if (!c || !(out = (char *)SDL_malloc(SDL_strlen(c) * 2 + 1))) return c;
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
    const char *drv = SDL_GetCurrentVideoDriver();
    gibus_error err;
    int i, fd, owner;

    reaktor_tray_close(app);
    if (!spec) return 0;
    if (!drv || SDL_strcmp(drv, "dummy") == 0 || SDL_strcmp(drv, "offscreen") == 0)
        return 0;
    gibus_error_init(&err);
    if (!(g_bus = gibus_session_private(&err))) {
        SDL_Log("tray: %s", err.set && err.message[0] ? err.message : "no session bus");
        gibus_error_free(&err);
        return 0;
    }
    g_n = spec->count < 0 ? 0 : spec->count < TRAY_ITEMS ? spec->count : TRAY_ITEMS;
    for (i = 0; i < g_n; i++) {
        g_item[i]  = spec->items[i];
        g_on[i]    = spec->items[i].checkbox && spec->items[i].checked;
        g_label[i] = spec->items[i].label ? label_dup(spec->items[i].label) : NULL;
    }
    g_tip = spec->tooltip ? reaktor_utf8_clean_dup(spec->tooltip) : NULL;
    icons_make();
    SDL_snprintf(g_name, sizeof g_name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());
    owner = 0;
    if (gibus_register_path(g_bus, SNI_PATH, on_message, NULL) &&
        gibus_register_path(g_bus, MENU_PATH, on_message, (void *)1)) {
        owner = gibus_request_name(g_bus, g_name, GIBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) ==
                GIBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
    }
    if (!owner || !register_item(1)) {
        reaktor_tray_close(app);
        return 0;
    }
    gibus_add_filter(g_bus, on_signal, NULL);
    gibus_add_match(g_bus, "type='signal',sender='" GIBUS_SERVICE_DBUS "',"
                    "interface='" GIBUS_INTERFACE_DBUS "',member='NameOwnerChanged',"
                    "arg0='" WATCHER "'", NULL);
    gibus_flush(g_bus);
    if ((fd = gibus_fd(g_bus)) >= 0 && pipe(g_stop) == 0 &&
        (g_drained = SDL_CreateSemaphore(0))) {
        g_gen++;
        SDL_SetAtomicInt(&g_running, 1);
        g_thread = SDL_CreateThread(watch_bus, "reaktor-tray", (void *)(intptr_t)fd);
    }
    if (!g_thread) {
        reaktor_tray_close(app);
        return 0;
    }
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
    pump();
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
    if (g_bus) gibus_unref(g_bus);
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
        gibus_conn *c = g_bus;
        SDL_Semaphore *drained = g_drained;

        if (!c) return;
        pump();
        if (g_bus == c && g_drained == drained && e->user.code == g_gen)
            SDL_SignalSemaphore(drained);
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
