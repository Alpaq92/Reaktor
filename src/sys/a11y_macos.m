/* a11y_macos.m - phase 4d of docs/ACCESSIBILITY.md: the tree, served to
 * macOS's NSAccessibility.
 *
 * The first Objective-C in this project, and the only file that is not C. It
 * has to be: NSAccessibility is a set of Objective-C protocols, and there is
 * no C entry point to them.
 *
 * The shape is the one AppKit intends for a view that draws its own widgets.
 * NSAccessibilityElement exists precisely for UI that has no NSView behind it:
 * an element answers for one node, and the content view is told to report our
 * root as its child. Nothing is subclassed and nothing is swizzled - SDL's
 * view stays SDL's, and the accessibility attributes AppKit lets you set on
 * any NSView are enough to graft the tree on.
 *
 * Two things differ from the other bridges:
 *
 * **Coordinates are upside down.** AppKit's screen origin is the bottom-left
 * of the main display and the model's is the top-left of the window, so a
 * frame goes through the view, the window and the screen to get there. This is
 * also what the AT-SPI bridge has left undone, and here it is unavoidable
 * because a wrong frame puts the cursor ring in the wrong place rather than
 * merely reporting an odd number.
 *
 * **The elements are objects with lifetimes.** The other bridges answer with
 * an id and build nothing; AppKit holds onto what it is given, so an element
 * per live node is cached and the cache is dropped whenever the tree's shape
 * changes. An element whose node has gone answers as an empty group rather
 * than with stale geometry.
 *
 * Everything else is the shared snapshot, exactly as on Windows: read a node,
 * answer, and post what a client asks for back to the app's own thread. */
#include "a11y.h"

#ifdef REAKTOR_HAVE_NSACCESSIBILITY

#include "a11y_snapshot.h"

#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>

@class ReaktorElement;

static struct {
    int ready;
    __strong NSMutableDictionary<NSNumber *, ReaktorElement *> *cache;
    __weak   NSView *view;
    __strong ReaktorElement *root;
    unsigned shape;          /* bumped when the tree's structure changed */
} g;

/* --- roles --------------------------------------------------------------- */

static NSAccessibilityRole
role_of(unsigned char role)
{
    switch (role) {
    case REAKTOR_A11Y_WINDOW:     return NSAccessibilityWindowRole;
    case REAKTOR_A11Y_GROUP:      return NSAccessibilityGroupRole;
    case REAKTOR_A11Y_TABLIST:    return NSAccessibilityTabGroupRole;
    /* A tab is a radio button in a tab group, which is what AppKit's own tab
     * views report and what VoiceOver's tab wording keys off. */
    case REAKTOR_A11Y_TAB:        return NSAccessibilityRadioButtonRole;
    case REAKTOR_A11Y_BUTTON:     return NSAccessibilityButtonRole;
    case REAKTOR_A11Y_LINK:       return NSAccessibilityLinkRole;
    case REAKTOR_A11Y_CHECKBOX:   return NSAccessibilityCheckBoxRole;
    case REAKTOR_A11Y_RADIO:      return NSAccessibilityRadioButtonRole;
    case REAKTOR_A11Y_TEXTBOX:    return NSAccessibilityTextFieldRole;
    case REAKTOR_A11Y_SLIDER:     return NSAccessibilitySliderRole;
    case REAKTOR_A11Y_SPINBUTTON: return NSAccessibilityIncrementorRole;
    case REAKTOR_A11Y_PROGRESS:   return NSAccessibilityProgressIndicatorRole;
    case REAKTOR_A11Y_COMBOBOX:   return NSAccessibilityPopUpButtonRole;
    case REAKTOR_A11Y_LISTITEM:   return NSAccessibilityRowRole;
    case REAKTOR_A11Y_TREEITEM:   return NSAccessibilityRowRole;
    case REAKTOR_A11Y_MENUBAR:    return NSAccessibilityMenuBarRole;
    case REAKTOR_A11Y_MENU:       return NSAccessibilityMenuRole;
    case REAKTOR_A11Y_MENUITEM:   return NSAccessibilityMenuItemRole;
    case REAKTOR_A11Y_DIALOG:     return NSAccessibilityWindowRole;
    case REAKTOR_A11Y_LABEL:      return NSAccessibilityStaticTextRole;
    default:                    return NSAccessibilityUnknownRole;
    }
}

