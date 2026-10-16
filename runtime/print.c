#include "internal.h"
#include "reaktor/launch.h"
#include "declare.h"
#include "hpdf.h"
#include "plutovg.h"

#define REAKTOR_PRINT_PAGES 64

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

typedef struct print_job {
    reaktor_print_spec spec;
    char              *path;
    int                index, to_printer;
} print_job;

typedef struct jpeg_buf {
    unsigned char *bytes;
    size_t         len, cap;
} jpeg_buf;

static void
print_page(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    print_job *job = (print_job *)user;

    job->spec.page(app, ctx, job->index, w, h, job->spec.user);
}

static void
jpeg_put(void *closure, void *data, int size)
{
    jpeg_buf *b = (jpeg_buf *)closure;

    if (b->len + (size_t)size > b->cap) {
        size_t         cap  = (b->len + (size_t)size) * 2;
        unsigned char *more = (unsigned char *)SDL_realloc(b->bytes, cap);

        if (!more) return;
        b->bytes = more;
        b->cap   = cap;
    }
    SDL_memcpy(b->bytes + b->len, data, (size_t)size);
    b->len += (size_t)size;
}

#ifdef _WIN32
/* The system's print dialog, then each page stretched over the printable area. */
static int
print_win32(App *app, SDL_Surface *const *page, int n, const char *title)
{
    PRINTDLGEXW pd;
    DOCINFOW    di;
    WCHAR       name[256];
    int         i, ok = 1;

    SDL_zero(pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner   = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(app->win),
                                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    pd.Flags       = PD_RETURNDC | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_NOPAGENUMS;
    pd.nStartPage  = START_PAGE_GENERAL;
    if (PrintDlgExW(&pd) != S_OK || pd.dwResultAction != PD_RESULT_PRINT || !pd.hDC) {
        if (pd.hDC) DeleteDC(pd.hDC);
        return 0;
    }
    MultiByteToWideChar(CP_UTF8, 0, title, -1, name, (int)SDL_arraysize(name) - 1);
    SDL_zero(di);
    di.cbSize      = sizeof di;
    di.lpszDocName = name;
    if (StartDocW(pd.hDC, &di) <= 0) ok = 0;
    for (i = 0; ok && i < n; i++) {
        int        pw = GetDeviceCaps(pd.hDC, HORZRES), ph = GetDeviceCaps(pd.hDC, VERTRES);
        BITMAPINFO bi;

        SDL_zero(bi);
        bi.bmiHeader.biSize        = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth       = page[i]->w;
        bi.bmiHeader.biHeight      = -page[i]->h;
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        ok = StartPage(pd.hDC) > 0 &&
             StretchDIBits(pd.hDC, 0, 0, pw, ph, 0, 0, page[i]->w, page[i]->h,
                           page[i]->pixels, &bi, DIB_RGB_COLORS, SRCCOPY) != GDI_ERROR &&
             EndPage(pd.hDC) > 0;
    }
    if (ok) EndDoc(pd.hDC);
    else AbortDoc(pd.hDC);
    DeleteDC(pd.hDC);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    return ok;
}
#elif defined(__APPLE__)
int reaktor_print_macos(const char *path);
#endif

static void
print_run(App *app, void *user)
{
    print_job   *job = (print_job *)user;
    float        keep = reaktor_scale(), pt_w = job->spec.w, pt_h = job->spec.h;
    SDL_Surface *page[REAKTOR_PRINT_PAGES];
    HPDF_Doc     doc = NULL;
    App         *pa = NULL;
    int          id, i, n = 0, ok = 1;

    SDL_zero(page);
    reaktor_set_scale((float)job->spec.dpi / 72.0f);
    id = reaktor_window_spawn(&(reaktor_window){
             .window = { .w = (int)pt_w, .h = (int)pt_h, .hidden = 1, .borderless = 1 },
             .page = print_page, .user = job }, NULL);
    pa = id ? reaktor_window_app(id) : NULL;
    for (i = 0; pa && i < job->spec.pages && i < REAKTOR_PRINT_PAGES; i++, n++) {
        int w, h, k;

        job->index = i;
        reaktor_app_size(pa, &w, &h);
        for (k = 0; k < 6 || (!reaktor_frame_settled(pa) && k < 24); k++)
            reaktor_app_frame(pa, w, h, "", print_page, job);
        pa->want_capture = 1;
        reaktor_app_frame(pa, w, h, "", print_page, job);
        page[i] = pa->capture ? SDL_ConvertSurface(pa->capture, SDL_PIXELFORMAT_ARGB8888) : NULL;
        SDL_DestroySurface(pa->capture);
        pa->capture = NULL;
        if (!page[i]) ok = 0;
    }
    if (id) reaktor_window_close(app, id);
    reaktor_set_scale(keep);
    if (!pa) ok = 0;

#ifdef _WIN32
    if (ok && job->to_printer)
        ok = print_win32(app, page, n, job->spec.title ? job->spec.title : "Reaktor");
    else
#endif
    if (ok) doc = HPDF_New(NULL, NULL);
    for (i = 0; doc && ok && i < n; i++) {
        plutovg_surface_t *s = plutovg_surface_create_for_data(page[i]->pixels, page[i]->w,
                                                               page[i]->h, page[i]->pitch);
        jpeg_buf   b = { NULL, 0, 0 };
        HPDF_Page  p = HPDF_AddPage(doc);
        HPDF_Image im = NULL;

        if (s) plutovg_surface_write_to_jpg_stream(s, jpeg_put, &b, 90);
        plutovg_surface_destroy(s);
        if (b.len) im = HPDF_LoadJpegImageFromMem(doc, b.bytes, (HPDF_UINT)b.len);
        SDL_free(b.bytes);
        HPDF_Page_SetWidth(p, pt_w);
        HPDF_Page_SetHeight(p, pt_h);
        if (!im || HPDF_Page_DrawImage(p, im, 0, 0, pt_w, pt_h) != HPDF_OK) ok = 0;
    }
    if (doc && job->spec.title) HPDF_SetInfoAttr(doc, HPDF_INFO_TITLE, job->spec.title);
    if (doc && ok) ok = HPDF_SaveToFile(doc, job->path) == HPDF_OK;
    if (doc) HPDF_Free(doc);
#if defined(__APPLE__)
    if (doc && ok && job->to_printer) ok = reaktor_print_macos(job->path);
#elif !defined(_WIN32)
    if (doc && ok && job->to_printer) {
        char *url = NULL;

        SDL_asprintf(&url, "file://%s", job->path);
        ok = url && SDL_OpenURL(url);
        SDL_free(url);
    }
#endif
    for (i = 0; i < REAKTOR_PRINT_PAGES; i++) SDL_DestroySurface(page[i]);
    if (job->spec.done) job->spec.done(app, ok, job->spec.user);
    SDL_free(job->path);
    SDL_free(job);
}

static int
print_queue(App *app, const char *path, const reaktor_print_spec *s, int to_printer)
{
    print_job *job;

    if (!app || !s || !s->page || s->pages <= 0 || (!path && !to_printer)) return 0;
    if (!(job = (print_job *)SDL_calloc(1, sizeof *job))) return 0;
    job->spec = *s;
    if (job->spec.w <= 0.0f) job->spec.w = 595.0f;
    if (job->spec.h <= 0.0f) job->spec.h = 842.0f;
    if (job->spec.dpi <= 0) job->spec.dpi = 200;
    job->to_printer = to_printer;
    if (path) {
        job->path = SDL_strdup(path);
    } else {
        char *dir = SDL_GetPrefPath(NULL, reaktor_app_ident());

        if (dir) SDL_asprintf(&job->path, "%sprint.pdf", dir);
        SDL_free(dir);
    }
    if (!job->path || !reaktor_timer(app, 1, 0, print_run, job)) {
        SDL_free(job->path);
        SDL_free(job);
        return 0;
    }
    return 1;
}

int
reaktor_print(App *app, const reaktor_print_spec *s)
{
    return print_queue(app, NULL, s, 1);
}

int
reaktor_print_pdf(App *app, const char *path, const reaktor_print_spec *s)
{
    return print_queue(app, path, s, 0);
}
