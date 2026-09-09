/* a11y_web.c - phase 4a of docs/ACCESSIBILITY.md: the tree, served to a
 * browser.
 *
 * A canvas is invisible to assistive technology, but a DOM subtree beside it
 * is not. So the shadow tree is mirrored into hidden elements - one per node,
 * with the ARIA role, name and states, positioned absolutely over the canvas
 * so a magnifier or a touch reader finds each node where it is drawn - and
 * the browser exposes that to every screen reader on every platform at once.
 *
 * The mirror is rebuilt in tree order whenever the frame reported a change,
 * and left alone otherwise. Rebuilding is one appendChild per node, which on
 * an element already in the DOM is a move and not a recreation: identity,
 * and so a reader's place, survives it. Attributes are compared before they
 * are set, so a node that did not change makes no mutation and the reader
 * hears nothing about it.
 *
 * Focus is not moved in the DOM - the canvas keeps it, or keys would stop
 * reaching the app - but announced through aria-activedescendant on the
 * canvas, which is what the ARIA application pattern is for. A reader that
 * presses a node or moves to one is answered through two exported functions,
 * which the shell turns into the same click and focus a key would make.
 *
 * Off the web this file stands aside: Windows, the free desktops and macOS
 * each have a bridge of their own under src/sys/. What is left here is the
 * no-op for a platform with none of them - see the #elif at the foot. */
#include "a11y.h"

static reaktor_a11y_action g_activate, g_focus;
static void *g_user;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE void
reaktor_a11y_web_activate(unsigned id)
{
    if (g_activate) g_activate(g_user, id);
}

EMSCRIPTEN_KEEPALIVE void
reaktor_a11y_web_focus(unsigned id)
{
    if (g_focus) g_focus(g_user, id);
}

EM_JS(void, web_a11y_init, (void), {
    var canvas = Module['canvas'];
    var root = document.createElement('div');
    root.id = 'a11y';
    /* Over the canvas, invisible, and no target for the pointer: a press
       has to reach the canvas underneath. opacity rather than display or
       visibility, which would hide it from the reader as well. */
    root.style.cssText = 'position:absolute;left:0;top:0;width:0;height:0;' +
        'overflow:visible;pointer-events:none;opacity:0;';
    document.body.appendChild(root);
    canvas.setAttribute('role', 'application');
    canvas.setAttribute('aria-label', 'Reaktor');
    canvas.setAttribute('aria-owns', 'a11y');
    Module['a11y'] = { root: root, nodes: {}, gen: 0 };
});

EM_JS(void, web_a11y_begin, (void), {
    var a = Module['a11y']; if (!a) return;
    var r = Module['canvas'].getBoundingClientRect();
    a.root.style.left = (r.left + window.scrollX) + 'px';
    a.root.style.top  = (r.top + window.scrollY) + 'px';
    a.gen++;
});

