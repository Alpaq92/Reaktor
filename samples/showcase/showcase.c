#include "showcase.h"

const char *const reaktor_tab_names[TAB_COUNT] = {
    "Login", "Buttons", "Inputs", "Display", "Layout", "Popups", "Animation",
    "Styling", "Translations", "Diagnostics"
};

void
head_and_note(App *app, struct reaktor_context *ctx, const char *title,
              const char *note)
{
    (void)app;
    REAKTOR_COLUMN(.name = title, .gap = reaktor_item_spacing(ctx).y) {
        reaktor_label(&(reaktor_label_spec){
            .text = title, .style = "h4",
            .color = "--text-bright",
            .box = { .flags = REAKTOR_LAY_FILL_X } });
        if (note)
            reaktor_label(&(reaktor_label_spec){
                .text = note, .style = ".section-note",
                .color = "--text-muted", .wrap = 1,
                .box = { .flags = REAKTOR_LAY_FILL_X } });
    }
    reaktor_layout_row_dynamic(ctx, 6.0f, 1);
    reaktor_spacer(ctx);
}

void
rule(struct reaktor_context *ctx)
{
    reaktor_layout_row_dynamic(ctx, 14.0f, 1);
    reaktor_spacer(ctx);
    reaktor_layout_row_dynamic(ctx, 1.0f, 1);
    reaktor_rule_horizontal(ctx, reaktor_token("--background-hover",
                                               reaktor_rgba(128, 128, 128, 90)),
                            reaktor_false);
    reaktor_layout_row_dynamic(ctx, 6.0f, 1);
    reaktor_spacer(ctx);
}

void
compact_push(struct reaktor_context *ctx)
{
    reaktor_style_push_vec2(ctx, &ctx->style.button.padding,
                            reaktor_vec2(0.0f, 0.0f));
}

void
compact_pop(struct reaktor_context *ctx)
{
    reaktor_style_pop_vec2(ctx);
}

void
api(App *app, struct reaktor_context *ctx, const char *text)
{
    reaktor_style_push_font(ctx, reaktor_style_font(app, ".api-line", 12, 0));
    reaktor_layout_row_dynamic(ctx, 18.0f, 1);
    reaktor_note_here(app, ctx, REAKTOR_A11Y_LABEL, text, 0);
    reaktor_label_colored(ctx, text, REAKTOR_TEXT_LEFT,
                          reaktor_accent_color(app));
    reaktor_style_pop_font(ctx);
    reaktor_layout_row_dynamic(ctx, 4.0f, 1);
    reaktor_spacer(ctx);
}

void
section(App *app, struct reaktor_context *ctx, const char *title,
        const char *note)
{
    rule(ctx);
    head_and_note(app, ctx, title, note);
}

void
popup_style_push(struct reaktor_context *ctx)
{
    reaktor_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                  reaktor_style_item_color(
                                 reaktor_token("--background",
                                               reaktor_rgb(53, 53, 53))));
    reaktor_style_push_float(ctx, &ctx->style.window.rounding,
                             reaktor_popup_rounding());
    reaktor_style_push_vec2(ctx, &ctx->style.window.popup_padding,
                            reaktor_vec2(16.0f, 12.0f));
}

void
popup_style_pop(struct reaktor_context *ctx)
{
    reaktor_style_pop_vec2(ctx);
    reaktor_style_pop_float(ctx);
    reaktor_style_pop_style_item(ctx);
}

void
draw_tooltip(App *app, struct reaktor_context *ctx, const char *const *lines,
             int n, float bar)
{
    const struct reaktor_user_font *f = reaktor_style_font(app, ".tooltip", 13, 0);
    struct reaktor_command_buffer *canvas = reaktor_window_get_canvas(ctx);
    struct reaktor_color fill = reaktor_token("--background-hover",
                                              reaktor_rgb(69, 69, 69));
    struct reaktor_color ink  = reaktor_token("--text-main",
                                              reaktor_rgb(247, 247, 247));
    struct reaktor_color edge = reaktor_token("--text-muted",
                                              reaktor_rgb(192, 192, 192));
    struct reaktor_color acc  = reaktor_accent_color(app);
    float pad = 10.0f, line = f->height + 6.0f;
    float w = 0.0f, h = 2.0f * pad + (float)n * line;
    struct reaktor_rect r;
    int i;

    for (i = 0; i < n; i++) {
        float lw = f->width(f->userdata, f->height, lines[i],
                            (int)strlen(lines[i]));
        if (lw > w) w = lw;
    }
    w += 2.0f * pad;
    if (bar >= 0.0f) h += 10.0f;

    r = reaktor_rect(ctx->input.mouse.pos.x + 14.0f,
                     ctx->input.mouse.pos.y + 18.0f, w, h);
    {
        struct reaktor_rect c = canvas->clip;

        if (r.x + r.w > c.x + c.w) r.x = ctx->input.mouse.pos.x - 4.0f - w;
        if (r.y + r.h > c.y + c.h) r.y = ctx->input.mouse.pos.y - 4.0f - h;
        if (r.x < c.x) r.x = c.x;
        if (r.y < c.y) r.y = c.y;
    }

    reaktor_fill_round(app, canvas, r, 6.0f, fill);
    reaktor_edge_round(app, canvas, r, 6.0f, 1.0f,
                       reaktor_rgba(edge.r, edge.g, edge.b, 140));

    for (i = 0; i < n; i++) {
        struct reaktor_rect t = reaktor_rect(r.x + pad,
                                             r.y + pad + (float)i * line,
                                             r.w - 2.0f * pad, line);
        reaktor_draw_text(canvas, t, lines[i], (int)strlen(lines[i]), f,
                          reaktor_rgba(0, 0, 0, 0), ink);
    }
    if (bar >= 0.0f) {
        struct reaktor_rect track = reaktor_rect(r.x + pad,
                                                 r.y + h - pad - 6.0f,
                                                 r.w - 2.0f * pad, 6.0f);
        reaktor_fill_round(app, canvas, track, 3.0f, reaktor_page_color(app));
        track.w *= bar;
        reaktor_fill_round(app, canvas, track, 3.0f, acc);
    }
}

