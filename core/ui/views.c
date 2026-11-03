#include <stdio.h>

#include "internal.h"
#include "declare.h"
#include "reaktor/launch.h"

typedef struct nav_look {
    const struct nk_user_font *font;
    reaktor_button_look        btn;
} nav_look;

static void
nav_look_of(nav_look *k, const char *sel)
{
    k->font = decl_style_font(sel);
    reaktor_button_look_of(sel, &k->btn);
}

static void
nav_face(struct nk_rect b, const nav_look *k, const char *icon, const char *label,
         const reaktor_style *edge, int across)
{
    struct nk_command_buffer *cv = nk_window_get_canvas(g_frame_ctx);
    const struct nk_user_font *f = k->font;
    float px = (float)(int)(f->height + 0.5f), x;
    struct nk_color fg;

    if ((g_frame_ctx->last_widget_state & NK_WIDGET_STATE_HOVER) && k->btn.hov.matched &&
        k->btn.hov.fg[3])
        fg = nk_rgba_bv(k->btn.hov.fg);
    else
        fg = k->btn.s.matched && k->btn.s.fg[3] ? nk_rgba_bv(k->btn.s.fg) : g_frame_app->text;
    if (edge && edge->matched && edge->bg[3]) {
        struct nk_rect e = reaktor_rect_trunc(b);
        float t = across ? (edge->height > 0.0f ? edge->height : 2.0f)
                         : (edge->width > 0.0f ? edge->width : 3.0f);
        struct nk_rect r = across ? nk_rect(e.x, e.y + e.h - t, e.w, t)
                                  : nk_rect(e.x, e.y + e.h * 0.25f, t, e.h * 0.5f);

        reaktor_fill_round(g_frame_app, cv, r, t * 0.5f, nk_rgba_bv(edge->bg));
    }
    x = label ? b.x + (k->btn.s.pad[REAKTOR_SIDE_LEFT] > 0.0f
                       ? k->btn.s.pad[REAKTOR_SIDE_LEFT] : 12.0f)
              : b.x + (float)(int)((b.w - px) * 0.5f);
    if (icon) {
        struct nk_image im = reaktor_ionicon_col(g_frame_app, icon, (int)px, fg);

        nk_draw_image(cv, nk_rect(x, (float)(int)(b.y + (b.h - px) * 0.5f), px, px),
                      &im, nk_rgb(255, 255, 255));
        x += px + 10.0f;
    }
    if (label)
        nk_draw_text(cv, nk_rect(x, (float)(int)(b.y + (b.h - f->height) * 0.5f),
                                 b.x + b.w - x, f->height),
                     label, (int)strlen(label), f, nk_rgba(0, 0, 0, 0), fg);
}

static int
nav_list(const reaktor_nav_spec *s, int across)
{
    static const unsigned char list_role[3] = {
        REAKTOR_A11Y_TABLIST, REAKTOR_A11Y_GROUP, REAKTOR_A11Y_GROUP
    };
    static const unsigned char item_role[3] = {
        REAKTOR_A11Y_TAB, REAKTOR_A11Y_RADIO, REAKTOR_A11Y_BUTTON
    };
    static const unsigned mark[3] = { REAKTOR_A11Y_SELECTED, REAKTOR_A11Y_CHECKED, 0u };
    const char *item;
    nav_look look[2];
    reaktor_style edge;
    reaktor_box list, row, down;
    struct nk_rect r;
    unsigned id;
    int i, kind, picked = -1;

    if (!g_frame_app || !s || !s->chosen) return 0;
    kind = s->kind <= REAKTOR_NAV_ACTIONS ? s->kind : REAKTOR_NAV_PAGES;
    item = decl_rule_or(s->style, across ? ".tab" : ".sidebar-item");
    nav_look_of(&look[0], item);
    nav_look_of(&look[1], across ? ".tab-current" : ".sidebar-current");
    reaktor_style_get(across ? ".tab-edge" : ".sidebar-edge", &edge);
    list = s->box;
    reaktor_box_open(across ? REAKTOR_LAY_ROW : REAKTOR_LAY_COLUMN, &list);
    memset(&down, 0, sizeof(down));
    down.flags = REAKTOR_LAY_FILL_X;
    if (!across) decl_box_from_style(&down, NULL, NULL, item, 12.0f, 8.0f);
    id = reaktor_note_push(g_frame_app, list_role[kind], s->name, NULL, 0u,
                           reaktor_box_rect(&r) ? r : nk_rect(0, 0, 0, 0));
    if (s->list_keys) reaktor_note_keys(g_frame_app, id, s->list_keys);
    for (i = 0; i < s->count; i++) {
        int on = kind != REAKTOR_NAV_ACTIONS && i == *s->chosen;
        const char *icon = s->icons ? s->icons[i] : NULL;
        const char *label = s->narrow && icon ? NULL : s->items[i];

        row = down;
        if (across) {
            memset(&row, 0, sizeof(row));
            decl_box_from_style(&row, label ? label : "", NULL, item, 14.0f, 8.0f);
            if (icon && label) row.w += look[0].font->height + 10.0f;
        }
        if (!decl_place(item_role[kind], s->items[i], NULL, on ? mark[kind] : 0u,
                        s->keys ? s->keys[i] : NULL, &row, &id))
            continue;
        r = nk_widget_bounds(g_frame_ctx);
        reaktor_note_mute(g_frame_app, 1);
        if (reaktor_css_button_look(g_frame_app, g_frame_ctx, &look[on].btn, "")) picked = i;
        reaktor_note_mute(g_frame_app, 0);
        nav_face(r, &look[on], icon, label, on ? &edge : NULL, across);
        if (reaktor_focus_activated(g_frame_app, id)) picked = i;
    }
    reaktor_note_pop(g_frame_app);
    reaktor_box_close();
    if (picked < 0 || (kind != REAKTOR_NAV_ACTIONS && picked == *s->chosen))
        return 0;
    *s->chosen = picked;
    return 1;
}

