#include "a11y.h"

#ifdef _WIN32

#include "a11y_snapshot.h"

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <SDL3/SDL.h>

static struct {
    HWND    hwnd;
    WNDPROC prev_proc;
    int     wanted;
} g;

static long
control_type(unsigned char role)
{
    switch (role) {
    case REAKTOR_A11Y_WINDOW:     return UIA_WindowControlTypeId;
    case REAKTOR_A11Y_GROUP:      return UIA_GroupControlTypeId;
    case REAKTOR_A11Y_TABLIST:    return UIA_TabControlTypeId;
    case REAKTOR_A11Y_TAB:        return UIA_TabItemControlTypeId;
    case REAKTOR_A11Y_BUTTON:     return UIA_ButtonControlTypeId;
    case REAKTOR_A11Y_LINK:       return UIA_HyperlinkControlTypeId;
    case REAKTOR_A11Y_CHECKBOX:   return UIA_CheckBoxControlTypeId;
    case REAKTOR_A11Y_RADIO:      return UIA_RadioButtonControlTypeId;
    case REAKTOR_A11Y_TEXTBOX:    return UIA_EditControlTypeId;
    case REAKTOR_A11Y_SLIDER:     return UIA_SliderControlTypeId;
    case REAKTOR_A11Y_SPINBUTTON: return UIA_SpinnerControlTypeId;
    case REAKTOR_A11Y_PROGRESS:   return UIA_ProgressBarControlTypeId;
    case REAKTOR_A11Y_COMBOBOX:   return UIA_ComboBoxControlTypeId;
    case REAKTOR_A11Y_LISTITEM:   return UIA_ListItemControlTypeId;
    case REAKTOR_A11Y_TREEITEM:   return UIA_TreeItemControlTypeId;
    case REAKTOR_A11Y_MENUBAR:    return UIA_MenuBarControlTypeId;
    case REAKTOR_A11Y_MENU:       return UIA_MenuControlTypeId;
    case REAKTOR_A11Y_MENUITEM:   return UIA_MenuItemControlTypeId;
    case REAKTOR_A11Y_DIALOG:     return UIA_WindowControlTypeId;
    case REAKTOR_A11Y_LABEL:      return UIA_TextControlTypeId;
    default:                    return UIA_CustomControlTypeId;
    }
}

typedef struct Provider {
    IRawElementProviderSimple       simple;
    IRawElementProviderFragment     fragment;
    IRawElementProviderFragmentRoot root;
    IInvokeProvider                 invoke;
    IValueProvider                  value;
    IToggleProvider                 toggle;
    ISelectionItemProvider          selection;
    IRangeValueProvider             range;
    LONG                            ref;
    unsigned                        id;
} Provider;

static Provider *provider_new(unsigned id);
static unsigned  g_last_focus;

#define FROM_SIMPLE(p)   ((Provider *)((char *)(p) - offsetof(Provider, simple)))
#define FROM_FRAGMENT(p) ((Provider *)((char *)(p) - offsetof(Provider, fragment)))
#define FROM_ROOT(p)     ((Provider *)((char *)(p) - offsetof(Provider, root)))
#define FROM_INVOKE(p)   ((Provider *)((char *)(p) - offsetof(Provider, invoke)))
#define FROM_VALUE(p)    ((Provider *)((char *)(p) - offsetof(Provider, value)))
#define FROM_TOGGLE(p)   ((Provider *)((char *)(p) - offsetof(Provider, toggle)))
#define FROM_SELECT(p)   ((Provider *)((char *)(p) - offsetof(Provider, selection)))
#define FROM_RANGE(p)    ((Provider *)((char *)(p) - offsetof(Provider, range)))

static int
provider_is_root(const Provider *p)
{
    return p->id == 0;
}

static int
node_of(unsigned id, reaktor_snap_node *out, char *buf, size_t cap)
{
    return reaktor_snap_get(id, out, buf, cap);
}

static int
node_role_is(unsigned id, unsigned char a, unsigned char b, unsigned char c)
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (!node_of(id, &n, buf, sizeof(buf))) return 0;
    return n.role == a || (b && n.role == b) || (c && n.role == c);
}

