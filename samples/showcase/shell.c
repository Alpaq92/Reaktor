#include "internal.h"
#include "showcase.h"

static const char *const g_theme_names[3] = { "system", "light", "dark" };
#include "declare.h"

void
sample_window(reaktor_window_spec *out)
{
    out->w = WINDOW_WIDTH;
    out->h = WINDOW_HEIGHT;
    out->title = "Reaktor";
    out->borderless = 1;
}

SDL_HitTestResult SDLCALL
window_hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
    App *app = (App *)data;
    int w = 0, h = 0, i;
    int left, right, top, bottom;

    SDL_GetWindowSize(win, &w, &h);

    if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)) {
        left   = pt->x < RESIZE_EDGE;
        right  = pt->x >= w - RESIZE_EDGE;
        top    = pt->y < RESIZE_EDGE;
        bottom = pt->y >= h - RESIZE_EDGE;

        if (top && left)     return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right)    return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top)             return SDL_HITTEST_RESIZE_TOP;
        if (bottom)          return SDL_HITTEST_RESIZE_BOTTOM;
        if (left)            return SDL_HITTEST_RESIZE_LEFT;
        if (right)           return SDL_HITTEST_RESIZE_RIGHT;
    }

    if (pt->y < TITLEBAR_H) {
        for (i = 0; i < app->ctl_n; i++) {
            struct nk_rect r = app->ctl[i];
            const int m = 4;
            if (pt->x >= r.x - m && pt->x <= r.x + r.w + m &&
                pt->y >= r.y - m && pt->y <= r.y + r.h + m)
                return SDL_HITTEST_NORMAL;
        }
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

static int g_tab = -1;
static int g_scroll0 = -1;

int
sample_tab(void)
{
    if (g_tab < 0) g_tab = TAB_LOGIN;
    return g_tab;
}

/* --tab and --scroll open the window on one page, scrolled to one place, so
 * two runs can be compared without clicking either of them into position.
 * The runtime has already taken its own flags out of argv. */
void
sample_args(App *app, int argc, char **argv)
{
    int i;

    for (i = 1; i + 1 < argc; i++) {
        int n = SDL_atoi(argv[i + 1]);

        if (SDL_strcmp(argv[i], "--tab") == 0) {
            if (n >= 0 && n < TAB_COUNT) g_tab = n;
            i++;
        } else if (SDL_strcmp(argv[i], "--scroll") == 0) {
            if (n >= 0) g_scroll0 = n;
            i++;
        }
    }
    (void)app;
}

void
set_tab(App *app, int tab)
{
    if (tab == sample_tab()) return;
    g_tab = tab;
    app->dirty = 1;
}

static int
titlebar_button(App *app, struct nk_context *ctx, const char *glyph,
                const char *name, int px)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    style_frame f = { 0, 0, 0, 0, 0 };
    unsigned char c[4];
    struct nk_color clear = nk_rgba(0, 0, 0, 0);
    struct nk_color wash = reaktor_style_token("--background-hover", c)
                         ? col_of(c) : nk_rgba(255, 255, 255, 26);
    char src[176];
    float pad_x, pad_y;
    int clicked;

    hot_push(app, b, 1, 1);
    reaktor_note(app, REAKTOR_A11Y_BUTTON, name, NULL, 0, b);
    if (app->ctl_n < (int)(sizeof(app->ctl) / sizeof(app->ctl[0])))
        app->ctl[app->ctl_n++] = b;

    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(clear));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(wash));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(wash));
    f.items = 3;

    nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
    nk_style_push_float(ctx, &ctx->style.button.rounding,
                        (b.h > 0.0f ? b.h : (float)CTL_SIZE) * 0.5f);
    nk_style_push_float(ctx, &ctx->style.button.color_factor_background, 1.0f);
    f.floats = 3;

    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));

    pad_x = pad_y = (b.h - (float)px) * 0.5f;
    if (pad_x < 0.0f) pad_x = pad_y = 0.0f;
    nk_style_push_vec2(ctx, &ctx->style.button.image_padding,
                       nk_vec2(pad_x, pad_y));
    f.vec2s = 2;

    if (app->dark) {
        SDL_snprintf(src, sizeof(src),
                     "external/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                     glyph, app->icon_hex, (double)GLYPH_STROKE);
    } else {
        SDL_snprintf(src, sizeof(src),
                     "external/ionicons/src/svg/%s.svg"
                     "?stroke=#%02x%02x%02x&sw=%.2f",
                     glyph, app->text.r, app->text.g, app->text.b,
                     (double)GLYPH_STROKE);
    }

    clicked = nk_button_image_label(ctx, icon(app, src, px), "",
                                    NK_TEXT_CENTERED);
    pop_style(ctx, f);
    return clicked;
}

