#include "internal.h"
#include "reaktor/launch.h"

#define TOAST_MAX   8
#define TOAST_SHOWN 4
#define TOAST_GAP   8.0f
#define TOAST_EDGE  16.0f
#define TOAST_ROW   34.0f
#define TOAST_MIN_W 240.0f
#define TOAST_MAX_W 560.0f

typedef struct toast {
    int                id;
    reaktor_toast_spec spec;
    char              *text, *icon, *action;
    Uint64             until;
    SDL_TimerID        timer;
} toast;

static toast g_toast[TOAST_MAX];
static int   g_toast_n, g_toast_next = 1, g_toast_fresh;

static Uint32 SDLCALL
expire(void *userdata, SDL_TimerID id, Uint32 interval)
{
    (void)id; (void)interval;
    reaktor_wake((App *)userdata);
    return 0;
}

static void
drop(int i)
{
    toast *t = &g_toast[i];

    if (t->timer) SDL_RemoveTimer(t->timer);
    SDL_free(t->text);
    SDL_free(t->icon);
    SDL_free(t->action);
    SDL_memmove(t, t + 1, (size_t)(g_toast_n - i - 1) * sizeof *t);
    g_toast_n--;
}

static char *
copy(const char *s)
{
    return s ? SDL_strdup(s) : NULL;
}

void
reaktor_toast(App *app, const reaktor_toast_spec *spec)
{
    toast *t;
    int ms;

    if (!spec) return;
    if (g_toast_n == TOAST_MAX) drop(0);
    t = &g_toast[g_toast_n++];
    SDL_zero(*t);
    t->id     = g_toast_next++;
    t->spec   = *spec;
    t->text   = copy(spec->text);
    t->icon   = copy(spec->icon);
    t->action = copy(spec->action);
    ms = spec->timeout_ms;
    /* Nothing would ever close it. */
    if (ms <= 0 && spec->no_close && !spec->action) ms = 5000;
    if (ms > 0) {
        t->until = SDL_GetTicks() + (Uint64)ms;
        t->timer = SDL_AddTimer((Uint32)ms, expire, app);
    }
    g_toast_fresh = 1;
    app->dirty = 1;
    reaktor_wake(app);
}

void
reaktor_toast_clear(void)
{
    while (g_toast_n) drop(g_toast_n - 1);
}

static float
text_w(const struct nk_user_font *f, const char *s)
{
    return s ? f->width(f->userdata, f->height, s, (int)SDL_strlen(s)) : 0.0f;
}

static float
action_w(const struct nk_context *ctx, const struct nk_user_font *f, const char *s)
{
    const struct nk_style_button *b = &ctx->style.button;

    return (float)(int)(text_w(f, s) + 2.0f * (b->padding.x + b->border + b->rounding) + 1.0f);
}

