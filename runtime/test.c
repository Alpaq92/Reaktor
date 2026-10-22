/* An application driven from a script, as a person would: the script runs on
   a thread of its own, finds widgets in the accessibility tree by name or
   mark, and acts through the same SDL events a mouse and keyboard send. Every
   look at the tree happens on the main thread, and every action waits until
   the window has drawn and settled. */
#include "internal.h"
#include "reaktor/test.h"
#include "declare.h"

#define TEST_WAIT_MS     5000
#define TEST_FIND_TRIES  20

struct reaktor_test {
    App           *app;
    SDL_Semaphore *done;
    char           text[256];
    int            checks, fails, stuck;
};

typedef struct test_ask {
    reaktor_test  *t;
    char           target[256];
    reaktor_a11y_node node;
    char           value[256];
    int            found;
    Uint64         seq;
} test_ask;

static reaktor_test_script g_script;
static void               *g_user;
static reaktor_test        g_test;
static SDL_Thread         *g_thread;
static void               *g_settle;
static test_ask            g_ask, g_settle_ask;

App *
reaktor_test_app_of(reaktor_test *t)
{
    return t->app;
}

static int
wait_main(reaktor_test *t)
{
    if (t->stuck) return 0;
    if (SDL_WaitSemaphoreTimeout(t->done, TEST_WAIT_MS)) return 1;
    SDL_Log("test: the window stopped answering");
    t->stuck = 1;
    return 0;
}

static int
matches(const reaktor_a11y_node *n, const char *target)
{
    if (target[0] == '#') return n->id && reaktor_mark_has(n->id, target + 1);
    return n->name && SDL_strcmp(n->name, target) == 0;
}

/* Whether the node at i sits, at any depth, inside a node marked scope. */
static int
within(const reaktor_a11y_node *tree, int i, const char *scope)
{
    int hops, k;

    for (hops = 0; hops < REAKTOR_A11Y_MAX_DEPTH && tree[i].parent; hops++) {
        if (reaktor_mark_has(tree[i].parent, scope)) return 1;
        for (k = i - 1; k >= 0 && tree[k].id != tree[i].parent; k--) {}
        if (k < 0) return 0;
        i = k;
    }
    return 0;
}

static void
look_main(App *app, void *user)
{
    test_ask *q = (test_ask *)user;
    const reaktor_a11y_node *tree;
    int n = 0, i;
    char        scope[64];
    const char *target = q->target, *space;
    int         scoped = 0;

    /* "#list Delete": a mark, then what to find inside it. A whole target
       that is itself a mark, spaces and all, is taken as one. */
    if (target[0] == '#' && !reaktor_mark_has(0, target + 1) &&
        (space = SDL_strchr(target, ' ')) && (size_t)(space - target) < sizeof scope) {
        SDL_strlcpy(scope, target + 1, (size_t)(space - target));
        target = space + 1;
        scoped = 1;
    }
    tree = reaktor_a11y_tree(&app->a11y, &n);
    q->found = 0;
    if (scoped && !reaktor_mark_has(0, scope)) {
        SDL_SignalSemaphore(q->t->done);
        return;
    }
    for (i = 0; i < n; i++)
        if (matches(&tree[i], target) && (!scoped || within(tree, i, scope))) {
            if (!q->found) {
                q->node = tree[i];
                SDL_strlcpy(q->value, tree[i].value ? tree[i].value : "", sizeof q->value);
            }
            q->found++;
        }
    if (q->found > 1)
        SDL_Log("test: %d widgets match \"%s\"; the first is used", q->found, q->target);
    SDL_SignalSemaphore(q->t->done);
}

void
reaktor_test_settle(reaktor_test *t)
{
    if (t->stuck) return;
    SDL_zero(g_settle_ask);
    g_settle_ask.t = t;
    SDL_SetAtomicPointer(&g_settle, &g_settle_ask);
    reaktor_wake(t->app);
    if (wait_main(t)) SDL_SetAtomicPointer(&g_settle, NULL);
}

static int
look(reaktor_test *t, const char *target, test_ask *q)
{
    int tries;

    SDL_zero(*q);
    if (t->stuck) return 0;
    for (tries = 0; tries < TEST_FIND_TRIES; tries++) {
        SDL_zero(g_ask);
        g_ask.t = t;
        SDL_strlcpy(g_ask.target, target, sizeof g_ask.target);
        reaktor_post(t->app, look_main, &g_ask);
        if (!wait_main(t)) return 0;
        *q = g_ask;
        if (q->found) return 1;
        reaktor_test_settle(t);
        if (t->stuck) return 0;
    }
    return 0;
}

int
reaktor_test_find(reaktor_test *t, const char *target)
{
    test_ask q;

    return target && look(t, target, &q);
}

int
reaktor_test_value(reaktor_test *t, const char *target, char *out, int cap)
{
    test_ask q;

    if (!target || !look(t, target, &q)) return 0;
    if (out && cap > 0) SDL_strlcpy(out, q.value, (size_t)cap);
    return 1;
}

unsigned
reaktor_test_state(reaktor_test *t, const char *target)
{
    test_ask q;

    return target && look(t, target, &q) ? q.node.state : 0u;
}

static void
push(reaktor_test *t, SDL_Event *e)
{
    e->common.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(e);
    reaktor_wake(t->app);
}

static int
press_at(reaktor_test *t, float x, float y)
{
    Uint32 win = SDL_GetWindowID(t->app->win);
    SDL_Event e;
    int down;

    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = win;
    e.motion.x = x;
    e.motion.y = y;
    push(t, &e);
    reaktor_test_settle(t);
    for (down = 1; down >= 0; down--) {
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = win;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = down != 0;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        push(t, &e);
        reaktor_test_settle(t);
    }
    return !t->stuck;
}

