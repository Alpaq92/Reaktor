#include <string.h>

#include "util.h"

static size_t utf8_valid(const unsigned char *s)
{
    unsigned char c = s[0];

    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return (s[1] & 0xC0) == 0x80 ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        unsigned char lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;

        return s[1] >= lo && s[1] <= hi && (s[2] & 0xC0) == 0x80 ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        unsigned char lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;

        return s[1] >= lo && s[1] <= hi && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80 ? 4 : 0;
    }
    return 0;
}

size_t reaktor_utf8_clean(char *dst, const char *src, size_t cap)
{
    const unsigned char *s = (const unsigned char *)src;
    size_t n = 0;

    if (!cap) return 0;
    while (*s) {
        size_t k = utf8_valid(s);
        const unsigned char *from = k ? s : (const unsigned char *)"\xEF\xBF\xBD";
        size_t len = k ? k : 3;

        if (n + len >= cap) break;
        memcpy(dst + n, from, len);
        n += len;
        s += k ? k : 1;
    }
    dst[n] = 0;
    return n;
}
