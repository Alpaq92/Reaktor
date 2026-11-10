#include "internal.h"
#include "reaktor/reaktor.h"
#include "reaktor/main.h"

#define JAPANESE "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
#define ARABIC   "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"

static char          g_name[64];
static int           g_name_len;
static reaktor_bool  g_loud;
static int           g_pick, g_fruit, g_row;
static float         g_level = 0.3f;
static unsigned      g_ticks;
static SDL_AtomicInt g_slide, g_tick;
static int           g_units, g_unit_fails;
static char          g_unit_log[1024];

static const char *const g_rows[] = {
    "Apple", "Banana", "Cherry", "Damson", "Elder", "Fig", "Grape", "Hazel",
    "Ice plum", "Jujube", "Kiwi", "Lime", "Mango", "Nectarine", "Olive", "Pear",
    "Quince", "Raspberry", "Sloe", "Tangerine"
};

static const char *
cell(int row, int column, void *user)
{
    (void)column; (void)user;
    return g_rows[row];
}

static void
toast(void *user)
{
    reaktor_toast((App *)user, &(reaktor_toast_spec){ .text = "Saved", .timeout_ms = 60000 });
}

static void
page(App *app, struct reaktor_context *ctx, int win_w, int win_h)
{
    static const char *const fruit[] = { "Apple", "Banana", "Cherry" };
    char  ticker[32];
    float x;
    int   i;

    (void)win_h;
    if (SDL_GetAtomicInt(&g_tick)) g_ticks++;
    SDL_snprintf(ticker, sizeof ticker, "Frame %u", g_ticks);
    {
        struct reaktor_image im = reaktor_image_file(app, "assets/icons/reaktor-icon.svg", 16);

        reaktor_draw_image(reaktor_window_get_canvas(ctx), reaktor_rect(12.0f, 14.0f, 300.0f, 12.0f),
                           &im, reaktor_rgb(255, 255, 255));
    }
    x = reaktor_animate(7, 0, SDL_GetAtomicInt(&g_slide) ? 1.0f : 0.0f, 300.0f,
                        REAKTOR_EASE_CUBIC_IN_OUT);
    REAKTOR_COLUMN(.w = (float)win_w, .gap = 6.0f, .ml = 8.0f, .mt = 8.0f) {
        REAKTOR_ROW(.h = 32.0f, .gap = 6.0f) {
            reaktor_button(&(reaktor_button_spec){ .label = "One", .fit_content = 1 });
            reaktor_button(&(reaktor_button_spec){ .label = "Two", .icon = "add-outline",
                                                   .fit_content = 1 });
            reaktor_button(&(reaktor_button_spec){ .label = "Three", .tooltip = "A tip",
                                                   .fit_content = 1 });
            reaktor_button(&(reaktor_button_spec){ .label = "Toast", .fit_content = 1,
                                                   .on_press = { toast, app } });
            reaktor_button(&(reaktor_button_spec){ .label = "Go", .accent = 1,
                                                   .fit_content = 1 });
        }
        REAKTOR_ROW(.h = 32.0f, .gap = 6.0f) {
            reaktor_field(&(reaktor_field_spec){
                .buf = g_name, .len = &g_name_len, .cap = sizeof g_name, .name = "Name",
                .box = { .w = 220.0f } });
            reaktor_check(&(reaktor_check_spec){ .label = "Loud", .on = &g_loud });
            reaktor_radio(&(reaktor_radio_spec){ .label = "Left", .choice = &g_pick, .value = 0 });
            reaktor_radio(&(reaktor_radio_spec){ .label = "Right", .choice = &g_pick, .value = 1 });
        }
        REAKTOR_ROW(.h = 32.0f, .gap = 6.0f) {
            reaktor_slider(&(reaktor_slider_spec){ .name = "Level", .value = &g_level,
                                                   .lo = 0.0f, .hi = 1.0f, .step = 0.05f,
                                                   .box = { .w = 200.0f } });
            REAKTOR_COMBO(.label = fruit[g_fruit], .name = "Fruit", .body_h = 120.0f,
                          .box = { .w = 160.0f }) {
                for (i = 0; i < 3; i++)
                    if (reaktor_combo_item(fruit[i], i == g_fruit)) g_fruit = i;
            }
            reaktor_label(&(reaktor_label_spec){ .text = ticker, .name = "Ticker",
                                                 .box = { .w = 120.0f } });
        }
        REAKTOR_ROW(.h = 30.0f) {
            reaktor_gap(x * 240.0f, 1.0f);
            reaktor_button(&(reaktor_button_spec){ .label = "Slide", .name = "Slide",
                                                   .fit_content = 1 });
        }
        REAKTOR_ROW(.h = 130.0f, .gap = 6.0f) {
            reaktor_list(&(reaktor_list_spec){
                .name = "Fruits", .count = (int)SDL_arraysize(g_rows), .cell = cell,
                .selected = &g_row, .row_h = 24.0f, .box = { .w = 220.0f, .h = 130.0f } });
            REAKTOR_COLUMN(.gap = 4.0f) {
                reaktor_label(&(reaktor_label_spec){ .text = JAPANESE " " ARABIC,
                                                     .name = "Scripts" });
                reaktor_icon(&(reaktor_icon_spec){ .name = "add-outline",
                                                   .box = { .w = 48.0f, .h = 48.0f } });
            }
        }
    }
}

