#include <time.h>

#include "internal.h"
#include "declare.h"
#include "reaktor/locale.h"

#define PICK_COLS 3
#define PICK_ROWS 9
#define PICK_GAP  8.0f

enum { PICK_DAY, PICK_MONTH, PICK_YEAR, PICK_HOUR, PICK_MINUTE, PICK_HALF };

typedef struct pick_col {
    int   kind, lo, hi, at, loop, clock12;
    float w;
} pick_col;

/* One picker is open at a time; its wheels hold the value until it is set. */
static struct {
    const void *owner;
    pick_col    col[PICK_COLS];
    int         n, key, fresh;
    struct nk_rect field;
} g_pick;

static const char *
tr_or(const char *key, const char *english)
{
    const char *s = reaktor_tr(key);

    return s && SDL_strcmp(s, key) != 0 ? s : english;
}

static float
text_w(const struct nk_user_font *f, const char *s)
{
    return f->width(f->userdata, f->height, s, (int)SDL_strlen(s));
}

static const char *
pick_label(const pick_col *c, int v, char *buf, size_t cap)
{
    struct tm t;

    SDL_zero(t);
    t.tm_year = 2000 - 1900;
    t.tm_mday = 1;
    switch (c->kind) {
    case PICK_MONTH: t.tm_mon = v - 1; return reaktor_format_pattern(buf, cap, &t, "LLLL");
    case PICK_HOUR:  SDL_snprintf(buf, cap, "%d", c->clock12 ? (v % 12 ? v % 12 : 12) : v);
                     return buf;
    case PICK_MINUTE: SDL_snprintf(buf, cap, "%02d", v); return buf;
    case PICK_HALF:  t.tm_hour = v * 12; return reaktor_format_pattern(buf, cap, &t, "a");
    default:         SDL_snprintf(buf, cap, "%d", v); return buf;
    }
}

static int
pick_wrap(const pick_col *c, int v)
{
    int n = c->hi - c->lo + 1;

    if (c->loop) return c->lo + ((v - c->lo) % n + n) % n;
    return v < c->lo ? c->lo : v > c->hi ? c->hi : v;
}

static void
pick_days(void)
{
    int k, y = 2000, m = 1;

    for (k = 0; k < g_pick.n; k++) {
        if (g_pick.col[k].kind == PICK_YEAR)  y = g_pick.col[k].at;
        if (g_pick.col[k].kind == PICK_MONTH) m = g_pick.col[k].at;
    }
    for (k = 0; k < g_pick.n; k++)
        if (g_pick.col[k].kind == PICK_DAY) {
            g_pick.col[k].hi = SDL_GetDaysInMonth(y, m);
            g_pick.col[k].at = pick_wrap(&g_pick.col[k], g_pick.col[k].at);
        }
}

/* The segments, in the order the language writes a short date. */
static int
date_order(int *kinds)
{
    struct tm   t;
    char        probe[64];
    const char *p;
    int         n = 0;

    SDL_zero(t);
    t.tm_year = 2033 - 1900;
    t.tm_mon  = 10;
    t.tm_mday = 22;
    reaktor_format_date(probe, sizeof probe, &t, REAKTOR_DATE_SHORT);
    for (p = probe; *p && n < 3; p++) {
        int kind = SDL_strncmp(p, "22", 2) == 0 ? PICK_DAY
                 : SDL_strncmp(p, "11", 2) == 0 ? PICK_MONTH
                 : SDL_strncmp(p, "33", 2) == 0 ? PICK_YEAR : -1;

        if (kind >= 0) { kinds[n++] = kind; p++; while (p[1] >= '0' && p[1] <= '9') p++; }
    }
    if (n != 3) { kinds[0] = PICK_MONTH; kinds[1] = PICK_DAY; kinds[2] = PICK_YEAR; n = 3; }
    return n;
}

