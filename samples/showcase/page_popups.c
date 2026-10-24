#include "showcase.h"

static int g_plain_floater, g_modal_floater;

static void
forget_id(App *app, void *user)
{
    (void)app;
    *(int *)user = 0;
}

static void
floater_text(App *app, struct reaktor_context *ctx, const char *title,
           const char *text)
{
    reaktor_layout_row_dynamic(ctx, 22.0f, 1);
    reaktor_style_push_font(ctx,
                            reaktor_style_font(app, ".popup-title", 16, 1));
    reaktor_label_colored(ctx, title, REAKTOR_TEXT_LEFT,
                          reaktor_token("--text-bright",
                                        reaktor_text_color(ctx)));
    reaktor_style_pop_font(ctx);

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);

    reaktor_layout_row_dynamic(ctx, 44.0f, 1);
    reaktor_style_push_font(ctx, reaktor_style_font(app, ".small", 13, 0));
    reaktor_label_colored_wrap(ctx, text,
                               reaktor_token("--text-muted",
                                             reaktor_text_color(ctx)));
    reaktor_style_pop_font(ctx);

    reaktor_layout_row_dynamic(ctx, 12.0f, 1);
    reaktor_spacer(ctx);

    reaktor_layout_row_template_begin(ctx, 36.0f);
    reaktor_layout_row_template_push_dynamic(ctx);
    reaktor_layout_row_template_push_static(ctx, 96.0f);
    reaktor_layout_row_template_push_static(ctx, 96.0f);
    reaktor_layout_row_template_end(ctx);
    reaktor_spacer(ctx);
}

static void
modal_floater(App *app, struct reaktor_context *ctx, int w, int h, void *user)
{
    (void)w; (void)h;
    floater_text(app, ctx, "Close without saving?",
               "Everything behind this is dimmed and takes no input until it "
               "is closed.");
    if (reaktor_button_label(app, ctx, "Cancel"))
        reaktor_floater_close(app, *(int *)user);
    if (reaktor_button_accent(app, ctx, "OK"))
        reaktor_floater_close(app, *(int *)user);
}

static void
plain_floater(App *app, struct reaktor_context *ctx, int w, int h, void *user)
{
    (void)w; (void)h;
    floater_text(app, ctx, "A floater",
               "The page under this stays live: it scrolls, and its buttons "
               "and fields still answer.");
    reaktor_spacer(ctx);
    if (reaktor_button_label(app, ctx, "Close"))
        reaktor_floater_close(app, *(int *)user);
}

void
showcase_open_floater(App *app, int modal)
{
    int *id = modal ? &g_modal_floater : &g_plain_floater;

    if (*id) return;
    *id = reaktor_floater_open(app, &(reaktor_floater){
        .title = modal ? "Close without saving?" : "A floater",
        .w     = 328.0f,
        .h     = 22.0f + 8.0f + 44.0f + 12.0f + 36.0f + 4.0f * REAKTOR_MENU_GAP,
        .modal = modal, .body = modal ? modal_floater : plain_floater,
        .closed = forget_id, .user = id });
}

static void
floater_demos(App *app, struct reaktor_context *ctx)
{
    section(app, ctx, "Floaters",
            "Dialogs drawn inside the window, above the page. A modal one dims "
            "the window and holds its input until it closes; the other leaves "
            "the page live under it.");
    api(app, ctx, "reaktor_floater_open  /  reaktor_floater_close");

    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label    = "Open a floater",
                .disabled = (unsigned char)(g_plain_floater != 0),
                .box      = { .flags = REAKTOR_LAY_FILL_X } }))
            showcase_open_floater(app, 0);
        if (reaktor_button(&(reaktor_button_spec){
                .label    = "Open a modal floater",
                .box      = { .flags = REAKTOR_LAY_FILL_X } }))
            showcase_open_floater(app, 1);
    }
}

static int     g_plain_win, g_modal_win;
static reaktor_bool g_win_ticked = 1;

