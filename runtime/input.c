#include "internal.h"
#include "reaktor/launch.h"

#ifdef __EMSCRIPTEN__
#include "keyboard_web.h"

static Uint32       g_page_key;
static enum nk_keys g_web_key;

/* A phone's named keys, REAKTOR_WEB_KEY_BACKSPACE (-1) first. */
static const struct { SDL_Keycode sdl; enum nk_keys nk; } g_soft[] = {
    { SDLK_BACKSPACE, NK_KEY_BACKSPACE }, { SDLK_RETURN, NK_KEY_ENTER },
    { SDLK_LEFT, NK_KEY_LEFT },           { SDLK_RIGHT, NK_KEY_RIGHT },
    { SDLK_UP, NK_KEY_UP },               { SDLK_DOWN, NK_KEY_DOWN },
    { SDLK_HOME, NK_KEY_TEXT_START },     { SDLK_END, NK_KEY_TEXT_END },
    { SDLK_DELETE, NK_KEY_DEL }
};
#define SOFT_KEYS ((int)(sizeof(g_soft) / sizeof(g_soft[0])))

/* Let go next frame, or Nuklear never sees it pressed. */
static void
page_key_take(struct nk_context *ctx, int code)
{
    if (code > 0) {
        nk_input_unicode(ctx, (nk_rune)code);
        return;
    }
    g_web_key = code < 0 && -code - 1 < SOFT_KEYS ? g_soft[-code - 1].nk : NK_KEY_NONE;
    if (g_web_key) nk_input_key(ctx, g_web_key, nk_true);
}

void
reaktor_input_start(void)
{
    g_page_key = SDL_RegisterEvents(1);
    reaktor_web_keys_init(g_page_key);
}

int
reaktor_input_page_key(App *app, SDL_Event *event)
{
    int c = event->user.code;

    if (event->type != g_page_key) return 0;
    app->dirty = 1;
    /* Not Backspace, which also replays edits. */
    if (c <= REAKTOR_WEB_KEY_ENTER && c >= REAKTOR_WEB_KEY_DELETE) {
        SDL_Event k;

        SDL_zero(k);
        k.type         = SDL_EVENT_KEY_DOWN;
        k.key.down     = true;
        k.key.key      = g_soft[-c - 1].sdl;
        k.key.scancode = SDL_GetScancodeFromKey(k.key.key, NULL);
        k.key.mod      = SDL_GetModState();
        k.key.windowID = SDL_GetWindowID(app->win);
        if (reaktor_key_hook(app, &k) || (!app->editing && focus_key(app, &k)))
            return 1;
    }
    page_key_take(app->ctx, c);
    return 1;
}
#endif

#define TEXT_ROOM (NK_INPUT_MAX - 1)

enum {
    HELD_MOTION = 1, HELD_BUTTON, HELD_KEY, HELD_TEXT, HELD_PAGE, HELD_WHEEL
};

static int
held_kind(Uint32 type)
{
#ifdef __EMSCRIPTEN__
    if (g_page_key && type == g_page_key) return HELD_PAGE;
#endif
    switch (type) {
    case SDL_EVENT_MOUSE_MOTION:      return HELD_MOTION;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:   return HELD_BUTTON;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:            return HELD_KEY;
    case SDL_EVENT_TEXT_INPUT:        return HELD_TEXT;
    case SDL_EVENT_MOUSE_WHEEL:       return HELD_WHEEL;
    default:                          return 0;
    }
}

enum { KEY_TYPES, KEY_MODIFIES, KEY_CHANGES };

static int
key_role(const App *app, const SDL_KeyboardEvent *k)
{
    if (k->scancode >= SDL_SCANCODE_LCTRL && k->scancode <= SDL_SCANCODE_RGUI)
        return KEY_MODIFIES;
    if (k->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) return KEY_CHANGES;
    if (k->key == SDLK_SPACE) return app->editing ? KEY_TYPES : KEY_CHANGES;
    if (k->key > SDLK_SPACE && k->key != SDLK_DELETE &&
        !(k->key & SDLK_SCANCODE_MASK))
        return KEY_TYPES;
    return KEY_CHANGES;
}

