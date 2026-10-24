#include "internal.h"
#include "layout.h"
#include "reaktor/launch.h"
#include "reaktor/keys.h"

#define LOG_MAX  1000
#define LOG_TEXT 240
#define ROWS     40
#define CSS_CAP  4096

enum { TAB_TREE, TAB_BOX, TAB_STYLE, TAB_FRAME, TAB_LOGS, TABS };

typedef struct log_entry {
    Uint64          ms;
    int             category;
    SDL_LogPriority priority;
    char            text[LOG_TEXT];
} log_entry;

static App                  *g_main;
static reaktor_inspector_spec g_spec;
static int                   g_attached, g_win, g_floater, g_tab, g_picking;
static unsigned              g_sel;

static reaktor_a11y_node     g_node[REAKTOR_A11Y_MAX_NODES];
static int                   g_parent[REAKTOR_A11Y_MAX_NODES];
static char                  g_label[REAKTOR_A11Y_MAX_NODES][96];
static unsigned char         g_open[REAKTOR_A11Y_MAX_NODES];
static int                   g_n, g_sel_ix = -1;

static char                  g_kv[ROWS][2][160];
static int                   g_rows;

static unsigned              g_frames, g_why;

static char                  g_css[CSS_CAP], g_css_applied[CSS_CAP];
static int                   g_css_len, g_css_on;

static SDL_Mutex            *g_log_lock;
static log_entry             g_log[LOG_MAX];
static int                   g_log_head, g_log_n, g_log_min;
static int                   g_log_row[LOG_MAX], g_log_rows;
static SDL_LogOutputFunction g_log_next;
static void                 *g_log_next_user;

static int  shown(void);
static void redraw_window_only(void);

static void SDLCALL
log_take(void *user, int category, SDL_LogPriority priority, const char *message)
{
    (void)user;
    if (g_log_lock) {
        log_entry *e;

        SDL_LockMutex(g_log_lock);
        e = &g_log[(g_log_head + g_log_n) % LOG_MAX];
        if (g_log_n < LOG_MAX) g_log_n++;
        else g_log_head = (g_log_head + 1) % LOG_MAX;
        e->ms       = SDL_GetTicks();
        e->category = category;
        e->priority = priority;
        SDL_strlcpy(e->text, message, sizeof e->text);
        SDL_UnlockMutex(g_log_lock);
        if (g_main && shown() && g_tab == TAB_LOGS) {
            redraw_window_only();
            reaktor_wake(g_main);
        }
    }
    if (g_log_next) g_log_next(g_log_next_user, category, priority, message);
}

static int
shown(void)
{
    return g_win || g_floater;
}

static void
redraw_window_only(void)
{
    App *w = g_win ? reaktor_window_app(g_win) : NULL;

    if (w) w->dirty = 1;
}

static void
redraw(void)
{
    redraw_window_only();
    if (g_main) {
        g_main->dirty = 1;
        reaktor_wake(g_main);
    }
}

static void
snapshot(void)
{
    const reaktor_a11y_node *t = reaktor_a11y_tree(&g_main->a11y, &g_n);
    int i, j;

    if (g_n > REAKTOR_A11Y_MAX_NODES) g_n = REAKTOR_A11Y_MAX_NODES;
    g_sel_ix = -1;
    for (i = 0; i < g_n; i++) {
        g_node[i]   = t[i];
        g_parent[i] = -1;
        for (j = i - 1; j >= 0; j--)
            if (t[j].id == t[i].parent) { g_parent[i] = j; break; }
        if (t[i].name && *t[i].name)
            SDL_snprintf(g_label[i], sizeof g_label[i], "%s \"%s\"",
                         reaktor_a11y_role_name(t[i].role), t[i].name);
        else
            SDL_strlcpy(g_label[i], reaktor_a11y_role_name(t[i].role), sizeof g_label[i]);
        if (t[i].id == g_sel) g_sel_ix = i;
    }
}

static const reaktor_a11y_node *
selected(void)
{
    return g_sel_ix >= 0 ? &g_node[g_sel_ix] : NULL;
}

static void
row(const char *k, const char *fmt, ...)
{
    va_list ap;

    if (g_rows >= ROWS) return;
    SDL_strlcpy(g_kv[g_rows][0], k, sizeof g_kv[g_rows][0]);
    va_start(ap, fmt);
    SDL_vsnprintf(g_kv[g_rows][1], sizeof g_kv[g_rows][1], fmt, ap);
    va_end(ap);
    g_rows++;
}