static void
window_page(App *app, struct reaktor_context *ctx, int w, int h, void *user)
{
    int modal = user == &g_modal_win;

    (void)ctx;
    REAKTOR_FREE(.w = (float)w, .h = (float)h) {
        REAKTOR_COLUMN(.w = w - 40.0f, .h = h - 40.0f, .ml = 20.0f, .mt = 20.0f,
                       .gap = 10.0f) {
            reaktor_label(&(reaktor_label_spec){
                .text  = modal ? "A modal window" : "A window of its own",
                .style = "h4", .color = "--text-bright",
                .box   = { .flags = REAKTOR_LAY_FILL_X } });
            reaktor_label(&(reaktor_label_spec){
                .text  = modal
                    ? "The main window takes no input until this one closes."
                    : "Its own renderer, fonts and Nuklear context, drawn by "
                      "the same widgets and sheets as the main window.",
                .color = "--text-muted", .wrap = 1,
                .box   = { .h = 44.0f, .flags = REAKTOR_LAY_FILL_X } });
            reaktor_check(&(reaktor_check_spec){
                .label = "Input reaches it", .on = &g_win_ticked });
            REAKTOR_ROW(.h = 34.0f,
                        .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_PACK_END) {
                if (reaktor_button(&(reaktor_button_spec){
                        .label = "Close", .box = { .w = 96.0f } }))
                    reaktor_window_close(app, *(int *)user);
            }
        }
    }
}

void
showcase_open_window(App *app, int modal)
{
    int *id = modal ? &g_modal_win : &g_plain_win;

    if (*id) return;
    *id = reaktor_window_open(app, &(reaktor_window){
        .window = { .title = modal ? "A modal window" : "A window of its own",
                    .w = 420, .h = 230 },
        .modal = modal, .page = window_page, .closed = forget_id,
        .user = id });
}

static void
window_demos(App *app, struct reaktor_context *ctx)
{
#ifdef __EMSCRIPTEN__
    const unsigned char none = 1;
#else
    const unsigned char none = 0;
#endif

    section(app, ctx, "Windows",
            none ? "Real windows of the system's own, each with its own "
                   "renderer. A page in a browser has none, so these are off "
                   "here."
                 : "Real windows of the system's own, each with its own "
                   "renderer, fonts and Nuklear context, drawn by the same "
                   "widgets. A modal one keeps the main window from input "
                   "until it closes.");
    api(app, ctx, "reaktor_window_open  /  reaktor_window_close");

    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label    = "Open a window",
                .disabled = (unsigned char)(none || g_plain_win),
                .box      = { .flags = REAKTOR_LAY_FILL_X } }))
            showcase_open_window(app, 0);
        if (reaktor_button(&(reaktor_button_spec){
                .label    = "Open a modal window",
                .disabled = (unsigned char)(none || g_modal_win),
                .box      = { .flags = REAKTOR_LAY_FILL_X } }))
            showcase_open_window(app, 1);
    }
}

static void
tray_demo(App *app, struct reaktor_context *ctx)
{
    static reaktor_bool hide;
    reaktor_bool on = (reaktor_bool)showcase_tray_on();

    section(app, ctx, "Tray",
            on ? "The window's icon in the system's tray, with a menu: show "
                 "the window, send a toast, pick the theme, quit. Minimizing "
                 "can hide the window there instead of in the taskbar."
               : "The window's icon in the system's tray, with a menu. It is "
                 "off here, or the system has no tray.");
    api(app, ctx, "reaktor_tray_open  /  reaktor_tray_check  /  reaktor_tray_close"
                  "  /  reaktor_tray_hide_on_minimize");
    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X) {
        if (reaktor_check(&(reaktor_check_spec){ .label = "Icon in the tray",
                                                 .on = &on }))
            showcase_tray_show(app, on);
    }
    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X) {
        if (reaktor_check(&(reaktor_check_spec){
                .label = "Minimize to the tray", .on = &hide }))
            reaktor_tray_hide_on_minimize(app, hide);
    }
}

static void
closing_demo(App *app, struct reaktor_context *ctx)
{
    static reaktor_bool ask = 1;

    section(app, ctx, "Closing",
            "A close can ask first. The close button, the tray's Quit and a "
            "first Ctrl+C show the question in a modal floater, and only its "
            "Quit closes; the closing hook is asked before it.");
    api(app, ctx, "reaktor_set_confirm_close  /  .confirm_close");
    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X) {
        if (reaktor_check(&(reaktor_check_spec){ .label = "Ask before closing",
                                                 .on = &ask }))
            reaktor_set_confirm_close(app, ask ? SC_CLOSE_QUESTION : NULL);
    }
}

static void
undo_delete(App *app, void *user)
{
    (void)user;
    reaktor_toast(app, &(reaktor_toast_spec){ .text = "'Lorem ipsum' restored",
                                              .timeout_ms = 5000 });
}

