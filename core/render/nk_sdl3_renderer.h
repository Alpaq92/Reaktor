/* nuklear - public domain */

/* VENDORED COPY - see docs/NOTICE.md. Changes are marked REAKTOR. */

/*
 * ==============================================================
 *
 *                              API
 *
 * ===============================================================
 */

#ifndef NK_SDL3_RENDERER_H_
#define NK_SDL3_RENDERER_H_

#if SDL_MAJOR_VERSION < 3
    #error "nk_sdl3_renderer requires at least SDL 3.0.0"
#endif
#ifndef NK_INCLUDE_COMMAND_USERDATA
    #error "nk_sdl3_renderer requires the NK_INCLUDE_COMMAND_USERDATA define"
#endif
#ifndef NK_INCLUDE_VERTEX_BUFFER_OUTPUT
    #error "nk_sdl3_renderer requires the NK_INCLUDE_VERTEX_BUFFER_OUTPUT define"
#endif

/* We have to redefine it because demos do not include any headers
 * This is the same default value as the one from "src/nuklear_internal.h" */
#ifndef NK_BUFFER_DEFAULT_INITIAL_SIZE
    #define NK_BUFFER_DEFAULT_INITIAL_SIZE (4*1024)
#endif

#ifdef __cplusplus
extern "C"{
#endif

NK_API struct nk_context*   nk_sdl_init(SDL_Window *win, SDL_Renderer *renderer, struct nk_allocator allocator);
/* REAKTOR: an atlas Reaktor bakes itself, alpha8. */
NK_API nk_handle            nk_sdl_font_upload(struct nk_context* ctx, const void *image, int width, int height);
NK_API int                  nk_sdl_handle_event(struct nk_context* ctx, SDL_Event *evt);
/* REAKTOR: fill and stroke feathering apart. */
NK_API void                 nk_sdl_render_ex(struct nk_context* ctx, enum nk_anti_aliasing shape_AA, enum nk_anti_aliasing line_AA);
NK_API void                 nk_sdl_update_TextInput(struct nk_context* ctx);
NK_API void                 nk_sdl_shutdown(struct nk_context* ctx);
NK_API struct nk_allocator  nk_sdl_allocator(void);
/* REAKTOR: exported for the Text module. */
NK_API bool                 nk_sdl_set_coverage_palette(SDL_Texture *tex);

#ifdef __cplusplus
}
#endif

#endif /* NK_SDL3_RENDERER_H_ */

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifdef NK_SDL3_RENDERER_IMPLEMENTATION
#ifndef NK_SDL3_RENDERER_IMPLEMENTATION_ONCE
#define NK_SDL3_RENDERER_IMPLEMENTATION_ONCE

struct nk_sdl_device {
    struct nk_buffer cmds;
    /* REAKTOR: kept across frames, as cmds is, so a frame allocates nothing. */
    struct nk_buffer vbuf, ebuf;
    struct nk_draw_null_texture tex_null;
    SDL_Texture *font_tex;
    /* REAKTOR: white texel for untextured geometry. */
    SDL_Texture *white_tex;
};

struct nk_sdl_vertex {
    float position[2];
    float uv[2];
    float col[4];
};

/* NK_ALIGNOF declares its struct inside offsetof, which MSVC warns about. */
struct nk_sdl_vertex_align { char c; struct nk_sdl_vertex v; };

struct nk_sdl {
    SDL_Window *win;
    SDL_Renderer *renderer;
    struct nk_sdl_device ogl;
    struct nk_context ctx;
    struct nk_allocator allocator;
    Uint64 last_render;
    bool insert_toggle;
    bool edit_was_active;
    bool is_sw;
};

NK_INTERN void *
nk_sdl_alloc(nk_handle user, void *old, nk_size size)
{
    NK_UNUSED(user);
    /* FIXME: nk_sdl_alloc should use SDL_realloc here, not SDL_malloc
     * but this could cause a double-free due to bug within Nuklear, see:
     * https://github.com/Immediate-Mode-UI/Nuklear/issues/768
     * */
#if 0
    return SDL_realloc(old, size);
#else
    NK_UNUSED(old);
    return SDL_malloc(size);
#endif
}

NK_INTERN void
nk_sdl_free(nk_handle user, void *old)
{
    NK_UNUSED(user);
    SDL_free(old);
}

NK_API struct nk_allocator
nk_sdl_allocator()
{
    struct nk_allocator allocator;
    allocator.userdata.ptr = 0;
    allocator.alloc = nk_sdl_alloc;
    allocator.free = nk_sdl_free;
    return allocator;
}

