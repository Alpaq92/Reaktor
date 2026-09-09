/* focus.c - the keyboard focus ring, its movement, and activation.
 *
 * Lifted out of main.c unchanged, as one section of it. The only edits are the
 * ones a file boundary forces.
 */
#include "internal.h"

/* --- keyboard focus ----------------------------------------------------- */
/* Phase 3 of docs/ACCESSIBILITY.md. Nuklear has no focus model - NK_KEY_TAB
 * inserts a tab - so the shell keeps one, over the tree the frame just
 * described. */

enum {
    FOCUS_NEXT = 1, FOCUS_PREV,        /* Tab, Shift-Tab: every focusable node */
    FOCUS_FIRST, FOCUS_LAST,           /* Home, End */
    FOCUS_SIB_NEXT, FOCUS_SIB_PREV     /* arrows: siblings only */
};

/* Whether the node at `i` has `ancestor` above it. The tree is flat and a
 * node names its parent by id, so this is the walk up; the guard is the
 * tree's own depth limit, which a cycle could not exceed. */
static int
node_under(const reaktor_a11y_node *t, int n, int i, unsigned ancestor)
{
    int guard = 0;

    while (t[i].parent && guard++ < REAKTOR_A11Y_MAX_DEPTH) {
        int k;

        if (t[i].parent == ancestor) return 1;
        for (k = 0; k < n; k++) if (t[k].id == t[i].parent) break;
        if (k == n) return 0;
        i = k;
    }
    return 0;
}

static int
focusable(const reaktor_a11y_node *n)
{
    switch (n->role) {
    case REAKTOR_A11Y_TAB:       case REAKTOR_A11Y_BUTTON:
    case REAKTOR_A11Y_LINK:      case REAKTOR_A11Y_CHECKBOX:
    case REAKTOR_A11Y_RADIO:     case REAKTOR_A11Y_TEXTBOX:
    case REAKTOR_A11Y_SLIDER:    case REAKTOR_A11Y_SPINBUTTON:
    case REAKTOR_A11Y_COMBOBOX:  case REAKTOR_A11Y_LISTITEM:
    case REAKTOR_A11Y_TREEITEM:  case REAKTOR_A11Y_MENUITEM:
        /* Off-window nodes count: the page scrolls to them - see focus_move. */
        return !(n->state & REAKTOR_A11Y_DISABLED);
    default:
        return 0;
    }
}

/* Moves focus now, against the tree of the last drawn frame - which is
 * complete, and still valid between frames. At the key rather than after
 * the next frame, because keys arrive faster than frames and two Tabs that
 * landed before one frame were merging into a single move. Tab walks every
 * focusable node in reading order and wraps; the arrows stay among
 * siblings, which is what makes a tab strip or a menu behave as one
 * control. A node that scrolled out of the window is skipped rather than
 * scrolled to: nothing here can move a group's scroll yet. */
/* Inside the page and outside its visible band: the page scrolls to it on the
 * frame that follows, where the group's scroll can be set. The band and the
 * node's bounds are both as of the last frame, which is the frame the node was
 * measured in, so they agree.
 *
 * Shared by the keyboard and by a screen reader, which is the point of it
 * being a function. A node a reader moved to is as much "where focus is" as
 * one Tab reached, and only the keyboard was asking: UIA's SetFocus, AT-SPI's
 * GrabFocus and the web mirror's focus event all stamped the node focused and
 * left the page where it was, so the ring was drawn somewhere nobody could
 * see. Checked on Linux over AT-SPI: GrabFocus on a node scrolled out of the
 * page reported it focused, not showing, and the page did not move. */
static void
focus_reveal(App *app, const reaktor_a11y_node *t, int n, int pick)
{
    const struct nk_rect *b = &app->body_rect, *r = &t[pick].bounds;

    if (node_under(t, n, pick, app->page_node) &&
        (r->y < b->y || r->y + r->h > b->y + b->h)) {
        app->focus_scroll      = 1;
        app->focus_scroll_rect = *r;
    }
}

