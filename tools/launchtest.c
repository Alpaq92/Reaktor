#include "internal.h"
#include "declare.h"
#include "reaktor/launch.h"
#include "reaktor/main.h"

static const char *g_mode = "";
static const char *g_log;
static int         g_closings, g_frames;

static void
note(const char *what)
{
    FILE *f;

    if (!g_log) return;
    f = fopen(g_log, "a");
    if (!f) return;
    fprintf(f, "%s\n", what);
    fclose(f);
}

static int
is(const char *mode)
{
    return SDL_strcmp(g_mode, mode) == 0;
}

static int            g_first, g_second;
static struct nk_rect g_field, g_button, g_link, g_combo;

static void push_key(App *app, SDL_EventType type, SDL_Keycode key);
static void push_text(App *app, const char *text);

static void
push_mouse(App *app, SDL_EventType type, float x, float y)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.windowID = SDL_GetWindowID(app->win);
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.windowID = SDL_GetWindowID(app->win);
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
    }
    SDL_PushEvent(&e);
}

static void
push_click(App *app, float x, float y)
{
    push_mouse(app, SDL_EVENT_MOUSE_MOTION, x, y);
    push_mouse(app, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
    push_mouse(app, SDL_EVENT_MOUSE_BUTTON_UP, x, y);
}

static void
push_tap(App *app, SDL_Keycode key)
{
    push_key(app, SDL_EVENT_KEY_DOWN, key);
    push_key(app, SDL_EVENT_KEY_UP, key);
}

static void
floater_body(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    static int clicked;
    char line[32];

    (void)w;
    if (SDL_strcmp((const char *)user, "Cover") == 0 && !clicked &&
        nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT,
                                        nk_window_get_bounds(ctx))) {
        clicked = 1;
        note("clicked Cover");
    }
    nk_layout_row_dynamic(ctx, h > 120 ? 120.0f : 28.0f, 1);
    if (reaktor_button_label(app, ctx, (const char *)user)) {
        SDL_snprintf(line, sizeof line, "pressed %s", (const char *)user);
        note(line);
    }
}

static char g_modal_buf[32];

static int floater(App *app, const char *name, float w, float h, int modal);

static void
keys_body(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    (void)app; (void)w; (void)h; (void)user;
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_label(ctx, "keys", NK_TEXT_LEFT);
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER)) note("Keys saw Enter");
}

static void
opener_body(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    (void)w; (void)h; (void)user;
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    if (reaktor_button_label(app, ctx, "Open")) floater(app, "M2", 140.0f, 60.0f, 1);
    if (reaktor_button_label(app, ctx, "Next")) note("pressed Next");
}

static void
slider_body(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    static float val, last;
    unsigned id;

    (void)w; (void)h; (void)user;
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    id = reaktor_note_here(app, ctx, REAKTOR_A11Y_SLIDER, "floater slider", 0);
    reaktor_slider_bar(app, ctx, id, &val, 0.0f, 10.0f, 1.0f);
    if (val != last) note("floater slider moved");
    last = val;
}

static void
field_body(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    static int first = 1;

    (void)app; (void)w; (void)h; (void)user;
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    if (first) nk_edit_focus(ctx, 0);
    first = 0;
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, g_modal_buf,
                                   sizeof g_modal_buf, nk_filter_default);
}

static int
floater(App *app, const char *name, float w, float h, int modal)
{
    return reaktor_floater_open(app, &(reaktor_floater){
        .title = name, .w = w, .h = h, .modal = modal,
        .body = SDL_strcmp(name, "Field") == 0 ? field_body
              : SDL_strcmp(name, "Slider") == 0 ? slider_body
              : SDL_strcmp(name, "Keys") == 0 ? keys_body
              : SDL_strcmp(name, "Opener") == 0 ? opener_body : floater_body,
        .user = (void *)name });
}

static const char *
when(void)
{
    return reaktor_floaters_modal() ? "under the modal" : "after it";
}

static void
said(const char *what)
{
    char line[64];

    SDL_snprintf(line, sizeof line, "%s %s", what, when());
    note(line);
}