int
reaktor_sidebar(const reaktor_sidebar_spec *s)
{
    return nav_list(s, 0);
}

int
reaktor_tabs(const reaktor_tabs_spec *s)
{
    return nav_list(s, 1);
}

/* A tree is a list whose first column is indented, with a chevron on each branch. */
typedef struct tree_rows {
    const reaktor_tree_spec *s;
    int *node, *depth, *row;
    int  n, jump;
} tree_rows;

static int
tree_branch(const tree_rows *t, int node)
{
    return node + 1 < t->s->count && t->s->parent(node + 1, t->s->user) == node;
}

static const char *
tree_cell(int r, int c, void *user)
{
    const tree_rows *t = (const tree_rows *)user;

    (void)c;
    return t->s->label(t->node[r], t->s->user);
}

static int
list_neighbor(const reaktor_list_spec *s, int r, struct nk_rect area, float row_h,
              float scrolled, int focused, const tree_rows *t)
{
    struct nk_rect row = nk_rect(area.x, area.y + (float)r * row_h - scrolled,
                                 area.w, row_h);
    unsigned id = reaktor_note(g_frame_app,
                               t ? REAKTOR_A11Y_TREEITEM : REAKTOR_A11Y_LISTITEM,
                               s->cell(r, 0, s->user),
                               NULL, s->selected && *s->selected == r
                                     ? REAKTOR_A11Y_SELECTED : 0u, row);

    return id && id == g_frame_app->focus_id ? r : focused;
}

static int
list_follow(const reaktor_list_spec *s, int focused, struct nk_rect area, float row_h)
{
    static unsigned seen;
    const char     *name = s->name ? s->name : "list";
    nk_uint         x, y;
    float           top;
    int             changed = 0;

    if (focused < 0 || g_frame_app->focus_id == seen) return 0;
    seen = g_frame_app->focus_id;
    if (s->selected && *s->selected != focused) {
        *s->selected = focused;
        changed      = 1;
    }
    nk_group_get_scroll(g_frame_ctx, name, &x, &y);
    top = (float)focused * row_h;
    if (top < (float)y)
        y = (nk_uint)top;
    else if (top + row_h > (float)y + area.h)
        y = (nk_uint)(top + row_h - area.h);
    nk_group_set_scroll(g_frame_ctx, name, x, y);
    g_frame_app->dirty = 1;
    reaktor_wake(g_frame_app);
    return changed;
}

/* Row r's first column in a tree; 1 when the selection should move to t->jump. */
static int
tree_first(tree_rows *t, int r, const char *label, unsigned id, struct nk_color fg, float pad)
{
    struct nk_context *ctx = g_frame_ctx;
    const struct nk_user_font *f = ctx->style.font;
    const reaktor_tree_spec *s = t->s;
    int node = t->node[r], branch = tree_branch(t, node), step, up;
    struct nk_rect b, chev;
    const char *icon = s->icon ? s->icon(node, s->user) : NULL;
    float x;

    nk_widget(&b, ctx);
    x = b.x + pad + 16.0f * (float)t->depth[node];
    chev = nk_rect(x, b.y, 16.0f, b.h);
    if (branch) {
        reaktor_chevron_at(g_frame_app, ctx, chev,
                           s->open[node] ? "chevron-down-outline" : "chevron-forward-outline",
                           fg);
        if (nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, chev))
            s->open[node] = !s->open[node];
    }
    x += 20.0f;
    if (icon) {
        float px = (float)(int)(f->height + 0.5f);
        struct nk_image im = reaktor_ionicon_col(g_frame_app, icon, (int)px, fg);

        nk_draw_image(nk_window_get_canvas(ctx),
                      nk_rect(x, (float)(int)(b.y + (b.h - px) * 0.5f), px, px), &im,
                      nk_rgb(255, 255, 255));
        x += px + 6.0f;
    }
    if (label)
        nk_draw_text(nk_window_get_canvas(ctx),
                     nk_rect(x, (float)(int)(b.y + (b.h - f->height) * 0.5f),
                             b.x + b.w - x, f->height),
                     label, (int)strlen(label), f, nk_rgba(0, 0, 0, 0), fg);
    if (t->jump == node) {
        reaktor_focus_take(g_frame_app, id, b);
        t->jump = -1;
    }
    step = reaktor_focus_step(g_frame_app, id);
    if (step > 0 && branch && !s->open[node]) s->open[node] = 1;
    else if (step > 0 && branch && r + 1 < t->n) t->jump = t->node[r + 1];
    else if (step < 0 && branch && s->open[node]) s->open[node] = 0;
    else if (step < 0 && (up = s->parent(node, s->user)) >= 0 && up < s->count) t->jump = up;
    if (step) {
        g_frame_app->dirty = 1;
        reaktor_wake(g_frame_app);
    }
    return step && t->jump >= 0;
}

static int list_draw(const reaktor_list_spec *s, tree_rows *t);