static void
titlebar(App *app, struct nk_context *ctx, int win_w)
{
    unsigned char c[4];
    int maximised = (SDL_GetWindowFlags(app->win) & SDL_WINDOW_MAXIMIZED) != 0;
    struct nk_rect bar;

    app->ctl_n = 0;
    bar = nk_widget_bounds(ctx);
    if (!nk_group_begin(ctx, "titlebar", NK_WINDOW_NO_SCROLLBAR)) return;
    reaktor_note_push(app, REAKTOR_A11Y_GROUP, "Title bar", NULL, 0, bar);

    nk_layout_row_begin(ctx, NK_STATIC, (float)CTL_SIZE, 6);

    nk_layout_row_push(ctx, (float)TITLE_PAD);
    nk_spacing(ctx, 1);

    nk_layout_row_push(ctx, (float)MARK_SIZE);
    image_centred(ctx, icon(app, REAKTOR_MARK, MARK_SIZE), MARK_SIZE);

    nk_layout_row_push(ctx, (float)(win_w - TITLE_PAD - MARK_SIZE -
                                    3 * CTL_SIZE - 2 * 4 - 5 * 4));
    {
        nk_style_push_font(ctx, pick_font(app, TITLE_PX, 1));
        if (app->dark && reaktor_style_token("--text-muted", c))
            nk_style_push_color(ctx, &ctx->style.text.color, col_of(c));
        else
            nk_style_push_color(ctx, &ctx->style.text.color, app->text);
        nk_style_push_vec2(ctx, &ctx->style.text.padding, nk_vec2(3.0f, 0.0f));
        reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, "Reaktor", 0);
        nk_label(ctx, "Reaktor", NK_TEXT_LEFT);
        nk_style_pop_vec2(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_font(ctx);
    }

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "remove-outline", "Minimize", GLYPH_MINIMISE))
        SDL_MinimizeWindow(app->win);

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx,
                        maximised ? "copy-outline" : "square-outline",
                        maximised ? "Restore" : "Maximize",
                        GLYPH_MAXIMISE)) {
        if (maximised) SDL_RestoreWindow(app->win);
        else           SDL_MaximizeWindow(app->win);
    }

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "close-outline", "Close", GLYPH_CLOSE))
        app->want_quit = 1;

    nk_layout_row_end(ctx);
    reaktor_note_pop(app);
    nk_group_end(ctx);
}

#define ROW_BRAND   28
#define ROW_FIELD   38
#define ROW_BUTTON  40
#define ROW_SMALL   18
#define ROW_GAP     15
#define CONTACT_ROWS 3

#define CARD_PAD_X  22
#define CARD_PAD_Y  18

static float
card_height(int with_diag)
{
    int rows = 7;
    float h = (float)(ROW_BRAND + ROW_FIELD + ROW_BUTTON +
                      ROW_SMALL + ROW_BUTTON + ROW_BUTTON + ROW_SMALL);
    if (with_diag) {
        rows += CONTACT_ROWS;
        h += CONTACT_ROWS * ROW_SMALL;
    }
    return h + (rows - 1) * ROW_GAP + 2.0f * CARD_PAD_Y + 6.0f;
}

