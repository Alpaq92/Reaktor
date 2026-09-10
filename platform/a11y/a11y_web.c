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

    var aria = r === 'progress' ? 'progressbar'
             : r === 'listitem' ? 'option'
             : r === 'window'   ? 'group'
             : r === 'label'    ? null : r;
    set('role', aria);
    var focusable = { button: 1, link: 1, checkbox: 1, radio: 1, textbox: 1,
                      slider: 1, spinbutton: 1, combobox: 1, option: 1,
                      treeitem: 1, menuitem: 1, tab: 1 };
    set('tabindex', aria && focusable[aria] ? '-1' : null);

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
    set('aria-live', (state & VOLATILE) ? 'off' : null);
    var valued = aria === 'slider' || aria === 'spinbutton' ||
                 aria === 'progressbar';
    set('aria-valuetext', valued && v ? v : null);
    var ranged = valued && hi > lo;
    set('aria-valuenow', ranged ? String(num) : null);
    set('aria-valuemin', ranged ? String(lo) : null);
    set('aria-valuemax', ranged ? String(hi) : null);
    set('aria-keyshortcuts', k ? k : null);

    el.style.left = x + 'px'; el.style.top = y + 'px';
    el.style.width = w + 'px'; el.style.height = h + 'px';

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
