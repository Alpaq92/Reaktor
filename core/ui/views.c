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

static int
list_neighbor(const reaktor_list_spec *s, int r, struct nk_rect area, float row_h,
              float scrolled, int focused)
{
    struct nk_rect row = nk_rect(area.x, area.y + (float)r * row_h - scrolled,
                                 area.w, row_h);
    unsigned id = reaktor_note(g_frame_app, REAKTOR_A11Y_LISTITEM, s->cell(r, 0, s->user),
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

int
reaktor_list(const reaktor_list_spec *s)
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
    head_bg = reaktor_visible(reaktor_visible(head_bg, reaktor_page_color(g_frame_app),
                                              g_frame_app->accent),
                              odd, g_frame_app->accent);

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

    body.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y;
    if (decl_place(REAKTOR_A11Y_NONE, NULL, NULL, 0u, NULL, &body, NULL)) {
        struct nk_rect area = nk_widget_bounds(g_frame_ctx);
        int            focused = -1;

        nk_style_push_vec2(g_frame_ctx, &g_frame_ctx->style.window.spacing, nk_vec2(0.0f, 0.0f));
        nk_style_push_vec2(g_frame_ctx, &g_frame_ctx->style.window.group_padding, nk_vec2(0.0f, 0.0f));
        nk_style_push_vec2(g_frame_ctx, &g_frame_ctx->style.text.padding, nk_vec2(pad, 0.0f));
        nk_style_push_font(g_frame_ctx, cell_f);
        if (nk_list_view_begin(g_frame_ctx, &view, s->name ? s->name : "list",
                               s->count <= shown ? NK_WINDOW_NO_SCROLLBAR : 0,
                               (int)row_h, s->count)) {
            if (view.begin > 0)
                focused = list_neighbor(s, view.begin - 1, area, row_h,
                                        (float)view.scroll_value, focused);
            for (r = view.begin; r < view.end; r++) {
                struct nk_command_buffer *cv = nk_window_get_canvas(g_frame_ctx);
                struct nk_color fg = g_frame_ctx->style.text.color;
                int             on = s->selected && *s->selected == r;
                struct nk_rect  row;
                unsigned        id;

                nk_layout_row(g_frame_ctx, NK_DYNAMIC, row_h, cols, ratio);
                row   = nk_widget_bounds(g_frame_ctx);
                row.w = nk_window_get_content_region(g_frame_ctx).w;
                nk_fill_rect(cv, row, 0.0f, (r & 1) ? even : odd);
                if (s->selected) reaktor_hot(g_frame_app, row, REAKTOR_CURSOR_DEFAULT, 1);
                id = reaktor_note(g_frame_app, REAKTOR_A11Y_LISTITEM, s->cell(r, 0, s->user),
                                  NULL, on ? REAKTOR_A11Y_SELECTED : 0u, row);
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
                    const char *t = s->cell(r, c, s->user);

                    nk_label_colored(g_frame_ctx, t ? t : "", NK_TEXT_LEFT, fg);
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
                                        (float)view.scroll_value, focused);
            nk_list_view_end(&view);
        }
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
