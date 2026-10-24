#include "showcase.h"

static void
ease_plot(App *app, struct reaktor_context *ctx, struct reaktor_rect slot,
          unsigned char curve, float head, int strong)
{
    struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
    struct reaktor_color line = strong ? reaktor_accent_color(app)
                                  : reaktor_token("--text-muted", reaktor_rgb(0, 112, 224));
    struct reaktor_color grid = reaktor_token("--background-hover",
                                              reaktor_rgba(128, 128, 128, 90));
    const float over = 0.28f;
    const float span = 1.0f + 2.0f * over;
    const float pad  = 4.0f;
    float w = slot.w - 2.0f * pad, h = slot.h - 2.0f * pad;
    float px = 0.0f, py = 0.0f;
    int   i;

    {
        float y0 = slot.y + pad + h * (over + 1.0f) / span;
        float y1 = slot.y + pad + h * over / span;
        reaktor_stroke_line(cv, slot.x + pad, y0, slot.x + pad + w, y0, 1.0f,
                            grid);
        reaktor_stroke_line(cv, slot.x + pad, y1, slot.x + pad + w, y1, 1.0f,
                            grid);
    }

    for (i = 0; i <= 24; i++) {
        float t = (float)i / 24.0f;
        float x = slot.x + pad + t * w;
        float y = slot.y + pad
                + h * (over + 1.0f - reaktor_ease_at(curve, t)) / span;

        if (i) reaktor_stroke_line(cv, px, py, x, y, strong ? 1.8f : 1.2f,
                                   line);
        px = x;
        py = y;
    }

    if (head >= 0.0f && head <= 1.0f) {
        float x = slot.x + pad + head * w;
        float y = slot.y + pad
                + h * (over + 1.0f - reaktor_ease_at(curve, head)) / span;
        reaktor_fill_round(app, cv, reaktor_rect(x - 3.5f, y - 3.5f, 7.0f,
                                                 7.0f),
                           3.5f,
                           reaktor_token("--text-bright",
                                         reaktor_rgb(255, 255, 255)));
    }
}

static void
track_at(App *app, struct reaktor_context *ctx, struct reaktor_rect r,
         float where,
         int lit)
{
    struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
    const float box = 40.0f, inset = 5.0f;

    reaktor_fill_round(app, cv, r, 6.0f,
                       reaktor_token("--background-alt",
                                     reaktor_rgb(48, 48, 48)));
    reaktor_fill_round(app, cv,
                       reaktor_rect(r.x + inset
                                    + where * (r.w - box - 2.0f * inset),
                                    r.y + inset, box, r.h - 2.0f * inset),
                       5.0f,
                       lit ? reaktor_accent_color(app)
                           : reaktor_token("--text-muted",
                                           reaktor_rgb(140, 140, 140)));
}

static struct reaktor_color
well_color(void)
{
    return reaktor_token("--background-alt", reaktor_rgb(48, 48, 48));
}

static struct reaktor_color
well_ink(float strength)
{
    struct reaktor_colorf well = reaktor_color_cf(well_color());

    return reaktor_rgb_cf(reaktor_anim_color_blend(well, reaktor_color_cf(reaktor_on(well_color())),
                                                   strength, REAKTOR_ANIM_COL_OKLAB));
}

static void
well_caption(struct reaktor_context *ctx, struct reaktor_rect r,
             const char *text)
{
    const struct reaktor_user_font *f = ctx->style.font;
    int len = (int)strlen(text);
    float w = f->width(f->userdata, f->height, text, len);

    reaktor_draw_text(reaktor_window_get_canvas(ctx),
                      reaktor_rect(r.x + 10.0f, r.y + r.h - f->height - 8.0f, w + 2.0f, f->height + 2.0f),
                      text, len, f, reaktor_rgba(0, 0, 0, 0), well_ink(0.6f));
}

static float
tile_value(struct reaktor_anim_context *a, reaktor_hash tile, reaktor_hash ch,
           float rest)
{
    float v;

    return reaktor_anim_instance_get_float(a, tile, ch, &v) ? v : rest;
}

