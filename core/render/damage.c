#include "internal.h"

#define LOOKAHEAD 32
#define SERIAL    "reaktor.serial"

typedef struct sink {
    reaktor_damage_list *l;
    unsigned             h;
    int                  ok;
    const void          *tex;
    Sint64               serial;
} sink;

void
reaktor_texture_serial(SDL_Texture *t)
{
    static Sint64 next;

    if (t) SDL_SetNumberProperty(SDL_GetTextureProperties(t), SERIAL, ++next);
}

static Sint64
serial_of(sink *k, const void *t)
{
    if (!t) return 0;
    if (t != k->tex) {
        k->tex    = t;
        k->serial = SDL_GetNumberProperty(SDL_GetTextureProperties((SDL_Texture *)t),
                                          SERIAL, 0);
    }
    return k->serial;
}

static void
put(sink *k, const void *p, size_t n)
{
    reaktor_damage_list *l = k->l;

    if (!l) {
        k->h = reaktor_fnv_n(k->h, p, n);
        return;
    }
    if (!k->ok || !reaktor_grow((void **)&l->key, &l->size, l->used + (int)n, 1)) {
        k->ok = 0;
        return;
    }
    SDL_memcpy(l->key + l->used, p, n);
    l->used += (int)n;
}

typedef struct span {
    float x0, y0, x1, y1;
    int   any;
} span;

static void
span_at(span *b, float x, float y)
{
    if (!b->any || x < b->x0) b->x0 = x;
    if (!b->any || y < b->y0) b->y0 = y;
    if (!b->any || x > b->x1) b->x1 = x;
    if (!b->any || y > b->y1) b->y1 = y;
    b->any = 1;
}

static void
span_rect(span *b, float x, float y, float w, float h)
{
    span_at(b, x, y);
    span_at(b, x + w, y + h);
}

static void
span_points(span *b, const struct nk_vec2i *p, int n)
{
    int i;

    for (i = 0; i < n; i++) span_at(b, (float)p[i].x, (float)p[i].y);
}

/* A command by its fields, one at a time: the structs have gaps, and a gap
   keeps whatever the buffer or a copied struct held before. */
#define K(f) put(k, &(f), sizeof(f))

