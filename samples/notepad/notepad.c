#include <stdio.h>
#include <string.h>

#include "reaktor/reaktor.h"
#include "reaktor/main.h"

static struct {
    char    *text;
    int      len, cap;
    char     find[128];
    int      find_len, find_open, find_at;
    char     path[520];
    unsigned saved_hash;
    int      saved_len;
    char     status[160];
    int      seeded;
} g;

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

static int
room(int n)
{
    char *more;

    if (n < g.cap) return 1;
    more = (char *)SDL_realloc(g.text, (size_t)n + 256);
    if (!more) return 0;
    g.text = more;
    g.cap  = n + 256;
    return 1;
}

static void
new_file(void)
{
    if (!room(0)) return;
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
    char *body = (char *)SDL_LoadFile(path, &n);

    if (!body) {
        SDL_snprintf(g.status, sizeof(g.status), "Could not read %s", path);
        return;
    }
    if (n > (size_t)SDL_MAX_SINT32 - 256 || !room((int)n)) {
        SDL_snprintf(g.status, sizeof(g.status), "%s does not fit in memory", path);
        SDL_free(body);
        return;
    }
    SDL_memcpy(g.text, body, n);
    g.text[n] = '\0';
    g.len = (int)n;
    SDL_free(body);

    SDL_strlcpy(g.path, path, sizeof(g.path));
    g.saved_hash = hash_of(g.text, g.len);
    g.saved_len  = g.len;
    SDL_snprintf(g.status, sizeof(g.status), "Opened %s", shown_name());
}

static const reaktor_file_filter g_filters[] = {
    { "Text", "txt;md;c;h;css;json" },
    { "All files", "*" }
};

static void save(App *app);

static void
opened(App *app, const char *const *paths, int count, void *user)
{
    (void)app; (void)user;
    if (count) load(paths[0]);
}

static void
saved_as(App *app, const char *const *paths, int count, void *user)
{
    (void)user;
    if (!count) return;
    SDL_strlcpy(g.path, paths[0], sizeof(g.path));
    save(app);
}

static void
ask_file(App *app, int kind)
{
    if (!reaktor_file_dialog_open(app, &(reaktor_file_dialog){
            .kind = kind, .filters = g_filters,
            .filter_count = (int)SDL_arraysize(g_filters),
            .chosen = kind == REAKTOR_FILE_SAVE ? saved_as : opened }))
        SDL_strlcpy(g.status, "A dialog is already open", sizeof(g.status));
}

static void
save(App *app)
{
    SDL_IOStream *io;
    int ok;

    if (!g.path[0]) {
        ask_file(app, REAKTOR_FILE_SAVE);
        return;
    }
    io = SDL_IOFromFile(g.path, "wb");
    ok = io != NULL;
    if (ok && g.len > 0) ok = SDL_WriteIO(io, g.text, (size_t)g.len) == (size_t)g.len;
    if (io && !SDL_CloseIO(io)) ok = 0;
    if (!ok) {
        SDL_snprintf(g.status, sizeof(g.status), "Could not write %s",
                     g.path);
        return;
    }

    g.saved_hash = hash_of(g.text, g.len);
    g.saved_len  = g.len;
    SDL_snprintf(g.status, sizeof(g.status), "Saved %s", shown_name());
}

#define PAGE_LINES 60

static int
line_count(void)
{
    int i, n = 1;

    for (i = 0; i < g.len; i++) n += g.text[i] == '\n';
    return n;
}

/* One printed page: its sixty lines on white, as plain labels. */
static void
print_page(App *app, struct reaktor_context *ctx, int index, int w, int h, void *user)
{
    const char *p = g.text, *end = g.text + g.len;
    char line[256];
    int skip = index * PAGE_LINES, k;

    (void)app; (void)user;
    reaktor_fill_rect(reaktor_window_get_canvas(ctx), reaktor_rect(0, 0, (float)w, (float)h),
                      0.0f, reaktor_rgb(255, 255, 255));
    while (skip > 0 && p < end) if (*p++ == '\n') skip--;
    REAKTOR_FREE(.w = (float)w, .h = (float)h) {
        REAKTOR_COLUMN(.w = (float)w - 96.0f, .ml = 48.0f, .mt = 40.0f) {
            for (k = 0; k < PAGE_LINES && p < end; k++) {
                const char *nl = memchr(p, '\n', (size_t)(end - p));
                int n = (int)((nl ? nl : end) - p);

                SDL_snprintf(line, sizeof line, "%.*s", n < 255 ? n : 255, p);
                reaktor_label(&(reaktor_label_spec){ .text = line, .silent = 1,
                                                     .color = "#000000",
                                                     .box = { .h = 12.5f } });
                p += n + (nl != NULL);
            }
        }
    }
}

