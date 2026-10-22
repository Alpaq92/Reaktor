#include <SDL3/SDL.h>

#include "util.h"

static int scheme_listed(const char *url, size_t n, const char *list)
{
    while (list && *list) {
        const char *end = SDL_strchr(list, ',');
        size_t      k = end ? (size_t)(end - list) : SDL_strlen(list);

        if (k == n && SDL_strncasecmp(url, list, n) == 0) return 1;
        list = end ? end + 1 : list + k;
    }
    return 0;
}

int reaktor_url_allowed(const char *url, const char *extra)
{
    size_t n;

    if (!url || !SDL_isalpha((unsigned char)url[0])) return 0;
    for (n = 1; url[n] != ':'; n++)
        if (!SDL_isalnum((unsigned char)url[n]) && url[n] != '+' && url[n] != '-' &&
            url[n] != '.')
            return 0;
    return scheme_listed(url, n, "http,https,mailto") || scheme_listed(url, n, extra);
}
