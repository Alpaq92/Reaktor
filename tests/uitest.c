#include "reaktor/reaktor.h"
#include "reaktor/main.h"

static char         g_name[64];
static int          g_name_len;
static int          g_count;
static reaktor_bool g_loud;
static char         g_greeting[96] = "Nobody greeted yet";

static void
greet(void)
{
    SDL_snprintf(g_greeting, sizeof g_greeting, g_loud ? "HELLO, %.*s!" : "Hello, %.*s",
                 g_name_len, g_name);
}

static void
page(App *app, struct reaktor_context *ctx, int win_w, int win_h)
{
    char count[32];

    (void)app; (void)ctx; (void)win_h;
    SDL_snprintf(count, sizeof count, "%d", g_count);
    REAKTOR_COLUMN(.w = (float)win_w, .gap = 8.0f) {
        reaktor_field(&(reaktor_field_spec){
            .buf = g_name, .len = &g_name_len, .cap = sizeof g_name, .name = "Name",
            .box = { .w = 240.0f } });
        reaktor_check(&(reaktor_check_spec){ .label = "Loud", .on = &g_loud });
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Greet", .keys = "Ctrl+G", .fit_content = 1 }))
            greet();
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Add one", .icon = "add-outline", .fit_content = 1,
                .box = { .mark = "add" } }))
            g_count++;
        reaktor_label(&(reaktor_label_spec){ .text = g_greeting, .name = "Greeting",
                                             .value = g_greeting });
        reaktor_label(&(reaktor_label_spec){ .text = count, .name = "Count", .value = count,
                                             .box = { .mark = "count" } });
    }
}

static int
on_key(App *app, const SDL_Event *e)
{
    (void)app;
    if (!reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_G)) return 0;
    greet();
    return 1;
}

static void
script(reaktor_test *t, void *user)
{
    char v[96];

    (void)user;
    reaktor_test_check(t, reaktor_test_find(t, "Greet"), "a button is found by its name");
    reaktor_test_check(t, reaktor_test_find(t, "#add"), "  and by its mark");
    reaktor_test_check(t, !reaktor_test_find(t, "Nothing like this"), "  and a missing one is not");

    reaktor_test_click(t, "#add");
    reaktor_test_click(t, "Add one");
    reaktor_test_check(t, reaktor_test_value(t, "#count", v, sizeof v) && SDL_strcmp(v, "2") == 0,
                       "two clicks count two");

    reaktor_test_type(t, "Name", "Ada");
    reaktor_test_click(t, "Greet");
    reaktor_test_check(t, reaktor_test_value(t, "Greeting", v, sizeof v) &&
                              SDL_strcmp(v, "Hello, Ada") == 0,
                       "typed text reaches the field and the click reads it");

    reaktor_test_click(t, "Loud");
    reaktor_test_check(t, (reaktor_test_state(t, "Loud") & REAKTOR_A11Y_CHECKED) != 0,
                       "a click checks a box");
    reaktor_test_key(t, "Ctrl+G");
    reaktor_test_check(t, reaktor_test_value(t, "Greeting", v, sizeof v) &&
                              SDL_strcmp(v, "HELLO, Ada!") == 0,
                       "a shortcut reaches the key hook");
}

int
main(int argc, char **argv)
{
    return reaktor_test_app(argc, argv, &(reaktor_launch){
        .name = "UI test", .window = { .w = 400, .h = 320 }, .page = page, .key = on_key },
        script, NULL);
}