static void
command_read(const struct nk_command *c, struct nk_rect clip, float ink, sink *k,
             span *b, float *thick)
{
    Sint64 id;

    *thick = 0.0f;
    K(c->type);
    switch (c->type) {
    case NK_COMMAND_SCISSOR: {
        const struct nk_command_scissor *x = (const void *)c;
        K(x->x); K(x->y); K(x->w); K(x->h);
        break;
    }
    case NK_COMMAND_LINE: {
        const struct nk_command_line *x = (const void *)c;
        K(x->line_thickness); K(x->begin); K(x->end); K(x->color);
        span_points(b, &x->begin, 1);
        span_points(b, &x->end, 1);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_CURVE: {
        const struct nk_command_curve *x = (const void *)c;
        K(x->line_thickness); K(x->begin); K(x->end); K(x->ctrl); K(x->color);
        span_points(b, &x->begin, 1);
        span_points(b, &x->end, 1);
        span_points(b, x->ctrl, 2);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_RECT: {
        const struct nk_command_rect *x = (const void *)c;
        K(x->rounding); K(x->line_thickness); K(x->x); K(x->y); K(x->w); K(x->h); K(x->color);
        span_rect(b, x->x, x->y, x->w, x->h);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_RECT_FILLED: {
        const struct nk_command_rect_filled *x = (const void *)c;
        K(x->rounding); K(x->x); K(x->y); K(x->w); K(x->h); K(x->color);
        span_rect(b, x->x, x->y, x->w, x->h);
        break;
    }
    case NK_COMMAND_RECT_MULTI_COLOR: {
        const struct nk_command_rect_multi_color *x = (const void *)c;
        K(x->x); K(x->y); K(x->w); K(x->h); K(x->left); K(x->top); K(x->bottom); K(x->right);
        span_rect(b, x->x, x->y, x->w, x->h);
        break;
    }
    case NK_COMMAND_TRIANGLE: {
        const struct nk_command_triangle *x = (const void *)c;
        K(x->line_thickness); K(x->a); K(x->b); K(x->c); K(x->color);
        span_points(b, &x->a, 1);
        span_points(b, &x->b, 1);
        span_points(b, &x->c, 1);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_TRIANGLE_FILLED: {
        const struct nk_command_triangle_filled *x = (const void *)c;
        K(x->a); K(x->b); K(x->c); K(x->color);
        span_points(b, &x->a, 1);
        span_points(b, &x->b, 1);
        span_points(b, &x->c, 1);
        break;
    }
    case NK_COMMAND_CIRCLE: {
        const struct nk_command_circle *x = (const void *)c;
        K(x->x); K(x->y); K(x->line_thickness); K(x->w); K(x->h); K(x->color);
        span_rect(b, x->x, x->y, x->w, x->h);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_CIRCLE_FILLED: {
        const struct nk_command_circle_filled *x = (const void *)c;
        K(x->x); K(x->y); K(x->w); K(x->h); K(x->color);
        span_rect(b, x->x, x->y, x->w, x->h);
        break;
    }
    case NK_COMMAND_ARC: {
        const struct nk_command_arc *x = (const void *)c;
        K(x->cx); K(x->cy); K(x->r); K(x->line_thickness); K(x->a); K(x->color);
        span_rect(b, (float)x->cx - x->r, (float)x->cy - x->r, 2.0f * x->r, 2.0f * x->r);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_ARC_FILLED: {
        const struct nk_command_arc_filled *x = (const void *)c;
        K(x->cx); K(x->cy); K(x->r); K(x->a); K(x->color);
        span_rect(b, (float)x->cx - x->r, (float)x->cy - x->r, 2.0f * x->r, 2.0f * x->r);
        break;
    }
    case NK_COMMAND_POLYGON:
    case NK_COMMAND_POLYLINE: {
        const struct nk_command_polygon *x = (const void *)c;
        K(x->color); K(x->line_thickness); K(x->point_count);
        put(k, x->points, (size_t)x->point_count * sizeof x->points[0]);
        span_points(b, x->points, x->point_count);
        *thick = x->line_thickness;
        break;
    }
    case NK_COMMAND_POLYGON_FILLED: {
        const struct nk_command_polygon_filled *x = (const void *)c;
        K(x->color); K(x->point_count);
        put(k, x->points, (size_t)x->point_count * sizeof x->points[0]);
        span_points(b, x->points, x->point_count);
        break;
    }
    case NK_COMMAND_IMAGE: {
        const struct nk_command_image *x = (const void *)c;
        K(x->x); K(x->y); K(x->w); K(x->h);
        id = serial_of(k, x->img.handle.ptr);
        K(x->img.handle.ptr); K(id); K(x->img.w); K(x->img.h); K(x->img.region); K(x->col);
        span_rect(b, x->x, x->y, x->w, x->h);
        break;
    }
    case NK_COMMAND_CUSTOM: {
        const struct nk_command_custom *x = (const void *)c;
        K(x->x); K(x->y); K(x->w); K(x->h); K(x->callback_data.ptr); K(x->callback);
        span_rect(b, clip.x, clip.y, clip.w, clip.h);
        break;
    }
    case NK_COMMAND_TEXT: {
        const struct nk_command_text *x = (const void *)c;
        float g = SDL_max((float)x->h, ink * x->height);

        id = serial_of(k, x->font ? x->font->texture.ptr : NULL);
        K(x->font); K(id); K(x->background); K(x->foreground);
        K(x->x); K(x->y); K(x->w); K(x->h); K(x->height); K(x->length);
        put(k, x->string, (size_t)x->length);
        span_rect(b, x->x - g, x->y - g, x->w + 2.0f * g, x->h + 2.0f * g);
        break;
    }
    default:
        break;
    }
}
#undef K

static int
image_scaled(const struct nk_command_image *x, float s)
{
    float sw, sh;

    if (x->img.w || x->img.h) {
        sw = (float)x->img.region[2];
        sh = (float)x->img.region[3];
    } else if (!x->img.handle.ptr ||
               !SDL_GetTextureSize((SDL_Texture *)x->img.handle.ptr, &sw, &sh)) {
        return 1;
    }
    return sw != (float)x->w * s || sh != (float)x->h * s;
}

static SDL_Rect
box_cut(SDL_Rect a, SDL_Rect b)
{
    SDL_Rect r;

    if (!SDL_GetRectIntersection(&a, &b, &r)) SDL_zero(r);
    return r;
}

static SDL_Rect
device_box(float x0, float y0, float x1, float y1, float s, int m)
{
    SDL_Rect d;

    d.x = (int)SDL_floorf(x0 * s) - m;
    d.y = (int)SDL_floorf(y0 * s) - m;
    d.w = (int)SDL_ceilf(x1 * s) + m - d.x;
    d.h = (int)SDL_ceilf(y1 * s) + m - d.y;
    return d;
}

static int
item_close(reaktor_damage_list *l, int at, SDL_Rect box, int scaled)
{
    reaktor_damage_item *it;

    if (!reaktor_grow((void **)&l->item, &l->cap, l->n + 1, sizeof *l->item) ||
        (scaled && !reaktor_grow((void **)&l->scaled, &l->scaled_cap, l->scaled_n + 1,
                                 sizeof *l->scaled)))
        return 0;
    if (scaled) l->scaled[l->scaled_n++] = l->n;
    it       = &l->item[l->n++];
    it->at   = (unsigned)at;
    it->len  = (unsigned)(l->used - at);
    it->hash = reaktor_fnv_n(REAKTOR_FNV, l->key + at, it->len);
    it->box  = box;
    return 1;
}

void
reaktor_damage_add(reaktor_damage_list *l, const void *key, int len,
                   SDL_Rect box, int scaled)
{
    int at = l->used;

    if (!reaktor_grow((void **)&l->key, &l->size, l->used + len, 1)) return;
    SDL_memcpy(l->key + at, key, (size_t)len);
    l->used += len;
    if (!item_close(l, at, box, scaled)) l->used = at;
}

static int
applies(const App *app)
{
    float s = app->render_scale;

    return app->renderer_is_sw && s >= 1.0f && s == SDL_floorf(s);
}

static unsigned
stream(struct nk_context *ctx)
{
    const struct nk_command *c;
    sink  k;
    span  b;
    float thick;

    SDL_zero(k);
    k.h = REAKTOR_FNV;
    nk_foreach(c, ctx) {
        if (c->type == NK_COMMAND_NOP) continue;
        SDL_zero(b);
        command_read(c, REAKTOR_NO_CLIP, 0.0f, &k, &b, &thick);
    }
    return k.h;
}

unsigned
reaktor_damage_collect(App *app, struct nk_context *ctx)
{
    reaktor_damage      *d = &app->damage;
    reaktor_damage_list *l = &d->list[d->now];
    const struct nk_command *c;
    struct nk_rect clip = REAKTOR_NO_CLIP;
    SDL_Rect out, cut;
    float s = app->render_scale;
    float ink;
    unsigned h = REAKTOR_FNV;
    sink k;
    int i;

    d->collected = 0;
    if (!applies(app)) return stream(ctx);
    ink = SDL_max(reaktor_font_ink_em(), reaktor_text_ink_em());
    l->n = l->used = l->scaled_n = 0;
    l->ow = l->oh = 0;
    SDL_GetRenderOutputSize(app->ren, &l->ow, &l->oh);
    l->laid_w = app->laid_w;
    l->laid_h = app->laid_h;
    l->s      = s;
    out.x = out.y = 0;
    out.w = l->ow;
    out.h = l->oh;
    cut = out;
    SDL_zero(k);
    k.l  = l;
    k.ok = 1;
    nk_foreach(c, ctx) {
        SDL_Rect box = { 0, 0, 0, 0 };
        span   b;
        float  thick;
        int    at = l->used, scaled = 0;
        short  sc[4];

        if (c->type == NK_COMMAND_SCISSOR) {
            const struct nk_command_scissor *x = (const void *)c;

            clip = nk_rect(x->x, x->y, x->w, x->h);
            cut  = box_cut(device_box(clip.x, clip.y, clip.x + clip.w, clip.y + clip.h, s, 0),
                           out);
            continue;
        }
        if (c->type == NK_COMMAND_NOP) continue;
        sc[0] = (short)clip.x;
        sc[1] = (short)clip.y;
        sc[2] = (short)clip.w;
        sc[3] = (short)clip.h;
        put(&k, sc, sizeof sc);
        SDL_zero(b);
        command_read(c, clip, ink, &k, &b, &thick);
        if (!k.ok) break;
        if (b.any) {
            int m = (int)SDL_ceilf((thick * 0.5f + 1.0f) * s) + 2;

            box = box_cut(device_box(b.x0, b.y0, b.x1, b.y1, s, m), cut);
            if (!SDL_RectEmpty(&box) && c->type == NK_COMMAND_IMAGE)
                scaled = image_scaled((const struct nk_command_image *)c, s);
        }
        if (!(k.ok = item_close(l, at, box, scaled))) break;
    }
    if (!k.ok) {
        l->n = l->used = l->scaled_n = 0;
        return stream(ctx);
    }
    d->collected = 1;
    for (i = 0; i < l->n; i++) h = reaktor_fnv_n(h, &l->item[i].hash, sizeof l->item[i].hash);
    return h;
}

static int
same(const reaktor_damage_list *a, int i, const reaktor_damage_list *b, int j)
{
    const reaktor_damage_item *x = &a->item[i], *y = &b->item[j];

    return x->hash == y->hash && x->len == y->len &&
           SDL_memcmp(a->key + x->at, b->key + y->at, x->len) == 0;
}

static long long
area(SDL_Rect a)
{
    return SDL_RectEmpty(&a) ? 0 : (long long)a.w * a.h;
}

static SDL_Rect
box_join(SDL_Rect a, SDL_Rect b)
{
    SDL_Rect r;

    SDL_GetRectUnion(&a, &b, &r);
    return r;
}

/* Sharing only an edge counts: SDL_HasRectIntersection would keep apart two
   rectangles that could be one. */
static int
touches(SDL_Rect a, SDL_Rect b)
{
    return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;
}

static void
settle(SDL_Rect *r, int *n)
{
    int i, j, again = 1;

    while (again) {
        again = 0;
        for (i = 0; i < *n && !again; i++)
            for (j = i + 1; j < *n && !again; j++)
                if (touches(r[i], r[j])) {
                    r[i] = box_join(r[i], r[j]);
                    r[j] = r[--*n];
                    again = 1;
                }
        if (!again && *n > REAKTOR_DAMAGE_RECTS) {
            long long best = -1;
            int bi = 0, bj = 1;

            for (i = 0; i < *n; i++)
                for (j = i + 1; j < *n; j++) {
                    long long cost = area(box_join(r[i], r[j])) - area(r[i]) - area(r[j]);

                    if (best < 0 || cost < best) {
                        best = cost;
                        bi   = i;
                        bj   = j;
                    }
                }
            r[bi] = box_join(r[bi], r[bj]);
            r[bj] = r[--*n];
            again = 1;
        }
    }
}

static void
take(SDL_Rect *r, int *n, SDL_Rect b)
{
    if (SDL_RectEmpty(&b)) return;
    r[(*n)++] = b;
    settle(r, n);
}

int
reaktor_damage_diff(const reaktor_damage_list *was, const reaktor_damage_list *now,
                    SDL_Rect *rects, int *count, long long *covered)
{
    SDL_Rect r[REAKTOR_DAMAGE_RECTS + 1];
    SDL_Rect o = { 0, 0, 0, 0 };
    int a = 0, b = 0, ae = was->n, be = now->n, n = 0, i, si, changed;

    *count   = 0;
    *covered = 0;
    if (was->ow != now->ow || was->oh != now->oh || was->s != now->s ||
        was->laid_w != now->laid_w || was->laid_h != now->laid_h ||
        now->ow <= 0 || now->oh <= 0 || now->s < 1.0f)
        return REAKTOR_DAMAGE_FULL;

    while (a < ae && b < be && same(was, a, now, b)) a++, b++;
    while (ae > a && be > b && same(was, ae - 1, now, be - 1)) ae--, be--;
    while (a < ae && b < be) {
        int d, hit = 0;

        if (same(was, a, now, b)) {
            a++, b++;
            continue;
        }
        for (d = 1; d <= LOOKAHEAD && !hit; d++) {
            if (b + d < be && same(was, a, now, b + d)) {
                for (i = 0; i < d; i++) take(r, &n, now->item[b + i].box);
                b += d;
                hit = 1;
            } else if (a + d < ae && same(was, a + d, now, b)) {
                for (i = 0; i < d; i++) take(r, &n, was->item[a + i].box);
                a += d;
                hit = 1;
            }
        }
        if (!hit) {
            take(r, &n, was->item[a++].box);
            take(r, &n, now->item[b++].box);
        }
    }
    while (a < ae) take(r, &n, was->item[a++].box);
    while (b < be) take(r, &n, now->item[b++].box);
    if (!n) return REAKTOR_DAMAGE_NONE;

    si = (int)now->s;
    do {
        changed = 0;
        for (i = 0; i < now->scaled_n; i++) {
            SDL_Rect box = now->item[now->scaled[i]].box;
            int j;

            for (j = 0; j < n; j++) {
                SDL_Rect g = box_join(r[j], box);

                if (SDL_HasRectIntersection(&r[j], &box) && !SDL_RectsEqual(&g, &r[j])) {
                    r[j]    = g;
                    changed = 1;
                }
            }
        }
        for (i = 0; i < n; i++) {
            SDL_Rect g;

            g.x = r[i].x / si * si;
            g.y = r[i].y / si * si;
            g.w = (r[i].x + r[i].w + si - 1) / si * si - g.x;
            g.h = (r[i].y + r[i].h + si - 1) / si * si - g.y;
            if (!SDL_RectsEqual(&g, &r[i])) {
                r[i]    = g;
                changed = 1;
            }
        }
        if (changed) settle(r, &n);
    } while (changed);

    o.w = now->ow;
    o.h = now->oh;
    for (i = 0; i < n; i++) {
        *covered += area(box_cut(r[i], o));
        rects[i]  = r[i];
    }
    *count = n;
    if (*covered * 2 > (long long)now->ow * now->oh) return REAKTOR_DAMAGE_FULL;
    return REAKTOR_DAMAGE_PARTIAL;
}

static SDL_Texture *
target(App *app, int w, int h)
{
    const SDL_PixelFormat *f = (const SDL_PixelFormat *)SDL_GetPointerProperty(
        SDL_GetRendererProperties(app->ren), SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, NULL);
    SDL_Texture *t;

    if (!f || *f == SDL_PIXELFORMAT_UNKNOWN) return NULL;
    t = SDL_CreateTexture(app->ren, *f, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    return t;
}

/* SDL keeps a view per target: the frame's scale on K, none on the window. */
int
reaktor_damage_views(App *app)
{
    float s = app->render_scale;

    if (!app->damage.k) return 0;
    SDL_SetRenderTarget(app->ren, NULL);
    SDL_SetRenderScale(app->ren, 1.0f, 1.0f);
    SDL_SetRenderTarget(app->ren, app->damage.k);
    SDL_SetRenderScale(app->ren, s, s);
    return 1;
}

static void
drop(App *app)
{
    reaktor_damage *d = &app->damage;

    if (!d->k) return;
    SDL_SetRenderTarget(app->ren, NULL);
    SDL_DestroyTexture(d->k);
    if (d->check) SDL_DestroyTexture(d->check);
    d->k = d->check = NULL;
    SDL_SetRenderScale(app->ren, app->render_scale, app->render_scale);
}

void
reaktor_damage_free(App *app)
{
    int i;

    if (app->ren) drop(app);
    for (i = 0; i < 2; i++) {
        SDL_free(app->damage.list[i].item);
        SDL_free(app->damage.list[i].key);
        SDL_free(app->damage.list[i].scaled);
    }
    SDL_zero(app->damage);
}

static int
pixel_mask(const SDL_Surface *a)
{
    return SDL_BYTESPERPIXEL(a->format) == 4 && !SDL_ISPIXELFORMAT_ALPHA(a->format);
}

static void
check(App *app, const SDL_Rect *r, int n)
{
    reaktor_damage *d = &app->damage;
    SDL_Surface *got = NULL, *want = NULL;
    Uint32 keep = 0xFFFFFFFFu;
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1, x, y, i;

    if (!d->check && !(d->check = target(app, d->kw, d->kh))) return;
    SDL_SetRenderTarget(app->ren, d->check);
    SDL_SetRenderScale(app->ren, app->render_scale, app->render_scale);
    reaktor_render(app, NULL, 0);
    want = SDL_RenderReadPixels(app->ren, NULL);
    SDL_SetRenderTarget(app->ren, d->k);
    got = SDL_RenderReadPixels(app->ren, NULL);
    if (got && want && pixel_mask(got)) {
        const SDL_PixelFormatDetails *f = SDL_GetPixelFormatDetails(got->format);

        if (f) keep = f->Rmask | f->Gmask | f->Bmask;
    }
    for (y = 0; got && want && y < got->h && y < want->h; y++) {
        const Uint8 *p = (const Uint8 *)got->pixels + y * got->pitch;
        const Uint8 *q = (const Uint8 *)want->pixels + y * want->pitch;
        int bpp = SDL_BYTESPERPIXEL(got->format);

        for (x = 0; x < got->w; x++) {
            int differ;

            if (bpp == 4) {
                Uint32 u, v;

                SDL_memcpy(&u, p + x * 4, 4);
                SDL_memcpy(&v, q + x * 4, 4);
                differ = ((u ^ v) & keep) != 0;
            } else {
                differ = SDL_memcmp(p + x * bpp, q + x * bpp, (size_t)bpp) != 0;
            }
            if (!differ) continue;
            if (x1 < 0) {
                x0 = x1 = x;
                y0 = y1 = y;
            }
            x0 = SDL_min(x0, x);
            x1 = SDL_max(x1, x);
            y0 = SDL_min(y0, y);
            y1 = SDL_max(y1, y);
        }
    }
    if (!got || !want) {
        SDL_Log("damage: cannot read the frame back (%s)", SDL_GetError());
        d->mismatched++;
    } else if (x1 >= 0) {
        char rs[256];
        int  at = 0;

        rs[0] = 0;
        for (i = 0; i < n && at < (int)sizeof rs; i++)
            at += SDL_snprintf(rs + at, sizeof rs - (size_t)at, " %d,%d %dx%d",
                               r[i].x, r[i].y, r[i].w, r[i].h);
        SDL_Log("damage: frame %u differs from a full redraw in %d,%d %dx%d (device);"
                " redrawn:%s", app->ctx->seq, x0, y0, x1 - x0 + 1, y1 - y0 + 1,
                n ? rs : " nothing");
        d->mismatched++;
    }
    SDL_DestroySurface(got);
    SDL_DestroySurface(want);
}

int
reaktor_damage_render(App *app)
{
    reaktor_damage      *d   = &app->damage;
    reaktor_damage_list *now = &d->list[d->now], *was = &d->list[!d->now];
    SDL_Rect   box[REAKTOR_DAMAGE_RECTS];
    SDL_Rect             r[REAKTOR_DAMAGE_RECTS];
    long long            covered = 0;
    int kind = REAKTOR_DAMAGE_FULL, n = 0, fresh = 0, direct = 0, si, i;

    if (!d->collected || !applies(app)) {
        drop(app);
        direct = 1;
    } else if (!d->k || d->kw != now->ow || d->kh != now->oh) {
        drop(app);
        if (!(d->k = target(app, now->ow, now->oh))) {
            SDL_SetRenderTarget(app->ren, NULL);
            direct = 1;
        } else {
            d->kw = now->ow;
            d->kh = now->oh;
            fresh = 1;
            reaktor_damage_views(app);
        }
    } else if (SDL_GetRenderTarget(app->ren) != d->k) {
        SDL_SetRenderTarget(app->ren, d->k);
    }

    if (!direct && !fresh && !d->forced &&
        SDL_memcmp(&d->clear, &app->clear, sizeof d->clear) == 0)
        kind = reaktor_damage_diff(was, now, box, &n, &covered);

    if (kind == REAKTOR_DAMAGE_NONE) return 0;
    reaktor_mask_rounded_fills(app);
    if (direct) {
        reaktor_render(app, NULL, 0);
        return 1;
    }
    if (kind == REAKTOR_DAMAGE_FULL) {
        reaktor_render(app, NULL, 0);
        d->full++;
    } else {
        float share = (float)covered / ((float)now->ow * (float)now->oh);

        si = (int)app->render_scale;
        for (i = 0; i < n; i++) {
            r[i].x = box[i].x / si;
            r[i].y = box[i].y / si;
            r[i].w = box[i].w / si;
            r[i].h = box[i].h / si;
        }
        if (share > d->peak) d->peak = share;
        reaktor_render(app, r, n);
        if (d->checking) check(app, r, n);
        d->partial++;
    }

    SDL_SetRenderTarget(app->ren, NULL);
    SDL_RenderTexture(app->ren, d->k, NULL, NULL);
    d->now   = !d->now;
    d->clear = app->clear;
    return 1;
}

void
reaktor_damage_presented(App *app)
{
    if (app->damage.k) SDL_SetRenderTarget(app->ren, app->damage.k);
}
