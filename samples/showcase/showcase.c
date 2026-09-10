#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nk_common.h"
#include "ui.h"
#include "showcase.h"
#include "declare.h"
#include "anim.h"
#include "style.h"


#define ROW       30.0f
#define ROW_TALL  34.0f
#define ROW_SMALL 22.0f

const char *const reaktor_rss_names[RSS_STEPS] = {
    "before any of it", "SDL_Init(VIDEO)", "window and renderer",
    "window icon", "Nuklear context", "font atlas", "stylesheets"
};

const char *const reaktor_tab_names[TAB_COUNT] = {
    "Login", "Buttons", "Inputs", "Display", "Layout", "Popups", "Animation",
    "Styling", "Diagnostics"
};

static void
head_and_note(App *app, struct nk_context *ctx, const char *title,
              const char *note)
{
    (void)app;
    REAKTOR_COLUMN(.name = title,                    .gap = ctx->style.window.spacing.y) {
        reaktor_label(&(reaktor_label_spec){
            .text = title, .style = ".section-title",
            .colour = "--text-bright",
            .box = { .h = 30.0f, .flags = REAKTOR_LAY_FILL_X } });
        if (note)
            reaktor_label(&(reaktor_label_spec){
                .text = note, .style = ".section-note",
                .colour = "--text-muted", .wrap = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X } });
    }
    nk_layout_row_dynamic(ctx, 6.0f, 1);
    nk_spacer(ctx);
}

static void
rule(struct nk_context *ctx)
{
    nk_layout_row_dynamic(ctx, 14.0f, 1);
    nk_spacer(ctx);
    nk_layout_row_dynamic(ctx, 1.0f, 1);
    nk_rule_horizontal(ctx, reaktor_token("--background-hover",
                                          nk_rgba(128, 128, 128, 90)),
                       nk_false);
    nk_layout_row_dynamic(ctx, 6.0f, 1);
    nk_spacer(ctx);
}

static void
compact_push(struct nk_context *ctx)
{
    nk_style_push_vec2(ctx, &ctx->style.button.padding, nk_vec2(0.0f, 0.0f));
}

static void
compact_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
}

static void
api(App *app, struct nk_context *ctx, const char *text)
{
    nk_style_push_font(ctx, reaktor_font(app, 12, 0));
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, text, 0);
    nk_label_colored(ctx, text, NK_TEXT_LEFT,
                     reaktor_token("--links", ctx->style.text.color));
    nk_style_pop_font(ctx);
    nk_layout_row_dynamic(ctx, 4.0f, 1);
    nk_spacer(ctx);
}

static void
section(App *app, struct nk_context *ctx, const char *title, const char *note)
{
    rule(ctx);
    head_and_note(app, ctx, title, note);
}

static void
popup_style_push(struct nk_context *ctx)
{
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                             nk_style_item_color(
                                 reaktor_token("--background",
                                               nk_rgb(53, 53, 53))));
    nk_style_push_float(ctx, &ctx->style.window.rounding,
                        reaktor_popup_rounding());
    nk_style_push_vec2(ctx, &ctx->style.window.popup_padding,
                       nk_vec2(16.0f, 12.0f));
}

static void
popup_style_pop(struct nk_context *ctx)
{
    nk_style_pop_vec2(ctx);
    nk_style_pop_float(ctx);
    nk_style_pop_style_item(ctx);
}

static struct nk_rect
tree_header_bounds(struct nk_context *ctx)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    b.h = ctx->style.font->height + 2.0f * ctx->style.tab.padding.y;
    return b;
}

static void
tree_chevron(App *app, struct nk_context *ctx, struct nk_rect b, int open)
{
    float h = ctx->style.font->height;
    struct nk_rect s = nk_rect(b.x + ctx->style.tab.padding.x,
                               b.y + ctx->style.tab.padding.y, h, h);

    reaktor_chevron_at(app, ctx, s,
               open ? "chevron-down-outline" : "chevron-forward-outline",
               ctx->style.tab.text);
}

static void
draw_tooltip(App *app, struct nk_context *ctx, const char *const *lines,
             int n, float bar)
{
    const struct nk_user_font *f = reaktor_font(app, 13, 0);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    struct nk_color fill = reaktor_token("--background-hover",
                                         nk_rgb(69, 69, 69));
    struct nk_color ink  = reaktor_token("--text-main", nk_rgb(247, 247, 247));
    struct nk_color edge = reaktor_token("--text-muted",
                                         nk_rgb(192, 192, 192));
    struct nk_color acc  = reaktor_token("--links", nk_rgb(86, 199, 255));
    float pad = 10.0f, line = f->height + 6.0f;
    float w = 0.0f, h = 2.0f * pad + (float)n * line;
    struct nk_rect r;
    int i;

    for (i = 0; i < n; i++) {
        float lw = f->width(f->userdata, f->height, lines[i],
                            (int)strlen(lines[i]));
        if (lw > w) w = lw;
    }
    w += 2.0f * pad;
    if (bar >= 0.0f) h += 10.0f;

    r = nk_rect(ctx->input.mouse.pos.x + 14.0f,
                ctx->input.mouse.pos.y + 18.0f, w, h);

    nk_fill_rect(canvas, r, 6.0f, fill);
    nk_stroke_rect(canvas, nk_rect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f,
                                   r.h - 1.0f), 6.0f, 1.0f,
                   nk_rgba(edge.r, edge.g, edge.b, 140));

    for (i = 0; i < n; i++) {
        struct nk_rect t = nk_rect(r.x + pad, r.y + pad + (float)i * line,
                                   r.w - 2.0f * pad, line);
        nk_draw_text(canvas, t, lines[i], (int)strlen(lines[i]), f,
                     nk_rgba(0, 0, 0, 0), ink);
    }
    if (bar >= 0.0f) {
        struct nk_rect track = nk_rect(r.x + pad, r.y + h - pad - 6.0f,
                                       r.w - 2.0f * pad, 6.0f);
        nk_fill_rect(canvas, track, 3.0f,
                     reaktor_token("--background-body", nk_rgb(37, 37, 37)));
        track.w *= bar;
        nk_fill_rect(canvas, track, 3.0f, acc);
    }
}

static int
item_with_icon(App *app, struct nk_context *ctx, const char *label,
               const char *ionicon, int contextual)
{
    struct nk_rect b = nk_widget_bounds(ctx);
    float px = 16.0f;
    struct nk_rect r;
    struct nk_image im;
    int hit;

    reaktor_hot_top(app, b, 1, 1);
    reaktor_note(app, REAKTOR_A11Y_MENUITEM, label, NULL, 0, b);
    hit = contextual ? nk_contextual_item_label(ctx, label, NK_TEXT_LEFT)
                     : nk_menu_item_label(ctx, label, NK_TEXT_LEFT);

    r = nk_rect(b.x + b.w - px - 8.0f, b.y + (b.h - px) * 0.5f, px, px);
    im = reaktor_ionicon(app, ionicon, (int)px);
    nk_draw_image(nk_window_get_canvas(ctx), r, &im, nk_rgb(255, 255, 255));
    return hit;
}

static unsigned
hot(App *app, struct nk_context *ctx, unsigned char role, const char *name,
    unsigned state)
{
    struct nk_rect b = nk_widget_bounds(ctx);

    reaktor_hot(app, b, 1, 1);
    if (role == REAKTOR_A11Y_NONE) return 0;
    return reaktor_note(app, role, name, NULL, state, b);
}

#define CHECK_CELL 190.0f
#define CHECK_GAP   12.0f
static void
seed(showcase_state *s)
{
    int i;

    s->anim_ms    = 320;
    s->anim_curve = REAKTOR_EASE_CUBIC_OUT;
    s->anim_slot  = 0;

    SDL_strlcpy(s->name, "Ada Lovelace", sizeof(s->name));
    s->name_len = (int)strlen(s->name);
    SDL_strlcpy(s->digits, "1815", sizeof(s->digits));
    s->digits_len = (int)strlen(s->digits);
    SDL_strlcpy(s->hex, "56c7ff", sizeof(s->hex));
    s->hex_len = (int)strlen(s->hex);
    SDL_strlcpy(s->note,
                "A multiline editor: NK_EDIT_BOX.\n"
                "Tab moves focus on, as it does\n"
                "everywhere; the scrollbars appear\n"
                "when they are needed.\n"
                "\n"
                "Cut, copy and paste work because the\n"
                "shell pairs nk_input_begin with\n"
                "nk_input_end - without that Nuklear\n"
                "computes no key edges at all and\n"
                "Ctrl+V is silently dead.", sizeof(s->note));
    s->note_len = (int)strlen(s->note);

    s->slider_f = 0.65f;
    s->slider_i = 40;
    s->knob     = 0.3f;
    s->prop_i   = 12;
    s->prop_f   = 1.75f;
    s->prop_d   = 6.25;
    s->progress = 62;
    s->flags    = 1u | 4u;
    s->radio    = 1;
    s->check_wrap  = nk_true;
    s->sel_tile[1] = nk_true;
    s->list_sel = -1;
    s->tint.r = 0.34f; s->tint.g = 0.78f; s->tint.b = 1.0f; s->tint.a = 1.0f;
    SDL_strlcpy(s->menu_pick, "nothing yet", sizeof(s->menu_pick));
    SDL_strlcpy(s->file_pick, "nothing yet", sizeof(s->file_pick));

    for (i = 0; i < SC_SERIES_N; i++) {
        float t = (float)i / (float)(SC_SERIES_N - 1);
        s->series[i] = sinf(t * 6.2831853f) * 0.7f + sinf(t * 18.849556f) * 0.3f;
    }
    s->seeded = 1;
}