static int
node_is_range(unsigned id)
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    return node_of(id, &n, buf, sizeof(buf)) && n.hi > n.lo;
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
        if (!node_role_is(p->id, REAKTOR_A11Y_BUTTON, REAKTOR_A11Y_LINK,
                          REAKTOR_A11Y_MENUITEM) &&
            !node_role_is(p->id, REAKTOR_A11Y_TAB, 0, 0))
            return E_NOINTERFACE;
        *out = &p->invoke;
    } else if (IsEqualIID(iid, &IID_IValueProvider)) {
        reaktor_snap_node n;
        char buf[REAKTOR_SNAP_TEXT];

        if (!node_of(p->id, &n, buf, sizeof(buf)) || !n.value)
            return E_NOINTERFACE;
        *out = &p->value;
    } else if (IsEqualIID(iid, &IID_IToggleProvider)) {
        if (!node_role_is(p->id, REAKTOR_A11Y_CHECKBOX, 0, 0))
            return E_NOINTERFACE;
        *out = &p->toggle;
    } else if (IsEqualIID(iid, &IID_ISelectionItemProvider)) {
        if (!node_role_is(p->id, REAKTOR_A11Y_TAB, REAKTOR_A11Y_LISTITEM,
                          REAKTOR_A11Y_RADIO))
            return E_NOINTERFACE;
        *out = &p->selection;
    } else if (IsEqualIID(iid, &IID_IRangeValueProvider)) {
        if (!node_is_range(p->id)) return E_NOINTERFACE;
        *out = &p->range;
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
IUNKNOWN_FOR(toggle,   IToggleProvider,                 FROM_TOGGLE)
IUNKNOWN_FOR(select,   ISelectionItemProvider,          FROM_SELECT)
IUNKNOWN_FOR(range,    IRangeValueProvider,             FROM_RANGE)

static HRESULT STDMETHODCALLTYPE
simple_get_ProviderOptions(IRawElementProviderSimple *self,
                           enum ProviderOptions *out)
{
    (void)self;
    *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
simple_GetPatternProvider(IRawElementProviderSimple *self, PATTERNID pattern,
                          IUnknown **out)
{
    Provider *p = FROM_SIMPLE(self);
    REFIID iid = pattern == UIA_InvokePatternId    ? &IID_IInvokeProvider
               : pattern == UIA_ValuePatternId     ? &IID_IValueProvider
               : pattern == UIA_TogglePatternId    ? &IID_IToggleProvider
               : pattern == UIA_SelectionItemPatternId
                                                   ? &IID_ISelectionItemProvider
               : pattern == UIA_RangeValuePatternId ? &IID_IRangeValueProvider
               : NULL;

    *out = NULL;
    if (!iid) return S_OK;
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
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    VariantInit(out);
    if (provider_is_root(p)) {
        if (prop == UIA_NamePropertyId) {
            str_variant(out, "Reaktor");
        } else if (prop == UIA_ControlTypePropertyId) {
            out->vt = VT_I4;
            out->lVal = UIA_WindowControlTypeId;
        } else if (prop == UIA_IsControlElementPropertyId ||
                   prop == UIA_IsContentElementPropertyId) {
            bool_variant(out, 1);
        }
        return S_OK;
    }
    if (!node_of(p->id, &n, buf, sizeof(buf))) return S_OK;
    {
        if (prop == UIA_NamePropertyId) {
            str_variant(out, n.name);
        } else if (prop == UIA_ControlTypePropertyId) {
            out->vt = VT_I4;
            out->lVal = control_type(n.role);
        } else if (prop == UIA_AutomationIdPropertyId) {
            char id[24];
            sprintf(id, "%u", n.id);
            str_variant(out, id);
        } else if (prop == UIA_IsEnabledPropertyId) {
            bool_variant(out, !(n.state & REAKTOR_A11Y_DISABLED));
        } else if (prop == UIA_IsKeyboardFocusablePropertyId) {
            bool_variant(out, reaktor_snap_focusable(n.role, n.state));
        } else if (prop == UIA_HasKeyboardFocusPropertyId) {
            bool_variant(out, (n.state & REAKTOR_A11Y_FOCUSED) != 0);
        } else if (prop == UIA_IsOffscreenPropertyId) {
            bool_variant(out, (n.state & REAKTOR_A11Y_OFFSCREEN) != 0);
        } else if (prop == UIA_IsControlElementPropertyId ||
                   prop == UIA_IsContentElementPropertyId) {
            bool_variant(out, 1);
        } else if (prop == UIA_ValueValuePropertyId) {
            str_variant(out, n.value);
        } else if (prop == UIA_AcceleratorKeyPropertyId) {
            str_variant(out, n.keys);
        } else if (prop == UIA_ToggleToggleStatePropertyId) {
                if (n.role == REAKTOR_A11Y_CHECKBOX ||
                    n.role == REAKTOR_A11Y_RADIO) {
                out->vt = VT_I4;
                out->lVal = (n.state & REAKTOR_A11Y_CHECKED) ? ToggleState_On
                                                           : ToggleState_Off;
            }
        } else if (prop == UIA_SelectionItemIsSelectedPropertyId) {
            if (n.role == REAKTOR_A11Y_TAB || n.role == REAKTOR_A11Y_LISTITEM)
                bool_variant(out, (n.state & REAKTOR_A11Y_SELECTED) != 0);
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
    if (!provider_is_root(p)) return S_OK;
    return UiaHostProviderFromHwnd(g.hwnd, out);
}

static IRawElementProviderSimpleVtbl g_simple_vtbl = {
    simple_QueryInterface, simple_AddRef, simple_Release,
    simple_get_ProviderOptions, simple_GetPatternProvider,
    simple_GetPropertyValue, simple_get_HostRawElementProvider
};

static HRESULT STDMETHODCALLTYPE
fragment_Navigate(IRawElementProviderFragment *self,
                  enum NavigateDirection dir, IRawElementProviderFragment **out)
{
    Provider *p = FROM_FRAGMENT(self);
    unsigned to = 0;

    *out = NULL;
    if (provider_is_root(p)) {
        if (dir == NavigateDirection_FirstChild) to = reaktor_snap_child(0, 0);
        else if (dir == NavigateDirection_LastChild) to = reaktor_snap_child(0, 1);
    } else {
        switch (dir) {
        case NavigateDirection_Parent:
            to = reaktor_snap_parent(p->id);
            if (!to) {
                Provider *r = provider_new(0);
                if (!r) return E_OUTOFMEMORY;
                *out = &r->fragment;
                return S_OK;
            }
            break;
        case NavigateDirection_FirstChild:  to = reaktor_snap_child(p->id, 0); break;
        case NavigateDirection_LastChild:   to = reaktor_snap_child(p->id, 1); break;
        case NavigateDirection_NextSibling: to = reaktor_snap_sibling(p->id, 0); break;
        case NavigateDirection_PreviousSibling:
            to = reaktor_snap_sibling(p->id, 1);
            break;
        default: break;
        }
    }
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
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];
    POINT origin;

    out->left = out->top = out->width = out->height = 0.0;
    if (provider_is_root(p)) return S_OK;
    if (!node_of(p->id, &n, buf, sizeof(buf))) return S_OK;
    out->left = n.x; out->top = n.y;
    out->width = n.w; out->height = n.h;

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
    *out = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
fragment_SetFocus(IRawElementProviderFragment *self)
{
    Provider *p = FROM_FRAGMENT(self);

    if (!provider_is_root(p)) reaktor_snap_request_focus(p->id);
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

static HRESULT STDMETHODCALLTYPE
root_ElementProviderFromPoint(IRawElementProviderFragmentRoot *self,
                              double x, double y,
                              IRawElementProviderFragment **out)
{
    POINT origin;
    unsigned hit;

    (void)self;
    *out = NULL;
    origin.x = origin.y = 0;
    if (!ClientToScreen(g.hwnd, &origin)) return S_OK;
    hit = reaktor_snap_hit((float)(x - origin.x), (float)(y - origin.y));
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
    id = reaktor_snap_focus();
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

static HRESULT STDMETHODCALLTYPE
invoke_Invoke(IInvokeProvider *self)
{
    reaktor_snap_request_activate(FROM_INVOKE(self)->id);
    return S_OK;
}

static IInvokeProviderVtbl g_invoke_vtbl = {
    invoke_QueryInterface, invoke_AddRef, invoke_Release, invoke_Invoke
};

static HRESULT STDMETHODCALLTYPE
value_SetValue(IValueProvider *self, LPCWSTR val)
{
    (void)self; (void)val;
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
    if (v.vt == VT_BSTR) *out = v.bstrVal;
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

static HRESULT STDMETHODCALLTYPE
toggle_Toggle(IToggleProvider *self)
{
    reaktor_snap_request_activate(FROM_TOGGLE(self)->id);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
toggle_get_ToggleState(IToggleProvider *self, enum ToggleState *out)
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    *out = ToggleState_Indeterminate;
    if (node_of(FROM_TOGGLE(self)->id, &n, buf, sizeof(buf)))
        *out = (n.state & REAKTOR_A11Y_CHECKED) ? ToggleState_On
                                              : ToggleState_Off;
    return S_OK;
}

static IToggleProviderVtbl g_toggle_vtbl = {
    toggle_QueryInterface, toggle_AddRef, toggle_Release,
    toggle_Toggle, toggle_get_ToggleState
};

static HRESULT STDMETHODCALLTYPE
select_Select(ISelectionItemProvider *self)
{
    reaktor_snap_request_activate(FROM_SELECT(self)->id);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
select_AddToSelection(ISelectionItemProvider *self)
{
    return select_Select(self);
}

static HRESULT STDMETHODCALLTYPE
select_RemoveFromSelection(ISelectionItemProvider *self)
{
    (void)self;
    return UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE
select_get_IsSelected(ISelectionItemProvider *self, BOOL *out)
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    *out = FALSE;
    if (node_of(FROM_SELECT(self)->id, &n, buf, sizeof(buf)))
        *out = (n.state & (REAKTOR_A11Y_SELECTED | REAKTOR_A11Y_CHECKED))
             ? TRUE : FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
select_get_SelectionContainer(ISelectionItemProvider *self,
                              IRawElementProviderSimple **out)
{
    unsigned parent = reaktor_snap_parent(FROM_SELECT(self)->id);

    *out = NULL;
    if (!parent) return S_OK;
    {
        Provider *q = provider_new(parent);
        if (!q) return E_OUTOFMEMORY;
        *out = &q->simple;
    }
    return S_OK;
}

static ISelectionItemProviderVtbl g_select_vtbl = {
    select_QueryInterface, select_AddRef, select_Release,
    select_Select, select_AddToSelection, select_RemoveFromSelection,
    select_get_IsSelected, select_get_SelectionContainer
};

static HRESULT STDMETHODCALLTYPE
range_SetValue(IRangeValueProvider *self, double val)
{
    (void)self; (void)val;
    return UIA_E_NOTSUPPORTED;
}

static HRESULT
range_field(IRangeValueProvider *self, size_t off, double *out)
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    *out = 0.0;
    if (node_of(FROM_RANGE(self)->id, &n, buf, sizeof(buf)))
        *out = *(float *)((char *)&n + off);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
range_get_Value(IRangeValueProvider *self, double *out)
{ return range_field(self, offsetof(reaktor_snap_node, num), out); }

static HRESULT STDMETHODCALLTYPE
range_get_Maximum(IRangeValueProvider *self, double *out)
{ return range_field(self, offsetof(reaktor_snap_node, hi), out); }

static HRESULT STDMETHODCALLTYPE
range_get_Minimum(IRangeValueProvider *self, double *out)
{ return range_field(self, offsetof(reaktor_snap_node, lo), out); }

static HRESULT STDMETHODCALLTYPE
range_get_SmallChange(IRangeValueProvider *self, double *out)
{ return range_field(self, offsetof(reaktor_snap_node, step), out); }

static HRESULT STDMETHODCALLTYPE
range_get_LargeChange(IRangeValueProvider *self, double *out)
{
    HRESULT hr = range_field(self, offsetof(reaktor_snap_node, step), out);

    *out *= 10.0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE
range_get_IsReadOnly(IRangeValueProvider *self, BOOL *out)
{
    (void)self;
    *out = TRUE;
    return S_OK;
}

static IRangeValueProviderVtbl g_range_vtbl = {
    range_QueryInterface, range_AddRef, range_Release,
    range_SetValue, range_get_Value, range_get_IsReadOnly,
    range_get_Maximum, range_get_Minimum, range_get_LargeChange,
    range_get_SmallChange
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
    p->toggle.lpVtbl   = &g_toggle_vtbl;
    p->selection.lpVtbl = &g_select_vtbl;
    p->range.lpVtbl    = &g_range_vtbl;
    p->ref = 1;
    p->id  = id;
    return p;
}

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

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    SDL_Window **wins;
    int count = 0;

    if (!reaktor_snap_init(activate, focus, user)) return;

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

void
reaktor_a11y_platform_drain(void)
{
    reaktor_snap_drain();
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    if (!reaktor_snap_update(a, focus_id)) return;

    if (!g.wanted || !UiaClientsAreListening()) return;
    {
        int m, i, structural = 0;
        const reaktor_a11y_change *c = reaktor_a11y_changes(a, &m);

        for (i = 0; i < m; i++)
            if (c[i].kind == REAKTOR_A11Y_ADDED ||
                c[i].kind == REAKTOR_A11Y_REMOVED) {
                structural = 1;
                break;
            }
        if (structural) {
            Provider *r = provider_new(0);

            if (!r) return;
            UiaRaiseStructureChangedEvent(
                (IRawElementProviderSimple *)&r->simple,
                StructureChangeType_ChildrenInvalidated, NULL, 0);
            IRawElementProviderSimple_Release(&r->simple);
        }
    }
    if (focus_id != g_last_focus) {
        g_last_focus = focus_id;
        if (focus_id) {
            Provider *f = provider_new(focus_id);

            if (!f) return;
            UiaRaiseAutomationEvent((IRawElementProviderSimple *)&f->simple,
                                    UIA_AutomationFocusChangedEventId);
            IRawElementProviderSimple_Release(&f->simple);
        }
    }
}

#endif