static int
takes_change(struct input_queue *q, SDL_Keycode key)
{
    if (q->changed || q->text) return 0;
    q->changed = 1;
    q->key = key;
    return 1;
}

static int
takes_text(struct input_queue *q, size_t len)
{
    if (q->changed || q->text + len > TEXT_ROOM) return 0;
    q->text += len;
    return 1;
}

static int
frame_takes(App *app, const SDL_Event *e)
{
    struct input_queue *q = &app->queue;
    int c;

    if (app->key_click == KEY_CLICK_ASKED || app->key_click == KEY_CLICK_PRESS)
        return 0;
    switch (held_kind(e->type)) {
    case HELD_MOTION:
        if (q->moved_ok) q->moved = 1;
        return q->moved_ok;
    case HELD_BUTTON:
        if (q->moved && !e->button.down) return 0;
        if (!takes_change(q, SDLK_UNKNOWN)) return 0;
        q->moved_ok = 0;
        return 1;
    case HELD_KEY:
        switch (key_role(app, &e->key)) {
        case KEY_TYPES:    return 1;
        case KEY_MODIFIES: return !q->changed;
        default:
            if (!e->key.down && e->key.key == q->key) return 1;
            return takes_change(q, e->key.down ? e->key.key : SDLK_UNKNOWN);
        }
    case HELD_TEXT:
        return takes_text(q, SDL_strlen(e->text.text));
    case HELD_PAGE:
        c = e->user.code;
        if (c <= 0) return takes_change(q, SDLK_UNKNOWN);
        return takes_text(q, c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4);
    default:
        return 1;
    }
}

static int
hold(struct input_queue *q, const SDL_Event *event, const char *text, size_t len)
{
    SDL_Event *more;
    char *copy = NULL;
    int cap;

    if (q->n == q->cap) {
        cap = q->cap ? q->cap * 2 : 32;
        more = (SDL_Event *)SDL_realloc(q->ev, (size_t)cap * sizeof(*more));
        if (!more) return 0;
        q->ev = more;
        q->cap = cap;
    }
    /* SDL frees an event's text on its next pump. */
    if (text) {
        if (!(copy = (char *)SDL_malloc(len + 1))) return 0;
        SDL_memcpy(copy, text, len);
        copy[len] = '\0';
    }
    q->ev[q->n] = *event;
    if (copy) q->ev[q->n].text.text = copy;
    q->n++;
    return 1;
}

static int
hold_for_next_frame(App *app, const SDL_Event *event)
{
    struct input_queue *q = &app->queue;
    const char *t;
    size_t n;

    if (!held_kind(event->type)) {
        /* Window events stay behind held input. */
        if (!q->n || event->type < SDL_EVENT_WINDOW_FIRST ||
            event->type > SDL_EVENT_WINDOW_LAST)
            return 0;
        return hold(q, event, NULL, 0);
    }
    if (event->type == SDL_EVENT_TEXT_INPUT &&
        SDL_strlen(event->text.text) > TEXT_ROOM) {
        for (t = event->text.text; *t; t += n) {
            n = SDL_strlen(t);
            if (n > TEXT_ROOM) {
                n = TEXT_ROOM;
                while (n > 1 && (t[n] & 0xC0) == 0x80) n--;
            }
            if (!hold(q, event, t, n)) return t != event->text.text;
        }
        return 1;
    }
    if (!q->n && frame_takes(app, event)) return 0;
    if (event->type == SDL_EVENT_MOUSE_MOTION && q->n &&
        q->ev[q->n - 1].type == SDL_EVENT_MOUSE_MOTION) {
        q->ev[q->n - 1] = *event;
        return 1;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat && q->n &&
        q->ev[q->n - 1].type == SDL_EVENT_KEY_DOWN &&
        q->ev[q->n - 1].key.key == event->key.key)
        return 1;
    if (event->type != SDL_EVENT_TEXT_INPUT) return hold(q, event, NULL, 0);
    return hold(q, event, event->text.text, SDL_strlen(event->text.text));
}

void
reaktor_app_input_free(App *app)
{
    struct input_queue *q = &app->queue;

    while (q->n--)
        if (q->ev[q->n].type == SDL_EVENT_TEXT_INPUT)
            SDL_free((void *)q->ev[q->n].text.text);
    SDL_free(q->ev);
    q->ev = NULL;
    q->n = q->cap = 0;
}

