#include <stdio.h>
#include <string.h>
#include <time.h>

#include "locale_internal.h"

#define CATALOG(code, text) \
    { code, (const unsigned char *)(text), sizeof(text) - 1 }

/* On purpose: out of order, and pl malformed in each way the loader allows. */
const struct reaktor_locale_catalog reaktor_locale_catalogs[] = {
    CATALOG("pl",
        "\xEF\xBB\xBF" "language = Polski\r\n"
        "plural = n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2\r\n"
        "hello = Cze\xC5\x9B\xC4\x87\r\n"
        "files = %d plik | %d pliki | %d plik\xC3\xB3w\r\n"
        "Use #tags = U\xC5\xBCyj #tag\xC3\xB3w\r\n"
        "Price = total = Cena = suma\r\n"
        "[Draft] Save   =   [Szkic] Zapisz  \r\n"
        "story = Pierwszy\\nDrugi \\\\ trzeci\r\n"
        "hello = the second one loses\r\n"
        "a line with no separator\r\n"
        "number.decimal = ,\r\n"
        "number.group = \xC2\xA0\r\n"
        "number.group.min = 2\r\n"
        "money = {n}\xC2\xA0{s}\r\n"
        "money.PLN = z\xC5\x82\r\n"
        "money.JPY.digits = 0\r\n"
        "date.full = EEEE, d MMMM y\r\n"
        "time.short = HH:mm\r\n"
        "date.months = stycznia | lutego | marca | kwietnia | maja | czerwca | "
        "lipca | sierpnia | wrze\xC5\x9Bnia | pa\xC5\xBA" "dziernika | listopada | "
        "grudnia\r\n"
        "date.days = niedziela | poniedzia\xC5\x82" "ek | wtorek | \xC5\x9Broda | "
        "czwartek | pi\xC4\x85tek | sobota\r\n"
        "matches = {n}\xC2\xA0zapa\xC5\x82ka | {n}\xC2\xA0zapa\xC5\x82ki | "
        "{n}\xC2\xA0zapa\xC5\x82" "ek\r\n"),
    CATALOG("en",
        "language = English\n"
        "plural = n != 1\n"
        "hello = Hello\n"
        "files = %d file | %d files\n"),
    CATALOG("ja",
        "language = \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\n"
        "plural = 0\n"
        "hello = \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\n"
        "files = %d \xE5\x80\x8B\xE3\x81\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\n"
        "money.JPY = \xEF\xBF\xA5\n"
        "money.JPY.digits = 0\n"
        "date.short = ''yy\n"
        "date.long = y\xE5\xB9\xB4M\xE6\x9C\x88" "d\xE6\x97\xA5\n"
        "time.short = a h'\xE6\x99\x82'mm'\xE5\x88\x86'\n"
        "time.ampm = \xE5\x8D\x88\xE5\x89\x8D | \xE5\x8D\x88\xE5\xBE\x8C\n"),
};
const int reaktor_locale_catalog_count =
    (int)(sizeof reaktor_locale_catalogs / sizeof reaktor_locale_catalogs[0]);

static int failures;

static void
check(const char *what, long got, long want)
{
    if (got == want) return;
    printf("FAIL %s: got %ld, want %ld\n", what, got, want);
    failures++;
}

static void
check_str(const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) return;
    printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got ? got : "(null)",
           want ? want : "(null)");
    failures++;
}

static void
plural(const char *expr, unsigned long n, long want)
{
    char what[160];

    snprintf(what, sizeof what, "plural \"%.100s\" for n=%lu", expr, n);
    check(what, reaktor_plural_eval(expr, n), want);
}

