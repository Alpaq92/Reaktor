#ifndef REAKTOR_TEST_H
#define REAKTOR_TEST_H

#include <stdio.h>

static int failures;

static void
test_ok(const char *what, int cond)
{
    if (!cond) failures++;
    printf("  %-56s %s\n", what, cond ? "ok" : "FAILED");
}

static void
test_eq(const char *what, long got, long want)
{
    if (got == want) {
        printf("  %-56s ok\n", what);
        return;
    }
    failures++;
    printf("  %-56s got %ld, wanted %ld   FAILED\n", what, got, want);
}

static int
test_done(const char *name)
{
    if (failures) {
        printf("\n%d check%s failed\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("\n%s: all checks passed\n", name);
    return 0;
}

#endif
