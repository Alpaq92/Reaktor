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

void
page_buttons(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    char line[96];
    int i;

    section(app, ctx, "Buttons",
            "The fill, border, radius, padding and font all come from "
            "tiny.css's `button` rule; the hover color is its "
            "--button-hover. The accent is --links, which is the only thing "
            "in a classless stylesheet that means \"this is the one to "
            "press\".");
    api(app, ctx, "reaktor_button  /  .accent");

    {
        REAKTOR_ROW(.h = 38.0f,
                    .gap = ctx->style.window.spacing.x) {
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "Default",
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } })) s->presses++;
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "Primary", .accent = 1,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } })) s->presses++;
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "With icon", .icon = "key-outline",
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } })) s->presses++;
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
        int row, col;

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            for (row = 0; row * 4 < ICON_N; row++) {
                REAKTOR_ROW(.h = 38.0f, .flags = REAKTOR_LAY_FILL_X,
                            .gap = ctx->style.window.spacing.x) {
                    for (col = 0; col < 4; col++) {
                        int k = row * 4 + col;

                        if (k >= ICON_N) break;
                        if (reaktor_button(&(reaktor_button_spec){
                                .label = g_icons[k].label,
                                .icon  = g_icons[k].icon,
                                .box = { .flags = REAKTOR_LAY_FILL_X |
                                                  REAKTOR_LAY_FILL_Y } }))
                            s->presses++;
                    }
                }
            }

            REAKTOR_ROW(.h = 38.0f, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                for (i = 0; i < 3; i++)
                    if (reaktor_button(&(reaktor_button_spec){
                            .label = nav[i].label, .icon = nav[i].icon,
                            .box = { .flags = REAKTOR_LAY_FILL_X |
                                              REAKTOR_LAY_FILL_Y } }))
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
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } }))
                s->repeats++;
            reaktor_button(&(reaktor_button_spec){
                .label = "Disabled", .disabled = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
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
