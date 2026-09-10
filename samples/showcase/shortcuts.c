#include "internal.h"
#include "showcase.h"
#include "keys.h"

enum {
    ACT_NEXT_PAGE = 1,
    ACT_PREV_PAGE,
    ACT_GOTO_PAGE,
    ACT_DIAGNOSTICS
};

static const reaktor_shortcut SHORTCUTS[] = {
    { 0,                                       SDLK_F1,    0,          ACT_DIAGNOSTICS, "Diagnostics" },
    { REAKTOR_MOD_CTRL,                        SDLK_TAB,   0,          ACT_NEXT_PAGE,   "Next page" },
    { REAKTOR_MOD_CMD  | REAKTOR_MOD_ALT,      SDLK_RIGHT, 0,          ACT_NEXT_PAGE,   "Next page" },
    { REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT,    SDLK_TAB,   0,          ACT_PREV_PAGE,   "Previous page" },
    { REAKTOR_MOD_CMD  | REAKTOR_MOD_ALT,      SDLK_LEFT,  0,          ACT_PREV_PAGE,   "Previous page" },
    { REAKTOR_MOD_CTRL,                        SDLK_1,     SDLK_1 + 8, ACT_GOTO_PAGE,   "Go to page" },
    { REAKTOR_MOD_CMD,                         SDLK_1,     SDLK_1 + 8, ACT_GOTO_PAGE,   "Go to page" }
};
#define SHORTCUT_N ((int)(sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0])))

void
sample_tab_keys(int tab, char *out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = '\0';
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