static BOOL
takes_press(unsigned char role)
{
    return role == REAKTOR_A11Y_BUTTON   || role == REAKTOR_A11Y_LINK ||
           role == REAKTOR_A11Y_TAB      || role == REAKTOR_A11Y_MENUITEM ||
           role == REAKTOR_A11Y_CHECKBOX || role == REAKTOR_A11Y_RADIO ||
           role == REAKTOR_A11Y_LISTITEM;
}

/* --- one node ------------------------------------------------------------ */

@interface ReaktorElement : NSAccessibilityElement
@property (nonatomic) unsigned nodeId;
@end

static ReaktorElement *element_for(unsigned id);

@implementation ReaktorElement

/* Every answer starts here: the node as of the last drawn frame, copied out
 * of the snapshot. NO means it has gone, and the caller answers emptily. */
- (BOOL)node:(reaktor_snap_node *)out buffer:(char *)buf
{
    return reaktor_snap_get(self.nodeId, out, buf, REAKTOR_SNAP_TEXT) ? YES : NO;
}

- (BOOL)isAccessibilityElement
{
    return YES;
}

- (NSAccessibilityRole)accessibilityRole
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf]) return NSAccessibilityGroupRole;
    return role_of(n.role);
}

- (NSString *)accessibilityLabel
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf] || !n.name) return nil;
    return [NSString stringWithUTF8String:n.name];
}

- (id)accessibilityValue
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf]) return nil;
    /* A range answers with a number, which is what the slider and the
     * progress bar are asked for; anything else answers with its text. A
     * checkbox and a radio answer with their state, which AppKit wants as
     * 0 or 1 rather than as words. */
    if (n.role == REAKTOR_A11Y_CHECKBOX || n.role == REAKTOR_A11Y_RADIO ||
        n.role == REAKTOR_A11Y_TAB)
        return @((n.state & (REAKTOR_A11Y_CHECKED | REAKTOR_A11Y_SELECTED)) ? 1 : 0);
    if (n.hi > n.lo) return @(n.num);
    return n.value ? [NSString stringWithUTF8String:n.value] : nil;
}

- (id)accessibilityMinValue
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf] || n.hi <= n.lo) return nil;
    return @(n.lo);
}

- (id)accessibilityMaxValue
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf] || n.hi <= n.lo) return nil;
    return @(n.hi);
}

- (NSArray *)accessibilityChildren
{
    NSMutableArray *kids = [NSMutableArray array];
    unsigned kid = reaktor_snap_child(self.nodeId, 0);

    while (kid) {
        ReaktorElement *e = element_for(kid);
        if (e) [kids addObject:e];
        kid = reaktor_snap_sibling(kid, 0);
    }
    return kids;
}

- (id)accessibilityParent
{
    unsigned up = reaktor_snap_parent(self.nodeId);

    /* A node with no parent hangs off the view, which is what grafts the
     * whole tree onto the window. */
    if (!up) return NSAccessibilityUnignoredAncestor(g.view);
    return element_for(up);
}

/* The model works in window coordinates with the origin at the top left;
 * AppKit wants screen coordinates with the origin at the bottom left of the
 * main display. View, then window, then screen. */
- (NSRect)accessibilityFrame
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];
    NSView *v = g.view;
    NSRect r;

    if (!v || ![self node:&n buffer:buf]) return NSZeroRect;
    r = NSMakeRect(n.x, v.bounds.size.height - n.y - n.h, n.w, n.h);
    if (v.isFlipped) r.origin.y = n.y;
    r = [v convertRect:r toView:nil];
    return [v.window convertRectToScreen:r];
}

- (BOOL)isAccessibilityFocused
{
    return reaktor_snap_focus() == self.nodeId;
}

- (void)setAccessibilityFocused:(BOOL)focused
{
    if (focused) reaktor_snap_request_focus(self.nodeId);
}

- (BOOL)isAccessibilityEnabled
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf]) return NO;
    return (n.state & REAKTOR_A11Y_DISABLED) ? NO : YES;
}