/* Rows with .drag start a drag; a list of the same kind shows where it lands. */
static float
list_drag(const reaktor_list_spec *s, struct nk_rect area, float row_h, int *changed)
{
    App *app = g_frame_app;
    struct nk_context *ctx = g_frame_ctx;
    const struct nk_input *in = &ctx->input;
    const char *name = s->name ? s->name : "list";
    struct nk_vec2 m = in->mouse.pos;
    nk_uint sx = 0, sy = 0;
    int at;

    nk_group_get_scroll(ctx, name, &sx, &sy);
    at = (int)((m.y - area.y + (float)sy) / row_h + 0.5f);
    if (nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) &&
        nk_input_is_mouse_hovering_rect(in, area) && at >= 0) {
        int r = (int)((m.y - area.y + (float)sy) / row_h);
        const char *label = r < s->count ? s->cell(r, 0, s->user) : NULL;

        if (r < s->count) {
            SDL_strlcpy(app->drag.kind, s->drag, sizeof app->drag.kind);
            SDL_strlcpy(app->drag.from, name, sizeof app->drag.from);
            SDL_strlcpy(app->drag.label, label ? label : "", sizeof app->drag.label);
            app->drag.item  = r;
            app->drag.armed = 1;
            app->drag.at    = m;
        }
    }
    if (app->drag.armed && !app->drag.active && SDL_strcmp(app->drag.from, name) == 0 &&
        (SDL_fabsf(m.x - app->drag.at.x) > 4.0f || SDL_fabsf(m.y - app->drag.at.y) > 4.0f))
        app->drag.active = 1;
    if (!app->drag.active || SDL_strcmp(app->drag.kind, s->drag) != 0) return -1.0f;
    if (SDL_strcmp(app->drag.from, name) == 0) app->tip = app->drag.label;
    if (!nk_input_is_mouse_hovering_rect(in, area)) return -1.0f;
    if (at > s->count) at = s->count;
    if (nk_input_is_mouse_released(in, NK_BUTTON_LEFT) && s->dropped) {
        s->dropped(app->drag.from, app->drag.item, at, s->user);
        app->drag.active = app->drag.armed = 0;
        *changed = 1;
        return -1.0f;
    }
    return area.y + (float)at * row_h - (float)sy - 1.0f;
}

int
reaktor_list(const reaktor_list_spec *s)
{
    return list_draw(s, NULL);
}

int
reaktor_tree(const reaktor_tree_spec *s)
{
    static int        *buf, cap, jump = -1;
    static const char *jump_in;
    tree_rows          t;
    reaktor_list_spec  l;
    int                i, sel = -1, changed;

    if (!g_frame_app || !s || s->count < 0 || !s->parent || !s->label || !s->open) return 0;
    if (s->count * 3 > cap) {
        int *more = (int *)SDL_realloc(buf, (size_t)s->count * 3 * sizeof *buf);

        if (!more) return 0;
        buf = more;
        cap = s->count * 3;
    }
    t.s     = s;
    t.node  = buf;
    t.depth = buf + s->count;
    t.row   = buf + 2 * s->count;
    t.n     = 0;
    t.jump  = jump_in == s->name && jump < s->count ? jump : -1;
    for (i = 0; i < s->count; i++) {
        int p = s->parent(i, s->user);

        t.depth[i] = p >= 0 && p < i ? t.depth[p] + 1 : 0;
        t.row[i]   = p < 0 || (p < i && t.row[p] >= 0 && s->open[p]) ? t.n : -1;
        if (t.row[i] >= 0) t.node[t.n++] = i;
    }
    if (s->selected && *s->selected >= 0 && *s->selected < s->count)
        sel = t.row[*s->selected];
    memset(&l, 0, sizeof l);
    l.name     = s->name;
    l.count    = t.n;
    l.cell     = tree_cell;
    l.user     = &t;
    l.selected = s->selected ? &sel : NULL;
    l.row_h    = s->row_h;
    l.box      = s->box;
    changed = list_draw(&l, &t);
    if (s->selected && sel >= 0 && sel < t.n && t.node[sel] != *s->selected) {
        *s->selected = t.node[sel];
        changed = 1;
    }
    jump    = t.jump;
    jump_in = s->name;
    return changed;
}

