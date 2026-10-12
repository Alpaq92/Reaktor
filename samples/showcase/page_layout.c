#include "showcase.h"

static void
tabs_demo(App *app, struct reaktor_context *ctx)
{
    static const char *const names[3] = { "Overview", "Details", "History" };
    static const char *const notes[3] = {
        "A row of entries sized to their labels; the current one is marked.",
        "The same list as the sidebar on the left, laid out across.",
        "Styled by the .tab, .tab:hover, .tab-current and .tab-edge rules."
    };
    static int tab;

    section(app, ctx, "Tabs",
            "A tab bar, and the sidebar at the left of this window: one list, "
            "drawn across or down. Each answers the entry chosen.");
    api(app, ctx, "reaktor_tabs  /  reaktor_sidebar");
    REAKTOR_COLUMN(.gap = 8.0f) {
        reaktor_tabs(&(reaktor_tabs_spec){
            .items = names, .count = 3, .chosen = &tab, .name = "Sections",
            .box = { .flags = REAKTOR_LAY_FILL_X } });
        reaktor_label(&(reaktor_label_spec){
            .text = notes[tab], .color = "--text-muted",
            .box  = { .h = 24.0f, .flags = REAKTOR_LAY_FILL_X } });
    }
}

void
page_layout(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    int i;

    (void)s;
    tabs_demo(app, ctx);

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

    section(app, ctx, "Splitters",
            "A splitter is a handle between two children that hands its drag "
            "to a share the page owns, so the two keep their proportions when "
            "the window resizes.");
    api(app, ctx, "reaktor_splitter");

    {
        static float split = 0.35f;

        REAKTOR_ROW(.h = 3.0f * ROW) {
            REAKTOR_COLUMN(.weight = split,
                           .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y)
                reaktor_button(&(reaktor_button_spec){
                    .label = "Left",
                    .box = { .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y } });
            reaktor_splitter(&(reaktor_splitter_spec){ .value = &split,
                                                       .name = "Split" });
            REAKTOR_COLUMN(.weight = 1.0f - split,
                           .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y)
                reaktor_button(&(reaktor_button_spec){
                    .label = "Right",
                    .box = { .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y } });
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
            "login card centered in the window on the first tab is one.");
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
    api(app, ctx, "reaktor_group_begin  /  reaktor_group_begin_titled");

    reaktor_layout_row_dynamic(ctx, 150.0f, 2);
    if (reaktor_group_begin(ctx, "plain", REAKTOR_WINDOW_BORDER)) {
        reaktor_layout_row_dynamic(ctx, 24.0f, 1);
        for (i = 0; i < 12; i++) {
            char lab[24];
            SDL_snprintf(lab, sizeof(lab), "scrolls: item %d", i + 1);
            reaktor_text_label(ctx, lab, REAKTOR_TEXT_LEFT);
        }
        reaktor_group_end(ctx);
    }
    if (reaktor_group_begin_titled(ctx, "titled", "With a title",
                                   REAKTOR_WINDOW_BORDER | REAKTOR_WINDOW_TITLE)) {
        reaktor_layout_row_dynamic(ctx, 24.0f, 1);
        reaktor_text_label(ctx, "REAKTOR_WINDOW_TITLE", REAKTOR_TEXT_LEFT);
        reaktor_text_label(ctx, "draws the header", REAKTOR_TEXT_LEFT);
        reaktor_group_end(ctx);
    }

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
