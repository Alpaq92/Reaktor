#include "showcase.h"

static const char *const rss_names[REAKTOR_RSS_STEPS] = {
    "before any of it", "SDL_Init(VIDEO)", "window and renderer",
    "window icon", "Nuklear context", "font atlas", "stylesheets"
};

static void
diag_row(App *app, struct reaktor_context *ctx, const char *name,
         const char *value)
{
    REAKTOR_ROW(.h = 24.0f, .gap = ctx->style.window.spacing.x,
                .flags = REAKTOR_LAY_FILL_X) {
        struct reaktor_rect r;
        unsigned       id;

        id = reaktor_note(app, REAKTOR_A11Y_LABEL, name, value,
                          REAKTOR_A11Y_VOLATILE, reaktor_rect(0, 0, 0, 0));
        if (reaktor_box_rect(&r)) reaktor_note_bounds(app, id, r);

        reaktor_style_push_font(ctx, reaktor_style_font(app, ".small", 13, 0));
        reaktor_label(&(reaktor_label_spec){
            .text   = name,
            .name   = name,
            .color = "--text-muted",
            .silent = 1,
            .box    = { .w = 190.0f, .flags = REAKTOR_LAY_FILL_Y } });
        reaktor_style_pop_font(ctx);

        reaktor_label(&(reaktor_label_spec){
            .text   = value,
            .name   = "value",
            .silent = 1,
            .box    = { .flags = REAKTOR_LAY_FILL_X |
                                 REAKTOR_LAY_FILL_Y } });
    }
}

#define DIAG_ROWS(ctx)                                                       \
    REAKTOR_COLUMN(.w = reaktor_widget_bounds(ctx).w,                        \
                   .gap = (ctx)->style.window.spacing.y)

void
page_diagnostics(App *app, struct reaktor_context *ctx, showcase_state *st)
{
    reaktor_diag d;
    char v[192];

    (void)st;
    reaktor_diagnostics(app, &d);

    section(app, ctx, "Inspector",
            "The widget tree, a widget's box and style, the last frame and "
            "the log, in a window of its own. Ctrl+Shift+I opens it from any "
            "page; Pick, then a click here, finds the widget under the "
            "pointer.");
    api(app, ctx, ".inspector = { .attach = 1 }  /  reaktor_inspector_open");
    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        int shown = reaktor_inspector_shown(app);

        if (reaktor_button(&(reaktor_button_spec){
                .label = shown ? "Close the Inspector" : "Open the Inspector",
                .name  = "Inspector", .keys = "Ctrl+Shift+I",
                .box   = { .w = 240.0f } })) {
            if (shown) reaktor_inspector_close(app);
            else reaktor_inspector_open(app);
        }
        reaktor_soak();
    }

    section(app, ctx, "One copy at a time",
            "The showcase is launched with .single_instance, so starting it again "
            "does not open a second window: the new copy hands its file arguments "
            "to this one, which gets each through .file_opened and comes to the "
            "front, and the new copy exits. --new-instance starts a separate copy "
            "anyway.");
    api(app, ctx, ".single_instance  /  .file_opened  /  --new-instance");
#ifndef __EMSCRIPTEN__
    REAKTOR_ROW(.h = ROW, .gap = ctx->style.window.spacing.x) {
        if (reaktor_button(&(reaktor_button_spec){
                .label = "Launch the showcase again, with a file",
                .name  = "Launch again", .box = { .w = 340.0f } })) {
            char        exe[1024];
            const char *base = SDL_GetBasePath();
            const char *args[3];
            SDL_Process *p;

            SDL_snprintf(exe, sizeof exe, "%sshowcase%s", base ? base : "",
#ifdef _WIN32
                         ".exe"
#else
                         ""
#endif
            );
            args[0] = exe;
            args[1] = exe;
            args[2] = NULL;
            p = SDL_CreateProcess(args, false);
            if (p) SDL_DestroyProcess(p);
            else SDL_Log("could not launch %s: %s", exe, SDL_GetError());
        }
        reaktor_soak();
    }