static int
list_draw(const reaktor_list_spec *s, tree_rows *t)
{
    const struct nk_user_font *head_f, *cell_f;
    struct nk_list_view        view;
    struct nk_color            odd, even, head_bg, none = nk_rgba(0, 0, 0, 0);
    reaktor_style              thead, th;
    reaktor_box                head = { 0 }, body = { 0 }, box;
    float                      ratio[REAKTOR_LIST_COLUMNS], sum = 0.0f, row_h, pad;
    int                        cols, c, r, shown, changed = 0;

    if (!g_frame_app || !s || !s->cell || s->count < 0) return 0;
    cols = s->column_count > 0 ? s->column_count : 1;
    if (cols > REAKTOR_LIST_COLUMNS) cols = REAKTOR_LIST_COLUMNS;
    for (c = 0; c < cols; c++)
        sum += ratio[c] = s->widths && s->widths[c] > 0.0f ? s->widths[c] : 1.0f;
    for (c = 0; c < cols; c++) ratio[c] /= sum;

    reaktor_style_get("thead", &thead);
    reaktor_style_get("th", &th);
    pad     = th.matched && th.pad_x > 0.0f ? th.pad_x : 8.0f;
    head_f  = decl_style_font("th");
    cell_f  = decl_style_font("td");
    row_h   = s->row_h > 0.0f ? s->row_h
            : (float)(int)(cell_f->height + 2.0f * (th.matched ? th.pad_y : 4.0f) + 0.5f);
    odd     = reaktor_token("--table-bg", none);
    even    = reaktor_token("--table-bg-alt", odd);
    head_bg = thead.matched && thead.bg[3] ? reaktor_col(thead.bg)
            : reaktor_token("--table", g_frame_app->accent);

    box = s->box;
    if (!box.name) box.name = s->name;
    if (box.w <= 0.0f) box.flags |= REAKTOR_LAY_FILL_X;
    shown = s->count < 8 ? s->count : 8;
    if (box.h <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_Y))
        box.h = row_h * (float)(shown + (s->columns ? 1 : 0));
    reaktor_box_open(REAKTOR_LAY_COLUMN, &box);

    if (s->columns) {
        head.h     = row_h;
        head.flags = REAKTOR_LAY_FILL_X;
        if (decl_place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &head, NULL)) {
            struct nk_command_buffer *cv = nk_window_get_canvas(g_frame_ctx);
            struct nk_rect            b  = nk_widget_bounds(g_frame_ctx);
            struct nk_color           fg = reaktor_on(head_bg);
            float                     x  = b.x;

            nk_spacing(g_frame_ctx, 1);
            nk_fill_rect(cv, b, 0.0f, head_bg);
            for (c = 0; c < cols; c++) {
                const char *t = s->columns[c] ? s->columns[c] : "";

                nk_draw_text(cv, nk_rect(x + pad, b.y + (b.h - head_f->height) * 0.5f,
                                         b.w * ratio[c] - pad, head_f->height),
                             t, (int)SDL_strlen(t), head_f, none, fg);
                x += b.w * ratio[c];
            }
        }
    }

    body.flags = REAKTOR_LAY_FILL;
    if (decl_place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &body, NULL)) {
        struct nk_rect area = nk_widget_bounds(g_frame_ctx);
        int            focused = -1;

        nk_style_push_vec2(g_frame_ctx, &g_frame_ctx->style.window.spacing, nk_vec2(0.0f, 0.0f));
        nk_style_push_vec2(g_frame_ctx, &g_frame_ctx->style.window.group_padding, nk_vec2(0.0f, 0.0f));
        nk_style_push_vec2(g_frame_ctx, &g_frame_ctx->style.text.padding, nk_vec2(pad, 0.0f));
        nk_style_push_font(g_frame_ctx, cell_f);
        float line = s->drag ? list_drag(s, area, row_h, &changed) : -1.0f;

        if (nk_list_view_begin(g_frame_ctx, &view, s->name ? s->name : "list",
                               s->count <= shown ? NK_WINDOW_NO_SCROLLBAR : 0,
                               (int)row_h, s->count)) {
            if (view.begin > 0)
                focused = list_neighbor(s, view.begin - 1, area, row_h,
                                        (float)view.scroll_value, focused, t);
            for (r = view.begin; r < view.end; r++) {
                struct nk_command_buffer *cv = nk_window_get_canvas(g_frame_ctx);
                struct nk_color fg = g_frame_ctx->style.text.color;
                int             on = s->selected && *s->selected == r;
                struct nk_rect  row;
                const char     *first = s->cell(r, 0, s->user);
                unsigned        id;

                nk_layout_row(g_frame_ctx, NK_DYNAMIC, row_h, cols, ratio);
                row   = nk_widget_bounds(g_frame_ctx);
                row.w = nk_window_get_content_region(g_frame_ctx).w;
                nk_fill_rect(cv, row, 0.0f, (r & 1) ? even : odd);
                if (s->selected) reaktor_hot(g_frame_app, row, REAKTOR_CURSOR_DEFAULT, 1);
                id = reaktor_note(g_frame_app,
                                  t ? REAKTOR_A11Y_TREEITEM : REAKTOR_A11Y_LISTITEM,
                                  first, NULL,
                                  (on ? REAKTOR_A11Y_SELECTED : 0u) |
                                  (t && tree_branch(t, t->node[r]) && t->s->open[t->node[r]]
                                   ? REAKTOR_A11Y_EXPANDED : 0u), row);
                if (id && id == g_frame_app->focus_id) focused = r;
                if (s->selected && on) {
                    nk_fill_rect(cv, row, 0.0f, g_frame_app->accent);
                    fg = reaktor_on(g_frame_app->accent);
                } else if (s->selected &&
                           nk_input_is_mouse_hovering_rect(&g_frame_ctx->input, row)) {
                    nk_fill_rect(cv, row, 0.0f,
                                 reaktor_token("--background-hover", nk_rgba(128, 128, 128, 60)));
                }
                for (c = 0; c < cols; c++) {
                    const char *cell = c ? s->cell(r, c, s->user) : first;

                    if (t && c == 0) {
                        if (tree_first(t, r, cell, id, fg, pad) && s->selected) {
                            int k = t->row[t->jump];

                            if (k >= 0 && *s->selected != k) {
                                *s->selected = k;
                                changed      = 1;
                            }
                        }
                        if (id && id == g_frame_app->focus_id) focused = r;
                        continue;
                    }
                    nk_label_colored(g_frame_ctx, cell ? cell : "", NK_TEXT_LEFT, fg);
                }
                if (nk_input_mouse_clicked(&g_frame_ctx->input, NK_BUTTON_LEFT, row)) on = 1;
                if (s->selected && *s->selected != r &&
                    (on || reaktor_focus_activated(g_frame_app, id))) {
                    *s->selected = r;
                    changed      = 1;
                }
            }
            if (view.end < s->count)
                focused = list_neighbor(s, view.end, area, row_h,
                                        (float)view.scroll_value, focused, t);
            nk_list_view_end(&view);
        }
        if (line >= 0.0f)
            nk_fill_rect(nk_window_get_canvas(g_frame_ctx), nk_rect(area.x, line, area.w, 2.0f),
                         0.0f, g_frame_app->accent);
        changed |= list_follow(s, focused, area, row_h);
        nk_style_pop_font(g_frame_ctx);
        nk_style_pop_vec2(g_frame_ctx);
        nk_style_pop_vec2(g_frame_ctx);
        nk_style_pop_vec2(g_frame_ctx);
    }
    reaktor_box_close();
    return changed;
}