static const char *
kv_cell(int r, int c, void *user)
{
    (void)user;
    return g_kv[r][c];
}

static void
kv_list(const char *name)
{
    static const char *const cols[] = { "Property", "Value" };
    static const float widths[] = { 2.0f, 3.0f };

    reaktor_list(&(reaktor_list_spec){
        .name = name, .count = g_rows, .columns = cols, .widths = widths,
        .column_count = 2, .cell = kv_cell,
        .box = { .flags = REAKTOR_LAY_FILL } });
}

static void
note(const char *text)
{
    reaktor_label(&(reaktor_label_spec){ .text = text, .wrap = 1,
                                         .box = { .flags = REAKTOR_LAY_FILL_X,
                                                  .ml = 16.0f, .mr = 12.0f } });
}

static int
tree_parent(int node, void *user)
{
    (void)user;
    return g_parent[node];
}

static const char *
tree_label(int node, void *user)
{
    (void)user;
    return g_label[node];
}

static void
tab_tree(void)
{
    int sel = g_sel_ix;

    reaktor_tree(&(reaktor_tree_spec){
        .name = "Widgets", .count = g_n, .parent = tree_parent, .label = tree_label,
        .open = g_open, .selected = &sel,
        .box = { .flags = REAKTOR_LAY_FILL } });
    if (sel != g_sel_ix && sel >= 0 && sel < g_n) {
        g_sel    = g_node[sel].id;
        g_sel_ix = sel;
        redraw();
    }
}

static const char *
size_of(float v, unsigned flags, unsigned fill, unsigned sized)
{
    if (flags & fill) return "fills its parent";
    if (v <= 0.0f) return "from its content";
    if (flags & sized) return "from the stylesheet or its text";
    return "fixed by the page: a rule cannot change it";
}

static void
tab_box(void)
{
    const reaktor_a11y_node *n = selected();
    struct nk_rect r;
    unsigned flags;
    unsigned char dir;
    float w, h;

    g_rows = 0;
    if (!n) {
        note("Choose a widget in the Tree tab, or pick one in the app.");
        return;
    }
    row("Role", "%s", reaktor_a11y_role_name(n->role));
    row("Name", "%s", n->name ? n->name : "");
    if (n->value) row("Value", "%s", n->value);
    row("Id", "%08x", n->id);
    row("Bounds", "%.0f, %.0f  %.0f x %.0f", n->bounds.x, n->bounds.y,
        n->bounds.w, n->bounds.h);
    if (reaktor_layout_rect(&g_main->lay, n->id, &r))
        row("Laid out, in its parent", "%.0f, %.0f  %.0f x %.0f", r.x, r.y, r.w, r.h);
    if (reaktor_layout_info(&g_main->lay, n->id, &flags, &dir, &w, &h)) {
        if (n->role == REAKTOR_A11Y_GROUP)
            row("Direction", "%s", dir == REAKTOR_LAY_ROW ? "row"
                                 : dir == REAKTOR_LAY_COLUMN ? "column" : "free");
        row("Width", "%.0f, %s", w, size_of(w, flags, REAKTOR_LAY_FILL_X, REAKTOR_LAY_SIZED_W));
        row("Height", "%.0f, %s", h, size_of(h, flags, REAKTOR_LAY_FILL_Y, REAKTOR_LAY_SIZED_H));
        row("Flags", "%s%s%s%s%s",
            flags & REAKTOR_LAY_WRAP ? "wrap " : "",
            flags & REAKTOR_LAY_CENTER_X ? "center-x " : "",
            flags & REAKTOR_LAY_CENTER_Y ? "center-y " : "",
            flags & REAKTOR_LAY_PACK_CENTER ? "pack-center " : "",
            flags & REAKTOR_LAY_PACK_END ? "pack-end " : "");
        row("Margins from the sheet", "%s%s%s%s",
            flags & REAKTOR_LAY_SHEET_L ? "left " : "",
            flags & REAKTOR_LAY_SHEET_T ? "top " : "",
            flags & REAKTOR_LAY_SHEET_R ? "right " : "",
            flags & REAKTOR_LAY_SHEET_B ? "bottom " : "");
    }
    row("State", "%s%s%s%s%s",
        n->state & REAKTOR_A11Y_FOCUSED ? "focused " : "",
        n->state & REAKTOR_A11Y_CHECKED ? "checked " : "",
        n->state & REAKTOR_A11Y_SELECTED ? "selected " : "",
        n->state & REAKTOR_A11Y_DISABLED ? "disabled " : "",
        n->state & REAKTOR_A11Y_OFFSCREEN ? "offscreen " : "");
    kv_list("Box");
}

