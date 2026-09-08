/* a11y_win32.c - phase 4b of docs/ACCESSIBILITY.md: the tree, served to
 * Windows UI Automation.
 *
 * UIA is a client/server protocol. A provider answers questions about elements
 * - what are your children, what is your name, what is your bounding rectangle
 * - and raises events when they change. Narrator, Magnifier, Voice Access and
 * every automation tool are clients of it.
 *
 * Three things make this different from the web bridge:
 *
 * **It is pulled, not pushed.** The DOM mirror is written once per changed
 * frame and the browser reads it whenever it likes. UIA instead calls back
 * into this process, and it does so **on another thread** - an RPC thread the
 * app never created and cannot block - while the main thread may be asleep in
 * SDL_WaitEvent. So the tree cannot be read where it lives: the frame rewrites
 * it, and a client walking it mid-frame would see half of one page and half of
 * another. The snapshot below is the answer. A drawn frame copies the tree
 * into it under a lock, strings and all, because the model's own strings live
 * in an arena that the next frame reuses.
 *
 * **COM from C.** An interface is a struct whose first member is a pointer to
 * a table of function pointers, and an object that implements three of them
 * embeds three such structs so QueryInterface can hand out an interior pointer
 * for each. Every method recovers the object from the interface pointer it was
 * called on. It is mechanical, and it is all this file's bulk.
 *
 * **Identity has to be stable.** A client holds a provider across frames and
 * compares elements by runtime id. The model already gives every node an id
 * that survives a redraw (see a11y.c), so a provider is a wrapper around one
 * of those numbers and nothing else; it reads the snapshot afresh on every
 * call, and a node that has gone answers with nothing rather than with stale
 * geometry.
 *
 * Value-only to start, as ACCESSIBILITY.md suggests: a text field reports its
 * text through IValueProvider, and ITextProvider - which is what a client
 * needs for caret and range queries - is not here yet. */
#include "a11y.h"

#ifdef _WIN32

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <SDL3/SDL.h>

/* --- the snapshot -------------------------------------------------------
 *
 * A copy of the last drawn frame's tree, taken under the lock the provider
 * reads it under. Fixed arrays for the same reason the model uses them: this
 * runs in the frame, and a frame does not allocate. */

typedef struct snap_node {
    unsigned      id, parent;
    unsigned char role;
    unsigned char level;            /* depth; the tie-break when hit testing */
    unsigned      state;
    int           name, value;      /* offsets into `str`, -1 for none */
    float         x, y, w, h;       /* window coordinates */
} snap_node;

#define SNAP_POOL (CURIE_A11Y_POOL * 2)

static struct {
    CRITICAL_SECTION lock;
    int              ready;

    snap_node        node[CURIE_A11Y_MAX_NODES];
    int              count;
    char             str[SNAP_POOL];
    int              str_used;
    unsigned         focus;

    HWND             hwnd;
    WNDPROC          prev_proc;
    /* Whether a client has ever asked for the tree. Until one has, the frame
     * still fills the snapshot - it is a few microseconds and it keeps the
     * first answer instant - but no event is raised, because raising one with
     * no client listening walks the whole tree for nothing. */
    int              wanted;

    curie_a11y_action activate, focus_action;
    void             *user;

    /* What a client asked for, waiting for the thread that owns the tree.
     * Written under the lock from UIA's RPC thread, read and cleared by
     * curie_a11y_platform_drain on the main one. One of each is enough: a
     * second request before the first is served is the client changing its
     * mind, and the last one is what it wants. */
    unsigned          want_focus, want_activate;
    Uint32            wake;        /* the event that gets the loop looking */
} g;

static int
snap_intern(const char *s)
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

static const char *
snap_str(int at)
{
    return at < 0 ? NULL : g.str + at;
}

/* Index of a node by id, or -1. Linear, because a client asks about one
 * element at a time and the tree is under three hundred entries; the model's
 * own hash is for the per-frame diff, which is quadratic without it. */
static int
snap_find(unsigned id)
{
    int i;

    for (i = 0; i < g.count; i++)
        if (g.node[i].id == id) return i;
    return -1;
}

