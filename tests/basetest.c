#include <string.h>

#include "util.h"
#include "test.h"

#define ok test_ok

int
main(void)
{
    ok("http, https and mailto open",
       reaktor_url_allowed("https://example.com", NULL) &&
       reaktor_url_allowed("http://example.com", NULL) &&
       reaktor_url_allowed("mailto:a@example.com", NULL));
    ok("  in any case", reaktor_url_allowed("HTTPS://example.com", NULL));
    ok("file, a drive path and a share do not",
       !reaktor_url_allowed("file:///C:/Windows/System32/calc.exe", NULL) &&
       !reaktor_url_allowed("C:\\Windows\\System32\\calc.exe", NULL) &&
       !reaktor_url_allowed("\\\\server\\share\\x", NULL));
    ok("nor a registered protocol, a blank start or no scheme at all",
       !reaktor_url_allowed("ms-settings:", NULL) &&
       !reaktor_url_allowed(" https://example.com", NULL) &&
       !reaktor_url_allowed("example.com", NULL) && !reaktor_url_allowed("", NULL) &&
       !reaktor_url_allowed(NULL, NULL));
    ok("a link's own schemes are added, whole names only",
       reaktor_url_allowed("file:///tmp/x", "file,ftp") &&
       reaktor_url_allowed("ftp://x", "file,ftp") &&
       !reaktor_url_allowed("fil:///tmp/x", "file,ftp") &&
       !reaktor_url_allowed("ms-settings:", "file,ftp"));

    {
        static const char *const cases[][2] = {
            { "h\xC3\xA9llo \xE6\x97\xA5 \xF0\x9F\x98\x80", "h\xC3\xA9llo \xE6\x97\xA5 \xF0\x9F\x98\x80" },
            { "a\x80" "b", "a\xEF\xBF\xBD" "b" },
            { "a\xE2\x82", "a\xEF\xBF\xBD\xEF\xBF\xBD" },
            { "\xC0\xAF", "\xEF\xBF\xBD\xEF\xBF\xBD" },
            { "\xED\xA0\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" },
            { "\xF4\x90\x80\x80", "\xEF\xBF\xBD" },
        };
        char out[64];
        int  k, bad = 0;

        for (k = 0; k < (int)(sizeof cases / sizeof cases[0]); k++) {
            reaktor_utf8_clean(out, cases[k][0], sizeof out);
            if (strcmp(out, cases[k][1]) != 0) bad++;
        }
        ok("invalid UTF-8 becomes U+FFFD, valid text is kept", bad == 0);
        reaktor_utf8_clean(out, "ab\xE6\x97\xA5", 4);
        ok("a short buffer cuts at a whole character", strlen(out) == 2);
    }

    return test_done("base");
}
