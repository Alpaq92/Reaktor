#include "locale_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NBSP "\xC2\xA0"

typedef struct out {
    char  *buf;
    size_t cap, len;
    int    full;
} out;

static out
start(char *buf, size_t cap)
{
    out o = { buf, cap, 0, 0 };

    buf[0] = '\0';
    return o;
}

static void
put(out *o, const char *s, size_t n)
{
    size_t room = o->cap - 1 - o->len;

    if (o->full) return;
    if (n > room) {
        n = room;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
        o->full = 1;
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = 0;
}

static void
puts_(out *o, const char *s)
{
    put(o, s, strlen(s));
}

static void
putf(out *o, const char *fmt, int v)
{
    char tmp[16];
    int  n = snprintf(tmp, sizeof tmp, fmt, v);

    if (n > 0) put(o, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

/* reaktor_tr answers key itself, the same pointer, when it has no value. */
static const char *
conv(const char *key, const char *fallback)
{
    const char *v = reaktor_tr(key);

    return v == key ? fallback : v;
}

static const char *
form(const char *list, long i, size_t *len)
{
    const char *end;

    for (; i > 0; i--) {
        const char *bar = strstr(list, " | ");
        if (!bar) break;
        list = bar + 3;
    }
    end = strstr(list, " | ");
    *len = end ? (size_t)(end - list) : strlen(list);
    return list;
}

static void
number(out *o, double value, int decimals)
{
    const char *dec   = conv("number.decimal", ".");
    const char *group = conv("number.group", ",");
    int         min   = atoi(conv("number.group.min", "1"));
    char        digits[64];
    int         n, i, whole;

    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    if (!isfinite(value)) {
        puts_(o, value != value ? "NaN" : value < 0 ? "-inf" : "inf");
        return;
    }
    n = snprintf(digits, sizeof digits, "%.*f", decimals, fabs(value));
    if (n <= 0 || n >= (int)sizeof digits) {
        puts_(o, "?");
        return;
    }
    for (whole = 0; digits[whole] >= '0' && digits[whole] <= '9'; whole++) ;
    if (value < 0) {
        for (i = 0; digits[i] && (digits[i] < '1' || digits[i] > '9'); i++) ;
        if (digits[i]) put(o, "-", 1);
    }

    if (min < 1) min = 1;
    for (i = 0; i < whole; i++) {
        put(o, digits + i, 1);
        if (whole >= 3 + min && i < whole - 1 && (whole - 1 - i) % 3 == 0)
            puts_(o, group);
    }
    if (decimals > 0 && digits[whole]) {
        puts_(o, dec);
        puts_(o, digits + whole + 1);
    }
}

const char *
reaktor_format_number(char *buf, size_t cap, double value, int decimals)
{
    out o;

    if (!buf || cap == 0) return buf;
    o = start(buf, cap);
    number(&o, value, decimals);
    return buf;
}

const char *
reaktor_trn(const char *key, unsigned long n, char *buf, size_t cap)
{
    const char *v = reaktor_tr(key), *rule, *end;
    char        num[32];
    size_t      len;
    long        i;
    out         o;

    if (!key || !buf || cap == 0 || v == key) return key;
    rule = conv("plural", NULL);
    i = rule ? reaktor_plural_eval(rule, n) : -1;
    if (i < 0) i = n != 1;
    v   = form(v, i, &len);
    end = v + len;
    reaktor_format_number(num, sizeof num, (double)n, 0);

    o = start(buf, cap);
    while (v < end) {
        if (end - v >= 3 && !strncmp(v, "{n}", 3)) {
            puts_(&o, num);
            v += 3;
        } else {
            const char *brace = memchr(v + 1, '{', (size_t)(end - v - 1));
            size_t      k = brace ? (size_t)(brace - v) : (size_t)(end - v);

            put(&o, v, k);
            v += k;
        }
    }
    return buf;
}

static int
is_letter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

const char *
reaktor_format_money(char *buf, size_t cap, double amount, const char *currency)
{
    const char *pattern, *symbol, *p;
    char        key[40], amt[64];
    int         digits;
    size_t      slen;
    out         o, a;

    if (!buf || cap == 0) return buf;
    o = start(buf, cap);
    if (!currency) currency = "";

    snprintf(key, sizeof key, "money.%s", currency);
    symbol = conv(key, currency);
    snprintf(key, sizeof key, "money.%s.digits", currency);
    digits = atoi(conv(key, "2"));
    pattern = conv("money", "{s}{n}");
    slen = strlen(symbol);

    a = start(amt, sizeof amt);
    number(&a, amount, digits);
    if (amt[0] == '-') put(&o, "-", 1);

    for (p = pattern; *p; ) {
        if (!strncmp(p, "{n}", 3)) {
            puts_(&o, amt[0] == '-' ? amt + 1 : amt);
            if (!strncmp(p + 3, "{s}", 3) && slen &&
                is_letter((unsigned char)symbol[0]))
                puts_(&o, NBSP);
            p += 3;
        } else if (!strncmp(p, "{s}", 3)) {
            put(&o, symbol, slen);
            if (!strncmp(p + 3, "{n}", 3) && slen &&
                is_letter((unsigned char)symbol[slen - 1]))
                puts_(&o, NBSP);
            p += 3;
        } else {
            const char *brace = strchr(p + 1, '{');
            size_t      n = brace ? (size_t)(brace - p) : strlen(p);

            put(&o, p, n);
            p += n;
        }
    }
    return buf;
}

static int
weekday(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

    if (m < 3) y--;
    return ((y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7 + 7) % 7;
}

static const char MONTHS[] =
    "January | February | March | April | May | June | July | August | "
    "September | October | November | December";
static const char MONTHS_SHORT[] =
    "Jan | Feb | Mar | Apr | May | Jun | Jul | Aug | Sep | Oct | Nov | Dec";
static const char DAYS[] =
    "Sunday | Monday | Tuesday | Wednesday | Thursday | Friday | Saturday";
static const char DAYS_SHORT[] = "Sun | Mon | Tue | Wed | Thu | Fri | Sat";

static void
name(out *o, const char *key, const char *english, int i)
{
    size_t      len;
    const char *f = form(conv(key, english), i, &len);

    put(o, f, len);
}

const char *
reaktor_format_date(char *buf, size_t cap, const struct tm *t, int style)
{
    static const char *const keys[] = {
        "date.short", "date.long", "date.full", "time.short"
    };
    static const char *const english[] = {
        "M/d/yy", "MMMM d, y", "EEEE, MMMM d, y", "h:mm a"
    };
    const char *p;
    int         year, month, day, hour, minute, wd;
    out         o;

    if (!buf || cap == 0) return buf;
    o = start(buf, cap);
    if (!t || style < REAKTOR_DATE_SHORT || style > REAKTOR_TIME_SHORT)
        return buf;

    year   = t->tm_year + 1900;
    month  = t->tm_mon + 1;
    day    = t->tm_mday;
    hour   = t->tm_hour;
    minute = t->tm_min;
    if (month < 1 || month > 12) return buf;
    wd = weekday(year, month, day);

    for (p = conv(keys[style], english[style]); *p; ) {
        char c = *p;
        int  run = 1;

        if (c == '\'') {
            const char *close;

            if (p[1] == '\'') { put(&o, "'", 1); p += 2; continue; }
            close = strchr(p + 1, '\'');
            run = close ? (int)(close - p - 1) : (int)strlen(p + 1);
            put(&o, p + 1, (size_t)run);
            p += run + 1 + (close != NULL);
            continue;
        }
        if (!is_letter((unsigned char)c)) {
            while (p[run] && p[run] != '\'' && !is_letter((unsigned char)p[run]))
                run++;
            put(&o, p, (size_t)run);
            p += run;
            continue;
        }
        while (p[run] == c) run++;
        switch (c) {
        case 'd': putf(&o, run > 1 ? "%02d" : "%d", day); break;
        case 'M':
            if (run >= 4)      name(&o, "date.months", MONTHS, month - 1);
            else if (run == 3) name(&o, "date.months.short", MONTHS_SHORT,
                                    month - 1);
            else               putf(&o, run > 1 ? "%02d" : "%d", month);
            break;
        case 'y':
            if (run == 2) putf(&o, "%02d", ((year % 100) + 100) % 100);
            else          putf(&o, run > 2 ? "%04d" : "%d", year);
            break;
        case 'E':
            if (run >= 4) name(&o, "date.days", DAYS, wd);
            else          name(&o, "date.days.short", DAYS_SHORT, wd);
            break;
        case 'H': putf(&o, run > 1 ? "%02d" : "%d", hour); break;
        case 'h': putf(&o, run > 1 ? "%02d" : "%d", hour % 12 ? hour % 12 : 12); break;
        case 'm': putf(&o, run > 1 ? "%02d" : "%d", minute); break;
        case 'a': name(&o, "time.ampm", "AM | PM", hour >= 12); break;
        default:  put(&o, p, (size_t)run); break;
        }
        p += run;
    }
    return buf;
}
