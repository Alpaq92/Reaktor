#include "showcase.h"

static const char *const g_grid[7][3] = {
    { "SDL3",      "Zlib",   "window, input, renderer, main loop" },
    { "Nuklear",   "MIT/PD", "the widgets and their layout" },
    { "LCUI css",  "MIT",    "parses the stylesheet, computes values" },
    { "tiny.css",  "MIT",    "the stylesheet itself" },
    { "plutosvg",  "MIT",    "rasterises the icons" },
    { "Ionicons",  "MIT",    "the icons" },
    { "Aileron",   "CC0",    "the typeface" }
};

static const char *
grid_cell(int row, int column, void *user)
{
    (void)user;
    return g_grid[row][column];
}

static void
section_grid(App *app, struct reaktor_context *ctx)
{
    static const char *const head[3] = { "Submodule", "License", "What it does" };
    static const float weights[3] = { 0.24f, 0.16f, 0.60f };
    static int chosen = -1;

    section(app, ctx, "Tables",
            "The only widget on these pages that Nuklear does not have - and "
            "one whose colors come from the stylesheet's own table rules: "
            "thead, th, td and the --table-bg stripes are read straight out of "
            "tiny.css. Only the rows on screen are drawn, so a list of a "
            "million costs what a list of eight does; a row is chosen by a "
            "click or from the keyboard.");
    api(app, ctx, "reaktor_list");

    reaktor_list(&(reaktor_list_spec){
        .name = "Submodules", .count = 7, .columns = head, .widths = weights,
        .column_count = 3, .cell = grid_cell, .selected = &chosen });
}