static void
printed(App *app, int ok, void *user)
{
    (void)app; (void)user;
    SDL_strlcpy(g.status, ok ? "Printed" : "Not printed", sizeof(g.status));
}

static reaktor_print_spec
print_spec(void)
{
    return (reaktor_print_spec){ .title = shown_name(),
                                 .pages = (line_count() + PAGE_LINES - 1) / PAGE_LINES,
                                 .page = print_page, .done = printed };
}

static void
exported(App *app, const char *const *paths, int count, void *user)
{
    reaktor_print_spec s = print_spec();

    (void)user;
    if (count && reaktor_print_pdf(app, paths[0], &s))
        SDL_strlcpy(g.status, "Writing the PDF...", sizeof(g.status));
}

static void
find_next(App *app)
{
    int i, k, line = 1;

    g.find[g.find_len] = '\0';
    if (!g.find_len) return;
    for (k = 0; k < 2; k++, g.find_at = 0)
        for (i = g.find_at; i + g.find_len <= g.len; i++)
            if (SDL_memcmp(g.text + i, g.find, (size_t)g.find_len) == 0) {
                g.find_at = i + g.find_len;
                reaktor_field_select(app, g.text, i, g.find_at);
                while (--i >= 0) line += g.text[i] == '\n';
                SDL_snprintf(g.status, sizeof(g.status), "Found on line %d", line);
                return;
            }
    SDL_snprintf(g.status, sizeof(g.status), "\"%s\" is not there", g.find);
}

