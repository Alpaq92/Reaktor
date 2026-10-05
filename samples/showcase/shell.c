#include "showcase.h"
#include "reaktor/main.h"

#define TITLE_PX     16
#define GLYPH_STROKE 2.0f
#define BODY_PAD_X   20.0f

typedef struct shell_style { int items, colors, floats, vec2s, fonts; } shell_style;

static struct nk_rect g_ctl[3];
static int            g_ctl_n;
static int            g_show_contact;

static void
pop_style(struct nk_context *ctx, shell_style f)
{
    int i;
    for (i = 0; i < f.fonts;  i++) nk_style_pop_font(ctx);
    for (i = 0; i < f.vec2s;  i++) nk_style_pop_vec2(ctx);
    for (i = 0; i < f.floats; i++) nk_style_pop_float(ctx);
    for (i = 0; i < f.colors; i++) nk_style_pop_color(ctx);
    for (i = 0; i < f.items;  i++) nk_style_pop_style_item(ctx);
}

static void
image_centered(struct nk_context *ctx, struct nk_image im, int px)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    float s = (float)px;
    struct nk_rect r = nk_rect(b.x + (b.w - s) * 0.5f,
                               b.y + (b.h - s) * 0.5f, s, s);

    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
    nk_spacing(ctx, 1);
}

static struct nk_color
text_main(void)
{
    return reaktor_token("--text-main", nk_rgb(51, 51, 51));
}

