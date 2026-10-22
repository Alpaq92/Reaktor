#include "util.h"
#include "test.h"

#define ok test_ok

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
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
    return test_done("url");
}