static void
pick_draw_wheels(App *app, struct nk_context *ctx, struct nk_rect body, float row)
{
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    const struct nk_user_font *f = ctx->style.font;
    struct nk_color fg = app->text, dim = reaktor_token("--text-muted", nk_rgb(150, 150, 150));
    struct nk_rect band = nk_rect(body.x, body.y + row * (PICK_ROWS / 2), body.w, row);
    float x = body.x;
    char buf[64];
    int k, i;

    reaktor_fill_round(app, cv, band, 0.0f, app->accent);
    for (k = 0; k < g_pick.n; k++) {
        pick_col *c = &g_pick.col[k];
        struct nk_rect col = nk_rect(x, body.y, c->w, row * PICK_ROWS);
        unsigned id;

        reaktor_hot(app, col, 1, 1);
        if (!g_pick.fresh && nk_input_is_mouse_hovering_rect(&ctx->input, col) &&
            ctx->input.mouse.scroll_delta.y != 0.0f) {
            c->at = pick_wrap(c, c->at - (ctx->input.mouse.scroll_delta.y > 0.0f ? 1 : -1));
            g_pick.key = k;
        }
        for (i = 0; i < PICK_ROWS; i++) {
            int v = c->at + i - PICK_ROWS / 2, shown = c->loop || (v >= c->lo && v <= c->hi);
            struct nk_rect r = nk_rect(x, body.y + row * (float)i, c->w, row);
            const char *t;
            float w;

            v = pick_wrap(c, v);
            if (!shown) continue;
            if (!g_pick.fresh && i != PICK_ROWS / 2 &&
                nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, r)) {
                c->at = v;
                g_pick.key = k;
            }
            t = pick_label(c, v, buf, sizeof buf);
            w = f->width(f->userdata, f->height, t, (int)SDL_strlen(t));
            nk_draw_text(cv, nk_rect(r.x + (r.w - w) * 0.5f,
                                     (float)(int)(r.y + (r.h - f->height) * 0.5f), w, f->height),
                         t, (int)SDL_strlen(t), f, nk_rgba(0, 0, 0, 0),
                         i == PICK_ROWS / 2 ? reaktor_on(app->accent) : i == 0 || i == PICK_ROWS - 1 ? dim : fg);
        }
        id = reaktor_note(app, REAKTOR_A11Y_SPINBUTTON, NULL,
                          pick_label(c, c->at, buf, sizeof buf), 0u,
                          nk_rect(x, band.y, c->w, row));
        if (k == g_pick.key) {
            app->focus_id = id;
            reaktor_a11y_set_focus(&app->a11y, id);
        }
        if (k) nk_fill_rect(cv, nk_rect(x, body.y, 1.0f, row * PICK_ROWS), 0.0f,
                            reaktor_token("--background-hover", dim));
        x += c->w;
    }
    pick_days();
}

static int
pick_key(App *app)
{
    SDL_Keycode key = app->popup_key;
    pick_col *c = &g_pick.col[g_pick.key];

    app->popup_key = 0;
    switch (key) {
    case SDLK_UP:    c->at = pick_wrap(c, c->at - 1); break;
    case SDLK_DOWN:  c->at = pick_wrap(c, c->at + 1); break;
    case SDLK_LEFT:  g_pick.key = (g_pick.key + g_pick.n - 1) % g_pick.n; break;
    case SDLK_RIGHT: g_pick.key = (g_pick.key + 1) % g_pick.n; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return 1;
    case SDLK_ESCAPE: return -1;
    default: return 0;
    }
    pick_days();
    return 0;
}

/* 1 when set, -1 when dismissed, 0 while open. */
static int
pick_flyout(App *app, struct nk_context *ctx, float row)
{
    struct nk_rect r = g_pick.field, body;
    float w = 0.0f, h = row * PICK_ROWS + row + 16.0f;
    int k, done;

    const char *no = tr_or("button.cancel", "Cancel"), *ok = tr_or("button.ok", "OK");
    reaktor_style btn;
    float need;

    reaktor_style_get("button", &btn);
    need = 2.0f * (NK_MAX(text_w(ctx->style.font, no), text_w(ctx->style.font, ok)) +
                   2.0f * (btn.pad_x + btn.border)) + 3.0f * PICK_GAP;

    for (k = 0; k < g_pick.n; k++) w += g_pick.col[k].w;
    if (w < need) {
        for (k = 0; k < g_pick.n; k++) g_pick.col[k].w = (float)(int)(g_pick.col[k].w * need / w);
        for (w = 0.0f, k = 0; k < g_pick.n; k++) w += g_pick.col[k].w;
    }
    r.x += (r.w - w) * 0.5f;
    r.w  = w;
    r.y  = g_pick.field.y + g_pick.field.h * 0.5f - row * (PICK_ROWS / 2 + 0.5f);
    r.h  = h;
    if (r.y + r.h > (float)app->laid_h) r.y = (float)app->laid_h - r.h;
    if (r.y < 0.0f) r.y = 0.0f;
    if (r.x + r.w > (float)app->laid_w) r.x = (float)app->laid_w - r.w;
    if (r.x < 0.0f) r.x = 0.0f;

    done = pick_key(app);
    if (!done && !g_pick.fresh && nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&ctx->input, r) &&
        !nk_input_is_mouse_hovering_rect(&ctx->input, g_pick.field))
        done = -1;
    if (reaktor_flyout_begin(ctx, "reaktor-picker", r)) {
        struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
        reaktor_style dlg;
        reaktor_surface s;
        struct nk_rect foot;

        reaktor_style_get("dialog", &dlg);
        s = reaktor_rule_surface(&dlg, reaktor_popup_rounding());
        reaktor_paint_surface(app, cv, r, &s);
        body = nk_rect(r.x, r.y + 4.0f, r.w, row * PICK_ROWS);
        nk_push_scissor(cv, body);
        pick_draw_wheels(app, ctx, body, row);
        nk_push_scissor(cv, r);
        foot = nk_rect(r.x + PICK_GAP, r.y + h - row - 6.0f, (r.w - 3.0f * PICK_GAP) * 0.5f, row);
        nk_layout_space_begin(ctx, NK_STATIC, h, 2);
        nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, foot));
        if (reaktor_button_label(app, ctx, no) && !g_pick.fresh) done = -1;
        foot.x += foot.w + PICK_GAP;
        nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, foot));
        if (reaktor_button_accent(app, ctx, ok) && !g_pick.fresh) done = 1;
        nk_layout_space_end(ctx);
        reaktor_flyout_end(ctx);
    }
    if (!done) app->popup_open = 1;
    g_pick.fresh   = 0;
    return done;
}