int
reaktor_splitter(const reaktor_splitter_spec *s)
{
    static unsigned  dragging;
    reaktor_box      box = { 0 };
    struct nk_rect   b, parent;
    const struct nk_input *in;
    unsigned         id = 0;
    float            lo, hi, span, step;
    char             share[8];
    int              keys;

    if (!g_frame_app || !s || !s->value) return 0;
    lo = s->lo > 0.0f ? s->lo : 0.1f;
    hi = s->hi > 0.0f ? s->hi : 0.9f;
    if (s->vertical) { box.h = 6.0f; box.flags = REAKTOR_LAY_FILL_X; }
    else             { box.w = 6.0f; box.flags = REAKTOR_LAY_FILL_Y; }
    SDL_snprintf(share, sizeof share, "%d%%", (int)(*s->value * 100.0f + 0.5f));
    if (!decl_place(REAKTOR_A11Y_SLIDER, s->name ? s->name : "Splitter", share, 0u, NULL,
                    &box, &id))
        return 0;
    keys = reaktor_focus_step(g_frame_app, id);
    if (keys) {
        *s->value += (s->vertical ? -0.05f : 0.05f) * (float)keys;
        if (*s->value < lo) *s->value = lo;
        if (*s->value > hi) *s->value = hi;
        g_frame_app->dirty = 1;
        reaktor_wake(g_frame_app);
    }

    b = nk_widget_bounds(g_frame_ctx);
    nk_spacing(g_frame_ctx, 1);
    reaktor_hot(g_frame_app, b, s->vertical ? REAKTOR_CURSOR_RESIZE_Y : REAKTOR_CURSOR_RESIZE_X, 0);
    nk_fill_rect(nk_window_get_canvas(g_frame_ctx),
                 s->vertical ? nk_rect(b.x, b.y + b.h * 0.5f, b.w, 1.0f)
                             : nk_rect(b.x + b.w * 0.5f, b.y, 1.0f, b.h),
                 0.0f, reaktor_token("--background-hover", nk_rgba(128, 128, 128, 90)));

    in = &g_frame_ctx->input;
    if (nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, b, nk_true))
        dragging = id;
    if (!in->mouse.buttons[NK_BUTTON_LEFT].down && dragging == id) dragging = 0;
    if (dragging != id || !reaktor_box_rect(&parent)) return keys != 0;
    span = s->vertical ? parent.h : parent.w;
    step = s->vertical ? in->mouse.delta.y : in->mouse.delta.x;
    if (span <= 0.0f || step == 0.0f) return keys != 0;
    *s->value += step / span;
    if (*s->value < lo) *s->value = lo;
    if (*s->value > hi) *s->value = hi;
    return 1;
}

#define MENU_DEPTH 4
#define MENU_COLS  (MENU_DEPTH + 1)
#define MENU_ITEMS  32

/* The windows that draw a column run after the frame that declared it: they draw a copy. */
typedef struct menu_col {
    const reaktor_menu_entry *items, *src;
    reaktor_menu_entry        copy[MENU_ITEMS];
    int                       count;
    const char               *name;
    struct nk_rect            r;
    float                     lead;
} menu_col;

/* One menu bar is open at a time; its columns are read by the windows that draw them. */
static struct {
    int        open, depth, kcol, kbd, keyed, focused;
    int        path[MENU_DEPTH], hi[MENU_COLS], win[MENU_COLS];
    menu_col   col[MENU_COLS];
    int        n, inline_;
    unsigned   open_tid;
    struct nk_rect open_title;
    int        picked_k, picked_i;
} g_menu = { -1 };

static int
menu_rule(const reaktor_menu_entry *e)
{
    return !e->label && !e->custom;
}

static int
menu_live(const reaktor_menu_entry *e)
{
    return e->label && !e->disabled;
}

static float
menu_entry_h(const reaktor_menu_entry *e)
{
    if (menu_rule(e)) return 1.0f;
    return e->custom && e->height > 0.0f ? e->height : REAKTOR_MENU_ROW;
}

static float
menu_entry_y(const menu_col *c, int k)
{
    float y = c->r.y + g_frame_ctx->style.window.menu_padding.y;
    int i;

    for (i = 0; i < k; i++) y += menu_entry_h(&c->items[i]) + REAKTOR_MENU_GAP;
    return y;
}

static void
menu_measure(menu_col *c, const struct nk_user_font *f, struct nk_vec2 pad)
{
    float w = 140.0f, h = 0.0f;
    int i;

    c->lead = 0.0f;
    for (i = 0; i < c->count; i++)
        if (c->items[i].icon || c->items[i].checked)
            c->lead = (float)(int)(f->height + 0.5f) + 8.0f;
    for (i = 0; i < c->count; i++) {
        const reaktor_menu_entry *e = &c->items[i];
        float iw = e->width;

        if (i) h += REAKTOR_MENU_GAP;
        h += menu_entry_h(e);
        if (!e->custom && e->label)
            iw = 28.0f + c->lead + reaktor_text_w(f, e->label) +
                 (e->keys ? 24.0f + reaktor_text_w(f, e->keys) : 0.0f) +
                 (e->items ? 22.0f : 0.0f);
        if (iw > w) w = iw;
    }
    c->r.w = (float)(int)w + 2.0f * pad.x;
    c->r.h = h + 2.0f * pad.y;
}

static void
menu_fit(menu_col *c, const menu_col *parent, struct nk_rect title, struct nk_rect b)
{
    if (!parent) {
        c->r.x = title.x;
        c->r.y = title.y + title.h;
        if (c->r.y + c->r.h > b.y + b.h && title.y - c->r.h >= b.y) c->r.y = title.y - c->r.h;
    } else {
        c->r.x = parent->r.x + parent->r.w;
        if (c->r.x + c->r.w > b.x + b.w) c->r.x = parent->r.x - c->r.w;
    }
    if (c->r.x + c->r.w > b.x + b.w) c->r.x = b.x + b.w - c->r.w;
    if (c->r.x < b.x) c->r.x = b.x;
    if (c->r.y + c->r.h > b.y + b.h) c->r.y = b.y + b.h - c->r.h;
    if (c->r.y < b.y) c->r.y = b.y;
}

static int
menu_step(const reaktor_menu_entry *items, int count, int from, int d)
{
    int i, k;

    for (k = 1; k <= count; k++) {
        i = ((from < 0 ? (d > 0 ? -1 : 0) : from) + d * k + count * 2) % count;
        if (menu_live(&items[i])) return i;
    }
    return -1;
}

static void
menu_changed(void)
{
    App *m = reaktor_main_app();

    int k;

    for (k = 0; k < MENU_COLS; k++) {
        App *w = g_menu.win[k] ? reaktor_window_app(g_menu.win[k]) : NULL;

        if (w) w->dirty = 1;
    }
    if (m) { m->dirty = 1; reaktor_wake(m); }
}

