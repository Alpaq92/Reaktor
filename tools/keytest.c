/* keytest.c - chord matching, exhaustively.
 *
 * The accessibility tree cannot check this: a keypress is not a frame, so the
 * golden dumps see nothing of it. This is what stands in for them.
 */
#include <stdio.h>
#include <string.h>

#include "keys.h"

static int g_fail;

static void
ok(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("  %-52s got %d, wanted %d   FAILED\n", what, got, want);
    } else {
        printf("  %-52s ok\n", what);
    }
}

static SDL_Event
press(SDL_Keycode key, SDL_Keymod mod)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.mod = mod;
    return e;
}

static void
test_chord(void)
{
    SDL_Event e;

    puts("chords match exactly");
    e = press(SDLK_TAB, SDL_KMOD_LCTRL);
    ok("Ctrl+Tab answers a Ctrl+Tab binding",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_TAB), 1);
    ok("...and not a plain Tab binding",
       reaktor_chord(&e, 0, SDLK_TAB), 0);
    ok("...and not a Ctrl+Shift+Tab binding",
       reaktor_chord(&e, REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT, SDLK_TAB), 0);
    ok("...and not a binding on another key",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_F1), 0);

    /* The bug in the obvious test: `mod & SDL_KMOD_CTRL` is true here. */
    e = press(SDLK_TAB, (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT));
    ok("Ctrl+Alt+Tab does NOT answer Ctrl+Tab",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_TAB), 0);
    ok("Ctrl+Alt+Tab answers Ctrl+Alt+Tab",
       reaktor_chord(&e, REAKTOR_MOD_CTRL | REAKTOR_MOD_ALT, SDLK_TAB), 1);

    puts("");
    puts("locks say nothing about what was pressed");
    e = press(SDLK_TAB, (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_CAPS));
    ok("Caps Lock does not spoil Ctrl+Tab",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_TAB), 1);
    e = press(SDLK_TAB, (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_NUM |
                                     SDL_KMOD_SCROLL | SDL_KMOD_MODE));
    ok("nor Num Lock, Scroll Lock or AltGr",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_TAB), 1);

    puts("");
    puts("left and right of a modifier are the same modifier");
    e = press(SDLK_TAB, SDL_KMOD_RCTRL);
    ok("right Ctrl answers Ctrl+Tab",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_TAB), 1);
    e = press(SDLK_TAB, SDL_KMOD_RGUI);
    ok("right Cmd answers Cmd+Tab",
       reaktor_chord(&e, REAKTOR_MOD_CMD, SDLK_TAB), 1);
    ok("...and Cmd is not Ctrl",
       reaktor_chord(&e, REAKTOR_MOD_CTRL, SDLK_TAB), 0);

    puts("");
    puts("an unmodified key");
    e = press(SDLK_F1, SDL_KMOD_NONE);
    ok("F1 answers a plain F1 binding", reaktor_chord(&e, 0, SDLK_F1), 1);
    e = press(SDLK_F1, SDL_KMOD_LCTRL);
    ok("Ctrl+F1 does not", reaktor_chord(&e, 0, SDLK_F1), 0);

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_UP;
    e.key.key = SDLK_F1;
    ok("a key release is not a press", reaktor_chord(&e, 0, SDLK_F1), 0);
    ok("a null event matches nothing", reaktor_chord(NULL, 0, SDLK_F1), 0);
}

