#ifndef REAKTOR_A11Y_H
#define REAKTOR_A11Y_H

#include "nk_common.h"
#include <stdio.h>
#include "reaktor/a11y.h"

typedef struct reaktor_a11y_node {
    unsigned       id;
    unsigned       parent;
    unsigned char  role;
    unsigned char  level;
    unsigned       state;
    const char    *name;
    const char    *value;
    const char    *keys;
    struct nk_rect bounds;
    float          num, lo, hi, step;
} reaktor_a11y_node;

enum {
    REAKTOR_A11Y_ADDED = 0,
    REAKTOR_A11Y_REMOVED,
    REAKTOR_A11Y_RENAMED,
    REAKTOR_A11Y_RESTATED,
    REAKTOR_A11Y_MOVED
};

typedef struct reaktor_a11y_change {
    unsigned char kind;
    unsigned      id;
    int           index;
} reaktor_a11y_change;

#define REAKTOR_A11Y_MAX_NODES   256
#define REAKTOR_A11Y_MAX_CHANGES 256
#define REAKTOR_A11Y_POOL        16384
#define REAKTOR_A11Y_MAX_DEPTH   32
#define REAKTOR_A11Y_SLOTS       512

typedef struct reaktor_a11y_slot {
    unsigned key;
    unsigned gen;
    int      val;
} reaktor_a11y_slot;

typedef struct reaktor_a11y {
    reaktor_a11y_node node[2][REAKTOR_A11Y_MAX_NODES];
    int             count[2];
    int             front;

    char            pool[REAKTOR_A11Y_POOL];
    int             pool_used;
    int             pool_entries;
    unsigned        pool_gen;

    unsigned parent[REAKTOR_A11Y_MAX_DEPTH];
    int      depth;

    reaktor_a11y_change change[REAKTOR_A11Y_MAX_CHANGES];
    int               change_count;

    int overflow_nodes;

    reaktor_a11y_slot bucket[REAKTOR_A11Y_SLOTS];
    reaktor_a11y_slot index[REAKTOR_A11Y_SLOTS];
    reaktor_a11y_slot strings[REAKTOR_A11Y_SLOTS * 2];
    unsigned        bucket_gen, index_gen;
    unsigned char   matched[REAKTOR_A11Y_MAX_NODES];

    int building;

    unsigned focus_id;
} reaktor_a11y;

void reaktor_a11y_begin(reaktor_a11y *a, const char *window_name,
                        struct nk_rect bounds);

unsigned reaktor_a11y_add(reaktor_a11y *a, unsigned char role,
                          const char *name, const char *value, unsigned state,
                          struct nk_rect bounds);

unsigned reaktor_a11y_push(reaktor_a11y *a, unsigned char role,
                           const char *name, const char *value, unsigned state,
                           struct nk_rect bounds);
void     reaktor_a11y_pop(reaktor_a11y *a);

int reaktor_a11y_end(reaktor_a11y *a);

void reaktor_a11y_set_focus(reaktor_a11y *a, unsigned id);

void reaktor_a11y_set_range(reaktor_a11y *a, unsigned id, float num, float lo,
                            float hi, float step);

void reaktor_a11y_set_keys(reaktor_a11y *a, unsigned id, const char *keys);

void reaktor_a11y_set_value(reaktor_a11y *a, unsigned id, const char *value);

void reaktor_a11y_set_bounds(reaktor_a11y *a, unsigned id, struct nk_rect r);

const reaktor_a11y_node *reaktor_a11y_tree(const reaktor_a11y *a, int *count);
const reaktor_a11y_change *reaktor_a11y_changes(const reaktor_a11y *a,
                                                int *count);
const char *reaktor_a11y_role_name(unsigned char role);

void reaktor_a11y_dump(const reaktor_a11y *a, FILE *out);

typedef void (*reaktor_a11y_action)(void *user, unsigned id);

void reaktor_a11y_platform_init(reaktor_a11y_action activate,
                                reaktor_a11y_action focus, void *user);
void reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id);

struct SDL_Window;
void reaktor_a11y_platform_window_push(struct SDL_Window *win,
                                       const reaktor_a11y *a, unsigned focus_id,
                                       reaktor_a11y_action activate,
                                       reaktor_a11y_action focus, void *user);
void reaktor_a11y_platform_window_drain(struct SDL_Window *win);
void reaktor_a11y_platform_window_gone(struct SDL_Window *win);

void reaktor_a11y_platform_drain(void);

#endif