static void
upload_controls(App *app, struct reaktor_context *ctx, void *user)
{
    reaktor_size *done = (reaktor_size *)user;

    reaktor_layout_row_dynamic(ctx, 26.0f, 1);
    reaktor_progress_bar(app, ctx, done, 100, REAKTOR_FIXED);
}

static reaktor_size g_uploaded = 60;

static const reaktor_toast_spec g_toast_icon = {
    .text = "Connected to the printer", .icon = "print-outline",
    .timeout_ms = 5000 };
static const reaktor_toast_spec g_toast_upload = {
    .text = "Uploading", .icon = "cloud-upload-outline",
    .content = upload_controls, .content_w = 140.0f, .user = &g_uploaded };
static const reaktor_toast_spec g_toast_undo = {
    .text = "'Lorem ipsum' deleted", .action = "Undo",
    .on_action = undo_delete, .timeout_ms = 5000 };

void
showcase_toasts(App *app)
{
    reaktor_toast(app, &g_toast_icon);
    reaktor_toast(app, &g_toast_upload);
    reaktor_toast(app, &g_toast_undo);
}

/* A page of what the Popups tab holds, laid out for paper. */
static void
print_sample(App *app, struct reaktor_context *ctx, int index, int w, int h, void *user)
{
    (void)app; (void)index; (void)user;
    reaktor_fill_rect(reaktor_window_get_canvas(ctx), reaktor_rect(0, 0, (float)w, (float)h),
                      0.0f, reaktor_rgb(255, 255, 255));
    REAKTOR_FREE(.w = (float)w, .h = (float)h) {
        REAKTOR_COLUMN(.w = (float)w - 96.0f, .gap = 10.0f, .ml = 48.0f, .mt = 48.0f) {
            reaktor_label(&(reaktor_label_spec){ .text = "A page from Reaktor", .style = "h4",
                                                 .color = "#000000" });
            reaktor_label(&(reaktor_label_spec){
                .text = "Laid out and drawn as the window draws it, then printed or "
                        "written to a PDF.", .wrap = 1, .color = "#333333",
                .box = { .flags = REAKTOR_LAY_FILL_X } });
            reaktor_button(&(reaktor_button_spec){ .label = "A button on paper",
                                                   .box = { .w = 180.0f } });
        }
    }
}

static void
print_done(App *app, int ok, void *user)
{
    (void)user;
    reaktor_toast(app, &(reaktor_toast_spec){ .text = ok ? "Printed" : "Not printed",
                                              .timeout_ms = 3000 });
}

static void
pdf_chosen(App *app, const char *const *paths, int count, void *user)
{
    (void)user;
    if (count)
        reaktor_print_pdf(app, paths[0], &(reaktor_print_spec){
            .title = "Reaktor", .pages = 1, .page = print_sample, .done = print_done });
}

static void
toast_demos(App *app, struct reaktor_context *ctx)
{
    section(app, ctx, "Toasts",
            "Messages at the bottom of the window, stacked newest last. Each "
            "holds text, an optional icon, an action, custom controls and a "
            "close button. One stays until it is closed, or goes by itself "
            "when it is given a timeout - five seconds here. A system "
            "notification leaves the window: a toast on Windows, the desktop's "
            "own on Linux, macOS and the BSDs, the browser's on the web.");
    api(app, ctx, "reaktor_toast  /  reaktor_notify");

    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Simple toast",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_toast(app, &(reaktor_toast_spec){ .text = "Saved",
                                                      .timeout_ms = 5000 });
        if (reaktor_button(&(reaktor_button_spec){
                .label = "With an action",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_toast(app, &g_toast_undo);
        if (reaktor_button(&(reaktor_button_spec){
                .label = "With an icon",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_toast(app, &g_toast_icon);
    }
    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "With custom controls",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_toast(app, &g_toast_upload);
        if (reaktor_button(&(reaktor_button_spec){
                .label = "With a long title",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_toast(app, &(reaktor_toast_spec){
                .text = "A toast with a title long enough to need the room "
                        "it is given", .action = "Details", .timeout_ms = 5000 });
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Until closed",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_toast(app, &(reaktor_toast_spec){
                .text = "This one stays until it is closed" });
    }
    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "System notification",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }) &&
            !reaktor_notify(app, &(reaktor_notification){
                .title = "Reaktor", .text = "A notification from outside the window" }))
            reaktor_toast(app, &(reaktor_toast_spec){
                .text = "This system has no notifications", .timeout_ms = 5000 });
    }
}

