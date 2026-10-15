#include "internal.h"

static void
undo_free(undo_stack *u, int from)
{
    while (u->n > from) {
        reaktor_undo_step *s = &u->step[--u->n];

        if (s->free) s->free(s->data);
    }
    if (u->top > u->n) u->top = u->n;
    if (u->saved > u->n) u->saved = -1;
}

void
reaktor_undo_push(App *app, const reaktor_undo_step *s)
{
    undo_stack *u = &app->undo;
    unsigned    group;

    if (!s || !s->undo || !s->redo) return;
    undo_free(u, u->top);
    if (u->n == u->cap) {
        int                cap  = u->cap ? u->cap * 2 : 32;
        reaktor_undo_step *step = (reaktor_undo_step *)SDL_realloc(u->step, (size_t)cap * sizeof *step);
        unsigned          *grp  = (unsigned *)SDL_realloc(u->group, (size_t)cap * sizeof *grp);

        if (step) u->step = step;
        if (grp) u->group = grp;
        if (!step || !grp) {
            if (s->free) s->free(s->data);
            return;
        }
        u->cap = cap;
    }
    if (u->depth)
        group = u->open;
    else if (s->merge && u->n && u->step[u->n - 1].merge == s->merge && u->saved != u->n)
        group = u->group[u->n - 1];
    else
        group = ++u->next;
    u->step[u->n]  = *s;
    u->group[u->n] = group;
    u->top = ++u->n;
}

void
reaktor_undo_group(App *app, int begin)
{
    undo_stack *u = &app->undo;

    if (begin && u->depth++ == 0) u->open = ++u->next;
    if (!begin && u->depth > 0) u->depth--;
}

int
reaktor_undo(App *app)
{
    undo_stack *u = &app->undo;
    unsigned    group;

    if (u->top == 0) return 0;
    group = u->group[u->top - 1];
    while (u->top > 0 && u->group[u->top - 1] == group) {
        reaktor_undo_step *s = &u->step[--u->top];

        s->undo(s->data);
    }
    app->dirty = 1;
    return 1;
}

int
reaktor_redo(App *app)
{
    undo_stack *u = &app->undo;
    unsigned    group;

    if (u->top == u->n) return 0;
    group = u->group[u->top];
    while (u->top < u->n && u->group[u->top] == group) {
        reaktor_undo_step *s = &u->step[u->top++];

        s->redo(s->data);
    }
    app->dirty = 1;
    return 1;
}

const char *
reaktor_undo_label(App *app)
{
    return app->undo.top > 0 ? app->undo.step[app->undo.top - 1].label : NULL;
}

const char *
reaktor_redo_label(App *app)
{
    return app->undo.top < app->undo.n ? app->undo.step[app->undo.top].label : NULL;
}

void
reaktor_undo_saved(App *app)
{
    app->undo.saved = app->undo.top;
}

int
reaktor_undo_dirty(App *app)
{
    return app->undo.top != app->undo.saved;
}

void
reaktor_undo_clear(App *app)
{
    undo_stack *u = &app->undo;

    undo_free(u, 0);
    SDL_free(u->step);
    SDL_free(u->group);
    SDL_zerop(u);
}
