#include "showcase.h"

static void
rule_says(const char *selector)
{
    reaktor_style st;
    char line[320], bg[16] = "transparent";

    reaktor_style_get(selector, &st);
    if (st.bg[3])
        SDL_snprintf(bg, sizeof(bg), "#%02x%02x%02x", st.bg[0], st.bg[1], st.bg[2]);
    if (st.matched)
        SDL_snprintf(line, sizeof(line),
                     "%s is #%02x%02x%02x on %s, corners %.0f, "
                     "border %.0f, padding %.1f %.1f %.1f %.1f, type %dpx, "
                     "box %.0fx%.0f min %.0fx%.0f max %.0fx%.0f, "
                     "margin %.1f %.1f %.1f %.1f, "
                     "line %.1f",
                     selector, st.fg[0], st.fg[1], st.fg[2], bg,
                     (double)st.rounding, (double)st.border,
                     (double)st.pad[REAKTOR_SIDE_TOP],
                     (double)st.pad[REAKTOR_SIDE_RIGHT],
                     (double)st.pad[REAKTOR_SIDE_BOTTOM],
                     (double)st.pad[REAKTOR_SIDE_LEFT],
                     st.font_px,
                     (double)st.width, (double)st.height,
                     (double)st.min_width, (double)st.min_height,
                     (double)st.max_width, (double)st.max_height,
                     (double)st.margin[REAKTOR_SIDE_TOP],
                     (double)st.margin[REAKTOR_SIDE_RIGHT],
                     (double)st.margin[REAKTOR_SIDE_BOTTOM],
                     (double)st.margin[REAKTOR_SIDE_LEFT],
                     (double)st.line_height);
    else
        SDL_snprintf(line, sizeof(line), "%s - no sheet has a rule for it",
                     selector);

    reaktor_label(&(reaktor_label_spec){
        .text = line, .name = selector,
        .value = st.matched ? line + strlen(selector) + 4 : "no rule",
        .color = st.matched ? "--text-main" : "--text-muted",
        .box = { .h = ROW_SMALL, .flags = REAKTOR_LAY_FILL_X } });
}

void
page_styling(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static const reaktor_asset simple_css = {
        .path = "external/simplecss/simple.css" };
    static reaktor_bool on;

    section(app, ctx, "Change the stylesheet, change the application",
            "Nothing on this page is about Reaktor. The switch below stops "
            "reading one CSS file and starts reading another, and everything "
            "in the window follows - the buttons, the fields, the borders, "
            "the corners. That is the only claim this library makes about "
            "styling, and it is either true in front of you or it is not.");
    api(app, ctx, "external/simplecss");

    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        if (reaktor_check(&(reaktor_check_spec){
                .label = "Read Simple.css over the top of Water.css",
                .name  = "Override stylesheet",
                .on    = &on,
                .box   = { .w = 380.0f, .flags = REAKTOR_LAY_CENTER_Y } }))
            reaktor_set_css(app, on ? &simple_css : NULL, on ? 1 : 0);
        reaktor_soak();
    }

    section(app, ctx, "Watch these while you do it",
            "Ordinary controls, drawn the way every other page draws them. "
            "They are here because a change you have to be told about is not "
            "a change you can see.");
    api(app, ctx, "the same widgets as everywhere else");

    REAKTOR_COLUMN(.gap = 10.0f) {
        REAKTOR_ROW(.gap = ctx->style.window.spacing.x,
                    .flags = REAKTOR_LAY_FILL_X) {
            reaktor_button(&(reaktor_button_spec){
                .label = "A button",
                .box = { .flags = REAKTOR_LAY_FILL_X } });
            reaktor_button(&(reaktor_button_spec){
                .label = "An accented one", .accent = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X } });
            reaktor_button(&(reaktor_button_spec){
                .label = "A disabled one", .disabled = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X } });
        }

        REAKTOR_ROW(.gap = ctx->style.window.spacing.x,
                    .flags = REAKTOR_LAY_FILL_X) {
            reaktor_field(&(reaktor_field_spec){
                .name = "Name", .hint = "a text field",
                .buf = s->name, .len = &s->name_len, .cap = SC_TEXT_CAP,
                .box = { .flags = REAKTOR_LAY_FILL_X } });
            REAKTOR_COMBO(.label = "a combo box", .name = "Combo",
                          .body_h = 110.0f,
                          .box = { .w = 260.0f }) {
                reaktor_layout_row_dynamic(ctx, 26.0f, 1);
                reaktor_combo_item("one", 1);
                reaktor_combo_item("two", 0);
            }
        }

        REAKTOR_ROW(.gap = 20.0f, .flags = REAKTOR_LAY_FILL_X) {
            reaktor_slider(&(reaktor_slider_spec){
                .name = "Slider", .value = &s->slider_f,
                .lo = 0.0f, .hi = 1.0f, .step = 0.01f,
                .box = { .flags = REAKTOR_LAY_FILL_X |
                                  REAKTOR_LAY_CENTER_Y } });
            reaktor_progress(&(reaktor_progress_spec){
                .name = "Progress", .value = &s->progress, .max = 100,
                .modifiable = 1,
                .box = { .w = 300.0f,
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
        rule_says("h4");
    }

    section(app, ctx, "Three sheets, read in order",
            "The palette, then Water.css, then this application's own, then "
            "the override if there is one. There is no merging step and no "
            "precedence table - CSS resolves a tie by document order, so the "
            "last sheet read wins, and reading one last is the whole "
            "mechanism. A selector the override says nothing about still "
            "resolves to Water.css's rule; one neither mentions leaves the "
            "widget on Nuklear's own default.");
    api(app, ctx, "reaktor_style_init(sheets, n, theme)");

    REAKTOR_COLUMN(.gap = ctx->style.window.spacing.y) {
        reaktor_label(&(reaktor_label_spec){
            .text = "Two controls here have no rule in either sheet - a "
                    "slider and a progress bar are not things a document "
                    "has - so they borrow: the rail from input, the fill "
                    "from a. Every sheet has a link color, which is why "
                    "that is read from the rule rather than from a --links "
                    "token some sheets will not have.",
            .name = "Controls with no rule", .color = "--text-muted",
            .wrap = 1, .box = { .flags = REAKTOR_LAY_FILL_X } });
        reaktor_label(&(reaktor_label_spec){
            .text = "Simple.css keeps its dark palette in "
                    "@media (prefers-color-scheme: dark), which this tree "
                    "used to drop along with every other at-rule - so a "
                    "sheet written that way loaded light-only. That branch "
                    "is now unwrapped for whichever scheme is active. Width "
                    "queries and print are still dropped: a viewport query "
                    "means nothing to a window that is not a document.",
            .name = "Media queries", .color = "--text-muted", .wrap = 1,
            .box = { .flags = REAKTOR_LAY_FILL_X } });
    }

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