static void
clip_demo(App *app, struct reaktor_context *ctx)
{
    const float cell = 34.0f;
    struct reaktor_anim_context *a = reaktor_anim(app);
    reaktor_hash clip = reaktor_anim_id("showcase.tiles");
    reaktor_hash size = reaktor_anim_id("size"), fade = reaktor_anim_id("fade");
    reaktor_hash lift = reaktor_anim_id("lift"), glow = reaktor_anim_id("glow");
    int i, replay = 0;

    section(app, ctx, "One clip, every tile",
            "A clip is keyframes written once and played anywhere. Here the "
            "same four tracks - size, opacity, a lift and a glow - run on "
            "every tile in the grid, each tile starting a little after its "
            "neighbor nearer the middle, so the grid opens from the center "
            "out. The delay comes from a tile's place in the grid, not from "
            "code that knows about the others. Under it is NukAnim's own "
            "debug timeline, following the outlined tile: its tracks, its "
            "keys, which one is running and where the playhead is.");
    api(app, ctx, "reaktor_anim_play_with_delay(anim, clip, tile, reaktor_anim_stagger_grid_delay_index(anim, i, &grid))"
                  "  /  reaktor_anim_show_debug_timeline(anim, ctx, tile)");

    if (!reaktor_anim_clip_exists(a, clip)) {
        reaktor_anim_clip_begin(a, clip);
        reaktor_anim_clip_key_float(a, size, 0.0f, 0.4f,
                                    REAKTOR_ANIM_EASE_OUT_BACK, NULL);
        reaktor_anim_clip_key_float(a, size, 0.5f, 1.0f,
                                    REAKTOR_ANIM_EASE_LINEAR, NULL);
        reaktor_anim_clip_key_float(a, fade, 0.0f, 0.0f,
                                    REAKTOR_ANIM_EASE_OUT_CUBIC, NULL);
        reaktor_anim_clip_key_float(a, fade, 0.3f, 1.0f,
                                    REAKTOR_ANIM_EASE_LINEAR, NULL);
        reaktor_anim_clip_key_vec2(a, lift, 0.0f, reaktor_vec2(0.0f, 12.0f), REAKTOR_ANIM_EASE_OUT_CUBIC, NULL);
        reaktor_anim_clip_key_vec2(a, lift, 0.45f, reaktor_vec2(0.0f, 0.0f), REAKTOR_ANIM_EASE_LINEAR, NULL);
        reaktor_anim_clip_key_float(a, glow, 0.0f, 1.0f,
                                    REAKTOR_ANIM_EASE_IN_OUT_SINE, NULL);
        reaktor_anim_clip_key_float(a, glow, 0.9f, 0.0f,
                                    REAKTOR_ANIM_EASE_LINEAR, NULL);
        reaktor_anim_clip_end(a);
    }

    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_PACK_CENTER) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Replay", .name = "Replay the tiles",
                .box = { .w = 96.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            replay = 1;
    }

    REAKTOR_COLUMN(.gap = 6.0f) REAKTOR_ROW(.h = 4.0f * cell, .flags = REAKTOR_LAY_FILL_X) {
        struct reaktor_rect r;

        if (reaktor_box_rect(&r)) {
            struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
            struct reaktor_anim_stagger_grid_opts grid = reaktor_anim_stagger_grid_opts_default();
            struct reaktor_colorf rest = reaktor_color_cf(well_color());
            struct reaktor_colorf lit = reaktor_color_cf(reaktor_accent_color(app));
            int cols = (int)(r.w / cell), n;
            float x0;

            if (cols < 1) cols = 1;
            n = cols * 4;
            x0 = r.x + (r.w - (float)cols * cell) * 0.5f;
            grid.cols = cols;
            grid.rows = 4;
            grid.from = REAKTOR_ANIM_STAGGER_CENTER;
            grid.delay = 0.6f / (float)(n - 1);

            for (i = 0; i < n; i++) {
                reaktor_hash tile = reaktor_anim_id_mix(clip, (reaktor_hash)i + 1u);
                float cx = x0 + ((float)(i % cols) + 0.5f) * cell;
                float cy = r.y + ((float)(i / cols) + 0.5f) * cell;
                float side = cell - 6.0f, k, alpha;
                struct reaktor_vec2 up = reaktor_vec2(0.0f, 0.0f);
                struct reaktor_color c;

                if (replay)
                    reaktor_anim_play_with_delay(a, clip, tile, reaktor_anim_stagger_grid_delay_index(a, i, &grid));
                else if (!reaktor_anim_instance_valid(a, tile) && reaktor_anim_play(a, clip, tile))
                    reaktor_anim_instance_seek(a, tile,
                                               reaktor_anim_clip_duration(a, clip));

                k = side * tile_value(a, tile, size, 1.0f);
                alpha = tile_value(a, tile, fade, 1.0f);
                reaktor_anim_instance_get_vec2(a, tile, lift, &up);
                c = reaktor_rgb_cf(reaktor_anim_color_blend(rest, lit, tile_value(a, tile, glow, 0.0f), REAKTOR_ANIM_COL_OKLAB));
                c.a = (reaktor_byte)(255.0f * (alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha) + 0.5f);

                if (i == 0)
                    reaktor_edge_round(app, cv, reaktor_rect(cx - side * 0.5f - 3.0f, cy - side * 0.5f - 3.0f,
                                                             side + 6.0f, side + 6.0f),
                                       8.0f, 2.0f, well_ink(0.7f));
                if (c.a && k > 0.0f)
                    reaktor_fill_round(app, cv, reaktor_rect(cx - k * 0.5f, cy - k * 0.5f + up.y, k, k),
                                       k * 0.2f, c);
            }
        }
    }

    reaktor_anim_show_debug_timeline(a, ctx, reaktor_anim_id_mix(clip, 1u));
    {
        const struct reaktor_panel *l = ctx->current->layout;

        reaktor_hot_follow(app, reaktor_rect(l->bounds.x, l->at_y - (float)*l->offset_y,
                                             l->bounds.w, l->row.height), 0);
    }
}

