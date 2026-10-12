#include "showcase.h"

static void
format_row(struct reaktor_context *ctx, const char *label, const char *value)
{
    REAKTOR_ROW(.gap = ctx->style.window.spacing.x, .flags = REAKTOR_LAY_FILL_X) {
        reaktor_label(&(reaktor_label_spec){
            .text = label, .color = "--text-muted", .box = { .w = 110.0f } });
        reaktor_label(&(reaktor_label_spec){
            .text = value, .wrap = 1, .box = { .flags = REAKTOR_LAY_FILL_X } });
    }
}

static void
page_translations_formats(struct reaktor_context *ctx)
{
    struct tm day;
    char      date[96], time[32], number[48], money[192], plural[192];
    char      a[48], b[48], c[48];

    memset(&day, 0, sizeof day);
    day.tm_year = 1843 - 1900;
    day.tm_mon  = 10;
    day.tm_mday = 11;
    day.tm_hour = 10;
    day.tm_min  = 30;
    reaktor_format_date(date, sizeof date, &day, REAKTOR_DATE_FULL);
    reaktor_format_date(time, sizeof time, &day, REAKTOR_TIME_SHORT);
    reaktor_format_number(number, sizeof number, 1234567.891, 2);
    SDL_snprintf(money, sizeof money, "%s \xC2\xB7 %s \xC2\xB7 %s",
                 reaktor_format_money(a, sizeof a, 1234.5, "PLN"),
                 reaktor_format_money(b, sizeof b, 1234.5, "USD"),
                 reaktor_format_money(c, sizeof c, 123456.0, "JPY"));
    SDL_snprintf(plural, sizeof plural, "%s \xC2\xB7 %s \xC2\xB7 %s",
                 reaktor_trn("locale.count", 1, a, sizeof a),
                 reaktor_trn("locale.count", 5, b, sizeof b),
                 reaktor_trn("locale.count", 22, c, sizeof c));

    REAKTOR_COLUMN(.w = 360.0f, .gap = ctx->style.window.spacing.y) {
        reaktor_label(&(reaktor_label_spec){
            .text = reaktor_tr("locale.formats"), .style = "h4",
            .color = "--text-bright", .box = { .flags = REAKTOR_LAY_FILL_X } });
        format_row(ctx, reaktor_tr("locale.date"),   date);
        format_row(ctx, reaktor_tr("locale.time"),   time);
        format_row(ctx, reaktor_tr("locale.number"), number);
        format_row(ctx, reaktor_tr("locale.money"),  money);
        format_row(ctx, reaktor_tr("locale.plural"), plural);
    }
}

void
page_translations(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    const char *story = reaktor_tr("locale.story");
    char        para[4096];

    (void)s;
    if (reaktor_locale_count() == 0) {
        section(app, ctx, "No catalogs",
                "This build has nothing to translate with: it was configured "
                "with -DREAKTOR_LOCALE=OFF, or assets/locale was left behind "
                "when it was copied.");
        return;
    }

    section(app, ctx, "The same page in three languages",
            "Pick a language and everything under the line is looked up by "
            "key in its catalog; the code that draws it does not know which "
            "one it was. Polish is Aileron, from the font atlas. Aileron has "
            "no Japanese, so the Text module shapes it and draws it from "
            "M PLUS 1p, at whatever size it is set in.");
    api(app, ctx, "reaktor_tr  /  reaktor_trn  /  reaktor_format_date  /  "
                  "reaktor_format_money");

    {
        int            i, n = reaktor_locale_count();
        struct reaktor_rect row;

        reaktor_layout_row_begin(ctx, REAKTOR_STATIC, (float)(TAB_H - 6),
                                 n + 1);
        row = reaktor_widget_bounds(ctx);
        row.w = reaktor_window_get_content_region_size(ctx).x;
        reaktor_note_push(app, REAKTOR_A11Y_GROUP, "Language", NULL, 0, row);
        if (sample_option(app, ctx, "System", 0.0f, reaktor_locale_system()))
            reaktor_locale_set(NULL);
        for (i = 0; i < n; i++) {
            const char *code = reaktor_locale_code(i);

            if (sample_option(app, ctx, reaktor_locale_name(i), 0.0f,
                              !reaktor_locale_system() &&
                              !strcmp(code, reaktor_locale_current())))
                reaktor_locale_set(code);
        }
        reaktor_note_pop(app);
        reaktor_layout_row_end(ctx);
    }
    rule(ctx);

    REAKTOR_ROW(.gap = 40.0f, .flags = REAKTOR_LAY_WRAP) {
    REAKTOR_COLUMN(.w = SDL_min(reaktor_window_get_content_region_size(ctx).x, 640.0f),
                   .gap = ctx->style.window.spacing.y) {
        reaktor_label(&(reaktor_label_spec){
            .text = reaktor_tr("locale.title"), .style = "h4",
            .color = "--text-bright",
            .box = { .flags = REAKTOR_LAY_FILL_X } });
        reaktor_label(&(reaktor_label_spec){
            .text = reaktor_tr("locale.author"), .color = "--text-muted",
            .box = { .flags = REAKTOR_LAY_FILL_X } });
        while (*story) {
            size_t n = strcspn(story, "\n");
            size_t len = n < sizeof para ? n : sizeof para - 1;

            memcpy(para, story, len);
            para[len] = '\0';
            story += n + (story[n] == '\n');
            reaktor_label(&(reaktor_label_spec){
                .text = para, .wrap = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X } });
        }
        reaktor_label(&(reaktor_label_spec){
            .text = reaktor_tr("locale.source"), .color = "--text-muted",
            .wrap = 1, .box = { .flags = REAKTOR_LAY_FILL_X } });
    }
    page_translations_formats(ctx);
    }
}