static App *g_app;

static void
move(reaktor_test *t, float x, float y)
{
    SDL_Event e;

    SDL_zero(e);
    e.type             = SDL_EVENT_MOUSE_MOTION;
    e.common.timestamp = SDL_GetTicksNS();
    e.motion.windowID  = SDL_GetWindowID(reaktor_sdl_window(g_app));
    e.motion.x         = x;
    e.motion.y         = y;
    SDL_PushEvent(&e);
    reaktor_wake(g_app);
    reaktor_test_settle(t);
}

static void
wheel(reaktor_test *t, float x, float y, float dy)
{
    SDL_Event e;

    move(t, x, y);
    SDL_zero(e);
    e.type             = SDL_EVENT_MOUSE_WHEEL;
    e.common.timestamp = SDL_GetTicksNS();
    e.wheel.windowID   = SDL_GetWindowID(reaktor_sdl_window(g_app));
    e.wheel.y          = dy;
    e.wheel.mouse_x    = x;
    e.wheel.mouse_y    = y;
    SDL_PushEvent(&e);
    reaktor_wake(g_app);
    reaktor_test_settle(t);
    reaktor_test_settle(t);
}

static void
sweep(reaktor_test *t, float y, float step)
{
    float x;

    for (x = 4.0f; x < 420.0f; x += step) move(t, x, y);
}

static void
peak_reset(App *app, void *user)
{
    (void)user;
    app->damage.peak = 0.0f;
}

static float g_scale_to;

static void
rescale(App *app, void *user)
{
    (void)user;
    reaktor_set_scale(g_scale_to);
    apply_render_scale(app);
    img_cache_clear(app);
    rebuild_font(app);
    app->dirty = 1;
}

static void
dark(App *app, void *user)
{
    (void)user;
    reaktor_set_theme(app, REAKTOR_THEME_DARK);
}

static void
unit(int ok, const char *what)
{
    g_units++;
    if (ok) return;
    g_unit_fails++;
    SDL_strlcat(g_unit_log, what, sizeof g_unit_log);
    SDL_strlcat(g_unit_log, "; ", sizeof g_unit_log);
}

static SDL_Rect
bx(int x0, int y0, int x1, int y1)
{
    SDL_Rect b;

    b.x = x0;
    b.y = y0;
    b.w = x1 - x0;
    b.h = y1 - y0;
    return b;
}

static void
list_of(reaktor_damage_list *l, const char *keys, const SDL_Rect *boxes,
        const char *scaled)
{
    int i;

    l->n = l->used = l->scaled_n = 0;
    l->ow = l->oh = 1000;
    l->laid_w = l->laid_h = 1000;
    l->s = 1.0f;
    for (i = 0; keys[i]; i++)
        reaktor_damage_add(l, &keys[i], 1, boxes[(keys[i] | 0x20) - 'a'],
                           scaled && SDL_strchr(scaled, keys[i]) != NULL);
}

static int
covers(const SDL_Rect *r, int n, SDL_Rect b)
{
    int i;

    for (i = 0; i < n; i++)
        if (r[i].x <= b.x && r[i].y <= b.y && r[i].x + r[i].w >= b.x + b.w &&
            r[i].y + r[i].h >= b.y + b.h)
            return 1;
    return 0;
}