static void
test_table(void)
{
    enum { ACT_NEXT = 1, ACT_PREV, ACT_GOTO, ACT_DIAG };
    static const reaktor_shortcut T[] = {
        { 0,                                     SDLK_F1,  0,        ACT_DIAG, "Diagnostics" },
        { REAKTOR_MOD_CTRL,                      SDLK_TAB, 0,        ACT_NEXT, "Next page" },
        { REAKTOR_MOD_CMD,                       SDLK_TAB, 0,        ACT_NEXT, "Next page" },
        { REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT,  SDLK_TAB, 0,        ACT_PREV, "Previous page" },
        { REAKTOR_MOD_CTRL,                      SDLK_1,   SDLK_1+6, ACT_GOTO, "Go to page" }
    };
    const int N = (int)(sizeof(T) / sizeof(T[0]));
    SDL_Event e;
    int i, idx;

    puts("");
    puts("a table of bindings");
    e = press(SDLK_TAB, SDL_KMOD_LCTRL);
    i = reaktor_shortcut_match(T, N, &e, &idx);
    ok("Ctrl+Tab finds the next-page row", i >= 0 ? T[i].action : 0, ACT_NEXT);

    e = press(SDLK_TAB, SDL_KMOD_LGUI);
    i = reaktor_shortcut_match(T, N, &e, &idx);
    ok("Cmd+Tab finds the same action by another row",
       i >= 0 ? T[i].action : 0, ACT_NEXT);

    e = press(SDLK_TAB, (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT));
    i = reaktor_shortcut_match(T, N, &e, &idx);
    ok("Ctrl+Shift+Tab finds previous-page", i >= 0 ? T[i].action : 0, ACT_PREV);

    puts("");
    puts("a row covering a run of keys");
    e = press(SDLK_3, SDL_KMOD_LCTRL);
    i = reaktor_shortcut_match(T, N, &e, &idx);
    ok("Ctrl+3 finds go-to-page", i >= 0 ? T[i].action : 0, ACT_GOTO);
    ok("...and reports how far into the run it fell", idx, 2);

    e = press(SDLK_1, SDL_KMOD_LCTRL);
    reaktor_shortcut_match(T, N, &e, &idx);
    ok("Ctrl+1 is the start of the run", idx, 0);

    e = press(SDLK_8, SDL_KMOD_LCTRL);
    ok("Ctrl+8 is past the end and matches nothing",
       reaktor_shortcut_match(T, N, &e, &idx), -1);

    e = press(SDLK_TAB, SDL_KMOD_LALT);
    ok("Alt+Tab is not in the table", reaktor_shortcut_match(T, N, &e, &idx), -1);
}

static void
text(const char *what, unsigned mods, SDL_Keycode key, const char *want)
{
    char buf[64];
    reaktor_chord_text(mods, key, buf, (int)sizeof(buf));
    if (strcmp(buf, want) != 0) {
        g_fail++;
        printf("  %-52s got \"%s\", wanted \"%s\"   FAILED\n", what, buf, want);
    } else {
        printf("  %-52s \"%s\"\n", what, buf);
    }
}

static void
test_text(void)
{
    char buf[4];

    puts("");
    puts("written the way ARIA writes it");
    text("plain key", 0, SDLK_F1, "F1");
    text("one modifier", REAKTOR_MOD_CTRL, SDLK_TAB, "Control+Tab");
    text("two, in a fixed order",
         REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT, SDLK_TAB, "Control+Shift+Tab");
    text("Cmd is Meta on the web", REAKTOR_MOD_CMD, SDLK_TAB, "Meta+Tab");
    text("all four",
         REAKTOR_MOD_CTRL | REAKTOR_MOD_ALT | REAKTOR_MOD_SHIFT | REAKTOR_MOD_CMD,
         SDLK_A, "Control+Meta+Alt+Shift+A");
    /* The same action on two platforms must not read as two chords: the
     * command modifier leads in both. */
    text("Ctrl and Shift", REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT, SDLK_TAB,
         "Control+Shift+Tab");
    text("...and Cmd and Shift, the same way round",
         REAKTOR_MOD_CMD | REAKTOR_MOD_SHIFT, SDLK_TAB, "Meta+Shift+Tab");
    text("Return is Enter", 0, SDLK_RETURN, "Enter");
    text("arrows are spelled out", REAKTOR_MOD_ALT, SDLK_UP, "Alt+ArrowUp");

    ok("a buffer too small writes nothing rather than half a chord",
       reaktor_chord_text(REAKTOR_MOD_CTRL, SDLK_TAB, buf, (int)sizeof(buf)), 0);
    ok("...and leaves it empty", (int)strlen(buf), 0);
}

