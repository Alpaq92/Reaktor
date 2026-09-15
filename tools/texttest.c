#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nk_common.h"
#include "reaktor.h"
#include "text.h"

#define FONT_FILE     "assets/fonts/Aileron-Regular.otf"
#define JAPANESE_FILE "assets/fonts/MPLUS1p-Regular.ttf"

#define RLO "\xE2\x80\xAE"
#define LRO "\xE2\x80\xAD"
#define PDF "\xE2\x80\xAC"
#define SHALOM "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"
#define HI     "\xE6\x97\xA5"
#define HON    "\xE6\x9C\xAC"
#define NIHON  HI HON

struct vertex {
    float   x, y, u, v;
    nk_byte col[4];
};

static int failures;

static void
check(const char *what, long got, long want)
{
    if (got == want) return;
    printf("FAIL %s: got %ld, want %ld\n", what, got, want);
    failures++;
}

static void
check_near(const char *what, float got, float want)
{
    if (SDL_fabs(got - want) < 0.01) return;
    printf("FAIL %s: got %.3f, want %.3f\n", what, got, want);
    failures++;
}

static void
breaks(void)
{
    check("after the space", reaktor_text_break("Hello world", 11, 7), 6);
    check("not inside a number", reaktor_text_break("a 1,234", 7, 5), 2);
    check("between kanji and kana", reaktor_text_break(NIHON "\xE3\x81\xA7\xE3\x81\x99", 12, 6), 6);
    check("not before a full stop",
          reaktor_text_break("\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82", 9, 6), 3);
}

static void
directions(void)
{
    check("Latin", reaktor_text_rtl("abc", 3), 0);
    check("Hebrew first", reaktor_text_rtl(SHALOM " abc", 12), 1);
    check("Latin first", reaktor_text_rtl("abc " SHALOM, 12), 0);
    check("digits are not strong", reaktor_text_rtl("123 " SHALOM, 12), 1);
    check("Japanese", reaktor_text_rtl(NIHON, 6), 0);
}

static float
width(const struct nk_font *font, const char *s)
{
    return font->handle.width(font->handle.userdata, font->handle.height, s,
                              (int)strlen(s));
}

static int
frame(struct nk_context *ctx, SDL_Renderer *ren, const char *text,
      struct nk_buffer *verts)
{
    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct vertex, x)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct vertex, u)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct vertex, col)},
        {NK_VERTEX_LAYOUT_END}
    };
    const struct nk_draw_command *cmd;
    struct nk_convert_config      cfg;
    struct nk_buffer              cmds, idx;
    int                           quads = 0;

    if (nk_begin(ctx, "text", nk_rect(0, 0, 400, 60), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, text, NK_TEXT_LEFT);
    }
    nk_end(ctx);

    memset(&cfg, 0, sizeof cfg);
    cfg.vertex_layout        = layout;
    cfg.vertex_size          = sizeof(struct vertex);
    cfg.vertex_alignment     = NK_ALIGNOF(struct vertex);
    cfg.circle_segment_count = cfg.curve_segment_count = cfg.arc_segment_count = 22;
    cfg.global_alpha         = 1.0f;
    nk_buffer_init_default(&cmds);
    nk_buffer_init_default(&idx);
    nk_buffer_init_default(verts);
    reaktor_text_prepare(ctx, ren);
    nk_convert(ctx, &cmds, verts, &idx, &cfg);
    nk_draw_foreach(cmd, ctx, &cmds)
        if (cmd->texture.ptr) quads += (int)cmd->elem_count / 6;
    nk_buffer_free(&cmds);
    nk_buffer_free(&idx);
    nk_clear(ctx);
    return quads;
}

static int
same_vertices(struct nk_buffer *a, struct nk_buffer *b)
{
    return a->needed == b->needed &&
           !memcmp(nk_buffer_memory_const(a), nk_buffer_memory_const(b), a->needed);
}

static void
drawing(void)
{
    struct nk_font_atlas  atlas;
    struct nk_font_config cfg = nk_font_config(16.0f);
    struct nk_context     ctx;
    struct nk_buffer      rtl, ltr, wrong;
    struct nk_font       *font;
    nk_text_width_f       atlas_width;
    SDL_Surface          *surface;
    SDL_Renderer         *ren;
    char                  path[1024], japanese[1024], line[200];
    int                   w, h;

    if (!reaktor_path(path, sizeof path, FONT_FILE) ||
        !reaktor_path(japanese, sizeof japanese, JAPANESE_FILE)) {
        printf("FAIL no %s or %s\n", FONT_FILE, JAPANESE_FILE);
        failures++;
        return;
    }
    reaktor_text_add_fallback(japanese);
    surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_ARGB8888);
    ren     = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    if (!ren) {
        printf("FAIL no software renderer: %s\n", SDL_GetError());
        failures++;
        return;
    }

    cfg.oversample_h = cfg.oversample_v = 1;
    cfg.pixel_snap   = 1;
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    font = nk_font_atlas_add_from_file(&atlas, path, 16.0f, &cfg);
    if (!font || !nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_ALPHA8)) {
        printf("FAIL could not bake %s\n", path);
        failures++;
        return;
    }
    nk_font_atlas_end(&atlas, nk_handle_id(0), NULL);
    nk_init_default(&ctx, &font->handle);

    atlas_width = font->handle.width;
    reaktor_text_attach_font(font, path);

    check_near("Latin measures as before", width(font, "Ab"),
               atlas_width(font->handle.userdata, 16.0f, "Ab", 2));
    check("Japanese has width", width(font, NIHON) > 10.0f, 1);
    check_near("Japanese adds up", width(font, NIHON),
               width(font, HI) + width(font, HON));
    check_near("direction does not change width", width(font, RLO "ab" PDF),
               width(font, LRO "ba" PDF));

    check("two Japanese glyphs drawn", frame(&ctx, ren, NIHON, &rtl), 2);
    nk_buffer_free(&rtl);

    check("forced right to left drawn", frame(&ctx, ren, RLO "ab" HI PDF, &rtl), 3);
    frame(&ctx, ren, LRO HI "ba" PDF, &ltr);
    frame(&ctx, ren, LRO "ab" HI PDF, &wrong);
    check("right to left is reversed", same_vertices(&rtl, &ltr), 1);
    check("and order shows", same_vertices(&rtl, &wrong), 0);
    nk_buffer_free(&rtl);
    nk_buffer_free(&ltr);
    nk_buffer_free(&wrong);

    reaktor_text_describe(line, sizeof line);
    check("the fallback counted", strcmp(line, "M PLUS 1p for 2 glyphs"), 0);
    if (strcmp(line, "M PLUS 1p for 2 glyphs")) printf("  described: \"%s\"\n", line);

    reaktor_text_attach_font(NULL, NULL);
    nk_free(&ctx);
    nk_font_atlas_clear(&atlas);
    SDL_DestroyRenderer(ren);
    SDL_DestroySurface(surface);
}

int
main(void)
{
    breaks();
    directions();
    drawing();

    if (failures) {
        printf("\n%d check%s failed\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("text: all checks passed\n");
    return 0;
}
