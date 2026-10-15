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

typedef struct file_node {
    const char *label, *icon;
    int         parent;
} file_node;

static const file_node g_files[] = {
    { "Documents",   "folder-outline",         -1 },
    { "Reports",     "folder-outline",          0 },
    { "2025.pdf",    "document-text-outline",   1 },
    { "2026.pdf",    "document-text-outline",   1 },
    { "Notes.txt",   "document-outline",        0 },
    { "Pictures",    "folder-outline",         -1 },
    { "Holiday",     "folder-outline",          5 },
    { "Beach.jpg",   "image-outline",           6 },
    { "Harbor.jpg",  "image-outline",           6 },
    { "Music",       "folder-outline",         -1 },
    { "Overture.ogg", "musical-notes-outline",  9 }
};

static int         file_parent(int n, void *u) { (void)u; return g_files[n].parent; }
static const char *file_label(int n, void *u)  { (void)u; return g_files[n].label; }
static const char *file_icon(int n, void *u)   { (void)u; return g_files[n].icon; }

/* Two lists of task numbers; a drop moves one from a list to a place in another. */
static const char *const g_tasks[] = {
    "Write the release notes", "Check the BSD builds", "Answer the issue on fonts",
    "Draw the new icon", "Measure the web build", "Update the screenshots"
};
static int g_lane[2][6] = { { 0, 1, 2, 3 }, { 4, 5 } };
static int g_lane_n[2]  = { 4, 2 };

static const char *
lane_cell(int row, int column, void *user)
{
    (void)column;
    return g_tasks[g_lane[(intptr_t)user][row]];
}

static App *g_lane_app;

typedef struct lane_move { int src, from, dst, to; } lane_move;

static void
lane_shift(int src, int from, int dst, int to)
{
    int task = g_lane[src][from], i;

    for (i = from; i < g_lane_n[src] - 1; i++) g_lane[src][i] = g_lane[src][i + 1];
    g_lane_n[src]--;
    for (i = g_lane_n[dst]; i > to; i--) g_lane[dst][i] = g_lane[dst][i - 1];
    g_lane[dst][to] = task;
    g_lane_n[dst]++;
}

static void lane_undo(void *d) { lane_move *m = d; lane_shift(m->dst, m->to, m->src, m->from); }
static void lane_redo(void *d) { lane_move *m = d; lane_shift(m->src, m->from, m->dst, m->to); }

static void
lane_dropped(const char *from, int item, int at, void *user)
{
    lane_move *m = (lane_move *)SDL_malloc(sizeof *m);

    if (!m) return;
    m->src  = SDL_strcmp(from, "To do") == 0 ? 0 : 1;
    m->from = item;
    m->dst  = (int)(intptr_t)user;
    m->to   = m->src == m->dst && at > item ? at - 1 : at;
    lane_redo(m);
    reaktor_undo_push(g_lane_app, &(reaktor_undo_step){
        .label = "Move a task", .undo = lane_undo, .redo = lane_redo, .free = SDL_free,
        .data = m });
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
            "A tree is its nodes in depth-first order, each naming its parent; "
            "the open flags and the selection are the caller's. Only the rows "
            "on screen are drawn. Up and Down move between rows, Right opens a "
            "branch or steps into it, Left closes it or steps out to the parent. "
            "Below it, a list view by hand emits only the rows on screen too, so "
            "its cost does not grow with the data.");
    api(app, ctx, "reaktor_tree  /  reaktor_list_view_begin");

    REAKTOR_COLUMN(.name = "Files", .flags = REAKTOR_LAY_FILL_X) {
        reaktor_tree(&(reaktor_tree_spec){
            .name = "Files", .count = (int)SDL_arraysize(g_files),
            .parent = file_parent, .label = file_label, .icon = file_icon,
            .open = s->tree_open, .selected = &s->tree_sel,
            .box = { .h = 200.0f, .flags = REAKTOR_LAY_FILL_X } });
    }

    section(app, ctx, "Drag and drop",
            "A list with a .drag kind lets its rows be dragged; a list of the same "
            "kind shows where one would land and hands the drop to .dropped, with "
            "the list it came from. Reordering is a drop on the same list. Each "
            "move here is a step on the window's undo stack: Ctrl+Z and Ctrl+Y, or "
            "the buttons, which name the step they would undo.");
    api(app, ctx, "reaktor_list .drag  /  .dropped  /  reaktor_undo_push");
    g_lane_app = app;

    REAKTOR_ROW(.h = 200.0f, .gap = ctx->style.window.spacing.x, .flags = REAKTOR_LAY_FILL_X) {
        reaktor_list(&(reaktor_list_spec){
            .name = "To do", .count = g_lane_n[0], .cell = lane_cell, .user = (void *)0,
            .drag = "task", .dropped = lane_dropped, .columns = (const char *const[]){ "To do" },
            .box = { .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y } });
        reaktor_list(&(reaktor_list_spec){
            .name = "Done", .count = g_lane_n[1], .cell = lane_cell, .user = (void *)1,
            .drag = "task", .dropped = lane_dropped, .columns = (const char *const[]){ "Done" },
            .box = { .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y } });
    }
    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        char undo[48], redo[48];

        SDL_snprintf(undo, sizeof undo, "Undo %s", reaktor_undo_label(app) ? reaktor_undo_label(app) : "");
        SDL_snprintf(redo, sizeof redo, "Redo %s", reaktor_redo_label(app) ? reaktor_redo_label(app) : "");
        if (reaktor_button(&(reaktor_button_spec){ .label = undo, .name = "Undo",
                                                   .disabled = !reaktor_undo_label(app),
                                                   .box = { .w = 180.0f } }))
            reaktor_undo(app);
        if (reaktor_button(&(reaktor_button_spec){ .label = redo, .name = "Redo",
                                                   .disabled = !reaktor_redo_label(app),
                                                   .box = { .w = 180.0f } }))
            reaktor_redo(app);
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
