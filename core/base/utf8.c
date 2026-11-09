#include <string.h>
#include <SDL3/SDL.h>

#include "util.h"

size_t reaktor_utf8_clean(char *dst, const char *src, size_t cap)
{
    size_t left = strlen(src), n = 0;
    char   ch[4];

    if (!cap) return 0;
    while (left) {
        size_t k = (size_t)(SDL_UCS4ToUTF8(SDL_StepUTF8(&src, &left), ch) - ch);

        if (n + k >= cap) break;
        memcpy(dst + n, ch, k);
        n += k;
    }
    dst[n] = 0;
    return n;
}

char *reaktor_utf8_clean_dup(const char *src)
{
    size_t cap = strlen(src) * 3 + 1;
    char  *dst = (char *)SDL_malloc(cap);

    if (dst) reaktor_utf8_clean(dst, src, cap);
    return dst;
}
