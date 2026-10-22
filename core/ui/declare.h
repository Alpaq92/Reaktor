#ifndef REAKTOR_DECLARE_H
#define REAKTOR_DECLARE_H

#include "layout.h"
#include "reaktor/widgets.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

void reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area);
void reaktor_frame_end(void);

int reaktor_frame_settled(const App *app);
unsigned reaktor_mark_id(const char *mark);

extern App               *g_frame_app;
extern struct nk_context *g_frame_ctx;
extern int                g_frame_unsettled;
extern float              g_frame_width[REAKTOR_LAY_DEPTH + 1];
extern int                g_frame_widths;

unsigned decl_note(unsigned char role, const char *name, const char *value,
                   unsigned state);
int      decl_emit(unsigned id, const char *keys, const reaktor_box *b);
float    decl_width_of(unsigned id);
int      decl_place(unsigned char role, const char *name, const char *value,
                    unsigned state, const char *keys, const reaktor_box *b,
                    unsigned *out_id);
int      decl_push_font(const char *selector);
const struct nk_user_font *decl_style_font(const char *selector);
void     decl_box_default(reaktor_box *b, float w, float h);
const char *decl_rule_or(const char *selector, const char *element);
void     decl_box_from_style(reaktor_box *box, const char *text, const char *selector,
                             const char *element, float pad_x, float pad_y);
void     decl_tip_at(const char *tip, struct nk_rect r, int cursor);

#endif
