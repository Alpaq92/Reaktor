#include "internal.h"
#include "reaktor/launch.h"

enum {
    FOCUS_NEXT = 1, FOCUS_PREV,
    FOCUS_FIRST, FOCUS_LAST,
    FOCUS_SIB_NEXT, FOCUS_SIB_PREV
};

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
        return !(n->state & REAKTOR_A11Y_DISABLED);
    default:
        return 0;
    }
}

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
    int n, i, cur = -1, pick = -1, lo, hi;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n; i++)
        if (t[i].id == app->focus_id) { cur = i; break; }
    lo = 0;
    hi = n - 1;
    if (app->popup_hi >= 0 && app->popup_hi < n) {
        lo = app->popup_lo;
        hi = app->popup_hi;
    } else if (app->trap_hi >= 0 && app->trap_hi < n) {
        lo = app->trap_lo;
        hi = app->trap_hi;
    }

    if (step == FOCUS_FIRST || step == FOCUS_LAST) {
        int d = step == FOCUS_FIRST ? 1 : -1;
        for (i = d > 0 ? lo : hi; i >= lo && i <= hi; i += d)
            if (focusable(&t[i])) { pick = i; break; }
    } else {
        int d   = (step == FOCUS_NEXT || step == FOCUS_SIB_NEXT) ? 1 : -1;
        int sib = (step == FOCUS_SIB_NEXT || step == FOCUS_SIB_PREV) && cur >= 0;
        int k, start = cur >= 0 ? cur : (d > 0 ? -1 : n);
        for (k = 1; k <= n; k++) {
            i = ((start + d * k) % n + n) % n;
            if (i == cur) break;
            if (i < lo || i > hi || !focusable(&t[i])) continue;
            if (sib && t[i].parent != t[cur].parent) continue;
            pick = i;
            break;
        }
    }
    if (pick < 0) return;

    app->focus_id = t[pick].id;
    reaktor_a11y_set_focus(&app->a11y, app->focus_id);
    app->focus_rect    = t[pick].bounds;
    app->focus_seen    = 1;
    app->focus_visible = 1;
    app->dirty = 1;
    focus_reveal(app, t, n, pick);
    if (t[pick].role == REAKTOR_A11Y_TEXTBOX) reaktor_key_click_ask(app);
}

void
reaktor_key_click_ask(App *app)
{
    app->key_click = app->key_click >= KEY_CLICK_RELEASE ? KEY_CLICK_AGAIN
                                                         : KEY_CLICK_ASKED;
}

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

int
focus_key(App *app, const SDL_Event *event)
{
    int editing = app->editing;

    if (event->key.key == SDLK_TAB) {
        if (editing) app->stop_editing = 1;
        focus_move(app, (event->key.mod & SDL_KMOD_SHIFT) ? FOCUS_PREV
                                                         : FOCUS_NEXT);
        app->dirty = 1;
        return 1;
    }
    if (editing) {
        /* The caret's, and the panel would scroll the field out of sight. */
        switch (event->key.key) {
        case SDLK_HOME:
            nk_input_key(app->ctx, NK_KEY_TEXT_START, nk_true);
            return 1;
        case SDLK_END:
            nk_input_key(app->ctx, NK_KEY_TEXT_END, nk_true);
            return 1;
        default:
            return 0;
        }
    }

    switch (event->key.key) {
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
        app->activate_id   = app->focus_id;
        app->focus_visible = 1;
        if (!app->dragging) app->restore_rate = 2;
        break;
    case SDLK_ESCAPE:
        app->focus_visible = 0;
        app->dirty = 1;
        return 0;
    default:
        return 0;
    }
    app->dirty = 1;
    return 1;
}

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

    reader_focus(user, id);
    if (app->focus_id != id) return;
    app->activate_id = id;
    reaktor_wake(app);
}

static int
held_back(const App *app)
{
    return reaktor_popup_open(app->ctx->current) || app->layer_held;
}

int
reaktor_focus_activated(App *app, unsigned id)
{
    if (!id || id != app->activate_id) return 0;
    app->activate_id = 0;
    return !held_back(app);
}

void
reaktor_focus_area(App *app, struct nk_rect area, unsigned node)
{
    app->body_rect = area;
    app->page_node = node;
}

int
reaktor_focus_scroll(App *app, struct nk_rect *reveal)
{
    if (!app->focus_scroll) return 0;
    app->focus_scroll = 0;
    if (reveal) *reveal = app->focus_scroll_rect;
    return 1;
}

int
reaktor_focus_step(App *app, unsigned id)
{
    int s;

    if (!id || id != app->focus_id || !app->focus_step) return 0;
    s = app->focus_step;
    app->focus_step = 0;
    return held_back(app) ? 0 : s;
}

int
reaktor_focus_covered(App *app)
{
    const struct nk_context *ctx = app->ctx;
    const struct nk_window *focus = app->focus_win, *w;
    float x = app->focus_rect.x + app->focus_rect.w * 0.5f;
    float y = app->focus_rect.y + app->focus_rect.h * 0.5f;
    int above = 0;

    if (reaktor_popup_open(focus)) {
        const struct nk_rect h = focus->popup.header;

        if (focus->popup.type != NK_PANEL_COMBO ||
            !NK_INBOX(x, y, h.x, h.y, h.w, h.h))
            return 1;
    }
    for (w = ctx->begin; w; w = w->next) {
        const struct nk_window *p = reaktor_popup_open(w) ? w->popup.win : NULL;

        if (p && w != focus && p != focus && p->seq == ctx->seq &&
            NK_INBOX(x, y, p->bounds.x, p->bounds.y, p->bounds.w, p->bounds.h))
            return 1;
        if (above && w->seq == ctx->seq && !(w->flags & NK_WINDOW_HIDDEN) &&
            NK_INBOX(x, y, w->bounds.x, w->bounds.y, w->bounds.w, w->bounds.h))
            return 1;
        if (w == focus) above = 1;
    }
    return 0;
}

void
focus_ring(App *app, struct nk_context *ctx, struct nk_command_buffer *cv)
{
    struct nk_color col = reaktor_token("--focus", nk_rgb(0x56, 0xc7, 0xff));
    struct nk_rect r = app->focus_rect;
    struct nk_rect save = cv->clip;
    int n, i, in_page = 0;
    const reaktor_a11y_node *t = reaktor_a11y_tree(&app->a11y, &n);

    for (i = 0; i < n && app->focus_layer == FOCUS_PAGE; i++)
        if (t[i].id == app->focus_id) {
            in_page = node_under(t, n, i, app->page_node);
            break;
        }
    if (in_page) nk_push_scissor(cv, app->body_rect);
    /* Truncated as nk_widget truncates the chrome it surrounds. */
    r = reaktor_rect_trunc(r);
    reaktor_edge_round(app, cv,
                       nk_rect(r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f),
                       ctx->style.button.rounding + 3.0f, 2.0f, col);
    if (in_page) nk_push_scissor(cv, save);
}

/* Popups draw after every window; the ring goes in the overlay, drawn last. */
void
reaktor_focus_ring_overlay(App *app, struct nk_context *ctx)
{
    struct nk_command_buffer *o = &ctx->overlay;

    o->base         = &ctx->memory;
    o->use_clipping = NK_CLIPPING_ON;
    o->clip         = REAKTOR_NO_CLIP;
    o->begin = o->end = o->last = ctx->memory.allocated;
    focus_ring(app, ctx, o);
    o->end = ctx->memory.allocated;
}
