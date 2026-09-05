/* container_nk.h - C interface to the litehtml document.
 *
 * litehtml is the project's only C++ component, confined behind this header.
 *
 * The C++ side never includes nuklear.h. That is partly forced - nuklear.h
 * declares C++ templates, so it cannot be wrapped in extern "C", and
 * including it unwrapped would give every nk_* symbol C++ linkage and break
 * against main.c - but it is also the better design: painting goes through
 * the small vtable below, so the backend is swappable. Today it is Nuklear;
 * pointing it straight at SDL_Renderer later is a change to one file
 * (FINDINGS §16). */
#ifndef CURIE_CONTAINER_NK_H
#define CURIE_CONTAINER_NK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the document needs in order to be measured and painted.
 * Coordinates are logical px with the document's origin at (0,0); the
 * implementation applies the widget offset. */
typedef struct curie_painter {
    void *user;

    /* `rounding` is the border-radius in px; 0 for square corners. */
    void  (*fill_rect)   (void *user, float x, float y, float w, float h,
                          float rounding,
                          unsigned char r, unsigned char g,
                          unsigned char b, unsigned char a);
    void  (*stroke_rect) (void *user, float x, float y, float w, float h,
                          float rounding, float thickness,
                          unsigned char r, unsigned char g,
                          unsigned char b, unsigned char a);

    /* Two-stop gradient. `vertical` selects top->bottom over left->right. */
    void  (*fill_gradient)(void *user, float x, float y, float w, float h,
                           const unsigned char *rgba_from,
                           const unsigned char *rgba_to, int vertical);

    void  (*draw_text)   (void *user, float x, float y, float w, float h,
                          const char *text, int len,
                          unsigned char r, unsigned char g,
                          unsigned char b, unsigned char a);
    void  (*push_clip)   (void *user, float x, float y, float w, float h);
    void  (*pop_clip)    (void *user);

    /* Images are resolved by path relative to the repo root. An optional
     * "?stroke=<family>-<shade>&fill=<family>-<shade>" query recolours an SVG
     * from Open-Color, which is how icons pick up theme colours - CSS cannot
     * reach inside an <img>. Returns 0 if the image cannot be loaded. */
    int   (*image_size)  (void *user, const char *src, int *w, int *h);
    void  (*image_draw)  (void *user, const char *src,
                          float x, float y, float w, float h);

    /* Fired when a link is activated. litehtml resolves which element was
     * hit; this is how a document reaches back into the application. */
    void  (*anchor_click)(void *user, const char *url);

    /* Measurement must come from the same source as drawing, or text wraps
     * at one width and paints at another. */
    float (*text_width)  (void *user, const char *text, int len);
    float (*font_height) (void *user);
} curie_painter;

typedef struct curie_doc curie_doc;

curie_doc *curie_doc_create(const curie_painter *painter);
void       curie_doc_destroy(curie_doc *doc);

/* Loads HTML plus `css_count` stylesheets, concatenated in order, all read
 * from disk at runtime. Order matters: Open-Color first so its custom
 * properties are defined before anything uses var(). Returns 0 on failure. */
int  curie_doc_load(curie_doc *doc, const char *html_path,
                    const char *const *css_paths, int css_count);

/* Same, but the markup is supplied directly. Used when the page is assembled
 * at runtime rather than read verbatim from disk. */
int  curie_doc_load_html(curie_doc *doc, const char *html,
                         const char *const *css_paths, int css_count);

/* Lays the document out into a `width` x `height` viewport, in logical px,
 * and returns the content height. The height matters: it is what media
 * queries and vh units resolve against. */
int  curie_doc_render(curie_doc *doc, int width, int height);

/* Paints the laid-out document with its origin at (x, y). */
void curie_doc_draw(curie_doc *doc, int x, int y, int w, int h);

/* Pointer input, in *document* coordinates. Press and release are separate
 * calls deliberately: litehtml fires an anchor click on every button-up it is
 * told about, so sending one per frame while the button is held would fire a
 * link repeatedly. The caller reports edges, not state.
 * Each returns 1 if anything needs repainting. */
int  curie_doc_mouse_move(curie_doc *doc, int x, int y);
int  curie_doc_mouse_down(curie_doc *doc, int x, int y);
int  curie_doc_mouse_up(curie_doc *doc, int x, int y);

/* Laid-out box of the first element matching `selector`, in document
 * coordinates. Returns 0 if there is no match. This is what lets the
 * application draw over a specific element - an editable field, say - without
 * rebuilding the page on every keystroke. */
/* CSS `cursor` value for the element currently under the pointer, as litehtml
 * resolved it ("pointer", "text", "default", ...). Never NULL. */
/* Sets an attribute on the root <html> element and re-applies the cascade
 * from the already-parsed stylesheets. This is the cheap path for state that
 * CSS can express as an attribute selector: no document rebuild, so none of
 * the ~45 ms spent re-parsing pico. Returns 0 if there is no document. */
int  curie_doc_set_root_attr(curie_doc *doc, const char *name, const char *value);

const char *curie_doc_cursor(curie_doc *doc);

int  curie_doc_element_rect(curie_doc *doc, const char *selector,
                            int *x, int *y, int *w, int *h);

const char *curie_doc_last_error(curie_doc *doc);
int         curie_doc_height(curie_doc *doc);

#ifdef __cplusplus
}
#endif

#endif /* CURIE_CONTAINER_NK_H */