static void
focus_move(App *app, int step)
{
    int n, i, cur = -1, pick = -1;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) { cur = i; break; }

    if (step == FOCUS_FIRST || step == FOCUS_LAST) {
        int d = step == FOCUS_FIRST ? 1 : -1;
        for (i = d > 0 ? 0 : n - 1; i >= 0 && i < n; i += d)
            if (focusable(&t[i])) { pick = i; break; }
    } else {
        int d   = (step == FOCUS_NEXT || step == FOCUS_SIB_NEXT) ? 1 : -1;
        int sib = (step == FOCUS_SIB_NEXT || step == FOCUS_SIB_PREV) && cur >= 0;
        int k, start = cur >= 0 ? cur : (d > 0 ? -1 : n);
        for (k = 1; k <= n; k++) {
            i = ((start + d * k) % n + n) % n;
            if (i == cur) break;
            if (!focusable(&t[i])) continue;
            if (sib && t[i].parent != t[cur].parent) continue;
            pick = i;
            break;
        }
    }
    if (pick < 0) return;

    app->focus_id = t[pick].id;
    reaktor_a11y_set_focus(&app->a11y, app->focus_id);
    /* The rect comes with the pick, so Enter a moment later presses this
     * node and not the one before it. The frame after draws the ring. */
    app->focus_rect    = t[pick].bounds;
    app->focus_seen    = 1;
    app->focus_visible = 1;
    app->dirty = 1;
    focus_reveal(app, t, n, pick);
    /* Tab into a field puts the caret in it, as it does everywhere else. */
    if (t[pick].role == REAKTOR_A11Y_TEXTBOX) {
        app->key_click   = 1;
        app->key_click_x = t[pick].bounds.x + t[pick].bounds.w * 0.5f;
        app->key_click_y = t[pick].bounds.y + t[pick].bounds.h * 0.5f;
    }
}

/* After a frame: a focus whose node went away - another page, a closed
 * menu, a scroll - is dropped rather than left pointing at nothing. */
void
focus_resolve(App *app)
{
    int n, i;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    if (!app->focus_id) return;
    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) {
            if (focusable(&t[i])) return;
            break;
        }
    app->focus_id = 0;
    reaktor_a11y_set_focus(&app->a11y, 0);
}

/* Whether what has focus takes the arrows as a value rather than as a move. */
static int
focus_is_range(App *app)
{
    int n, i;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    if (!app->focus_id) return 0;
    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id)
            return t[i].role == REAKTOR_A11Y_SLIDER ||
                   t[i].role == REAKTOR_A11Y_SPINBUTTON;
    return 0;
}

/* Tab walks the focusable nodes, the arrows walk siblings, Home and End
 * jump, Enter and Space press. Answers whether the key was taken, so the
 * caller keeps it from Nuklear - otherwise Tab would still land in a field
 * as a character. While a field is being edited the editor keeps every key
 * but Tab, which leaves it. The keypad arrows count: with Num Lock off
 * they are the only arrows some keyboards have. */
int
focus_key(App *app, const SDL_Event *event)
{
    struct nk_window *pw = nk_window_find(app->ctx, "page");
    int editing = pw && pw->edit.active;

    if (event->key.key == SDLK_TAB) {
        if (editing) pw->edit.active = nk_false;
        focus_move(app, (event->key.mod & SDL_KMOD_SHIFT) ? FOCUS_PREV
                                                         : FOCUS_NEXT);
        app->dirty = 1;
        return 1;
    }
    if (editing) return 0;

    switch (event->key.key) {
    /* On a range the arrows are the value, not the focus - Right and Up up,
     * Left and Down down, which is what every platform's slider does. The
     * shell cannot apply the step itself: it knows the node's value as the
     * text a reader would hear and nothing of its bounds or its grain, so it
     * records the direction and the widget answers it (reaktor_focus_step). */
    case SDLK_RIGHT: case SDLK_KP_6: case SDLK_UP: case SDLK_KP_8:
        if (focus_is_range(app)) { app->focus_step++; break; }
        focus_move(app, (event->key.key == SDLK_UP ||
                         event->key.key == SDLK_KP_8) ? FOCUS_SIB_PREV
                                                      : FOCUS_SIB_NEXT);
        break;
    case SDLK_LEFT:  case SDLK_KP_4: case SDLK_DOWN: case SDLK_KP_2:
        if (focus_is_range(app)) { app->focus_step--; break; }
        focus_move(app, (event->key.key == SDLK_DOWN ||
                         event->key.key == SDLK_KP_2) ? FOCUS_SIB_NEXT
                                                      : FOCUS_SIB_PREV);
        break;
    case SDLK_HOME:  case SDLK_KP_7: focus_move(app, FOCUS_FIRST); break;
    case SDLK_END:   case SDLK_KP_1: focus_move(app, FOCUS_LAST);  break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
        if (!app->focus_id || !app->focus_seen) return 0;
        /* Offered to the widget first; the frame that ends without it taken
         * turns it into the click this used to be. */
        app->activate_id   = app->focus_id;
        app->focus_visible = 1;
        break;
    case SDLK_ESCAPE:
        /* Hides the ring; Nuklear may want the key as well. */
        app->focus_visible = 0;
        app->dirty = 1;
        return 0;
    default:
        return 0;
    }
    app->dirty = 1;
    return 1;
}