static void
menu_open_at(int m, int kbd)
{
    g_menu.open  = m;
    g_menu.picked_k = -1;
    g_menu.depth = 0;
    g_menu.kcol  = 0;
    g_menu.kbd   = kbd;
    g_menu.keyed = kbd;
    g_menu.hi[0] = -1;
}

static void
menu_draw_entry(const reaktor_menu_entry *e, struct nk_rect b, float lead, int lit)
{
    struct nk_context *ctx = g_frame_ctx;
    App *app = g_frame_app;
    const struct nk_user_font *f = ctx->style.font;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    const struct nk_style_button *st = &ctx->style.contextual_button;
    struct nk_color fg = e->disabled ? reaktor_token("--text-muted", nk_rgb(160, 160, 160))
                       : lit ? st->text_hover : st->text_normal;

    if (lit && st->hover.type == NK_STYLE_ITEM_COLOR)
        reaktor_fill_round(app, cv, b, st->rounding, st->hover.data.color);
    if (e->icon) {
        float px = (float)(int)(f->height + 0.5f);
        struct nk_image im = reaktor_ionicon_col(app, e->icon, (int)px, fg);

        nk_draw_image(cv, nk_rect(b.x + 8.0f + (lead - 8.0f - px) * 0.5f,
                                  (float)(int)(b.y + (b.h - px) * 0.5f), px, px),
                      &im, nk_rgb(255, 255, 255));
    } else if (e->checked && *e->checked) {
        float px = (float)(int)(f->height * 0.45f);

        reaktor_glyph_at(app, ctx,
                         nk_rect(b.x + 8.0f + (lead - 8.0f - px) * 0.5f,
                                 (float)(int)(b.y + (b.h - px) * 0.5f), px, px),
                         REAKTOR_DISC_ROUND, fg, (int)px, 0.0f);
    }
    nk_draw_text(cv, nk_rect(b.x + 8.0f + lead, (float)(int)(b.y + (b.h - f->height) * 0.5f),
                             b.w - 16.0f - lead, f->height),
                 e->label, (int)strlen(e->label), f, nk_rgba(0, 0, 0, 0), fg);
    if (e->keys) {
        float w = reaktor_text_w(f, e->keys);

        nk_draw_text(cv, nk_rect(b.x + b.w - w - 8.0f,
                                 (float)(int)(b.y + (b.h - f->height) * 0.5f), w, f->height),
                     e->keys, (int)strlen(e->keys), f, nk_rgba(0, 0, 0, 0),
                     reaktor_token("--text-muted", nk_rgb(160, 160, 160)));
    }
    if (e->items)
        reaktor_chevron_at(app, ctx, nk_rect(b.x + b.w - 22.0f, b.y, 16.0f, b.h),
                           "chevron-forward-outline", fg);
}

/* Draws column k with its top left at `at`, in whichever window holds it. */
static void
menu_draw_col(int k, struct nk_vec2 at)
{
    struct nk_context *ctx = g_frame_ctx;
    App *app = g_frame_app;
    const menu_col *c = &g_menu.col[k];
    struct nk_vec2 pad = ctx->style.window.menu_padding;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_color fill = reaktor_token("--background", nk_rgb(53, 53, 53));
    struct nk_rect panel = nk_rect(at.x, at.y, c->r.w, c->r.h);
    float y = pad.y, rr = g_menu.inline_ ? reaktor_popup_rounding() : 0.0f;
    int i;

    reaktor_fill_round(app, cv, panel, rr, reaktor_token("--background-hover", fill));
    reaktor_fill_round(app, cv, nk_rect(panel.x + 1.0f, panel.y + 1.0f,
                                        panel.w - 2.0f, panel.h - 2.0f),
                       rr > 1.0f ? rr - 1.0f : 0.0f, fill);
    reaktor_note_push(app, REAKTOR_A11Y_MENU, c->name, NULL, REAKTOR_A11Y_EXPANDED, panel);
    for (i = 0; i < c->count; i++) {
        const reaktor_menu_entry *e = &c->items[i];
        float eh = menu_entry_h(e);
        struct nk_rect slot = nk_rect(at.x + pad.x, at.y + y, c->r.w - 2.0f * pad.x, eh);
        int open = k < g_menu.n - 1 && g_menu.path[k] == i;
        unsigned state = 0u, id;

        y += eh + REAKTOR_MENU_GAP;
        nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, slot));
        if (menu_rule(e)) {
            nk_rule_horizontal(ctx, reaktor_token("--background-hover", nk_rgb(90, 90, 90)),
                               nk_false);
            continue;
        }
        if (e->custom) {
            e->custom(app, ctx, e->on_press.user);
            continue;
        }
        if (e->checked && *e->checked) state |= REAKTOR_A11Y_CHECKED;
        if (e->disabled) state |= REAKTOR_A11Y_DISABLED;
        if (open) state |= REAKTOR_A11Y_EXPANDED;
        id = reaktor_note(app, REAKTOR_A11Y_MENUITEM, e->label, NULL, state, slot);
        if (e->keys) reaktor_note_keys(app, id, e->keys);
        if (!e->disabled) {
            reaktor_hot_top(app, slot, 1, 1);
            if (nk_input_is_mouse_hovering_rect(&ctx->input, slot) &&
                nk_input_is_mouse_moved(&ctx->input) &&
                (g_menu.hi[k] != i || g_menu.kcol != k || g_menu.depth != k + !!e->items)) {
                g_menu.hi[k] = i;
                g_menu.kcol  = k;
                g_menu.depth = k + !!e->items;
                if (e->items) { g_menu.path[k] = i; g_menu.hi[k + 1] = -1; }
                menu_changed();
            }
        }
        if (nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT) &&
            nk_input_is_mouse_hovering_rect(&ctx->input, slot) && !e->disabled && !e->items) {
            g_menu.picked_k = k;
            g_menu.picked_i = i;
            menu_changed();
        }
        nk_spacing(ctx, 1);
        if (g_menu.keyed && g_menu.kcol == k && g_menu.hi[k] == i) {
            app->focus_id = id;
            reaktor_a11y_set_focus(&app->a11y, id);
        }
        menu_draw_entry(e, slot, c->lead, open || (g_menu.hi[k] == i && g_menu.kcol >= k));
    }
    reaktor_note_pop(app);
}

