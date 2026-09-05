/* container_nk.cpp - litehtml's document_container, painted via curie_painter.
 *
 * This is the single C++ translation unit in the project. It implements the
 * 30 pure virtuals litehtml requires and exposes only the C API declared in
 * container_nk.h.
 *
 * Note what is *not* included: nuklear.h. It declares C++ templates, so it
 * cannot be wrapped in extern "C", and including it unwrapped would give every
 * nk_* symbol C++ linkage and break against main.c. Painting therefore goes
 * through the curie_painter vtable, which also makes the backend swappable
 * (FINDINGS 16). */

#include <litehtml.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <string>

extern "C" {
#include "container_nk.h"
#include "curie.h"
}

namespace {

class curie_container : public litehtml::document_container
{
public:
    explicit curie_container(const curie_painter &p)
        : m_p(p), m_origin_x(0), m_origin_y(0),
          m_viewport_w(800), m_viewport_h(600) {}

    void begin_paint(int x, int y, int w, int h)
    {
        m_origin_x = x;
        m_origin_y = y;
        m_viewport_w = w;
        m_viewport_h = h;
    }

    void set_viewport(int w, int h) { m_viewport_w = w; m_viewport_h = h; }

    /* --- fonts -------------------------------------------------------- */
    litehtml::uint_ptr create_font(const litehtml::font_description &descr,
                                   const litehtml::document *doc,
                                   litehtml::font_metrics *fm) override
    {
        (void)doc;
        /* One atlas font for now, so every request maps to it. Real family
         * and weight selection arrives with the font manager (PLAN M3). */
        if (fm) {
            float h = m_p.font_height ? m_p.font_height(m_p.user)
                                      : (float)descr.size;
            fm->font_size   = (litehtml::pixel_t)descr.size;
            fm->height      = (litehtml::pixel_t)h;
            fm->ascent      = (litehtml::pixel_t)(h * 0.8f);
            fm->descent     = (litehtml::pixel_t)(h * 0.2f);
            fm->x_height    = (litehtml::pixel_t)(h * 0.5f);
            fm->ch_width    = (litehtml::pixel_t)measure("0", 1);
            fm->draw_spaces = false;
            fm->sub_shift   = (litehtml::pixel_t)(h * 0.15f);
            fm->super_shift = (litehtml::pixel_t)(h * 0.3f);
        }
        return (litehtml::uint_ptr)1;
    }

    void delete_font(litehtml::uint_ptr) override {}

    litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr) override
    {
        return (litehtml::pixel_t)measure(text, text ? (int)strlen(text) : 0);
    }

    void draw_text(litehtml::uint_ptr, const char *text, litehtml::uint_ptr,
                   litehtml::web_color color,
                   const litehtml::position &pos) override
    {
        if (!m_p.draw_text || !text) return;
        m_p.draw_text(m_p.user, (float)(pos.x + m_origin_x),
                      (float)(pos.y + m_origin_y), (float)pos.width,
                      (float)pos.height, text, (int)strlen(text),
                      color.red, color.green, color.blue, color.alpha);
    }

    litehtml::pixel_t pt_to_px(float pt) const override
    {
        return (litehtml::pixel_t)(pt * 96.0f / 72.0f + 0.5f);
    }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    const char *get_default_font_name() const override { return "Zerove"; }

    /* --- painting ------------------------------------------------------ */
    void draw_solid_fill(litehtml::uint_ptr,
                         const litehtml::background_layer &layer,
                         const litehtml::web_color &color) override
    {
        if (!m_p.fill_rect || color.alpha == 0) return;
        const litehtml::position &b = layer.border_box;
        m_p.fill_rect(m_p.user, (float)(b.x + m_origin_x),
                      (float)(b.y + m_origin_y), (float)b.width,
                      (float)b.height, radius_of(layer.border_radius),
                      color.red, color.green, color.blue, color.alpha);
    }

