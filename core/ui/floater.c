#include "internal.h"
#include "reaktor/launch.h"

#define FLOATER_MAX 4

typedef struct floater {
    int             id;
    reaktor_floater spec;
    char           *title;
    int             closing, held, fresh;
    char            keep[NK_WINDOW_MAX_NAME];
} floater;

static floater g_floater[FLOATER_MAX];
static int   g_floater_n, g_floater_next = 1;

static void
window_name(char *out, size_t cap, int id)
{
    SDL_snprintf(out, cap, "reaktor-floater-%d", id);
}

int
reaktor_floater_open(App *app, const reaktor_floater *spec)
{
    floater *o;

    if (!spec || !spec->body || g_floater_n == FLOATER_MAX) return 0;
    o = &g_floater[g_floater_n++];
    SDL_zero(*o);
    o->id    = g_floater_next++;
    o->spec  = *spec;
    o->title = SDL_strdup(spec->title ? spec->title : "");
    o->fresh = 1;
    SDL_strlcpy(o->keep,
                app->ctx && app->ctx->active &&
                SDL_strcmp(app->ctx->active->name_string, "reaktor-toasts") != 0
                ? app->ctx->active->name_string : "page",
                sizeof(o->keep));
    app->dirty = 1;
    reaktor_wake(app);
    return o->id;
}

void
reaktor_floater_close(App *app, int id)
{
    int i;

    for (i = 0; i < g_floater_n; i++)
        if (g_floater[i].id == id) g_floater[i].closing = 1;
    app->dirty = 1;
    reaktor_wake(app);
}

int
reaktor_floater_is_open(App *app, int id)
{
    int i;

    (void)app;
    for (i = 0; i < g_floater_n; i++)
        if (g_floater[i].id == id) return !g_floater[i].closing;
    return 0;
}

int
reaktor_floaters_modal(void)
{
    int i;

    for (i = 0; i < g_floater_n; i++)
        if (g_floater[i].spec.modal && !g_floater[i].closing) return 1;
    return 0;
}

/* Nuklear's focus, left with a closing floater or the toasts, goes back. */
static void
hand_back(App *app, const floater *o)
{
    struct nk_context *ctx = app->ctx;
    struct nk_window *mine, *to;
    char name[40];
    int i;

    if (!ctx) return;
    window_name(name, sizeof(name), o->id);
    mine = nk_window_find(ctx, name);
    if (ctx->active && ctx->active != mine &&
        ctx->active != nk_window_find(ctx, "reaktor-toasts"))
        return;
    if (reaktor_floaters_modal()) {
        /* A modal still holds what is under it: the keys go to the top. */
        for (i = g_floater_n - 1; i >= 0 && g_floater[i].closing; i--) {}
        window_name(name, sizeof(name), g_floater[i].id);
        /* Not drawn yet, it takes them itself when it is. */
        if (!(to = nk_window_find(ctx, name))) return;
    } else if (!(to = nk_window_find(ctx, o->keep))) {
        to = nk_window_find(ctx, "page");
    }
    if (to && to != mine) reaktor_layer_focus(ctx, to);
}

static void
shut(App *app, int i, int tell)
{
    floater o = g_floater[i];

    SDL_memmove(&g_floater[i], &g_floater[i + 1], (size_t)(g_floater_n - i - 1) * sizeof *g_floater);
    g_floater_n--;
    hand_back(app, &o);
    SDL_free(o.title);
    if (tell && o.spec.closed) o.spec.closed(app, o.spec.user);
    /* The page is already drawn this frame; what closed shows in the next. */
    reaktor_wake(app);
}

void
reaktor_floaters_clear(App *app)
{
    while (g_floater_n) shut(app, g_floater_n - 1, 0);
}

void
reaktor_floaters_draw(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    struct nk_rect whole = nk_rect(0.0f, 0.0f, (float)win_w, (float)win_h);
    struct nk_color none = nk_rgba(0, 0, 0, 0);
    int i, top = -1;

    for (i = g_floater_n - 1; i >= 0; i--)
        if (g_floater[i].closing) shut(app, i, 1);

    for (i = 0; i < g_floater_n; i++) {
        floater *o = &g_floater[i];
        float w = o->spec.w > 0.0f ? o->spec.w : 360.0f;
        float h = o->spec.h > 0.0f ? o->spec.h : 200.0f;
        struct nk_rect r;
        char name[40];
        int shown, k, was = top;

        /* A body drawn before this one may have opened a modal. */
        for (top = -1, k = 0; k < g_floater_n; k++)
            if (g_floater[k].spec.modal && !g_floater[k].closing) top = k;
        if (i > 0 && top != was) app->trap_lo = app->trap_hi = -1;

        if (w > win_w - 16.0f) w = win_w - 16.0f;
        if (h > win_h - 16.0f) h = win_h - 16.0f;
        r = nk_rect((float)(int)((win_w - w) * 0.5f), (float)(int)((win_h - h) * 0.5f),
                    w, h);
        window_name(name, sizeof(name), o->id);

        nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(16.0f, 12.0f));
        nk_style_push_vec2(ctx, &ctx->style.window.spacing,
                           nk_vec2(8.0f, REAKTOR_MENU_GAP));
        nk_style_push_float(ctx, &ctx->style.window.border, 0.0f);
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                 nk_style_item_color(none));
        /* New this frame, it is not for the input that opened it. */
        shown = reaktor_layer_begin(app, ctx, name, r, NK_WINDOW_NO_SCROLLBAR,
                                    FOCUS_FLOATER, top >= 0 && i >= top,
                                    i < top ? HOLD_ALL
                                    : o->fresh ? HOLD_FRESH : HOLD_NONE,
                                    &o->held);
        o->fresh = 0;
        if (shown) {
            reaktor_floater_frame(app, ctx, whole, o->spec.modal);
            reaktor_note_push(app, REAKTOR_A11Y_DIALOG, o->title, NULL, 0, r);
            o->spec.body(app, ctx, (int)w, (int)h, o->spec.user);
            reaktor_note_pop(app);
        }
        reaktor_layer_end(app, ctx, shown);
        nk_style_pop_style_item(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_vec2(ctx);
        nk_style_pop_vec2(ctx);
    }
}