/* --- roles ---------------------------------------------------------------
 * Curie's vocabulary onto UIA's. Every one of these is a control type UIA has
 * had since Windows 7; nothing here needs a newer client. */
static long
control_type(unsigned char role)
{
    switch (role) {
    case CURIE_A11Y_WINDOW:     return UIA_WindowControlTypeId;
    case CURIE_A11Y_GROUP:      return UIA_GroupControlTypeId;
    case CURIE_A11Y_TABLIST:    return UIA_TabControlTypeId;
    case CURIE_A11Y_TAB:        return UIA_TabItemControlTypeId;
    case CURIE_A11Y_BUTTON:     return UIA_ButtonControlTypeId;
    case CURIE_A11Y_LINK:       return UIA_HyperlinkControlTypeId;
    case CURIE_A11Y_CHECKBOX:   return UIA_CheckBoxControlTypeId;
    case CURIE_A11Y_RADIO:      return UIA_RadioButtonControlTypeId;
    case CURIE_A11Y_TEXTBOX:    return UIA_EditControlTypeId;
    case CURIE_A11Y_SLIDER:     return UIA_SliderControlTypeId;
    case CURIE_A11Y_SPINBUTTON: return UIA_SpinnerControlTypeId;
    case CURIE_A11Y_PROGRESS:   return UIA_ProgressBarControlTypeId;
    case CURIE_A11Y_COMBOBOX:   return UIA_ComboBoxControlTypeId;
    case CURIE_A11Y_LISTITEM:   return UIA_ListItemControlTypeId;
    case CURIE_A11Y_TREEITEM:   return UIA_TreeItemControlTypeId;
    case CURIE_A11Y_MENUBAR:    return UIA_MenuBarControlTypeId;
    case CURIE_A11Y_MENU:       return UIA_MenuControlTypeId;
    case CURIE_A11Y_MENUITEM:   return UIA_MenuItemControlTypeId;
    case CURIE_A11Y_DIALOG:     return UIA_WindowControlTypeId;
    case CURIE_A11Y_LABEL:      return UIA_TextControlTypeId;
    default:                    return UIA_CustomControlTypeId;
    }
}

/* Which nodes a client can reach with the keyboard - the same set the shell
 * tabs through, which is the point: what UIA reports focusable is what
 * Tab actually reaches. */
static int
keyboard_focusable(unsigned char role, unsigned state)
{
    if (state & CURIE_A11Y_DISABLED) return 0;
    switch (role) {
    case CURIE_A11Y_TAB:      case CURIE_A11Y_BUTTON:   case CURIE_A11Y_LINK:
    case CURIE_A11Y_CHECKBOX: case CURIE_A11Y_RADIO:    case CURIE_A11Y_TEXTBOX:
    case CURIE_A11Y_SLIDER:   case CURIE_A11Y_SPINBUTTON:
    case CURIE_A11Y_COMBOBOX: case CURIE_A11Y_LISTITEM: case CURIE_A11Y_TREEITEM:
    case CURIE_A11Y_MENUITEM:
        return 1;
    default:
        return 0;
    }
}

/* --- the provider object -------------------------------------------------
 *
 * One object per element a client is holding. Three interfaces are embedded
 * rather than inherited - C has no inheritance - so each has its own vtable
 * pointer at its own offset, and QueryInterface hands out the address of the
 * member. Every method starts by recovering the object from whichever member
 * it was called through. */

typedef struct Provider {
    IRawElementProviderSimple       simple;
    IRawElementProviderFragment     fragment;
    IRawElementProviderFragmentRoot root;
    IInvokeProvider                 invoke;
    IValueProvider                  value;
    LONG                            ref;
    /* The node this stands for. Zero is the fragment root: the window itself,
     * which is the one element that exists whether or not a frame has been
     * drawn. */
    unsigned                        id;
} Provider;

static Provider *provider_new(unsigned id);