/* The field is an input like any other: the value as the language writes it, or a hint. */
static int
pick_field(const void *owner, const char *name, const reaktor_box *spec_box,
           pick_col *cols, int n, const char *hint, const char *value)
{
    App *app = g_frame_app;
    struct nk_context *ctx = g_frame_ctx;
    const struct nk_user_font *f = ctx->style.font;
    reaktor_style in;
    reaktor_style_frame sf;
    reaktor_box box = *spec_box;
    struct nk_rect r;
    float row;
    char buf[64];
    unsigned id;
    int k, v, done = 0;

    /* The padding a text field resolves, so the hint sits where a field's does. */
    sf = push_edit_style(ctx, &in, 0, NULL);
    reaktor_style_frame_pop(ctx, sf);
    decl_box_from_style(&box, NULL, "input.type-text", "input", 0.0f, 7.0f);
    if (box.w <= 0.0f && !(box.flags & REAKTOR_LAY_FILL_X))
        box.w = (float)(int)(text_w(f, value ? value : hint) + 2.0f * in.pad_x + 0.5f);
    if (!decl_place(REAKTOR_A11Y_BUTTON, name, value,
                    g_pick.owner == owner ? REAKTOR_A11Y_EXPANDED : 0u, NULL, &box, &id))
        return 0;
    nk_widget(&r, ctx);
    reaktor_edit_edge(app, ctx, r, &in);
    reaktor_hot(app, r, REAKTOR_CURSOR_POINTER, 1);
    if (value)
        nk_draw_text(nk_window_get_canvas(ctx),
                     nk_rect(r.x + in.pad_x, (float)(int)(r.y + (r.h - f->height) * 0.5f),
                             r.w - 2.0f * in.pad_x, f->height),
                     value, (int)SDL_strlen(value), f, nk_rgba(0, 0, 0, 0),
                     ctx->style.edit.text_normal);
    else
        draw_hint(ctx, r, hint, &in);
    row = (float)(int)(f->height + 14.0f);
    if (g_pick.owner != owner &&
        (nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, r) ||
         reaktor_focus_activated(app, id))) {
        g_pick.owner   = owner;
        g_pick.n       = n;
        g_pick.key     = 0;
        g_pick.fresh   = 1;
        SDL_memcpy(g_pick.col, cols, sizeof(pick_col) * (size_t)n);
        for (k = 0; k < n; k++) {
            float w = 0.0f;

            for (v = cols[k].lo; v <= cols[k].hi; v++)
                w = NK_MAX(w, text_w(f, pick_label(&cols[k], v, buf, sizeof buf)));
            g_pick.col[k].w = (float)(int)(w + 32.0f);
        }
        pick_days();
    }
    if (g_pick.owner == owner) {
        g_pick.field = r;
        done = pick_flyout(app, ctx, row);
        if (done) {
            if (done > 0) SDL_memcpy(cols, g_pick.col, sizeof(pick_col) * (size_t)n);
            g_pick.owner = NULL;
            reaktor_focus_take(app, id, r);
            app->dirty = 1;
            reaktor_wake(app);
        }
    }
    return done > 0;
}