void
reaktor_toasts_draw(App *app, struct nk_context *ctx, int win_w, int win_h,
                    int blocked)
{
    const struct nk_user_font *font;
    struct nk_color fg, none = nk_rgba(0, 0, 0, 0);
    reaktor_surface surf;
    reaktor_style s;
    struct nk_rect area;
    float w[TOAST_SHOWN], h, pad_l, pad_r, pad_in, pad_y, widest = 0.0f;
    Uint64 now = SDL_GetTicks();
    static int held;
    struct nk_window *keep;
    int i, first, n, act = -1, shut = -1, shown, hold;

    for (i = g_toast_n - 1; i >= 0; i--)
        if (g_toast[i].until && now >= g_toast[i].until) drop(i);
    if (!g_toast_n) return;

    reaktor_style_get(".toast", &s);
    surf = reaktor_rule_surface(&s, ctx->style.button.rounding);
    fg = s.matched && s.fg[3] ? col_of(s.fg)
       : reaktor_token("--text-main", nk_rgb(247, 247, 247));
    pad_l  = s.matched ? s.pad[REAKTOR_SIDE_LEFT] : 16.0f;
    pad_r  = s.matched ? s.pad[REAKTOR_SIDE_RIGHT] : 16.0f;
    pad_in = pad_l < pad_r ? pad_l : pad_r;
    pad_y  = s.matched ? s.pad_y : 6.0f;
    font = s.matched && s.font_px > 0 ? reaktor_font(app, s.font_px, s.bold) : ctx->style.font;
    h = TOAST_ROW + 2.0f * pad_y;

    first = g_toast_n > TOAST_SHOWN ? g_toast_n - TOAST_SHOWN : 0;
    n = g_toast_n - first;
    for (i = 0; i < n; i++) {
        const toast *t = &g_toast[first + i];
        float tw = pad_l + pad_r + text_w(font, t->text);

        if (t->icon) tw += 20.0f + 8.0f;
        if (t->spec.content) tw += t->spec.content_w + 8.0f;
        if (t->action) tw += action_w(ctx, font, t->action) + 8.0f;
        if (!t->spec.no_close) tw += TOAST_ROW + 8.0f;
        if (tw < TOAST_MIN_W) tw = TOAST_MIN_W;
        if (tw > TOAST_MAX_W) tw = TOAST_MAX_W;
        if (tw > win_w - 2.0f * TOAST_EDGE) tw = win_w - 2.0f * TOAST_EDGE;
        w[i] = (float)(int)tw;
        if (w[i] > widest) widest = w[i];
    }
    area.w = widest;
    area.h = n * h + (n - 1) * TOAST_GAP;
    area.x = (float)(int)((win_w - widest) * 0.5f);
    area.y = (float)(int)(win_h - TOAST_EDGE - area.h);

    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));
    nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(8.0f, 0));
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(pad_in, pad_y));
    nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
    nk_style_push_float(ctx, &ctx->style.window.group_border, 0.0f);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(none));
    nk_style_push_font(ctx, font);

    /* Creating the toasts window takes Nuklear's focus. */
    keep = nk_window_find(ctx, "reaktor-toasts") ? NULL : ctx->active;
    hold = blocked ? HOLD_ALL : g_toast_fresh ? HOLD_FRESH : HOLD_NONE;
    g_toast_fresh = 0;
    if ((shown = reaktor_layer_begin(app, ctx, "reaktor-toasts", area,
                                     NK_WINDOW_NO_SCROLLBAR, FOCUS_TOAST, 0,
                                     hold, &held))) {
        struct nk_command_buffer *cv = nk_window_get_canvas(ctx);

        nk_layout_space_begin(ctx, NK_STATIC, area.h, n);
        for (i = 0; i < n; i++) {
            toast *t = &g_toast[first + i];
            struct nk_rect r = nk_rect((float)(int)((widest - w[i]) * 0.5f),
                                       i * (h + TOAST_GAP), w[i], h);
            struct nk_rect abs = nk_rect(area.x + r.x, area.y + r.y, r.w, r.h);
            char name[32];

            reaktor_paint_surface(app, cv, abs, &surf);
            nk_layout_space_push(ctx, nk_rect(r.x + pad_l - pad_in, r.y,
                                              r.w - pad_l - pad_r + 2.0f * pad_in, r.h));
            SDL_snprintf(name, sizeof(name), "toast %d", t->id);
            reaktor_note_push(app, REAKTOR_A11Y_GROUP,
                              t->text ? t->text : "Notification", NULL, 0, abs);
            if (nk_group_begin_titled(ctx, name, NULL, NK_WINDOW_NO_SCROLLBAR)) {
                nk_layout_row_template_begin(ctx, TOAST_ROW);
                if (t->icon) nk_layout_row_template_push_static(ctx, 20.0f);
                nk_layout_row_template_push_dynamic(ctx);
                if (t->spec.content)
                    nk_layout_row_template_push_static(ctx, t->spec.content_w);
                if (t->action)
                    nk_layout_row_template_push_static(ctx, action_w(ctx, font, t->action));
                if (!t->spec.no_close)
                    nk_layout_row_template_push_static(ctx, TOAST_ROW);
                nk_layout_row_template_end(ctx);

                if (t->icon)
                    image_centred(ctx, reaktor_ionicon_col(app, t->icon, 20, fg),
                                  20);
                nk_label_colored(ctx, t->text ? t->text : "", NK_TEXT_LEFT, fg);
                if (t->spec.content) {
                    if (nk_group_begin_titled(ctx, "content", NULL,
                                              NK_WINDOW_NO_SCROLLBAR)) {
                        t->spec.content(app, ctx, t->spec.user);
                        nk_group_end(ctx);
                    }
                }
                if (t->action &&
                    reaktor_button_label_as(app, ctx, ".toast-button", t->action))
                    act = first + i;
                if (!t->spec.no_close &&
                    reaktor_css_button_image(
                        app, ctx, ".toast-close",
                        reaktor_ionicon_col(app, "close-outline", 18, fg), 18.0f,
                        "Close"))
                    shut = first + i;
                nk_group_end(ctx);
            }
            reaktor_note_pop(app);
        }
        nk_layout_space_end(ctx);
    }
    reaktor_layer_end(app, ctx, shown);
    if (keep) reaktor_layer_focus(ctx, keep);

    nk_style_pop_font(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_vec2(ctx);
    nk_style_pop_vec2(ctx);
    nk_style_pop_vec2(ctx);

    if (act >= 0) {
        toast t = g_toast[act];

        drop(act);
        if (shut > act) shut--;
        else if (shut == act) shut = -1;
        if (t.spec.on_action) t.spec.on_action(app, t.spec.user);
        reaktor_wake(app);
    }
    if (shut >= 0 && shut < g_toast_n) {
        drop(shut);
        reaktor_wake(app);
    }
}