    void draw_borders(litehtml::uint_ptr, const litehtml::borders &borders,
                      const litehtml::position &pos, bool) override
    {
        if (!m_p.fill_rect) return;

        const float x = (float)(pos.x + m_origin_x);
        const float y = (float)(pos.y + m_origin_y);
        const float w = (float)pos.width;
        const float h = (float)pos.height;

        const litehtml::border &t = borders.top;
        const litehtml::border &r = borders.right;
        const litehtml::border &b = borders.bottom;
        const litehtml::border &l = borders.left;

        /* A uniform border can be one rounded stroke, which is the only way
         * the radius gets honoured. Anything else - and `border-top` alone is
         * very common - must be drawn edge by edge, or a single-sided border
         * would paint as a full box. */
        bool uniform =
            t.width == r.width && t.width == b.width && t.width == l.width &&
            same(t.color, r.color) && same(t.color, b.color) &&
            same(t.color, l.color);

        if (uniform) {
            if ((float)t.width > 0.0f && t.color.alpha > 0 && m_p.stroke_rect)
                m_p.stroke_rect(m_p.user, x, y, w, h,
                                radius_of(borders.radius), (float)t.width,
                                t.color.red, t.color.green, t.color.blue,
                                t.color.alpha);
            return;
        }

        if ((float)t.width > 0.0f && t.color.alpha > 0)
            m_p.fill_rect(m_p.user, x, y, w, (float)t.width, 0.0f,
                          t.color.red, t.color.green, t.color.blue, t.color.alpha);
        if ((float)b.width > 0.0f && b.color.alpha > 0)
            m_p.fill_rect(m_p.user, x, y + h - (float)b.width, w, (float)b.width,
                          0.0f,
                          b.color.red, b.color.green, b.color.blue, b.color.alpha);
        if ((float)l.width > 0.0f && l.color.alpha > 0)
            m_p.fill_rect(m_p.user, x, y, (float)l.width, h, 0.0f,
                          l.color.red, l.color.green, l.color.blue, l.color.alpha);
        if ((float)r.width > 0.0f && r.color.alpha > 0)
            m_p.fill_rect(m_p.user, x + w - (float)r.width, y, (float)r.width, h,
                          0.0f,
                          r.color.red, r.color.green, r.color.blue, r.color.alpha);
    }

    void set_clip(const litehtml::position &pos,
                  const litehtml::border_radiuses &) override
    {
        if (!m_p.push_clip) return;
        m_p.push_clip(m_p.user, (float)(pos.x + m_origin_x),
                      (float)(pos.y + m_origin_y), (float)pos.width,
                      (float)pos.height);
    }

    void del_clip() override
    {
        if (m_p.pop_clip) m_p.pop_clip(m_p.user);
    }

    /* Gradients and images are not part of the first slice; they draw nothing
     * rather than something wrong. */
    void draw_linear_gradient(litehtml::uint_ptr,
        const litehtml::background_layer &layer,
        const litehtml::background_layer::linear_gradient &g) override
    {
        if (!m_p.fill_gradient || g.color_points.size() < 2) return;

        /* Nuklear interpolates between corner colours, so only the two end
         * stops are honoured; multi-stop gradients degrade to first->last. */
        const litehtml::web_color &a = g.color_points.front().color;
        const litehtml::web_color &b = g.color_points.back().color;
        unsigned char from[4] = { a.red, a.green, a.blue, a.alpha };
        unsigned char to[4]   = { b.red, b.green, b.blue, b.alpha };

        const litehtml::position &r = layer.border_box;
        int vertical = abs(g.end.y - g.start.y) >= abs(g.end.x - g.start.x);
        m_p.fill_gradient(m_p.user, (float)(r.x + m_origin_x),
                          (float)(r.y + m_origin_y), (float)r.width,
                          (float)r.height, from, to, vertical);
    }
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::radial_gradient &) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::conic_gradient &) override {}
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker &) override {}
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer &layer,
                    const std::string &url, const std::string &) override
    {
        if (!m_p.image_draw || url.empty()) return;
        const litehtml::position &r = layer.border_box;
        m_p.image_draw(m_p.user, url.c_str(), (float)(r.x + m_origin_x),
                       (float)(r.y + m_origin_y), (float)r.width,
                       (float)r.height);
    }

    /* Decoding happens lazily inside image_size/image_draw, which cache, so
     * there is nothing to prefetch here. */
    void load_image(const char *, const char *, bool) override {}

    void get_image_size(const char *src, const char *, litehtml::size &sz) override
    {
        int w = 0, h = 0;
        sz.width = 0;
        sz.height = 0;
        if (!m_p.image_size || !src) return;
        if (m_p.image_size(m_p.user, src, &w, &h)) {
            sz.width = w;
            sz.height = h;
        }
    }

    /* --- document services --------------------------------------------- */
    void set_caption(const char *) override {}
    void set_base_url(const char *) override {}
    void link(const std::shared_ptr<litehtml::document> &,
              const litehtml::element::ptr &) override {}
    void on_anchor_click(const char *url, const litehtml::element::ptr &) override
    {
        if (m_p.anchor_click && url) m_p.anchor_click(m_p.user, url);
    }
    void on_mouse_event(const litehtml::element::ptr &,
                        litehtml::mouse_event) override {}
    void set_cursor(const char *cursor) override
    {
        m_cursor = cursor ? cursor : "";
    }

    const char *cursor() const { return m_cursor.c_str(); }

    void transform_text(std::string &text, litehtml::text_transform tt) override
    {
        /* ASCII-only, which is honest: correct casing needs Unicode tables. */
        if (tt == litehtml::text_transform_uppercase) {
            for (size_t i = 0; i < text.size(); i++)
                if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 32;
        } else if (tt == litehtml::text_transform_lowercase) {
            for (size_t i = 0; i < text.size(); i++)
                if (text[i] >= 'A' && text[i] <= 'Z') text[i] += 32;
        }
    }

    void import_css(std::string &text, const std::string &url,
                    std::string &) override
    {
        /* Resolves @import against the repo root, same as every other asset. */
        char path[1024];
        if (!curie_path(path, sizeof(path), url.c_str())) return;
        size_t len = 0;
        char *data = curie_read_file(path, &len);
        if (data) {
            text.assign(data, len);
            curie_free(data);
        }
    }

    void get_viewport(litehtml::position &viewport) const override
    {
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = m_viewport_w;
        viewport.height = m_viewport_h;
    }

    litehtml::element::ptr create_element(const char *, const litehtml::string_map &,
        const std::shared_ptr<litehtml::document> &) override { return nullptr; }

    void get_media_features(litehtml::media_features &media) const override
    {
        /* pico.css carries 18 media queries; without these it would render
         * its smallest breakpoint at every size. */
        media.type          = litehtml::media_type_screen;
        media.width         = m_viewport_w;
        media.height        = m_viewport_h;
        media.device_width  = m_viewport_w;
        media.device_height = m_viewport_h;
        media.color         = 8;
        media.monochrome    = 0;
        media.color_index   = 256;
        media.resolution    = 96;
    }

    void get_language(std::string &language, std::string &culture) const override
    {
        language = "en";
        culture = "";
    }