#define FROM_SIMPLE(p)   ((Provider *)((char *)(p) - offsetof(Provider, simple)))
#define FROM_FRAGMENT(p) ((Provider *)((char *)(p) - offsetof(Provider, fragment)))
#define FROM_ROOT(p)     ((Provider *)((char *)(p) - offsetof(Provider, root)))
#define FROM_INVOKE(p)   ((Provider *)((char *)(p) - offsetof(Provider, invoke)))
#define FROM_VALUE(p)    ((Provider *)((char *)(p) - offsetof(Provider, value)))

/* The root stands for the window and answers even before the first frame; any
 * other id has to be in the snapshot to answer at all. */
static int
provider_is_root(const Provider *p)
{
    return p->id == 0;
}

static HRESULT
provider_qi(Provider *p, REFIID iid, void **out)
{
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) ||
        IsEqualIID(iid, &IID_IRawElementProviderSimple))
        *out = &p->simple;
    else if (IsEqualIID(iid, &IID_IRawElementProviderFragment))
        *out = &p->fragment;
    else if (IsEqualIID(iid, &IID_IRawElementProviderFragmentRoot) &&
             provider_is_root(p))
        *out = &p->root;
    else if (IsEqualIID(iid, &IID_IInvokeProvider)) {
        int i = snap_find(p->id);
        if (i < 0 || !(g.node[i].role == CURIE_A11Y_BUTTON ||
                       g.node[i].role == CURIE_A11Y_LINK ||
                       g.node[i].role == CURIE_A11Y_MENUITEM ||
                       g.node[i].role == CURIE_A11Y_TAB))
            return E_NOINTERFACE;
        *out = &p->invoke;
    } else if (IsEqualIID(iid, &IID_IValueProvider)) {
        int i = snap_find(p->id);
        if (i < 0 || g.node[i].value < 0) return E_NOINTERFACE;
        *out = &p->value;
    } else {
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&p->ref);
    return S_OK;
}

static ULONG
provider_release(Provider *p)
{
    LONG n = InterlockedDecrement(&p->ref);

    if (n == 0) free(p);
    return (ULONG)n;
}

/* Each interface repeats IUnknown, because each has its own vtable and a
 * client may hold any one of them. They all reach the same refcount. */
#define IUNKNOWN_FOR(NAME, IFACE, RECOVER)                                    \
    static HRESULT STDMETHODCALLTYPE                                          \
    NAME##_QueryInterface(IFACE *self, REFIID iid, void **out)                \
    { return provider_qi(RECOVER(self), iid, out); }                          \
    static ULONG STDMETHODCALLTYPE                                            \
    NAME##_AddRef(IFACE *self)                                                \
    { return (ULONG)InterlockedIncrement(&RECOVER(self)->ref); }              \
    static ULONG STDMETHODCALLTYPE                                            \
    NAME##_Release(IFACE *self)                                               \
    { return provider_release(RECOVER(self)); }

IUNKNOWN_FOR(simple,   IRawElementProviderSimple,       FROM_SIMPLE)
IUNKNOWN_FOR(fragment, IRawElementProviderFragment,     FROM_FRAGMENT)
IUNKNOWN_FOR(root,     IRawElementProviderFragmentRoot, FROM_ROOT)
IUNKNOWN_FOR(invoke,   IInvokeProvider,                 FROM_INVOKE)
IUNKNOWN_FOR(value,    IValueProvider,                  FROM_VALUE)

/* --- IRawElementProviderSimple ------------------------------------------ */

static HRESULT STDMETHODCALLTYPE
simple_get_ProviderOptions(IRawElementProviderSimple *self,
                           enum ProviderOptions *out)
{
    (void)self;
    /* Server-side: the provider runs in the application's own process, which
     * is what lets it answer without a cross-process hop per property.
     * UseComThreading makes UIA marshal calls onto this object's apartment
     * rather than calling it on an arbitrary RPC thread. */
    *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
simple_GetPatternProvider(IRawElementProviderSimple *self, PATTERNID pattern,
                          IUnknown **out)
{
    Provider *p = FROM_SIMPLE(self);
    REFIID iid = pattern == UIA_InvokePatternId ? &IID_IInvokeProvider
               : pattern == UIA_ValuePatternId  ? &IID_IValueProvider
               : NULL;

    *out = NULL;
    if (!iid) return S_OK;
    /* A pattern the element does not support is not an error: UIA asks every
     * element about every pattern and reads null as "does not have it". */
    if (FAILED(provider_qi(p, iid, (void **)out))) *out = NULL;
    return S_OK;
}

static void
str_variant(VARIANT *v, const char *utf8)
{
    int n;
    WCHAR *w;

    VariantInit(v);
    if (!utf8) return;
    n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) return;
    w = (WCHAR *)malloc((size_t)n * sizeof(WCHAR));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocString(w);
    free(w);
    if (!v->bstrVal) v->vt = VT_EMPTY;
}

