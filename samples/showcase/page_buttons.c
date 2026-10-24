#include "showcase.h"

#define CHECK_CELL 190.0f
#define CHECK_GAP   12.0f

static const struct { const char *icon, *label; } g_icons[] = {
    { "add-outline",          "Add"      }, { "remove-outline",   "Remove"   },
    { "close-outline",        "Close"    }, { "search-outline",   "Search"   },
    { "settings-outline",     "Settings" }, { "save-outline",     "Save"     },
    { "trash-outline",        "Delete"   }, { "share-social-outline", "Share" },
    { "refresh-outline",      "Refresh"  }, { "download-outline", "Download" },
    { "star-outline",         "Star"     }, { "heart-outline",    "Favourite" }
};
#define ICON_N ((int)(sizeof(g_icons) / sizeof(g_icons[0])))

/* A count after the label, in a pill of the accent. */
static void
paint_inbox(App *app, struct reaktor_command_buffer *cv, struct reaktor_rect c,
            struct reaktor_color ink, void *user)
{
    const struct reaktor_user_font *f = reaktor_style_font(app, "button", 14, 0);
    const char *n = (const char *)user;
    float nw = f->width(f->userdata, f->height, n, (int)SDL_strlen(n)) + 14.0f;
    float lw = f->width(f->userdata, f->height, "Inbox", 5);
    float x = c.x + (c.w - lw - 8.0f - nw) * 0.5f;
    float y = c.y + (c.h - f->height) * 0.5f;
    struct reaktor_color accent = reaktor_token("--links", reaktor_rgb(0, 118, 209));

    reaktor_draw_text(cv, reaktor_rect(x, y, lw, f->height), "Inbox", 5, f,
                      reaktor_rgba(0, 0, 0, 0), ink);
    reaktor_fill_round(app, cv, reaktor_rect(x + lw + 8.0f, y - 2.0f, nw, f->height + 4.0f),
                       (f->height + 4.0f) * 0.5f, accent);
    reaktor_draw_text(cv, reaktor_rect(x + lw + 15.0f, y, nw, f->height), n,
                      (int)SDL_strlen(n), f, reaktor_rgba(0, 0, 0, 0), reaktor_on(accent));
}

/* An icon over two lines of text, as a download list shows one. */
static void
paint_download(App *app, struct reaktor_command_buffer *cv, struct reaktor_rect c,
               struct reaktor_color ink, void *user)
{
    const struct reaktor_user_font *f = reaktor_style_font(app, "button", 14, 1);
    const struct reaktor_user_font *small = reaktor_style_font(app, "button", 12, 0);
    struct reaktor_image im = reaktor_ionicon(app, "download-outline", 24);
    float y = c.y + (c.h - f->height - small->height - 2.0f) * 0.5f;

    (void)user;
    reaktor_draw_image(cv, reaktor_rect(c.x, c.y + (c.h - 24.0f) * 0.5f, 24.0f, 24.0f), &im, ink);
    reaktor_draw_text(cv, reaktor_rect(c.x + 36.0f, y, c.w - 36.0f, f->height),
                      "reaktor-0.7.1-web.zip", 21, f, reaktor_rgba(0, 0, 0, 0), ink);
    reaktor_draw_text(cv, reaktor_rect(c.x + 36.0f, y + f->height + 2.0f, c.w - 36.0f,
                                       small->height),
                      "4.2 MB of 9.8 MB", 16, small, reaktor_rgba(0, 0, 0, 0),
                      reaktor_token("--text-muted", ink));
}

