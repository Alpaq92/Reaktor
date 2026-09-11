#include <stdio.h>

#include "internal.h"
#include "declare.h"
#include "keys.h"
#include "sample.h"

#define TEXT_CAP 65536

static struct {
    char     text[TEXT_CAP];
    int      len;
    char     path[SC_PATH_CAP];
    unsigned saved_hash;
    int      saved_len;
    char     status[160];
    int      seeded;
} g;

#define MENUBAR_H 30.0f
#define STATUS_H  26.0f

static unsigned
hash_of(const char *p, int n)
{
    unsigned h = 2166136261u;
    int i;

    for (i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 16777619u;
    }
    return h;
}

/* The length first, because it settles it without touching the text: an
 * insertion or a deletion changes it, and only an edit that happens to keep
 * the length the same has to be hashed. This runs on every drawn frame. */
static int
dirty(void)
{
    return g.len != g.saved_len || hash_of(g.text, g.len) != g.saved_hash;
}

static const char *
shown_name(void)
{
    const char *slash;

    if (!g.path[0]) return "Untitled";
    slash = SDL_strrchr(g.path, '/');
    if (!slash) slash = SDL_strrchr(g.path, '\\');
    return slash ? slash + 1 : g.path;
}

static void
new_file(void)
{
    g.text[0] = '\0';
    g.len = 0;
    g.path[0] = '\0';
    g.saved_hash = hash_of(g.text, 0);
    g.saved_len  = 0;
    SDL_strlcpy(g.status, "New file", sizeof(g.status));
}

static void
load(const char *path)
{
    size_t n = 0;
    char *body = reaktor_read_file(path, &n);

    if (!body) {
        SDL_snprintf(g.status, sizeof(g.status), "Could not read %s", path);
        return;
    }
    if (n >= TEXT_CAP) {
        SDL_snprintf(g.status, sizeof(g.status),
                     "%s is %.0f KB, and this editor holds %d KB",
                     path, (double)n / 1024.0, TEXT_CAP / 1024);
        reaktor_free(body);
        return;
    }
    SDL_memcpy(g.text, body, n);
    g.text[n] = '\0';
    g.len = (int)n;
    reaktor_free(body);

    SDL_strlcpy(g.path, path, sizeof(g.path));
    g.saved_hash = hash_of(g.text, g.len);
    g.saved_len  = g.len;
    SDL_snprintf(g.status, sizeof(g.status), "Opened %s", shown_name());
}

static void
save(void)
{
    FILE *f;

    if (!g.path[0]) {
        SDL_strlcpy(g.status,
                    "Nowhere to save to yet - open a file first",
                    sizeof(g.status));
        return;
    }
    f = fopen(g.path, "wb");
    if (!f) {
        SDL_snprintf(g.status, sizeof(g.status), "Could not write %s",
                     g.path);
        return;
    }
    if (g.len > 0) fwrite(g.text, 1, (size_t)g.len, f);
    fclose(f);

    g.saved_hash = hash_of(g.text, g.len);
    g.saved_len  = g.len;
    SDL_snprintf(g.status, sizeof(g.status), "Saved %s", shown_name());
}

static void
menu_cursor(App *app, struct nk_context *ctx)
{
    reaktor_hot_top(app, nk_window_get_bounds(ctx), 1, 0);
}

static void
quit_now(void)
{
    SDL_Event q;

    SDL_zero(q);
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
}