static const char *
selector_of(unsigned char role)
{
    switch (role) {
    case REAKTOR_A11Y_BUTTON:     return "button";
    case REAKTOR_A11Y_LINK:       return "a";
    case REAKTOR_A11Y_CHECKBOX:   return "input.type-checkbox";
    case REAKTOR_A11Y_RADIO:      return "input.type-radio";
    case REAKTOR_A11Y_TEXTBOX:
    case REAKTOR_A11Y_SPINBUTTON: return "input";
    case REAKTOR_A11Y_COMBOBOX:   return "select";
    case REAKTOR_A11Y_SLIDER:     return "input.type-range";
    case REAKTOR_A11Y_PROGRESS:   return "progress";
    case REAKTOR_A11Y_MENU:
    case REAKTOR_A11Y_MENUITEM:   return "menu";
    case REAKTOR_A11Y_DIALOG:     return "dialog";
    case REAKTOR_A11Y_LABEL:      return "p";
    default:                      return "body";
    }
}

static void
color_row(const char *k, const unsigned char c[4])
{
    row(k, "#%02x%02x%02x%02x", c[0], c[1], c[2], c[3]);
}

static void
apply_css(void)
{
    g_css_on = g_css_len > 0;
    SDL_memcpy(g_css_applied, g_css, (size_t)g_css_len);
    if (g_css_on)
        reaktor_asset_register(".reaktor-inspector.css", g_css_applied, (size_t)g_css_len);
    reaktor_restyle();
}

static void
tab_style(void)
{
    const reaktor_a11y_node *n = selected();
    const char *sel = n ? selector_of(n->role) : "body";
    reaktor_style st;

    g_rows = 0;
    reaktor_style_get(sel, &st);
    row("Selector", "%s%s", sel, st.matched ? "" : "  (no rule matches)");
    color_row("color", st.fg);
    color_row("background", st.bg);
    color_row("border-color", st.border_col);
    row("border-width", "%.0f %.0f %.0f %.0f", st.border_w[0], st.border_w[1],
        st.border_w[2], st.border_w[3]);
    row("border-radius", "%.0f %.0f %.0f %.0f", st.radius[0], st.radius[1],
        st.radius[2], st.radius[3]);
    row("padding", "%.0f %.0f %.0f %.0f", st.pad[0], st.pad[1], st.pad[2], st.pad[3]);
    row("margin", "%.0f %.0f %.0f %.0f", st.margin[0], st.margin[1], st.margin[2],
        st.margin[3]);
    row("width / height", "%.0f / %.0f", st.width, st.height);
    row("min", "%.0f x %.0f", st.min_width, st.min_height);
    row("max", "%.0f x %.0f", st.max_width, st.max_height);
    row("font", "%dpx%s", st.font_px, st.bold ? " bold" : "");
    REAKTOR_COLUMN(.flags = REAKTOR_LAY_FILL, .gap = 8.0f) {
        kv_list("Style");
        note("CSS here applies after every other sheet, to this run only.");
        reaktor_field(&(reaktor_field_spec){
            .buf = g_css, .len = &g_css_len, .cap = CSS_CAP, .multiline = 1,
            .name = "Extra CSS", .hint = "button { border-radius: 0 }",
            .pad_x = 10.0f, .pad_y = 8.0f,
            .box = { .flags = REAKTOR_LAY_FILL_X, .h = 120.0f, .ml = 12.0f, .mr = 12.0f } });
        REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X, .gap = 2.0f, .ml = 12.0f) {
            if (reaktor_button(&(reaktor_button_spec){ .label = "Apply", .accent = 1,
                                                       .fit_content = 1 }))
                apply_css();
            if (reaktor_button(&(reaktor_button_spec){ .label = "Remove", .fit_content = 1,
                                                       .disabled = (unsigned char)!g_css_on })) {
                g_css_len = 0;
                apply_css();
            }
        }
    }
}

