#ifndef REAKTOR_A11Y_SNAPSHOT_H
#define REAKTOR_A11Y_SNAPSHOT_H

#include <SDL3/SDL.h>

#include "a11y.h"

typedef struct reaktor_snap_node {
    unsigned      id, parent;
    unsigned char role, level;
    unsigned      state;
    const char   *name, *value, *keys;
    float         x, y, w, h;
    float         num, lo, hi, step;
} reaktor_snap_node;

#define REAKTOR_SNAP_TEXT 4096

/* One window's tree, as screen readers read it from their own threads. */
typedef struct reaktor_snap reaktor_snap;

int  reaktor_snap_init(reaktor_a11y_action activate, reaktor_a11y_action focus,
                       void *user);

reaktor_snap *reaktor_snap_main(void);
reaktor_snap *reaktor_snap_new(reaktor_a11y_action activate,
                               reaktor_a11y_action focus, void *user,
                               Uint32 window);
void          reaktor_snap_bind(reaktor_snap *s, reaktor_a11y_action activate,
                                reaktor_a11y_action focus, void *user,
                                Uint32 window);

int      reaktor_snap_update_in(reaktor_snap *s, const reaktor_a11y *a,
                                unsigned focus_id);
int      reaktor_snap_get_in(reaktor_snap *s, unsigned id, reaktor_snap_node *out,
                             char *buf, size_t cap);
unsigned reaktor_snap_child_in(reaktor_snap *s, unsigned parent, int last);
unsigned reaktor_snap_sibling_in(reaktor_snap *s, unsigned id, int back);
unsigned reaktor_snap_parent_in(reaktor_snap *s, unsigned id);
unsigned reaktor_snap_hit_in(reaktor_snap *s, float x, float y);
unsigned reaktor_snap_focus_in(reaktor_snap *s);
void     reaktor_snap_request_focus_in(reaktor_snap *s, unsigned id);
void     reaktor_snap_request_activate_in(reaktor_snap *s, unsigned id);
void     reaktor_snap_drain_in(reaktor_snap *s);

int  reaktor_snap_update(const reaktor_a11y *a, unsigned focus_id);

int  reaktor_snap_get(unsigned id, reaktor_snap_node *out, char *buf,
                      size_t cap);

unsigned reaktor_snap_child(unsigned parent, int last);
unsigned reaktor_snap_sibling(unsigned id, int back);
unsigned reaktor_snap_parent(unsigned id);

unsigned reaktor_snap_hit(float x, float y);

unsigned reaktor_snap_focus(void);

int reaktor_snap_focusable(unsigned char role, unsigned state);

void reaktor_snap_request_focus(unsigned id);
void reaktor_snap_request_activate(unsigned id);
void reaktor_snap_drain(void);

#endif