static void
login_card(App *app, struct nk_context *ctx, float win_w, float body_y,
           float body_h)
{
    struct nk_rect at;
    float card_h = card_height(app->show_contact);
    float side = (win_w - (float)CARD_W) * 0.5f;
    float top  = body_y + (body_h - card_height(0)) * 0.5f;

    if (side < 8.0f) side = 8.0f;

    if (top + card_h > body_y + body_h - 8.0f)
        top = body_y + body_h - card_h - 8.0f;
    if (top  < body_y + 8.0f) top = body_y + 8.0f;

    nk_layout_space_push(ctx, nk_rect(side, top, (float)CARD_W, card_h));

    at = nk_widget_bounds(ctx);

    reaktor_fill_round(app, nk_window_get_canvas(ctx), at,
                       ctx->style.button.rounding, app->card_bg);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding, nk_vec2(0, 0));
    if (nk_group_begin(ctx, "card", NK_WINDOW_NO_SCROLLBAR)) {
        REAKTOR_COLUMN(.name = "Proceed with login",
                       .w = CARD_W, .h = card_h, .gap = ROW_GAP) {
            REAKTOR_ROW(.h = ROW_BRAND, .gap = 10, .flags = REAKTOR_LAY_FILL_X,
                        .ml = CARD_PAD_X, .mr = CARD_PAD_X, .mt = CARD_PAD_Y) {
                reaktor_icon(&(reaktor_icon_spec){
                    .name = "person-circle-outline", .accent = 1,
                    .box  = { .w = ROW_BRAND, .h = ROW_BRAND } });
                reaktor_label(&(reaktor_label_spec){
                    .text  = "Proceed with login",
                    .style = "h4",
                    .box   = { .flags = REAKTOR_LAY_FILL_X |
                                        REAKTOR_LAY_FILL_Y } });
            }

            {
                int used = (int)app->edit.string.len;
                reaktor_field(&(reaktor_field_spec){
                    .buf = app->edit_buf, .len = &used,
                    .cap = (int)sizeof(app->edit_buf),
                    .hint = "you@example.com",
                    .box = { .h = ROW_FIELD, .flags = REAKTOR_LAY_FILL_X,
                             .ml = CARD_PAD_X, .mr = CARD_PAD_X } });
            }

            reaktor_button(&(reaktor_button_spec){
                .label = "Continue with email", .accent = 1,
                .box = { .h = ROW_BUTTON, .flags = REAKTOR_LAY_FILL_X,
                         .ml = CARD_PAD_X, .mr = CARD_PAD_X } });

            reaktor_label(&(reaktor_label_spec){
                .text = "or", .style = ".card-note", .align = REAKTOR_CENTRE,
                .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                         .ml = CARD_PAD_X, .mr = CARD_PAD_X } });

            reaktor_button(&(reaktor_button_spec){
                .label = "Use a passkey", .icon = "key-outline",
                .box = { .h = ROW_BUTTON, .flags = REAKTOR_LAY_FILL_X,
                         .ml = CARD_PAD_X, .mr = CARD_PAD_X } });
            reaktor_button(&(reaktor_button_spec){
                .label = "Use biometrics", .icon = "finger-print-outline",
                .box = { .h = ROW_BUTTON, .flags = REAKTOR_LAY_FILL_X,
                         .ml = CARD_PAD_X, .mr = CARD_PAD_X } });

            if (reaktor_link(&(reaktor_link_spec){
                    .text = app->show_contact ? "hide contact" : "contact",
                    .style = ".card-link",
                    .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                             .ml = CARD_PAD_X, .mr = CARD_PAD_X } }))
                app->show_contact = !app->show_contact;

            if (app->show_contact) {
                static const char *const lines[CONTACT_ROWS] = {
                    "support@reaktor.example",
                    "+44 20 7946 0958",
                    "Mon-Fri, 09:00-17:00 UTC"
                };
                int i;

                for (i = 0; i < CONTACT_ROWS; i++)
                    reaktor_label(&(reaktor_label_spec){
                        .text = lines[i], .style = ".card-link", .align = REAKTOR_CENTRE,
                        .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                                 .ml = CARD_PAD_X, .mr = CARD_PAD_X } });
            }
        }
        nk_group_end(ctx);
    }
    nk_style_pop_vec2(ctx);
    nk_style_pop_style_item(ctx);
}