private:
    static bool same(const litehtml::web_color &a, const litehtml::web_color &b)
    {
        return a.red == b.red && a.green == b.green &&
               a.blue == b.blue && a.alpha == b.alpha;
    }

    /* Nuklear takes a single corner radius, so the largest corner wins; a
     * per-corner path would mean drawing the rect as a polygon. */
    static float radius_of(const litehtml::border_radiuses &r)
    {
        float m = (float)r.top_left_x;
        if ((float)r.top_right_x > m)    m = (float)r.top_right_x;
        if ((float)r.bottom_left_x > m)  m = (float)r.bottom_left_x;
        if ((float)r.bottom_right_x > m) m = (float)r.bottom_right_x;
        return m;
    }

    float measure(const char *text, int len) const
    {
        if (!m_p.text_width || !text || len <= 0) return 0.0f;
        return m_p.text_width(m_p.user, text, len);
    }

    curie_painter m_p;
    int           m_origin_x, m_origin_y;
    int           m_viewport_w, m_viewport_h;
    std::string   m_cursor;
};

} /* namespace */

/* ======================================================================= */
/* C API                                                                    */
/* ======================================================================= */

struct curie_doc {
    curie_container        *container;
    litehtml::document::ptr document;
    std::string             error;
    int                     height;
    int                     last_w;   /* viewport the cascade was built for */
    int                     last_h;
};

