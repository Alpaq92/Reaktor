/* shell.c - the sample application's own chrome and screen.
 *
 * The borderless titlebar and its controls, the desktop hit test that makes a
 * frameless window draggable, the tab strip, the login card, and the page
 * frame the showcase draws into. None of it is library: it is what this
 * particular application looks like.
 *
 * Lifted out of main.c unchanged.
 */
#include "internal.h"
#include "sample.h"
#include "declare.h"

/* Which parts of a frameless window the desktop treats as chrome - without it
 * the window cannot be moved, resized, snapped or maximised by double-click.
 * SDL calls this from its own event handling, so it reads only recorded
 * rects and never touches Nuklear. */
SDL_HitTestResult SDLCALL
window_hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
    App *app = (App *)data;
    int w = 0, h = 0, i;
    int left, right, top, bottom;

    SDL_GetWindowSize(win, &w, &h);

    /* A maximised window has no outside edge to grab. */
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
        /* The buttons are holes in the drag region, or they could never be
         * clicked - the desktop would start moving the window instead. */
        /* Grown a few pixels, and the gaps between them matter as much as the
         * hits: a draggable region is HTCAPTION, and over a caption Windows
         * sends WM_NCMOUSEMOVE, which SDL does not deliver as motion. */
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


/* Which page is on screen, and where it opens scrolled. The environment
 * picks both, so a screenshot of one page needs no click at coordinates
 * guessed from outside and no synthesised wheel event. -1 is "not read yet":
 * there is no startup hook here, and the first frame is early enough. */
static int g_tab = -1;
static int g_scroll0 = -1;

int
sample_tab(void)
{
    if (g_tab < 0) {
        g_tab = env_int("REAKTOR_TAB", TAB_LOGIN);
        if (g_tab < 0 || g_tab >= TAB_COUNT) g_tab = TAB_LOGIN;
    }
    return g_tab;
}

void
set_tab(App *app, int tab)
{
    if (tab == sample_tab()) return;
    g_tab = tab;
    app->dirty = 1;
}