int
item_with_icon(App *app, struct reaktor_context *ctx, const char *label,
               const char *ionicon, int contextual)
{
    struct reaktor_rect b = reaktor_widget_bounds(ctx);
    float px = 16.0f;
    struct reaktor_rect r;
    struct reaktor_image im;
    int hit;

    reaktor_hot_top(app, b, 1, 1);
    reaktor_note(app, REAKTOR_A11Y_MENUITEM, label, NULL, 0, b);
    hit = contextual ? reaktor_contextual_item_label(ctx, label, REAKTOR_TEXT_LEFT)
                     : reaktor_menu_item_label(ctx, label, REAKTOR_TEXT_LEFT);

    r = reaktor_rect(b.x + b.w - px - 8.0f, b.y + (b.h - px) * 0.5f, px, px);
    im = reaktor_ionicon(app, ionicon, (int)px);
    reaktor_draw_image(reaktor_window_get_canvas(ctx), r, &im,
                       reaktor_rgb(255, 255, 255));
    return hit;
}

unsigned
hot(App *app, struct reaktor_context *ctx, unsigned char role, const char *name,
    unsigned state)
{
    struct reaktor_rect b = reaktor_widget_bounds(ctx);

    reaktor_hot(app, b, REAKTOR_CURSOR_POINTER, 1);
    if (role == REAKTOR_A11Y_NONE) return 0;
    return reaktor_note(app, role, name, NULL, state, b);
}

static void
seed(showcase_state *s)
{
    int i;

    s->anim_ms    = 320;
    s->anim_curve = REAKTOR_EASE_CUBIC_OUT;
    s->anim_slot  = 0;
    for (i = 0; i < 4; i++) s->grow[i] = 1.0f;
    s->tree_open[0] = s->tree_open[1] = 1;
    s->tree_sel     = 2;
    s->time_h = s->time12_h = -1;

    SDL_strlcpy(s->name, "Ada Lovelace", sizeof(s->name));
    s->name_len = (int)strlen(s->name);
    SDL_strlcpy(s->digits, "1815", sizeof(s->digits));
    s->digits_len = (int)strlen(s->digits);
    SDL_strlcpy(s->hex, "56c7ff", sizeof(s->hex));
    s->hex_len = (int)strlen(s->hex);
    SDL_strlcpy(s->note,
                "A multiline editor: REAKTOR_EDIT_BOX.\n"
                "Tab moves focus on, as it does\n"
                "everywhere; the scrollbars appear\n"
                "when they are needed.\n"
                "\n"
                "Cut, copy and paste work because the\n"
                "shell pairs reaktor_input_begin with\n"
                "reaktor_input_end - without that Nuklear\n"
                "computes no key edges at all and\n"
                "Ctrl+V is silently dead.", sizeof(s->note));
    s->note_len = (int)strlen(s->note);

    s->slider_f = 0.65f;
    s->slider_i = 40;
    s->knob     = 0.3f;
    s->prop_i   = 12;
    s->prop_f   = 1.75f;
    s->prop_d   = 6.25;
    s->progress = 62;
    s->flags    = 1u | 4u;
    s->radio    = 1;
    s->check_wrap  = reaktor_true;
    s->sel_tile[1] = reaktor_true;
    s->list_sel = -1;
    s->tint.r = 0.34f; s->tint.g = 0.78f; s->tint.b = 1.0f; s->tint.a = 1.0f;
    SDL_strlcpy(s->menu_pick, "nothing yet", sizeof(s->menu_pick));
    SDL_strlcpy(s->file_pick, "nothing yet", sizeof(s->file_pick));

    for (i = 0; i < SC_SERIES_N; i++) {
        float t = (float)i / (float)(SC_SERIES_N - 1);
        s->series[i] = sinf(t * 6.2831853f) * 0.7f + sinf(t * 18.849556f) * 0.3f;
    }
    s->seeded = 1;
}

static showcase_state g_show;

showcase_state *
sample_state(void)
{
    return &g_show;
}

void
sample_file_opened(App *app, const char *path)
{
    (void)app;
    SDL_strlcpy(g_show.file_pick, path ? path : "no file",
                sizeof(g_show.file_pick));
    g_show.handed++;
    SDL_Log("file opened: %s", g_show.file_pick);
}

void
reaktor_showcase_page(App *app, struct reaktor_context *ctx, int tab,
                      float w, float h)
{
    showcase_state *s = sample_state();

    (void)w; (void)h;
    if (!s->seeded) seed(s);

    reaktor_style_push_vec2(ctx, &ctx->style.window.spacing,
                            reaktor_vec2(8.0f, 9.0f));

    switch (tab) {
    case TAB_BUTTONS: page_buttons(app, ctx, s); break;
    case TAB_INPUTS:  page_inputs(app, ctx, s);  break;
    case TAB_DISPLAY: page_display(app, ctx, s); break;
    case TAB_LAYOUT:  page_layout(app, ctx, s);  break;
    case TAB_POPUPS:  page_popups(app, ctx, s);  break;
    case TAB_ANIM:    page_animation(app, ctx, s); break;
    case TAB_STYLING: page_styling(app, ctx, s); break;
    case TAB_TRANSLATIONS: page_translations(app, ctx, s); break;
    case TAB_DIAG:    page_diagnostics(app, ctx, s); break;
    default: break;
    }

    reaktor_style_pop_vec2(ctx);
}