static void
tab_frame(void)
{
    reaktor_diag d;

    g_rows = 0;
    reaktor_diagnostics(g_main, &d);
    row("Frames drawn", "%u", g_frames);
    row("Last frame ran for", "%s%s%s%s%s",
        g_why & REAKTOR_WHY_INPUT ? "input " : "",
        g_why & REAKTOR_WHY_RESIZE ? "resize " : "",
        g_why & REAKTOR_WHY_MOTION ? "motion " : "",
        g_why & REAKTOR_WHY_STYLE ? "stylesheets " : "",
        g_why ? "" : "a timer, a wake or an animation");
    row("Build", "%.2f ms", d.build_ms);
    row("Render", "%.2f ms", d.render_ms);
    row("Present", "%.2f ms", d.present_ms);
    row("Since the previous frame", "%.1f ms", d.frame_gap_ms);
    row("Stylesheets", "%d, loaded in %.2f ms", d.sheets, d.style_ms);
    row("Renderer", "%s, %s", d.renderer ? d.renderer : "", d.mode ? d.mode : "");
    row("Scale", "%.2f", d.scale);
    row("Process memory", "%lu KiB", d.rss_bytes / 1024ul);
    kv_list("Frame");
}

static const char *
log_cell(int r, int c, void *user)
{
    static const char *const level[] = { "", "trace", "verbose", "debug", "info",
                                         "warn", "error", "critical" };
    static char buf[32];
    const log_entry *e = &g_log[g_log_row[r]];

    (void)user;
    switch (c) {
    case 0:
        SDL_snprintf(buf, sizeof buf, "%llu.%03llu", (unsigned long long)(e->ms / 1000),
                     (unsigned long long)(e->ms % 1000));
        return buf;
    case 1:
        return (unsigned)e->priority < SDL_arraysize(level) ? level[e->priority] : "?";
    default:
        return e->text;
    }
}

static void
tab_logs(void)
{
    static const char *const cols[] = { "Time", "Level", "Message" };
    static const float widths[] = { 1.0f, 1.0f, 6.0f };
    static const char *const levels[] = { "Everything", "Debug and up", "Info and up",
                                          "Warnings and errors", "Errors" };
    static const SDL_LogPriority floor_of[] = { SDL_LOG_PRIORITY_TRACE,
        SDL_LOG_PRIORITY_DEBUG, SDL_LOG_PRIORITY_INFO, SDL_LOG_PRIORITY_WARN,
        SDL_LOG_PRIORITY_ERROR };
    int i;

    REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X, .gap = 8.0f) {
        reaktor_spinner(&(reaktor_spinner_spec){
            .label = "Show:", .items = levels, .count = 5, .chosen = &g_log_min,
            .box = { .w = 300.0f, .ml = 12.0f, .mb = 8.0f } });
    }
    SDL_LockMutex(g_log_lock);
    for (g_log_rows = i = 0; i < g_log_n; i++) {
        int k = (g_log_head + i) % LOG_MAX;

        if (g_log[k].priority >= floor_of[g_log_min]) g_log_row[g_log_rows++] = k;
    }
    SDL_UnlockMutex(g_log_lock);
    reaktor_list(&(reaktor_list_spec){
        .name = "Log", .count = g_log_rows, .columns = cols, .widths = widths,
        .column_count = 3, .cell = log_cell,
        .box = { .flags = REAKTOR_LAY_FILL } });
}

/* A flat icon beside the tabs; its name is also what the pointer shows. */
static int
tool(const char *icon, const char *name, const char *tip, int on, int off, float mr)
{
    return reaktor_button(&(reaktor_button_spec){
        .icon = icon, .name = name, .tooltip = tip ? tip : name, .flat = 1,
        .accent = (unsigned char)on, .disabled = (unsigned char)off,
        .box = { .w = 24.0f, .h = 32.0f, .mr = mr } });
}

