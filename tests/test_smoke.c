#include "test.h"

#define FREE_ASKED "marked ROM-free but asked for the ROM"
#define ROM_UNASKED "marked as needing the ROM but never asked for it"
#define FREE_SKIPPED "marked ROM-free but skipped with APOLLO2_NO_ROM set"

/* The runner's verdict (M5b spec 4.3; the M5b plan's Review Focus 5) over every combination of mark, ROM request,
   skips and mode, for an ordinary suite and for suite_audio, the one ROM-free suite allowed a skip. */
static void test_verdict_table(void)
{
    static const struct {
        const char *name;
        test_mark mark;
        bool asked;
        int skips;
        bool no_rom;
        const char *want;
    } k_rows[] = {
        { "suite_x", TEST_ROM_FREE, false, 0, false, NULL },
        { "suite_x", TEST_ROM_FREE, false, 0, true, NULL },
        { "suite_x", TEST_ROM_FREE, false, 1, false, NULL },
        { "suite_x", TEST_ROM_FREE, false, 1, true, FREE_SKIPPED },
        { "suite_x", TEST_ROM_FREE, true, 0, false, FREE_ASKED },
        { "suite_x", TEST_ROM_FREE, true, 0, true, FREE_ASKED },
        { "suite_x", TEST_ROM_FREE, true, 1, false, FREE_ASKED },
        { "suite_x", TEST_ROM_FREE, true, 1, true, FREE_ASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 0, false, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 0, true, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 1, false, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 1, true, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, true, 0, false, NULL },
        { "suite_x", TEST_NEEDS_ROM, true, 0, true, NULL },
        { "suite_x", TEST_NEEDS_ROM, true, 1, false, NULL },
        { "suite_x", TEST_NEEDS_ROM, true, 1, true, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 0, false, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 0, true, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 1, false, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 1, true, NULL },
        { "suite_audio", TEST_ROM_FREE, true, 0, false, FREE_ASKED },
        { "suite_audio", TEST_ROM_FREE, true, 0, true, FREE_ASKED },
        { "suite_audio", TEST_ROM_FREE, true, 1, false, FREE_ASKED },
        { "suite_audio", TEST_ROM_FREE, true, 1, true, FREE_ASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 0, false, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 0, true, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 1, false, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 1, true, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, true, 0, false, NULL },
        { "suite_audio", TEST_NEEDS_ROM, true, 0, true, NULL },
        { "suite_audio", TEST_NEEDS_ROM, true, 1, false, NULL },
        { "suite_audio", TEST_NEEDS_ROM, true, 1, true, NULL },
    };
    for (size_t i = 0; i < sizeof k_rows / sizeof k_rows[0]; i++) {
        const char *got = test_verdict(k_rows[i].name, k_rows[i].mark, k_rows[i].asked, k_rows[i].skips,
                                       k_rows[i].no_rom);
        bool ok = k_rows[i].want ? got != NULL && !strcmp(got, k_rows[i].want) : got == NULL;
        if (!ok) {
            fprintf(stderr, "  verdict row %zu: got \"%s\"\n", i, got ? got : "(none)");
            g_test_failures++;
        }
    }
    CHECK(test_verdict("suite_x", TEST_ROM_FREE, false, 3, true) != NULL);   /* any number of skips */
}

void suite_smoke(void)
{
    CHECK_EQ(1 + 1, 2);
    CHECK_STR("ok", "ok");
    test_verdict_table();
}