extern "C" {

curie_doc *curie_doc_create(const curie_painter *painter)
{
    if (!painter) return nullptr;
    curie_doc *d = new (std::nothrow) curie_doc();
    if (!d) return nullptr;
    d->container = new (std::nothrow) curie_container(*painter);
    if (!d->container) { delete d; return nullptr; }
    d->height = 0;
    d->last_w = 0;
    d->last_h = 0;
    return d;
}

void curie_doc_destroy(curie_doc *d)
{
    if (!d) return;
    d->document.reset();
    delete d->container;
    delete d;
}

/* Reads and concatenates the stylesheets, then builds the document. */
static int load_with_css(curie_doc *d, const std::string &html_str,
                         const char *const *css_paths, int css_count)
{
    char path[1024];
    size_t len = 0;

    /* Concatenated in the order given: Open-Color defines the custom
     * properties, simple.css styles the semantics, app.css layers on top. */
    std::string css_str;
    for (int i = 0; i < css_count; i++) {
        if (!css_paths || !css_paths[i]) continue;
        if (!curie_path(path, sizeof(path), css_paths[i])) {
            d->error = std::string("cannot resolve ") + css_paths[i];
            return 0;
        }
        char *css = curie_read_file(path, &len);
        if (!css) { d->error = std::string("cannot read ") + path; return 0; }
        css_str.append(css, len);
        css_str.append("\n");
        curie_free(css);
    }

    d->document = litehtml::document::createFromString(
        html_str.c_str(), d->container, litehtml::master_css, css_str);
    if (!d->document) { d->error = "createFromString returned null"; return 0; }

    d->last_w = 0;   /* force media re-evaluation on the next render */
    d->last_h = 0;
    d->error.clear();
    return 1;
}

int curie_doc_load(curie_doc *d, const char *html_path,
                   const char *const *css_paths, int css_count)
{
    char path[1024];
    size_t len = 0;

    if (!d || !html_path) return 0;
    if (!curie_path(path, sizeof(path), html_path)) {
        d->error = "cannot resolve html path";
        return 0;
    }
    char *html = curie_read_file(path, &len);
    if (!html) { d->error = std::string("cannot read ") + path; return 0; }
    std::string html_str(html, len);
    curie_free(html);

    return load_with_css(d, html_str, css_paths, css_count);
}

int curie_doc_load_html(curie_doc *d, const char *html,
                        const char *const *css_paths, int css_count)
{
    if (!d || !html) return 0;
    return load_with_css(d, std::string(html), css_paths, css_count);
}

int curie_doc_render(curie_doc *d, int width, int height)
{
    if (!d || !d->document) return 0;

    /* litehtml reads media features once, when the document is constructed
     * (document.cpp: get_media_features in the ctor). The container cannot
     * know the real viewport that early, so the first cascade is built
     * against its default - and any responsive stylesheet then lays out for
     * the wrong breakpoint until something forces a re-parse. pico shifts
     * --pico-font-size at 576px and above, so the whole page reflowed the
     * first time anything rebuilt it.
     *
     * media_changed() re-reads the features and re-applies the cascade. */
    if (width != d->last_w || height != d->last_h) {
        d->container->set_viewport(width, height);
        d->document->media_changed();
        d->last_w = width;
        d->last_h = height;
    }

    d->height = (int)d->document->render(width);
    return d->height;
}

void curie_doc_draw(curie_doc *d, int x, int y, int w, int h)
{
    if (!d || !d->document) return;

    litehtml::position clip(0, 0, w, h);
    d->container->begin_paint(x, y, w, h);
    d->document->draw((litehtml::uint_ptr)0, 0, 0, &clip);
}

/* litehtml wants a redraw-region callback; the whole page is repainted every
 * frame, so there is nothing to accumulate yet. */
static void noop_redraw(const litehtml::position &) {}

int curie_doc_mouse_move(curie_doc *d, int x, int y)
{
    if (!d || !d->document) return 0;
    return d->document->on_mouse_over(x, y, x, y, noop_redraw) ? 1 : 0;
}

int curie_doc_mouse_down(curie_doc *d, int x, int y)
{
    if (!d || !d->document) return 0;
    return d->document->on_lbutton_down(x, y, x, y, noop_redraw) ? 1 : 0;
}

int curie_doc_mouse_up(curie_doc *d, int x, int y)
{
    if (!d || !d->document) return 0;
    return d->document->on_lbutton_up(x, y, x, y, noop_redraw) ? 1 : 0;
}

int curie_doc_set_root_attr(curie_doc *d, const char *name, const char *value)
{
    if (!d || !d->document || !name || !value) return 0;

    litehtml::element::ptr root = d->document->root();
    if (!root) return 0;

    /* set_attr, refresh_styles and compute_styles are all public on element,
     * so the restyle media_changed() performs internally is reachable without
     * patching litehtml. The render tree is left alone deliberately: an
     * attribute that only changes colours cannot change any element's
     * display, so no render items need creating or destroying. */
    root->set_attr(name, value);
    root->refresh_styles();
    root->compute_styles();

    d->last_w = 0;   /* force the next render() to re-lay-out */
    d->last_h = 0;
    return 1;
}

const char *curie_doc_cursor(curie_doc *d)
{
    return (d && d->container) ? d->container->cursor() : "";
}

int curie_doc_element_rect(curie_doc *d, const char *selector,
                           int *x, int *y, int *w, int *h)
{
    if (!d || !d->document || !selector) return 0;

    litehtml::element::ptr root = d->document->root();
    if (!root) return 0;

    litehtml::element::ptr el = root->select_one(selector);
    if (!el) return 0;

    litehtml::position p = el->get_placement();
    if (x) *x = (int)p.x;
    if (y) *y = (int)p.y;
    if (w) *w = (int)p.width;
    if (h) *h = (int)p.height;
    return 1;
}

const char *curie_doc_last_error(curie_doc *d)
{
    return (d && !d->error.empty()) ? d->error.c_str() : "";
}

int curie_doc_height(curie_doc *d)
{
    return d ? d->height : 0;
}

} /* extern "C" */
