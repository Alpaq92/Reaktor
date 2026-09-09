/* declare.c - see declare.h.
 *
 * Every widget here is three steps and no drawing of its own:
 *
 *   1. report itself to the accessibility tree, which answers with an id;
 *   2. declare a box under that id, and ask layout where the same id went on
 *      the previous frame;
 *   3. draw there, through the imperative helper that already knows how.
 *
 * Step 3 is why this file is short and why behaviour cannot drift: nothing is
 * reimplemented. Step 1 comes first because the id is what steps 2 and 3 both
 * need, and reaktor_note computes it from the tree's shape rather than from
 * anything on screen - so it is available before the widget knows where it is.
 */
#include "internal.h"
#include "declare.h"

/* The frame being described. See declare.h on why this is not an argument. */
static App              *g_app;
static struct nk_context *g_ctx;

void
reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area)
{
    g_app = app;
    g_ctx = ctx;
    reaktor_layout_begin(&app->lay, area);
}

void
reaktor_frame_end(void)
{
    if (g_app) reaktor_layout_end(&g_app->lay);
    g_app = NULL;
    g_ctx = NULL;
}

void
reaktor_box_open(unsigned char dir, const reaktor_box *b)
{
    reaktor_box box;
    unsigned    id;

    if (!g_app) return;
    box = *b;
    box.dir = dir;

    /* A container is a group in the tree: it has no name of its own, and a
     * reader walks through it to its children. */
    id = reaktor_note_push(g_app, REAKTOR_A11Y_GROUP, NULL, NULL, 0u,
                           nk_rect(0, 0, 0, 0));
    reaktor_layout_open(&g_app->lay, id, &box);
    {
        struct nk_rect r;
        if (reaktor_layout_rect(&g_app->lay, id, &r))
            reaktor_note_bounds(g_app, id, r);
    }
}

void
reaktor_box_close(void)
{
    if (!g_app) return;
    reaktor_layout_close(&g_app->lay);
    reaktor_note_pop(g_app);
}

/* Steps 1 and 2, which every widget shares. Answers 0 when layout has not
 * placed this box yet - the first frame it exists, and the frame after the
 * tree changed shape - in which case the caller draws nothing at all rather
 * than drawing it somewhere wrong. */
static int
place(unsigned char role, const char *name, const char *value, unsigned state,
      const char *keys, const reaktor_box *b, unsigned *out_id,
      struct nk_rect *out_rect)
{
    unsigned id = reaktor_note(g_app, role, name, value, state,
                               nk_rect(0, 0, 0, 0));

    *out_id = id;
    reaktor_layout_leaf(&g_app->lay, id, b);
    if (keys) reaktor_note_keys(g_app, id, keys);
    if (!reaktor_layout_rect(&g_app->lay, id, out_rect)) return 0;
    reaktor_note_bounds(g_app, id, *out_rect);
    return 1;
}

int
reaktor_button(const reaktor_button_spec *s)
{
    reaktor_box    box;
    struct nk_rect r;
    unsigned       id;
    int            hit;

    if (!g_app || !s) return 0;
    box = s->box;
    /* A button is as wide as its label unless told otherwise; the stylesheet
     * owns the padding, so this asks Nuklear's own measurement for it. */
    if (box.w <= 0.0f && s->label)
        box.w = g_ctx->style.font->width(g_ctx->style.font->userdata,
                                         g_ctx->style.font->height, s->label,
                                         (int)strlen(s->label))
              + 2.0f * g_ctx->style.button.padding.x;
    if (box.h <= 0.0f)
        box.h = g_ctx->style.font->height
              + 2.0f * g_ctx->style.button.padding.y;

    if (!place(REAKTOR_A11Y_BUTTON, s->name ? s->name : s->label, NULL,
               s->disabled ? REAKTOR_A11Y_DISABLED : 0u, s->keys, &box,
               &id, &r))
        return 0;

    /* Placed absolutely, because the layout engine has already decided where
     * this goes; nk_layout_space is how Nuklear is told a caller means it. */
    nk_layout_space_begin(g_ctx, NK_STATIC, r.h, 1);
    nk_layout_space_push(g_ctx, nk_rect(r.x, r.y, r.w, r.h));
    hit = s->accent ? reaktor_button_accent(g_app, g_ctx, s->label)
                    : reaktor_button_label(g_app, g_ctx, s->label);
    nk_layout_space_end(g_ctx);

    if (reaktor_focus_activated(g_app, id)) hit = 1;
    if (hit && s->on_press.fn) s->on_press.fn(s->on_press.user);
    return hit;
}

void
reaktor_label(const reaktor_label_spec *s)
{
    reaktor_box    box;
    struct nk_rect r;
    unsigned       id;

    if (!g_app || !s || !s->text) return;
    box = s->box;
    if (box.w <= 0.0f)
        box.w = g_ctx->style.font->width(g_ctx->style.font->userdata,
                                         g_ctx->style.font->height, s->text,
                                         (int)strlen(s->text));
    if (box.h <= 0.0f) box.h = g_ctx->style.font->height;

    if (!place(REAKTOR_A11Y_LABEL, s->name ? s->name : s->text, NULL, 0u, NULL,
               &box, &id, &r))
        return;

    nk_layout_space_begin(g_ctx, NK_STATIC, r.h, 1);
    nk_layout_space_push(g_ctx, nk_rect(r.x, r.y, r.w, r.h));
    nk_label(g_ctx, s->text, NK_TEXT_LEFT);
    nk_layout_space_end(g_ctx);
}