static SDL_HitTestResult SDLCALL
window_hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
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

    (void)data;
    if (pt->y < TITLEBAR_H) {
        for (i = 0; i < g_ctl_n; i++) {
            struct nk_rect r = g_ctl[i];
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
static char g_login[128];
static int  g_login_len;

int
sample_tab(void)
{
    if (g_tab < 0) g_tab = TAB_LOGIN;
    return g_tab;
}

static int
sample_start(App *app, int argc, char **argv)
{
    int i;

    showcase_tray_show(app, 1);
    for (i = 1; i < argc; i++) {
        int n = i + 1 < argc ? SDL_atoi(argv[i + 1]) : -1;

        if (SDL_strcmp(argv[i], "--floater") == 0) showcase_open_floater(app, 0);
        else if (SDL_strcmp(argv[i], "--modal-floater") == 0) showcase_open_floater(app, 1);
        else if (SDL_strcmp(argv[i], "--toasts") == 0) showcase_toasts(app);
        else if (SDL_strcmp(argv[i], "--window") == 0) showcase_open_window(app, 0);
        else if (SDL_strcmp(argv[i], "--modal-window") == 0) showcase_open_window(app, 1);
        else if (i + 1 < argc && SDL_strcmp(argv[i], "--tab") == 0) {
            if (n >= 0 && n < TAB_COUNT) g_tab = n;
            i++;
        } else if (i + 1 < argc && SDL_strcmp(argv[i], "--scroll") == 0) {
            if (n >= 0) g_scroll0 = n;
            i++;
        }
    }
    return 0;
}

void
set_tab(App *app, int tab)
{
    if (tab == sample_tab()) return;
    g_tab = tab;
    reaktor_wake(app);
}

static int
titlebar_button(App *app, struct nk_context *ctx, const char *glyph,
                const char *name, int px)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    shell_style f = { 0, 0, 0, 0, 0 };
    unsigned char c[4];
    struct nk_color clear = nk_rgba(0, 0, 0, 0);
    struct nk_color wash = reaktor_style_token("--background-hover", c)
                         ? reaktor_col(c) : nk_rgba(255, 255, 255, 26);
    char src[176];
    float pad_x, pad_y;
    int clicked;

    reaktor_hot(app, b, 1, 1);
    reaktor_note(app, REAKTOR_A11Y_BUTTON, name, NULL, 0, b);
    if (g_ctl_n < (int)(sizeof(g_ctl) / sizeof(g_ctl[0])))
        g_ctl[g_ctl_n++] = b;

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

    if (reaktor_dark(app)) {
        if (!reaktor_style_token("--text-muted", c)) {
            c[0] = 0x6a; c[1] = 0x6a; c[2] = 0x6a;
        }
    } else {
        struct nk_color t = text_main();

        c[0] = t.r; c[1] = t.g; c[2] = t.b;
    }
    SDL_snprintf(src, sizeof(src),
                 "external/ionicons/src/svg/%s.svg"
                 "?stroke=#%02x%02x%02x&sw=%.2f",
                 glyph, c[0], c[1], c[2], (double)GLYPH_STROKE);

    clicked = nk_button_image_label(ctx, reaktor_svg(app, src, px), "",
                                    NK_TEXT_CENTERED);
    pop_style(ctx, f);
    return clicked;
}

static void
titlebar(App *app, struct nk_context *ctx, int win_w)
{
    unsigned char c[4];
    SDL_Window *win = reaktor_sdl_window(app);
    int maximised = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
    struct nk_rect bar;

    g_ctl_n = 0;
    bar = nk_widget_bounds(ctx);
    if (!nk_group_begin(ctx, "titlebar", NK_WINDOW_NO_SCROLLBAR)) return;
    reaktor_note_push(app, REAKTOR_A11Y_GROUP, "Title bar", NULL, 0, bar);

    nk_layout_row_begin(ctx, NK_STATIC, (float)CTL_SIZE, 6);

    nk_layout_row_push(ctx, (float)TITLE_PAD);
    nk_spacing(ctx, 1);

    nk_layout_row_push(ctx, (float)MARK_SIZE);
    image_centered(ctx, reaktor_svg(app, REAKTOR_MARK, MARK_SIZE), MARK_SIZE);

    nk_layout_row_push(ctx, (float)(win_w - TITLE_PAD - MARK_SIZE -
                                    3 * CTL_SIZE - 2 * 4 - 5 * 4));
    {
        nk_style_push_font(ctx, reaktor_font(app, TITLE_PX, 1));
        if (reaktor_dark(app) && reaktor_style_token("--text-muted", c))
            nk_style_push_color(ctx, &ctx->style.text.color, reaktor_col(c));
        else
            nk_style_push_color(ctx, &ctx->style.text.color, text_main());
        nk_style_push_vec2(ctx, &ctx->style.text.padding, nk_vec2(3.0f, 0.0f));
        reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, "Reaktor", 0);
        nk_label(ctx, "Reaktor", NK_TEXT_LEFT);
        nk_style_pop_vec2(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_font(ctx);
    }

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "remove-outline", "Minimize", GLYPH_MINIMISE))
        SDL_MinimizeWindow(win);

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx,
                        maximised ? "copy-outline" : "square-outline",
                        maximised ? "Restore" : "Maximize",
                        GLYPH_MAXIMISE)) {
        if (maximised) SDL_RestoreWindow(win);
        else           SDL_MaximizeWindow(win);
    }

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "close-outline", "Close", GLYPH_CLOSE))
        reaktor_request_quit(app);

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
login_card(App *app, struct nk_context *ctx, float body_x, float body_w,
           float body_y, float body_h)
{
    struct nk_rect at;
    float card_h = card_height(g_show_contact);
    float side = body_x + (body_w - (float)CARD_W) * 0.5f;
    float top  = body_y + (body_h - card_height(0)) * 0.5f;

    if (side < body_x + 8.0f) side = body_x + 8.0f;

    if (top + card_h > body_y + body_h - 8.0f)
        top = body_y + body_h - card_h - 8.0f;
    if (top  < body_y + 8.0f) top = body_y + 8.0f;

    nk_layout_space_push(ctx, nk_rect(side, top, (float)CARD_W, card_h));

    at = nk_widget_bounds(ctx);

    reaktor_fill_round(app, nk_window_get_canvas(ctx), at,
                       ctx->style.button.rounding,
                       reaktor_token("--background", nk_rgb(226, 226, 226)));
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

            reaktor_field(&(reaktor_field_spec){
                .buf = g_login, .len = &g_login_len,
                .cap = (int)sizeof(g_login),
                .hint = "you@example.com",
                .box = { .h = ROW_FIELD, .flags = REAKTOR_LAY_FILL_X,
                         .ml = CARD_PAD_X, .mr = CARD_PAD_X } });

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
                    .text = g_show_contact ? "hide contact" : "contact",
                    .style = ".card-link",
                    .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                             .ml = CARD_PAD_X, .mr = CARD_PAD_X } }))
                g_show_contact = !g_show_contact;

            if (g_show_contact) {
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

static shell_style
push_strip_look(struct nk_context *ctx, struct nk_color fg, struct nk_color wash)
{
    nk_style_push_style_item(ctx, &ctx->style.button.normal,
                             nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_style_item(ctx, &ctx->style.button.hover,
                             nk_style_item_color(wash));
    nk_style_push_style_item(ctx, &ctx->style.button.active,
                             nk_style_item_color(wash));
    nk_style_push_color(ctx, &ctx->style.button.text_normal, fg);
    nk_style_push_color(ctx, &ctx->style.button.text_hover,  fg);
    nk_style_push_color(ctx, &ctx->style.button.text_active, fg);
    nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
    nk_style_push_float(ctx, &ctx->style.button.rounding, 4.0f);
    return (shell_style){ 3, 3, 2, 0, 0 };
}

static struct nk_color
strip_wash(void)
{
    return reaktor_token("--background-hover", nk_rgba(128, 128, 128, 40));
}

int
sample_option(App *app, struct nk_context *ctx, const char *label, float w,
              int on)
{
    struct nk_color fg = reaktor_token(on ? "--links" : "--text-muted", text_main());
    struct nk_rect  b;
    shell_style     look;
    unsigned        id;
    int             hit;

    if (w <= 0.0f) {
        const struct nk_user_font *f = ctx->style.font;
        w = f->width(f->userdata, f->height, label, (int)strlen(label))
          + 2.0f * (float)TAB_PAD_X;
    }
    nk_layout_row_push(ctx, w);
    b = nk_widget_bounds(ctx);
    reaktor_hot(app, b, 1, 1);

    look = push_strip_look(ctx, fg, strip_wash());
    id   = reaktor_note(app, REAKTOR_A11Y_RADIO, label, NULL,
                        on ? REAKTOR_A11Y_CHECKED : 0u, b);
    hit  = nk_button_label(ctx, label);
    if (reaktor_focus_activated(app, id)) hit = 1;
    pop_style(ctx, look);
    return hit && !on;
}

#ifndef __EMSCRIPTEN__
static void
toggle_titlebar(App *app)
{
    reaktor_set_borderless(app, !reaktor_borderless(app));
    g_ctl_n = 0;
}
#endif

static void
nav(App *app, float w, float h)
{
    static const char *const icons[TAB_COUNT] = {
        "person-outline", "tablet-landscape-outline", "create-outline",
        "eye-outline", "copy-outline", "chatbubble-outline",
        "play-circle-outline", "color-fill-outline", "language-outline",
        "pulse-outline"
    };
    static const char *const schemes[3] = { "System", "Light", "Dark" };
    static const char *const scheme_icons[3] = {
        "contrast-outline", "sunny-outline", "moon-outline"
    };
    static char keys[TAB_COUNT][96];
    const char *key_of[TAB_COUNT];
    char all[160];
    unsigned char narrow = w < (float)NAV_W;
    int tab = sample_tab(), scheme = (int)reaktor_get_theme(app), i;

    for (i = 0; i < TAB_COUNT; i++) {
        sample_tab_keys(i, keys[i], (int)sizeof(keys[i]));
        key_of[i] = keys[i];
    }
    sample_tablist_keys(all, (int)sizeof(all));

    REAKTOR_FREE(.w = w, .h = h)
    REAKTOR_COLUMN(.w = w - 16.0f, .h = h - 16.0f, .ml = 8.0f, .mt = 8.0f,
                   .gap = 2.0f) {
        if (reaktor_sidebar(&(reaktor_sidebar_spec){
                .items = reaktor_tab_names, .icons = icons, .keys = key_of,
                .count = TAB_COUNT, .chosen = &tab, .name = "Pages",
                .list_keys = all, .narrow = narrow,
                .box = { .flags = REAKTOR_LAY_FILL_X } }))
            set_tab(app, tab);
        REAKTOR_COLUMN(.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y) {}
        if (reaktor_sidebar(&(reaktor_sidebar_spec){
                .items = schemes, .icons = scheme_icons, .count = 3,
                .chosen = &scheme, .name = "Color scheme",
                .kind = REAKTOR_NAV_CHOICE, .narrow = narrow,
                .box = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_set_theme(app, (reaktor_theme)scheme);
#ifndef __EMSCRIPTEN__
        {
            static const char *const bar_icon[1] = { "browsers-outline" };
            const char *bar[1];
            int pick = -1;

            bar[0] = reaktor_borderless(app) ? "Native titlebar" : "Custom titlebar";
            if (reaktor_sidebar(&(reaktor_sidebar_spec){
                    .items = bar, .icons = bar_icon, .count = 1, .chosen = &pick,
                    .name = "Window", .kind = REAKTOR_NAV_ACTIONS,
                    .narrow = narrow, .box = { .flags = REAKTOR_LAY_FILL_X } }))
                toggle_titlebar(app);
        }
#endif
    }
}

enum { TRAY_SHOW, TRAY_TOAST, TRAY_GAP, TRAY_SYSTEM, TRAY_LIGHT, TRAY_DARK };

static int g_tray_on;

static reaktor_theme g_tray_theme[3] = {
    REAKTOR_THEME_SYSTEM, REAKTOR_THEME_LIGHT, REAKTOR_THEME_DARK
};

static void
tray_show(App *app, int checked, void *user)
{
    (void)checked; (void)user;
    SDL_RestoreWindow(reaktor_sdl_window(app));
    SDL_RaiseWindow(reaktor_sdl_window(app));
}

static void
tray_toast(App *app, int checked, void *user)
{
    (void)checked; (void)user;
    reaktor_toast(app, &(reaktor_toast_spec){ .text = "Sent from the tray",
                                              .timeout_ms = 5000 });
}

static void
tray_theme(App *app, int checked, void *user)
{
    (void)checked;
    reaktor_set_theme(app, *(reaktor_theme *)user);
}

static void
tray_quit(App *app, int checked, void *user)
{
    (void)checked; (void)user;
    reaktor_request_quit(app);
}

static const reaktor_tray_item g_tray_items[] = {
    { .label = "Show Showcase", .chosen = tray_show },
    { .label = "Send a toast", .chosen = tray_toast },
    { NULL },
    { .label = "System Theme", .checkbox = 1, .chosen = tray_theme,
      .user = &g_tray_theme[0] },
    { .label = "Light Theme", .checkbox = 1, .chosen = tray_theme,
      .user = &g_tray_theme[1] },
    { .label = "Dark Theme", .checkbox = 1, .chosen = tray_theme,
      .user = &g_tray_theme[2] },
    { NULL },
    { .label = "Quit", .chosen = tray_quit }
};

int
showcase_tray_on(void)
{
    return g_tray_on;
}

void
showcase_tray_show(App *app, int on)
{
    if (on && !g_tray_on) {
        g_tray_on = reaktor_tray_open(app, &(reaktor_tray){
            .tooltip = "Showcase", .items = g_tray_items,
            .count = (int)(sizeof(g_tray_items) / sizeof(g_tray_items[0])) });
    } else if (!on && g_tray_on) {
        reaktor_tray_close(app);
        g_tray_on = 0;
    }
}

static void
body_metrics_push(struct nk_context *ctx, float pad_x)
{
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(pad_x, 12.0f));
    nk_style_push_vec2(ctx, &ctx->style.window.scrollbar_size,
                       nk_vec2(ctx->style.window.scrollbar_size.x, 0.0f));
}

static void
body_metrics_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
    nk_style_pop_vec2(ctx);
}

static void
page_shell(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    int   framed = reaktor_borderless(app);
    float top    = framed ? (float)TITLEBAR_H : 0.0f;
    float nav_w  = win_w < NAV_WIDE_MIN ? (float)NAV_W_NARROW : (float)NAV_W;
    float body_h = (float)win_h - top;
    struct nk_rect panel = nk_rect(nav_w, top + (framed ? 0.0f : PANEL_GAP),
                                   (float)win_w - nav_w - PANEL_GAP, 0.0f);
    struct nk_rect body, reveal;
    struct nk_color card = reaktor_token("--background", nk_rgb(226, 226, 226));
    reaktor_theme scheme = reaktor_get_theme(app);

    panel.h = (float)win_h - panel.y - PANEL_GAP;
    if (panel.w < 1.0f) panel.w = 1.0f;
    if (panel.h < 1.0f) panel.h = 1.0f;
    if (body_h < 1.0f) body_h = 1.0f;
    body = nk_rect(panel.x, panel.y + PANEL_R * 0.5f, panel.w, panel.h - PANEL_R);
    reaktor_tray_check(app, TRAY_SYSTEM, scheme == REAKTOR_THEME_SYSTEM);
    reaktor_tray_check(app, TRAY_LIGHT, scheme == REAKTOR_THEME_LIGHT);
    reaktor_tray_check(app, TRAY_DARK, scheme == REAKTOR_THEME_DARK);
    {
        struct nk_command_buffer *cv = nk_window_get_canvas(ctx);

        nk_fill_rect(cv, nk_rect(0.0f, 0.0f, (float)win_w, (float)win_h), 0.0f,
                     card);
        reaktor_fill_round(app, cv, panel, PANEL_R,
                           reaktor_token("--background-body", nk_rgb(247, 247, 247)));
    }

    nk_layout_space_begin(ctx, NK_STATIC, (float)win_h, 3);

    if (framed) {
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                 nk_style_item_color(card));
        nk_layout_space_push(ctx, nk_rect(0, 0, (float)win_w,
                                          (float)TITLEBAR_H));
        titlebar(app, ctx, win_w);
        nk_style_pop_style_item(ctx);
    }

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(card));
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding, nk_vec2(0, 0));
    nk_layout_space_push(ctx, nk_rect(0, top, nav_w, body_h));
    if (nk_group_begin(ctx, "nav", NK_WINDOW_NO_SCROLLBAR)) {
        nav(app, nav_w, body_h);
        nk_group_end(ctx);
    }
    nk_style_pop_vec2(ctx);
    nk_style_pop_style_item(ctx);

    if (sample_tab() == TAB_LOGIN) {
        login_card(app, ctx, panel.x, panel.w, panel.y, panel.h);
        nk_layout_space_end(ctx);
        return;
    }

    nk_layout_space_push(ctx, body);
    if (g_scroll0 < 0) g_scroll0 = 0;
    if (g_scroll0 > 0) {
        static int tries;
        nk_uint sx, sy;

        /* Nuklear clamps to the content, which is placed a frame late. */
        nk_group_get_scroll(ctx, "body", &sx, &sy);
        if (sy == (nk_uint)g_scroll0 || ++tries > 8) {
            g_scroll0 = 0;
        } else {
            nk_group_set_scroll(ctx, "body", 0, (nk_uint)g_scroll0);
            reaktor_wake(app);
        }
    }
    if (reaktor_focus_scroll(app, &reveal)) {
        const float air = 12.0f;
        struct nk_rect r = reveal;
        nk_uint sx, sy;
        float dy = 0.0f;

        nk_group_get_scroll(ctx, "body", &sx, &sy);
        if (r.y < body.y + air)
            dy = r.y - (body.y + air);
        else if (r.y + r.h > body.y + body.h - air)
            dy = r.y + r.h - (body.y + body.h - air);
        if ((float)sy + dy < 0.0f) dy = -(float)sy;
        nk_group_set_scroll(ctx, "body", sx, (nk_uint)((float)sy + dy));
        reaktor_wake(app);
    }

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_hide());
    body_metrics_push(ctx, BODY_PAD_X);
    if (nk_group_begin(ctx, "body", 0)) {
        struct nk_vec2 sz = nk_window_get_content_region_size(ctx);

        {
            struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
            struct nk_rect c = cv->clip;

            nk_push_scissor(cv, nk_rect(c.x - 2.0f, c.y, c.w + 4.0f, c.h));
        }

        body_metrics_pop(ctx);
        nk_style_pop_style_item(ctx);

        reaktor_hot(app, body, 0, 0);

        reaktor_focus_area(app, body,
                           reaktor_note_push(app, REAKTOR_A11Y_GROUP,
                                             reaktor_tab_names[sample_tab()],
                                             NULL, 0, body));
        reaktor_showcase_page(app, ctx, sample_tab(), sz.x, sz.y);
        reaktor_note_pop(app);
        /* nk_group_end sets the bar this far past the content: mid-gutter. */
        body_metrics_push(ctx, BODY_PAD_X * 0.5f);
        nk_group_end(ctx);
        body_metrics_pop(ctx);
    } else {
        body_metrics_pop(ctx);
        nk_style_pop_style_item(ctx);
    }

    nk_layout_space_end(ctx);
}

int
main(int argc, char **argv)
{
    return launchApp(argc, argv, &(reaktor_launch){
        .name           = "Showcase",
        .window         = { .w = WINDOW_WIDTH, .h = WINDOW_HEIGHT,
                            .borderless = 1, .hit_test = window_hit_test },
        .font_fallbacks = { { .path = "assets/fonts/MPLUS1p-Regular.ttf" } },
        .page           = page_shell,
        .key            = sample_key,
        .start          = sample_start,
        .file_opened    = sample_file_opened });
}