void
page_shell(App *app, struct nk_context *ctx, int win_w, int win_h)
{
    char line[220];

    (void)win_w; (void)win_h;

    if (!g.seeded) {
        new_file();
        SDL_strlcpy(g.status, "Ready", sizeof(g.status));
        g.seeded = 1;
    }

    reaktor_menu_style_push(ctx);
    nk_menubar_begin(ctx);

    {
        struct nk_style_item clear = nk_style_item_color(nk_rgba(0, 0, 0, 0));
        nk_style_push_style_item(ctx, &ctx->style.menu_button.normal, clear);
        nk_style_push_style_item(ctx, &ctx->style.menu_button.hover, clear);
        nk_style_push_style_item(ctx, &ctx->style.menu_button.active, clear);
    }

    nk_layout_row_begin(ctx, NK_STATIC, MENUBAR_H, 2);

    nk_layout_row_push(ctx, 60.0f);
    if (nk_menu_begin_label(ctx, "File", NK_TEXT_CENTERED,
                            nk_vec2(200.0f, reaktor_menu_height(4)))) {
        menu_cursor(app, ctx);
        nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
        if (reaktor_menu_item(app, ctx, "New", "Ctrl+N", 0))
            new_file();
        if (reaktor_menu_item(app, ctx, "Open...", "Ctrl+O", 0)) {
            if (!reaktor_file_open(app))
                SDL_strlcpy(g.status, "A dialog is already open",
                            sizeof(g.status));
        }
        if (reaktor_menu_item(app, ctx, "Save", "Ctrl+S", 0))
            save();
        if (reaktor_menu_item(app, ctx, "Quit", "Ctrl+Q", 0))
            quit_now();
        nk_menu_end(ctx);
    }

    nk_layout_row_push(ctx, 60.0f);
    if (nk_menu_begin_label(ctx, "Help", NK_TEXT_CENTERED,
                            nk_vec2(200.0f, reaktor_menu_height(2)))) {
        menu_cursor(app, ctx);
        nk_layout_row_dynamic(ctx, REAKTOR_MENU_ROW, 1);
        if (reaktor_menu_item(app, ctx, "Shortcuts", "Ctrl+K", 0))
            SDL_strlcpy(g.status,
                        "Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q quit",
                        sizeof(g.status));
        if (reaktor_menu_item(app, ctx, "About", NULL, 0))
            SDL_strlcpy(g.status,
                        "A text editor in one file, on Reaktor",
                        sizeof(g.status));
        nk_menu_end(ctx);
    }

    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    nk_style_pop_style_item(ctx);
    reaktor_menu_style_pop(ctx);

    REAKTOR_COLUMN(.h = nk_window_get_content_region_size(ctx).y,
                   .gap = 1.0f) {

        reaktor_field(&(reaktor_field_spec){
            .buf = g.text, .len = &g.len, .cap = TEXT_CAP,
            .name = "Document", .multiline = 1,
            .box = { .ml = 6.0f, .mr = 6.0f, .mt = 5.0f, .mb = 3.0f,
                     .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y } });

        REAKTOR_ROW(.h = STATUS_H, .flags = REAKTOR_LAY_FILL_X) {
            SDL_snprintf(line, sizeof(line), "%s%s  -  %d characters  -  %s",
                         shown_name(), dirty() ? " (modified)" : "",
                         g.len, g.status);

            nk_style_push_font(ctx, reaktor_style_font(app, ".status",
                                                       13, 0));
            reaktor_label(&(reaktor_label_spec){
                .text = line, .name = "Status", .value = line,
                .color = "--text-muted",
                .box = { .ml = 8.0f, .flags = REAKTOR_LAY_FILL_X |
                                              REAKTOR_LAY_CENTER_Y } });
            nk_style_pop_font(ctx);
        }
    }
}

int
sample_key(App *app, const SDL_Event *e)
{
    /* reaktor_chord rather than a raw modifier test: it folds Cmd and Ctrl
     * together, so the same table works on macOS without a second spelling. */
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_N)) { new_file(); return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_O)) {
        if (!reaktor_file_open(app))
            SDL_strlcpy(g.status, "A dialog is already open",
                        sizeof(g.status));
        return 1;
    }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_S)) { save(); return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_Q)) { quit_now(); return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_K)) {
        SDL_strlcpy(g.status,
                    "Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q quit",
                    sizeof(g.status));
        return 1;
    }
    return 0;
}

void
sample_args(App *app, int argc, char **argv)
{
    (void)app;
    if (argc < 2 || !argv[1] || !argv[1][0]) return;
    g.seeded = 1;
    new_file();
    load(argv[1]);
}

void
sample_window(reaktor_window_spec *out)
{
    out->w = 820;
    out->h = 600;
    out->title = "Notepad";
}

SDL_HitTestResult SDLCALL
window_hit_test(SDL_Window *win, const SDL_Point *pt, void *data)
{
    (void)win; (void)pt; (void)data;
    return SDL_HITTEST_NORMAL;
}

void
sample_file_taken(App *app)
{
    char answer[SC_PATH_CAP];

    if (!reaktor_file_taken(app, answer, (int)sizeof(answer))) return;
    if (SDL_strcmp(answer, "canceled") == 0) {
        SDL_strlcpy(g.status, "Canceled", sizeof(g.status));
        return;
    }
    load(answer);
}
