#include "text.h"

void reaktor_text_add_fallback(const char *path) { (void)path; }

void
reaktor_text_attach_font(struct nk_font *font, const char *path)
{
    (void)font; (void)path;
}

void
reaktor_text_prepare(struct nk_context *ctx, struct SDL_Renderer *renderer)
{
    (void)ctx; (void)renderer;
}

int
reaktor_text_break(const char *s, int len, int limit)
{
    (void)len;
    for (; limit > 0; limit--)
        if (s[limit - 1] == ' ') return limit;
    return 0;
}

int
reaktor_text_rtl(const char *s, int len)
{
    (void)s; (void)len;
    return 0;
}

void
reaktor_text_describe(char *buf, int cap)
{
    if (cap > 0) buf[0] = 0;
}
