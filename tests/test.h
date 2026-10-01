#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>

extern int g_test_failures;
extern int g_test_skips;

#define CHECK(cond) \
    do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
                        g_test_failures++; } } while (0)

#define CHECK_EQ(actual, expected) \
    do { long long a_ = (long long)(actual), e_ = (long long)(expected); \
         if (a_ != e_) { fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (got 0x%llX, want 0x%llX)\n", \
                                 __FILE__, __LINE__, #actual, #expected, a_, e_); g_test_failures++; } } while (0)

#define CHECK_STR(actual, expected) \
    do { const char *a_ = (actual), *e_ = (expected); \
         if (strcmp(a_, e_) != 0) { fprintf(stderr, "%s:%d: CHECK_STR failed: got \"%s\", want \"%s\"\n", \
                                            __FILE__, __LINE__, a_, e_); g_test_failures++; } } while (0)

#define SKIP(reason) \
    do { fprintf(stderr, "  SKIP: %s\n", reason); g_test_skips++; return; } while (0)

#endif