void
reaktor_replay_held(App *app)
{
    struct input_queue *q = &app->queue;
    SDL_Event e;
    int taken = 0;

    q->changed  = 0;
    q->key      = SDLK_UNKNOWN;
    q->moved    = 0;
    q->moved_ok = 1;
    q->text     = 0;
#ifdef __EMSCRIPTEN__
    if (g_web_key) {
        nk_input_key(app->ctx, g_web_key, nk_false);
        g_web_key = NK_KEY_NONE;
    }
#endif
    if (q->pointer_back) {
        nk_input_motion(app->ctx, (int)q->pointer.x, (int)q->pointer.y);
        q->pointer_back = 0;
    }
    if (app->key_click >= KEY_CLICK_RELEASE) {
        q->changed  = 1;
        q->moved_ok = 0;
    }
    while (q->n && frame_takes(app, &q->ev[0])) {
        e = q->ev[0];
        q->n--;
        SDL_memmove(q->ev, q->ev + 1, (size_t)q->n * sizeof(*q->ev));
        reaktor_take_event(app, &e);
        if (e.type == SDL_EVENT_TEXT_INPUT) SDL_free((void *)e.text.text);
        taken = 1;
    }
    if (taken) reaktor_app_wake(app);
}

static void
feed(App *app, SDL_Event *event)
{
    if (hold_for_next_frame(app, event)) app->dirty = 1;
    else reaktor_take_event(app, event);
}

enum { TOUCH_NONE, TOUCH_PRESSED, TOUCH_CAUGHT, TOUCH_DRAGGING, TOUCH_SCROLLING };

#define TOUCH_SLOP   8.0f
#define FLING_TAU_MS 325.0f
#define FLING_MIN    60.0f
#define FLING_STOP   20.0f

static void
touch_pointer(App *app, float x, float y, int release)
{
    SDL_Event e;

    SDL_zero(e);
    e.type            = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(app->win);
    e.motion.which    = SDL_TOUCH_MOUSEID;
    e.motion.x        = x;
    e.motion.y        = y;
    feed(app, &e);
    if (!release) return;
    SDL_zero(e);
    e.type            = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(app->win);
    e.button.which    = SDL_TOUCH_MOUSEID;
    e.button.button   = SDL_BUTTON_LEFT;
    e.button.clicks   = 1;
    e.button.x        = x;
    e.button.y        = y;
    feed(app, &e);
}

static void
touch_wheel(App *app, float dy)
{
    SDL_Event e;

    if (dy == 0.0f || app->touch_unit <= 0.0f) return;
    SDL_zero(e);
    e.type           = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = SDL_GetWindowID(app->win);
    e.wheel.which    = SDL_TOUCH_MOUSEID;
    e.wheel.y        = dy / app->touch_unit;
    e.wheel.mouse_x  = app->touch_x0;
    e.wheel.mouse_y  = app->touch_y0;
    feed(app, &e);
}

static void
touch_sample(App *app, float y, Uint64 ns)
{
    app->touch_ys[app->touch_n % 4] = y;
    app->touch_ns[app->touch_n % 4] = ns;
    app->touch_n++;
}

/* Nuklear moves a panel a tenth of its height per wheel step. */
static float
touch_unit(const App *app)
{
    struct nk_rect b = app->body_rect;
    float h = (float)app->laid_h;

    if (b.h > 0.0f && app->touch_x0 >= b.x && app->touch_x0 < b.x + b.w &&
        app->touch_y0 >= b.y && app->touch_y0 < b.y + b.h)
        h = b.h;
    return 0.1f * h;
}

static void
fling_start(App *app, Uint64 now)
{
    int    last = app->touch_n - 1, first = last, i;
    Uint64 t;
    float  v;

    if (last < 1 || now - app->touch_ns[last % 4] > 100000000u) return;
    for (i = last - 1; i >= 0 && i > last - 4; i--) {
        if (app->touch_ns[last % 4] - app->touch_ns[i % 4] > 100000000u) break;
        first = i;
    }
    t = app->touch_ns[last % 4] - app->touch_ns[first % 4];
    if (first == last || !t) return;
    v = (app->touch_ys[last % 4] - app->touch_ys[first % 4]) / ((float)t / 1e9f);
    if (SDL_fabsf(v) < FLING_MIN) return;
    app->fling_v      = v;
    app->fling_ns     = now;
    app->restore_rate = 0;
    reaktor_hint(SDL_HINT_MAIN_CALLBACK_RATE, app->drag_rate);
    app->dirty = 1;
}