static int
disjoint(const SDL_Rect *r, int n)
{
    int i, j;

    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (SDL_HasRectIntersection(&r[i], &r[j]))
                return 0;
    return 1;
}

static void
diff_units(void)
{
    SDL_Rect boxes[26], r[REAKTOR_DAMAGE_RECTS];
    reaktor_damage_list a, b;
    long long covered;
    int i, n, kind;

    SDL_zero(a);
    SDL_zero(b);
    for (i = 0; i < 26; i++) boxes[i] = bx(i * 37, (i % 5) * 190, i * 37 + 10, (i % 5) * 190 + 10);

    list_of(&a, "abcdefgh", boxes, NULL);
    list_of(&b, "abcdefgh", boxes, NULL);
    unit(reaktor_damage_diff(&a, &b, r, &n, &covered) == REAKTOR_DAMAGE_NONE, "same lists: none");

    list_of(&b, "abcxefgh", boxes, NULL);
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && n == 2 && covers(r, n, boxes['d' - 'a']) &&
         covers(r, n, boxes['x' - 'a']), "a changed item: both boxes");

    list_of(&b, "abcdyefgh", boxes, NULL);
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && n == 1 && covered == 100 &&
         SDL_memcmp(&r[0], &boxes['y' - 'a'], sizeof r[0]) == 0, "an insertion: its box alone");

    list_of(&b, "abcefgh", boxes, NULL);
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && n == 1 &&
         SDL_memcmp(&r[0], &boxes['d' - 'a'], sizeof r[0]) == 0, "a deletion: the old box");

    list_of(&b, "abdcefgh", boxes, NULL);
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && (covers(r, n, boxes['c' - 'a']) ||
                                            covers(r, n, boxes['d' - 'a'])),
         "a swap damages what moved");

    list_of(&a, "abcdefghijklmnop", boxes, NULL);
    list_of(&b, "AbCdEfGhIjKlMnOp", boxes, NULL);
    for (i = 0; i < b.n; i++) b.item[i].box = a.item[i].box;
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(n <= REAKTOR_DAMAGE_RECTS && disjoint(r, n), "many changes: four rectangles at most");
    for (i = 0; i < 16; i += 2)
        unit(kind == REAKTOR_DAMAGE_FULL || covers(r, n, a.item[i].box),
             "many changes: every box covered");

    boxes[0] = bx(100, 100, 200, 200);
    boxes[1] = bx(200, 150, 300, 250);
    list_of(&a, "ab", boxes, NULL);
    a.item[0].hash ^= 1;
    a.item[1].hash ^= 1;
    list_of(&b, "ab", boxes, NULL);
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && n == 1 &&
         SDL_RectsEqual(&r[0], &(SDL_Rect){ 100, 100, 200, 150 }),
         "touching boxes join");

    boxes[2] = bx(150, 150, 600, 160);
    list_of(&a, "abc", boxes, "c");
    list_of(&b, "xbc", boxes, "c");
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && covers(r, n, boxes[2]),
         "a stretched image the damage meets is redrawn whole");

    boxes[3] = bx(0, 0, 900, 900);
    list_of(&a, "d", boxes, NULL);
    list_of(&b, "e", boxes, NULL);
    b.item[0].box = boxes[3];
    unit(reaktor_damage_diff(&a, &b, r, &n, &covered) == REAKTOR_DAMAGE_FULL, "past half the window: full");

    boxes[4] = bx(3, 3, 5, 5);
    list_of(&a, "a", boxes, NULL);
    list_of(&b, "e", boxes, NULL);
    a.s = b.s = 2.0f;
    kind = reaktor_damage_diff(&a, &b, r, &n, &covered);
    unit(kind == REAKTOR_DAMAGE_PARTIAL && covers(r, n, boxes[4]) && r[0].x % 2 == 0 &&
         r[0].y % 2 == 0 && r[0].w % 2 == 0 && r[0].h % 2 == 0,
         "at scale 2 the edges fall on whole logical pixels");

    list_of(&a, "abc", boxes, NULL);
    list_of(&b, "abc", boxes, NULL);
    b.laid_w = 999;
    unit(reaktor_damage_diff(&a, &b, r, &n, &covered) == REAKTOR_DAMAGE_FULL, "a new layout size: full");

    SDL_free(a.item); SDL_free(a.key); SDL_free(a.scaled);
    SDL_free(b.item); SDL_free(b.key); SDL_free(b.scaled);
}