static void
tab_strip(App *app, struct nk_context *ctx, int win_w)
{
    const struct nk_user_font *font = pick_font(app, 16, 0);
    unsigned char c[4];
    struct nk_color accent = reaktor_style_token("--links", c)
                           ? col_of(c) : app->text;
    struct nk_color muted  = reaktor_style_token("--text-muted", c)
                           ? col_of(c) : app->text;
    struct nk_color wash   = reaktor_style_token("--background-hover", c)
                           ? col_of(c) : nk_rgba(128, 128, 128, 40);
    struct nk_color clear  = nk_rgba(0, 0, 0, 0);
    struct nk_command_buffer *canvas;
    struct nk_rect strip;
    struct nk_rect active_r = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    const char *swl = app->borderless ? "native titlebar" : "custom titlebar";
    float tabw[TAB_COUNT], themew[3], sw_w = 0.0f, rest = 0.0f;
    const float SW_PAD_X = 8.0f;
    const float TAB_SEP = 10.0f;
    const float TAB_EDGE = 4.0f;
    float sep = TAB_SEP, edge = TAB_EDGE;
    int i;

    (void)win_w;
    strip = nk_widget_bounds(ctx);
    if (!nk_group_begin(ctx, "tabs", NK_WINDOW_NO_SCROLLBAR)) return;
    canvas = nk_window_get_canvas(ctx);
    {
        unsigned id = reaktor_note_push(app, REAKTOR_A11Y_TABLIST, "Pages",
                                        NULL, 0, strip);
        char keys[160];
        sample_tablist_keys(keys, (int)sizeof(keys));
        reaktor_note_keys(app, id, keys);
    }

    {
        float used = 0.0f;
        for (i = 0; i < TAB_COUNT; i++) {
            const char *n = reaktor_tab_names[i];
            tabw[i] = font->width(font->userdata, font->height, n,
                                  (int)strlen(n)) + 2.0f * (float)TAB_PAD_X;
            used += tabw[i];
        }
        for (i = 0; i < 3; i++) {
            const char *n = g_theme_names[i];
            themew[i] = font->width(font->userdata, font->height, n,
                                    (int)strlen(n)) + 2.0f * (float)TAB_PAD_X;
            used += themew[i];
        }
        /* Tighter than a tab. A tab's padding is what separates it from the
         * tab beside it; this one stands alone at the end of the strip, so
         * the same padding just reads as a wide gray slab when it lights up. */
        sw_w = font->width(font->userdata, font->height, swl,
                           (int)strlen(swl)) + 2.0f * SW_PAD_X;
#ifdef __EMSCRIPTEN__
        sw_w = 0.0f;
#endif
        rest = (float)win_w - used - sw_w - sep - edge - 2.0f * 4.0f
             - (float)(TAB_COUNT + 5) * 4.0f;

        if (rest < 0.0f) { sep += rest; rest = 0.0f; }
        if (sep < 8.0f)  { edge += sep - 8.0f; sep = 8.0f; }
        if (edge < 4.0f) edge = 4.0f;
    }

    nk_style_push_font(ctx, font);
    nk_layout_row_begin(ctx, NK_STATIC, (float)(TAB_H - 6), TAB_COUNT + 6);
    for (i = 0; i < TAB_COUNT; i++) {
        const char *name = reaktor_tab_names[i];
        float w = tabw[i];
        struct nk_color fg = (i == sample_tab()) ? accent : muted;
        struct nk_rect b;

        nk_layout_row_push(ctx, w);
        b = nk_widget_bounds(ctx);
        hot_push(app, b, 1, 1);

        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(clear));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(wash));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(wash));
        nk_style_push_color(ctx, &ctx->style.button.text_normal, fg);
        nk_style_push_color(ctx, &ctx->style.button.text_hover,  fg);
        nk_style_push_color(ctx, &ctx->style.button.text_active, fg);
        nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
        nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);

        {
            unsigned id = reaktor_note(app, REAKTOR_A11Y_TAB, name, NULL,
                                       i == sample_tab() ? REAKTOR_A11Y_SELECTED
                                                     : 0u,
                                       b);
            char keys[96];
            int hit = nk_button_label(ctx, name);

            sample_tab_keys(i, keys, (int)sizeof(keys));
            reaktor_note_keys(app, id, keys);

            if (reaktor_focus_activated(app, id)) hit = 1;
            if (hit) set_tab(app, i);
        }
        if (i == sample_tab()) active_r = b;

        nk_style_pop_float(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
    }
    nk_layout_row_push(ctx, rest);
    nk_spacing(ctx, 1);
    reaktor_note_pop(app);

    reaktor_note_push(app, REAKTOR_A11Y_GROUP, "Color scheme", NULL, 0,
                      strip);
    for (i = 0; i < 3; i++) {
        struct nk_color fg = (i == app->theme_mode) ? accent : muted;
        struct nk_rect b;

        nk_layout_row_push(ctx, themew[i]);
        b = nk_widget_bounds(ctx);
        hot_push(app, b, 1, 1);

        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(clear));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(wash));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(wash));
        nk_style_push_color(ctx, &ctx->style.button.text_normal, fg);
        nk_style_push_color(ctx, &ctx->style.button.text_hover,  fg);
        nk_style_push_color(ctx, &ctx->style.button.text_active, fg);
        nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
        nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);

        reaktor_note(app, REAKTOR_A11Y_RADIO, g_theme_names[i], NULL,
                     i == app->theme_mode ? REAKTOR_A11Y_CHECKED : 0u, b);
        if (nk_button_label(ctx, g_theme_names[i]) && i != app->theme_mode) {
            app->theme_pending = i + 1;
            app->dirty = 1;
        }

        nk_style_pop_float(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_row_push(ctx, sep);
    nk_spacing(ctx, 1);
    reaktor_note_pop(app);

#ifndef __EMSCRIPTEN__
    nk_layout_row_push(ctx, sw_w);
    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 1);
        reaktor_note(app, REAKTOR_A11Y_BUTTON, swl, NULL, 0, b);
        nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                 nk_style_item_color(clear));
        nk_style_push_style_item(ctx, &ctx->style.button.hover,
                                 nk_style_item_color(wash));
        nk_style_push_style_item(ctx, &ctx->style.button.active,
                                 nk_style_item_color(wash));
        nk_style_push_color(ctx, &ctx->style.button.text_normal, muted);
        nk_style_push_color(ctx, &ctx->style.button.text_hover,  muted);
        nk_style_push_color(ctx, &ctx->style.button.text_active, muted);
        nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
        nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);
        /* The slot pushed above is the wash, so the label has to fit inside
         * it with the sheet's own button padding taken off - and tiny.css
         * asks for nearly all of SW_PAD_X. Zero here: the wash is the
         * padding. */
        nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));

        if (nk_button_label(ctx, swl)) {
#ifdef __APPLE__
            if (app->borderless_lock_frames == 0) {
                app->borderless_pending = 1;
                app->dirty = 1;
            }
#else
            app->borderless = !app->borderless;
            SDL_SetWindowBordered(app->win, app->borderless ? false : true);
            SDL_SetWindowHitTest(app->win,
                                 app->borderless ? window_hit_test : NULL,
                                 app->borderless ? app : NULL);
            app->ctl_n = 0;
            app->dirty = 1;
#endif
        }

        nk_style_pop_vec2(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_float(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
        nk_style_pop_style_item(ctx);
    }

#endif

    nk_layout_row_end(ctx);
    nk_style_pop_font(ctx);

    if (active_r.w > 0.0f)
        nk_fill_rect(canvas, nk_rect(active_r.x, active_r.y + active_r.h,
                                     active_r.w, 2.0f), 0.0f, accent);
    nk_group_end(ctx);
}