int
reaktor_date_pick(const reaktor_date_spec *s)
{
    pick_col    cols[3];
    const char *hints[3];
    struct tm   tm;
    char        hint[96], value[64];
    int         kinds[3], k, n, set, today = 1;

    if (!g_frame_app || !s || !s->year || !s->month || !s->day) return 0;
    set = *s->day > 0;
    if (!set) {
        SDL_Time now;
        SDL_DateTime dt;

        SDL_zero(dt);
        if (SDL_GetCurrentTime(&now)) SDL_TimeToDateTime(now, &dt, true);
        *s->year = dt.year ? dt.year : 2000;
        *s->month = dt.month ? dt.month : 1;
        today = dt.day ? dt.day : 1;
    }
    n = date_order(kinds);
    for (k = 0; k < n; k++) {
        pick_col *c = &cols[k];

        SDL_zero(*c);
        c->kind = kinds[k];
        if (c->kind == PICK_DAY) {
            c->lo = 1; c->hi = SDL_GetDaysInMonth(*s->year, *s->month); c->loop = 1;
            c->at = set ? *s->day : today;
            hints[k] = tr_or("date.day", "day");
        } else if (c->kind == PICK_MONTH) {
            c->lo = 1; c->hi = 12; c->loop = 1; c->at = *s->month;
            hints[k] = tr_or("date.month", "month");
        } else {
            c->lo = s->min_year ? s->min_year : 1900;
            c->hi = s->max_year ? s->max_year : 2100;
            c->at = *s->year;
            hints[k] = tr_or("date.year", "year");
        }
    }
    SDL_snprintf(hint, sizeof hint, "%s / %s / %s", hints[0], hints[1], hints[2]);
    SDL_zero(tm);
    tm.tm_year = *s->year - 1900;
    tm.tm_mon  = *s->month - 1;
    tm.tm_mday = set ? *s->day : 1;
    reaktor_format_date(value, sizeof value, &tm, REAKTOR_DATE_SHORT);
    if (!pick_field(s->day, s->name ? s->name : "Date", &s->box, cols, n, hint,
                    set ? value : NULL))
        return 0;
    for (k = 0; k < n; k++) {
        if (cols[k].kind == PICK_DAY)   *s->day = cols[k].at;
        if (cols[k].kind == PICK_MONTH) *s->month = cols[k].at;
        if (cols[k].kind == PICK_YEAR)  *s->year = cols[k].at;
    }
    return 1;
}

int
reaktor_time_pick(const reaktor_time_spec *s)
{
    static char half[16];
    pick_col    cols[3];
    const char *hints[3];
    struct tm   tm;
    char        hint[96], value[32];
    int         n = 2, set;

    if (!g_frame_app || !s || !s->hour || !s->minute) return 0;
    set = *s->hour >= 0;
    SDL_zero(cols);
    cols[0].kind = PICK_HOUR;
    cols[0].lo = s->clock12 ? 1 : 0;
    cols[0].hi = s->clock12 ? 12 : 23;
    cols[0].loop = 1;
    cols[0].clock12 = s->clock12;
    cols[0].at = !set ? cols[0].lo : s->clock12 ? (*s->hour % 12 ? *s->hour % 12 : 12) : *s->hour;
    cols[1].kind = PICK_MINUTE;
    cols[1].lo = 0;
    cols[1].hi = 59;
    cols[1].loop = 1;
    cols[1].at = set ? *s->minute : 0;
    hints[0] = tr_or("time.hour", "hour");
    hints[1] = tr_or("time.minute", "minute");
    if (s->clock12) {
        cols[2].kind = PICK_HALF;
        cols[2].lo = 0;
        cols[2].hi = 1;
        cols[2].at = set && *s->hour >= 12;
        hints[2] = pick_label(&cols[2], 0, half, sizeof half);
        n = 3;
    }
    SDL_snprintf(hint, sizeof hint, n == 3 ? "%s:%s %s" : "%s:%s", hints[0], hints[1],
                 n == 3 ? hints[2] : "");
    SDL_zero(tm);
    tm.tm_hour = set ? *s->hour : 0;
    tm.tm_min  = set ? *s->minute : 0;
    reaktor_format_pattern(value, sizeof value, &tm, s->clock12 ? "h:mm a" : "HH:mm");
    if (!pick_field(s->hour, s->name ? s->name : "Time", &s->box, cols, n, hint,
                    set ? value : NULL))
        return 0;
    *s->minute = cols[1].at;
    *s->hour   = s->clock12 ? cols[0].at % 12 + (cols[2].at ? 12 : 0) : cols[0].at;
    return 1;
}