static void
hold_page(App *app, struct nk_context *ctx, int w, int h)
{
    static char buf[32];
    static int  active;
    static struct nk_rect item[2];
    static float val, last;
    static int was;
    char line[64];
    unsigned id;
    int open;

    nk_layout_row_static(ctx, 28.0f, 140, 1);
    if (is("hold-combo-other")) {
        reaktor_note_here(app, ctx, REAKTOR_A11Y_COMBOBOX, "X", 0);
        if (nk_combo_begin_label(ctx, "X", nk_vec2(140.0f, 60.0f))) {
            static int seen;

            if (!seen) note("X opened");
            seen = 1;
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            nk_combo_item_label(ctx, "x", NK_TEXT_LEFT);
            nk_combo_end(ctx);
        }
    }
    g_field = nk_widget_bounds(ctx);
    if ((is("hold-toast-page") || is("hold-fresh-key")) && g_frames == 1)
        nk_edit_focus(ctx, 0);
    if (is("hold-tab-field") || is("hold-tab-free"))
        reaktor_note_here(app, ctx, REAKTOR_A11Y_TEXTBOX, "noted field", 0);
    {
        nk_flags st = nk_edit_string_zero_terminated(
            ctx, (nk_flags)NK_EDIT_FIELD | (is("hold-fresh-key") ? (nk_flags)NK_EDIT_SIG_ENTER : 0),
            buf, sizeof buf, nk_filter_default);

        if ((st & NK_EDIT_ACTIVE) && !active && !is("hold-toast-page") &&
            !is("hold-fresh-key")) {
            active = 1;
            said("field active");
        }
        if (is("hold-fresh-key") && (st & NK_EDIT_COMMITED)) {
            note("committed");
            g_second = floater(app, "Keys", 120.0f, 60.0f, 1);
        }
    }
    g_button = nk_widget_bounds(ctx);
    if (reaktor_button_label(app, ctx, "Page")) {
        said("pressed Page");
        if (is("hold-fresh")) g_second = floater(app, "Modal", 400.0f, 400.0f, 1);
        if (is("hold-fresh-tab")) g_second = floater(app, "Modal", 100.0f, 50.0f, 1);
    }
    g_link = nk_widget_bounds(ctx);
    if (reaktor_link_label(app, ctx, "link", 0)) said("pressed link");
    g_combo = nk_widget_bounds(ctx);
    if (is("hold-combo-keys"))
        reaktor_note_here(app, ctx, REAKTOR_A11Y_COMBOBOX, "noted combo", 0);
    open = 0;
    if (nk_combo_begin_label(ctx, "combo", nk_vec2(140.0f, 80.0f))) {
        open = 1;
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        item[0] = nk_widget_bounds(ctx);
        if (nk_combo_item_label(ctx, "first", NK_TEXT_LEFT)) said("picked first");
        item[1] = nk_widget_bounds(ctx);
        if (nk_combo_item_label(ctx, "second", NK_TEXT_LEFT)) said("picked second");
        nk_combo_end(ctx);
    }
    if ((is("hold-combo-keys") || is("hold-combo-other")) && open != was) {
        note(open ? "combo opened" : "combo closed");
        was = open;
    }
    if (reaktor_link_label(app, ctx, "below", 0)) said("pressed below");
    id = reaktor_note_here(app, ctx, REAKTOR_A11Y_SLIDER, "slider", 0);
    reaktor_slider_bar(app, ctx, id, &val, 0.0f, 10.0f, 1.0f);
    if (val != last) said("slider moved");
    last = val;

    if (is("hold-press")) {
        if (g_frames == 3 || g_frames == 14)
            push_mouse(app, SDL_EVENT_MOUSE_MOTION, g_button.x + 20.0f,
                       g_button.y + g_button.h * 0.5f);
        if (g_frames == 4 || g_frames == 17)
            push_click(app, g_link.x + 10.0f, g_link.y + g_link.h * 0.5f);
        if (g_frames == 7 || g_frames == 17) {
            SDL_snprintf(line, sizeof line, "cursor %d", app->want_cursor);
            note(line);
        }
        if (g_frames == 7 || g_frames == 12)
            push_click(app, g_field.x + 20.0f, g_field.y + g_field.h * 0.5f);
        if (g_frames == 10) reaktor_floater_close(app, g_second);
    }
    if (is("hold-popup")) {
        if (g_frames == 3)
            push_click(app, g_combo.x + 20.0f, g_combo.y + g_combo.h * 0.5f);
        if (g_frames == 6) g_second = floater(app, "Modal", 100.0f, 50.0f, 1);
        if (g_frames == 9)
            push_click(app, item[0].x + 10.0f, item[0].y + item[0].h * 0.5f);
        if (g_frames == 12) reaktor_floater_close(app, g_second);
        if (g_frames == 15)
            push_click(app, item[1].x + 10.0f, item[1].y + item[1].h * 0.5f);
    }
    if (is("hold-raise") && g_frames == 3)
        push_click(app, (w - 280.0f) * 0.5f + 8.0f, (h - 160.0f) * 0.5f + 8.0f);
    if (is("hold-keys") && g_frames == 3) push_tap(app, SDLK_TAB);
    if (is("hold-keys") && g_frames == 6) push_tap(app, SDLK_RETURN);
    if (is("hold-reach") && (g_frames == 3 || g_frames == 6)) push_tap(app, SDLK_TAB);
    if (is("hold-reach") && g_frames == 9) push_tap(app, SDLK_RETURN);
    if (is("hold-click")) {
        if (g_frames == 3) push_tap(app, SDLK_TAB);
        if (g_frames == 6) g_second = floater(app, "Modal", 400.0f, 400.0f, 1);
        if (g_frames == 9) push_tap(app, SDLK_RETURN);
    }
    if (is("hold-step") || is("hold-cover") || is("hold-enter")) {
        int tabs = is("hold-step") ? 4 : is("hold-cover") ? 3 : 2;

        if (g_frames >= 3 && g_frames < 3 + 2 * tabs && (g_frames - 3) % 2 == 0)
            push_tap(app, SDLK_TAB);
        if (is("hold-step") && g_frames == 14)
            g_second = floater(app, "Modal", 100.0f, 50.0f, 1);
        if (is("hold-cover") && g_frames == 14)
            push_click(app, g_combo.x + 20.0f, g_combo.y + g_combo.h * 0.5f);
        if (g_frames == 17)
            push_tap(app, is("hold-step") ? SDLK_RIGHT : SDLK_RETURN);
        if (is("hold-step") && g_frames == 19) reaktor_floater_close(app, g_second);
        if (is("hold-step") && g_frames == 21) push_tap(app, SDLK_RIGHT);
    }
    if (is("hold-fresh-key") && g_frames == 3) push_tap(app, SDLK_RETURN);
    if (is("hold-combo-other")) {
        if (g_frames == 3) push_tap(app, SDLK_TAB);
        if (g_frames == 6)
            push_click(app, g_combo.x + 20.0f, g_combo.y + g_combo.h * 0.5f);
        if (g_frames == 10) push_tap(app, SDLK_RETURN);
    }
    if (is("hold-modal-tab")) {
        if (g_frames == 3) push_tap(app, SDLK_TAB);
        if (g_frames == 6) {
            push_tap(app, SDLK_RETURN);
            push_tap(app, SDLK_TAB);
        }
        if (g_frames == 12) push_tap(app, SDLK_RETURN);
    }
    if (is("hold-stack") && g_frames == 3) reaktor_floater_close(app, g_second);
    if (is("hold-stack") && g_frames == 6)
        push_click(app, (w - 140.0f) * 0.5f + 70.0f, (h - 60.0f) * 0.5f + 26.0f);
    if (is("hold-fresh-tab")) {
        if (g_frames == 3) push_tap(app, SDLK_TAB);
        if (g_frames == 6) {
            push_tap(app, SDLK_RETURN);
            push_tap(app, SDLK_TAB);
        }
        if (g_frames == 12) push_tap(app, SDLK_RETURN);
    }
    if ((is("hold-tab-field") || is("hold-tab-free")) && g_frames == 3)
        push_tap(app, SDLK_TAB);
    if (is("hold-combo-keys")) {
        if (g_frames == 3 || g_frames == 5 || g_frames == 7) push_tap(app, SDLK_TAB);
        if (g_frames == 10 || g_frames == 15) push_tap(app, SDLK_RETURN);
    }
    if (is("hold-step-floater")) {
        if (g_frames == 3) push_tap(app, SDLK_TAB);
        if (g_frames == 6) push_tap(app, SDLK_RIGHT);
    }
    if (is("hold-fresh") && g_frames == 3)
        push_click(app, g_button.x + g_button.w * 0.5f, g_button.y + g_button.h * 0.5f);
    if (is("hold-toast-floater") || is("hold-close-toast")) {
        int closing = is("hold-close-toast");

        if (g_frames == 2)
            push_click(app, g_field.x + 20.0f, g_field.y + g_field.h * 0.5f);
        if (g_frames == 5) push_text(app, "a");
        if (g_frames == 7)
            reaktor_toast(app, &(reaktor_toast_spec){ .text = "toast" });
        if (closing && g_frames == 9)
            g_second = floater(app, "Modal", 100.0f, 50.0f, 1);
        if (closing && g_frames == 11)
            push_mouse(app, SDL_EVENT_MOUSE_MOTION, g_field.x + 30.0f,
                       g_field.y + g_field.h * 0.5f);
        if (closing && g_frames == 13) reaktor_floater_close(app, g_second);
        if (g_frames == 16) push_text(app, "b");
        if (g_frames == 20) {
            SDL_snprintf(line, sizeof line, "typed '%s', text input %d", buf,
                         (int)SDL_TextInputActive(app->win));
            note(line);
        }
    }
    if (is("hold-toast") || is("hold-toast-page")) {
        if (g_frames == 2) push_mouse(app, SDL_EVENT_MOUSE_MOTION, 2.0f, 2.0f);
        if (g_frames == 4) push_text(app, "a");
        if (g_frames == 6)
            reaktor_toast(app, &(reaktor_toast_spec){ .text = "toast" });
        if (g_frames == 9) push_text(app, "b");
        if (g_frames == 13) {
            SDL_snprintf(line, sizeof line, "typed '%s', text input %d",
                         is("hold-toast") ? g_modal_buf : buf,
                         (int)SDL_TextInputActive(app->win));
            note(line);
        }
    }
    if (g_frames == 26) {
        char want[32];

        SDL_snprintf(want, sizeof want, "reaktor-floater-%d", g_second);
        SDL_snprintf(line, sizeof line, "top %s",
                     ctx->end && SDL_strcmp(ctx->end->name_string, want) == 0
                     ? "second" : "other");
        if (is("hold-raise")) note(line);
        reaktor_quit(app, 0);
    }
    reaktor_wake(app);
}

