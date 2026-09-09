/* shortcuts.c - which keys do what, in this application.
 *
 * The matching is core's (core/ui/keys.c); the bindings are here, because
 * nothing about Ctrl+Tab meaning "next page" is a fact about GUI libraries.
 *
 * A table rather than a run of ifs, for a reason beyond tidiness: a table can
 * be read back, and stage 05 of the plan hands it to the accessibility bridges
 * so a screen reader can announce what the keyboard offers. A branch inside an
 * event switch can be announced to nobody.
 *
 * Ctrl and Cmd each get their own row. Nothing here quietly means one on one
 * platform and the other elsewhere, so both work everywhere and the Windows
 * build exercises the same rows a Mac would.
 *
 * Cmd+Tab is not among them, and cannot be: macOS reserves it for the app
 * switcher and Windows reserves Win+Tab for Task View, so the key never
 * reaches the application on either. That was survivable while the table only
 * matched keys; it stopped being survivable once the table began feeding
 * screen readers, because announcing a chord the desktop eats first is worse
 * than announcing nothing. So the Cmd rows carry Cmd+Alt+Arrow instead - no
 * platform reserves it, arrows sit in the same place on every keyboard layout,
 * and it is already where a Mac user looks for the next tab. Cmd+1..7 stays as
 * it was: that one does reach the application.
 */
#include "internal.h"
#include "sample.h"
#include "keys.h"

enum {
    ACT_NEXT_PAGE = 1,
    ACT_PREV_PAGE,
    ACT_GOTO_PAGE,
    ACT_DIAGNOSTICS
};

static const reaktor_shortcut SHORTCUTS[] = {
    /* mods                                    key         key_last    action           name */
    { 0,                                       SDLK_F1,    0,          ACT_DIAGNOSTICS, "Diagnostics" },
    { REAKTOR_MOD_CTRL,                        SDLK_TAB,   0,          ACT_NEXT_PAGE,   "Next page" },
    { REAKTOR_MOD_CMD  | REAKTOR_MOD_ALT,      SDLK_RIGHT, 0,          ACT_NEXT_PAGE,   "Next page" },
    { REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT,    SDLK_TAB,   0,          ACT_PREV_PAGE,   "Previous page" },
    { REAKTOR_MOD_CMD  | REAKTOR_MOD_ALT,      SDLK_LEFT,  0,          ACT_PREV_PAGE,   "Previous page" },
    { REAKTOR_MOD_CTRL,                        SDLK_1,     SDLK_1 + 6, ACT_GOTO_PAGE,   "Go to page" },
    { REAKTOR_MOD_CMD,                         SDLK_1,     SDLK_1 + 6, ACT_GOTO_PAGE,   "Go to page" }
};
#define SHORTCUT_N ((int)(sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0])))

/* The other direction: the table read back rather than matched against. This
 * is the reason it is a table - a run of ifs has nothing to hand a reader. */
void
sample_tab_keys(int tab, char *out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = '\0';
    /* Diagnostics answers F1 as well as its own number, so it carries both. */
    if (tab == TAB_DIAG)
        reaktor_shortcut_text(SHORTCUTS, SHORTCUT_N, ACT_DIAGNOSTICS, 0,
                              out, cap);
    reaktor_shortcut_text(SHORTCUTS, SHORTCUT_N, ACT_GOTO_PAGE, tab, out, cap);
}

void
sample_tablist_keys(char *out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = '\0';
    reaktor_shortcut_text(SHORTCUTS, SHORTCUT_N, ACT_NEXT_PAGE, 0, out, cap);
    reaktor_shortcut_text(SHORTCUTS, SHORTCUT_N, ACT_PREV_PAGE, 0, out, cap);
}

int
sample_key(App *app, const SDL_Event *e)
{
    int index = 0;
    int row = reaktor_shortcut_match(SHORTCUTS, SHORTCUT_N, e, &index);

    if (row < 0) return 0;

    switch (SHORTCUTS[row].action) {
    case ACT_DIAGNOSTICS:
        set_tab(app, TAB_DIAG);
        return 1;
    case ACT_NEXT_PAGE:
        set_tab(app, (sample_tab() + 1) % TAB_COUNT);
        return 1;
    case ACT_PREV_PAGE:
        set_tab(app, (sample_tab() - 1 + TAB_COUNT) % TAB_COUNT);
        return 1;
    case ACT_GOTO_PAGE:
        if (index < TAB_COUNT) set_tab(app, index);
        return 1;
    default:
        return 0;
    }
}