/* One window control: an Ionicon on a circular hover wash, through
 * nk_button_image rather than the canvas - the hand-rolled version painted
 * three white squares from SVGs that rasterise correctly at this size. A
 * rounding of half the height makes the highlight a circle, and image_padding
 * sets the glyph size; without it the icon fills the whole slot. */
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
    /* The glyph is the whole button, so the name is the only thing a reader
     * would have to go on - which is why it is a parameter and not derived
     * from the icon. */
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
    /* nk_draw_button_image tints the glyph by this factor, and it is not part
     * of any style we set: at zero the image multiplies to black. */
    nk_style_push_float(ctx, &ctx->style.button.color_factor_background, 1.0f);
    f.floats = 3;

    /* Zeroed first: nk_do_button_image insets by padding *and then* by
     * image_padding, so the default collapsed the content rect. The glyph
     * square then comes from the button's *height*, so on a square button
     * image_padding alone sizes and centres it. */
    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));

    pad_x = pad_y = (b.h - (float)px) * 0.5f;
    if (pad_x < 0.0f) pad_x = pad_y = 0.0f;
    nk_style_push_vec2(ctx, &ctx->style.button.image_padding,
                       nk_vec2(pad_x, pad_y));
    f.vec2s = 2;

    /* Every other icon is --text-muted. On the light scheme that leaves the
     * window controls a grey barely off the titlebar, and these three are
     * the one set of glyphs that must never be hunted for - so on a light
     * surface they take --text-main, as the platform's own do. Dark keeps
     * the muted stroke, which reads fine there. */
    if (app->dark) {
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg?stroke=%s&sw=%.2f",
                     glyph, app->icon_hex, (double)GLYPH_STROKE);
    } else {
        SDL_snprintf(src, sizeof(src),
                     "third_party/ionicons/src/svg/%s.svg"
                     "?stroke=#%02x%02x%02x&sw=%.2f",
                     glyph, app->text.r, app->text.g, app->text.b,
                     (double)GLYPH_STROKE);
    }

    /* nk_button_image_label with an empty label: nk_button_image draws the
     * background and then nothing at all here. */
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
    /* Before nk_group_begin: inside the group, nk_window_get_bounds answers
     * the enclosing window's rect rather than the group's, and a node whose
     * bounds are the whole window is one a magnifier cannot follow. */
    bar = nk_widget_bounds(ctx);
    if (!nk_group_begin(ctx, "titlebar", NK_WINDOW_NO_SCROLLBAR)) return;
    reaktor_note_push(app, REAKTOR_A11Y_GROUP, "Title bar", NULL, 0, bar);

    nk_layout_row_begin(ctx, NK_STATIC, (float)CTL_SIZE, 6);

    nk_layout_row_push(ctx, (float)TITLE_PAD);
    nk_spacing(ctx, 1);

    /* The same mark the desktop shows for the window and the executable, so
     * a frameless window still identifies itself. No query string, so nothing
     * in it is recoloured: the artwork carries its own colours, and its yellow
     * ground keeps it legible on either theme. */
    nk_layout_row_push(ctx, (float)MARK_SIZE);
    image_centred(ctx, icon(app, REAKTOR_MARK, MARK_SIZE), MARK_SIZE);

    /* No spacer between the mark and the name: the 4px Nuklear puts between
     * any two columns is the whole gap, and it read as a word-space with a
     * spacer column adding its own width and a second 4px. */

    /* Exactly the remainder, so the controls finish flush with the right edge:
     * the group's padding either side plus the five gaps Nuklear inserts
     * across six columns. A guessed constant left a strip of dead titlebar. */
    nk_layout_row_push(ctx, (float)(win_w - TITLE_PAD - MARK_SIZE -
                                    3 * CTL_SIZE - 2 * 4 - 5 * 4));
    {
        /* At the body size, in the same ink as the window controls beside
         * it: --text-muted on the dark scheme, --text-main on the light one -
         * the rule titlebar_button follows. */
        nk_style_push_font(ctx, pick_font(app, TITLE_PX, 1));
        if (app->dark && reaktor_style_token("--text-muted", c))
            nk_style_push_color(ctx, &ctx->style.text.color, col_of(c));
        else
            nk_style_push_color(ctx, &ctx->style.text.color, app->text);
        /* 3px of text padding rather than Nuklear's 4: with the column gap
         * and the mark's own margin, 4 held the name a word-space off the
         * mark and 0 put it against it. */
        nk_style_push_vec2(ctx, &ctx->style.text.padding, nk_vec2(3.0f, 0.0f));
        reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, "Reaktor", 0);
        nk_label(ctx, "Reaktor", NK_TEXT_LEFT);
        nk_style_pop_vec2(ctx);
        nk_style_pop_color(ctx);
        nk_style_pop_font(ctx);
    }

    /* Glyph names only - titlebar_button builds the path and appends the
     * theme's stroke colour. */
    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx, "remove-outline", "Minimise", GLYPH_MINIMISE))
        SDL_MinimizeWindow(app->win);

    nk_layout_row_push(ctx, (float)CTL_SIZE);
    if (titlebar_button(app, ctx,
                        maximised ? "copy-outline" : "square-outline",
                        maximised ? "Restore" : "Maximise",
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

/* --- the screen ---------------------------------------------------------- */

/* Row heights in logical px, and one gap between every row - which gives the
 * card an even rhythm and makes its height a sum that can be stated up front,
 * as Nuklear needs before the contents are emitted. */
#define ROW_BRAND   28
#define ROW_LABEL   16
#define ROW_FIELD   38
#define ROW_BUTTON  40
#define ROW_SMALL   18
#define ROW_GAP     15
#define CONTACT_ROWS 3

/* Nuklear's default group padding is 4px, which put the field and the buttons
 * hard against the card's edge. */
#define CARD_PAD_X  22
#define CARD_PAD_Y  18

static float
card_height(int with_diag)
{
    int rows = 7;                    /* brand..contact link */
    float h = (float)(ROW_BRAND + ROW_FIELD + ROW_BUTTON +
                      ROW_SMALL + ROW_BUTTON + ROW_BUTTON + ROW_SMALL);
    if (with_diag) {
        rows += CONTACT_ROWS;
        h += CONTACT_ROWS * ROW_SMALL;
    }
    /* Plus the card's own padding, top and bottom. */
    return h + (rows - 1) * ROW_GAP + 2.0f * CARD_PAD_Y + 6.0f;
}

/* A clickable line of text: Nuklear has no link widget and tiny.css no
 * component for one, so this is a label that reports its own hover and click.
 * The resting colour is tiny.css's --links, so it tracks the theme. */

/* The card, centred in whatever region the shell hands it. Placed with
 * nk_layout_space, which takes an explicit rect: the row APIs advance a
 * cursor, and mixing nk_spacing into a pushed row does not advance it the way
 * centring arithmetic assumes. */
/* The card's type, from assets/reaktor.css rather than from a number written
 * next to the label. tiny.css has no heading rule anywhere in it, so these
 * three sizes had nowhere to come from and were spelled out here; they are a
 * class each now, and this is the only thing that reads them.
 *
 * The fallback is what each one used to be. A stylesheet that loses a rule
 * should cost a page its styling, not its legibility. */
static const struct nk_user_font *
card_font(App *app, const char *selector, int px, int bold)
{
    reaktor_style st;

    reaktor_style_get(selector, &st);
    return pick_font(app, st.matched ? st.font_px : px,
                     st.matched ? st.bold : bold);
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

    /* Centred on the collapsed height so the card does not jump when contact
     * opens, but slid up if the expanded card would run off the bottom. */
    if (top + card_h > body_y + body_h - 8.0f)
        top = body_y + body_h - card_h - 8.0f;
    if (top  < body_y + 8.0f) top = body_y + 8.0f;

    /* Pushed into the shell's space, not a space of its own: the caller has
     * one open already, and a widget is what a pushed rect expects. */
    nk_layout_space_push(ctx, nk_rect(side, top, (float)CARD_W, card_h));

    /* Where that push actually lands, in screen coordinates. A rect pushed
     * into a space is local to it, so `side` and `top` are screen coordinates
     * only while the space starts at the origin - true on the desktop and not
     * in the browser, where the card's fill and its contents came apart by
     * exactly the difference. Taken here because inside the group
     * nk_window_get_bounds answers the enclosing window, which is the same
     * trap the titlebar above already documents. */
    at = nk_widget_bounds(ctx);

    /* The card's corners, taking the radius the buttons inside it are
     * already using. Nuklear fills a panel square and window.rounding is 0
     * because the page is, so the fill is drawn here instead - through the
     * masked primitive, so the corners are anti-aliased on the software
     * renderer like every other round thing - and Nuklear's own is pushed
     * transparent so it does not paint a square one over the top. */
    reaktor_fill_round(app, nk_window_get_canvas(ctx), at,
                       ctx->style.button.rounding, app->card_bg);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    /* Zero, not CARD_PAD: the padding is the declared boxes' margins now, and
     * applying it twice is exactly the trap core/ui/layout.h warns about. */
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding, nk_vec2(0, 0));
    if (nk_group_begin(ctx, "card", NK_WINDOW_NO_SCROLLBAR)) {
        /* Declared, not drawn. Every rect below comes from Onlay, every node
         * from the widget that made it, and the only numbers here are the
         * ones this card is actually built to. */
        REAKTOR_COLUMN(.name = "Proceed with login",
                       .w = CARD_W, .h = card_h, .gap = ROW_GAP) {
            REAKTOR_ROW(.h = ROW_BRAND, .gap = 10, .flags = REAKTOR_LAY_FILL_X,
                        .ml = CARD_PAD_X, .mr = CARD_PAD_X, .mt = CARD_PAD_Y) {
                reaktor_icon(&(reaktor_icon_spec){
                    .name = "person-circle-outline", .accent = 1,
                    .box  = { .w = ROW_BRAND, .h = ROW_BRAND } });
                reaktor_label(&(reaktor_label_spec){
                    .text  = "Proceed with login",
                    .style = ".card-title",
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
                .text = "or", .style = ".card-note", .centred = 1,
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
                /* Stub, and obviously so: the reserved example.com domain and
                 * an Ofcom drama number, which cannot reach anyone. */
                static const char *const lines[CONTACT_ROWS] = {
                    "support@reaktor.example",
                    "+44 20 7946 0958",
                    "Mon-Fri, 09:00-17:00 UTC"
                };
                int i;

                for (i = 0; i < CONTACT_ROWS; i++)
                    reaktor_label(&(reaktor_label_spec){
                        .text = lines[i], .style = ".card-link", .centred = 1,
                        .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                                 .ml = CARD_PAD_X, .mr = CARD_PAD_X } });
            }
        }
        nk_group_end(ctx);
    }
    nk_style_pop_vec2(ctx);            /* group_padding */
    nk_style_pop_style_item(ctx);
}


/* --- the tab strip -------------------------------------------------------
 *
 * Nuklear has no tab widget - nk_style_tab is the tree header - so a tab is a
 * flat button with an accent rule under the active one, sized to its own
 * label through the font. */
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
    /* Which frame the window wears. Here rather than on the login card because
     * the card is one page of seven, and with the native frame in use there is
     * no drawn titlebar to put it in. */
    const char *swl = app->borderless ? "native titlebar" : "custom titlebar";
    float tabw[TAB_COUNT], themew[3], sw_w = 0.0f, rest = 0.0f;
    /* Clear air between the scheme switch and the titlebar switch: they do
     * unrelated things and should not read as one row of five words. */
    const float TAB_SEP = 26.0f;
    int i;

    (void)win_w;
    strip = nk_widget_bounds(ctx);      /* see the note in titlebar() */
    if (!nk_group_begin(ctx, "tabs", NK_WINDOW_NO_SCROLLBAR)) return;
    canvas = nk_window_get_canvas(ctx);
    /* Two lists in one row, and a reader should not be told they are one: the
     * pages are a tablist, the colour schemes are a group of their own. */
    {
        /* Moving between pages belongs to the list, not to any one page in
         * it - which is also where a reader looks for it. */
        unsigned id = reaktor_note_push(app, REAKTOR_A11Y_TABLIST, "Pages",
                                        NULL, 0, strip);
        char keys[160];
        sample_tablist_keys(keys, (int)sizeof(keys));
        reaktor_note_keys(app, id, keys);
    }

    /* Measured first, because the switch at the far end needs to know what
     * is left over. */
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
        sw_w = font->width(font->userdata, font->height, swl,
                           (int)strlen(swl)) + 2.0f * (float)TAB_PAD_X;
#ifdef __EMSCRIPTEN__
        /* No desktop frame to switch to: the canvas is the window. */
        sw_w = 0.0f;
#endif
        /* The group pads either side, and Nuklear inserts 4px between each
         * column: tabs, spacer, three scheme links, separator, switch. */
        rest = (float)win_w - used - sw_w - TAB_SEP - 2.0f * 4.0f
             - (float)(TAB_COUNT + 5) * 4.0f;
        if (rest < 0.0f) rest = 0.0f;
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
            /* Not `||`: that short-circuits, so a tab that was clicked would
             * leave the activation unconsumed and the frame would turn it
             * into a second press. */
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

    /* "system" follows SDL_GetSystemTheme(), the other two pin it. Changing it
     * reloads the stylesheets - tiny.css ships light and dark as two files. */
    reaktor_note_push(app, REAKTOR_A11Y_GROUP, "Colour scheme", NULL, 0,
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
            /* Not applied here: see the top of SDL_AppIterate. */
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

    nk_layout_row_push(ctx, TAB_SEP);
    nk_spacing(ctx, 1);
    reaktor_note_pop(app);

#ifndef __EMSCRIPTEN__
    nk_layout_row_push(ctx, sw_w);
    {
        struct nk_rect b = nk_widget_bounds(ctx);

        hot_push(app, b, 1, 1);
        /* Its label says what it will switch to, so the checked state is the
         * frame it is *not* wearing - name it by what it does. */
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

        if (nk_button_label(ctx, swl)) {
            /* Live, not at startup. SDL_SetWindowBordered puts the frame
             * back, and the hit test has to go with it or it keeps claiming
             * the top of a window that no longer draws a titlebar.
             *
             * On macOS: record a request and let SDL_AppIterate apply it,
             * so the flip is not inline with a click that macOS is about
             * to synthesise a duplicate of. See borderless_pending. */
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

    /* After the buttons, so it lands on top of the hover wash. */
    if (active_r.w > 0.0f)
        nk_fill_rect(canvas, nk_rect(active_r.x, active_r.y + active_r.h,
                                     active_r.w, 2.0f), 0.0f, accent);
    nk_group_end(ctx);
}

/* --- the shell -----------------------------------------------------------
 *
 * Titlebar (only when frameless), tab strip, body. The body is a group so a
 * page can be taller than the window and need not know where on screen it is. */
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

    /* The login card is placed absolutely and never scrolls, so it goes into
     * this same space. Wrapping it in a body group cost a second full-window
     * fill every frame - four times the CPU while the pointer swept the card,
     * and invisible in build/render/present because it was in the renderer. */
    if (sample_tab() == TAB_LOGIN) {
        login_card(app, ctx, (float)win_w, top, body_h);
        nk_layout_space_end(ctx);
        return;
    }

    nk_layout_space_push(ctx, nk_rect(0, top, (float)win_w, body_h));
    if (g_scroll0 < 0) {
        g_scroll0 = env_int("REAKTOR_SCROLL", 0);
        if (g_scroll0 < 0) g_scroll0 = 0;
    }
    if (g_scroll0 > 0) {
        nk_group_set_scroll(ctx, "body", 0, (nk_uint)g_scroll0);
        g_scroll0 = 0;             /* a starting position, not a lock */
    }
    app->body_rect = nk_rect(0, top, (float)win_w, body_h);
    /* Keyboard focus landed outside the band above: bring it in, with a
     * little air, so the ring is not drawn against the edge. */
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

    /* A showcase page can be taller than the window. No background of its
     * own: the window was already cleared to exactly this colour. */
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_hide());
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(20.0f, 12.0f));
    /* The y is the height reserved for a horizontal scrollbar, which
     * nk_panel_begin subtracts whether or not one is shown, with no matching
     * adjustment at the top - so the thumb ran flush into the tab strip while
     * keeping a 10px gap below. These pages never scroll sideways. */
    nk_style_push_vec2(ctx, &ctx->style.window.scrollbar_size,
                       nk_vec2(ctx->style.window.scrollbar_size.x, 0.0f));
    if (nk_group_begin(ctx, "body", 0)) {
        struct nk_vec2 sz = nk_window_get_content_region_size(ctx);

        /* Nuklear puts the first widget of a row one pixel left of the
         * group's content edge, and the group's scissor is exactly that
         * edge - so the leftmost widget in every row lost the outer pixel of
         * its 2px border and read as cut down one side. Visible on both
         * renderers, so it is the layout and not the rasteriser. The scissor
         * is widened by two pixels either side; the group spans the whole
         * window, so this stays well inside it. */
        {
            struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
            struct nk_rect c = cv->clip;

            nk_push_scissor(cv, nk_rect(c.x - 2.0f, c.y, c.w + 4.0f, c.h));
        }

        /* Popped here rather than after the group ends: both were read when
         * the panel began, and leaving the hidden background on the stack
         * meant every popup and menu inside the page inherited it. */
        nk_style_pop_vec2(ctx);            /* scrollbar_size */
        nk_style_pop_vec2(ctx);            /* group_padding */
        nk_style_pop_style_item(ctx);

        /* A backstop under the whole page, pushed first so the last-match rule
         * lets any widget override it. It asks for no repaint: controls that
         * change on hover register themselves. */
        hot_push(app, nk_rect(0, top, (float)win_w, body_h), 0, 0);

        /* The page is a container, named by its tab, so a reader is told
         * which page it is walking rather than handed a flat list. */
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