EM_JS(void, web_a11y_node, (unsigned id, unsigned parent, const char *role,
                            const char *name, const char *value,
                            const char *keys, unsigned state, float x,
                            float y, float w, float h, float num, float lo,
                            float hi), {
    var a = Module['a11y']; if (!a) return;
    var el = a.nodes[id];
    if (!el) {
        el = document.createElement('div');
        el.id = 'a11y-' + id;
        el.style.cssText = 'position:absolute;pointer-events:none;';
        el.addEventListener('click', function () {
            Module['_reaktor_a11y_web_activate'](id);
        });
        el.addEventListener('focus', function () {
            Module['_reaktor_a11y_web_focus'](id);
        });
        a.nodes[id] = el;
    }
    el.dataset.gen = a.gen;

    var set = function (k, v) {
        if (v === null) { if (el.hasAttribute(k)) el.removeAttribute(k); }
        else if (el.getAttribute(k) !== v) el.setAttribute(k, v);
    };
    var r = UTF8ToString(role), n = UTF8ToString(name), v = UTF8ToString(value),
        k = UTF8ToString(keys);
    var CHECKED = 2, EXPANDED = 4, SELECTED = 8, DISABLED = 16, READONLY = 32,
        VOLATILE = 128;

    /* Reaktor's vocabulary is the one every platform shares; three names
       differ in ARIA. A label has no role - it is text. */
    var aria = r === 'progress' ? 'progressbar'
             : r === 'listitem' ? 'option'
             : r === 'window'   ? 'group'
             : r === 'label'    ? null : r;
    set('role', aria);
    /* A focusable node is focusable for the reader, not for Tab: the app
       owns Tab, and this keeps the mirror out of the page's own sequence. */
    var focusable = { button: 1, link: 1, checkbox: 1, radio: 1, textbox: 1,
                      slider: 1, spinbutton: 1, combobox: 1, option: 1,
                      treeitem: 1, menuitem: 1, tab: 1 };
    set('tabindex', aria && focusable[aria] ? '-1' : null);

    /* The name is read from content for text and from aria-label for a
       control; a field's content is its value. */
    /* A reading is a name and a value; roleless, it is text, and the two
       halves are joined the way they are drawn. Each platform composes it
       its own way - the tree keeps them apart. */
    var text = aria === 'textbox' ? v
             : aria ? '' : (v ? n + ': ' + v : n);
    if (el.textContent !== text) el.textContent = text;
    set('aria-label', aria && n ? n : null);

    var checkable = aria === 'checkbox' || aria === 'radio';
    set('aria-checked', checkable ? ((state & CHECKED) ? 'true' : 'false') : null);
    var expandable = aria === 'treeitem' || aria === 'combobox';
    set('aria-expanded', expandable ? ((state & EXPANDED) ? 'true' : 'false') : null);
    var selectable = aria === 'tab' || aria === 'option';
    set('aria-selected', selectable ? ((state & SELECTED) ? 'true' : 'false') : null);
    set('aria-disabled', (state & DISABLED) ? 'true' : null);
    set('aria-readonly', (state & READONLY) ? 'true' : null);
    /* Off is the default for anything that is not a live region, but a
       reading that changes every frame is exactly what someone would be
       tempted to make one, so the intent is written down. */
    set('aria-live', (state & VOLATILE) ? 'off' : null);
    var valued = aria === 'slider' || aria === 'spinbutton' ||
                 aria === 'progressbar';
    set('aria-valuetext', valued && v ? v : null);
    /* From the numbers the widget reported, not from parsing the text back
       out of the value - "40" and "0.65" happened to parse, and "3 of 8"
       would not have. hi > lo is how a node says it has a range at all. */
    var ranged = valued && hi > lo;
    set('aria-valuenow', ranged ? String(num) : null);
    set('aria-valuemin', ranged ? String(lo) : null);
    set('aria-valuemax', ranged ? String(hi) : null);
    /* Announced, never bound - the app owns the key, and ARIA says so in as
       many words. A space-separated list, which is how one action carries
       both Control+1 and Meta+1. */
    set('aria-keyshortcuts', k ? k : null);

    el.style.left = x + 'px'; el.style.top = y + 'px';
    el.style.width = w + 'px'; el.style.height = h + 'px';

    /* Appended in tree order every time, which is a move for an element
       already placed and so keeps reading order equal to draw order. */
    var p = parent ? a.nodes[parent] : a.root;
    if (!p) p = a.root;
    if (el.parentNode !== p || p.lastChild !== el) p.appendChild(el);
});

EM_JS(void, web_a11y_end, (unsigned focus), {
    var a = Module['a11y']; if (!a) return;
    for (var id in a.nodes) {
        var el = a.nodes[id];
        if (el.dataset.gen != a.gen) { el.remove(); delete a.nodes[id]; }
    }
    var canvas = Module['canvas'];
    var want = focus ? 'a11y-' + focus : null;
    if (want === null) canvas.removeAttribute('aria-activedescendant');
    else if (canvas.getAttribute('aria-activedescendant') !== want)
        canvas.setAttribute('aria-activedescendant', want);
});

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    g_activate = activate;
    g_focus    = focus;
    g_user     = user;
    web_a11y_init();
}

void
reaktor_a11y_platform_drain(void)
{
    /* The browser calls the exported functions on the main thread, so the
     * action has already run by the time anything asks. */
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    static unsigned last_focus;
    int n, m, i;
    const reaktor_a11y_node *t = reaktor_a11y_tree(a, &n);

    reaktor_a11y_changes(a, &m);
    if (m == 0 && focus_id == last_focus) return;
    last_focus = focus_id;

    web_a11y_begin();
    for (i = 0; i < n; i++) {
        const reaktor_a11y_node *nd = &t[i];
        web_a11y_node(nd->id, nd->parent, reaktor_a11y_role_name(nd->role),
                      nd->name ? nd->name : "", nd->value ? nd->value : "",
                      nd->keys ? nd->keys : "",
                      nd->state, nd->bounds.x, nd->bounds.y, nd->bounds.w,
                      nd->bounds.h, nd->num, nd->lo, nd->hi);
    }
    web_a11y_end(focus_id);
}

/* Windows has src/sys/a11y_win32.c, a free desktop with a dbus to build
 * against has src/sys/a11y_atspi.c, and macOS has src/sys/a11y_macos.m.
 * Anything else - a Linux or BSD without dbus - falls through to the no-op
 * below, which is also what a desktop with no accessibility stack running
 * amounts to. */
#elif !defined(_WIN32) && !defined(REAKTOR_HAVE_ATSPI) &&       !defined(REAKTOR_HAVE_NSACCESSIBILITY)

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    g_activate = activate;
    g_focus    = focus;
    g_user     = user;
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    (void)a; (void)focus_id;
}

void
reaktor_a11y_platform_drain(void)
{
}

#endif
