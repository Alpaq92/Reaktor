#ifndef REAKTOR_LOCALE_H
#define REAKTOR_LOCALE_H

#include <stddef.h>

void        reaktor_locale_start(const char *lang);
int         reaktor_locale_count(void);
const char *reaktor_locale_code(int index);
const char *reaktor_locale_name(int index);
int         reaktor_locale_set(const char *code);
const char *reaktor_locale_current(void);

const char *reaktor_tr(const char *key);
const char *reaktor_trn(const char *key, unsigned long n, char *buf, size_t cap);

/* The caller SDL_frees them. */
unsigned *reaktor_locale_glyphs(const char *font_path);

const char *reaktor_format_number(char *buf, size_t cap, double value,
                                  int decimals);
const char *reaktor_format_money(char *buf, size_t cap, double amount,
                                 const char *currency);

enum {
    REAKTOR_DATE_SHORT,
    REAKTOR_DATE_LONG,
    REAKTOR_DATE_FULL,
    REAKTOR_TIME_SHORT
};

struct tm;
const char *reaktor_format_date(char *buf, size_t cap, const struct tm *t,
                                int style);

#endif