static void
build_paths(struct reaktor_anim_context *a, reaktor_hash curve, reaktor_hash wave, float w, float h, float wave_h)
{
    const float m = 24.0f;
    int i, n = (int)(w / 160.0f);
    float y = wave_h * 0.66f, amp = wave_h * 0.2f, step;

    n = n < 2 ? 2 : n > 4 ? 4 : n;
    step = w / (float)n;
    reaktor_anim_path_begin(a, curve, reaktor_vec2(m, h - m));
    reaktor_anim_path_cubic_to(a, reaktor_vec2(w * 0.2f, h - m), reaktor_vec2(w * 0.24f, m), reaktor_vec2(w * 0.4f, m));
    reaktor_anim_path_line_to(a, reaktor_vec2(w * 0.55f, m));
    reaktor_anim_path_cubic_to(a, reaktor_vec2(w * 0.75f, m), reaktor_vec2(w * 0.62f, h - m), reaktor_vec2(w * 0.84f, h - m));
    reaktor_anim_path_quadratic_to(a, reaktor_vec2(w - m, h - m),
                                   reaktor_vec2(w - m, m));
    reaktor_anim_path_end(a);
    reaktor_anim_path_build_arc_lut(a, curve, 0);

    reaktor_anim_path_begin(a, wave, reaktor_vec2(0.0f, y));
    for (i = 0; i < n; i++) {
        float x = step * (float)i, dy = i & 1 ? amp : -amp;

        reaktor_anim_path_cubic_to(a, reaktor_vec2(x + step * 0.36f, y + dy), reaktor_vec2(x + step * 0.64f, y + dy),
                                   reaktor_vec2(x + step, y));
    }
    reaktor_anim_path_end(a);
    reaktor_anim_path_build_arc_lut(a, wave, 0);
}

static void
path_dots(App *app, struct reaktor_context *ctx, struct reaktor_anim_context *a, reaktor_hash path, struct reaktor_vec2 at)
{
    struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
    struct reaktor_color ink = well_ink(0.35f);
    float len = reaktor_anim_path_length(a, path), d;

    for (d = 0.0f; d <= len; d += 12.0f) {
        struct reaktor_vec2 p = reaktor_anim_path_evaluate_at_distance(a, path, d);

        reaktor_fill_round(app, cv, reaktor_rect(at.x + p.x - 1.5f, at.y + p.y - 1.5f, 3.0f, 3.0f), 1.5f, ink);
    }
}

static void
path_demo(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static float built_w, built_h;
    static const char words[] = "Text can follow any path you draw";
    const float wave_h = 96.0f;
    struct reaktor_anim_context *a = reaktor_anim(app);
    reaktor_hash curve = reaktor_anim_id("showcase.curve"), wave = reaktor_anim_id("showcase.wave");
    reaktor_hash run = (reaktor_hash)s->path_runs;
    struct reaktor_anim_ease linear = reaktor_anim_ease(REAKTOR_ANIM_EASE_LINEAR);
    struct reaktor_rect r;

    section(app, ctx, "Along a path",
            "A path is lines and curves joined end to end. Ridden by plain t, "
            "a dot slows wherever a curve bunches up; NukAnim measures the "
            "path once and rides it by distance, so the dot keeps one speed "
            "through bends and straights alike, and the path's direction "
            "turns it to face where it is going. The dots it passes are "
            "spaced the same way, every twelve pixels along the line. Text "
            "can travel a path too, a glyph at a time, each one standing on "
            "the line.");
    api(app, ctx, "reaktor_anim_tween_path(anim, id, run, path, 2.4f, ease, 0)  /  reaktor_anim_tween_path_angle  /  "
                  "reaktor_anim_text_path_animated(anim, ctx, path, text, t, "
                  "&opts)");

    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_PACK_CENTER) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Send", .name = "Send the dot and the text",
                .box = { .w = 96.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->path_runs++;
    }

    REAKTOR_COLUMN(.gap = 6.0f) {
        REAKTOR_ROW(.h = 140.0f, .flags = REAKTOR_LAY_FILL_X) {
            if (reaktor_box_rect(&r)) {
                struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
                unsigned id = reaktor_box_id();
                struct reaktor_vec2 p, tip, at = reaktor_vec2(r.x, r.y);
                float angle;

                if (r.w != built_w || r.h != built_h || !reaktor_anim_path_exists(a, curve)) {
                    build_paths(a, curve, wave, r.w, r.h, wave_h);
                    built_w = r.w;
                    built_h = r.h;
                }
                reaktor_fill_round(app, cv, r, 6.0f, well_color());
                path_dots(app, ctx, a, curve, at);

                if (run) {
                    p = reaktor_anim_tween_path(a, id, run, curve, 2.4f, linear, REAKTOR_ANIM_POLICY_CROSSFADE);
                    angle = reaktor_anim_tween_path_angle(a, id, run, curve, 2.4f, linear, REAKTOR_ANIM_POLICY_CROSSFADE);
                } else {
                    p = reaktor_anim_path_evaluate(a, curve, 0.0f);
                    angle = reaktor_anim_path_angle(a, curve, 0.0f);
                }
                p.x += at.x;
                p.y += at.y;
                tip = reaktor_vec2(p.x + 18.0f * cosf(angle), p.y + 18.0f * sinf(angle));
                reaktor_stroke_line(cv, p.x, p.y, tip.x, tip.y, 2.0f, well_ink(1.0f));
                reaktor_fill_round(app, cv, reaktor_rect(p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f), 8.0f,
                                   reaktor_accent_color(app));
                reaktor_fill_round(app, cv, reaktor_rect(tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f), 3.0f,
                                   well_ink(1.0f));
            }
        }

        REAKTOR_ROW(.h = wave_h, .flags = REAKTOR_LAY_FILL_X) {
            unsigned id = reaktor_box_id();
            float t = run ? reaktor_anim_tween_float(a, id, run, 1.0f, 1.4f, linear, REAKTOR_ANIM_POLICY_CROSSFADE, 0.0f)
                          : 1.0f;

            if (reaktor_box_rect(&r) && reaktor_anim_path_exists(a, wave)) {
                struct reaktor_anim_text_path_opts o = reaktor_anim_text_path_opts_default();

                reaktor_fill_round(app, reaktor_window_get_canvas(ctx), r, 6.0f, well_color());
                path_dots(app, ctx, a, wave, reaktor_vec2(r.x, r.y));
                o.origin = reaktor_vec2(r.x, r.y);
                o.align = REAKTOR_ANIM_TEXT_ALIGN_CENTER;
                o.color = well_ink(1.0f);
                reaktor_anim_text_path_animated(a, ctx, wave, words, t, &o);
            }
        }
    }
}

