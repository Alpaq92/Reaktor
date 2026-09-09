/* keys.c - see keys.h. */
#include <stdio.h>
#include <string.h>

#include "keys.h"

/* Modifiers that say something about what the user pressed. Caps Lock, Num
 * Lock and Scroll Lock are states of the keyboard rather than keys being held,
 * and AltGr is a layout, so a binding must not be sensitive to any of them. */
#define MEANINGFUL (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)

static unsigned
ours(SDL_Keymod sdl)
{
    unsigned m = 0;
    if (sdl & SDL_KMOD_SHIFT) m |= REAKTOR_MOD_SHIFT;
    if (sdl & SDL_KMOD_CTRL)  m |= REAKTOR_MOD_CTRL;
    if (sdl & SDL_KMOD_ALT)   m |= REAKTOR_MOD_ALT;
    if (sdl & SDL_KMOD_GUI)   m |= REAKTOR_MOD_CMD;
    return m;
}

int
reaktor_chord(const SDL_Event *e, unsigned mods, SDL_Keycode key)
{
    if (!e || e->type != SDL_EVENT_KEY_DOWN) return 0;
    if (e->key.key != key) return 0;
    return ours((SDL_Keymod)(e->key.mod & MEANINGFUL)) == mods;
}

int
reaktor_shortcut_match(const reaktor_shortcut *table, int count,
                       const SDL_Event *e, int *index)
{
    int i;

    if (index) *index = 0;
    if (!table || !e || e->type != SDL_EVENT_KEY_DOWN) return -1;

    for (i = 0; i < count; i++) {
        const reaktor_shortcut *r = &table[i];
        SDL_Keycode last = r->key_last ? r->key_last : r->key;

        if (e->key.key < r->key || e->key.key > last) continue;
        if (ours((SDL_Keymod)(e->key.mod & MEANINGFUL)) != r->mods) continue;
        if (index) *index = (int)(e->key.key - r->key);
        return i;
    }
    return -1;
}

/* ARIA's names, not SDL's: a reader is told "Control", never "Left Ctrl". The
 * order is fixed so the same chord always reads the same way. */
int
reaktor_chord_text(unsigned mods, SDL_Keycode key, char *out, int cap)
{
    /* ARIA does not care in what order the modifiers come, so this is chosen
     * for the person hearing it: the command modifier leads, which is how
     * every platform writes its own - Ctrl+Shift+Tab, Cmd+Shift+T. Ordering
     * Shift ahead of Meta instead would have spelled one action's two rows
     * "Control+Shift+Tab" and "Shift+Meta+Tab", which reads as two different
     * chords. */
    static const struct { unsigned bit; const char *name; } MODS[] = {
        { REAKTOR_MOD_CTRL,  "Control" },
        { REAKTOR_MOD_CMD,   "Meta"    },
        { REAKTOR_MOD_ALT,   "Alt"     },
        { REAKTOR_MOD_SHIFT, "Shift"   }
    };
    /* SDL names a few keys differently from the web platform. */
    static const struct { SDL_Keycode key; const char *name; } NAMED[] = {
        { SDLK_RETURN,   "Enter"      },
        { SDLK_KP_ENTER, "Enter"      },
        { SDLK_UP,       "ArrowUp"    },
        { SDLK_DOWN,     "ArrowDown"  },
        { SDLK_LEFT,     "ArrowLeft"  },
        { SDLK_RIGHT,    "ArrowRight" },
        { SDLK_PAGEUP,   "PageUp"     },
        { SDLK_PAGEDOWN, "PageDown"   }
    };
    const char *name = NULL;
    int n = 0, i;

    if (!out || cap <= 0) return 0;
    out[0] = '\0';

    for (i = 0; i < (int)(sizeof(MODS) / sizeof(MODS[0])); i++) {
        if (!(mods & MODS[i].bit)) continue;
        n += snprintf(out + n, (size_t)(cap - n), "%s+", MODS[i].name);
        if (n >= cap) { out[0] = '\0'; return 0; }
    }

    for (i = 0; i < (int)(sizeof(NAMED) / sizeof(NAMED[0])); i++)
        if (NAMED[i].key == key) { name = NAMED[i].name; break; }
    if (!name) name = SDL_GetKeyName(key);
    if (!name || !*name) { out[0] = '\0'; return 0; }

    n += snprintf(out + n, (size_t)(cap - n), "%s", name);
    if (n >= cap) { out[0] = '\0'; return 0; }
    return n;
}

int
reaktor_shortcut_text(const reaktor_shortcut *table, int count,
                      int action, int index, char *out, int cap)
{
    int n, i;

    if (!out || cap <= 0) return 0;
    n = (int)strlen(out);
    if (!table || index < 0) return n;

    for (i = 0; i < count; i++) {
        const reaktor_shortcut *r = &table[i];
        SDL_Keycode last = r->key_last ? r->key_last : r->key;
        char chord[64];
        int len;

        if (r->action != action) continue;
        if (r->key + (SDL_Keycode)index > last) continue;
        len = reaktor_chord_text(r->mods, r->key + (SDL_Keycode)index,
                                 chord, (int)sizeof(chord));
        if (!len) continue;
        /* Stop rather than truncate: half a chord is worse than one fewer. */
        if (n + (n ? 1 : 0) + len >= cap) break;
        if (n) out[n++] = ' ';
        memcpy(out + n, chord, (size_t)len + 1);
        n += len;
    }
    return n;
}