/* What a reader's press and a reader's move become: the same click and the
 * same focus a key would make, so a node reached through the platform
 * behaves exactly as one reached through Tab - see a11y_web.c. */
void
reader_focus(void *user, unsigned id)
{
    App *app = (App *)user;
    int n, i;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == id) {
            app->focus_id = id;
            reaktor_a11y_set_focus(&app->a11y, id);
            app->focus_rect    = t[i].bounds;
            app->focus_seen    = 1;
            app->focus_visible = 1;
            app->dirty = 1;
            focus_reveal(app, t, n, i);
            return;
        }
}

void
reader_activate(void *user, unsigned id)
{
    App *app = (App *)user;
    SDL_Event e;

    reader_focus(user, id);
    if (app->focus_id != id) return;
    app->activate_id = id;
    /* The widget takes it on the next frame, and dirty is cleared at the end
     * of this one - so the frame has to be asked for. */
    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    SDL_PushEvent(&e);
}

/* Whether this node was asked to activate, taken once.
 *
 * Enter and a platform client's press both used to become a synthetic mouse
 * click at the node's centre, and for widgets inside the page group that was
 * not reliable: the click reaches Nuklear's input and the widget does not act
 * on it, while the identical injection from a key does. Rather than keep
 * chasing that, a widget can be told directly - it knows its own state and
 * changing it is a line - and the click stays only as the fallback for the
 * ones that have not been taught to listen. */
int
reaktor_focus_activated(App *app, unsigned id)
{
    if (!id || id != app->activate_id) return 0;
    app->activate_id = 0;
    return 1;
}

/* How many steps the arrows asked this node for, taken once. Zero for every
 * node but the focused one, and for that one only until it has read it. */
int
reaktor_focus_step(App *app, unsigned id)
{
    int s;

    if (!id || id != app->focus_id || !app->focus_step) return 0;
    s = app->focus_step;
    app->focus_step = 0;
    return s;
}

/* The ring, from one place once the page has drawn: 2px in --focus, a pixel
 * outside the node so it never covers the widget's own edge. Shown once a
 * key has moved focus and hidden by the next click - the :focus-visible
 * rule every desktop follows.
 *
 * Clipped to the page's band when the focused node is in the page. The ring
 * is drawn after the group has ended, so nothing else would clip it, and a
 * node half scrolled under the band's edge was getting a whole ring - drawn
 * over the tab strip above it, or over the window's edge below. */
void
focus_ring(App *app, struct nk_context *ctx)
{
    unsigned char c[4];
    struct nk_color col = reaktor_style_token("--focus", c)
                        ? col_of(c) : nk_rgb(0x56, 0xc7, 0xff);
    struct nk_rect r = app->focus_rect;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_rect save = cv->clip;
    int n, i, in_page = 0;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) {
            in_page = node_under(t, n, i, app->page_node);
            break;
        }
    if (in_page) nk_push_scissor(cv, app->body_rect);
    nk_stroke_rect(cv,
                   nk_rect(r.x - 2.0f, r.y - 2.0f, r.w + 4.0f, r.h + 4.0f),
                   5.0f, 2.0f, col);
    if (in_page) nk_push_scissor(cv, save);
}
