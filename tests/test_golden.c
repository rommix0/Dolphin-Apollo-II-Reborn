#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
#include "engine/native_engine.h"

static const char *const k_corpus[] = { CORPUS_NAMES };

static machine g_m;
static native_engine g_native;

static bool write_file(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(s, 1, strlen(s), f) == strlen(s);
    return fclose(f) == 0 && ok;
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

static void report_diff(const char *name, const char *got, const char *want)
{
    size_t line = 1, i = 0;
    while (got[i] && got[i] == want[i]) {
        if (got[i] == '\n')
            line++;
        i++;
    }
    fprintf(stderr, "  %s: produced frames differ from golden at frame %zu\n", name, line);
}

/* Speaks text (with the boot frames if include_boot), then compares the frames with
   tests/golden/<name>.frm, or rewrites that file in update mode. */
static void check_golden(const uint8_t *rom, size_t len, const char *name, const char *text,
                         bool include_boot, bool update)
{
    char frm_path[128];
    snprintf(frm_path, sizeof frm_path, "tests/golden/%s.frm", name);
    frame_list fl = { 0 };
    machine_status st = rom_engine_speak(&g_m, rom, len, text, include_boot, &fl);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    CHECK(fl.count > 0);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    char *hex = frame_list_hex(&fl);

    if (update) {
        if (st == MACHINE_IDLE && fl.count > 0 && g_m.unmodelled_count == 0) {
            CHECK(write_file(frm_path, hex));
            printf("  updated %s (%zu frames)\n", frm_path, fl.count);
        } else {
            fprintf(stderr, "  NOT updating %s: %s\n", frm_path,
                    st != MACHINE_IDLE ? "run did not reach idle"
                    : fl.count == 0 ? "no frames produced" : "unmodelled accesses logged");
            g_test_failures++;
        }
    } else {
        char *want = test_read_file(frm_path);
        if (!want) {
            fprintf(stderr, "  missing %s: run once with APOLLO2_UPDATE_GOLDEN=1 and review\n", frm_path);
            g_test_failures++;
        } else {
            if (strcmp(hex, want) != 0) {
                report_diff(name, hex, want);
                g_test_failures++;
            }
            free(want);
        }
    }
    free(hex);
    frame_list_free(&fl);
}

/* Spec section 4: the produced-frame stream does not depend on CPU timing. Scale the cycles the
   peripheral timers see (1/4 = timers 4x slower relative to the CPU, 3/1 = 3x faster: there the ROM finishes some
   frames after their period has ended, and the timer interrupt sends the previous frame again). First the rom
   engine, then the hybrid engine with every replacement (M4.4b spec 4.4): its frame loop and frame builder are C
   (M4.5), which take no emulated time, so it builds faster (only its isr_* and ser_* ROM calls take cycles); the
   produced frames must not change either way. Then the native engine (M5a spec 4.5), which charges the hybrid's
   cycles: the same frames, and the hybrid's cycle count at each scale. */
static void test_timing_invariance(const uint8_t *rom, size_t len)
{
    static const uint32_t k_scale[][2] = { { 1, 4 }, { 3, 1 } };
    static const char *const k_engine[] = { "", "hybrid ", "native " };
    const size_t n = sizeof k_scale / sizeof k_scale[0];
    uint64_t hybrid_cycles[sizeof k_scale / sizeof k_scale[0]] = { 0 };
    char *text = test_read_file("tests/corpus/hello.txt");
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(text != NULL);
    CHECK(want != NULL);
    if (text && want) {
        strip_newline(text);
        for (size_t j = 0; j < 3 * n; j++) {
            size_t i = j % n, engine = j / n;
            frame_list fl = { 0 };
            uint64_t cycles;
            unsigned unmodelled;
            if (engine < 2) {
                machine_config cfg;
                machine_default_config(&cfg);
                cfg.cycle_num = k_scale[i][0];
                cfg.cycle_den = k_scale[i][1];
                machine_status st = engine ? hybrid_engine_speak(&g_m, rom, len, &cfg, NULL, text, false, &fl, NULL,
                                                                 NULL)
                                           : rom_engine_speak_cfg(&g_m, rom, len, &cfg, text, false, &fl);
                if (st != MACHINE_IDLE)
                    machine_dump(&g_m, stderr);
                CHECK_EQ(st, MACHINE_IDLE);
                print_unmodelled(&g_m);
                cycles = g_m.cpu.cycles;
                unmodelled = g_m.unmodelled_count;
                if (engine == 1)
                    hybrid_cycles[i] = cycles;
            } else {
                native_config cfg;
                native_default_config(&cfg);
                cfg.cycle_num = k_scale[i][0];
                cfg.cycle_den = k_scale[i][1];
                CHECK_EQ(native_engine_speak(&g_native, &cfg, text, false, &fl, NULL), ENGINE_IDLE);
                cycles = g_native.b.cycles;
                unmodelled = g_native.b.unmodelled_count;
                CHECK_EQ(cycles, hybrid_cycles[i]);
            }
            CHECK_EQ(unmodelled, 0);
            char *hex = frame_list_hex(&fl);
            if (strcmp(hex, want) != 0) {
                fprintf(stderr, "  %stimer scale %u/%u:", k_engine[engine], (unsigned)k_scale[i][0],
                        (unsigned)k_scale[i][1]);
                report_diff("hello", hex, want);
                g_test_failures++;
            }
            printf("  %stimer scale %u/%u: %zu frames, %llu CPU cycles\n", k_engine[engine], (unsigned)k_scale[i][0],
                   (unsigned)k_scale[i][1], fl.count, (unsigned long long)cycles);
            free(hex);
            frame_list_free(&fl);
        }
    }
    free(text);
    free(want);
}

/* Query commands (@c?) only queue a reply for the host (which needs the RTS/CTS handshake);
   they must not change the speech. */
static void test_queries_are_silent(void)
{
    char *queries = test_read_file("tests/golden/queries.frm");
    char *hello = test_read_file("tests/golden/hello.frm");
    CHECK(queries != NULL);
    CHECK(hello != NULL);
    if (queries && hello)
        CHECK(strcmp(queries, hello) == 0);
    free(queries);
    free(hello);
}

void suite_golden(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    bool update = getenv("APOLLO2_UPDATE_GOLDEN") != NULL && getenv("APOLLO2_UPDATE_GOLDEN")[0] != '\0';

    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char txt_path[128];
        snprintf(txt_path, sizeof txt_path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(txt_path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        check_golden(rom, len, k_corpus[c], text, false, update);
        free(text);
    }
    check_golden(rom, len, "boot", "", true, update);   /* the startup announcement */
    if (!update) {
        test_queries_are_silent();
        test_timing_invariance(rom, len);
    }
}
