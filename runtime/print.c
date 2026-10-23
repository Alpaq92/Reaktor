#include "internal.h"
#include "reaktor/launch.h"
#include "declare.h"
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

typedef struct buffer {
    unsigned char *bytes;
    size_t         len, cap;
    int            failed;
} buffer;

static void
print_page(App *app, struct nk_context *ctx, int w, int h, void *user)
{
    print_job *job = (print_job *)user;

    job->spec.page(app, ctx, job->index, w, h, job->spec.user);
}

static void
put_bytes(void *closure, void *data, int size)
{
    buffer *b = (buffer *)closure;

    if (b->failed || size <= 0) return;
    if (b->len + (size_t)size > b->cap) {
        size_t         cap  = (b->len + (size_t)size) * 2;
        unsigned char *more = (unsigned char *)SDL_realloc(b->bytes, cap);

        if (!more) {
            b->failed = 1;
            return;
        }
        b->bytes = more;
        b->cap   = cap;
    }
    SDL_memcpy(b->bytes + b->len, data, (size_t)size);
    b->len += (size_t)size;
}

static void
put(buffer *b, const char *fmt, ...)
{
    char    line[256];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = SDL_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof line) b->failed = 1;
    else put_bytes(b, line, n);
}

/* A PDF text string: as it is when it is plain ASCII, else UTF-16BE. */
static void
put_text(buffer *b, const char *s)
{
    size_t      left = SDL_strlen(s);
    const char *p;

    for (p = s; *p; p++)
        if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E ||
            *p == '(' || *p == ')' || *p == '\\')
            break;
    if (!*p) {
        put(b, "(");
        put_bytes(b, (void *)s, (int)left);
        put(b, ")");
        return;
    }
    put(b, "<FEFF");
    while (left) {
        Uint32 c = SDL_StepUTF8(&s, &left);

        if (c > 0xFFFF) put(b, "%04X%04X", 0xD800 + ((c - 0x10000) >> 10), 0xDC00 + (c & 0x3FF));
        else if (c) put(b, "%04X", c);
    }
    put(b, ">");
}

/* Each page a JPEG drawn over the whole of it. */
static int
pdf_write(const char *path, const char *title, SDL_Surface *const *page, int n,
          float pt_w, float pt_h)
{
    buffer       pdf = { NULL, 0, 0, 0 };
    size_t       at[4 + 3 * REAKTOR_PRINT_PAGES], xref, k;
    SDL_Time     now;
    SDL_DateTime t;
    Uint64       id = 0xcbf29ce484222325u;
    int          objects = 3 + 3 * n, i, ok;

    put(&pdf, "%%PDF-1.3\n%%\xE2\xE3\xCF\xD3\n");
    at[1] = pdf.len;
    put(&pdf, "1 0 obj\n<< /Creator (Reaktor)");
    if (title && *title) {
        put(&pdf, " /Title ");
        put_text(&pdf, title);
    }
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, false))
        put(&pdf, " /CreationDate (D:%04d%02d%02d%02d%02d%02dZ)", t.year, t.month, t.day,
            t.hour, t.minute, t.second);
    put(&pdf, " >>\nendobj\n");
    at[2] = pdf.len;
    put(&pdf, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", n);
    for (i = 0; i < n; i++) put(&pdf, " %d 0 R", 4 + 3 * i);
    put(&pdf, " ] >>\nendobj\n");
    at[3] = pdf.len;
    put(&pdf, "3 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    for (i = 0; i < n && !pdf.failed; i++) {
        plutovg_surface_t *s = plutovg_surface_create_for_data(page[i]->pixels, page[i]->w,
                                                               page[i]->h, page[i]->pitch);
        buffer             jpg = { NULL, 0, 0, 0 };
        char               draw[96];
        int                o = 4 + 3 * i, len;

        if (s) plutovg_surface_write_to_jpg_stream(s, put_bytes, &jpg, 90);
        plutovg_surface_destroy(s);
        len = SDL_snprintf(draw, sizeof draw, "q %.4f 0 0 %.4f 0 0 cm /Im Do Q",
                           (double)pt_w, (double)pt_h);
        if (!jpg.len || jpg.failed) pdf.failed = 1;
        at[o] = pdf.len;
        put(&pdf, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.4f %.4f]", o,
            (double)pt_w, (double)pt_h);
        put(&pdf, " /Resources << /XObject << /Im %d 0 R >> >> /Contents %d 0 R >>\nendobj\n",
            o + 1, o + 2);
        at[o + 1] = pdf.len;
        put(&pdf, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d", o + 1,
            page[i]->w, page[i]->h);
        put(&pdf, " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode"
                  " /Length %zu >>\nstream\n", jpg.len);
        put_bytes(&pdf, jpg.bytes, (int)jpg.len);
        put(&pdf, "\nendstream\nendobj\n");
        at[o + 2] = pdf.len;
        put(&pdf, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", o + 2, len, draw);
        SDL_free(jpg.bytes);
    }

    xref = pdf.len;
    put(&pdf, "xref\n0 %d\n0000000000 65535 f \n", objects + 1);
    for (i = 1; i <= objects; i++) put(&pdf, "%010zu 00000 n \n", at[i]);
    for (k = 0; k < pdf.len; k++) id = (id ^ pdf.bytes[k]) * 0x100000001b3u;
    put(&pdf, "trailer\n<< /Size %d /Root 3 0 R /Info 1 0 R", objects + 1);
    put(&pdf, " /ID [<%016" SDL_PRIx64 "> <%016" SDL_PRIx64 ">] >>\n", id, id);
    put(&pdf, "startxref\n%zu\n%%%%EOF\n", xref);
    ok = !pdf.failed && SDL_SaveFile(path, pdf.bytes, pdf.len);
    SDL_free(pdf.bytes);
    return ok;
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
        for (k = 0; k < 3 || (!reaktor_frame_settled(pa) && k < 24); k++)
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
    if (ok) ok = pdf_write(job->path, job->spec.title, page, n, pt_w, pt_h);
#if defined(__APPLE__)
    if (ok && job->to_printer) ok = reaktor_print_macos(job->path);
#elif !defined(_WIN32)
    if (ok && job->to_printer) {
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