static void
print_demos(App *app, struct reaktor_context *ctx)
{
    section(app, ctx, "Printing",
            "A page is drawn as the window would draw it, at the printer's "
            "resolution, and goes to the system's print dialog on Windows and "
            "macOS, or opens as a PDF on Linux and the BSDs. The same pages can "
            "be written to a PDF file instead.");
    api(app, ctx, "reaktor_print  /  reaktor_print_pdf");

    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Print a page",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }) &&
            !reaktor_print(app, &(reaktor_print_spec){ .title = "Reaktor", .pages = 1,
                                                       .page = print_sample,
                                                       .done = print_done }))
            reaktor_toast(app, &(reaktor_toast_spec){
                .text = "This build does not print", .timeout_ms = 5000 });
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Export as PDF",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_file_dialog_open(app, &(reaktor_file_dialog){
                .kind = REAKTOR_FILE_SAVE, .chosen = pdf_chosen,
                .location = "exported.pdf",
                .filters = (const reaktor_file_filter[]){ { "PDF", "pdf" } },
                .filter_count = 1 });
    }
}

static reaktor_size *g_menu_progress;

static void
menu_progress(App *app, struct reaktor_context *ctx, void *user)
{
    (void)user;
    reaktor_progress_bar(app, ctx, g_menu_progress, 100, REAKTOR_MODIFIABLE);
}

static void
answered(App *app, int button, void *user)
{
    static const char *const said[3] = { "Kept the draft", "Discarded it",
                                         "Saved it" };

    (void)user;
    reaktor_toast(app, &(reaktor_toast_spec){
        .text = button >= 0 ? said[button] : "Closed without an answer",
        .timeout_ms = 3000 });
}

static void
reminded(App *app, void *user)
{
    (void)user;
    reaktor_toast(app, &(reaktor_toast_spec){ .text = "Two seconds are up",
                                              .timeout_ms = 3000 });
}

static void
ask_demo(App *app, struct reaktor_context *ctx)
{
    static const char *const buttons[3] = { "Cancel", "Discard", "Save" };

    section(app, ctx, "Questions and timers",
            "A question is a modal floater with a button per answer, the last "
            "one accented; it answers once, with -1 when it is closed some "
            "other way. A timer calls back on the main thread and draws a frame "
            "after, so the window idles between ticks; reaktor_post does the "
            "same for a call made on any other thread.");
    api(app, ctx, "reaktor_ask  /  reaktor_timer  /  reaktor_post");

    REAKTOR_ROW(.h = ROW, .gap = reaktor_item_spacing(ctx).x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Ask a question",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_ask(app, &(reaktor_question){
                .text = "Save the changes to this draft?",
                .buttons = buttons, .count = 3, .answered = answered });
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Remind me in two seconds",
                .box   = { .flags = REAKTOR_LAY_FILL_X } }))
            reaktor_timer(app, 2000, 0, reminded, NULL);
    }
}

static void
settings_demo(App *app, struct reaktor_context *ctx)
{
    static char note[64], said[96] = "Nothing saved this run.";
    static int  note_len = -1;
    const char *kept;

    if (note_len < 0) {
        kept = reaktor_setting("showcase.note");
        note_len = (int)SDL_strlcpy(note, kept ? kept : "", sizeof note);
        if (note_len >= (int)sizeof note) note_len = (int)sizeof note - 1;
    }
    section(app, ctx, "Settings",
            "A setting is a string kept between runs in settings.conf, an INI "
            "file in the user's preference folder; on the web the same file "
            "lives in the browser's IndexedDB. The application picks when it is written - never, on quit, "
            "on every change or after a delay - and can save or clear it at "
            "any moment.");
    api(app, ctx, "reaktor_setting_set  /  reaktor_settings_save  /  reaktor_settings_clear");

    REAKTOR_COLUMN(.name = "Settings", .flags = REAKTOR_LAY_FILL_X,
                   .gap = reaktor_item_spacing(ctx).y) {
        REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_FILL_X, .gap = reaktor_item_spacing(ctx).x) {
            reaktor_field(&(reaktor_field_spec){
                .buf = note, .len = &note_len, .cap = (int)sizeof note,
                .hint = "A note to keep", .name = "Note",
                .box = { .flags = REAKTOR_LAY_FILL_X } });
            if (reaktor_button(&(reaktor_button_spec){ .label = "Save",
                                                        .box = { .w = 96 } })) {
                note[note_len] = '\0';
                reaktor_setting_set("showcase.note", note_len ? note : NULL);
                reaktor_settings_save();
                SDL_strlcpy(said, "Saved: it is there on the next start.", sizeof said);
            }
            if (reaktor_button(&(reaktor_button_spec){ .label = "Clear",
                                                        .box = { .w = 96 } })) {
                reaktor_settings_clear();
                note_len = 0;
                SDL_strlcpy(said, "Cleared: settings.conf is gone.", sizeof said);
            }
        }
        reaktor_label(&(reaktor_label_spec){ .text = said, .name = "Settings status",
                                             .color = "--text-muted" });
    }
}

