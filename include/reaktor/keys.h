#ifndef REAKTOR_KEYS_H
#define REAKTOR_KEYS_H

#include <SDL3/SDL.h>

enum {
    REAKTOR_MOD_SHIFT = 1u << 0,
    REAKTOR_MOD_CTRL  = 1u << 1,
    REAKTOR_MOD_ALT   = 1u << 2,
    REAKTOR_MOD_CMD   = 1u << 3
};

typedef struct reaktor_shortcut {
    unsigned     mods;
    SDL_Keycode  key, key_last;
    int          action;
    const char  *name;
} reaktor_shortcut;

int reaktor_chord(const SDL_Event *e, unsigned mods, SDL_Keycode key);

int reaktor_shortcut_match(const reaktor_shortcut *table, int count,
                           const SDL_Event *e, int *index);

int reaktor_chord_text(unsigned mods, SDL_Keycode key, char *out, int cap);

int reaktor_shortcut_text(const reaktor_shortcut *table, int count,
                          int action, int index, char *out, int cap);

#endif
