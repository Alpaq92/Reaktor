#include "showcase.h"

void
page_inputs(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static const char *const sizes[] = { "Compact", "Comfortable", "Spacious" };
    char line[96];

    section(app, ctx, "Text",
            "One rule - tiny.css's `input` - supplies the fill, the radius, "
            "the padding and the border - 2px of transparent until "
            "`input:focus` colors it. Only the focused field takes that "
            "color; the rest get the neutral edge, because the rule assumes "
            "a field sits on the page and on the login card the field and the "
            "card are the same color. A filter rejects a keystroke before it "
            "reaches the buffer, so the field cannot hold a value it would "
            "have to validate later.");
    api(app, ctx, "reaktor_field with .filter = reaktor_filter_default / _decimal / _hex");

    {
        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            reaktor_field(&(reaktor_field_spec){
                .buf = s->name, .len = &s->name_len, .cap = SC_TEXT_CAP,
                .hint = "Full name", .filter = reaktor_filter_default,
                .box = { .h = 36.0f, .flags = REAKTOR_LAY_FILL_X } });

            REAKTOR_ROW(.h = 36.0f, .flags = REAKTOR_LAY_FILL_X,
                        .gap = ctx->style.window.spacing.x) {
                reaktor_field(&(reaktor_field_spec){
                    .buf = s->digits, .len = &s->digits_len,
                    .cap = SC_TEXT_CAP, .hint = "Digits only",
                    .filter = reaktor_filter_decimal,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
                reaktor_field(&(reaktor_field_spec){
                    .buf = s->hex, .len = &s->hex_len,
                    .cap = SC_TEXT_CAP, .hint = "Hex only",
                    .filter = reaktor_filter_hex,
                    .box = { .flags = REAKTOR_LAY_FILL_X |
                                      REAKTOR_LAY_FILL_Y } });
            }
        }
    }

    api(app, ctx, "reaktor_field with .multiline "
                  "(Nuklear breaks lines on newlines only - there is no wrap)");
    {
        REAKTOR_ROW(.h = 92.0f) {
            reaktor_field(&(reaktor_field_spec){
                .buf = s->note, .len = &s->note_len, .cap = SC_BOX_CAP,
                .name = "Notes", .multiline = 1,
                .filter = reaktor_filter_default,
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
    api(app, ctx, "reaktor_slider  /  reaktor_progress  /  reaktor_knob");

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
            "A property is a labeled number that can be dragged, clicked "
            "through its two steppers, or typed into - the three ways a "
            "person expects to change a number, in one widget.");
    api(app, ctx, "reaktor_property with .ivalue / .fvalue / .dvalue");

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
            "reaktor_combo is the whole widget in one call, for the common case "
            "of picking a string out of an array. The begin/end form opens an "
            "empty popup instead and lets any layout go inside it, which is how "
            "a combo grows a color picker or a set of sliders.");
    api(app, ctx, "REAKTOR_COMBO  /  reaktor_combo_item");

    popup_style_push(ctx);
    {
        int i;

        REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
            REAKTOR_ROW(.h = ROW_TALL, .gap = ctx->style.window.spacing.x,
                        .flags = REAKTOR_LAY_FILL_X) {
                REAKTOR_COMBO(.label = sizes[s->combo_size], .name = "Size",
                              .body_h = 130.0f,
                              .box = { .flags = REAKTOR_LAY_FILL_X |
                                                REAKTOR_LAY_FILL_Y }) {
                    reaktor_layout_row_dynamic(ctx, 26.0f, 1);
                    for (i = 0; i < 3; i++)
                        if (reaktor_combo_item(sizes[i], i == s->combo_size))
                            s->combo_size = i;
                }
                REAKTOR_COMBO(.label = sizes[s->combo_symbol],
                              .name = "Size, with a symbol",
                              .body_h = 130.0f, .disc = 1,
                              .box = { .flags = REAKTOR_LAY_FILL_X |
                                                REAKTOR_LAY_FILL_Y }) {
                    reaktor_layout_row_dynamic(ctx, 26.0f, 1);
                    for (i = 0; i < 3; i++)
                        if (reaktor_combo_item(sizes[i], i == s->combo_symbol))
                            s->combo_symbol = i;
                }
            }
            REAKTOR_ROW(.h = ROW_TALL, .gap = ctx->style.window.spacing.x,
                        .flags = REAKTOR_LAY_FILL_X) {
                struct reaktor_color tint = reaktor_rgb_cf(s->tint);

                REAKTOR_COMBO(.name = "Tint", .body_h = 150.0f,
                              .swatch = &tint,
                              .box = { .flags = REAKTOR_LAY_FILL_X |
                                                REAKTOR_LAY_FILL_Y }) {
                    reaktor_layout_row_dynamic(ctx, 26.0f, 1);
                    reaktor_hot_follow(app, reaktor_widget_bounds(ctx), 0);
                    reaktor_property_float(ctx, "R:", 0.0f, &s->tint.r, 1.0f,
                                           0.01f, 0.005f);
                    reaktor_hot_follow(app, reaktor_widget_bounds(ctx), 0);
                    reaktor_property_float(ctx, "G:", 0.0f, &s->tint.g, 1.0f,
                                           0.01f, 0.005f);
                    reaktor_hot_follow(app, reaktor_widget_bounds(ctx), 0);
                    reaktor_property_float(ctx, "B:", 0.0f, &s->tint.b, 1.0f,
                                           0.01f, 0.005f);
                }
                REAKTOR_COMBO(.label = "Anything at all",
                              .name = "Anything at all", .body_h = 130.0f,
                              .box = { .flags = REAKTOR_LAY_FILL_X |
                                                REAKTOR_LAY_FILL_Y }) {
                    reaktor_layout_row_dynamic(ctx, 26.0f, 1);
                    reaktor_text_label(ctx, "A combo is just a popup",
                                       REAKTOR_TEXT_LEFT);
                    reaktor_slider_bar(app, ctx, 0, &s->slider_f, 0.0f, 1.0f,
                                       0.01f);
                    reaktor_hot_top(app, reaktor_widget_bounds(ctx), 0, 1);
                    reaktor_checkbox_label(ctx, "with a layout in it",
                                           &s->check_spell);
                }
            }
        }
    }
    popup_style_pop(ctx);

    section(app, ctx, "Color picker",
            "The one widget Nuklear draws as a continuous field rather than "
            "from the style: a saturation-value square with a hue bar. Its "
            "value is an reaktor_colorf, which is what the swatch and the combo "
            "above are reading.");
    api(app, ctx, "reaktor_color_pick");

    {
        struct reaktor_color c  = reaktor_rgb_cf(s->tint);

        SDL_snprintf(line, sizeof(line), "#%02x%02x%02x", c.r, c.g, c.b);

        REAKTOR_ROW(.h = 132.0f,
                    .gap = ctx->style.window.spacing.x) {
            reaktor_color_pick(&(reaktor_color_spec){
                .name = "Color picker", .value = &s->tint,
                .box = { .w = 210.0f, .h = 132.0f } });

            REAKTOR_COLUMN(.w = 210.0f,
                           .gap = ctx->style.window.spacing.y) {
                reaktor_swatch(&(reaktor_swatch_spec){
                    .name = "Chosen color", .fill = c,
                    .box = { .h = 44.0f, .flags = REAKTOR_LAY_FILL_X } });
                reaktor_label(&(reaktor_label_spec){
                    .text = line, .align = REAKTOR_CENTER,
                    .box = { .h = 22.0f, .flags = REAKTOR_LAY_FILL_X } });
            }
        }
    }

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
