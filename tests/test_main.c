#include <stdio.h>
#include <string.h>
#include "test.h"

int g_test_failures = 0;
int g_test_skips = 0;

#define SUITE(fn) void fn(void);
#include "suites.h"
#undef SUITE

static const struct { const char *name; void (*fn)(void); } k_suites[] = {
#define SUITE(fn) { #fn, fn },
#include "suites.h"
#undef SUITE
};

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : NULL;
    int run = 0;
    for (size_t i = 0; i < sizeof k_suites / sizeof k_suites[0]; i++) {
        if (filter && !strstr(k_suites[i].name, filter))
            continue;
        int before = g_test_failures;
        k_suites[i].fn();
        printf("%-28s %s\n", k_suites[i].name, g_test_failures == before ? "ok" : "FAILED");
        run++;
    }
    if (filter && run == 0) {
        fprintf(stderr, "no suite matches \"%s\"\n", filter);
        return 1;
    }
    printf("%d suite(s), %d failure(s), %d skip(s)\n", run, g_test_failures, g_test_skips);
    return g_test_failures ? 1 : 0;
}