static char g_win_buf[64];

static void
window_page(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    static int frames;
    char line[96];

    (void)w; (void)h; (void)user;
    frames++;
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    if (frames == 1) nk_edit_focus(ctx, 0);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, g_win_buf,
                                   sizeof g_win_buf, nk_filter_default);
    if (frames == 3) {
        SDL_Event e;

        SDL_zero(e);
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.windowID = SDL_GetWindowID(app->win);
        e.text.text = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae"
                      "\xe3\x83\x86\xe3\x82\xad";
        SDL_PushEvent(&e);
    }
    if (frames == 10) {
        SDL_snprintf(line, sizeof line, "window text %d bytes, text input %d",
                     (int)SDL_strlen(g_win_buf), (int)SDL_TextInputActive(app->win));
        note(line);
        reaktor_quit(app, 0);
    }
    app->dirty = 1;
    reaktor_wake(app);
}

static void
page(App *app, struct nk_context *ctx, int w, int h)
{
    char line[80];

    g_frames++;
    if (SDL_strncmp(g_mode, "hold-", 5) == 0) {
        hold_page(app, ctx, w, h);
        return;
    }
    if (ctx->input.keyboard.text_len > 0) {
        SDL_snprintf(line, sizeof line, "text %.*s",
                     ctx->input.keyboard.text_len, ctx->input.keyboard.text);
        note(line);
    }
    if (is("title")) reaktor_set_title(app, "Renamed");
    REAKTOR_FREE(.w = (float)w, .h = (float)h) {
        reaktor_label(&(reaktor_label_spec){ .text = "launchtest",
                                             .name = "Mode" });
    }
}