void
page_display(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    struct reaktor_list_view view;
    int i;

    section(app, ctx, "Text",
            "Alignment is a flag, not a layout: the label fills the row it "
            "was given and puts the text where the flag says. Wrapping is a "
            "separate call, because a wrapped label has to be given its "
            "height in advance - Nuklear cannot grow the row after the fact.");
    api(app, ctx, "reaktor_label  /  .align  /  .wrap");

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
                        .text = tokens[i], .color = tokens[i],
                        .align = (unsigned char)i,
                        .box = { .flags = REAKTOR_LAY_FILL_X |
                                          REAKTOR_LAY_FILL_Y } });
            }
            reaktor_label(&(reaktor_label_spec){
                .text = "A wrapped label breaks between words inside the row "
                        "it was given, which is why the paragraphs on these "
                        "pages measure their own height first.",
                .wrap = 1,
                .box = { .h = 40.0f, .flags = REAKTOR_LAY_FILL_X } });
        }
    }

    section(app, ctx, "Images",
            "Every icon here is an SVG read out of the Ionicons submodule and "
            "rasterized at the size it is drawn, twice over for the "
            "downscale. The stroke color is substituted into the file before "
            "it is parsed, which is how an icon follows the stylesheet - CSS "
            "cannot reach inside an SVG. The calendar is drawn in --links "
            "and the rest in the text color, from the same artwork - the "
            "color is the caller's, not the file's. A PNG or a JPEG in .src "
            "is decoded as it is, and stretched to its box.");
    api(app, ctx, "reaktor_icon  /  .src");

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
            reaktor_icon(&(reaktor_icon_spec){
                .src     = "assets/icons/reaktor-icon.png",
                .tooltip = "reaktor-icon.png",
                .box     = { .w = 30.0f, .h = 30.0f } });
        }
    }

    section(app, ctx, "Charts",
            "A chart is pushed one value at a time and reports back whether "
            "each was hovered or clicked, so the data never has to be copied "
            "anywhere first. Slots stack several series in one frame; "
            "reaktor_plot is the same thing for an array you already have.");
    api(app, ctx,
        "reaktor_chart_begin / reaktor_chart_push  /  reaktor_chart_add_slot  / "
        " "
                  "reaktor_plot");

    reaktor_layout_row_dynamic(ctx, 100.0f, 1);
    if (reaktor_chart_begin(ctx, REAKTOR_CHART_LINES, SC_SERIES_N, -1.1f,
                            1.1f)) {
        for (i = 0; i < SC_SERIES_N; i++) reaktor_chart_push(ctx, s->series[i]);
        reaktor_chart_end(ctx);
    }

    reaktor_layout_row_dynamic(ctx, 100.0f, 1);
    if (reaktor_chart_begin(ctx, REAKTOR_CHART_COLUMN, SC_SERIES_N, -1.1f,
                            1.1f)) {
        reaktor_chart_add_slot(ctx, REAKTOR_CHART_LINES, SC_SERIES_N, -1.1f,
                               1.1f);
        for (i = 0; i < SC_SERIES_N; i++) {
            reaktor_chart_push_slot(ctx, s->series[i], 0);
            reaktor_chart_push_slot(ctx, s->series[SC_SERIES_N - 1 - i], 1);
        }
        reaktor_chart_end(ctx);
    }

    reaktor_layout_row_dynamic(ctx, 70.0f, 1);
    reaktor_plot(ctx, REAKTOR_CHART_LINES, s->series, SC_SERIES_N, 0);

    section(app, ctx, "Progress and rules",
            "A fixed progress bar reports; reaktor_rule_horizontal is the "
            "separator every page on this tab is divided by, and takes its "
            "color as an argument rather than from the style.");
    api(app, ctx, "reaktor_progress  /  reaktor_rule_horizontal");

    {
        reaktor_size   fixed = s->progress;

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
            "on. reaktor_tree_element makes the header itself selectable, drawn "
            "through reaktor_style_selectable - so an unselected one shows "
            "nothing but its label, and a selected one fills with the accent. A "
            "list view emits only the rows actually on screen, so its cost does "
            "not grow with the data.");
    api(app, ctx, "reaktor_tree_push  /  reaktor_tree_element_push  /  "
                  "reaktor_list_view_begin");

    {
        struct reaktor_rect hb = tree_header_bounds(ctx);
        int open;

        hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
        open = reaktor_tree_push(ctx, REAKTOR_TREE_TAB, "A tab-style tree",
                                 REAKTOR_MAXIMIZED);
        tree_chevron(app, ctx, hb, open);
        reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, "A tab-style tree", NULL,
                          open ? REAKTOR_A11Y_EXPANDED : 0u, hb);
        if (open) {
            reaktor_layout_row_dynamic(ctx, ROW_SMALL, 1);
            reaktor_text_label(ctx, "Its children are indented under it.",
                               REAKTOR_TEXT_LEFT);

            hb = tree_header_bounds(ctx);
            hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
            open = reaktor_tree_push(ctx, REAKTOR_TREE_NODE, "A node inside it",
                                     REAKTOR_MINIMIZED);
            tree_chevron(app, ctx, hb, open);
            reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, "A node inside it",
                              NULL, open ? REAKTOR_A11Y_EXPANDED : 0u, hb);
            if (open) {
                reaktor_layout_row_dynamic(ctx, ROW_SMALL, 1);
                reaktor_text_label(ctx, "Nesting is unlimited.",
                                   REAKTOR_TEXT_LEFT);
                reaktor_tree_pop(ctx);
            }
            reaktor_note_pop(app);

            for (i = 0; i < 3; i++) {
                char lab[32];
                SDL_snprintf(lab, sizeof(lab), "Selectable branch %d", i + 1);
                hb = tree_header_bounds(ctx);
                hot(app, ctx, REAKTOR_A11Y_NONE, NULL, 0);
                open = reaktor_tree_element_push(ctx, REAKTOR_TREE_NODE, lab,
                                                 REAKTOR_MINIMIZED,
                                                 &s->tree_leaf[i]);
                tree_chevron(app, ctx, hb, open);
                reaktor_note_push(app, REAKTOR_A11Y_TREEITEM, lab, NULL,
                                  (open ? REAKTOR_A11Y_EXPANDED : 0u) |
                                  (s->tree_leaf[i] ? REAKTOR_A11Y_CHECKED
                                                   : 0u),
                                  hb);
                if (open) {
                    reaktor_layout_row_dynamic(ctx, ROW_SMALL, 1);
                    reaktor_text_label(ctx, "with a checkbox in the header",
                                       REAKTOR_TEXT_LEFT);
                    reaktor_tree_pop(ctx);
                }
                reaktor_note_pop(app);
            }
            reaktor_tree_pop(ctx);
        }
        reaktor_note_pop(app);
    }

    reaktor_layout_row_dynamic(ctx, 10.0f, 1);
    reaktor_spacer(ctx);

    reaktor_layout_row_dynamic(ctx, 152.0f, 1);
    reaktor_style_push_vec2(ctx, &ctx->style.window.spacing,
                            reaktor_vec2(0.0f, 0.0f));
    if (reaktor_list_view_begin(ctx, &view, "rows", REAKTOR_WINDOW_BORDER, 24,
                                SC_LIST_N)) {
        reaktor_layout_row_dynamic(ctx, 24.0f, 1);
        for (i = view.begin; i < view.end; i++) {
            char lab[40];
            reaktor_bool on = (s->list_sel == i);
            SDL_snprintf(lab, sizeof(lab), "Row %d of %d", i + 1, SC_LIST_N);
            hot(app, ctx, REAKTOR_A11Y_LISTITEM, lab,
                on ? REAKTOR_A11Y_SELECTED : 0u);
            if (reaktor_selectable_label(ctx, lab, REAKTOR_TEXT_LEFT,
                                         &on) && on)
                s->list_sel = i;
        }
        reaktor_list_view_end(&view);
    }
    reaktor_style_pop_vec2(ctx);

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