void
page_shell(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    float chrome = app->borderless ? (float)TITLEBAR_H : 0.0f;
    float top    = chrome + (float)TAB_H;
    float body_h = (float)win_h - top;

    if (body_h < 1.0f) body_h = 1.0f;

    nk_layout_space_begin(ctx, NK_STATIC, (float)win_h, 3);

    if (app->borderless) {
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                 nk_style_item_color(app->card_bg));
        nk_layout_space_push(ctx, nk_rect(0, 0, (float)win_w,
                                          (float)TITLEBAR_H));
        titlebar(app, ctx, win_w);
        nk_style_pop_style_item(ctx);
    }

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(app->card_bg));
    nk_layout_space_push(ctx, nk_rect(0, chrome, (float)win_w, (float)TAB_H));
    tab_strip(app, ctx, win_w);
    nk_style_pop_style_item(ctx);

    if (sample_tab() == TAB_LOGIN) {
        login_card(app, ctx, (float)win_w, top, body_h);
        nk_layout_space_end(ctx);
        return;
    }

    nk_layout_space_push(ctx, nk_rect(0, top, (float)win_w, body_h));
    if (g_scroll0 < 0) g_scroll0 = 0;
    if (g_scroll0 > 0) {
        nk_group_set_scroll(ctx, "body", 0, (nk_uint)g_scroll0);
        g_scroll0 = 0;
    }
    app->body_rect = nk_rect(0, top, (float)win_w, body_h);
    if (app->focus_scroll) {
        const float air = 12.0f;
        struct nk_rect r = app->focus_scroll_rect;
        nk_uint sx, sy;
        float dy = 0.0f;

        nk_group_get_scroll(ctx, "body", &sx, &sy);
        if (r.y < top + air)
            dy = r.y - (top + air);
        else if (r.y + r.h > top + body_h - air)
            dy = r.y + r.h - (top + body_h - air);
        if ((float)sy + dy < 0.0f) dy = -(float)sy;
        nk_group_set_scroll(ctx, "body", sx, (nk_uint)((float)sy + dy));
        app->focus_scroll = 0;
        app->dirty = 1;
    }

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_hide());
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(20.0f, 12.0f));
    nk_style_push_vec2(ctx, &ctx->style.window.scrollbar_size,
                       nk_vec2(ctx->style.window.scrollbar_size.x, 0.0f));
    if (nk_group_begin(ctx, "body", 0)) {
        struct nk_vec2 sz = nk_window_get_content_region_size(ctx);

        {
            struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
            struct nk_rect c = cv->clip;

            nk_push_scissor(cv, nk_rect(c.x - 2.0f, c.y, c.w + 4.0f, c.h));
        }

        nk_style_pop_vec2(ctx);
        nk_style_pop_vec2(ctx);
        nk_style_pop_style_item(ctx);

        hot_push(app, nk_rect(0, top, (float)win_w, body_h), 0, 0);

        app->page_node =
            reaktor_note_push(app, REAKTOR_A11Y_GROUP,
                              reaktor_tab_names[sample_tab()], NULL, 0,
                              nk_rect(0, top, (float)win_w, body_h));
        reaktor_showcase_page(app, ctx, sample_tab(), sz.x, sz.y);
        reaktor_note_pop(app);
        nk_group_end(ctx);
    } else {
        nk_style_pop_vec2(ctx);
        nk_style_pop_vec2(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_space_end(ctx);
}