static void
fx_demo(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static const char *const what[3] = { "oscillate", "wiggle", "noise" };
    struct reaktor_anim_context *a = reaktor_anim(app);
    reaktor_hash fx = reaktor_anim_id("showcase.fx"), shake = reaktor_anim_id("showcase.shake");
    float on;
    int i;

    section(app, ctx, "Motion nobody keyed",
            "Some motion has no keyframes: a bob, a drift, a tremor. "
            "Oscillate is a wave, wiggle wanders smoothly between random "
            "points, and noise reads Perlin noise along time - each one a "
            "function of the clock, so none of it needs a start or an end. "
            "They move only while Play is on, because a page at rest should "
            "draw nothing. A shake is the same idea with a decay: it starts "
            "when something goes wrong and dies out on its own. Sign in "
            "below - the password is wrong, and the field says so.");
    api(app, ctx, "reaktor_anim_oscillate  /  reaktor_anim_wiggle_vec2  /  reaktor_anim_noise_channel_vec2  /  "
                  "reaktor_anim_trigger_shake + reaktor_anim_shake");

    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_PACK_CENTER) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = s->fx_play ? "Pause" : "Play", .name = "Play the motion",
                .accent = (unsigned char)s->fx_play,
                .box = { .w = 96.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->fx_play = !s->fx_play;
    }

    on = reaktor_anim_tween_float(a, fx, 0, s->fx_play ? 1.0f : 0.0f, 0.4f, reaktor_anim_ease(REAKTOR_ANIM_EASE_OUT_CUBIC),
                                  REAKTOR_ANIM_POLICY_CROSSFADE, 0.0f);
    if (on <= 0.0f) s->trail_n = 0;

    REAKTOR_COLUMN(.gap = 6.0f) REAKTOR_ROW(.h = 120.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
        for (i = 0; i < 3; i++)
            REAKTOR_ROW(.name = what[i], .flags = REAKTOR_LAY_FILL) {
                struct reaktor_rect r;

                if (reaktor_box_rect(&r)) {
                    struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
                    float ax = r.w * 0.34f, ay = (r.h - 24.0f) * 0.36f;
                    struct reaktor_vec2 c = reaktor_vec2(r.x + r.w * 0.5f, r.y + (r.h - 24.0f) * 0.5f);
                    struct reaktor_vec2 d = reaktor_vec2(0.0f, 0.0f);
                    struct reaktor_color dot = reaktor_accent_color(app);
                    int j;

                    reaktor_fill_round(app, cv, r, 6.0f, well_color());
                    well_caption(ctx, r, what[i]);
                    if (i == 0)
                        reaktor_stroke_line(cv, c.x, c.y - ay, c.x, c.y + ay, 2.0f, well_ink(0.25f));
                    if (on > 0.0f) {
                        reaktor_hash id = reaktor_anim_id_mix(fx, (reaktor_hash)i + 1u);

                        if (i == 0) {
                            d.y = reaktor_anim_oscillate(a, id, ay, 0.6f, REAKTOR_ANIM_WAVE_SINE, 0.0f);
                        } else if (i == 1) {
                            d = reaktor_anim_wiggle_vec2(a, id, reaktor_vec2(ax, ay),
                                                         1.4f);
                        } else {
                            struct reaktor_anim_noise_opts o = reaktor_anim_noise_opts_default();

                            o.octaves = 2;
                            d = reaktor_anim_noise_channel_vec2(a, id, reaktor_vec2(0.7f, 0.7f),
                                                                reaktor_vec2(ax * 1.6f, ay * 1.6f), &o);
                            d.x = d.x < -ax ? -ax : d.x > ax ? ax : d.x;
                            d.y = d.y < -ay ? -ay : d.y > ay ? ay : d.y;
                        }
                        d.x *= on;
                        d.y *= on;
                        if (i == 2 && reaktor_anim_time(a) - s->trail_at >= 0.035) {
                            s->trail_at = reaktor_anim_time(a);
                            if (s->trail_n == (int)REAKTOR_LEN(s->trail)) {
                                memmove(s->trail, s->trail + 1, sizeof(s->trail) - sizeof(s->trail[0]));
                                s->trail_n--;
                            }
                            s->trail[s->trail_n++] = d;
                        }
                    }
                    if (i == 2)
                        for (j = 0; j + 1 < s->trail_n; j++) {
                            float k = (float)(j + 1) / (float)s->trail_n, rad = 2.0f + 4.0f * k;
                            struct reaktor_color t = dot;

                            t.a = (reaktor_byte)(160.0f * k);
                            reaktor_fill_round(app, cv,
                                               reaktor_rect(c.x + s->trail[j].x - rad, c.y + s->trail[j].y - rad,
                                                            2.0f * rad, 2.0f * rad),
                                               rad, t);
                        }
                    reaktor_fill_round(app, cv, reaktor_rect(c.x + d.x - 8.0f, c.y + d.y - 8.0f, 16.0f, 16.0f),
                                       8.0f, dot);
                }
            }
    }

    REAKTOR_COLUMN(.gap = 6.0f) REAKTOR_ROW(.h = 40.0f, .gap = ctx->style.window.spacing.x,
                                            .flags = REAKTOR_LAY_FILL_X) {
        REAKTOR_ROW(.name = "Password", .flags = REAKTOR_LAY_FILL) {
            float dx = reaktor_anim_shake(a, shake, 26.0f, 22.0f, 0.6f);
            struct reaktor_rect r;

            if (reaktor_box_rect(&r)) {
                struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
                const struct reaktor_style_edit *e = &ctx->style.edit;
                struct reaktor_rect f = reaktor_rect(r.x + dx, r.y, r.w, r.h);
                int j;

                reaktor_fill_round(app, cv, f, e->rounding,
                                   e->normal.type == REAKTOR_STYLE_ITEM_COLOR ? e->normal.data.color : well_color());
                reaktor_edge_round(app, cv, f, e->rounding, 2.0f,
                                   reaktor_token("--background-hover", reaktor_rgb(69, 69, 69)));
                for (j = 0; j < 8; j++)
                    reaktor_fill_round(app, cv, reaktor_rect(f.x + 14.0f + 14.0f * (float)j, f.y + f.h * 0.5f - 3.5f,
                                                             7.0f, 7.0f),
                                       3.5f, e->text_normal);
            }
        }
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Sign in",
                .box = { .w = 110.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            reaktor_anim_trigger_shake(a, shake);
    }
}

