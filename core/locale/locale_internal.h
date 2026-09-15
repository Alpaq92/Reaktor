#ifndef REAKTOR_LOCALE_INTERNAL_H
#define REAKTOR_LOCALE_INTERNAL_H

#include "locale.h"

int  reaktor_locale_init(void);
int  reaktor_locale_find(const char *code);
int  reaktor_locale_codepoints(unsigned *out, int cap);
long reaktor_plural_eval(const char *expr, unsigned long n);

#ifdef REAKTOR_LOCALE_EMBEDDED
struct reaktor_locale_catalog {
    const char          *code;
    const unsigned char *data;
    size_t               size;
};
extern const struct reaktor_locale_catalog reaktor_locale_catalogs[];
extern const int reaktor_locale_catalog_count;
#endif

#endif
