#include "locale.h"

#include <stdio.h>
#include <time.h>

void        reaktor_locale_start(const char *lang)  { (void)lang; }
int         reaktor_locale_count(void)              { return 0; }
const char *reaktor_locale_code(int i)              { (void)i; return NULL; }
const char *reaktor_locale_name(int i)              { (void)i; return NULL; }
int         reaktor_locale_set(const char *c)       { (void)c; return 0; }
int         reaktor_locale_system(void)             { return 0; }
const char *reaktor_locale_current(void)            { return ""; }
const char *reaktor_tr(const char *key)             { return key; }

const char *
reaktor_trn(const char *key, unsigned long n, char *buf, size_t cap)
{
    (void)n; (void)buf; (void)cap;
    return key;
}

unsigned *
reaktor_locale_glyphs(const unsigned char *ttf)
{
    (void)ttf;
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

/* English names and numbers: the letters reaktor_format_pattern knows. */
const char *
reaktor_format_pattern(char *buf, size_t cap, const struct tm *t, const char *pattern)
{
    static const char *const months[12] = {
        "January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December"
    };
    size_t n = 0;

    if (!buf || !cap) return buf;
    buf[0] = '\0';
    if (!t || !pattern) return buf;
    for (; *pattern && n + 1 < cap; ) {
        char c = *pattern;
        int  run = 1, w;

        while (pattern[run] == c) run++;
        switch (c) {
        case 'd': w = snprintf(buf + n, cap - n, run > 1 ? "%02d" : "%d", t->tm_mday); break;
        case 'L':
        case 'M': w = run >= 3 ? snprintf(buf + n, cap - n, run == 3 ? "%.3s" : "%s",
                                          months[t->tm_mon % 12])
                               : snprintf(buf + n, cap - n, run > 1 ? "%02d" : "%d",
                                          t->tm_mon + 1);
                  break;
        case 'y': w = snprintf(buf + n, cap - n, "%04d", t->tm_year + 1900); break;
        case 'H': w = snprintf(buf + n, cap - n, run > 1 ? "%02d" : "%d", t->tm_hour); break;
        case 'h': w = snprintf(buf + n, cap - n, run > 1 ? "%02d" : "%d",
                               t->tm_hour % 12 ? t->tm_hour % 12 : 12);
                  break;
        case 'm': w = snprintf(buf + n, cap - n, "%02d", t->tm_min); break;
        case 'a': w = snprintf(buf + n, cap - n, "%s", t->tm_hour >= 12 ? "PM" : "AM"); break;
        default:  w = snprintf(buf + n, cap - n, "%.*s", run, pattern); break;
        }
        n += w > 0 ? (size_t)w : 0;
        if (n >= cap) n = cap - 1;
        pattern += run;
    }
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
