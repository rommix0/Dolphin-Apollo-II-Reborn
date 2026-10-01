#include <stdio.h>
#include "test.h"
#include "rom_fixture.h"
#include "machine/machine.h"

static machine g_m;

void suite_rom_boot(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(machine_init(&g_m, rom, len, NULL));
    machine_status st = machine_run(&g_m, 100000000ULL);   /* 50 s of emulated time */
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    printf("  boot: %llu cycles, %llu frames produced, %llu DSP port writes, %u serial bytes sent\n",
           (unsigned long long)g_m.cpu.cycles, (unsigned long long)g_m.frames_produced,
           (unsigned long long)g_m.port_writes, (unsigned)g_m.sfr.uart_tx_count);
}