enum { ACT_NEXT = 1, ACT_PREV, ACT_GOTO, ACT_DIAG };

static const reaktor_shortcut LIST[] = {
    { 0,                                     SDLK_F1,  0,        ACT_DIAG, "Diagnostics" },
    { REAKTOR_MOD_CTRL,                      SDLK_TAB, 0,        ACT_NEXT, "Next page" },
    { REAKTOR_MOD_CMD,                       SDLK_TAB, 0,        ACT_NEXT, "Next page" },
    { REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT,  SDLK_TAB, 0,        ACT_PREV, "Previous page" },
    { REAKTOR_MOD_CTRL,                      SDLK_1,   SDLK_1+6, ACT_GOTO, "Go to page" }
};
#define LIST_N ((int)(sizeof(LIST) / sizeof(LIST[0])))

static void
list(const char *what, int action, int index, const char *want)
{
    char buf[128];

    buf[0] = '\0';
    reaktor_shortcut_text(LIST, LIST_N, action, index, buf, (int)sizeof(buf));
    if (strcmp(buf, want) != 0) {
        g_fail++;
        printf("  %-52s got \"%s\", wanted \"%s\"   FAILED\n", what, buf, want);
    } else {
        printf("  %-52s \"%s\"\n", what, buf);
    }
}

static void
test_list(void)
{
    /* Two rows, one action: the shape a chord wanted on both Ctrl and Cmd
     * takes, and the one ARIA collapses into a single value. */
    static const reaktor_shortcut PAIR[] = {
        { REAKTOR_MOD_CTRL, SDLK_TAB, 0, ACT_NEXT, "Next page" },
        { REAKTOR_MOD_CMD,  SDLK_TAB, 0, ACT_NEXT, "Next page" }
    };
    char buf[128], small[12];
    int n;

    puts("");
    puts("the table read back, as ARIA writes a list");
    list("two rows, one action, one space-separated value",
         ACT_NEXT, 0, "Control+Tab Meta+Tab");
    list("a modifier that only one row carries", ACT_PREV, 0, "Control+Shift+Tab");
    list("a row covering a run, at its start", ACT_GOTO, 0, "Control+1");
    list("...and three keys in", ACT_GOTO, 2, "Control+3");
    list("past the end of the run is not a chord", ACT_GOTO, 7, "");
    list("an action nothing is bound to", 99, 0, "");
    list("an index a single-key row cannot take", ACT_DIAG, 1, "");

    puts("");
    puts("composing two actions onto one node");
    buf[0] = '\0';
    n = reaktor_shortcut_text(PAIR, 2, ACT_NEXT, 0,buf, (int)sizeof(buf));
    ok("appending to an empty string writes both", n, 20);
    n = reaktor_shortcut_text(PAIR, 2, ACT_NEXT, 0,buf, (int)sizeof(buf));
    ok("...and appending again adds to it, not over it", n, 41);

    small[0] = '\0';
    reaktor_shortcut_text(PAIR, 2, ACT_NEXT, 0,small, (int)sizeof(small));
    ok("a chord that will not fit is left out whole",
       (int)strlen(small), 11);
    ok("...leaving what did fit", strcmp(small, "Control+Tab"), 0);

    text("a page key spelled ARIA's way", 0, SDLK_PAGEUP, "PageUp");
    text("and its neighbour", 0, SDLK_PAGEDOWN, "PageDown");
}

int
main(void)
{
    test_chord();
    test_table();
    test_text();
    test_list();
    printf("\n%s\n", g_fail ? "FAILED" : "keys: all checks passed");
    return g_fail ? 1 : 0;
}