static const reaktor_file_filter g_sheet_filters[] = {
    { "Stylesheets", "css" },
    { "All files",   "*"   }
};

static void
file_chosen(App *app, const char *const *paths, int count, void *user)
{
    (void)user;
    sample_file_opened(app, count ? paths[0] : NULL);
}

void
page_popups(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    char line[SC_PATH_CAP + 32];
    struct reaktor_rect bar;
    int tip = 0;

    popup_style_push(ctx);
    REAKTOR_ROW(.name = "Menus", .h = 40.0f, .flags = REAKTOR_LAY_FILL_X) {
        static const reaktor_menu_entry file[] = {
            { .label = "New",     .icon = "add-outline",         .keys = "Ctrl+N" },
            { .label = "Open...", .icon = "folder-open-outline", .keys = "Ctrl+O" },
            { NULL },
            { .label = "Close",   .icon = "close-outline",       .keys = "Ctrl+W" }
        };
        static const reaktor_menu_entry zoom[] = {
            { .label = "Zoom in",  .keys = "Ctrl++" },
            { .label = "Zoom out", .keys = "Ctrl+-" },
            { NULL },
            { .label = "Actual size", .keys = "Ctrl+0" }
        };
        reaktor_menu_entry edit[] = {
            { .label = "Cut",   .keys = "Ctrl+X" },
            { .label = "Copy",  .keys = "Ctrl+C" },
            { .label = "Paste", .keys = "Ctrl+V", .disabled = 1 },
            { NULL },
            { .label = "Overwrite", .checked = &s->check_spell }
        };
        reaktor_menu_entry view[] = {
            { .custom = menu_progress },
            { .label = "Reset progress" },
            { NULL },
            { .label = "Zoom", .items = zoom, .count = (int)SDL_arraysize(zoom) },
            { .label = "Full screen", .disabled = 1 }
        };
        const reaktor_menu_entry *pe = NULL;
        reaktor_menu menus[3] = {
            { "File", file, (int)SDL_arraysize(file) },
            { "Edit", edit, (int)SDL_arraysize(edit) },
            { "View", view, (int)SDL_arraysize(view) }
        };
        int pm = -1, pi = -1;

        g_menu_progress = &s->progress;

        if (reaktor_box_rect(&bar))
            reaktor_edge_round(app, reaktor_window_get_canvas(ctx), bar,
                               ctx->style.window.rounding, 1.0f,
                               ctx->style.window.border_color);
        if (reaktor_menubar(&(reaktor_menubar_spec){
                .menus = menus, .count = 3, .name = "Menu bar",
                .box = { .ml = 12.0f, .mt = 7.0f },
                .picked_menu = &pm, .picked_item = &pi, .picked = &pe }) && pe) {
            SDL_snprintf(s->menu_pick, sizeof(s->menu_pick), "%s > %s",
                         menus[pm].label, pe->label);
            if (pm == 0 && pi == 1 &&
                reaktor_file_dialog_open(app, &(reaktor_file_dialog){
                    .filters = g_sheet_filters,
                    .filter_count = (int)SDL_arraysize(g_sheet_filters),
                    .chosen = file_chosen }))
                SDL_strlcpy(s->file_pick, "waiting for the picker...",
                            sizeof(s->file_pick));
            if (pe == &view[1]) s->progress = 50;
        }
    }
    popup_style_pop(ctx);

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
    head_and_note(app, ctx, "Menus",
            "The bar above is declared: menus of items, each with an optional "
            "icon, a shortcut shown at the right, a check and a disabled state, "
            "separators, submenus, and rows that hold a widget of their own - "
            "the View menu's progress bar. Tab reaches the titles, Enter opens "
            "one and moves into it, Up and Down walk its items, Right opens a "
            "submenu or the next menu, Left goes back, and Escape closes; "
            "screen readers see a menu bar, menus and items.");
    api(app, ctx, "reaktor_menubar  /  reaktor_menu  /  reaktor_menu_entry");

    {
        reaktor_style_push_font(ctx, reaktor_style_font(app, ".small", 13, 0));
        REAKTOR_COLUMN(.gap = reaktor_item_spacing(ctx).y) {
            SDL_snprintf(line, sizeof(line), "last chosen: %s", s->menu_pick);
            reaktor_label(&(reaktor_label_spec){
                .text   = line,
                .name   = "Last chosen",
                .value  = s->menu_pick,
                .color = "--text-muted",
                .box    = { .h = ROW_SMALL,
                            .flags = REAKTOR_LAY_FILL_X } });

            SDL_snprintf(line, sizeof(line), "file picker: %s", s->file_pick);
            reaktor_label(&(reaktor_label_spec){
                .text   = line,
                .name   = "File picker",
                .value  = s->file_pick,
                .color = "--text-muted",
                .box    = { .h = ROW_SMALL,
                            .flags = REAKTOR_LAY_FILL_X } });
        }
        reaktor_style_pop_font(ctx);
    }

    section(app, ctx, "Context menu",
            "The same popup, opened by a right-click inside a rect the caller "
            "names. The login field on the first tab uses one for cut, copy "
            "and paste.");
    api(app, ctx, "reaktor_contextual_begin");

    popup_style_push(ctx);
    {
        struct reaktor_rect trigger = reaktor_rect(0, 0, 0, 0);

        REAKTOR_ROW(.h = 54.0f) {
            reaktor_box_rect(&trigger);
            reaktor_button(&(reaktor_button_spec){
                .label = "Right-click anywhere on this button",
                .box   = { .flags = REAKTOR_LAY_FILL } });
        }
        if (reaktor_contextual_begin(ctx, REAKTOR_WINDOW_BORDER, reaktor_vec2(150.0f, reaktor_menu_height(3)),
                                     trigger)) {
            reaktor_menu_style_push(ctx);
            reaktor_menu_edge(app, ctx);
            reaktor_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
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
            reaktor_contextual_end(ctx);
            reaktor_menu_style_pop(ctx);
        }
    }
    popup_style_pop(ctx);

    section(app, ctx, "Tooltips",
            "Positioned against the pointer rather than against a widget, and "
            "emitted only while the thing it describes is hovered - so it costs "
            "nothing on the frames where it is not showing. A widget's .tooltip "
            "is drawn over everything from the sheet's .tooltip rule; one with "
            "more than a line in it is the page's own drawing, rather than "
            "reaktor_tooltip: that opens a REAKTOR_POPUP_DYNAMIC panel, which "
            "does not fill its body when it begins and strokes its border at "
            "bounds sized before the content shrank them, leaving a frame "
            "standing off the box that no style value moves.");
    api(app, ctx, ".tooltip  /  draw_tooltip");

    popup_style_push(ctx);
    {
        struct reaktor_rect b;

        REAKTOR_ROW(.h = 34.0f,
                    .gap = reaktor_item_spacing(ctx).x) {
            reaktor_button(&(reaktor_button_spec){
                .label   = "Hover for a plain tooltip",
                .tooltip = "A one-line tooltip.",
                .box     = { .flags = REAKTOR_LAY_FILL } });

            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL) {
                b = reaktor_rect(0, 0, 0, 0);
                reaktor_box_rect(&b);
                reaktor_hot_follow(app, b, 1);
                ctx->last_widget_state = 0;
                reaktor_button(&(reaktor_button_spec){
                    .label = "Hover for a laid-out one",
                    .box   = { .flags = REAKTOR_LAY_FILL } });
                if (ctx->last_widget_state & REAKTOR_WIDGET_STATE_HOVER) tip = 1;
            }
        }
    }
    popup_style_pop(ctx);

    floater_demos(app, ctx);
    window_demos(app, ctx);
    closing_demo(app, ctx);
    tray_demo(app, ctx);
    toast_demos(app, ctx);
    print_demos(app, ctx);
    ask_demo(app, ctx);
    settings_demo(app, ctx);

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);

    /* Last, so nothing the page draws after its button lands on top. */
    if (tip) {
        static const char *const many[2] = {
            "A tooltip is not limited to a line:",
            "this one carries a progress bar."
        };
        struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
        struct reaktor_rect keep = cv->clip;

        reaktor_push_scissor(cv, ctx->current->layout->clip);
        draw_tooltip(app, ctx, many, 2, (float)s->progress / 100.0f);
        reaktor_push_scissor(cv, keep);
    }
}