static void
bool_variant(VARIANT *v, int b)
{
    VariantInit(v);
    v->vt = VT_BOOL;
    v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
}

static HRESULT STDMETHODCALLTYPE
simple_GetPropertyValue(IRawElementProviderSimple *self, PROPERTYID prop,
                        VARIANT *out)
{
    Provider *p = FROM_SIMPLE(self);
    snap_node n;
    int i;

    VariantInit(out);
    EnterCriticalSection(&g.lock);
    if (provider_is_root(p)) {
        LeaveCriticalSection(&g.lock);
        /* if rather than switch throughout: the SDK spells a property id
         * `const long`, which C does not accept as a case label. */
        if (prop == UIA_NamePropertyId) {
            str_variant(out, "Curie");
        } else if (prop == UIA_ControlTypePropertyId) {
            out->vt = VT_I4;
            out->lVal = UIA_WindowControlTypeId;
        } else if (prop == UIA_IsControlElementPropertyId ||
                   prop == UIA_IsContentElementPropertyId) {
            bool_variant(out, 1);
        }
        return S_OK;
    }
    i = snap_find(p->id);
    if (i < 0) { LeaveCriticalSection(&g.lock); return S_OK; }
    n = g.node[i];
    /* Copied out of the snapshot before the lock goes, so the string work
     * below - which allocates - happens with the frame free to run. */
    {
        const char *name = snap_str(n.name), *value = snap_str(n.value);
        char nbuf[256], vbuf[256];

        if (name)  { strncpy(nbuf, name, sizeof(nbuf) - 1); nbuf[sizeof(nbuf)-1] = 0; }
        if (value) { strncpy(vbuf, value, sizeof(vbuf) - 1); vbuf[sizeof(vbuf)-1] = 0; }
        LeaveCriticalSection(&g.lock);

        if (prop == UIA_NamePropertyId) {
            str_variant(out, name ? nbuf : NULL);
        } else if (prop == UIA_ControlTypePropertyId) {
            out->vt = VT_I4;
            out->lVal = control_type(n.role);
        } else if (prop == UIA_AutomationIdPropertyId) {
            char id[24];
            /* The model's id, which survives a redraw - so a test can name an
             * element and find it again on the next frame. */
            sprintf(id, "%u", n.id);
            str_variant(out, id);
        } else if (prop == UIA_IsEnabledPropertyId) {
            bool_variant(out, !(n.state & CURIE_A11Y_DISABLED));
        } else if (prop == UIA_IsKeyboardFocusablePropertyId) {
            bool_variant(out, keyboard_focusable(n.role, n.state));
        } else if (prop == UIA_HasKeyboardFocusPropertyId) {
            bool_variant(out, (n.state & CURIE_A11Y_FOCUSED) != 0);
        } else if (prop == UIA_IsOffscreenPropertyId) {
            bool_variant(out, (n.state & CURIE_A11Y_OFFSCREEN) != 0);
        } else if (prop == UIA_IsControlElementPropertyId ||
                   prop == UIA_IsContentElementPropertyId) {
            bool_variant(out, 1);
        } else if (prop == UIA_ValueValuePropertyId) {
            str_variant(out, value ? vbuf : NULL);
        /* The last two are a pattern's properties, and a client that asks
         * through the pattern will not reach here until IToggleProvider and
         * ISelectionItemProvider exist. One that asks for the property
         * directly does, which several do, so they are answered. */
        } else if (prop == UIA_ToggleToggleStatePropertyId) {
            if (n.role == CURIE_A11Y_CHECKBOX || n.role == CURIE_A11Y_RADIO) {
                out->vt = VT_I4;
                out->lVal = (n.state & CURIE_A11Y_CHECKED) ? ToggleState_On
                                                           : ToggleState_Off;
            }
        } else if (prop == UIA_SelectionItemIsSelectedPropertyId) {
            if (n.role == CURIE_A11Y_TAB || n.role == CURIE_A11Y_LISTITEM)
                bool_variant(out, (n.state & CURIE_A11Y_SELECTED) != 0);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
simple_get_HostRawElementProvider(IRawElementProviderSimple *self,
                                  IRawElementProviderSimple **out)
{
    Provider *p = FROM_SIMPLE(self);

    *out = NULL;
    /* Only the root has a host: the HWND provider, which supplies everything
     * a window has by virtue of being a window - its rectangle, its process,
     * its runtime id. A child element has no window of its own. */
    if (!provider_is_root(p)) return S_OK;
    return UiaHostProviderFromHwnd(g.hwnd, out);
}

static IRawElementProviderSimpleVtbl g_simple_vtbl = {
    simple_QueryInterface, simple_AddRef, simple_Release,
    simple_get_ProviderOptions, simple_GetPatternProvider,
    simple_GetPropertyValue, simple_get_HostRawElementProvider
};

/* --- IRawElementProviderFragment ---------------------------------------- */

/* The first child of `parent`, or the sibling of `from` in `dir`. Everything
 * navigation needs is one pass over the snapshot, because the tree is stored
 * flat in draw order and draw order is document order. */
static unsigned
nav_child(unsigned parent, int last)
{
    unsigned found = 0;
    int i;

    for (i = 0; i < g.count; i++)
        if (g.node[i].parent == parent) {
            found = g.node[i].id;
            if (!last) break;
        }
    return found;
}

static unsigned
nav_sibling(unsigned id, int back)
{
    int i = snap_find(id), k, seen = 0;
    unsigned parent, prev = 0;

    if (i < 0) return 0;
    parent = g.node[i].parent;
    /* One pass, both directions: the node before it in the parent's run, or
     * the one after. Draw order is document order, so the run is the order a
     * client should see. */
    for (k = 0; k < g.count; k++) {
        if (g.node[k].parent != parent) continue;
        if (g.node[k].id == id) {
            if (back) return prev;
            seen = 1;
            continue;
        }
        if (seen) return g.node[k].id;
        prev = g.node[k].id;
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE
fragment_Navigate(IRawElementProviderFragment *self,
                  enum NavigateDirection dir, IRawElementProviderFragment **out)
{
    Provider *p = FROM_FRAGMENT(self);
    unsigned to = 0;
    int i;

    *out = NULL;
    EnterCriticalSection(&g.lock);
    if (provider_is_root(p)) {
        /* The root's children are the model's roots - nodes with no parent. */
        if (dir == NavigateDirection_FirstChild) to = nav_child(0, 0);
        else if (dir == NavigateDirection_LastChild) to = nav_child(0, 1);
    } else if ((i = snap_find(p->id)) >= 0) {
        switch (dir) {
        case NavigateDirection_Parent:        to = g.node[i].parent; break;
        case NavigateDirection_FirstChild:    to = nav_child(p->id, 0); break;
        case NavigateDirection_LastChild:     to = nav_child(p->id, 1); break;
        case NavigateDirection_NextSibling:   to = nav_sibling(p->id, 0); break;
        case NavigateDirection_PreviousSibling: to = nav_sibling(p->id, 1); break;
        default: break;
        }
        /* A node whose parent is 0 is a child of the fragment root, and the
         * root is what Parent has to answer with - not nothing. */
        if (dir == NavigateDirection_Parent && to == 0) {
            LeaveCriticalSection(&g.lock);
            {
                Provider *r = provider_new(0);
                if (!r) return E_OUTOFMEMORY;
                *out = &r->fragment;
            }
            return S_OK;
        }
    }
    LeaveCriticalSection(&g.lock);

    if (!to) return S_OK;
    {
        Provider *q = provider_new(to);
        if (!q) return E_OUTOFMEMORY;
        *out = &q->fragment;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
fragment_GetRuntimeId(IRawElementProviderFragment *self, SAFEARRAY **out)
{
    Provider *p = FROM_FRAGMENT(self);
    int data[2];
    SAFEARRAY *a;
    LONG k;

    *out = NULL;
    /* The root has none of its own: it defers to the HWND provider, which is
     * what makes the window identify as this window. */
    if (provider_is_root(p)) return S_OK;

    data[0] = UiaAppendRuntimeId;
    data[1] = (int)p->id;
    a = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!a) return E_OUTOFMEMORY;
    for (k = 0; k < 2; k++) SafeArrayPutElement(a, &k, &data[k]);
    *out = a;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
fragment_get_BoundingRectangle(IRawElementProviderFragment *self,
                               struct UiaRect *out)
{
    Provider *p = FROM_FRAGMENT(self);
    POINT origin;
    int i;

    out->left = out->top = out->width = out->height = 0.0;
    /* The root's rectangle comes from the HWND provider, so an empty one here
     * is the right answer rather than a missing one. */
    if (provider_is_root(p)) return S_OK;

    EnterCriticalSection(&g.lock);
    i = snap_find(p->id);
    if (i >= 0) {
        out->left = g.node[i].x; out->top = g.node[i].y;
        out->width = g.node[i].w; out->height = g.node[i].h;
    }
    LeaveCriticalSection(&g.lock);
    if (i < 0) return S_OK;

    /* The model works in window coordinates; UIA wants the screen's. */
    origin.x = origin.y = 0;
    if (ClientToScreen(g.hwnd, &origin)) {
        out->left += origin.x;
        out->top  += origin.y;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
fragment_GetEmbeddedFragmentRoots(IRawElementProviderFragment *self,
                                  SAFEARRAY **out)
{
    (void)self;
    *out = NULL;   /* one fragment, one root */
    return S_OK;
}

/* A request from the client's thread, left for the app's. Waking the loop is
 * the other half: with nothing else happening it is asleep in SDL_WaitEvent,
 * and an id recorded and never looked at is a click that does nothing. */
static void
request(unsigned *slot, unsigned id)
{
    SDL_Event e;

    EnterCriticalSection(&g.lock);
    *slot = id;
    LeaveCriticalSection(&g.lock);

    SDL_zero(e);
    e.type = g.wake;
    SDL_PushEvent(&e);
}

static HRESULT STDMETHODCALLTYPE
fragment_SetFocus(IRawElementProviderFragment *self)
{
    Provider *p = FROM_FRAGMENT(self);

    if (!provider_is_root(p)) request(&g.want_focus, p->id);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
fragment_get_FragmentRoot(IRawElementProviderFragment *self,
                          IRawElementProviderFragmentRoot **out)
{
    Provider *r;

    (void)self;
    *out = NULL;
    r = provider_new(0);
    if (!r) return E_OUTOFMEMORY;
    *out = &r->root;
    return S_OK;
}

static IRawElementProviderFragmentVtbl g_fragment_vtbl = {
    fragment_QueryInterface, fragment_AddRef, fragment_Release,
    fragment_Navigate, fragment_GetRuntimeId, fragment_get_BoundingRectangle,
    fragment_GetEmbeddedFragmentRoots, fragment_SetFocus,
    fragment_get_FragmentRoot
};

/* --- IRawElementProviderFragmentRoot ------------------------------------ */

static HRESULT STDMETHODCALLTYPE
root_ElementProviderFromPoint(IRawElementProviderFragmentRoot *self,
                              double x, double y,
                              IRawElementProviderFragment **out)
{
    POINT origin;
    unsigned hit = 0;
    unsigned char best = 0;
    int i;

    (void)self;
    *out = NULL;
    origin.x = origin.y = 0;
    if (!ClientToScreen(g.hwnd, &origin)) return S_OK;
    x -= origin.x;
    y -= origin.y;

    EnterCriticalSection(&g.lock);
    /* The deepest node containing the point, and among equals the last drawn.
     * Depth first, not draw order alone: a group's bounds cover its children,
     * and the tab strip has two groups over the same band - taking the last
     * containing node answered with the group beside the tabs rather than
     * with the tab. UIA wants the innermost element. */
    for (i = 0; i < g.count; i++) {
        const snap_node *n = &g.node[i];

        if (n->state & CURIE_A11Y_OFFSCREEN) continue;
        if (x < n->x || x >= n->x + n->w) continue;
        if (y < n->y || y >= n->y + n->h) continue;
        if (!hit || n->level >= best) { hit = n->id; best = n->level; }
    }
    LeaveCriticalSection(&g.lock);

    if (!hit) return S_OK;
    {
        Provider *q = provider_new(hit);
        if (!q) return E_OUTOFMEMORY;
        *out = &q->fragment;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
root_GetFocus(IRawElementProviderFragmentRoot *self,
              IRawElementProviderFragment **out)
{
    unsigned id;

    (void)self;
    *out = NULL;
    EnterCriticalSection(&g.lock);
    id = g.focus && snap_find(g.focus) >= 0 ? g.focus : 0;
    LeaveCriticalSection(&g.lock);
    if (!id) return S_OK;
    {
        Provider *q = provider_new(id);
        if (!q) return E_OUTOFMEMORY;
        *out = &q->fragment;
    }
    return S_OK;
}

static IRawElementProviderFragmentRootVtbl g_root_vtbl = {
    root_QueryInterface, root_AddRef, root_Release,
    root_ElementProviderFromPoint, root_GetFocus
};

/* --- IInvokeProvider, IValueProvider ------------------------------------ */

static HRESULT STDMETHODCALLTYPE
invoke_Invoke(IInvokeProvider *self)
{
    request(&g.want_activate, FROM_INVOKE(self)->id);
    return S_OK;
}

static IInvokeProviderVtbl g_invoke_vtbl = {
    invoke_QueryInterface, invoke_AddRef, invoke_Release, invoke_Invoke
};

static HRESULT STDMETHODCALLTYPE
value_SetValue(IValueProvider *self, LPCWSTR val)
{
    (void)self; (void)val;
    /* Read-only for now: setting a field's text means driving Nuklear's editor
     * from another thread, which is phase 4b's second half along with
     * ITextProvider. Reported honestly through IsReadOnly below, so a client
     * does not offer what does not work. */
    return UIA_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE
value_get_Value(IValueProvider *self, BSTR *out)
{
    Provider *p = FROM_VALUE(self);
    VARIANT v;
    HRESULT hr;

    *out = NULL;
    hr = simple_GetPropertyValue(&p->simple, UIA_ValueValuePropertyId, &v);
    if (FAILED(hr)) return hr;
    if (v.vt == VT_BSTR) *out = v.bstrVal;   /* ownership moves to the caller */
    else VariantClear(&v);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
value_get_IsReadOnly(IValueProvider *self, BOOL *out)
{
    (void)self;
    *out = TRUE;
    return S_OK;
}

static IValueProviderVtbl g_value_vtbl = {
    value_QueryInterface, value_AddRef, value_Release,
    value_SetValue, value_get_Value, value_get_IsReadOnly
};

static Provider *
provider_new(unsigned id)
{
    Provider *p = (Provider *)calloc(1, sizeof(Provider));

    if (!p) return NULL;
    p->simple.lpVtbl   = &g_simple_vtbl;
    p->fragment.lpVtbl = &g_fragment_vtbl;
    p->root.lpVtbl     = &g_root_vtbl;
    p->invoke.lpVtbl   = &g_invoke_vtbl;
    p->value.lpVtbl    = &g_value_vtbl;
    p->ref = 1;
    p->id  = id;
    return p;
}

/* --- the window ---------------------------------------------------------
 *
 * WM_GETOBJECT is a question asked of the window procedure, and the answer is
 * its return value - which a message hook cannot give, since SDL's hook only
 * says whether to keep processing a message. So the window is subclassed and
 * everything else handed straight on. */
static LRESULT CALLBACK
wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_GETOBJECT && (DWORD)lp == (DWORD)UiaRootObjectId) {
        Provider *r = provider_new(0);
        LRESULT res;

        if (!r) return 0;
        g.wanted = 1;
        res = UiaReturnRawElementProvider(h, wp, lp,
                                          (IRawElementProviderSimple *)&r->simple);
        IRawElementProviderSimple_Release(&r->simple);
        return res;
    }
    if (msg == WM_DESTROY) UiaReturnRawElementProvider(h, 0, 0, NULL);
    return CallWindowProcW(g.prev_proc, h, msg, wp, lp);
}

/* --- the seam ----------------------------------------------------------- */

void
curie_a11y_platform_init(curie_a11y_action activate, curie_a11y_action focus,
                         void *user)
{
    SDL_Window **wins;
    int count = 0;

    g.activate     = activate;
    g.focus_action = focus;
    g.user         = user;

    InitializeCriticalSection(&g.lock);
    g.wake  = SDL_RegisterEvents(1);
    g.ready = 1;

    /* Asked for rather than guessed: the shell has exactly one window by the
     * time this runs, but its id is SDL's business. */
    wins = SDL_GetWindows(&count);
    if (wins && count > 0)
        g.hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(wins[0]),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    SDL_free(wins);
    if (!g.hwnd) {
        SDL_Log("a11y: no HWND; UI Automation is not served");
        return;
    }
    g.prev_proc = (WNDPROC)SetWindowLongPtrW(g.hwnd, GWLP_WNDPROC,
                                             (LONG_PTR)wnd_proc);
    if (!g.prev_proc)
        SDL_Log("a11y: could not subclass the window (%lu)",
                (unsigned long)GetLastError());
}

/* On the main thread, before the frame is built: the tree is whole here and
 * nothing else is writing it, which is the whole reason the request waited. */
void
curie_a11y_platform_drain(void)
{
    unsigned focus, activate;

    if (!g.ready) return;
    EnterCriticalSection(&g.lock);
    focus    = g.want_focus;
    activate = g.want_activate;
    g.want_focus = g.want_activate = 0;
    LeaveCriticalSection(&g.lock);

    /* The same two calls a reader's move and press make on the web, which are
     * the same ones Tab and Enter make - see reader_focus in main.c. */
    if (focus && g.focus_action) g.focus_action(g.user, focus);
    if (activate && g.activate)  g.activate(g.user, activate);
}

void
curie_a11y_platform_push(const curie_a11y *a, unsigned focus_id)
{
    int n, m, i;
    const curie_a11y_node *t;

    if (!g.ready) return;
    curie_a11y_changes(a, &m);
    t = curie_a11y_tree(a, &n);
    if (m == 0 && focus_id == g.focus) return;

    EnterCriticalSection(&g.lock);
    g.str_used = 0;
    g.count    = 0;
    for (i = 0; i < n && i < CURIE_A11Y_MAX_NODES; i++) {
        snap_node *s = &g.node[g.count++];

        s->id     = t[i].id;
        s->parent = t[i].parent;
        s->role   = t[i].role;
        s->level  = t[i].level;
        s->state  = t[i].state;
        s->name   = snap_intern(t[i].name);
        s->value  = snap_intern(t[i].value);
        s->x = t[i].bounds.x; s->y = t[i].bounds.y;
        s->w = t[i].bounds.w; s->h = t[i].bounds.h;
    }
    g.focus = focus_id;
    LeaveCriticalSection(&g.lock);

    /* Events only once a client has asked for the tree. Structure first,
     * because a client that has not walked the new tree cannot be told which
     * element took focus. */
    if (!g.wanted || !UiaClientsAreListening()) return;
    {
        Provider *r = provider_new(0);

        if (!r) return;
        if (m) UiaRaiseStructureChangedEvent(
                   (IRawElementProviderSimple *)&r->simple,
                   StructureChangeType_ChildrenBulkAdded, NULL, 0);
        IRawElementProviderSimple_Release(&r->simple);
    }
    if (focus_id) {
        Provider *f = provider_new(focus_id);

        if (!f) return;
        UiaRaiseAutomationEvent((IRawElementProviderSimple *)&f->simple,
                                UIA_AutomationFocusChangedEventId);
        IRawElementProviderSimple_Release(&f->simple);
    }
}

#endif /* _WIN32 */