static void
color_demo(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static const char *const name[5] = { "sRGB", "linear", "HSV", "OKLAB", "OKLCH" };
    static const int space[5] = {
        REAKTOR_ANIM_COL_SRGB, REAKTOR_ANIM_COL_SRGB_LINEAR, REAKTOR_ANIM_COL_HSV, REAKTOR_ANIM_COL_OKLAB, REAKTOR_ANIM_COL_OKLCH
    };
    struct reaktor_anim_context *a = reaktor_anim(app);
    struct reaktor_colorf from = reaktor_color_cf(reaktor_rgb(14, 165, 233)), to = reaktor_color_cf(reaktor_rgb(234, 88, 12));
    int i;

    section(app, ctx, "Five ways to mix two colors",
            "Halfway from sky blue to orange is a different color depending "
            "on what gets averaged. The numbers of sRGB meet in a muddy gray, "
            "and linear light - the way light really adds - meets brighter. "
            "HSV turns the hue and passes through green. OKLAB is built so "
            "that equal steps look equal, and OKLCH turns the hue in it the "
            "other way round, through violet: each takes the short way round "
            "its own wheel, and the two wheels disagree. Each strip is one "
            "blend drawn whole, and Swap sends the swatch beside it from one "
            "end to the other through the same space.");
    api(app, ctx,
        "reaktor_anim_color_blend(a, b, t, REAKTOR_ANIM_COL_OKLCH)  /  "
                  "reaktor_anim_tween_color(anim, id, 0, to, 1.6f, ease, "
                  "REAKTOR_ANIM_POLICY_CROSSFADE, REAKTOR_ANIM_COL_OKLCH, from)");

    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_PACK_CENTER) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Swap", .name = "Swap the colors",
                .box = { .w = 96.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->color_swap = !s->color_swap;
    }

    REAKTOR_COLUMN(.gap = 6.0f) {
        for (i = 0; i < 5; i++)
            REAKTOR_ROW(.h = 34.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
                struct reaktor_rect r;

                reaktor_label(&(reaktor_label_spec){
                    .text = name[i], .color = "--text-bright",
                    .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
                REAKTOR_ROW(.flags = REAKTOR_LAY_FILL) {
                    if (reaktor_box_rect(&r)) {
                        struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
                        int n = (int)(r.w / 4.0f), j;

                        for (j = 0; j < n; j++) {
                            float x0 = r.x + (float)(int)((float)j * r.w / (float)n);
                            float x1 = r.x + (float)(int)((float)(j + 1) * r.w / (float)n);

                            reaktor_fill_rect(cv, reaktor_rect(x0, r.y, x1 - x0, r.h), 0.0f,
                                              reaktor_rgb_cf(reaktor_anim_color_blend(from, to, ((float)j + 0.5f) / (float)n,
                                                                                      space[i])));
                        }
                    }
                }
                REAKTOR_ROW(.w = 64.0f, .flags = REAKTOR_LAY_FILL_Y) {
                    struct reaktor_colorf c = reaktor_anim_tween_color(a, reaktor_box_id(), 0, s->color_swap ? to : from,
                                                                       1.6f, reaktor_anim_ease(REAKTOR_ANIM_EASE_IN_OUT_SINE),
                                                                       REAKTOR_ANIM_POLICY_CROSSFADE, space[i], from);

                    if (reaktor_box_rect(&r))
                        reaktor_fill_round(app, reaktor_window_get_canvas(ctx), r, 6.0f, reaktor_rgb_cf(c));
                }
            }
    }
}

static void
text_demo(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static const char *const name[5] = { "fade", "slide up", "bounce", "typewriter", "scale" };
    static const int effect[5] = {
        REAKTOR_ANIM_TEXT_FX_FADE, REAKTOR_ANIM_TEXT_FX_SLIDE_UP, REAKTOR_ANIM_TEXT_FX_BOUNCE, REAKTOR_ANIM_TEXT_FX_TYPEWRITER, REAKTOR_ANIM_TEXT_FX_SCALE
    };
    static const char line[] = "Each glyph waits its turn";
    struct reaktor_anim_context *a = reaktor_anim(app);
    int i;

    section(app, ctx, "A glyph at a time",
            "Text that arrives one letter after another reads as being said "
            "rather than shown. Every line below is the same stagger - each "
            "glyph starts a fixed time after the one before it - with a "
            "different thing done to a glyph as it arrives: faded in, slid "
            "up, overshot in size, typed, or grown.");
    api(app, ctx, "reaktor_anim_text_stagger(anim, ctx, text, progress, &opts)  /  REAKTOR_ANIM_TEXT_FX_*");

    REAKTOR_ROW(.h = ROW, .flags = REAKTOR_LAY_PACK_CENTER) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Replay", .name = "Replay the text",
                .box = { .w = 96.0f, .flags = REAKTOR_LAY_FILL_Y } }))
            s->text_plays++;
    }

    REAKTOR_COLUMN(.gap = 6.0f) {
        for (i = 0; i < 5; i++)
            REAKTOR_ROW(.h = 34.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
                reaktor_label(&(reaktor_label_spec){
                    .text = name[i], .color = "--text-bright",
                    .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
                REAKTOR_ROW(.flags = REAKTOR_LAY_FILL) {
                    struct reaktor_anim_text_stagger_opts o = reaktor_anim_text_stagger_opts_default();
                    float t = 1.0f;
                    struct reaktor_rect r;

                    o.effect = effect[i];
                    o.char_delay = 0.035f;
                    o.char_duration = 0.4f;
                    o.effect_intensity = 14.0f;
                    o.color = well_ink(1.0f);
                    if (s->text_plays)
                        t = reaktor_anim_tween_float(a, reaktor_box_id(), (reaktor_hash)s->text_plays, 1.0f,
                                                     reaktor_anim_text_stagger_duration(line, &o), reaktor_anim_ease(REAKTOR_ANIM_EASE_LINEAR),
                                                     REAKTOR_ANIM_POLICY_CROSSFADE, 0.0f);
                    if (reaktor_box_rect(&r)) {
                        struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
                        struct reaktor_rect keep = cv->clip;
                        float x0 = r.x > keep.x ? r.x : keep.x, y0 = r.y > keep.y ? r.y : keep.y;
                        float x1 = r.x + r.w < keep.x + keep.w ? r.x + r.w : keep.x + keep.w;
                        float y1 = r.y + r.h < keep.y + keep.h ? r.y + r.h : keep.y + keep.h;

                        reaktor_fill_round(app, cv, r, 6.0f, well_color());
                        o.pos = reaktor_vec2(r.x + 12.0f, r.y + (r.h - ctx->style.font->height) * 0.5f);
                        reaktor_push_scissor(cv, reaktor_rect(x0, y0, x1 > x0 ? x1 - x0 : 0.0f, y1 > y0 ? y1 - y0 : 0.0f));
                        reaktor_anim_text_stagger(a, ctx, line, t, &o);
                        reaktor_push_scissor(cv, keep);
                    }
                }
            }
    }
}

void
page_animation(App *app, struct reaktor_context *ctx, showcase_state *s)
{
    static const float stop[3] = { 0.0f, 0.5f, 1.0f };
    static const char *const stop_name[3] = { "Left", "Center", "Right" };
    static const char *const kind[3] = { "spring", "steps", "bezier" };
    unsigned char curve = (unsigned char)s->anim_curve;
    float ms = (float)s->anim_ms;
    struct reaktor_anim_ease ease[3];
    int   i;

    ease[0] = reaktor_anim_ease_spring(1.0f, 170.0f, 16.0f, 0.0f);
    ease[1] = reaktor_anim_ease_steps(6, 0);
    ease[2] = reaktor_anim_ease_bezier(0.68f, -0.55f, 0.27f, 1.55f);

    section(app, ctx, "The same change, five ways",
            "Every block is told to go to the same place by the same button. "
            "The top one is simply put there, which is what a frame does "
            "without help. The rest are given the time to travel, and the "
            "difference is the whole subject of this page: the eye follows a "
            "thing that moves and has to re-find a thing that jumps. Eased "
            "runs the curve picked further down. The last three go past the "
            "classic thirty-one, with NukAnim, which runs every animation on "
            "this page: a spring that settles the way a spring does, steps "
            "that move in jumps like the hand of a clock, and whatever cubic "
            "bezier a design hands over. Press one stop and then another "
            "before they arrive - each redirects from where it is rather "
            "than starting again.");
    api(app, ctx, "reaktor_animate(id, channel, to, ms, curve)  /  "
                  "reaktor_anim_tween_float(reaktor_anim(app), id, 0, to, 0.6f, "
                  "reaktor_anim_ease_spring(1, 170, 16, 0), ...)");

    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        for (i = 0; i < 3; i++)
            if (reaktor_button(&(reaktor_button_spec){
                    .label = stop_name[i],
                    .accent = (unsigned char)(s->anim_slot == i),
                    .box = { .flags = REAKTOR_LAY_FILL } }))
                s->anim_slot = i;
    }

    REAKTOR_COLUMN(.gap = 6.0f) {
        REAKTOR_ROW(.h = 44.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
            reaktor_label(&(reaktor_label_spec){
                .text = "instant", .color = "--text-muted",
                .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL) {
                struct reaktor_rect r;
                if (reaktor_box_rect(&r))
                    track_at(app, ctx, r, stop[s->anim_slot], 0);
            }
        }

        REAKTOR_ROW(.h = 44.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
            reaktor_label(&(reaktor_label_spec){
                .text = "eased", .color = "--text-bright",
                .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
            REAKTOR_ROW(.flags = REAKTOR_LAY_FILL) {
                unsigned id = reaktor_box_id();
                float where = reaktor_animate(id, 0, stop[s->anim_slot],
                                              ms, curve);
                struct reaktor_rect r;

                if (reaktor_box_rect(&r))
                    track_at(app, ctx, r, where, 1);
            }
        }

        for (i = 0; i < 3; i++)
            REAKTOR_ROW(.h = 44.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
                reaktor_label(&(reaktor_label_spec){
                    .text = kind[i], .color = "--text-bright",
                    .box = { .w = 74.0f, .flags = REAKTOR_LAY_CENTER_Y } });
                REAKTOR_ROW(.flags = REAKTOR_LAY_FILL) {
                    unsigned id = reaktor_box_id();
                    float to = stop[s->anim_slot];
                    float where = reaktor_anim_tween_float(reaktor_anim(app), id, 0, to, 0.6f,
                                                           ease[i], REAKTOR_ANIM_POLICY_CROSSFADE, to);
                    struct reaktor_rect r;

                    if (reaktor_box_rect(&r))
                        track_at(app, ctx, r, where, 1);
                }
            }
    }

    section(app, ctx, "Pick a curve by looking at it",
            "Thirty-one of them, drawn from the same function that runs them. "
            "A curve is a shape, and a list of names is a poor way to choose "
            "one - so the shapes are the control. In accelerates away from "
            "rest, out arrives calm, in-out does both: for something "
            "appearing, out is almost always right, and a page that eases "
            "everything in-out reads as sluggish because the slow middle is "
            "where the eye is. The last three leave the 0 and 1 lines on "
            "purpose. Pick one and press Play: the dot travels the cell you "
            "chose, which is the value arriving.");
    api(app, ctx, "reaktor_ease_at(curve, t)  /  REAKTOR_EASE_*");

    REAKTOR_ROW(.gap = ctx->style.window.spacing.x,
                .flags = REAKTOR_LAY_PACK_CENTER | REAKTOR_LAY_WRAP) {
        unsigned id = reaktor_box_id();

        reaktor_animate(id, 0, s->anim_play ? 1.0f : 0.0f, ms, curve);
        s->anim_head = reaktor_anim_progress(id, 0);

        if (reaktor_button(&(reaktor_button_spec){
                .label  = "Play",
                .name   = "Run the selected curve",
                .box = { .w = 96.0f, .h = ROW } }))
            s->anim_play = !s->anim_play;

        REAKTOR_COMBO(.label = reaktor_ease_name(curve), .name = "Curve",
                      .body_h = 300.0f,
                      .box = { .w = 190.0f, .h = ROW }) {
            reaktor_layout_row_dynamic(ctx, 26.0f, 1);
            for (i = 0; i < REAKTOR_EASE_COUNT; i++)
                if (reaktor_combo_item(reaktor_ease_name((unsigned char)i),
                                       i == s->anim_curve))
                    s->anim_curve = i;
        }

        reaktor_property(&(reaktor_property_spec){
            .label = "ms:", .name = "Duration",
            .ivalue = &s->anim_ms, .lo = 60, .hi = 3000, .step = 20,
            .grain = 4.0f,
            .box = { .w = 200.0f, .h = ROW } });
    }

    REAKTOR_ROW(.gap = 6.0f, .flags = REAKTOR_LAY_WRAP) {
        for (i = 0; i < REAKTOR_EASE_COUNT; i++) {
            REAKTOR_ROW(.w = 104.0f, .h = 52.0f) {
                struct reaktor_rect r;
                int on = (i == s->anim_curve);

                if (reaktor_swatch(&(reaktor_swatch_spec){
                        .name = reaktor_ease_name((unsigned char)i),
                        .fill = reaktor_token(on ? "--background-hover"
                                                 : "--background-alt",
                                              reaktor_rgb(48, 48, 48)),
                        .box = { .flags = REAKTOR_LAY_FILL } }))
                    s->anim_curve = i;

                if (reaktor_box_rect(&r))
                    ease_plot(app, ctx, r, (unsigned char)i,
                              on ? s->anim_head : -1.0f, on);
            }
        }
    }

    section(app, ctx, "Grows when you point at it",
            "A tile that rises a little under the pointer says it can be "
            "pressed before anyone presses it. Each one keeps its size in a "
            "float of its own, and reaktor_anim_animate, which comes from "
            "ImAnimate, eases that float up while the pointer is over the tile "
            "and back when it leaves - from wherever it got to, so a quick pass "
            "never jumps. The color crossfades the same way, through OKLAB, "
            "where the middle of two colors still looks like both of them.");
    api(app, ctx, "reaktor_anim_animate(reaktor_anim(app), 1, 1.1f, 150, &size, REAKTOR_ANIM_EASE_OUT_CUBIC)");

    REAKTOR_COLUMN(.gap = 6.0f) REAKTOR_ROW(.h = 96.0f, .gap = 12.0f, .flags = REAKTOR_LAY_FILL_X) {
        static const char *const word[4] = { "Home", "Search", "Library", "Profile" };
        struct reaktor_anim_context *anim = reaktor_anim(app);
        struct reaktor_command_buffer *cv = reaktor_window_get_canvas(ctx);
        const struct reaktor_user_font *f = ctx->style.font;
        struct reaktor_colorf rest = reaktor_color_cf(reaktor_token("--background-alt", reaktor_rgb(48, 48, 48)));
        struct reaktor_colorf lit = reaktor_color_cf(reaktor_accent_color(app));

        for (i = 0; i < 4; i++)
            REAKTOR_ROW(.name = word[i], .flags = REAKTOR_LAY_FILL) {
                struct reaktor_rect r;

                if (reaktor_box_rect(&r)) {
                    int over = reaktor_input_is_mouse_hovering_rect(&ctx->input, r);
                    int len = (int)strlen(word[i]);
                    float w, h, tw;
                    struct reaktor_color bg;

                    reaktor_hot(app, r, 0, 1);
                    if (over) reaktor_anim_animate(anim, 1.0f, 1.1f, 150.0f, &s->grow[i], REAKTOR_ANIM_EASE_OUT_CUBIC);
                    else      reaktor_anim_animate(anim, 1.1f, 1.0f, 250.0f, &s->grow[i], REAKTOR_ANIM_EASE_OUT_CUBIC);
                    bg = reaktor_rgb_cf(reaktor_anim_tween_color(anim, reaktor_box_id(), 1, over ? lit : rest, 0.2f,
                                                                 reaktor_anim_ease(REAKTOR_ANIM_EASE_OUT_CUBIC), REAKTOR_ANIM_POLICY_CROSSFADE,
                                                                 REAKTOR_ANIM_COL_OKLAB, rest));
                    w = r.w * 0.86f * s->grow[i];
                    h = r.h * 0.8f * s->grow[i];
                    reaktor_fill_round(app, cv, reaktor_rect(r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h),
                                       10.0f * s->grow[i], bg);
                    tw = f->width(f->userdata, f->height, word[i], len);
                    reaktor_draw_text(cv, reaktor_rect(r.x + (r.w - tw) * 0.5f, r.y + (r.h - f->height) * 0.5f,
                                                       tw + 2.0f, f->height + 2.0f),
                                      word[i], len, f, reaktor_rgba(0, 0, 0, 0), reaktor_on(bg));
                }
            }
    }

    clip_demo(app, ctx);
    path_demo(app, ctx, s);
    fx_demo(app, ctx, s);
    color_demo(app, ctx, s);
    text_demo(app, ctx, s);

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
