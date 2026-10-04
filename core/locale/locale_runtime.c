#include <SDL3/SDL.h>

#include "nk_common.h"
#include "locale_internal.h"

void
reaktor_locale_start(const char *lang)
{
    SDL_Locale **pref;
    int          i, n = 0;

    if (reaktor_locale_init() <= 0) return;
    if (lang) {
        if (reaktor_locale_set(lang)) return;
        SDL_Log("no catalog for --lang %s", lang);
    }
    pref = SDL_GetPreferredLocales(&n);
    for (i = 0; pref && i < n; i++)
        if (pref[i] && reaktor_locale_set(pref[i]->language)) break;
    SDL_free(pref);
    if (!reaktor_locale_current()[0] && !reaktor_locale_set("en"))
        reaktor_locale_set(reaktor_locale_code(0));
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