static void
page(App *app, struct reaktor_context *ctx, int w, int h, void *user)
{
    static const char *const tabs[TABS] = { "Tree", "Box", "Style", "Frame", "Logs" };

    (void)app; (void)ctx; (void)user; (void)w;
    if (!g_main) return;
    snapshot();
    REAKTOR_COLUMN(.h = (float)h - 12.0f, .gap = 10.0f, .flags = REAKTOR_LAY_FILL_X) {
        REAKTOR_ROW(.flags = REAKTOR_LAY_FILL_X, .gap = 2.0f) {
            reaktor_tabs(&(reaktor_tabs_spec){ .items = tabs, .count = TABS, .chosen = &g_tab,
                                               .name = "Inspector",
                                               .box = { .flags = REAKTOR_LAY_FILL_X } });
            if (g_tab == TAB_LOGS && tool("checkmark-circle-outline", "Clear the log", NULL, 0, 0, 0.0f)) {
                SDL_LockMutex(g_log_lock);
                g_log_n = g_log_head = 0;
                SDL_UnlockMutex(g_log_lock);
            }
            if (tool("help-circle-outline", "Pick a widget",
                     g_picking ? "Click a widget in the app" : "Pick a widget in the app",
                     g_picking, 0, 0.0f)) {
                g_picking = !g_picking;
                redraw();
            }
#ifdef __EMSCRIPTEN__
            if (tool("remove-circle-outline", "Clear the selection", NULL, 0, g_sel == 0, 0.0f)) {
                g_sel = 0;
                redraw();
            }
            /* The page's floater has no titlebar to close it from. */
            if (tool("close-circle-outline", "Close the Inspector", NULL, 0, 0, 8.0f))
                reaktor_inspector_close(g_main);
#else
            if (tool("remove-circle-outline", "Clear the selection", NULL, 0, g_sel == 0, 8.0f)) {
                g_sel = 0;
                redraw();
            }
#endif
        }
        REAKTOR_COLUMN(.flags = REAKTOR_LAY_FILL, .gap = 8.0f) {
            switch (g_tab) {
            case TAB_TREE:  tab_tree();  break;
            case TAB_BOX:   tab_box();   break;
            case TAB_STYLE: tab_style(); break;
            case TAB_FRAME: tab_frame(); break;
            default:        tab_logs();  break;
            }
        }
    }
}

static void
closed(App *app, void *user)
{
    (void)app; (void)user;
    g_win = g_floater = 0;
    g_picking = 0;
    redraw();
}

int
reaktor_inspector_open(App *app)
{
    char title[160];

    (void)app;
    if (!g_attached || !g_main) return 0;
    if (shown()) return 1;
    SDL_snprintf(title, sizeof title, "%s - Inspector",
                 SDL_GetWindowTitle(g_main->win) ? SDL_GetWindowTitle(g_main->win) : "Reaktor");
#ifdef __EMSCRIPTEN__
    g_floater = reaktor_floater_open(g_main, &(reaktor_floater){
        .title = "Inspector", .w = 560.0f, .h = 600.0f, .body = page, .closed = closed });
#else
    g_win = reaktor_window_open(g_main, &(reaktor_window){
        .window = { .title = title, .w = 560, .h = 680, .min_w = 360, .min_h = 320 },
        .page = page, .closed = closed });
#endif
    redraw();
    return shown();
}

void
reaktor_inspector_close(App *app)
{
    (void)app;
    if (g_win) reaktor_window_close(g_main, g_win);
    if (g_floater) reaktor_floater_close(g_main, g_floater);
    closed(g_main, NULL);
}

int
reaktor_inspector_shown(App *app)
{
    (void)app;
    return shown();
}

static void
log_release(void)
{
    SDL_LogOutputFunction now;
    void *now_user;

    if (!g_log_lock) return;
    SDL_GetLogOutputFunction(&now, &now_user);
    if (now == log_take) SDL_SetLogOutputFunction(g_log_next, g_log_next_user);
    SDL_DestroyMutex(g_log_lock);
    g_log_lock = NULL;
}

void
reaktor_inspector_early(void)
{
    if (g_log_lock || !(g_log_lock = SDL_CreateMutex())) return;
    SDL_GetLogOutputFunction(&g_log_next, &g_log_next_user);
    SDL_SetLogOutputFunction(log_take, NULL);
}

