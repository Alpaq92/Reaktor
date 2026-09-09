/* keys.h - matching a keyboard chord, and saying what it is.
 *
 * The mechanism is here; which keys do what is the application's business and
 * belongs in the application. A binding is a row in a table rather than a
 * branch in an event switch for one reason above tidiness: a table can be read
 * back. ARIA has carried aria-keyshortcuts and UI Automation AcceleratorKey
 * for exactly that, so a screen-reader user can discover what the keyboard
 * offers; a branch buried in a switch can be announced to nobody.
 */
#ifndef REAKTOR_KEYS_H
#define REAKTOR_KEYS_H

#include <SDL3/SDL.h>

/* Ctrl and Cmd are separate keys and stay separate: nothing here quietly means
 * one on one platform and the other elsewhere. An action wanted on both simply
 * takes two rows, which is also how it reaches a reader - ARIA writes several
 * chords for one action as a list. */
enum {
    REAKTOR_MOD_SHIFT = 1u << 0,
    REAKTOR_MOD_CTRL  = 1u << 1,
    REAKTOR_MOD_ALT   = 1u << 2,
    REAKTOR_MOD_CMD   = 1u << 3    /* Command, Super, or the Windows key */
};

/* One binding. `key_last` gives a run of keys - Ctrl+1 through Ctrl+7 is one
 * row, not seven - and is 0 for a single key. `action` is the application's
 * own enum; this file never looks at it. `name` is what the chord does, in
 * the words a reader should hear. */
typedef struct reaktor_shortcut {
    unsigned     mods;
    SDL_Keycode  key, key_last;
    int          action;
    const char  *name;
} reaktor_shortcut;

/* Does this event press exactly this chord?
 *
 * Exactly: Ctrl+Alt+Tab does not answer a Ctrl+Tab binding, which is the bug
 * in the obvious `mod & SDL_KMOD_CTRL` test. Caps Lock, Num Lock, Scroll Lock
 * and AltGr are ignored - they say nothing about what the user pressed. */
int reaktor_chord(const SDL_Event *e, unsigned mods, SDL_Keycode key);

/* The first row of `table` the event matches, or -1. For a row with a key
 * range, `*index` receives how far into that range the key fell, so an action
 * shared by several keys can tell them apart; otherwise it receives 0. */
int reaktor_shortcut_match(const reaktor_shortcut *table, int count,
                           const SDL_Event *e, int *index);

/* The chord as ARIA writes it - "Control+Shift+Tab" - into `out`. Answers the
 * length written, or 0 if it would not fit. This is what aria-keyshortcuts
 * takes verbatim and what UI Automation's AcceleratorKey wants. */
int reaktor_chord_text(unsigned mods, SDL_Keycode key, char *out, int cap);

/* Every chord bound to `action`, in one string, the way ARIA writes a list:
 * space-separated, because one action commonly has several - Ctrl+Tab and
 * Cmd+Tab are two rows here and one value there.
 *
 * It appends, so composing two actions onto one node is two calls; `out` must
 * already be a string, empty for the first. `index` picks one key out of a row
 * covering a run - page 3 of Ctrl+1..7 - and is 0 for a row that covers one
 * key. Answers the length of `out` afterwards. */
int reaktor_shortcut_text(const reaktor_shortcut *table, int count,
                          int action, int index, char *out, int cap);

#endif /* REAKTOR_KEYS_H */