static void
page_shell(App *app, struct reaktor_context *ctx, int win_w, int win_h)
{
    char line[220];

    (void)win_w; (void)win_h;

    if (!g.seeded) {
        new_file();
        SDL_strlcpy(g.status, "Ready", sizeof(g.status));
        g.seeded = 1;
    }

    REAKTOR_COLUMN(.h = reaktor_window_get_content_region_size(ctx).y,
                   .gap = 1.0f) {
        {
            static const reaktor_menu_entry file[] = {
                { .label = "New",     .keys = "Ctrl+N" },
                { .label = "Open...", .keys = "Ctrl+O" },
                { .label = "Save",    .keys = "Ctrl+S" },
                { NULL },
                { .label = "Print...",          .keys = "Ctrl+P" },
                { .label = "Export as PDF..." },
                { NULL },
                { .label = "Quit",    .keys = "Ctrl+Q" }
            };
            static const reaktor_menu_entry edit[] = {
                { .label = "Find...",   .keys = "Ctrl+F" },
                { .label = "Find next", .keys = "F3" }
            };
            static const reaktor_menu_entry help[] = {
                { .label = "Shortcuts", .keys = "Ctrl+K" },
                { .label = "About" }
            };
            static const reaktor_menu menus[3] = {
                { "File", file, 8 }, { "Edit", edit, 2 }, { "Help", help, 2 }
            };
            int m = -1, i = -1;

            if (reaktor_menubar(&(reaktor_menubar_spec){
                    .menus = menus, .count = 3,
                    .box = { .ml = 4.0f, .mt = 6.0f, .mb = 2.0f },
                    .picked_menu = &m, .picked_item = &i })) {
                if (m == 0 && i == 0) new_file();
                if (m == 0 && i == 1) ask_file(app, REAKTOR_FILE_OPEN);
                if (m == 0 && i == 2) save(app);
                if (m == 0 && i == 4) {
                    reaktor_print_spec ps = print_spec();

                    reaktor_print(app, &ps);
                }
                if (m == 0 && i == 5 &&
                    !reaktor_file_dialog_open(app, &(reaktor_file_dialog){
                        .kind = REAKTOR_FILE_SAVE, .chosen = exported,
                        .filters = (const reaktor_file_filter[]){ { "PDF", "pdf" } },
                        .filter_count = 1 }))
                    SDL_strlcpy(g.status, "A dialog is already open", sizeof(g.status));
                if (m == 0 && i == 7) reaktor_request_quit(app);
                if (m == 1 && i == 0) g.find_open = 1;
                if (m == 1 && i == 1) find_next(app);
                if (m == 2 && i == 0)
                    SDL_strlcpy(g.status,
                                "Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+F find, F3 next, Ctrl+Q quit",
                                sizeof(g.status));
                if (m == 2 && i == 1)
                    SDL_strlcpy(g.status, "A text editor in one file, on Reaktor",
                                sizeof(g.status));
            }
        }

        if (g.find_open)
            REAKTOR_ROW(.h = 30.0f, .gap = 6.0f, .ml = 6.0f, .mr = 6.0f, .mt = 4.0f,
                        .flags = REAKTOR_LAY_FILL_X) {
                reaktor_field(&(reaktor_field_spec){
                    .buf = g.find, .len = &g.find_len, .cap = (int)sizeof g.find,
                    .hint = "Find", .name = "Find",
                    .box = { .flags = REAKTOR_LAY_FILL_X } });
                if (reaktor_button(&(reaktor_button_spec){ .label = "Next", .keys = "F3",
                                                           .box = { .w = 80.0f } }))
                    find_next(app);
                if (reaktor_button(&(reaktor_button_spec){ .label = "Close",
                                                           .box = { .w = 80.0f } }))
                    g.find_open = 0;
            }

        reaktor_field(&(reaktor_field_spec){
            .heap = &g.text, .heap_cap = &g.cap, .len = &g.len,
            .name = "Document", .multiline = 1, .line_numbers = 1,
            .box = { .ml = 6.0f, .mr = 6.0f, .mt = 2.0f, .mb = 3.0f,
                     .flags = REAKTOR_LAY_FILL } });

        REAKTOR_ROW(.h = STATUS_H, .flags = REAKTOR_LAY_FILL_X) {
            SDL_snprintf(line, sizeof(line), "%s%s  -  %d characters  -  %s",
                         shown_name(), dirty() ? " (modified)" : "",
                         g.len, g.status);

            reaktor_style_push_font(ctx, reaktor_style_font(app, ".status",
                                                            13, 0));
            reaktor_label(&(reaktor_label_spec){
                .text = line, .name = "Status", .value = line,
                .color = "--text-muted",
                .box = { .ml = 8.0f, .flags = REAKTOR_LAY_FILL_X |
                                              REAKTOR_LAY_CENTER_Y } });
            reaktor_style_pop_font(ctx);
        }
    }
}

static int
sample_key(App *app, const SDL_Event *e)
{
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_N)) { new_file(); return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_O)) {
        ask_file(app, REAKTOR_FILE_OPEN);
        return 1;
    }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_S)) { save(app); return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_F)) { g.find_open = 1; return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_P)) {
        reaktor_print_spec ps = print_spec();

        reaktor_print(app, &ps);
        return 1;
    }
    if (reaktor_chord(e, 0, SDLK_F3)) { find_next(app); return 1; }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_Q)) {
        reaktor_request_quit(app);
        return 1;
    }
    if (reaktor_chord(e, REAKTOR_MOD_CTRL, SDLK_K)) {
        SDL_strlcpy(g.status,
                    "Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+F find, F3 next, Ctrl+Q quit",
                    sizeof(g.status));
        return 1;
    }
    return 0;
}

static int
sample_start(App *app, int argc, char **argv)
{
    (void)app;
    if (argc < 2 || !argv[1] || !argv[1][0]) return 0;
    g.seeded = 1;
    new_file();
    load(argv[1]);
    return 0;
}

static void
sample_file_opened(App *app, const char *path)
{
    (void)app;
    if (!path) {
        SDL_strlcpy(g.status, "No file opened", sizeof(g.status));
        return;
    }
    load(path);
}

int
main(int argc, char **argv)
{
    return launchApp(argc, argv, &(reaktor_launch){
        .name        = "Notepad",
        .window      = { .w = 820, .h = 600 },
        .page        = page_shell,
        .key         = sample_key,
        .start       = sample_start,
        .file_opened = sample_file_opened,
        .remember_window = 1,
        .save_settings   = REAKTOR_SAVE_ON_QUIT });
}