static int
clip_mask(SDL_Renderer *ren, const SDL_Rect *clip, Uint8 *out, int w, int h)
{
    SDL_Surface *s;
    int x, y;

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_SetRenderClipRect(ren, NULL);
    SDL_RenderClear(ren);
    if (clip) {
        SDL_SetRenderClipRect(ren, clip);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderFillRect(ren, NULL);
    }
    if (!(s = SDL_RenderReadPixels(ren, NULL))) return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            Uint8 r = 0, g = 0, b = 0;

            SDL_ReadSurfacePixel(s, x, y, &r, &g, &b, NULL);
            out[y * w + x] = r;
        }
    SDL_DestroySurface(s);
    return 1;
}

static int
clip_identity(float scale)
{
    enum { W = 24, H = 4 };
    SDL_Surface  *surf = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer *ren  = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    Uint8 mc[W * H], md[W * H], mi[W * H];
    int holds = 1, xc, wc, xd, wd, i;

    if (!ren) {
        SDL_DestroySurface(surf);
        return -1;
    }
    SDL_SetRenderScale(ren, scale, scale);
    for (xc = 0; xc < 8 && holds; xc++)
        for (wc = 1; wc < 6 && holds; wc++)
            for (xd = 0; xd < 8 && holds; xd++)
                for (wd = 1; wd < 6 && holds; wd++) {
                    SDL_Rect c = { xc, 0, wc, 2 }, d = { xd, 0, wd, 2 }, cd;
                    int any = SDL_GetRectIntersection(&c, &d, &cd);

                    clip_mask(ren, &c, mc, W, H);
                    clip_mask(ren, &d, md, W, H);
                    clip_mask(ren, any ? &cd : NULL, mi, W, H);
                    for (i = 0; i < W * H; i++)
                        if (mi[i] != (mc[i] & md[i])) holds = 0;
                }
    SDL_DestroyRenderer(ren);
    SDL_DestroySurface(surf);
    return holds;
}

static void
units(App *app, void *user)
{
    (void)app; (void)user;
    diff_units();
    unit(clip_identity(1.0f) == 1, "P(C and D) = P(C) and P(D) at scale 1");
    unit(clip_identity(2.0f) == 1, "P(C and D) = P(C) and P(D) at scale 2");
    unit(clip_identity(1.25f) == 0, "P(C and D) = P(C) and P(D) fails at scale 1.25");
}

