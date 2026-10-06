#include <SDL3/SDL.h>

#include "nk_common.h"
#include "locale_internal.h"

void
reaktor_locale_start(const char *lang)
{
    if (reaktor_locale_init() <= 0) return;
    if (lang) {
        if (reaktor_locale_set(lang)) return;
        SDL_Log("no catalog for --lang %s", lang);
    }
    reaktor_locale_set(NULL);
}

const char *
reaktor_locale_system_code(void)
{
    SDL_Locale **pref;
    int          i, at = -1, n = 0;

    pref = SDL_GetPreferredLocales(&n);
    for (i = 0; pref && i < n && at < 0; i++)
        if (pref[i]) at = reaktor_locale_find(pref[i]->language);
    SDL_free(pref);
    if (at < 0) at = reaktor_locale_find("en");
    return reaktor_locale_code(at < 0 ? 0 : at);
}

unsigned *
reaktor_locale_glyphs(const unsigned char *ttf)
{
    unsigned *cp, *ranges = NULL;
    int       n = reaktor_locale_codepoints(NULL, 0);

    if (n <= 0 || !ttf) return NULL;

    cp = SDL_malloc((size_t)n * sizeof *cp);
    if (cp) {
        reaktor_locale_codepoints(cp, n);
        ranges = reaktor_font_ranges(ttf, cp, n);
    }
    SDL_free(cp);
    return ranges;
}