void
reaktor_inspector_start(App *app, const reaktor_inspector_spec *spec, int flag)
{
    g_spec     = *spec;
    g_attached = flag >= 0 && spec->attach;
    if (!g_attached) {
        log_release();
        return;
    }
    g_main = app;
    SDL_memset(g_open, 1, sizeof g_open);
    if (g_spec.open || flag > 0) reaktor_inspector_open(app);
}

void
reaktor_inspector_stop(App *app)
{
    (void)app;
    if (g_attached) reaktor_inspector_close(g_main);
    log_release();
    g_main     = NULL;
    g_attached = 0;
}

static int
chord(const SDL_Event *e)
{
    if (g_spec.key == (SDL_Keycode)-1) return 0;
    if (g_spec.key || g_spec.mods) return reaktor_chord(e, g_spec.mods, g_spec.key);
#ifdef __APPLE__
    return reaktor_chord(e, REAKTOR_MOD_CMD | REAKTOR_MOD_ALT, SDLK_I);
#else
    return reaktor_chord(e, REAKTOR_MOD_CTRL | REAKTOR_MOD_SHIFT, SDLK_I);
#endif
}

static unsigned
widget_at(float x, float y)
{
    const reaktor_a11y_node *t;
    unsigned id = 0;
    int i, n, depth = -1;

    t = reaktor_a11y_tree(&g_main->a11y, &n);
    for (i = 0; i < n; i++) {
        const struct nk_rect *r = &t[i].bounds;

        if (t[i].level >= depth && r->w > 0.0f && r->h > 0.0f &&
            x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h) {
            id    = t[i].id;
            depth = t[i].level;
        }
    }
    return id;
}

int
reaktor_inspector_event(App *app, const SDL_Event *e)
{
    if (!g_attached || app != g_main) return 0;
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && chord(e)) {
        if (shown()) reaktor_inspector_close(app);
        else reaktor_inspector_open(app);
        return 1;
    }
    if (g_picking && e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        g_sel     = widget_at(e->button.x, e->button.y);
        g_picking = 0;
        g_tab     = TAB_BOX;
        redraw();
        return 1;
    }
    if (g_picking && e->type == SDL_EVENT_MOUSE_BUTTON_UP) return 1;
    return 0;
}

void
reaktor_inspector_frame(App *app)
{
    if (app != g_main) return;
    g_frames++;
    g_why    = app->why;
    app->why = 0;
    if (!shown()) return;
    /* Only the Frame tab changes with every frame; the rest follow the tree. */
    {
        static unsigned last;
        const reaktor_a11y_node *n;
        unsigned h = REAKTOR_FNV;
        int i, count;

        n = reaktor_a11y_tree(&app->a11y, &count);
        for (i = 0; i < count; i++) {
            unsigned v[6];
            int k;

            v[0] = n[i].id;
            v[1] = n[i].state;
            v[2] = (unsigned)(int)n[i].bounds.x;
            v[3] = (unsigned)(int)n[i].bounds.y;
            v[4] = (unsigned)(int)n[i].bounds.w;
            v[5] = (unsigned)(int)n[i].bounds.h;
            h = reaktor_fnv_n(h, v, sizeof v);
            h = reaktor_fnv(h, n[i].value);
        }
        h ^= reaktor_style_loads();
        if (h == last && g_tab != TAB_FRAME) return;
        last = h;
    }
    redraw_window_only();
}

int
reaktor_inspector_marks(App *app)
{
    return app == g_main && shown() && g_sel != 0;
}

void
reaktor_inspector_mark(App *app, struct nk_context *ctx, struct nk_command_buffer *o)
{
    const reaktor_a11y_node *t;
    unsigned char c[4] = { 0xd0, 0x3a, 0xe0, 0xff };
    int i, n;

    (void)ctx;
    if (!reaktor_inspector_marks(app)) return;
    reaktor_style_token("--accent", c);
    t = reaktor_a11y_tree(&app->a11y, &n);
    for (i = 0; i < n; i++) {
        if (t[i].id != g_sel) continue;
        nk_fill_rect(o, t[i].bounds, 0.0f, nk_rgba(c[0], c[1], c[2], 48));
        nk_stroke_rect(o, t[i].bounds, 0.0f, 2.0f, nk_rgba(c[0], c[1], c[2], 255));
        break;
    }
}

const char *
reaktor_inspector_sheet(void)
{
    return g_css_on ? ".reaktor-inspector.css" : NULL;
}