static void
menu_window(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    int k = (int)(intptr_t)user;

    struct nk_rect o;

    (void)app; (void)w;
    if (k >= g_menu.n) return;
    nk_layout_space_begin(ctx, NK_STATIC, (float)h, g_menu.col[k].count);
    o = nk_layout_space_rect_to_screen(ctx, nk_rect(0, 0, 0, 0));
    menu_draw_col(k, nk_vec2(o.x, o.y));
    nk_layout_space_end(ctx);
}

static void
menu_key(const reaktor_menubar_spec *s, App *app)
{
    SDL_Keycode key = app->popup_key;
    int k = g_menu.kcol, *hi = &g_menu.hi[k];
    const menu_col *c = &g_menu.col[k];
    const reaktor_menu_entry *e = *hi >= 0 ? &c->items[*hi] : NULL;

    app->popup_key = 0;
    if (!key) return;
    g_menu.keyed = 1;
    menu_changed();
    switch (key) {
    case SDLK_UP:   *hi = menu_step(c->items, c->count, *hi, -1); return;
    case SDLK_DOWN: *hi = menu_step(c->items, c->count, *hi, 1);  return;
    case SDLK_HOME: *hi = menu_step(c->items, c->count, -1, 1);   return;
    case SDLK_END:  *hi = menu_step(c->items, c->count, -1, -1);  return;
    case SDLK_RIGHT: case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
        if (e && e->items && k < MENU_DEPTH) {
            g_menu.path[k] = *hi;
            g_menu.depth   = k + 1;
            g_menu.kcol    = k + 1;
            g_menu.hi[k + 1] = menu_step(e->items, e->count, -1, 1);
        } else if (key != SDLK_RIGHT && e) {
            g_menu.picked_k = k;
            g_menu.picked_i = *hi;
        } else if (key == SDLK_RIGHT) {
            menu_open_at((g_menu.open + 1) % s->count, 1);
        }
        return;
    case SDLK_LEFT:
        if (k > 0) { g_menu.depth = k - 1; g_menu.kcol = k - 1; }
        else menu_open_at((g_menu.open + s->count - 1) % s->count, 1);
        return;
    case SDLK_ESCAPE:
        if (k > 0) { g_menu.depth = k - 1; g_menu.kcol = k - 1; return; }
        reaktor_focus_take(app, g_menu.open_tid, g_menu.open_title);
        g_menu.open = -1;
        return;
    default:
        return;
    }
}

static void
menu_build(const reaktor_menubar_spec *s)
{
    struct nk_context *ctx = g_frame_ctx;
    struct nk_vec2 pad = ctx->style.window.menu_padding;
    const reaktor_menu *menu = &s->menus[g_menu.open];
    struct nk_rect b = nk_rect(0.0f, 0.0f, (float)g_frame_app->laid_w,
                               (float)g_frame_app->laid_h);
    int k;

    g_menu.inline_ = 1;
#ifndef __EMSCRIPTEN__
    g_menu.inline_ = !reaktor_window_bounds(&b);
#endif
    g_menu.col[0].items = menu->items;
    g_menu.col[0].count = menu->count;
    g_menu.col[0].name  = menu->label;
    g_menu.n = 1;
    for (k = 0; k < g_menu.n; k++) {
        menu_col *c = &g_menu.col[k];
        int at;

        c->src   = c->items;
        c->count = NK_MIN(c->count, MENU_ITEMS);
        SDL_memcpy(c->copy, c->src, (size_t)c->count * sizeof *c->copy);
        c->items = c->copy;
        if (k >= g_menu.depth || g_menu.n == MENU_COLS) break;
        at = g_menu.path[k];
        if (at < 0 || at >= c->count || !c->items[at].items) break;
        g_menu.col[g_menu.n].items = c->items[at].items;
        g_menu.col[g_menu.n].count = c->items[at].count;
        g_menu.col[g_menu.n].name  = c->items[at].label;
        g_menu.n++;
    }
    g_menu.depth = g_menu.n - 1;
    if (g_menu.kcol > g_menu.depth) g_menu.kcol = g_menu.depth;
    for (k = 0; k < g_menu.n; k++) {
        menu_col *c = &g_menu.col[k];

        menu_measure(c, ctx->style.font, pad);
        menu_fit(c, k ? &g_menu.col[k - 1] : NULL, g_menu.open_title, b);
        if (k) {
            c->r.y = menu_entry_y(&g_menu.col[k - 1], g_menu.path[k - 1]) - pad.y;
            menu_fit(c, &g_menu.col[k - 1], g_menu.open_title, b);
            if (c->r.y + c->r.h > b.y + b.h) c->r.y = b.y + b.h - c->r.h;
        }
    }
}

static void
menu_windows(void)
{
    int k;

    for (k = 0; k < MENU_COLS; k++) {
        int want = g_menu.open >= 0 && !g_menu.inline_ && k < g_menu.n;

        if (!want && g_menu.win[k]) {
            reaktor_window_close(g_frame_app, g_menu.win[k]);
            g_menu.win[k] = 0;
        } else if (want && g_menu.win[k]) {
            reaktor_window_move(g_menu.win[k], g_menu.col[k].r);
        } else if (want) {
            g_menu.win[k] = reaktor_window_popup(g_menu.col[k].r, menu_window,
                                                 (void *)(intptr_t)k);
            if (!g_menu.win[k]) g_menu.inline_ = 1;
        }
    }
}

