#ifndef REAKTOR_TEST_H
#define REAKTOR_TEST_H

#include "reaktor/launch.h"

typedef struct reaktor_test reaktor_test;

typedef void (*reaktor_test_script)(reaktor_test *t, void *user);

int reaktor_test_app(int argc, char **argv, const reaktor_launch *launch,
                     reaktor_test_script script, void *user);

int      reaktor_test_find(reaktor_test *t, const char *target);
int      reaktor_test_click(reaktor_test *t, const char *target);
int      reaktor_test_type(reaktor_test *t, const char *target, const char *text);
int      reaktor_test_key(reaktor_test *t, const char *chord);
int      reaktor_test_value(reaktor_test *t, const char *target, char *out, int cap);
unsigned reaktor_test_state(reaktor_test *t, const char *target);
void     reaktor_test_settle(reaktor_test *t);
int      reaktor_test_check(reaktor_test *t, int ok, const char *what);
App     *reaktor_test_app_of(reaktor_test *t);

#endif