static const struct { const char *icon, *label; } g_icons[] = {
    { "add-outline",          "Add"      }, { "remove-outline",   "Remove"   },
    { "close-outline",        "Close"    }, { "search-outline",   "Search"   },
    { "settings-outline",     "Settings" }, { "save-outline",     "Save"     },
    { "trash-outline",        "Delete"   }, { "share-social-outline", "Share" },
    { "refresh-outline",      "Refresh"  }, { "download-outline", "Download" },
    { "star-outline",         "Star"     }, { "heart-outline",    "Favourite" }
};
#define ICON_N ((int)(sizeof(g_icons) / sizeof(g_icons[0])))

static void
page_buttons(App *app, struct nk_context *ctx, showcase_state *s)
{
    char line[96];
    int i;

    section(app, ctx, "Buttons",
            "The fill, border, radius, padding and font all come from "
            "tiny.css's `button` rule; the hover colour is its "
            "--button-hover. The accent is --links, which is the only thing "
            "in a classless stylesheet that means \"this is the one to "
            "press\".");
    api(app, ctx, "nk_button_label  /  nk_button_image_label");

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
            "rasterised at the size it is drawn - twice over, for the "
            "downscale. The stroke colour is substituted into the file "
            "before it is parsed, which is how an icon follows the "
            "stylesheet: CSS cannot reach inside an SVG. The label is the "
            "icon's name to a reader as well as to the eye.");
    api(app, ctx, "nk_button_image_label  /  reaktor_button_icon");

    {
        static const struct { const char *icon, *label; } nav[3] = {
            { "chevron-back-outline",    "Back" },
            { "chevron-forward-outline", "Forward" },
            { "menu-outline",            "Menu" }
        };
        int row, col;

        REAKTOR_COLUMN(                       .gap = ctx->style.window.spacing.y) {
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

    section(app, ctx, "Colour, image, repeat and disabled",
            "nk_button_color paints a swatch and nothing else. A repeater "
            "fires for as long as it is held rather than once on release. "
            "nk_widget_disable_begin swallows the click and multiplies "
            "every colour by a factor - which reads as \"greyed out\" on a "
            "dark palette and, since multiplying can only darken, as a "
            "*darker* button on a light one. Worth knowing before "
            "relying on it to mean unavailable.");
    api(app, ctx, "nk_button_color  /  nk_button_image  /  "
                  "nk_button_set_behavior  /  nk_widget_disable_begin");

    {
        REAKTOR_ROW(.h = 40.0f,
                    .gap = ctx->style.window.spacing.x) {
            compact_push(ctx);
            if (reaktor_swatch(&(reaktor_swatch_spec){
                    .name = "Tint swatch", .fill = nk_rgb_cf(s->tint),
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

    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "%d presses, %d repeat ticks",
                 s->presses, s->repeats);
    nk_style_push_font(ctx, reaktor_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);

    section(app, ctx, "Checkboxes and radios",
            "A checkbox writes a bool. nk_checkbox_flags_label writes one bit "
            "of an unsigned instead, which is how a set of independent "
            "options is held in a single value. Radios are a group only "
            "because the code makes them one: nk_option_label takes the "
            "answer to \"am I the active one\" and returns whether it was "
            "clicked. Both sit their box at the right of the cell, the way "
            "the Edit menu does; the flags form has no aligned variant, so "
            "this page wraps the aligned call in its own four lines.");
    api(app, ctx, "nk_checkbox_label_align  /  nk_option_label_align");

    {
        static const struct { const char *label; unsigned bit; } perms[3] = {
            { "read", 1u }, { "write", 2u }, { "execute", 4u }
        };

        REAKTOR_COLUMN(                       .gap = ctx->style.window.spacing.y) {
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

    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
    SDL_snprintf(line, sizeof(line), "flags = 0x%02x", s->flags);
    nk_style_push_font(ctx, reaktor_font(app, 13, 0));
    nk_label_colored(ctx, line, NK_TEXT_LEFT,
                     reaktor_token("--text-muted", ctx->style.text.color));
    nk_style_pop_font(ctx);

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
    api(app, ctx, "nk_selectable_label  /  nk_selectable_symbol_label  /  "
                  "nk_selectable_image_label");

    {
        REAKTOR_ROW(.h = 32.0f,
                    .gap = ctx->style.window.spacing.x) {
            for (i = 0; i < 4; i++) {
                char lab[16];

                SDL_snprintf(lab, sizeof(lab), "Tile %d", i + 1);
                reaktor_select(&(reaktor_select_spec){
                    .label = lab, .on = &s->sel_tile[i], .centred = 1,
                    .box = { .w = 140.0f, .flags = REAKTOR_LAY_FILL_Y } });
            }
        }
    }

    {
        REAKTOR_ROW(.h = 32.0f,
                    .gap = ctx->style.window.spacing.x) {
            reaktor_select(&(reaktor_select_spec){
                .label = "With a symbol", .on = &s->sel_row,
                .disc = 1, .centred = 1,
                .box = { .w = 190.0f, .flags = REAKTOR_LAY_FILL_Y } });
            reaktor_select(&(reaktor_select_spec){
                .label = "With an image", .on = &s->toggle,
                .icon = "star-outline", .centred = 1,
                .box = { .w = 190.0f, .flags = REAKTOR_LAY_FILL_Y } });
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
page_inputs(App *app, struct nk_context *ctx, showcase_state *s)
{
    static const char *const sizes[] = { "Compact", "Comfortable", "Spacious" };
    char line[96];

    section(app, ctx, "Text",
            "One rule - tiny.css's `input` - supplies the fill, the radius, "
            "the padding and the border - 2px of transparent until "
            "`input:focus` colours it. Only the focused field takes that "
            "colour; the rest get the neutral edge, because the rule assumes "
            "a field sits on the page and on the login card the field and the "
            "card are the same colour. A filter rejects a keystroke before it "
            "reaches the buffer, so the field cannot hold a value it would "
            "have to validate later.");
    api(app, ctx, "nk_edit_string with nk_filter_default / _decimal / _hex");

    {
        REAKTOR_COLUMN(                       .gap = ctx->style.window.spacing.y) {
            reaktor_field(&(reaktor_field_spec){
                .buf = s->name, .len = &s->name_len, .cap = SC_TEXT_CAP,
                .hint = "Full name", .filter = nk_filter_default,
                .box = { .h = 36.0f, .flags = REAKTOR_LAY_FILL_X } });

            REAKTOR_ROW(.h = 36.0f, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_field(&(reaktor_field_spec){
                    .buf = s->digits, .len = &s->digits_len,
                    .cap = SC_TEXT_CAP, .hint = "Digits only",
                    .filter = nk_filter_decimal,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
                reaktor_field(&(reaktor_field_spec){
                    .buf = s->hex, .len = &s->hex_len,
                    .cap = SC_TEXT_CAP, .hint = "Hex only",
                    .filter = nk_filter_hex,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
            }
        }
    }

    api(app, ctx, "nk_edit_string with NK_EDIT_BOX "
                  "(Nuklear breaks lines on newlines only - there is no wrap)");
    {
        REAKTOR_ROW(.h = 92.0f) {
            reaktor_field(&(reaktor_field_spec){
                .buf = s->note, .len = &s->note_len, .cap = SC_BOX_CAP,
                .name = "Notes", .multiline = 1,
                .filter = nk_filter_default,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
        }
    }

    section(app, ctx, "Ranges",
            "A slider steps a value between two bounds; a progress bar is the "
            "same shape but reports rather than accepts - unless it is made "
            "modifiable, which is the fourth argument. A knob is a slider "
            "wrapped around a circle, and is the one widget here with no "
            "counterpart in CSS at all.");
    api(app, ctx, "nk_slider_float  /  nk_slider_int  /  nk_progress  /  "
                  "nk_knob_float");

    {
        char fv[32], iv[32];

        SDL_snprintf(fv, sizeof(fv), "%.2f", (double)s->slider_f);
        SDL_snprintf(iv, sizeof(iv), "%d", s->slider_i);

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_slider(&(reaktor_slider_spec){
                    .name = "Float", .value = &s->slider_f,
                    .lo = 0.0f, .hi = 1.0f, .step = 0.01f,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
                reaktor_label(&(reaktor_label_spec){
                    .text = fv,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
            }
            REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_slider(&(reaktor_slider_spec){
                    .name = "Integer", .ivalue = &s->slider_i,
                    .lo = 0.0f, .hi = 100.0f, .step = 1.0f,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
                reaktor_label(&(reaktor_label_spec){
                    .text = iv,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
            }
            REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_progress(&(reaktor_progress_spec){
                    .name = "Progress", .value = &s->progress, .max = 100,
                    .modifiable = 1,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
                reaktor_label(&(reaktor_label_spec){
                    .text = "modifiable - drag it",
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
            }
            REAKTOR_ROW(.h = 62.0f, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_knob(&(reaktor_knob_spec){
                    .name = "Knob", .value = &s->knob, .lo = 0.0f, .hi = 1.0f,
                    .box = { .w = 62.0f, .h = 62.0f } });
            }
        }
    }

    section(app, ctx, "Properties",
            "A property is a labelled number that can be dragged, clicked "
            "through its two steppers, or typed into - the three ways a "
            "person expects to change a number, in one widget.");
    api(app, ctx, "nk_property_int  /  nk_property_float  /  "
                  "nk_property_double");

    {
        REAKTOR_ROW(.h = ROW,
                    .gap = ctx->style.window.spacing.x) {
            reaktor_property(&(reaktor_property_spec){
                .label = "Columns:", .ivalue = &s->prop_i,
                .lo = 1, .hi = 24, .step = 1, .grain = 0.25f,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
            reaktor_property(&(reaktor_property_spec){
                .label = "Stroke:", .fvalue = &s->prop_f,
                .lo = 0.25, .hi = 8.0, .step = 0.05, .grain = 0.01f,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
            reaktor_property(&(reaktor_property_spec){
                .label = "Ratio:", .dvalue = &s->prop_d,
                .lo = 0.0, .hi = 100.0, .step = 0.25, .grain = 0.05f,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
        }
    }

    section(app, ctx, "Combo boxes",
            "nk_combo is the whole widget in one call, for the common case of "
            "picking a string out of an array. The begin/end form opens an "
            "empty popup instead and lets any layout go inside it, which is "
            "how a combo grows a colour picker or a set of sliders.");
    api(app, ctx, "nk_combo  /  nk_combo_begin_label  /  "
                  "nk_combo_begin_symbol_label");

    popup_style_push(ctx);
    {
        int i;

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW_TALL, .gap = ctx->style.window.spacing.x) {
                REAKTOR_COMBO(.label = sizes[s->combo_size], .name = "Size",
                              .body_h = 130.0f,
                              .box = { .w = 440.0f,
                                       .flags = REAKTOR_LAY_FILL_Y }) {
                    nk_layout_row_dynamic(ctx, 26.0f, 1);
                    for (i = 0; i < 3; i++)
                        if (reaktor_combo_item(sizes[i], i == s->combo_size))
                            s->combo_size = i;
                }
                REAKTOR_COMBO(.label = sizes[s->combo_symbol],
                              .name = "Size, with a symbol",
                              .body_h = 130.0f, .disc = 1,
                              .box = { .w = 440.0f,
                                       .flags = REAKTOR_LAY_FILL_Y }) {
                    nk_layout_row_dynamic(ctx, 26.0f, 1);
                    for (i = 0; i < 3; i++)
                        if (reaktor_combo_item(sizes[i], i == s->combo_symbol))
                            s->combo_symbol = i;
                }
            }
            REAKTOR_ROW(.h = ROW_TALL, .gap = ctx->style.window.spacing.x) {
                struct nk_color tint = nk_rgb_cf(s->tint);

                REAKTOR_COMBO(.name = "Tint", .body_h = 150.0f,
                              .swatch = &tint,
                              .box = { .w = 440.0f,
                                       .flags = REAKTOR_LAY_FILL_Y }) {
                    nk_layout_row_dynamic(ctx, 26.0f, 1);
                    nk_property_float(ctx, "R:", 0.0f, &s->tint.r, 1.0f,
                                      0.01f, 0.005f);
                    nk_property_float(ctx, "G:", 0.0f, &s->tint.g, 1.0f,
                                      0.01f, 0.005f);
                    nk_property_float(ctx, "B:", 0.0f, &s->tint.b, 1.0f,
                                      0.01f, 0.005f);
                }
                REAKTOR_COMBO(.label = "Anything at all",
                              .name = "Anything at all", .body_h = 130.0f,
                              .box = { .w = 440.0f,
                                       .flags = REAKTOR_LAY_FILL_Y }) {
                    nk_layout_row_dynamic(ctx, 26.0f, 1);
                    nk_label(ctx, "A combo is just a popup", NK_TEXT_LEFT);
                    reaktor_slider_bar(app, ctx, 0, &s->slider_f, 0.0f, 1.0f,
                                       0.01f);
                    nk_checkbox_label(ctx, "with a layout in it",
                                      &s->check_spell);
                }
            }
        }
    }
    popup_style_pop(ctx);

    section(app, ctx, "Colour picker",
            "The one widget Nuklear draws as a continuous field rather than "
            "from the style: a saturation-value square with a hue bar. Its "
            "value is an nk_colorf, which is what the swatch and the combo "
            "above are reading.");
    api(app, ctx, "nk_color_pick");

    {
        struct nk_color c  = nk_rgb_cf(s->tint);

        SDL_snprintf(line, sizeof(line), "#%02x%02x%02x", c.r, c.g, c.b);

        REAKTOR_ROW(.h = 132.0f,
                    .gap = ctx->style.window.spacing.x) {
            reaktor_colour_pick(&(reaktor_colour_spec){
                .name = "Colour picker", .value = &s->tint,
                .box = { .w = 210.0f, .h = 132.0f } });

            REAKTOR_COLUMN(.w = 210.0f,
                           .gap = ctx->style.window.spacing.y) {
                reaktor_swatch(&(reaktor_swatch_spec){
                    .name = "Chosen colour", .fill = c,
                    .box = { .h = 44.0f, .flags = REAKTOR_LAY_FILL_X } });
                reaktor_label(&(reaktor_label_spec){
                    .text = line, .align = REAKTOR_CENTRE,
                    .box = { .h = 22.0f, .flags = REAKTOR_LAY_FILL_X } });
            }
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
grid_row(App *app, struct nk_context *ctx, const char *const *cells, int n,
         const float *weights, struct nk_color bg, struct nk_color fg,
         int header, float pad)
{
    int i;

    nk_style_push_font(ctx, reaktor_font(app, 13, header));
    nk_style_push_color(ctx, &ctx->style.text.color, fg);
    nk_style_push_vec2(ctx, &ctx->style.text.padding, nk_vec2(pad, 0.0f));

    REAKTOR_ROW(.h = 26.0f, .flags = REAKTOR_LAY_FILL_X) {
        struct nk_rect row;

        if (bg.a && reaktor_box_rect(&row))
            nk_fill_rect(nk_window_get_canvas(ctx), row, 0.0f, bg);

        for (i = 0; i < n; i++)
            reaktor_label(&(reaktor_label_spec){
                .text = cells[i],
                .box  = { .weight = weights[i],
                          .flags  = REAKTOR_LAY_FILL_X |
                                    REAKTOR_LAY_FILL_Y } });
    }

    nk_style_pop_vec2(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_font(ctx);
}

static void
section_grid(App *app, struct nk_context *ctx)
{
    static const char *const head[3] = { "Submodule", "Licence", "What it does" };
    static const char *const rows[7][3] = {
        { "SDL3",      "Zlib",   "window, input, renderer, main loop" },
        { "Nuklear",   "MIT/PD", "the widgets and their layout" },
        { "LCUI css",  "MIT",    "parses the stylesheet, computes values" },
        { "tiny.css",  "MIT",    "the stylesheet itself" },
        { "plutosvg",  "MIT",    "rasterises the icons" },
        { "Ionicons",  "MIT",    "the icons" },
        { "Aileron",   "CC0",    "the typeface" }
    };
    static const float weights[3] = { 0.24f, 0.16f, 0.60f };

    reaktor_style head_s, cell;
    struct nk_color odd, even, head_bg, head_fg;
    float pad;
    int i;

    section(app, ctx, "Tables",
            "The only widget on these pages that Nuklear does not have - and "
            "the only one whose every colour comes from a rule rather than a "
            "token, because a stylesheet does have tables. thead, tbody tr, "
            "its :nth-child(2n) sibling and the padding on th, td are all "
            "read straight out of tiny.css.");
    api(app, ctx, "REAKTOR_ROW with weighted cells + nk_fill_rect per row");

    reaktor_style_get("thead", &head_s);
    reaktor_style_get("th", &cell);

    odd     = reaktor_token("--table-bg", nk_rgb(53, 53, 53));
    even    = reaktor_token("--table-bg-alt", nk_rgb(37, 37, 37));

    {
        struct nk_color page = reaktor_token("--background-body", nk_rgb(37, 37, 37));
        struct nk_color accent = reaktor_token("--links", nk_rgb(0, 112, 224));
        struct nk_color want = head_s.matched && head_s.bg[3]
                             ? reaktor_col(head_s.bg)
                             : reaktor_token("--table", accent);

        head_bg = reaktor_visible(want, page, accent);
        head_bg = reaktor_visible(head_bg, odd, accent);
        head_fg = reaktor_on(head_bg);
    }
    pad     = cell.matched && cell.pad_x > 0.0f ? cell.pad_x : 8.0f;

    {
        REAKTOR_COLUMN() {
            grid_row(app, ctx, head, 3, weights, head_bg, head_fg, 1, pad);
            for (i = 0; i < 7; i++)
                grid_row(app, ctx, rows[i], 3, weights,
                         (i & 1) ? even : odd, ctx->style.text.color, 0, pad);
        }
    }
}

static void
page_display(App *app, struct nk_context *ctx, showcase_state *s)
{
    struct nk_list_view view;
    int i;

    section(app, ctx, "Text",
            "Alignment is a flag, not a layout: the label fills the row it "
            "was given and puts the text where the flag says. Wrapping is a "
            "separate call, because a wrapped label has to be given its "
            "height in advance - Nuklear cannot grow the row after the fact.");
    api(app, ctx, "nk_label  /  nk_label_colored  /  nk_label_wrap  /  nk_text");

    {
        static const char *const edges[3] = { "left", "centered", "right" };
        static const char *const tokens[3] = {
            "--text-main", "--text-muted", "--links"
        };
        int i;

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                for (i = 0; i < 3; i++)
                    reaktor_label(&(reaktor_label_spec){
                        .text = edges[i], .align = (unsigned char)i,
                        .box = { .flags = REAKTOR_LAY_FILL_X |
                                          REAKTOR_LAY_FILL_Y } });
            }
            REAKTOR_ROW(.h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                for (i = 0; i < 3; i++)
                    reaktor_label(&(reaktor_label_spec){
                        .text = tokens[i], .colour = tokens[i],
                        .align = (unsigned char)i,
                        .box = { .flags = REAKTOR_LAY_FILL_X |
                                          REAKTOR_LAY_FILL_Y } });
            }
            reaktor_label(&(reaktor_label_spec){
                .text = "nk_label_wrap breaks on words inside the row it was "
                        "given, which is why the paragraphs on these pages "
                        "measure their own height first.",
                .wrap = 1,
                .box = { .h = 40.0f, .flags = REAKTOR_LAY_FILL_X } });
        }
    }

    section(app, ctx, "Images",
            "Every icon here is an SVG read out of the Ionicons submodule and "
            "rasterised at the size it is drawn, twice over for the "
            "downscale. The stroke colour is substituted into the file before "
            "it is parsed, which is how an icon follows the stylesheet - CSS "
            "cannot reach inside an SVG. The calendar is drawn in --links "
            "and the rest in the text colour, from the same artwork - the "
            "colour is the caller's, not the file's.");
    api(app, ctx, "nk_image");

    {
        static const char *const names[8] = {
            "home-outline", "search-outline", "settings-outline",
            "heart-outline", "cloud-outline", "calendar-outline",
            "mail-outline", "lock-closed-outline"
        };
        int            k;

        static const int accent = 5;

        REAKTOR_ROW(.h = 30.0f,
                    .gap = ctx->style.window.spacing.x) {
            for (k = 0; k < 8; k++)
                reaktor_icon(&(reaktor_icon_spec){
                    .name   = names[k],
                    .accent = (unsigned char)(k == accent),
                    .box    = { .w = 30.0f, .h = 30.0f } });
        }
    }

    section(app, ctx, "Charts",
            "A chart is pushed one value at a time and reports back whether "
            "each was hovered or clicked, so the data never has to be copied "
            "anywhere first. Slots stack several series in one frame; nk_plot "
            "is the same thing for an array you already have.");
    api(app, ctx, "nk_chart_begin / nk_chart_push  /  nk_chart_add_slot  /  "
                  "nk_plot");

    nk_layout_row_dynamic(ctx, 100.0f, 1);
    if (nk_chart_begin(ctx, NK_CHART_LINES, SC_SERIES_N, -1.1f, 1.1f)) {
        for (i = 0; i < SC_SERIES_N; i++) nk_chart_push(ctx, s->series[i]);
        nk_chart_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 100.0f, 1);
    if (nk_chart_begin(ctx, NK_CHART_COLUMN, SC_SERIES_N, -1.1f, 1.1f)) {
        nk_chart_add_slot(ctx, NK_CHART_LINES, SC_SERIES_N, -1.1f, 1.1f);
        for (i = 0; i < SC_SERIES_N; i++) {
            nk_chart_push_slot(ctx, s->series[i], 0);
            nk_chart_push_slot(ctx, s->series[SC_SERIES_N - 1 - i], 1);
        }
        nk_chart_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 70.0f, 1);
    nk_plot(ctx, NK_CHART_LINES, s->series, SC_SERIES_N, 0);

    section(app, ctx, "Progress and rules",
            "A fixed progress bar reports; nk_rule_horizontal is the "
            "separator every page on this tab is divided by, and takes its "
            "colour as an argument rather than from the style.");
    api(app, ctx, "nk_progress with NK_FIXED  /  nk_rule_horizontal");

    {
        nk_size        fixed = s->progress;

        REAKTOR_ROW(.h = 20.0f)
            reaktor_progress(&(reaktor_progress_spec){
                .name  = "Loading",
                .value = &fixed,
                .max   = 100,
                .box   = { .h = 20.0f, .flags = REAKTOR_LAY_FILL_X } });
    }

    section_grid(app, ctx);

    section(app, ctx, "Trees and lists",
            "A tree remembers whether it is open between frames without the "
            "caller holding a bool, by hashing the source line it was written "
            "on. nk_tree_element makes the header itself selectable, drawn "
            "through nk_style_selectable - so an unselected one shows nothing "
            "but its label, and a selected one fills with the accent. A list "
            "view emits only the rows actually on screen, so its cost does "
            "not grow with the data.");
    api(app, ctx, "nk_tree_push  /  nk_tree_element_push  /  "
                  "nk_list_view_begin");

    {
        struct nk_rect hb = tree_header_bounds(ctx);
        int open;

        hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
        open = nk_tree_push(ctx, NK_TREE_TAB, "A tab-style tree", NK_MAXIMIZED);
        tree_chevron(app, ctx, hb, open);
        reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, "A tab-style tree", NULL,
                          open ? REAKTOR_A11Y_EXPANDED : 0u, hb);
        if (open) {
            nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
            nk_label(ctx, "Its children are indented under it.", NK_TEXT_LEFT);

            hb = tree_header_bounds(ctx);
            hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
            open = nk_tree_push(ctx, NK_TREE_NODE, "A node inside it",
                                NK_MINIMIZED);
            tree_chevron(app, ctx, hb, open);
            reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, "A node inside it",
                              NULL, open ? REAKTOR_A11Y_EXPANDED : 0u, hb);
            if (open) {
                nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
                nk_label(ctx, "Nesting is unlimited.", NK_TEXT_LEFT);
                nk_tree_pop(ctx);
            }
            reaktor_note_pop(app);

            for (i = 0; i < 3; i++) {
                char lab[32];
                SDL_snprintf(lab, sizeof(lab), "Selectable branch %d", i + 1);
                hb = tree_header_bounds(ctx);
                hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
                open = nk_tree_element_push(ctx, NK_TREE_NODE, lab,
                                            NK_MINIMIZED, &s->tree_leaf[i]);
                tree_chevron(app, ctx, hb, open);
                reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, lab, NULL,
                                  (open ? REAKTOR_A11Y_EXPANDED : 0u) |
                                  (s->tree_leaf[i] ? REAKTOR_A11Y_CHECKED
                                                   : 0u),
                                  hb);
                if (open) {
                    nk_layout_row_dynamic(ctx, ROW_SMALL, 1);
                    nk_label(ctx, "with a checkbox in the header",
                             NK_TEXT_LEFT);
                    nk_tree_pop(ctx);
                }
                reaktor_note_pop(app);
            }
            nk_tree_pop(ctx);
        }
        reaktor_note_pop(app);
    }

    nk_layout_row_dynamic(ctx, 10.0f, 1);
    nk_spacer(ctx);

    nk_layout_row_dynamic(ctx, 152.0f, 1);
    nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(0.0f, 0.0f));
    if (nk_list_view_begin(ctx, &view, "rows", NK_WINDOW_BORDER, 24,
                           SC_LIST_N)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        for (i = view.begin; i < view.end; i++) {
            char lab[40];
            nk_bool on = (s->list_sel == i);
            SDL_snprintf(lab, sizeof(lab), "Row %d of %d", i + 1, SC_LIST_N);
            hot(app, ctx, REAKTOR_A11Y_LISTITEM, lab,
                on ? REAKTOR_A11Y_SELECTED : 0u);
            if (nk_selectable_label(ctx, lab, NK_TEXT_LEFT, &on) && on)
                s->list_sel = i;
        }
        nk_list_view_end(&view);
    }
    nk_style_pop_vec2(ctx);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
page_layout(App *app, struct nk_context *ctx, showcase_state *s)
{
    int i;

    (void)s;

    section(app, ctx, "Rows and columns",
            "A row lays its children out left to right, a column top to "
            "bottom, and neither is told how many there are. A child that "
            "fills takes an equal share of what is left over; one that names "
            "a width keeps it. The gap is the space between children and "
            "belongs to the container, not to the boxes either side of it.");
    api(app, ctx, "REAKTOR_ROW  /  REAKTOR_COLUMN  /  .gap");

    {
        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X)
                reaktor_button(&(reaktor_button_spec){
                    .label = "one child, filling",
                    .box   = { .flags = REAKTOR_LAY_FILL_X } });

            REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x,
                        .flags = REAKTOR_LAY_FILL_X)
                for (i = 0; i < 3; i++)
                    reaktor_button(&(reaktor_button_spec){
                        .label = "an equal share",
                        .box   = { .flags = REAKTOR_LAY_FILL_X } });

            REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x,
                        .flags = REAKTOR_LAY_FILL_X)
                for (i = 0; i < 4; i++)
                    reaktor_button(&(reaktor_button_spec){
                        .label = "90px",
                        .box   = { .w = 90.0f } });
        }
    }

    section(app, ctx, "Wrapping",
            "A row that wraps starts a second line once the next child no "
            "longer fits, so a grid is a row and a loop rather than any "
            "arithmetic here. Resize the window and the same twelve boxes "
            "find a different number of columns.");
    api(app, ctx, "REAKTOR_LAY_WRAP");

    {
        REAKTOR_ROW(.gap = 8.0f, .flags = REAKTOR_LAY_WRAP) {
            for (i = 0; i < 12; i++) {
                char lab[16];
                SDL_snprintf(lab, sizeof(lab), "%d", i + 1);
                reaktor_button(&(reaktor_button_spec){
                    .label = lab,
                    .box   = { .w = 110.0f, .h = 46.0f } });
            }
        }
    }

    section(app, ctx, "Widths and shares",
            "A child either names a width in pixels or asks for a share of "
            "what is left. Shares are relative, not fractions - 2, 5 and 3 "
            "divide a row exactly as 0.2, 0.5 and 0.3 do - so a row stays "
            "correct when a child is added to it.");
    api(app, ctx, ".w = pixels  /  .weight = share");

    {
        static const float w[3]     = { 60.0f, 140.0f, 220.0f };
        static const float share[3] = { 2.0f, 5.0f, 3.0f };
        static const char *const share_lab[3] = { "20%", "50%", "30%" };

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x,
                        .flags = REAKTOR_LAY_FILL_X)
                for (i = 0; i < 3; i++) {
                    char lab[16];
                    SDL_snprintf(lab, sizeof(lab), "%d", (int)w[i]);
                    reaktor_button(&(reaktor_button_spec){
                        .label = lab, .box = { .w = w[i] } });
                }

            REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x,
                        .flags = REAKTOR_LAY_FILL_X)
                for (i = 0; i < 3; i++)
                    reaktor_button(&(reaktor_button_spec){
                        .label = share_lab[i],
                        .box   = { .weight = share[i],
                                   .flags  = REAKTOR_LAY_FILL_X } });
        }
    }

    section(app, ctx, "Floors",
            "The three are one rule, not three: a width a filling child names "
            "is a floor rather than a size. So a fixed column is a width on "
            "its own, a column that will not shrink past 80 but grows with "
            "the window is that same width with a fill, and a column that "
            "takes whatever is left is a fill with no width at all.");
    api(app, ctx, ".w alone  /  .w with FILL_X  /  FILL_X alone");

    {
        REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
            reaktor_button(&(reaktor_button_spec){
                .label = "fixed 80", .box = { .w = 80.0f } });
            reaktor_button(&(reaktor_button_spec){
                .label = "at least 80",
                .box   = { .w = 80.0f, .flags = REAKTOR_LAY_FILL_X } });
            reaktor_button(&(reaktor_button_spec){
                .label = "the rest",
                .box   = { .flags = REAKTOR_LAY_FILL_X } });
        }
    }

    section(app, ctx, "Free placement",
            "A free container lays nothing out: each child goes where its own "
            "margins put it, takes no notice of its siblings, and may overlap "
            "them. It is the escape hatch for what a row cannot say - the "
            "login card centred in the window on the first tab is one.");
    api(app, ctx, "REAKTOR_FREE  /  .ml, .mt");

    {
        REAKTOR_FREE(.h = 110.0f) {
            reaktor_button(&(reaktor_button_spec){
                .label = "0, 0",
                .box   = { .w = 130.0f, .h = 44.0f } });
            reaktor_button(&(reaktor_button_spec){
                .label = "60, 33",
                .box   = { .w = 130.0f, .h = 44.0f,
                           .ml = 60.0f, .mt = 33.0f } });
            reaktor_button(&(reaktor_button_spec){
                .label = "120, 66",
                .box   = { .w = 130.0f, .h = 44.0f,
                           .ml = 120.0f, .mt = 66.0f } });
            reaktor_button(&(reaktor_button_spec){
                .label = "and anywhere",
                .box   = { .w = 160.0f, .h = 86.0f,
                           .ml = 300.0f, .mt = 10.0f } });
        }
    }

    section(app, ctx, "Groups",
            "A group is a window inside a window: it clips, it scrolls, and "
            "it starts a fresh layout. It is Nuklear's, not this library's, "
            "and stays imperative for now - a declared tree belongs to one "
            "panel, and a group is another. The card on the login tab, this "
            "page's body and the titlebar are all groups.");
    api(app, ctx, "nk_group_begin  /  nk_group_begin_titled");

    nk_layout_row_dynamic(ctx, 150.0f, 2);
    if (nk_group_begin(ctx, "plain", NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        for (i = 0; i < 12; i++) {
            char lab[24];
            SDL_snprintf(lab, sizeof(lab), "scrolls: item %d", i + 1);
            nk_label(ctx, lab, NK_TEXT_LEFT);
        }
        nk_group_end(ctx);
    }
    if (nk_group_begin_titled(ctx, "titled", "With a title",
                              NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_label(ctx, "NK_WINDOW_TITLE", NK_TEXT_LEFT);
        nk_label(ctx, "draws the header", NK_TEXT_LEFT);
        nk_group_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
page_popups(App *app, struct nk_context *ctx, showcase_state *s)
{
    char line[SC_PATH_CAP + 32];
    struct nk_rect bar;
    int ok;

    popup_style_push(ctx);
    nk_layout_row_dynamic(ctx, 40.0f, 1);
    bar = nk_widget_bounds(ctx);
    nk_style_push_vec2(ctx, &ctx->style.window.group_padding,
                       nk_vec2(12.0f, 7.0f));
    ok = nk_group_begin(ctx, "menubar", NK_WINDOW_NO_SCROLLBAR);
    nk_style_pop_vec2(ctx);
    if (ok) {
    reaktor_note_push(app, REAKTOR_A11Y_MENUBAR, "Menu bar", NULL, 0, bar);
    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, 26.0f, 3);
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, REAKTOR_A11Y_MENU, "File", 0);
    if (nk_menu_begin_label(ctx, "File", NK_TEXT_LEFT,
                            nk_vec2(150.0f, reaktor_menu_height(3)))) {
        reaktor_menu_style_push(ctx);
        nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
        if (item_with_icon(app, ctx, "New", "add-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > New", sizeof(s->menu_pick));
        if (item_with_icon(app, ctx, "Open...", "folder-open-outline", 0)) {
            SDL_strlcpy(s->menu_pick, "File > Open", sizeof(s->menu_pick));
            if (reaktor_file_open(app))
                SDL_strlcpy(s->file_pick, "waiting for the picker...",
                            sizeof(s->file_pick));
        }
        if (item_with_icon(app, ctx, "Close", "close-outline", 0))
            SDL_strlcpy(s->menu_pick, "File > Close", sizeof(s->menu_pick));
        reaktor_menu_style_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, REAKTOR_A11Y_MENU, "Edit", 0);
    if (nk_menu_begin_label(ctx, "Edit", NK_TEXT_LEFT,
                            nk_vec2(190.0f, reaktor_menu_height(4)))) {
        reaktor_menu_style_push(ctx);
        nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
        if (reaktor_menu_item(app, ctx, "Cut", NULL, 0))
            SDL_strlcpy(s->menu_pick, "Edit > Cut", sizeof(s->menu_pick));
        if (reaktor_menu_item(app, ctx, "Copy", NULL, 0))
            SDL_strlcpy(s->menu_pick, "Edit > Copy", sizeof(s->menu_pick));
        nk_layout_row_template_begin(ctx, REAKTOR_MENU_ROW);
        nk_layout_row_template_push_static(ctx, 5.0f);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 6.0f);
        nk_layout_row_template_end(ctx);
        nk_spacer(ctx);
        nk_checkbox_label_align(ctx, "Overwrite", &s->check_spell,
                                NK_WIDGET_RIGHT, NK_TEXT_LEFT);
        nk_spacer(ctx);
        nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
        reaktor_slider_bar(app, ctx, 0, &s->slider_f, 0.0f, 1.0f, 0.01f);
        reaktor_menu_style_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60.0f);
    hot(app, ctx, REAKTOR_A11Y_MENU, "View", 0);
    if (nk_menu_begin_label(ctx, "View", NK_TEXT_LEFT,
                            nk_vec2(170.0f, reaktor_menu_height(2)))) {
        reaktor_menu_style_push(ctx);
        nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
        reaktor_progress_bar(app, ctx, &s->progress, 100, NK_MODIFIABLE);
        if (reaktor_menu_item(app, ctx, "Reset", NULL, 0))
            s->progress = 50;
        reaktor_menu_style_pop(ctx);
        nk_menu_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
    reaktor_note_pop(app);
    nk_group_end(ctx);
    }
    nk_stroke_rect(nk_window_get_canvas(ctx),
                   nk_rect(bar.x + 0.5f, bar.y + 0.5f, bar.w - 1.0f, bar.h - 1.0f),
                   ctx->style.window.rounding, 1.0f,
                   ctx->style.window.border_color);
    popup_style_pop(ctx);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
    head_and_note(app, ctx, "Menus",
            "The bar above. A menu is a popup anchored to the item that "
            "opened it, and its items are ordinary widgets - the View menu "
            "has a draggable progress bar in it. An item's glyph and the "
            "Edit menu's checkbox both sit at the right edge: the checkbox "
            "through nk_checkbox_label_align, the glyph painted there, "
            "because nk_menu_item_symbol_label centres its label whatever "
            "alignment it is given.");
    api(app, ctx, "nk_menubar_begin  /  nk_menu_begin_label  /  "
                  "nk_menu_item_label  /  nk_checkbox_label_align");

    {
        nk_style_push_font(ctx, reaktor_font(app, 13, 0));
        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            SDL_snprintf(line, sizeof(line), "last chosen: %s", s->menu_pick);
            reaktor_label(&(reaktor_label_spec){
                .text   = line,
                .name   = "Last chosen",
                .value  = s->menu_pick,
                .colour = "--text-muted",
                .box    = { .h = ROW_SMALL,
                            .flags = REAKTOR_LAY_FILL_X } });

            SDL_snprintf(line, sizeof(line), "file picker: %s", s->file_pick);
            reaktor_label(&(reaktor_label_spec){
                .text   = line,
                .name   = "File picker",
                .value  = s->file_pick,
                .colour = "--text-muted",
                .box    = { .h = ROW_SMALL,
                            .flags = REAKTOR_LAY_FILL_X } });
        }
        nk_style_pop_font(ctx);
    }

    section(app, ctx, "Context menu",
            "The same popup, opened by a right-click inside a rect the caller "
            "names. The login field on the first tab uses one for cut, copy "
            "and paste.");
    api(app, ctx, "nk_contextual_begin");

    popup_style_push(ctx);
    {
        struct nk_rect trigger = nk_rect(0, 0, 0, 0);

        REAKTOR_ROW(.h = 54.0f) {
            reaktor_box_rect(&trigger);
            reaktor_button(&(reaktor_button_spec){
                .label = "Right-click anywhere on this button",
                .box   = { .flags = REAKTOR_LAY_FILL_X |
                                    REAKTOR_LAY_FILL_Y } });
        }
        if (nk_contextual_begin(ctx, NK_WINDOW_BORDER, nk_vec2(150.0f, reaktor_menu_height(3)),
                                trigger)) {
            reaktor_menu_style_push(ctx);
            nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
            if (item_with_icon(app, ctx, "First", "star-outline", 1))
                SDL_strlcpy(s->menu_pick, "context: First",
                            sizeof(s->menu_pick));
            if (item_with_icon(app, ctx, "Second", "bookmark-outline", 1))
                SDL_strlcpy(s->menu_pick, "context: Second",
                            sizeof(s->menu_pick));
            if (item_with_icon(app, ctx, "Third", "refresh-outline",
                               1))
                SDL_strlcpy(s->menu_pick, "context: Third",
                            sizeof(s->menu_pick));
            reaktor_menu_style_pop(ctx);
            nk_contextual_end(ctx);
        }
    }
    popup_style_pop(ctx);

    section(app, ctx, "Tooltips",
            "Positioned against the pointer rather than against a widget, "
            "and emitted only while the thing it describes is hovered - so it "
            "costs nothing on the frames where it is not showing. Drawn here "
            "rather than with nk_tooltip: that opens a NK_POPUP_DYNAMIC "
            "panel, which does not fill its body when it begins and strokes "
            "its border at bounds sized before the content shrank them, "
            "leaving a frame standing off the box that no style value "
            "moves.");
    api(app, ctx, "drawn onto the canvas - see draw_tooltip");

    popup_style_push(ctx);
    {
        static const char *const one[1] = { "A one-line tooltip." };
        static const char *const many[2] = {
            "A tooltip is not limited to a line:",
            "this one carries a progress bar."
        };
        struct nk_rect b;

        REAKTOR_ROW(.h = 34.0f,
                    .gap = ctx->style.window.spacing.x) {
            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y) {
                b = nk_rect(0, 0, 0, 0);
                reaktor_box_rect(&b);
                reaktor_hot_follow(app, b, 1);
                reaktor_button(&(reaktor_button_spec){
                    .label = "Hover for a plain tooltip",
                    .box   = { .flags = REAKTOR_LAY_FILL_X |
                                        REAKTOR_LAY_FILL_Y } });
                if (nk_input_is_mouse_hovering_rect(&ctx->input, b))
                    draw_tooltip(app, ctx, one, 1, -1.0f);
            }

            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y) {
                b = nk_rect(0, 0, 0, 0);
                reaktor_box_rect(&b);
                reaktor_hot_follow(app, b, 1);
                reaktor_button(&(reaktor_button_spec){
                    .label = "Hover for a laid-out one",
                    .box   = { .flags = REAKTOR_LAY_FILL_X |
                                        REAKTOR_LAY_FILL_Y } });
                if (nk_input_is_mouse_hovering_rect(&ctx->input, b))
                    draw_tooltip(app, ctx, many, 2,
                                 (float)s->progress / 100.0f);
            }
        }
    }
    popup_style_pop(ctx);

    section(app, ctx, "Popups",
            "A static popup is a fixed rect that stays until it is closed - a "
            "dialog. A dynamic one is sized by its contents. Both hold the "
            "input while they are open, which is what makes the first modal "
            "without any modality machinery.");
    api(app, ctx, "nk_popup_begin(NK_POPUP_STATIC)  /  nk_popup_close");

    {
        REAKTOR_ROW(.h = 34.0f, .gap = 14.0f) {
            if (reaktor_button(&(reaktor_button_spec){
                    .label = "Open a dialog",
                    .box   = { .w = 240.0f,
                               .flags = REAKTOR_LAY_FILL_Y } }))
                s->popup_open = 1;

            reaktor_label(&(reaktor_label_spec){
                .text = s->popup_open ? "open" : "closed",
                .name = "Dialog",
                .box  = { .flags = REAKTOR_LAY_FILL_X |
                                   REAKTOR_LAY_CENTER_Y } });
        }
    }

    popup_style_push(ctx);
    if (s->popup_open) {
        struct nk_vec2 vis = nk_window_get_content_region_size(ctx);
        float pw = 360.0f;
        float ph = (22.0f + 8.0f + 44.0f + 12.0f + 36.0f)
                 + 4.0f * 2.0f
                 + 2.0f * 12.0f;
        struct nk_rect at = nk_rect((vis.x - pw) * 0.5f, (vis.y - ph) * 0.5f,
                                    pw, ph);

        if (nk_popup_begin(ctx, NK_POPUP_STATIC, "Confirm",
                           NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, at)) {
            reaktor_menu_style_push(ctx);
            nk_layout_row_dynamic(ctx, 22.0f, 1);
            nk_style_push_font(ctx, reaktor_font(app, 16, 1));
            nk_label_colored(ctx, "Close without saving?", NK_TEXT_LEFT,
                             reaktor_token("--text-bright",
                                           ctx->style.text.color));
            nk_style_pop_font(ctx);

            nk_layout_row_dynamic(ctx, 8.0f, 1);
            nk_spacer(ctx);

            nk_layout_row_dynamic(ctx, 44.0f, 1);
            nk_style_push_font(ctx, reaktor_font(app, 13, 0));
            nk_label_colored_wrap(ctx,
                "Everything behind this is inert until the popup is closed - "
                "which is what makes it modal, with no modality machinery.",
                reaktor_token("--text-muted", ctx->style.text.color));
            nk_style_pop_font(ctx);

            nk_layout_row_dynamic(ctx, 12.0f, 1);
            nk_spacer(ctx);

            nk_layout_row_template_begin(ctx, 36.0f);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 96.0f);
            nk_layout_row_template_push_static(ctx, 96.0f);
            nk_layout_row_template_end(ctx);
            nk_spacer(ctx);
            if (reaktor_button_label(app, ctx, "Cancel")) {
                s->popup_open = 0;
                nk_popup_close(ctx);
            }
            if (reaktor_button_accent(app, ctx, "OK")) {
                s->popup_open = 0;
                nk_popup_close(ctx);
            }
            reaktor_menu_style_pop(ctx);
            nk_popup_end(ctx);
        } else {
            s->popup_open = 0;
        }
    }
    popup_style_pop(ctx);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
ease_plot(App *app, struct nk_context *ctx, struct nk_rect slot,
          unsigned char curve, float head, int strong)
{
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_color line = reaktor_token(strong ? "--links" : "--text-muted",
                                         nk_rgb(0, 112, 224));
    struct nk_color grid = reaktor_token("--background-hover",
                                         nk_rgba(128, 128, 128, 90));
    const float over = 0.28f;
    const float span = 1.0f + 2.0f * over;
    const float pad  = 4.0f;
    float w = slot.w - 2.0f * pad, h = slot.h - 2.0f * pad;
    float px = 0.0f, py = 0.0f;
    int   i;

    {
        float y0 = slot.y + pad + h * (over + 1.0f) / span;
        float y1 = slot.y + pad + h * over / span;
        nk_stroke_line(cv, slot.x + pad, y0, slot.x + pad + w, y0, 1.0f, grid);
        nk_stroke_line(cv, slot.x + pad, y1, slot.x + pad + w, y1, 1.0f, grid);
    }

    for (i = 0; i <= 24; i++) {
        float t = (float)i / 24.0f;
        float x = slot.x + pad + t * w;
        float y = slot.y + pad
                + h * (over + 1.0f - reaktor_ease_at(curve, t)) / span;

        if (i) nk_stroke_line(cv, px, py, x, y, strong ? 1.8f : 1.2f, line);
        px = x;
        py = y;
    }

    if (head >= 0.0f && head <= 1.0f) {
        float x = slot.x + pad + head * w;
        float y = slot.y + pad
                + h * (over + 1.0f - reaktor_ease_at(curve, head)) / span;
        reaktor_fill_round(app, cv, nk_rect(x - 3.5f, y - 3.5f, 7.0f, 7.0f),
                           3.5f,
                           reaktor_token("--text-bright",
                                         nk_rgb(255, 255, 255)));
    }
}

static void
track_at(App *app, struct nk_context *ctx, struct nk_rect r, float where,
         int lit)
{
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    const float box = 40.0f, inset = 5.0f;

    reaktor_fill_round(app, cv, r, 6.0f,
                       reaktor_token("--background-alt", nk_rgb(48, 48, 48)));
    reaktor_fill_round(app, cv,
                       nk_rect(r.x + inset
                               + where * (r.w - box - 2.0f * inset),
                               r.y + inset, box, r.h - 2.0f * inset),
                       5.0f,
                       lit ? reaktor_token("--links", nk_rgb(0, 112, 224))
                           : reaktor_token("--text-muted",
                                           nk_rgb(140, 140, 140)));
}

static void
page_animation(App *app, struct nk_context *ctx, showcase_state *s)
{
    static const float stop[3] = { 0.0f, 0.5f, 1.0f };
    static const char *const stop_name[3] = { "Left", "Centre", "Right" };
    unsigned char curve = (unsigned char)s->anim_curve;
    float ms = (float)s->anim_ms;
    int   i;

    section(app, ctx, "The same change, twice",
            "Both blocks are told to go to the same place by the same button. "
            "The top one is simply put there, which is what a frame does "
            "without help. The bottom one is given the time to travel, and "
            "the difference is the whole subject of this page: the eye "
            "follows a thing that moves and has to re-find a thing that "
            "jumps. Press one stop and then another before it arrives - it "
            "redirects from where it is rather than starting again.");
    api(app, ctx, "reaktor_animate(id, channel, to, ms, curve)");

    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        for (i = 0; i < 3; i++)
            if (reaktor_button(&(reaktor_button_spec){
                    .label = stop_name[i],
                    .accent = (unsigned char)(s->anim_slot == i),
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } }))
                s->anim_slot = i;
    }

    REAKTOR_COLUMN(.gap = 6.0f) {
        REAKTOR_ROW(.h = 44.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
            reaktor_label(&(reaktor_label_spec){
                .text = "instant", .colour = "--text-muted",
                .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y) {
                struct nk_rect r;
                if (reaktor_box_rect(&r))
                    track_at(app, ctx, r, stop[s->anim_slot], 0);
            }
        }

        REAKTOR_ROW(.h = 44.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
            reaktor_label(&(reaktor_label_spec){
                .text = "eased", .colour = "--text-bright",
                .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y) {
                unsigned id = reaktor_box_id();
                float where = reaktor_animate(id, 0, stop[s->anim_slot],
                                              ms, curve);
                struct nk_rect r;

                if (reaktor_box_rect(&r))
                    track_at(app, ctx, r, where, 1);
            }
        }
    }

    section(app, ctx, "Pick a curve by looking at it",
            "Thirty-one of them, drawn from the same function that runs them. "
            "A curve is a shape, and a list of names is a poor way to choose "
            "one - so the shapes are the control. In accelerates away from "
            "rest, out arrives calm, in-out does both: for something "
            "appearing, out is almost always right, and a page that eases "
            "everything in-out reads as sluggish because the slow middle is "
            "where the eye is. The last three leave the 0 and 1 lines on "
            "purpose. Pick one and press Play: the dot travels the cell you "
            "chose, which is the value arriving.");
    api(app, ctx, "reaktor_ease_at(curve, t)  /  REAKTOR_EASE_*");

    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x,
                .flags = REAKTOR_LAY_PACK_CENTER) {
        unsigned id = reaktor_box_id();

        reaktor_animate(id, 0, s->anim_play ? 1.0f : 0.0f, ms, curve);
        s->anim_head = reaktor_anim_progress(id, 0);

        if (reaktor_button(&(reaktor_button_spec){
                .label  = "Play",
                .name   = "Run the selected curve",
                .box = { .w = 96.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->anim_play = !s->anim_play;

        REAKTOR_COMBO(.label = reaktor_ease_name(curve), .name = "Curve",
                      .body_h = 300.0f,
                      .box = { .w = 190.0f,
                               .flags = REAKTOR_LAY_FILL_Y }) {
            nk_layout_row_dynamic(ctx, 26.0f, 1);
            for (i = 0; i < REAKTOR_EASE_COUNT; i++)
                if (reaktor_combo_item(reaktor_ease_name((unsigned char)i),
                                       i == s->anim_curve))
                    s->anim_curve = i;
        }

        reaktor_property(&(reaktor_property_spec){
            .label = "ms:", .name = "Duration",
            .ivalue = &s->anim_ms, .lo = 60, .hi = 3000, .step = 20,
            .grain = 4.0f,
            .box = { .w = 200.0f, .flags = REAKTOR_LAY_FILL_Y } });
    }

    REAKTOR_ROW(.gap = 6.0f, .flags = REAKTOR_LAY_WRAP) {
        for (i = 0; i < REAKTOR_EASE_COUNT; i++) {
            REAKTOR_ROW(.w = 104.0f, .h = 52.0f) {
                struct nk_rect r;
                int on = (i == s->anim_curve);

                if (reaktor_swatch(&(reaktor_swatch_spec){
                        .name = reaktor_ease_name((unsigned char)i),
                        .fill = reaktor_token(on ? "--background-hover"
                                                 : "--background-alt",
                                              nk_rgb(48, 48, 48)),
                        .box = { .flags = REAKTOR_LAY_FILL_X |
                                          REAKTOR_LAY_FILL_Y } }))
                    s->anim_curve = i;

                if (reaktor_box_rect(&r))
                    ease_plot(app, ctx, r, (unsigned char)i,
                              on ? s->anim_head : -1.0f, on);
            }
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
rule_says(const char *selector)
{
    reaktor_style st;
    char line[140];

    reaktor_style_get(selector, &st);
    if (st.matched)
        SDL_snprintf(line, sizeof(line),
                     "%s is #%02x%02x%02x on #%02x%02x%02x, %.0fpx corners, "
                     "%.0fpx border, %.1f/%.1f padding, %dpx type",
                     selector, st.fg[0], st.fg[1], st.fg[2],
                     st.bg[0], st.bg[1], st.bg[2],
                     (double)st.rounding, (double)st.border,
                     (double)st.pad_x, (double)st.pad_y, st.font_px);
    else
        SDL_snprintf(line, sizeof(line), "%s - no sheet has a rule for it",
                     selector);

    reaktor_label(&(reaktor_label_spec){
        .text = line, .name = selector,
        .value = st.matched ? line + strlen(selector) + 4 : "no rule",
        .colour = st.matched ? "--text-main" : "--text-muted",
        .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X } });
}

static void
page_styling(App *app, struct nk_context *ctx, showcase_state *s)
{
    nk_bool on = (nk_bool)reaktor_css_override_on(app);

    section(app, ctx, "Change the stylesheet, change the application",
            "Nothing on this page is about Reaktor. The switch below stops "
            "reading one CSS file and starts reading another, and everything "
            "in the window follows - the buttons, the fields, the borders, "
            "the corners. That is the only claim this library makes about "
            "styling, and it is either true in front of you or it is not.");
    api(app, ctx, "third_party/simplecss  /  REAKTOR_CSS=<path>");

    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        if (reaktor_check(&(reaktor_check_spec){
                .label = "Read simple.css over the top of tiny.css",
                .name  = "Override stylesheet",
                .on    = &on,
                .box   = { .w = 380.0f, .flags = REAKTOR_LAY_CENTER_Y } }))
            reaktor_css_override(app, on);
        reaktor_soak();
    }

    section(app, ctx, "Watch these while you do it",
            "Ordinary controls, drawn the way every other page draws them. "
            "They are here because a change you have to be told about is not "
            "a change you can see.");
    api(app, ctx, "the same widgets as everywhere else");

    REAKTOR_COLUMN(.gap = 10.0f) {
        REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x,
                    .flags = REAKTOR_LAY_FILL_X) {
            reaktor_button(&(reaktor_button_spec){
                .label = "A button",
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
            reaktor_button(&(reaktor_button_spec){
                .label = "An accented one", .accent = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
            reaktor_button(&(reaktor_button_spec){
                .label = "A disabled one", .disabled = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
        }

        REAKTOR_ROW(.h = ROW_TALL, .gap = ctx->style.window.spacing.x,
                    .flags = REAKTOR_LAY_FILL_X) {
            reaktor_field(&(reaktor_field_spec){
                .name = "Name", .hint = "a text field",
                .buf = s->name, .len = &s->name_len, .cap = SC_TEXT_CAP,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_FILL_Y } });
            REAKTOR_COMBO(.label = "a combo box", .name = "Combo",
                          .body_h = 110.0f,
                          .box = { .w = 260.0f,
                                   .flags = REAKTOR_LAY_FILL_Y }) {
                nk_layout_row_dynamic(ctx, 26.0f, 1);
                reaktor_combo_item("one", 1);
                reaktor_combo_item("two", 0);
            }
        }

        REAKTOR_ROW(.h = 30.0f, .gap = 20.0f, .flags = REAKTOR_LAY_FILL_X) {
            reaktor_slider(&(reaktor_slider_spec){
                .name = "Slider", .value = &s->slider_f,
                .lo = 0.0f, .hi = 1.0f, .step = 0.01f,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_CENTER_Y } });
            reaktor_progress(&(reaktor_progress_spec){
                .name = "Progress", .value = &s->progress, .max = 100,
                .modifiable = 1,
                .box = { .w = 300.0f, .h = 20.0f,
                         .flags = REAKTOR_LAY_CENTER_Y } });
        }
    }

    section(app, ctx, "What the rules say right now",
            "Read back through the same call the widgets are painted from, so "
            "these cannot say one thing while the screen shows another. Flip "
            "the switch and they change with it.");
    api(app, ctx, "reaktor_style_get(selector, &out)");

    REAKTOR_COLUMN(.gap = 2.0f) {
        rule_says("button");
        rule_says("input");
        rule_says("select");
        rule_says("a");
    }

    section(app, ctx, "Three sheets, read in order",
            "The palette, then tiny.css, then this application's own, then "
            "the override if there is one. There is no merging step and no "
            "precedence table - CSS resolves a tie by document order, so the "
            "last sheet read wins, and reading one last is the whole "
            "mechanism. A selector the override says nothing about still "
            "resolves to tiny.css's rule; one neither mentions leaves the "
            "widget on Nuklear's own default.");
    api(app, ctx, "reaktor_style_init(sheets, n, theme)");

    REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
        reaktor_label(&(reaktor_label_spec){
            .text = "Two controls here have no rule in either sheet - a "
                    "slider and a progress bar are not things a document "
                    "has - so they borrow: the rail from input, the fill "
                    "from a. Every sheet has a link colour, which is why "
                    "that is read from the rule rather than from a --links "
                    "token some sheets will not have.",
            .name = "Controls with no rule", .colour = "--text-muted",
            .wrap = 1, .box = { .flags = REAKTOR_LAY_FILL_X } });
        reaktor_label(&(reaktor_label_spec){
            .text = "simple.css keeps its dark palette in "
                    "@media (prefers-color-scheme: dark), which this tree "
                    "used to drop along with every other at-rule - so a "
                    "sheet written that way loaded light-only. That branch "
                    "is now unwrapped for whichever scheme is active. Width "
                    "queries and print are still dropped: a viewport query "
                    "means nothing to a window that is not a document.",
            .name = "Media queries", .colour = "--text-muted", .wrap = 1,
            .box = { .flags = REAKTOR_LAY_FILL_X } });
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static void
diag_row(App *app, struct nk_context *ctx, const char *name, const char *value)
{
    REAKTOR_ROW(.h = 24.0f, .gap = ctx->style.window.spacing.x,
                .flags = REAKTOR_LAY_FILL_X) {
        struct nk_rect r;
        unsigned       id;

        id = reaktor_note(app, REAKTOR_A11Y_LABEL, name, value,
                          REAKTOR_A11Y_VOLATILE, nk_rect(0, 0, 0, 0));
        if (reaktor_box_rect(&r)) reaktor_note_bounds(app, id, r);

        nk_style_push_font(ctx, reaktor_font(app, 13, 0));
        reaktor_label(&(reaktor_label_spec){
            .text   = name,
            .name   = name,
            .colour = "--text-muted",
            .silent = 1,
            .box    = { .w = 190.0f, .flags = REAKTOR_LAY_FILL_Y } });
        nk_style_pop_font(ctx);

        reaktor_label(&(reaktor_label_spec){
            .text   = value,
            .name   = "value",
            .silent = 1,
            .box    = { .flags = REAKTOR_LAY_FILL_X |
                                 REAKTOR_LAY_FILL_Y } });
    }
}

#define DIAG_ROWS(ctx)                                                       \
    REAKTOR_COLUMN(.w = nk_widget_bounds(ctx).w,                             \
                   .gap = (ctx)->style.window.spacing.y)

static void
page_diagnostics(App *app, struct nk_context *ctx, showcase_state *st)
{
    reaktor_diag d;
    char v[192];

    (void)st;
    reaktor_diagnostics(app, &d);

    section(app, ctx, "Rendering",
            "Which backend SDL settled on, and what the app asked for. "
            "REAKTOR_RENDERER picks between auto, gpu and software; "
            "REAKTOR_VSYNC and REAKTOR_AA turn the other two off. "
            "Anti-aliasing reads what the frame is drawn with: the software "
            "rasteriser has no partial coverage, so it feathers strokes, "
            "which grades a curve, and not fills, which would only draw a "
            "hairline. REAKTOR_SW_NOAA=1 drops both.");

    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "backend", d.renderer);
        SDL_snprintf(v, sizeof(v), "%s", d.mode);
        diag_row(app, ctx, "requested", v);
        diag_row(app, ctx, "vsync", d.vsync ? "on" : "off");
        diag_row(app, ctx, "anti-aliasing", d.aa);
    }

    section(app, ctx, "Pacing",
            "How often SDL calls the app back. At rest that is \"waitevent\" - "
            "nothing is drawn until something happens - and for the duration "
            "of a drag it becomes the display's refresh, so the frames come "
            "at an even cadence rather than one per mouse event. This page "
            "forces no frames of its own, so the readings below hold still "
            "while nothing is happening - which is the measurement. They come "
            "from the gap between the last two frames rather than from a "
            "count over a second, because a second only advances when a frame "
            "is drawn: counted that way the figure would read stale for as "
            "long as the app was quiet. Move the pointer across the page.");

    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "at rest", d.frame_rate);
        diag_row(app, ctx, "while dragging", d.drag_rate);
        if (d.frame_gap_ms >= 1000.0f)
            SDL_snprintf(v, sizeof(v), "%.2f s since the last one",
                         (double)d.frame_gap_ms / 1000.0);
        else if (d.frame_gap_ms > 0.0f)
            SDL_snprintf(v, sizeof(v), "%.0f ms since the last one",
                         (double)d.frame_gap_ms);
        else SDL_strlcpy(v, "the first", sizeof(v));
        diag_row(app, ctx, "this frame", v);
        if (d.cpu_ms_per_frame > 0.0f)
            SDL_snprintf(v, sizeof(v), "%.1f ms of CPU, all threads",
                         (double)d.cpu_ms_per_frame);
        else SDL_strlcpy(v, "not yet measured", sizeof(v));
        diag_row(app, ctx, "a drawn frame costs", v);
        SDL_snprintf(v, sizeof(v), "one per %d ms while the pointer moves",
                     d.hover_gap_ms);
        diag_row(app, ctx, "hover redraws", v);
    }

    section(app, ctx, "Where a frame goes",
            "Split three ways because guessing which one dominates has been "
            "wrong more than once. Building the UI and converting it to "
            "geometry are both fractions of a millisecond; the wait on "
            "present is the whole frame.");

    SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.build_ms);
    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "build", v);
        SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.render_ms);
        diag_row(app, ctx, "render", v);
        SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.present_ms);
        diag_row(app, ctx, "present", v);
    }

    section(app, ctx, "Style and text",
            "The stylesheets are parsed by LCUI's libcss on every theme "
            "change, and the font atlas is baked per size at the display's "
            "scale.");

    SDL_snprintf(v, sizeof(v), "%s", d.dark ? "dark" : "light");
    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "scheme", v);
        SDL_snprintf(v, sizeof(v), "%d, parsed in %.2f ms", d.sheets,
                     (double)d.style_ms);
        diag_row(app, ctx, "stylesheets", v);
        SDL_snprintf(v, sizeof(v), "%.2fx", (double)d.scale);
        diag_row(app, ctx, "display scale", v);
        diag_row(app, ctx, "font", d.font);
    }

    section(app, ctx, "Memory",
            "Where the process's memory has gone. Nuklear's command buffer "
            "grows to fit the busiest frame it has been asked to draw and is "
            "never handed back, so it records the high-water mark rather than "
            "the current page; the icon cache holds every SVG rasterised so "
            "far, at twice the size it is drawn. The font atlas holds every "
            "baked size in one texture, normally 8-bit indexed - a glyph is "
            "coverage, so a byte a pixel and a palette say what four bytes "
            "did; the row below reports what this run got. Private is what "
            "this process has committed, resident is what is in memory now "
            "including every mapped DLL and driver. Only the first is ours - "
            "and with no GPU, as here, textures are in it too.");

    SDL_snprintf(v, sizeof(v), "%.0f KB reserved, %.0f KB used by this frame",
                 d.nk_bytes / 1024.0, d.nk_used / 1024.0);
    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "Nuklear buffer", v);
        SDL_snprintf(v, sizeof(v), "%.0f KB in %d rasters", d.icon_bytes / 1024.0,
                     d.icons);
        diag_row(app, ctx, "icon cache", v);
        SDL_snprintf(v, sizeof(v), "%d x %d %s, %.0f KB", d.atlas_w, d.atlas_h,
                     !d.atlas_bpp ? "none" : d.atlas_bpp == 1 ? "indexed"
                                                              : "RGBA32",
                     d.atlas_w * (double)d.atlas_h * d.atlas_bpp / 1024.0);
        diag_row(app, ctx, "font atlas", v);
        SDL_snprintf(v, sizeof(v), "%.1f MB", d.private_bytes / 1048576.0);
        diag_row(app, ctx, "process, private", v);
        SDL_snprintf(v, sizeof(v), "%.1f MB", d.rss_bytes / 1048576.0);
        diag_row(app, ctx, "process, resident", v);
    }

    section(app, ctx, "What startup costs",
            "Both counters at each step, and the difference each one made. "
            "Private is what the step cost this process; resident is what it "
            "mapped in, the graphics driver's shared pages included. The gap "
            "between the two columns is the point: a step can map in ten "
            "megabytes and commit almost none of it. Anything before the "
            "first line is what the C runtime and the loader wanted before "
            "main ran.");

    {
        int i;

        SDL_snprintf(v, sizeof(v), "%.1f MB private, %.1f MB resident",
                     d.priv_at[RSS_ENTRY] / 1048576.0,
                     d.rss_at[RSS_ENTRY] / 1048576.0);
        DIAG_ROWS(ctx) {
            diag_row(app, ctx, reaktor_rss_names[RSS_ENTRY], v);
            for (i = RSS_ENTRY + 1; i < RSS_STEPS; i++) {
                double dp = (d.priv_at[i] - (double)d.priv_at[i - 1]) / 1048576.0;
                double dr = (d.rss_at[i] - (double)d.rss_at[i - 1]) / 1048576.0;
                SDL_snprintf(v, sizeof(v), "%+.1f MB private   %+.1f MB resident",
                             dp, dr);
                diag_row(app, ctx, reaktor_rss_names[i], v);
            }
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacer(ctx);
}

static showcase_state g_show;

showcase_state *
sample_state(void)
{
    return &g_show;
}

void
sample_file_taken(App *app)
{
    reaktor_file_taken(app, g_show.file_pick, (int)sizeof(g_show.file_pick));
}

void
reaktor_showcase_page(App *app, struct nk_context *ctx, int tab,
                      float w, float h)
{
    showcase_state *s = sample_state();

    (void)w; (void)h;
    if (!s->seeded) seed(s);

    nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(8.0f, 9.0f));

    switch (tab) {
    case TAB_BUTTONS: page_buttons(app, ctx, s); break;
    case TAB_INPUTS:  page_inputs(app, ctx, s);  break;
    case TAB_DISPLAY: page_display(app, ctx, s); break;
    case TAB_LAYOUT:  page_layout(app, ctx, s);  break;
    case TAB_POPUPS:  page_popups(app, ctx, s);  break;
    case TAB_ANIM:    page_animation(app, ctx, s); break;
    case TAB_STYLING: page_styling(app, ctx, s); break;
    case TAB_DIAG:    page_diagnostics(app, ctx, s); break;
    default: break;
    }

    nk_style_pop_vec2(ctx);
}

void
sample_args(App *app, int argc, char **argv)
{
    (void)app; (void)argc; (void)argv;
}