#endif
    SDL_snprintf(v, sizeof v, "%d, the last %s", st->handed,
                 st->handed ? st->file_pick : "none yet");
    REAKTOR_ROW(.h = ROW_SMALL) {
        reaktor_label(&(reaktor_label_spec){ .text = "Files handed over", .name = "Files handed over",
                                             .box = { .w = 180.0f } });
        reaktor_label(&(reaktor_label_spec){ .text = v, .name = "Handed over",
                                             .box = { .flags = REAKTOR_LAY_FILL_X } });
    }

    section(app, ctx, "Rendering",
            "Which backend SDL settled on. It is the software rasterizer "
            "on every platform: this draws nothing at rest, so a GPU buys it "
            "nothing it can measure, and one rasterizer everywhere is one set "
            "of pixels to reason about. Anti-aliasing reads what the frame is "
            "drawn with - the software rasterizer has no partial coverage, so "
            "it feathers strokes, which grades a curve, and not fills, which "
            "would only draw a hairline.");

    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "backend", d.renderer);
        SDL_snprintf(v, sizeof(v), "%s", d.mode);
        diag_row(app, ctx, "requested", v);
        diag_row(app, ctx, "vsync", d.vsync ? "on" : "off");
        diag_row(app, ctx, "anti-aliasing", d.aa);
    }

    section(app, ctx, "Pacing",
            "How often SDL calls the app back. At rest that is \"waitevent\" - "
            "nothing is drawn until something happens - and for the duration "
            "of a drag it becomes the display's refresh, so the frames come "
            "at an even cadence rather than one per mouse event. This page "
            "forces no frames of its own, so the readings below hold still "
            "while nothing is happening - which is the measurement. They come "
            "from the gap between the last two frames rather than from a "
            "count over a second, because a second only advances when a frame "
            "is drawn: counted that way the figure would read stale for as "
            "long as the app was quiet. Move the pointer across the page.");

    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "at rest", d.frame_rate);
        diag_row(app, ctx, "while dragging", d.drag_rate);
        if (d.frame_gap_ms >= 1000.0f)
            SDL_snprintf(v, sizeof(v), "%.2f s since the last one",
                         (double)d.frame_gap_ms / 1000.0);
        else if (d.frame_gap_ms > 0.0f)
            SDL_snprintf(v, sizeof(v), "%.0f ms since the last one",
                         (double)d.frame_gap_ms);
        else SDL_strlcpy(v, "the first", sizeof(v));
        diag_row(app, ctx, "this frame", v);
        if (d.cpu_ms_per_frame > 0.0f)
            SDL_snprintf(v, sizeof(v), "%.1f ms of CPU, all threads",
                         (double)d.cpu_ms_per_frame);
        else SDL_strlcpy(v, "not yet measured", sizeof(v));
        diag_row(app, ctx, "a drawn frame costs", v);
        SDL_snprintf(v, sizeof(v), "one per %d ms while the pointer moves",
                     d.hover_gap_ms);
        diag_row(app, ctx, "hover redraws", v);
    }

    section(app, ctx, "Where a frame goes",
            "Split three ways because guessing which one dominates has been "
            "wrong more than once. Building the UI and converting it to "
            "geometry are both fractions of a millisecond; the wait on "
            "present is the whole frame.");

    SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.build_ms);
    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "build", v);
        SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.render_ms);
        diag_row(app, ctx, "render", v);
        SDL_snprintf(v, sizeof(v), "%.2f ms", (double)d.present_ms);
        diag_row(app, ctx, "present", v);
    }

    section(app, ctx, "Style and text",
            "The stylesheets are parsed by LCUI's libcss on every theme "
            "change, and the font atlas is baked per size at the display's "
            "scale. The system's reduced motion and contrast reach the sheets as "
            "@media queries, and with --watch-css a sheet saved on disk restyles "
            "the window.");
    api(app, ctx, "reaktor_reduced_motion  /  reaktor_high_contrast  /  "
                  "--motion, --contrast, --watch-css");

    SDL_snprintf(v, sizeof(v), "%s", d.dark ? "dark" : "light");
    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "scheme", v);
        SDL_snprintf(v, sizeof(v), "%d, parsed in %.2f ms", d.sheets,
                     (double)d.style_ms);
        diag_row(app, ctx, "stylesheets", v);
        SDL_snprintf(v, sizeof(v), "%.2fx", (double)d.scale);
        diag_row(app, ctx, "display scale", v);
        diag_row(app, ctx, "font", d.font);
        diag_row(app, ctx, "reduced motion",
                 reaktor_reduced_motion() ? "on: animations jump to their end" : "off");
        diag_row(app, ctx, "contrast",
                 reaktor_high_contrast() ? "more: @media (prefers-contrast: more)" : "no preference");
    }

    section(app, ctx, "Memory",
            "Where the process's memory has gone. Nuklear's command buffer "
            "grows to fit the busiest frame it has been asked to draw and is "
            "never handed back, so it records the high-water mark rather than "
            "the current page; the icon cache holds every SVG rasterized so "
            "far, at twice the size it is drawn. The font atlas holds every "
            "baked size in one texture, normally 8-bit indexed - a glyph is "
            "coverage, so a byte a pixel and a palette say what four bytes "
            "did; the row below reports what this run got. Private is what "
            "this process has committed, resident is what is in memory now "
            "including every mapped DLL and driver. Only the first is ours - "
            "and with no GPU, as here, textures are in it too.");

    SDL_snprintf(v, sizeof(v), "%.0f KB reserved, %.0f KB used by this frame",
                 d.nk_bytes / 1024.0, d.nk_used / 1024.0);
    DIAG_ROWS(ctx) {
        diag_row(app, ctx, "Nuklear buffer", v);
        SDL_snprintf(v, sizeof(v), "%.0f KB in %d rasters", d.icon_bytes / 1024.0,
                     d.icons);
        diag_row(app, ctx, "icon cache", v);
        SDL_snprintf(v, sizeof(v), "%d x %d %s, %.0f KB", d.atlas_w, d.atlas_h,
                     !d.atlas_bpp ? "none" : d.atlas_bpp == 1 ? "indexed"
                                                              : "RGBA32",
                     d.atlas_w * (double)d.atlas_h * d.atlas_bpp / 1024.0);
        diag_row(app, ctx, "font atlas", v);
        SDL_snprintf(v, sizeof(v), "%.1f MB", d.private_bytes / 1048576.0);
        diag_row(app, ctx, "process, private", v);
        SDL_snprintf(v, sizeof(v), "%.1f MB", d.rss_bytes / 1048576.0);
        diag_row(app, ctx, "process, resident", v);
    }

    section(app, ctx, "What startup costs",
            "Both counters at each step, and the difference each one made. "
            "Private is what the step cost this process; resident is what it "
            "mapped in, the graphics driver's shared pages included. The gap "
            "between the two columns is the point: a step can map in ten "
            "megabytes and commit almost none of it. Anything before the "
            "first line is what the C runtime and the loader wanted before "
            "main ran.");

    {
        int i;

        SDL_snprintf(v, sizeof(v), "%.1f MB private, %.1f MB resident",
                     d.priv_at[REAKTOR_RSS_ENTRY] / 1048576.0,
                     d.rss_at[REAKTOR_RSS_ENTRY] / 1048576.0);
        DIAG_ROWS(ctx) {
            diag_row(app, ctx, rss_names[REAKTOR_RSS_ENTRY], v);
            for (i = REAKTOR_RSS_ENTRY + 1; i < REAKTOR_RSS_STEPS; i++) {
                double dp = (d.priv_at[i] - (double)d.priv_at[i - 1]) / 1048576.0;
                double dr = (d.rss_at[i] - (double)d.rss_at[i - 1]) / 1048576.0;
                SDL_snprintf(v, sizeof(v), "%+.1f MB private   %+.1f MB resident",
                             dp, dr);
                diag_row(app, ctx, rss_names[i], v);
            }
        }
    }

    reaktor_layout_row_dynamic(ctx, 8.0f, 1);
    reaktor_spacer(ctx);
}
