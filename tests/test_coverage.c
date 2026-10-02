#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "machine/coverage.h"
#include "machine/machine.h"

static coverage g_cov;
static uint8_t g_rom[MACHINE_ROM_SIZE];
static machine g_m;

static void test_writer(void)
{
    coverage_init(&g_cov);
    coverage_mark(&g_cov, COV_LOW, 0x4123);
    coverage_mark(&g_cov, COV_ROM, 0xA61C);
    coverage_mark(&g_cov, COV_ROM, 0x8000);
    coverage_mark(&g_cov, COV_ROM, 0x8000);
    coverage_add_edge(&g_cov, COV_ROM, 0xB2F0, COV_ROM, 0x9A10);
    coverage_add_edge(&g_cov, COV_ROM, 0xB2F0, COV_ROM, 0x9A10);   /* duplicate: stored once */
    coverage_add_edge(&g_cov, COV_RAMCODE, 0x0210, COV_ROM, 0x8100);
    CHECK(coverage_hit(&g_cov, COV_ROM, 0x8000));
    CHECK(!coverage_hit(&g_cov, COV_LOW, 0x8000));
    CHECK_EQ(coverage_count(&g_cov, COV_ROM), 2);
    CHECK_EQ(g_cov.edge_count, 2);
    CHECK_STR(coverage_space_name(COV_RAMCODE), "ramcode");

    FILE *f = fopen("build/test_coverage.cov", "wb");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(coverage_write(&g_cov, f));
    fclose(f);
    char *got = test_read_file("build/test_coverage.cov");
    CHECK(got != NULL);
    if (got)
        CHECK_STR(got,
                  "# apollo2 coverage v1: x SPACE ADDR = executed instruction; "
                  "e SPACE FROM SPACE TO = indirect JMP/JSR target\n"
                  "x rom 8000\n"
                  "x rom A61C\n"
                  "x low 4123\n"
                  "e rom B2F0 rom 9A10\n"
                  "e ramcode 0210 rom 8100\n");
    free(got);
    coverage_free(&g_cov);
}

/* A hand-assembled program that runs code in every coverage space and makes one indirect jump. */
static void test_machine_hook(void)
{
    static const uint8_t k_main[] = {
        0xA9, 0xF0,                    /* 9000 LDA #$F0                                    */
        0x85, M50734_P2,               /* 9002 STA P2      bank $70, P2.7 = 1 (lower half) */
        0xA9, 0xFD,                    /* 9004 LDA #$FD                                    */
        0x85, M50734_P2_DIR,           /* 9006 STA P2_DIR                                  */
        0x20, 0x00, 0x40,              /* 9008 JSR $4000   runs file 0x0000                */
        0x6C, 0x20, 0x90,              /* 900B JMP ($9020) -> $900E                        */
        0x20, 0x00, 0x03,              /* 900E JSR $0300   RAMCODE range                   */
        0x20, 0x00, 0x10,              /* 9011 JSR $1000   other RAM                       */
        0x4C, 0x14, 0x90,              /* 9014 JMP $9014                                   */
    };
    machine_config cfg;
    memset(g_rom, 0, sizeof g_rom);
    memcpy(g_rom + 0x9000, k_main, sizeof k_main);
    g_rom[0x9020] = 0x0E; g_rom[0x9021] = 0x90;
    g_rom[0x0000] = 0x60;                                  /* RTS, reached through the lower window */
    g_rom[0xFFFE] = 0x00; g_rom[0xFFFF] = 0x90;
    coverage_init(&g_cov);
    machine_default_config(&cfg);
    CHECK(cfg.coverage == NULL);                           /* off by default */
    cfg.coverage = &g_cov;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, &cfg));
    g_m.ram[0x0300] = 0x60;
    g_m.ram[0x1000] = 0x60;
    CHECK_EQ(machine_run(&g_m, 400), MACHINE_CYCLE_LIMIT);
    CHECK_EQ(g_m.fault, MFAULT_NONE);

    CHECK(coverage_hit(&g_cov, COV_ROM, 0x9000));
    CHECK(coverage_hit(&g_cov, COV_ROM, 0x900B));
    CHECK(coverage_hit(&g_cov, COV_ROM, 0x9014));
    CHECK(coverage_hit(&g_cov, COV_LOW, 0x4000));
    CHECK(!coverage_hit(&g_cov, COV_ROM, 0x4000));
    CHECK(coverage_hit(&g_cov, COV_RAMCODE, 0x0300));
    CHECK(!coverage_hit(&g_cov, COV_RAM, 0x0300));
    CHECK(coverage_hit(&g_cov, COV_RAM, 0x1000));
    CHECK_EQ(coverage_count(&g_cov, COV_ROM), 9);
    CHECK_EQ(coverage_count(&g_cov, COV_LOW), 1);
    CHECK_EQ(coverage_count(&g_cov, COV_RAMCODE), 1);
    CHECK_EQ(coverage_count(&g_cov, COV_RAM), 1);
    CHECK_EQ(g_cov.edge_count, 1);
    if (g_cov.edge_count == 1) {
        CHECK_EQ(g_cov.edges[0].from_space, COV_ROM);
        CHECK_EQ(g_cov.edges[0].from, 0x900B);
        CHECK_EQ(g_cov.edges[0].to_space, COV_ROM);
        CHECK_EQ(g_cov.edges[0].to, 0x900E);
    }
    coverage_free(&g_cov);
}

void suite_coverage(void)
{
    test_writer();
    test_machine_hook();
}