static void
plurals(void)
{
    const char *pl = "n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2";
    const char *ru = "n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2";
    const char *ar = "n==0 ? 0 : n==1 ? 1 : n==2 ? 2 : n%100>=3 && n%100<=10 ? 3 : n%100>=11 ? 4 : 5";
    char deep[4096];
    int  i;

    plural("n != 1", 0, 1);
    plural("n != 1", 1, 0);
    plural("n != 1", 2, 1);

    plural(pl, 0, 2);   plural(pl, 1, 0);   plural(pl, 2, 1);
    plural(pl, 4, 1);   plural(pl, 5, 2);   plural(pl, 12, 2);
    plural(pl, 14, 2);  plural(pl, 21, 2);  plural(pl, 22, 1);
    plural(pl, 25, 2);  plural(pl, 101, 2); plural(pl, 102, 1);
    plural(pl, 112, 2); plural(pl, 124, 1);

    plural(ru, 1, 0);   plural(ru, 11, 2);  plural(ru, 21, 0);
    plural(ru, 2, 1);   plural(ru, 5, 2);   plural(ru, 111, 2);

    plural(ar, 0, 0);   plural(ar, 1, 1);   plural(ar, 2, 2);
    plural(ar, 3, 3);   plural(ar, 10, 3);  plural(ar, 11, 4);
    plural(ar, 99, 4);  plural(ar, 100, 5); plural(ar, 102, 5);

    plural("0", 7, 0);
    plural("!n", 0, 1);
    plural("!n", 3, 0);
    plural("n+1-2*3/3%4 == n-1", 5, 1);
    plural("nplurals=2; plural=n != 1;", 1, 0);
    plural("nplurals=2; plural=n != 1;", 2, 1);
    plural("nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : 2);", 11, 2);

    plural("", 1, -1);
    plural("n ==", 1, -1);
    plural("(n", 1, -1);
    plural("n / 0", 1, -1);
    plural("n % (1-1)", 1, -1);
    plural("banana", 1, -1);
    plural("n n", 1, -1);
    plural("1 ? 2", 1, -1);
    check("plural of NULL", reaktor_plural_eval(NULL, 1), -1);

    for (i = 0; i < 2000; i++) deep[i] = '(';
    deep[i] = 'n';
    for (i = 0; i < 2000; i++) deep[2001 + i] = ')';
    deep[4001] = '\0';
    plural(deep, 1, -1);
    for (i = 0; i < 4000; i++) deep[i] = '!';
    deep[4000] = 'n';
    deep[4001] = '\0';
    plural(deep, 1, -1);
}