int
reaktor_test_click(reaktor_test *t, const char *target)
{
    test_ask q;
    struct nk_rect r;

    if (!target || !look(t, target, &q)) {
        SDL_Log("test: no %s to click", target ? target : "(null)");
        return 0;
    }
    if (q.node.state & REAKTOR_A11Y_OFFSCREEN) {
        SDL_Log("test: %s is scrolled out of view", target);
        return 0;
    }
    r = q.node.bounds;
    return press_at(t, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
}

int
reaktor_test_type(reaktor_test *t, const char *target, const char *text)
{
    SDL_Event e;

    if (target && !reaktor_test_click(t, target)) return 0;
    SDL_strlcpy(t->text, text ? text : "", sizeof t->text);
    SDL_zero(e);
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.windowID = SDL_GetWindowID(t->app->win);
    e.text.text = t->text;
    push(t, &e);
    reaktor_test_settle(t);
    return !t->stuck;
}

/* "Ctrl+Shift+S", "Enter", "Escape": the modifiers, then one key by its SDL name. */
int
reaktor_test_key(reaktor_test *t, const char *chord)
{
    char part[64];
    const char *p = chord, *plus;
    SDL_Keymod mod = SDL_KMOD_NONE;
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Event e;
    int down;

    while (p && *p) {
        size_t n;

        plus = SDL_strchr(p, '+');
        n = plus && plus[1] ? (size_t)(plus - p) : SDL_strlen(p);
        if (n >= sizeof part) return 0;
        SDL_memcpy(part, p, n);
        part[n] = 0;
        p = plus && plus[1] ? plus + 1 : NULL;
        if (!p) key = SDL_GetKeyFromName(part);
        else if (SDL_strcasecmp(part, "Ctrl") == 0) mod |= SDL_KMOD_LCTRL;
        else if (SDL_strcasecmp(part, "Shift") == 0) mod |= SDL_KMOD_LSHIFT;
        else if (SDL_strcasecmp(part, "Alt") == 0) mod |= SDL_KMOD_LALT;
        else if (SDL_strcasecmp(part, "Cmd") == 0 || SDL_strcasecmp(part, "Gui") == 0)
            mod |= SDL_KMOD_LGUI;
        else return 0;
    }
    if (key == SDLK_UNKNOWN) {
        SDL_Log("test: no key called %s", chord ? chord : "(null)");
        return 0;
    }
    for (down = 1; down >= 0; down--) {
        SDL_zero(e);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = SDL_GetWindowID(t->app->win);
        e.key.key = key;
        e.key.scancode = SDL_GetScancodeFromKey(key, NULL);
        e.key.mod = mod;
        e.key.down = down != 0;
        push(t, &e);
        reaktor_test_settle(t);
    }
    return !t->stuck;
}

int
reaktor_test_check(reaktor_test *t, int ok, const char *what)
{
    t->checks++;
    if (!ok) t->fails++;
    SDL_Log("test: %-56s %s", what ? what : "", ok ? "ok" : "FAILED");
    return ok;
}

static int SDLCALL
script_thread(void *data)
{
    reaktor_test *t = (reaktor_test *)data;

    g_script(t, g_user);
    if (!t->checks) reaktor_test_check(t, 0, "the script checked nothing");
    SDL_Log("test: %d of %d checks passed", t->checks - t->fails, t->checks);
    reaktor_quit(t->app, t->fails || t->stuck ? 1 : 0);
    return 0;
}

/* From the main window's frame: the script starts once the first frame settles. */
static void
test_frame(App *app)
{
    test_ask *q = (test_ask *)SDL_GetAtomicPointer(&g_settle);

    /* A settle asked for: two frames drawn since, and the last one settled. */
    if (q) {
        if (!q->seq) q->seq = app->ctx->seq + 1;
        if (app->ctx->seq >= q->seq + 2 && reaktor_frame_settled(app)) {
            SDL_SetAtomicPointer(&g_settle, NULL);
            SDL_SignalSemaphore(q->t->done);
        } else {
            app->dirty = 1;
            reaktor_wake(app);
        }
        return;
    }
    if (!g_script || g_thread) return;
    if (!reaktor_frame_settled(app)) {
        app->dirty = 1;
        reaktor_wake(app);
        return;
    }
    g_test.app = app;
    g_test.done = SDL_CreateSemaphore(0);
    if (g_test.done) g_thread = SDL_CreateThread(script_thread, "reaktor-test", &g_test);
    if (!g_thread) {
        SDL_Log("test: cannot start the script");
        reaktor_quit(app, 1);
    }
}

int
reaktor_test_app(int argc, char **argv, const reaktor_launch *launch,
                 reaktor_test_script script, void *user)
{
    static char video[] = "--video", dummy[] = "dummy", fresh[] = "--new-instance";
    char **args = (char **)SDL_calloc((size_t)argc + 4, sizeof *args);
    int i, n = 0, own_video = 0, rc;

    if (!args) return 1;
    for (i = 1; i < argc; i++)
        if (SDL_strcmp(argv[i], "--video") == 0) own_video = 1;
    args[n++] = argc > 0 ? argv[0] : fresh;
    if (!own_video) {
        args[n++] = video;
        args[n++] = dummy;
    }
    args[n++] = fresh;
    for (i = 1; i < argc; i++) args[n++] = argv[i];
    g_script = script;
    g_user = user;
    reaktor_test_hook = test_frame;
    rc = reaktor_launch_app(n, args, launch);
    if (g_thread) SDL_WaitThread(g_thread, NULL);
    if (g_test.done) SDL_DestroySemaphore(g_test.done);
    SDL_free(args);
    return rc;
}