void
page_buttons(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    char line[96];
    int i;

    section(app, ctx, "Buttons",
            "The fill, border, radius, padding and font all come from "
            "Water.css's `button` rule; the hover color is its "
            "--button-hover. The accent is --links, which is the only thing "
            "in a classless stylesheet that means \"this is the one to "
            "press\".");
    api(app, ctx, "reaktor_button  /  .accent");

    {
        REAKTOR_ROW(.h = 38.0f,
                    .gap = ctx->style.window.spacing.x) {
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "Default",
                    .box = { .flags = REAKTOR_LAY_FILL } })) s->presses++;
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "Primary", .accent = 1,
                    .box = { .flags = REAKTOR_LAY_FILL } })) s->presses++;
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "With icon", .icon = "key-outline",
                    .box = { .flags = REAKTOR_LAY_FILL } })) s->presses++;
        }
    }

    section(app, ctx, "Icons",
            "Every icon is an Ionicon, read out of the submodule and "
            "rasterized at the size it is drawn - twice over, for the "
            "downscale. The stroke color is substituted into the file "
            "before it is parsed, which is how an icon follows the "
            "stylesheet: CSS cannot reach inside an SVG. The label is the "
            "icon's name to a reader as well as to the eye.");
    api(app, ctx, "reaktor_button  /  .icon");

    {
        static const struct { const char *icon, *label; } nav[3] = {
            { "chevron-back-outline",    "Back" },
            { "chevron-forward-outline", "Forward" },
            { "menu-outline",            "Menu" }
        };
        REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_WRAP,
                    .gap = ctx->style.window.spacing.x) {
            for (i = 0; i < ICON_N; i++)
                if (reaktor_button(&(reaktor_button_spec){
                        .label = g_icons[i].label, .icon = g_icons[i].icon,
                        .fit_content = 1 }))
                    s->presses++;
            for (i = 0; i < 3; i++)
                if (reaktor_button(&(reaktor_button_spec){
                        .label = nav[i].label, .icon = nav[i].icon,
                        .fit_content = 1 }))
                    s->presses++;
        }
    }

    section(app, ctx, "Width",
            "A button is as wide as its content with .fit_content, the way "
            "GTK and Qt size one, and its icon and label are centered "
            "together at any width. A .box.w with .fit_content is the least "
            "it takes, so short labels line up while a long or translated one "
            "still grows; .box.w alone is exact, and REAKTOR_LAY_FILL_X shares "
            "the row out.");
    api(app, ctx, ".fit_content  /  .box.w  /  REAKTOR_LAY_FILL_X  /  reaktor_button_width  "
                  "/  reaktor_button_accent_icon");
    {
        static const char *const how[4] = { "Content", "At least 100", "Exactly 100",
                                            "Shared" };
        static const reaktor_box box[4] = {
            { 0 }, { .w = 100.0f }, { .w = 100.0f }, { .flags = REAKTOR_LAY_FILL_X } };
        int m;

        for (m = 0; m < 4; m++) {
            REAKTOR_ROW(.h = 38.0f, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_label(&(reaktor_label_spec){ .text = how[m],
                                                     .box = { .w = 110.0f, .flags = REAKTOR_LAY_FILL_Y } });
                if (reaktor_button(&(reaktor_button_spec){
                        .label = "Back", .icon = "chevron-back-outline",
                        .fit_content = m < 2, .box = box[m] }))
                    s->presses++;
                if (reaktor_button(&(reaktor_button_spec){
                        .label = "Add folder", .icon = "folder-open-outline",
                        .fit_content = m < 2, .box = box[m] }))
                    s->presses++;
                if (reaktor_button(&(reaktor_button_spec){
                        .label = "Wipe", .icon = "flame-outline", .accent = 1,
                        .fit_content = m < 2, .box = box[m] }))
                    s->presses++;
            }
        }
    }

    section(app, ctx, "Custom content",
            "The sheet's button with nothing in it: its fill, edge, hover, "
            "focus ring and click are the runtime's, and a callback paints "
            "the inside, given the content box inside the padding and the "
            "text color for the button's state. Its name is what a reader hears.");
    api(app, ctx, ".paint  /  .paint_user  /  reaktor_button_custom(app, ctx, name, paint, user)");
    REAKTOR_ROW(.h = 52.0f, .flags = REAKTOR_LAY_FILL_X,
                .gap = ctx->style.window.spacing.x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Inbox, 12 new", .paint = paint_inbox, .paint_user = "12",
                .box = { .w = 180.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->presses++;
        if (reaktor_button(&(reaktor_button_spec){
                .label = "reaktor-0.7.1-web.zip, 4.2 MB of 9.8 MB", .paint = paint_download,
                .box = { .w = 280.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->presses++;
    }

    section(app, ctx, "Flat icons",
            "An icon with no label and .flat set is a toolbar button: no "
            "fill and no edge until the pointer is over it, then the sheet's "
            "button:hover. The icon takes the text color, or the accent's "
            "with .accent, which is how a tool shows it is on. Its .name is "
            "what a reader hears, and .tooltip what the pointer shows.");
    api(app, ctx, "reaktor_button  /  .icon  /  .flat");

    {
        static const struct { const char *icon, *name; } tools[8] = {
            { "cut-outline",       "Cut" },     { "copy-outline",     "Copy" },
            { "clipboard-outline", "Paste" },   { "arrow-undo-outline", "Undo" },
            { "arrow-redo-outline", "Redo" },   { "search-outline",   "Find" },
            { "locate-outline",    "Pick" },    { "trash-outline",    "Delete" }
        };
        static int on = 6;

        REAKTOR_ROW(.h = 32.0f, .gap = 2.0f) {
            for (i = 0; i < 8; i++)
                if (reaktor_button(&(reaktor_button_spec){
                        .icon = tools[i].icon, .name = tools[i].name,
                        .tooltip = tools[i].name, .flat = 1,
                        .accent = (unsigned char)(i == on),
                        .box = { .w = 32.0f, .h = 32.0f } })) {
                    on = i;
                    s->presses++;
                }
        }
    }

    section(app, ctx, "Color, image, repeat and disabled",
            "reaktor_button_color paints a swatch and nothing else. A repeater "
            "fires for as long as it is held rather than once on release. "
            "reaktor_widget_disable_begin swallows the click and multiplies "
            "every color by a factor - which reads as \"greyed out\" on a "
            "dark palette and, since multiplying can only darken, as a "
            "*darker* button on a light one. Worth knowing before "
            "relying on it to mean unavailable.");
    api(app, ctx, "reaktor_swatch  /  reaktor_button  /  .repeat  /  .disabled");

    {
        REAKTOR_ROW(.h = 40.0f,
                    .gap = ctx->style.window.spacing.x) {
            compact_push(ctx);
            if (reaktor_swatch(&(reaktor_swatch_spec){
                    .name = "Tint swatch", .fill = reaktor_rgb_cf(s->tint),
                    .box = { .w = 40.0f, .flags = REAKTOR_LAY_FILL_Y } }))
                s->presses++;
            if (reaktor_button(&(reaktor_button_spec){
                    .name = "Download", .icon = "cloud-download-outline",
                    .box = { .w = 40.0f, .flags = REAKTOR_LAY_FILL_Y } }))
                s->presses++;
            compact_pop(ctx);

            if (reaktor_button(&(reaktor_button_spec){
                    .label = "Hold me", .repeat = 1,
                    .box = { .flags = REAKTOR_LAY_FILL } }))
                s->repeats++;
            reaktor_button(&(reaktor_button_spec){
                .label = "Disabled", .disabled = 1,
                .box = { .flags = REAKTOR_LAY_FILL } });
        }
    }

    reaktor_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "%d presses, %d repeat ticks",
                 s->presses, s->repeats);
    reaktor_style_push_font(ctx, reaktor_style_font(app, ".small", 13, 0));
    reaktor_label_colored(ctx, line, REAKTOR_TEXT_LEFT,
                          reaktor_token("--text-muted", ctx->style.text.color));
    reaktor_style_pop_font(ctx);

    section(app, ctx, "Checkboxes and radios",
            "A checkbox writes a bool. reaktor_checkbox_flags_label writes one "
            "bit of an unsigned instead, which is how a set of independent "
            "options is held in a single value. Radios are a group only because "
            "the code makes them one: reaktor_option_label takes the answer to "
            "\"am I the active one\" and returns whether it was clicked. Both "
            "sit their box at the right of the cell, the way the Edit menu "
            "does; the flags form has no aligned variant, so this page wraps "
            "the aligned call in its own four lines.");
    api(app, ctx, "reaktor_check  /  reaktor_radio");

    {
        static const struct { const char *label; unsigned bit; } perms[3] = {
            { "read", 1u }, { "write", 2u }, { "execute", 4u }
        };

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_check(&(reaktor_check_spec){
                    .label = "Wrap long lines", .on = &s->check_wrap,
                    .box_right = 1,
                    .box = { .w = CHECK_CELL,
                             .flags = REAKTOR_LAY_FILL_Y } });
                reaktor_gap(CHECK_GAP, 0.0f);
                reaktor_check(&(reaktor_check_spec){
                    .label = "Check spelling", .on = &s->check_spell,
                    .box_right = 1,
                    .box = { .w = CHECK_CELL,
                             .flags = REAKTOR_LAY_FILL_Y } });
                reaktor_gap(CHECK_GAP, 0.0f);
                reaktor_soak();
            }

            REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                for (i = 0; i < 3; i++) {
                    reaktor_check(&(reaktor_check_spec){
                        .label = perms[i].label, .flags = &s->flags,
                        .bit = perms[i].bit, .box_right = 1,
                        .box = { .w = CHECK_CELL,
                                 .flags = REAKTOR_LAY_FILL_Y } });
                    reaktor_gap(CHECK_GAP, 0.0f);
                }
                reaktor_soak();
            }
        }
    }

    reaktor_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "flags = 0x%02x", s->flags);
    reaktor_style_push_font(ctx, reaktor_style_font(app, ".small", 13, 0));
    reaktor_label_colored(ctx, line, REAKTOR_TEXT_LEFT,
                          reaktor_token("--text-muted", ctx->style.text.color));
    reaktor_style_pop_font(ctx);

    {
        static const char *const names[3] = { "Never", "On Wi-Fi", "Always" };

        REAKTOR_ROW(.h = ROW,
                    .gap = ctx->style.window.spacing.x) {
            for (i = 0; i < 3; i++) {
                reaktor_radio(&(reaktor_radio_spec){
                    .label = names[i], .choice = &s->radio, .value = i,
                    .box = { .w = CHECK_CELL,
                             .flags = REAKTOR_LAY_FILL_Y } });
                reaktor_gap(CHECK_GAP, 0.0f);
            }
            reaktor_soak();
        }
    }

    section(app, ctx, "Selectables",
            "A selectable is a label that holds a pressed state - the widget "
            "a list or a tile grid is built out of. It also takes a symbol "
            "or an image, which is how a toolbar is made without a single "
            "custom draw call.");
    api(app, ctx, "reaktor_select  /  .icon");

    {
        REAKTOR_ROW(.h = 32.0f,
                    .gap = ctx->style.window.spacing.x) {
            for (i = 0; i < 4; i++) {
                char lab[16];

                SDL_snprintf(lab, sizeof(lab), "Tile %d", i + 1);
                reaktor_select(&(reaktor_select_spec){
                    .label = lab, .on = &s->sel_tile[i], .centered = 1,
                    .box = { .w = 140.0f, .flags = REAKTOR_LAY_FILL_Y } });
            }
        }
    }

    {
        REAKTOR_ROW(.h = 32.0f,
                    .gap = ctx->style.window.spacing.x) {
            reaktor_select(&(reaktor_select_spec){
                .label = "With a symbol", .on = &s->sel_row,
                .disc = 1, .centered = 1,
                .box = { .w = 190.0f, .flags = REAKTOR_LAY_FILL_Y } });
            reaktor_select(&(reaktor_select_spec){
                .label = "With an image", .on = &s->toggle,
                .icon = "star-outline", .centered = 1,
                .box = { .w = 190.0f, .flags = REAKTOR_LAY_FILL_Y } });
        }
    }

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