static void
script(reaktor_test *t, void *user)
{
    reaktor_damage *d;
    unsigned long partial, full;
    char log[64];
    int i;

    (void)user;
    g_app = reaktor_test_app_of(t);
    d     = &g_app->damage;
    reaktor_post(g_app, units, NULL);
    reaktor_test_settle(t);
    reaktor_test_check(t, g_unit_fails == 0 && g_units > 0, "damage diff, merge and clip units");
    if (g_unit_fails) SDL_Log("test: units failed: %s", g_unit_log);

    reaktor_test_check(t, SDL_strcmp(SDL_GetRendererName(reaktor_sdl_renderer(g_app)),
                                     "software") == 0, "the window draws in software");

    reaktor_post(g_app, peak_reset, NULL);
    reaktor_test_settle(t);
    partial = d->partial;
    sweep(t, 58.0f, 3.0f);
    sweep(t, 58.0f, 47.0f);
    reaktor_test_check(t, d->partial > partial, "a hover sweep draws partial frames");
    SDL_snprintf(log, sizeof log, "hover damage under 5%% (peak %.2f%%)", d->peak * 100.0f);
    reaktor_test_check(t, d->peak < 0.05f, log);
    sweep(t, 20.0f, 3.0f);
    sweep(t, 20.0f, 47.0f);

    move(t, 160.0f, 20.0f);
    SDL_Delay(900);
    reaktor_test_settle(t);
    move(t, 600.0f, 400.0f);

    reaktor_test_type(t, "Name", "Hello " JAPANESE " " ARABIC);
    for (i = 0; i < 6; i++) reaktor_test_key(t, "Left");
    reaktor_test_key(t, "Home");
    reaktor_test_key(t, "End");
    reaktor_test_key(t, "Backspace");
    reaktor_test_type(t, NULL, JAPANESE);

    reaktor_test_click(t, "Loud");
    reaktor_test_click(t, "Right");
    sweep(t, 96.0f, 5.0f);

    reaktor_test_click(t, "Fruit");
    sweep(t, 140.0f, 9.0f);
    reaktor_test_key(t, "Escape");

    if (reaktor_test_check(t, reaktor_test_find(t, "Fruits"), "the list is there to scroll")) {
        for (i = 0; i < 6; i++) wheel(t, 100.0f, 210.0f, -1.0f);
        for (i = 0; i < 3; i++) wheel(t, 100.0f, 210.0f, 1.0f);
        for (i = 0; i < 12; i++) move(t, 40.0f + i * 10.0f, 180.0f + i * 6.0f);
    }

    reaktor_test_click(t, "Toast");
    reaktor_test_settle(t);
    sweep(t, 20.0f, 13.0f);

    move(t, 600.0f, 460.0f);
    for (i = 0; i < 8; i++) reaktor_test_key(t, "Tab");
    reaktor_test_key(t, "Shift+Tab");

    SDL_SetAtomicInt(&g_tick, 1);
    for (i = 0; i < 20; i++) move(t, 300.0f + i, 300.0f);
    SDL_SetAtomicInt(&g_slide, 1);
    reaktor_wake(g_app);
    SDL_Delay(500);
    reaktor_test_settle(t);
    SDL_SetAtomicInt(&g_slide, 0);
    reaktor_wake(g_app);
    SDL_Delay(500);
    reaktor_test_settle(t);
    SDL_SetAtomicInt(&g_tick, 0);
    if (reaktor_test_find(t, "Slide")) reaktor_test_click(t, "Slide");

    full = d->full;
    reaktor_post(g_app, dark, NULL);
    reaktor_test_settle(t);
    reaktor_test_settle(t);
    reaktor_test_check(t, d->full > full, "a theme switch redraws in full");
    partial = d->partial;
    sweep(t, 20.0f, 7.0f);
    reaktor_test_check(t, d->partial > partial, "the dark theme draws partial frames");

    g_scale_to = 2.0f;
    reaktor_post(g_app, rescale, NULL);
    reaktor_test_settle(t);
    partial = d->partial;
    for (i = 0; i < 40; i++) move(t, 4.0f + i * 4.0f, 10.0f);
    reaktor_test_type(t, "Name", JAPANESE);
    reaktor_test_check(t, d->partial > partial, "scale 2 draws partial frames");

    g_scale_to = 1.25f;
    reaktor_post(g_app, rescale, NULL);
    reaktor_test_settle(t);
    partial = d->partial;
    full    = d->full;
    for (i = 0; i < 30; i++) move(t, 4.0f + i * 6.0f, 14.0f);
    reaktor_test_check(t, d->partial == partial && d->full == full && !d->k,
                       "a fractional scale draws straight to the window");

    g_scale_to = 1.0f;
    reaktor_post(g_app, rescale, NULL);
    reaktor_test_settle(t);
    partial = d->partial;
    for (i = 0; i < 30; i++) move(t, 4.0f + i * 6.0f, 14.0f);
    reaktor_test_check(t, d->partial > partial, "back at scale 1, partial frames again");

    SDL_Delay(1300);
    reaktor_test_settle(t);
    reaktor_test_check(t, !d->k, "an idle second frees the last frame");
    partial = d->partial;
    full    = d->full;
    move(t, 600.0f, 400.0f);
    move(t, 30.0f, 20.0f);
    move(t, 110.0f, 20.0f);
    reaktor_test_check(t, d->k && d->full == full + 1 && d->partial > partial,
                       "after an idle second, one full frame and then partial ones");

    SDL_snprintf(log, sizeof log, "%lu partial and %lu full frames compared",
                 d->partial, d->full);
    reaktor_test_check(t, d->partial > 50, log);
}

int
main(int argc, char **argv)
{
    return reaktor_test_app(argc, argv, &(reaktor_launch){
        .name           = "Damagetest",
        .window         = { .w = 640, .h = 480 },
        .console        = REAKTOR_CONSOLE_PARENT,
        .font_fallbacks = { { .path = "assets/fonts/MPLUS1p-Regular.ttf" } },
        .page           = page }, script, NULL);
}