void
reaktor_fling_step(App *app)
{
    Uint64 now = SDL_GetTicksNS();
    float  dt;

    if (app->fling_v == 0.0f) return;
    dt = (float)(now - app->fling_ns) / 1e9f;
    app->fling_ns = now;
    if (app->touch_unit > 0.0f)
        nk_input_scroll(app->ctx, nk_vec2(0.0f, app->fling_v * dt / app->touch_unit));
    app->fling_v *= SDL_expf(-dt * 1000.0f / FLING_TAU_MS);
    if (SDL_fabsf(app->fling_v) < FLING_STOP) {
        app->fling_v      = 0.0f;
        app->restore_rate = 2;
    }
}

/* A finger that moves mostly up or down scrolls, and presses nothing. */
static int
touch_takes(App *app, const SDL_Event *e)
{
    struct nk_rect r = app->field_rect;
    float dx, dy;
    int   was = app->touch;

    switch (e->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.which != SDL_TOUCH_MOUSEID || e->button.button != SDL_BUTTON_LEFT)
            return 0;
        app->touch_x0 = e->button.x;
        app->touch_y0 = e->button.y;
        app->touch_n  = 0;
        touch_sample(app, e->button.y, e->button.timestamp);
        app->touch_in_field = app->field_rect_valid &&
                              e->button.x >= r.x && e->button.x <= r.x + r.w &&
                              e->button.y >= r.y && e->button.y <= r.y + r.h;
        if (app->fling_v != 0.0f) {
            app->fling_v      = 0.0f;
            app->restore_rate = 2;
            app->touch        = TOUCH_CAUGHT;
            return 1;
        }
        app->touch = TOUCH_PRESSED;
        return 0;

    case SDL_EVENT_MOUSE_MOTION:
        if (e->motion.which != SDL_TOUCH_MOUSEID) return 0;
        switch (app->touch) {
        case TOUCH_SCROLLING:
            touch_wheel(app, e->motion.y - app->touch_last);
            app->touch_last = e->motion.y;
            touch_sample(app, e->motion.y, e->motion.timestamp);
            return 1;
        case TOUCH_PRESSED:
        case TOUCH_CAUGHT:
            dx = e->motion.x - app->touch_x0;
            dy = e->motion.y - app->touch_y0;
            if (SDL_fabsf(dx) <= TOUCH_SLOP && SDL_fabsf(dy) <= TOUCH_SLOP)
                return app->touch == TOUCH_CAUGHT;
            if (app->touch_in_field || SDL_fabsf(dy) <= SDL_fabsf(dx)) {
                if (app->touch == TOUCH_CAUGHT) return 1;
                app->touch = TOUCH_DRAGGING;
                return 0;
            }
            /* Released away from every widget, the press clicks nothing. */
            if (app->touch == TOUCH_PRESSED) touch_pointer(app, -1e4f, -1e4f, 1);
            touch_pointer(app, app->touch_x0, app->touch_y0, 0);
            app->touch      = TOUCH_SCROLLING;
            app->touch_unit = touch_unit(app);
            app->touch_last = app->touch_y0 + (dy > 0.0f ? TOUCH_SLOP : -TOUCH_SLOP);
            touch_wheel(app, e->motion.y - app->touch_last);
            app->touch_last = e->motion.y;
            touch_sample(app, e->motion.y, e->motion.timestamp);
            return 1;
        default:
            return 0;
        }

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.which != SDL_TOUCH_MOUSEID || e->button.button != SDL_BUTTON_LEFT)
            return 0;
        app->touch = TOUCH_NONE;
        if (was == TOUCH_SCROLLING) {
            fling_start(app, e->button.timestamp);
            return 1;
        }
        return was == TOUCH_CAUGHT;

    default:
        return 0;
    }
}

void
reaktor_input_event(App *app, SDL_Event *event)
{
    if (!touch_takes(app, event)) feed(app, event);
}