static int
menu_inside(struct nk_vec2 p)
{
    int k;

    for (k = 0; k < g_menu.n; k++)
        if (NK_INBOX(p.x, p.y, g_menu.col[k].r.x, g_menu.col[k].r.y,
                     g_menu.col[k].r.w, g_menu.col[k].r.h))
            return 1;
    return 0;
}

int
reaktor_menubar(const reaktor_menubar_spec *s)
{
    struct nk_context *ctx = g_frame_ctx;
    App *app = g_frame_app;
    const struct nk_style_button *mb;
    const struct nk_user_font *f;
    reaktor_box bar, title;
    struct nk_rect r;
    float h;
    int m, picked = 0, on_title = 0;

    if (!app || !s || !s->menus || s->count <= 0) return 0;
    f  = ctx->style.font;
    mb = &ctx->style.menu_button;
    h  = (float)(int)(f->height + 2.0f * (mb->padding.y + mb->border) + 0.5f);
    if (h < 26.0f) h = 26.0f;
    if (g_menu.open >= s->count) g_menu.open = -1;
    {
        int focused = (SDL_GetWindowFlags(app->win) & SDL_WINDOW_INPUT_FOCUS) != 0;

        if (g_menu.focused && !focused) g_menu.open = -1;
        g_menu.focused = focused;
    }
    if (g_menu.open >= 0) menu_key(s, app);

    bar = s->box;
    decl_box_default(&bar, 0.0f, h);
    reaktor_box_open(REAKTOR_LAY_ROW, &bar);
    reaktor_note_push(app, REAKTOR_A11Y_MENUBAR, s->name ? s->name : "Menu bar",
                      NULL, 0u, reaktor_box_rect(&r) ? r : nk_rect(0, 0, 0, 0));
    for (m = 0; m < s->count; m++) {
        const char *label = s->menus[m].label;
        struct nk_style_button st = *mb;
        unsigned tid = 0;

        memset(&title, 0, sizeof(title));
        title.w = (float)(int)(reaktor_text_w(f, label) +
                               2.0f * (mb->padding.x + mb->border + mb->rounding) + 2.5f);
        title.h = h;
        if (!decl_place(REAKTOR_A11Y_MENU, label, NULL,
                        g_menu.open == m ? REAKTOR_A11Y_EXPANDED : 0u, NULL, &title,
                        &tid))
            continue;
        r = nk_widget_bounds(ctx);
        reaktor_hot(app, r, REAKTOR_CURSOR_POINTER, 1);
        if (g_menu.open == m) { st.normal = st.active; st.text_normal = st.text_active; }
        nk_button_text_styled(ctx, &st, label, (int)strlen(label));
        if (nk_input_is_mouse_hovering_rect(&ctx->input, r)) {
            on_title = 1;
            if (g_menu.open >= 0 && g_menu.open != m && nk_input_is_mouse_moved(&ctx->input))
                menu_open_at(m, 0);
            if (nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT)) {
                if (g_menu.open == m) g_menu.open = -1;
                else menu_open_at(m, app->focus_id == tid && app->focus_visible);
            }
        }
        if (g_menu.open == m) {
            g_menu.open_tid   = tid;
            g_menu.open_title = r;
        }
    }
    reaktor_note_pop(app);
    reaktor_box_close();

    if (g_menu.open >= 0) {
        menu_build(s);
        if (g_menu.kbd && g_menu.hi[0] < 0)
            g_menu.hi[0] = menu_step(g_menu.col[0].items, g_menu.col[0].count, -1, 1);
        g_menu.kbd = 0;
        if (!on_title && nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT) &&
            !(g_menu.inline_ && menu_inside(ctx->input.mouse.pos)))
            g_menu.open = -1;
    }
    menu_windows();
    if (g_menu.open >= 0 && g_menu.inline_) {
        struct nk_rect u = g_menu.col[0].r;
        int k;

        for (k = 1; k < g_menu.n; k++) u = nk_rect(NK_MIN(u.x, g_menu.col[k].r.x),
                                                   NK_MIN(u.y, g_menu.col[k].r.y),
                                                   0.0f, 0.0f);
        for (k = 0; k < g_menu.n; k++) {
            u.w = NK_MAX(u.w, g_menu.col[k].r.x + g_menu.col[k].r.w - u.x);
            u.h = NK_MAX(u.h, g_menu.col[k].r.y + g_menu.col[k].r.h - u.y);
        }
        if (reaktor_flyout_begin(ctx, "reaktor-menu", u)) {
            nk_layout_space_begin(ctx, NK_STATIC, u.h, 64);
            for (k = 0; k < g_menu.n; k++)
                menu_draw_col(k, nk_vec2(g_menu.col[k].r.x, g_menu.col[k].r.y));
            nk_layout_space_end(ctx);
            reaktor_flyout_end(ctx);
        }
    }
    if (g_menu.open >= 0) app->popup_open = 1;

    if (g_menu.open >= 0 && g_menu.picked_k >= 0 && g_menu.picked_k < g_menu.n &&
        g_menu.picked_i >= 0 && g_menu.picked_i < g_menu.col[g_menu.picked_k].count) {
        const reaktor_menu_entry *e = &g_menu.col[g_menu.picked_k].src[g_menu.picked_i];

        if (e->checked) *e->checked = !*e->checked;
        if (e->on_press.fn) e->on_press.fn(e->on_press.user);
        if (s->picked_menu) *s->picked_menu = g_menu.open;
        if (s->picked_item) *s->picked_item = g_menu.picked_i;
        if (s->picked) *s->picked = e;
        if (g_menu.open >= 0 && g_menu.keyed)
            reaktor_focus_take(app, g_menu.open_tid, g_menu.open_title);
        g_menu.picked_k = -1;
        g_menu.open     = -1;
        app->popup_open = 0;
        menu_windows();
        picked = 1;
    }
    return picked;
}