static int SDLCALL
quit_later(void *app)
{
    SDL_Delay(300);
    reaktor_quit((App *)app, 7);
    return 0;
}

static int SDLCALL
request_twice(void *app)
{
    SDL_Delay(300);
    reaktor_request_quit((App *)app);
    reaktor_request_quit((App *)app);
    return 0;
}

static int SDLCALL
count_frames(void *app)
{
    char line[32];

    SDL_Delay(1500);
    SDL_snprintf(line, sizeof line, "frames %d", g_frames);
    note(line);
    reaktor_quit((App *)app, 0);
    return 0;
}

static void
push_key(App *app, SDL_EventType type, SDL_Keycode key)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = type;
    e.key.windowID = SDL_GetWindowID(app->win);
    e.key.key = key;
    e.key.down = type == SDL_EVENT_KEY_DOWN;
    SDL_PushEvent(&e);
}

static void
push_text(App *app, const char *text)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.windowID = SDL_GetWindowID(app->win);
    e.text.text = text;
    SDL_PushEvent(&e);
}

static int
start(App *app, int argc, char **argv)
{
    if (argc > 1) g_mode = argv[1];
    if (argc > 2) g_log = argv[2];
    note("start");
    if (is("start-fail")) return 5;
    if (is("quit") || is("twice"))
        SDL_DetachThread(SDL_CreateThread(quit_later, "quit", app));
    if (is("request")) SDL_DetachThread(SDL_CreateThread(request_twice, "request", app));
    if (is("title")) {
        reaktor_set_title(app, "Renamed");
        SDL_DetachThread(SDL_CreateThread(count_frames, "frames", app));
    }
    if (is("keyeat")) {
        push_key(app, SDL_EVENT_KEY_DOWN, SDLK_X);
        push_text(app, "x");
        push_key(app, SDL_EVENT_KEY_UP, SDLK_X);
        push_key(app, SDL_EVENT_KEY_DOWN, SDLK_Y);
        push_text(app, "y");
        push_key(app, SDL_EVENT_KEY_UP, SDLK_Y);
        SDL_DetachThread(SDL_CreateThread(count_frames, "frames", app));
    }
    if (is("hold-press")) g_second = floater(app, "Modal", 100.0f, 50.0f, 1);
    if (is("hold-raise")) {
        g_first  = floater(app, "Under", 280.0f, 160.0f, 0);
        g_second = floater(app, "Modal", 100.0f, 50.0f, 1);
    }
    if (is("hold-keys") || is("hold-stack")) {
        g_first  = floater(app, "Under", 140.0f, 60.0f, 1);
        g_second = floater(app, "Top", 140.0f, 60.0f, 1);
    }
    if (is("hold-tab-field")) g_second = floater(app, "Cover", 400.0f, 400.0f, 0);
    if (is("hold-modal-tab")) g_first = floater(app, "Opener", 160.0f, 110.0f, 1);
    if (is("hold-reach")) {
        g_first  = floater(app, "Modal", 140.0f, 60.0f, 1);
        g_second = floater(app, "Above", 140.0f, 60.0f, 0);
    }
    if (is("hold-toast")) g_second = floater(app, "Field", 160.0f, 70.0f, 1);
    if (is("hold-step-floater")) g_second = floater(app, "Slider", 160.0f, 70.0f, 1);
    if (is("hold-toast-floater")) g_second = floater(app, "Plain", 100.0f, 50.0f, 0);
    if (is("window-text"))
        reaktor_window_open(app, &(reaktor_window){
            .window = { .title = "text", .w = 240, .h = 80 },
            .page = window_page });
    return 0;
}

static int
key(App *app, const SDL_Event *e)
{
    (void)app;
    return is("keyeat") && e->key.key == SDLK_X;
}

static int
closing(App *app, reaktor_quit_reason why)
{
    char line[64];

    (void)app;
    SDL_snprintf(line, sizeof line, "closing %d", (int)why);
    note(line);
    return (is("veto") || is("request")) && ++g_closings == 1;
}

static void
stop(App *app)
{
    (void)app;
    note("stop");
}

static const char red_css[] = ":root { --background-body: #ff0000; }\n";

int
main(int argc, char **argv)
{
    reaktor_launch l = {
        .name    = "launchtest",
        .window  = { .w = 320, .h = 200, .min_w = 300, .min_h = 180 },
        .css     = { { .name = "red.css", .data = red_css,
                       .size = sizeof red_css - 1 } },
        .page    = page,
        .start   = start,
        .key     = key,
        .closing = closing,
        .stop    = stop };
    int rc = launchApp(argc, argv, &l);

    if (is("twice")) {
        char line[32];

        SDL_snprintf(line, sizeof line, "again %d", launchApp(argc, argv, &l));
        note(line);
    }
    return rc;
}
