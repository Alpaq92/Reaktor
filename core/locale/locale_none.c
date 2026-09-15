#include "locale.h"

#include <stdio.h>
#include <time.h>

void        reaktor_locale_start(const char *lang)  { (void)lang; }
int         reaktor_locale_count(void)              { return 0; }
const char *reaktor_locale_code(int i)              { (void)i; return NULL; }
const char *reaktor_locale_name(int i)              { (void)i; return NULL; }
int         reaktor_locale_set(const char *c)       { (void)c; return 0; }
const char *reaktor_locale_current(void)            { return ""; }
const char *reaktor_tr(const char *key)             { return key; }

const char *
reaktor_trn(const char *key, unsigned long n, char *buf, size_t cap)
{
    (void)n; (void)buf; (void)cap;
    return key;
}

unsigned *
reaktor_locale_glyphs(const char *font_path)
{
    (void)font_path;
    return NULL;
}

const char *
reaktor_format_number(char *buf, size_t cap, double value, int decimals)
{
    if (buf && cap) snprintf(buf, cap, "%.*f", decimals, value);
    return buf;
}

const char *
reaktor_format_money(char *buf, size_t cap, double amount, const char *currency)
{
    if (buf && cap)
        snprintf(buf, cap, "%s %.2f", currency ? currency : "", amount);
    return buf;
}

const char *
reaktor_format_date(char *buf, size_t cap, const struct tm *t, int style)
{
    if (!buf || !cap) return buf;
    buf[0] = '\0';
    if (!t) return buf;
    if (style == REAKTOR_TIME_SHORT)
        snprintf(buf, cap, "%02d:%02d", t->tm_hour, t->tm_min);
    else
        snprintf(buf, cap, "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1,
                 t->tm_mday);
    return buf;
}
