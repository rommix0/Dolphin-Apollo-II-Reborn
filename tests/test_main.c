#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"

int g_test_failures = 0;
int g_test_skips = 0;
bool g_test_rom_asked = false;

#define SUITE(fn) void fn(void);
#define SUITE_ROM(fn) void fn(void);
#include "suites.h"
#undef SUITE
#undef SUITE_ROM

static const struct { const char *name; void (*fn)(void); test_mark mark; } k_suites[] = {
#define SUITE(fn) { #fn, fn, TEST_ROM_FREE },
#define SUITE_ROM(fn) { #fn, fn, TEST_NEEDS_ROM },
#include "suites.h"
#undef SUITE
#undef SUITE_ROM
};

const char *test_verdict(const char *name, test_mark mark, bool asked_rom, int skips, bool no_rom)
{
    if (mark == TEST_ROM_FREE && asked_rom)
        return "marked ROM-free but asked for the ROM";
    if (mark == TEST_NEEDS_ROM && !asked_rom)
        return "marked as needing the ROM but never asked for it";
    if (no_rom && mark == TEST_ROM_FREE && skips > 0 && strcmp(name, "suite_audio") != 0)
        return "marked ROM-free but skipped with APOLLO2_NO_ROM set";
    return NULL;
}

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : NULL;
    const char *no_rom_env = getenv("APOLLO2_NO_ROM");
    bool no_rom = no_rom_env && *no_rom_env;
    int run = 0, rom_free_full = 0, rom_asked = 0;
    for (size_t i = 0; i < sizeof k_suites / sizeof k_suites[0]; i++) {
        if (filter && !strstr(k_suites[i].name, filter))
            continue;
        int before = g_test_failures, skips_before = g_test_skips;
        g_test_rom_asked = false;
        k_suites[i].fn();
        int skips = g_test_skips - skips_before;
        const char *wrong = test_verdict(k_suites[i].name, k_suites[i].mark, g_test_rom_asked, skips, no_rom);
        if (wrong) {
            fprintf(stderr, "  %s: %s\n", k_suites[i].name, wrong);
            g_test_failures++;
        }
        printf("%-28s %s\n", k_suites[i].name, g_test_failures == before ? "ok" : "FAILED");
        run++;
        rom_asked += g_test_rom_asked;
        rom_free_full += k_suites[i].mark == TEST_ROM_FREE && !g_test_rom_asked && skips == 0;
    }
    if (filter && run == 0) {
        fprintf(stderr, "no suite matches \"%s\"\n", filter);
        return 1;
    }
    printf("%d suite(s), %d failure(s), %d skip(s)\n", run, g_test_failures, g_test_skips);
    printf("ROM-free: %d suite(s) ran in full; ROM: %d suite(s) asked for the ROM\n", rom_free_full, rom_asked);
    return g_test_failures ? 1 : 0;
}