/* REAKTOR: entry i is white at alpha i. */
NK_API bool
nk_sdl_set_coverage_palette(SDL_Texture *tex)
{
    SDL_Palette *pal = SDL_CreatePalette(256);
    SDL_Color ramp[256];
    bool ok;
    int i;

    if (!pal) return false;
    for (i = 0; i < 256; i++) {
        ramp[i].r = ramp[i].g = ramp[i].b = 255;
        ramp[i].a = (Uint8)i;
    }
    SDL_SetPaletteColors(pal, ramp, 0, 256);
    ok = SDL_SetTexturePalette(tex, pal);
    SDL_DestroyPalette(pal);
    return ok;
}

NK_INTERN void
nk_sdl_device_upload_atlas(struct nk_context* ctx, const void *image, int width, int height)
{
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    NK_ASSERT(image);
    NK_ASSERT(width > 0);
    NK_ASSERT(height > 0);

    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* Clean up if the texture already exists. */
    if (sdl->ogl.font_tex != NULL) {
        SDL_DestroyTexture(sdl->ogl.font_tex);
        sdl->ogl.font_tex = NULL;
    }

    /* REAKTOR: 8-bit indexed. */
    sdl->ogl.font_tex = SDL_CreateTexture(sdl->renderer, SDL_PIXELFORMAT_INDEX8, SDL_TEXTUREACCESS_STATIC, width, height);
    if (sdl->ogl.font_tex && !nk_sdl_set_coverage_palette(sdl->ogl.font_tex)) {
        SDL_DestroyTexture(sdl->ogl.font_tex);
        sdl->ogl.font_tex = NULL;
    }

    if (sdl->ogl.font_tex) {
        SDL_UpdateTexture(sdl->ogl.font_tex, NULL, image, width);
    } else {
        size_t n = (size_t)width * (size_t)height, i;
        Uint32 *rgba = (Uint32 *)SDL_malloc(n * 4);

        SDL_Log("nk_sdl: indexed font atlas unavailable (%s); using RGBA32",
                SDL_GetError());
        if (rgba) {
            /* REAKTOR: Nuklear's converter is part of its font baking. */
            for (i = 0; i < n; i++)
                rgba[i] = (Uint32)((const Uint8 *)image)[i] << 24 | 0x00FFFFFFu;
            sdl->ogl.font_tex = SDL_CreateTexture(sdl->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
            if (sdl->ogl.font_tex)
                SDL_UpdateTexture(sdl->ogl.font_tex, NULL, rgba, 4 * width);
            SDL_free(rgba);
        }
    }

    if (!sdl->ogl.font_tex) {
        SDL_Log("nk_sdl: no font atlas texture (%s); text will not draw",
                SDL_GetError());
        return;
    }
    SDL_SetTextureBlendMode(sdl->ogl.font_tex, SDL_BLENDMODE_BLEND);
}

NK_API void
nk_sdl_update_TextInput(struct nk_context* ctx)
{
    struct nk_sdl* sdl;
    bool active;
    SDL_Window *keys;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* REAKTOR: text input is left to whichever window has the keys. */
    keys = SDL_GetKeyboardFocus();
    if (keys && keys != sdl->win) return;

    /* Determine if Nuklear is using any top-level "edit" widget.
     * Popups take higher priority because they block any incomming input.
     * This will not work, if the widget is not updating context state properly. */
    if (!ctx->active)
        active = false;
    else if (ctx->active->popup.win)
        active = ctx->active->popup.win->edit.active;
    else
        active = ctx->active->edit.active;

    /* decide, if TextInputActive should be unchanged/stoped/started
     * and change its state accordingly for owned SDL Window */
    if (active != sdl->edit_was_active)
    {
        const bool window_edit_active = SDL_TextInputActive(sdl->win);

        /* If you ever hit this check, it means that the demo and your app
         * (or something else) are all trying to manage TextInputActive state.
         * This can cause subtle bugs where the state won't be what you expect.
         * You can safely remove this assert and the demo will keep working,
         * but make sure it does not cause any issues for you */
        NK_ASSERT(window_edit_active == sdl->edit_was_active && "something else changed TextInputActive state for this Window");

        if (!window_edit_active && !sdl->edit_was_active && active)
            SDL_StartTextInput(sdl->win);
        else if (window_edit_active && sdl->edit_was_active && !active)
            SDL_StopTextInput(sdl->win);
        sdl->edit_was_active = active;
    }

    /* FIXME:
     * for full SDL3 integration, you also need to find current edit widget
     * bounds and the text cursor offset, and pass this data into SDL_SetTextInputArea.
     * This is currently not possible to do safely as Nuklear does not support it.
     * https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea
     * https://github.com/Immediate-Mode-UI/Nuklear/pull/857
     */
}

/* REAKTOR: count of the draw list's indices from ix, over its n vertices. */
NK_INTERN void
nk_sdl_geometry(SDL_Renderer *ren, SDL_Texture *tex, const void *vertices,
                int n, const nk_draw_index *ix, int count)
{
    const struct nk_sdl_vertex *v = (const struct nk_sdl_vertex *)vertices;
    int vs = (int)sizeof(*v);

    SDL_RenderGeometryRaw(ren, tex, v->position, vs, (const SDL_FColor *)v->col,
                          vs, v->uv, vs, n, ix, count, (int)sizeof(*ix));
}

/* REAKTOR: opaque triangles unblended on software. */
NK_INTERN void
nk_sdl_geometry_sw(SDL_Renderer *ren, SDL_Texture *tex, const void *vertices,
                   int n, const nk_draw_index *ix, int count)
{
    const struct nk_sdl_vertex *v = (const struct nk_sdl_vertex *)vertices;
    int k, opaque = !tex;

    for (k = 0; opaque && k < count; k++)
        if (v[ix[k]].col[3] < 1.0f) opaque = 0;
    if (opaque) SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    nk_sdl_geometry(ren, tex, vertices, n, ix, count);
    if (opaque) SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
}

/* REAKTOR: a rectangle among the triangles, on whole device pixels. */
struct nk_sdl_rect {
    SDL_FRect    d, src;
    const float *col;
    int          opaque;
};

/* REAKTOR: what a run of rectangles left set, put back before triangles. */
struct nk_sdl_sw {
    float tint[4];
    int   tinted, unblended;
};

/* REAKTOR: whether the six indices at ix are one rectangle. */
NK_INTERN int
nk_sdl_quad(const void *vertices, const nk_draw_index *ix, SDL_Texture *tex,
            float tw, float th, float sx, float sy, struct nk_sdl_rect *q)
{
    const struct nk_sdl_vertex *v[6];
    float x0, x1, y0, y1, u[2] = {0, 0}, w[2] = {0, 0};
    int k, seen = 0, tri[2] = {0, 0};
    SDL_FRect d;

    for (k = 0; k < 6; k++) {
        v[k] = (const struct nk_sdl_vertex *)vertices + ix[k];
        if (SDL_memcmp(v[k]->col, v[0]->col, sizeof(v[0]->col)) != 0)
            return 0;
    }
    x0 = x1 = v[0]->position[0];
    y0 = y1 = v[0]->position[1];
    for (k = 1; k < 6; k++) {
        if (v[k]->position[0] < x0) x0 = v[k]->position[0];
        if (v[k]->position[0] > x1) x1 = v[k]->position[0];
        if (v[k]->position[1] < y0) y0 = v[k]->position[1];
        if (v[k]->position[1] > y1) y1 = v[k]->position[1];
    }
    if (!(x1 > x0 && y1 > y0)) return 0;
    for (k = 0; k < 6; k++) {
        int cx = v[k]->position[0] == x1;
        int cy = v[k]->position[1] == y1;

        if ((v[k]->position[0] != x0 && !cx) ||
            (v[k]->position[1] != y0 && !cy))
            return 0;
        if (tex) {
            if (seen & (1 << cx) ? u[cx] != v[k]->uv[0] : 0) return 0;
            if (seen & (4 << cy) ? w[cy] != v[k]->uv[1] : 0) return 0;
            u[cx] = v[k]->uv[0];
            w[cy] = v[k]->uv[1];
            seen |= (1 << cx) | (4 << cy);
        }
        tri[k / 3] |= 1 << (cy * 2 + cx);
    }
    if ((tri[0] | tri[1]) != 0xF ||
        ((tri[0] & tri[1]) != 0x9 && (tri[0] & tri[1]) != 0x6))
        return 0;
    if (tex && !(u[1] > u[0] && w[1] > w[0])) return 0;

    /* SDL truncates toward zero, so a start left of zero is nudged left. */
    d.x = SDL_floorf(x0 * sx);
    d.y = SDL_floorf(y0 * sy);
    d.w = (SDL_floorf(x1 * sx) - d.x + 0.25f) / sx;
    d.h = (SDL_floorf(y1 * sy) - d.y + 0.25f) / sy;
    d.x = (d.x + (d.x < 0.0f ? -0.25f : 0.25f)) / sx;
    d.y = (d.y + (d.y < 0.0f ? -0.25f : 0.25f)) / sy;

    if (tex) {
        if (!(tw > 0.0f && th > 0.0f)) return 0;
        /* REAKTOR: a sub-image ends on whole texels, but u * size comes back a
         * hair under one, and SDL's software copy truncates it a texel short. */
        q->src.x = SDL_roundf(u[0] * tw);
        q->src.y = SDL_roundf(w[0] * th);
        q->src.w = SDL_roundf(u[1] * tw) - q->src.x;
        q->src.h = SDL_roundf(w[1] * th) - q->src.y;
    }
    q->d      = d;
    q->col    = v[0]->col;
    q->opaque = !tex && v[0]->col[3] >= 1.0f && x1 - x0 >= 2.0f &&
                y1 - y0 >= 2.0f;
    return 1;
}

/* REAKTOR: SDL truncates a scaled rectangle's position and size apart. */
NK_INTERN void
nk_sdl_quad_draw(SDL_Renderer *ren, SDL_Texture *tex, const struct nk_sdl_rect *q,
                 struct nk_sdl_sw *st)
{
    if (tex) {
        if (!st->tinted || SDL_memcmp(st->tint, q->col, sizeof st->tint) != 0) {
            SDL_SetTextureColorModFloat(tex, q->col[0], q->col[1], q->col[2]);
            SDL_SetTextureAlphaModFloat(tex, q->col[3]);
            SDL_memcpy(st->tint, q->col, sizeof st->tint);
            st->tinted = 1;
        }
        SDL_RenderTexture(ren, tex, &q->src, &q->d);
        return;
    }
    if (q->opaque != st->unblended) {
        SDL_SetRenderDrawBlendMode(ren, q->opaque ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
        st->unblended = q->opaque;
    }
    SDL_SetRenderDrawColorFloat(ren, q->col[0], q->col[1], q->col[2], q->col[3]);
    SDL_RenderFillRect(ren, &q->d);
}

NK_INTERN void
nk_sdl_sw_restore(SDL_Renderer *ren, SDL_Texture *tex, struct nk_sdl_sw *st)
{
    if (st->tinted) {
        SDL_SetTextureColorModFloat(tex, 1.0f, 1.0f, 1.0f);
        SDL_SetTextureAlphaModFloat(tex, 1.0f);
    }
    if (st->unblended) SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    st->tinted = st->unblended = 0;
}

/* REAKTOR: fill and stroke feathering apart. */
NK_API void
nk_sdl_render_ex(struct nk_context* ctx, enum nk_anti_aliasing shape_AA,
                 enum nk_anti_aliasing line_AA)
{
    /* setup global state */
    struct nk_sdl* sdl;
    int is_sw;
    float sx = 1.0f, sy = 1.0f;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    { /* setup internal delta time that Nuklear needs for animations */
        const Uint64 now = SDL_GetTicksNS();
        ctx->delta_time_seconds = (float)(now - sdl->last_render) / (float)SDL_NS_PER_SECOND;
        sdl->last_render = now;
    }

    {
        SDL_Rect saved_clip, last_clip = {0, 0, -1, -1};
        SDL_BlendMode saved_blend;
        float saved_r, saved_g, saved_b, saved_a;
        bool clipping_enabled;
        int vs = sizeof(struct nk_sdl_vertex);
        size_t vp = NK_OFFSETOF(struct nk_sdl_vertex, position);

        /* convert from command queue into draw list and draw to screen */
        const struct nk_draw_command *cmd;
        const nk_draw_index *offset = NULL;

        /* fill converting configuration */
        struct nk_convert_config config;
        static const struct nk_draw_vertex_layout_element vertex_layout[] = {
            {NK_VERTEX_POSITION,    NK_FORMAT_FLOAT,                NK_OFFSETOF(struct nk_sdl_vertex, position)},
            {NK_VERTEX_TEXCOORD,    NK_FORMAT_FLOAT,                NK_OFFSETOF(struct nk_sdl_vertex, uv)},
            {NK_VERTEX_COLOR,       NK_FORMAT_R32G32B32A32_FLOAT,   NK_OFFSETOF(struct nk_sdl_vertex, col)},
            {NK_VERTEX_LAYOUT_END}
        };
        NK_MEMSET(&config, 0, sizeof(config));
        config.vertex_layout = vertex_layout;
        config.vertex_size = sizeof(struct nk_sdl_vertex);
        config.vertex_alignment = offsetof(struct nk_sdl_vertex_align, v);
        config.tex_null = sdl->ogl.tex_null;
        /* REAKTOR: 22 upstream. */
        config.circle_segment_count = 48;
        config.curve_segment_count = 22;
        config.arc_segment_count = 22;
        config.global_alpha = 1.0f;
        config.shape_AA = shape_AA;
        config.line_AA = line_AA;

        /* convert shapes into vertices */
        nk_convert(&sdl->ctx, &sdl->ogl.cmds, &sdl->ogl.vbuf, &sdl->ogl.ebuf, &config);

        /* REAKTOR: software vertices on the device pixel grid. */
        is_sw = sdl->is_sw;
        if (!SDL_GetRenderScale(sdl->renderer, &sx, &sy) || sx <= 0.0f ||
            sy <= 0.0f)
            sx = sy = 1.0f;
        if (is_sw) {
            nk_byte *v = (nk_byte *)nk_buffer_memory(&sdl->ogl.vbuf);
            nk_size i, n = sdl->ogl.vbuf.needed / (nk_size)vs;
            for (i = 0; i < n; i++) {
                float *pos = (float *)(v + i * (nk_size)vs + vp);
                pos[0] = (SDL_floorf(pos[0] * sx + 0.5f) + 0.5f) / sx;
                pos[1] = (SDL_floorf(pos[1] * sy + 0.5f) + 0.5f) / sy;
            }
        }

        /* iterate over and execute each draw command */
        offset = (const nk_draw_index*)nk_buffer_memory_const(&sdl->ogl.ebuf);

        clipping_enabled = SDL_RenderClipEnabled(sdl->renderer);
        SDL_GetRenderClipRect(sdl->renderer, &saved_clip);

        /* Ensure alpha blending is enabled for geometry rendering. */
        saved_blend = SDL_BLENDMODE_INVALID;
        SDL_GetRenderDrawBlendMode(sdl->renderer, &saved_blend);
        /* REAKTOR: saved for the fill fast path. */
        SDL_GetRenderDrawColorFloat(sdl->renderer, &saved_r, &saved_g,
                                    &saved_b, &saved_a);
        SDL_SetRenderDrawBlendMode(sdl->renderer, SDL_BLENDMODE_BLEND);

        nk_draw_foreach(cmd, &sdl->ctx, &sdl->ogl.cmds)
        {
            if (!cmd->elem_count) continue;

            /* REAKTOR: glyph quads on the pixel grid. */
            if (!is_sw && cmd->texture.ptr == (void *)sdl->ogl.font_tex) {
                nk_byte *v = (nk_byte *)nk_buffer_memory(&sdl->ogl.vbuf);
                nk_uint k;
                for (k = 0; k < cmd->elem_count; k++) {
                    float *pos = (float *)(v + (nk_size)offset[k] * (nk_size)vs
                                           + vp);
                    pos[0] = SDL_floorf(pos[0] + 0.5f);
                    pos[1] = SDL_floorf(pos[1] + 0.5f);
                }
            }

            {
                SDL_Rect r;
                r.x = cmd->clip_rect.x;
                r.y = cmd->clip_rect.y;
                r.w = cmd->clip_rect.w;
                r.h = cmd->clip_rect.h;
                if (SDL_memcmp(&r, &last_clip, sizeof r) != 0) {
                    SDL_SetRenderClipRect(sdl->renderer, &r);
                    last_clip = r;
                }
            }

            {
                const void *vertices = nk_buffer_memory_const(&sdl->ogl.vbuf);
                int n = (int)(sdl->ogl.vbuf.needed / (nk_size)vs);

                /* REAKTOR: shapes untextured. */
                SDL_Texture *tex = (SDL_Texture *)cmd->texture.ptr;

                if (tex == sdl->ogl.white_tex) tex = NULL;

                /* REAKTOR: software rectangles from device pixels. */
                if (is_sw) {
                    struct nk_sdl_sw st;
                    nk_uint k = 0, from = 0;
                    float tw = 0.0f, th = 0.0f;

                    SDL_zero(st);
                    if (tex && !SDL_GetTextureSize(tex, &tw, &th)) tw = th = 0.0f;
                    while (k + 6 <= cmd->elem_count) {
                        struct nk_sdl_rect q;

                        if (!nk_sdl_quad(vertices, offset + k, tex, tw, th, sx, sy, &q)) {
                            k += 3;
                            continue;
                        }
                        /* What came before it in the list is drawn before it. */
                        if (k > from) {
                            nk_sdl_sw_restore(sdl->renderer, tex, &st);
                            nk_sdl_geometry_sw(sdl->renderer, tex, vertices, n,
                                               offset + from, (int)(k - from));
                        }
                        nk_sdl_quad_draw(sdl->renderer, tex, &q, &st);
                        k += 6;
                        from = k;
                    }
                    nk_sdl_sw_restore(sdl->renderer, tex, &st);
                    if (cmd->elem_count > from)
                        nk_sdl_geometry_sw(sdl->renderer, tex, vertices, n,
                                           offset + from,
                                           (int)(cmd->elem_count - from));
                    offset += cmd->elem_count;
                    continue;
                }

                /* REAKTOR: nk_sdl_geometry, not SDL_RenderGeometryRaw. */
                nk_sdl_geometry(sdl->renderer, tex, vertices, n, offset,
                                (int)cmd->elem_count);

                offset += cmd->elem_count;
            }
        }

        /* Restore the original blend mode. */
        SDL_SetRenderDrawColorFloat(sdl->renderer, saved_r, saved_g,
                                    saved_b, saved_a);
        if (saved_blend != SDL_BLENDMODE_INVALID) {
            SDL_SetRenderDrawBlendMode(sdl->renderer, saved_blend);
        }

        SDL_SetRenderClipRect(sdl->renderer, &saved_clip);
        if (!clipping_enabled) {
            SDL_SetRenderClipRect(sdl->renderer, NULL);
        }

        nk_clear(&sdl->ctx);
        nk_buffer_clear(&sdl->ogl.cmds);
        nk_buffer_clear(&sdl->ogl.vbuf);
        nk_buffer_clear(&sdl->ogl.ebuf);
    }
}

NK_INTERN void
nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
{
    char *text;
    int len;
    NK_UNUSED(usr);

    /* this function returns empty string on failure, not NULL */
    text = SDL_GetClipboardText();
    NK_ASSERT(text);

    if (text[0] != '\0') {
        /* FIXME: there is a bug in Nuklear that affects UTF8 clipboard handling
         * "len" should be a buffer length, but due to bug it must be a glyph count
         * see: https://github.com/Immediate-Mode-UI/Nuklear/pull/841 */
#if 0
        len = nk_strlen(text);
#else
        len = SDL_utf8strlen(text);
#endif
        nk_textedit_paste(edit, text, len);
    }
    SDL_free(text);
}

NK_INTERN void
nk_sdl_clipboard_copy(nk_handle usr, const char *text, int len)
{
    const char *ptext;
    char *str;
    size_t buflen;
    int i;
    struct nk_sdl* sdl = (struct nk_sdl*)usr.ptr;
    NK_ASSERT(sdl);
    if (len <= 0 || text == NULL) return;

    /* FIXME: there is a bug in Nuklear that affects UTF8 clipboard handling
     * "len" is expected to be a buffer length, but due to bug it actually is a glyph count
     * see: https://github.com/Immediate-Mode-UI/Nuklear/pull/841 */
#if 0
    buflen = len + 1;
    NK_UNUSED(ptext);
#else
    ptext = text;
    for (i = len; i > 0; i--)
        (void)SDL_StepUTF8(&ptext, NULL);
    buflen = (size_t)(ptext - text) + 1;
#endif

    str = (char*)sdl->allocator.alloc(sdl->allocator.userdata, 0, buflen);
    if (!str) return;
    SDL_strlcpy(str, text, buflen);
    SDL_SetClipboardText(str);
    sdl->allocator.free(sdl->allocator.userdata, str);
}

NK_API struct nk_context*
nk_sdl_init(SDL_Window *win, SDL_Renderer *renderer, struct nk_allocator allocator)
{
    struct nk_sdl* sdl;
    NK_ASSERT(win);
    NK_ASSERT(renderer);
    NK_ASSERT(allocator.alloc);
    NK_ASSERT(allocator.free);
    sdl = (struct nk_sdl*)allocator.alloc(allocator.userdata, 0, sizeof(*sdl));
    NK_ASSERT(sdl);
    SDL_zerop(sdl);
    sdl->allocator.userdata = allocator.userdata;
    sdl->allocator.alloc = allocator.alloc;
    sdl->allocator.free = allocator.free;
    sdl->win = win;
    sdl->renderer = renderer;
    sdl->is_sw = SDL_strcmp(SDL_GetRendererName(renderer), "software") == 0;
    /* REAKTOR: UVs stay in [0, 1], so SDL need not scan them for wrapping. */
    SDL_SetRenderTextureAddressMode(renderer, SDL_TEXTURE_ADDRESS_CLAMP,
                                    SDL_TEXTURE_ADDRESS_CLAMP);
    nk_init(&sdl->ctx, &sdl->allocator, 0);
    sdl->ctx.userdata = nk_handle_ptr((void*)sdl);
    sdl->ctx.clip.copy = nk_sdl_clipboard_copy;
    sdl->ctx.clip.paste = nk_sdl_clipboard_paste;
    sdl->ctx.clip.userdata = nk_handle_ptr((void*)sdl);
    nk_buffer_init(&sdl->ogl.cmds, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&sdl->ogl.vbuf, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&sdl->ogl.ebuf, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    sdl->edit_was_active = false;
    sdl->insert_toggle = false;
    sdl->last_render = SDL_GetTicksNS();
    return &sdl->ctx;
}


/* REAKTOR */
NK_API nk_handle
nk_sdl_font_upload(struct nk_context* ctx, const void *image, int width, int height)
{
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);
    nk_sdl_device_upload_atlas(&sdl->ctx, image, width, height);
    /* REAKTOR: see white_tex. */
    if (!sdl->ogl.white_tex) {
        Uint32 px = 0xFFFFFFFFu;
        sdl->ogl.white_tex = SDL_CreateTexture(sdl->renderer,
            SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
        if (sdl->ogl.white_tex) {
            SDL_UpdateTexture(sdl->ogl.white_tex, NULL, &px, 4);
            SDL_SetTextureBlendMode(sdl->ogl.white_tex, SDL_BLENDMODE_BLEND);
        }
    }
    /* REAKTOR: every bake points tex_null at white_tex again. */
    if (sdl->ogl.white_tex) {
        sdl->ogl.tex_null.texture = nk_handle_ptr(sdl->ogl.white_tex);
        sdl->ogl.tex_null.uv = nk_vec2(0.5f, 0.5f);
    }
    return nk_handle_ptr(sdl->ogl.font_tex);
}

/* REAKTOR: Nuklear counts an up for a key that is not down as a press. */
NK_INTERN void
nk_sdl_key_change(struct nk_context *ctx, enum nk_keys key, int down)
{
    if (down || ctx->input.keyboard.keys[key].down) nk_input_key(ctx, key, down);
}

NK_API int
nk_sdl_handle_event(struct nk_context* ctx, SDL_Event *evt)
{
    struct nk_sdl* sdl;

    NK_ASSERT(ctx);
    NK_ASSERT(evt);

    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* We only care about Window currently used by Nuklear */
    if (sdl->win != SDL_GetWindowFromEvent(evt)) {
        return 0;
    }

    switch(evt->type)
    {
        case SDL_EVENT_KEY_UP: /* KEYUP & KEYDOWN share same routine */
        case SDL_EVENT_KEY_DOWN:
            {
                int down = evt->type == SDL_EVENT_KEY_DOWN;
                int ctrl_down = evt->key.mod & SDL_KMOD_CTRL;

                switch(evt->key.key)
                {
                    case SDLK_LALT:
                    case SDLK_RALT:      nk_input_key(ctx, NK_KEY_ALT, down); break;
                    case SDLK_RSHIFT: /* RSHIFT & LSHIFT share same routine */
                    case SDLK_LSHIFT:    nk_input_key(ctx, NK_KEY_SHIFT, down); break;
                    case SDLK_DELETE:    nk_input_key(ctx, NK_KEY_DEL, down); break;
                    case SDLK_KP_ENTER:
                    case SDLK_RETURN:    nk_input_key(ctx, NK_KEY_ENTER, down); break;
                    case SDLK_TAB:       nk_input_key(ctx, NK_KEY_TAB, down); break;
                    case SDLK_BACKSPACE: nk_input_key(ctx, NK_KEY_BACKSPACE, down); break;
                    case SDLK_HOME:      nk_sdl_key_change(ctx, NK_KEY_TEXT_START, down);
                                         nk_sdl_key_change(ctx, NK_KEY_SCROLL_START, down); break;
                    case SDLK_END:       nk_sdl_key_change(ctx, NK_KEY_TEXT_END, down);
                                         nk_sdl_key_change(ctx, NK_KEY_SCROLL_END, down); break;
                    case SDLK_PAGEDOWN:  nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down); break;
                    case SDLK_PAGEUP:    nk_input_key(ctx, NK_KEY_SCROLL_UP, down); break;
                    case SDLK_F1:        nk_input_key(ctx, NK_KEY_F1, down); break;
                    case SDLK_F2:        nk_input_key(ctx, NK_KEY_F2, down); break;
                    case SDLK_F3:        nk_input_key(ctx, NK_KEY_F3, down); break;
                    case SDLK_F4:        nk_input_key(ctx, NK_KEY_F4, down); break;
                    case SDLK_F5:        nk_input_key(ctx, NK_KEY_F5, down); break;
                    case SDLK_F6:        nk_input_key(ctx, NK_KEY_F6, down); break;
                    case SDLK_F7:        nk_input_key(ctx, NK_KEY_F7, down); break;
                    case SDLK_F8:        nk_input_key(ctx, NK_KEY_F8, down); break;
                    case SDLK_F9:        nk_input_key(ctx, NK_KEY_F9, down); break;
                    case SDLK_F10:       nk_input_key(ctx, NK_KEY_F10, down); break;
                    case SDLK_F11:       nk_input_key(ctx, NK_KEY_F11, down); break;
                    case SDLK_F12:       nk_input_key(ctx, NK_KEY_F12, down); break;
                    case SDLK_A:         nk_sdl_key_change(ctx, NK_KEY_TEXT_SELECT_ALL, down && ctrl_down); break;
                    case SDLK_Z:         nk_sdl_key_change(ctx, NK_KEY_TEXT_UNDO, down && ctrl_down); break;
                    case SDLK_R:         nk_sdl_key_change(ctx, NK_KEY_TEXT_REDO, down && ctrl_down); break;
                    case SDLK_C:         nk_sdl_key_change(ctx, NK_KEY_COPY, down && ctrl_down); break;
                    case SDLK_V:         nk_sdl_key_change(ctx, NK_KEY_PASTE, down && ctrl_down); break;
                    case SDLK_X:         nk_sdl_key_change(ctx, NK_KEY_CUT, down && ctrl_down); break;
                    case SDLK_B:         nk_sdl_key_change(ctx, NK_KEY_TEXT_LINE_START, down && ctrl_down); break;
                    case SDLK_E:         nk_sdl_key_change(ctx, NK_KEY_TEXT_LINE_END, down && ctrl_down); break;
                    case SDLK_UP:        nk_input_key(ctx, NK_KEY_UP, down); break;
                    case SDLK_DOWN:      nk_input_key(ctx, NK_KEY_DOWN, down); break;
                    case SDLK_ESCAPE:    nk_input_key(ctx, NK_KEY_TEXT_RESET_MODE, down); break;
                    case SDLK_INSERT:
                        if (down) sdl->insert_toggle = !sdl->insert_toggle;
                        if (sdl->insert_toggle) {
                            nk_input_key(ctx, NK_KEY_TEXT_INSERT_MODE, down);
                        } else {
                            nk_input_key(ctx, NK_KEY_TEXT_REPLACE_MODE, down);
                        }
                        break;
                    case SDLK_LEFT:
                        if (ctrl_down)
                            nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
                        else
                            nk_input_key(ctx, NK_KEY_LEFT, down);
                        break;
                    case SDLK_RIGHT:
                        if (ctrl_down)
                            nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
                        else
                            nk_input_key(ctx, NK_KEY_RIGHT, down);
                        break;
                    default:
                        return 0;
                }
                return 1;
            }

        case SDL_EVENT_MOUSE_BUTTON_UP: /* MOUSEBUTTONUP & MOUSEBUTTONDOWN share same routine */
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                const int x = evt->button.x, y = evt->button.y;
                const int down = evt->button.down;
                switch(evt->button.button)
                {
                    case SDL_BUTTON_LEFT:
                        if (evt->button.clicks > 1)
                            nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
                        nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
                        break;
                    case SDL_BUTTON_MIDDLE: nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down); break;
                    case SDL_BUTTON_RIGHT:  nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down); break;
                    case SDL_BUTTON_X1:     nk_input_button(ctx, NK_BUTTON_X1, x, y, down); break;
                    case SDL_BUTTON_X2:     nk_input_button(ctx, NK_BUTTON_X2, x, y, down); break;
                    default:
                        return 0;
                }
            }
            return 1;

        case SDL_EVENT_MOUSE_MOTION:
            ctx->input.mouse.pos.x = evt->motion.x;
            ctx->input.mouse.pos.y = evt->motion.y;
            ctx->input.mouse.delta.x = ctx->input.mouse.pos.x - ctx->input.mouse.prev.x;
            ctx->input.mouse.delta.y = ctx->input.mouse.pos.y - ctx->input.mouse.prev.y;
            return 1;

        /* REAKTOR: text of any length, rune by rune; upstream asserts one glyph. */
        case SDL_EVENT_TEXT_INPUT:
            {
                const char *text = evt->text.text;
                int len = (int)SDL_strlen(text), n;
                nk_rune rune;

                while (len > 0 && (n = nk_utf_decode(text, &rune, len)) > 0) {
                    nk_input_unicode(ctx, rune);
                    text += n;
                    len -= n;
                }
            }
            return 1;

        case SDL_EVENT_MOUSE_WHEEL:
            nk_input_scroll(ctx, nk_vec2(evt->wheel.x, evt->wheel.y));
            return 1;
    }
    return 0;
}

NK_API
void nk_sdl_shutdown(struct nk_context* ctx)
{
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    nk_buffer_free(&sdl->ogl.cmds);
    nk_buffer_free(&sdl->ogl.vbuf);
    nk_buffer_free(&sdl->ogl.ebuf);

    if (sdl->ogl.font_tex != NULL) {
        SDL_DestroyTexture(sdl->ogl.font_tex);
        sdl->ogl.font_tex = NULL;
    }
    if (sdl->ogl.white_tex != NULL) {
        SDL_DestroyTexture(sdl->ogl.white_tex);
        sdl->ogl.white_tex = NULL;
    }

    nk_free(ctx);
    sdl->allocator.free(sdl->allocator.userdata, sdl);
}

#endif /* NK_SDL3_RENDERER_IMPLEMENTATION_ONCE */
#endif /* NK_SDL3_RENDERER_IMPLEMENTATION */