static void
catalogs(void)
{
    char     buf[64];
    unsigned cps[64];
    int      i, n, sorted = 1, dup = 0, has_s = 0, has_ko = 0, has_ni = 0,
             has_o = 0;

    check_str("no current catalog answers with the key", reaktor_tr("hello"),
              "hello");
    check("catalogs loaded", reaktor_locale_init(), 3);
    check("count", reaktor_locale_count(), 3);
    check_str("sorted by code: 0", reaktor_locale_code(0), "en");
    check_str("sorted by code: 1", reaktor_locale_code(1), "ja");
    check_str("sorted by code: 2", reaktor_locale_code(2), "pl");
    check_str("name from `language`", reaktor_locale_name(2), "Polski");
    check_str("a name in its own script", reaktor_locale_name(1),
              "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    check("find pl", reaktor_locale_find("pl"), 2);
    check("find an unknown code", reaktor_locale_find("xx"), -1);
    check("an unknown code is refused", reaktor_locale_set("xx"), 0);
    check_str("and leaves no current catalog", reaktor_locale_current(), "");

    check("set pl", reaktor_locale_set("pl"), 1);
    check_str("current", reaktor_locale_current(), "pl");
    check_str("byte-order mark and CRLF", reaktor_tr("language"), "Polski");
    check_str("the first of a repeated key", reaktor_tr("hello"),
              "Cze\xC5\x9B\xC4\x87");
    check_str("a key with a hash", reaktor_tr("Use #tags"),
              "U\xC5\xBCyj #tag\xC3\xB3w");
    check_str("split at the first \" = \"", reaktor_tr("Price"),
              "total = Cena = suma");
    check_str("a key with brackets, spaces trimmed",
              reaktor_tr("[Draft] Save"), "[Szkic] Zapisz");
    check_str("escapes", reaktor_tr("story"), "Pierwszy\nDrugi \\ trzeci");
    check_str("a missing key answers with itself", reaktor_tr("missing"),
              "missing");
    check_str("a non-entry line is not a key",
              reaktor_tr("a line with no separator"),
              "a line with no separator");

    check_str("pl 1 plik", reaktor_trn("files", 1, buf, sizeof buf), "%d plik");
    check_str("pl 2 pliki", reaktor_trn("files", 2, buf, sizeof buf), "%d pliki");
    check_str("pl 5 plikow", reaktor_trn("files", 5, buf, sizeof buf),
              "%d plik\xC3\xB3w");
    check_str("pl 12 plikow", reaktor_trn("files", 12, buf, sizeof buf),
              "%d plik\xC3\xB3w");
    check_str("pl 22 pliki", reaktor_trn("files", 22, buf, sizeof buf),
              "%d pliki");
    check_str("pl 104 pliki", reaktor_trn("files", 104, buf, sizeof buf),
              "%d pliki");
    check_str("a form truncated to the buffer",
              reaktor_trn("files", 1, buf, 4), "%d ");
    check_str("plural of a missing key answers with it",
              reaktor_trn("nope", 3, buf, sizeof buf), "nope");

    reaktor_locale_set("en");
    check_str("en 1 file", reaktor_trn("files", 1, buf, sizeof buf), "%d file");
    check_str("en 0 files", reaktor_trn("files", 0, buf, sizeof buf), "%d files");

    reaktor_locale_set("ja");
    check_str("one form serves every n",
              reaktor_trn("files", 5, buf, sizeof buf),
              "%d \xE5\x80\x8B\xE3\x81\xAE\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB");

    n = reaktor_locale_codepoints(NULL, 0);
    check("codepoints counted without a buffer",
          reaktor_locale_codepoints(cps, (int)(sizeof cps / sizeof cps[0])), n);
    for (i = 0; i < n && i < 64; i++) {
        if (i && cps[i] <= cps[i - 1]) { sorted = 0; dup += cps[i] == cps[i - 1]; }
        has_s  |= cps[i] == 0x015B;
        has_ko |= cps[i] == 0x3053;
        has_ni |= cps[i] == 0x65E5;
        has_o  |= cps[i] == 0x00F3;
    }
    check("codepoints ascending", sorted, 1);
    check("codepoints unique", dup, 0);
    check("codepoints include s-acute", has_s, 1);
    check("codepoints include hiragana ko", has_ko, 1);
    check("codepoints include the kanji in the language name", has_ni, 1);
    check("codepoints leave out Latin-1", has_o, 0);

    check("init again reloads cleanly", reaktor_locale_init(), 3);
    check_str("and forgets the current catalog", reaktor_locale_current(), "");
}

static void
formats(void)
{
    struct tm eve, dawn;
    char      buf[96];

    memset(&eve, 0, sizeof eve);
    eve.tm_year = 1845 - 1900; eve.tm_mon = 11; eve.tm_mday = 31;
    eve.tm_hour = 21; eve.tm_min = 30;
    dawn = eve;
    dawn.tm_hour = 0; dawn.tm_min = 5;

    reaktor_locale_init();

    reaktor_locale_set("en");
    check_str("en number", reaktor_format_number(buf, sizeof buf, 1234567.891, 2),
              "1,234,567.89");
    check_str("en number below grouping",
              reaktor_format_number(buf, sizeof buf, 999, 0), "999");
    check_str("en number grouped at four digits",
              reaktor_format_number(buf, sizeof buf, 1000, 0), "1,000");
    check_str("a half rounds to even, as printf and CLDR both round it",
              reaktor_format_number(buf, sizeof buf, 2.5, 0), "2");
    check_str("no sign on a negative that rounds to zero",
              reaktor_format_number(buf, sizeof buf, -0.004, 2), "0.00");
    check_str("en negative", reaktor_format_number(buf, sizeof buf, -1234.5, 1),
              "-1,234.5");
    check_str("a symbol that is a code keeps off the amount",
              reaktor_format_money(buf, sizeof buf, 1234.5, "PLN"),
              "PLN\xC2\xA0" "1,234.50");
    check_str("en negative money",
              reaktor_format_money(buf, sizeof buf, -3, "USD"),
              "-USD\xC2\xA0" "3.00");
    check_str("en short date",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_DATE_SHORT),
              "12/31/45");
    check_str("en long date",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_DATE_LONG),
              "December 31, 1845");
    check_str("en full date, with the weekday worked out",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_DATE_FULL),
              "Wednesday, December 31, 1845");
    check_str("en time", reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_TIME_SHORT),
              "9:30 PM");
    check_str("en time past midnight",
              reaktor_format_date(buf, sizeof buf, &dawn, REAKTOR_TIME_SHORT),
              "12:05 AM");

    reaktor_locale_set("pl");
    check_str("pl number", reaktor_format_number(buf, sizeof buf, 1234567.891, 2),
              "1\xC2\xA0" "234\xC2\xA0" "567,89");
    check_str("pl leaves four digits ungrouped",
              reaktor_format_number(buf, sizeof buf, 1234, 0), "1234");
    check_str("pl groups five", reaktor_format_number(buf, sizeof buf, 12345, 0),
              "12\xC2\xA0" "345");
    check_str("pl money", reaktor_format_money(buf, sizeof buf, 1234.5, "PLN"),
              "1234,50\xC2\xA0z\xC5\x82");
    check_str("pl negative money",
              reaktor_format_money(buf, sizeof buf, -1234.5, "PLN"),
              "-1234,50\xC2\xA0z\xC5\x82");
    check_str("a currency with no decimals",
              reaktor_format_money(buf, sizeof buf, 123456, "JPY"),
              "123\xC2\xA0" "456\xC2\xA0JPY");
    check_str("pl full date",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_DATE_FULL),
              "\xC5\x9Broda, 31 grudnia 1845");
    check_str("pl time", reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_TIME_SHORT),
              "21:30");
    check_str("{n} in a plural form, formatted",
              reaktor_trn("matches", 12345, buf, sizeof buf),
              "12\xC2\xA0" "345\xC2\xA0zapa\xC5\x82" "ek");
    check_str("{n} with the few form",
              reaktor_trn("matches", 22, buf, sizeof buf),
              "22\xC2\xA0zapa\xC5\x82ki");
    check_str("a form cut between characters, not through one",
              reaktor_trn("matches", 1, buf, 9), "1\xC2\xA0zapa");

    reaktor_locale_set("ja");
    check_str("ja money", reaktor_format_money(buf, sizeof buf, 123456, "JPY"),
              "\xEF\xBF\xA5" "123,456");
    check_str("ja long date",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_DATE_LONG),
              "1845\xE5\xB9\xB4" "12\xE6\x9C\x88" "31\xE6\x97\xA5");
    check_str("ja time, with quotes and the day half",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_TIME_SHORT),
              "\xE5\x8D\x88\xE5\xBE\x8C 9\xE6\x99\x82" "30\xE5\x88\x86");
    check_str("a doubled quote is one quote",
              reaktor_format_date(buf, sizeof buf, &eve, REAKTOR_DATE_SHORT),
              "'45");
    check_str("a date cut between characters",
              reaktor_format_date(buf, 7, &eve, REAKTOR_DATE_LONG), "1845");
}

int
main(void)
{
    plurals();
    catalogs();
    formats();

    if (failures) {
        printf("\n%d check%s failed\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("locale: all checks passed\n");
    return 0;
}