- (BOOL)accessibilityPerformPress
{
    reaktor_snap_node n;
    char buf[REAKTOR_SNAP_TEXT];

    if (![self node:&n buffer:buf] || !takes_press(n.role)) return NO;
    /* The same request every other bridge makes: recorded here, run on the
     * app's own thread, taken by the widget itself. */
    reaktor_snap_request_activate(self.nodeId);
    return YES;
}

- (id)accessibilityHitTest:(NSPoint)point
{
    /* Screen point back to the model's window coordinates, the reverse of
     * accessibilityFrame. */
    NSView *v = g.view;
    NSRect asRect;
    NSPoint inWindow, inView;
    unsigned hit;

    if (!v) return self;
    asRect = NSMakeRect(point.x, point.y, 0.0, 0.0);
    inWindow = [v.window convertRectFromScreen:asRect].origin;
    inView = [v convertPoint:inWindow fromView:nil];
    if (!v.isFlipped) inView.y = v.bounds.size.height - inView.y;

    hit = reaktor_snap_hit((float)inView.x, (float)inView.y);
    if (!hit) return self;
    return element_for(hit);
}

@end

/* One element per live node, made when a client first asks for it. AppKit
 * keeps what it is handed, so identity has to hold across frames or a reader
 * loses its place - which the model's ids already guarantee. */
static ReaktorElement *
element_for(unsigned id)
{
    NSNumber *key = @(id);
    ReaktorElement *e;

    if (!g.cache) return nil;
    e = g.cache[key];
    if (!e) {
        e = [[ReaktorElement alloc] init];
        e.nodeId = id;
        g.cache[key] = e;
    }
    return e;
}

/* --- the seam ------------------------------------------------------------ */

void
reaktor_a11y_platform_init(reaktor_a11y_action activate, reaktor_a11y_action focus,
                         void *user)
{
    SDL_Window **wins;
    int count = 0;
    NSWindow *window = nil;

    if (!reaktor_snap_init(activate, focus, user)) return;

    wins = SDL_GetWindows(&count);
    if (wins && count > 0)
        window = (__bridge NSWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(wins[0]),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    SDL_free(wins);
    if (!window || !window.contentView) {
        SDL_Log("a11y: no Cocoa window; NSAccessibility is not served");
        return;
    }

    g.cache = [NSMutableDictionary dictionary];
    g.view  = window.contentView;
    g.root  = element_for(0);

    /* SDL's view is left alone but told what it contains. These setters are
     * what AppKit provides for a view that draws its own widgets - no
     * subclass, no category, nothing of SDL's overridden. */
    g.view.accessibilityElement  = YES;
    g.view.accessibilityRole     = NSAccessibilityGroupRole;
    g.view.accessibilityLabel    = @"Reaktor";
    g.view.accessibilityChildren = @[ g.root ];

    g.ready = 1;
}

void
reaktor_a11y_platform_drain(void)
{
    reaktor_snap_drain();
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    static unsigned last_focus;
    int m, i, structural = 0;
    const reaktor_a11y_change *c;

    if (!reaktor_snap_update(a, focus_id)) return;
    if (!g.ready) return;

    c = reaktor_a11y_changes(a, &m);
    for (i = 0; i < m; i++)
        if (c[i].kind == REAKTOR_A11Y_ADDED || c[i].kind == REAKTOR_A11Y_REMOVED) {
            structural = 1;
            break;
        }

    if (structural) {
        /* The elements for a page that has gone would otherwise sit in the
         * cache for the life of the process, each answering emptily. Dropping
         * them costs the next walk a fresh set, which is what a client does
         * after a layout change anyway. The root is kept: the view holds it. */
        [g.cache removeAllObjects];
        g.cache[@0] = g.root;
        g.shape++;
        NSAccessibilityPostNotification(
            g.root, NSAccessibilityLayoutChangedNotification);
    }

    if (focus_id != last_focus) {
        last_focus = focus_id;
        if (focus_id) {
            ReaktorElement *e = element_for(focus_id);
            if (e)
                NSAccessibilityPostNotification(
                    e, NSAccessibilityFocusedUIElementChangedNotification);
        }
    }
}

#endif /* REAKTOR_HAVE_NSACCESSIBILITY */
