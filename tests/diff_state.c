#include <stdio.h>
#include <string.h>
#include "diff_state.h"
#include "engine/thunks.h"

#define ICR2_ENABLES 0x15   /* timers 1-3: enable bits 0, 2, 4 */

const char *const diff_probed[] = {
    "ser_tx_get", "ser_tx_send", "ser_tx_service", "ser_tx_next_page", "isr_int4_host_line", "isr_host_line_high",
};
const size_t diff_probed_count = sizeof diff_probed / sizeof diff_probed[0];

bool diff_is_probed(const char *name)
{
    for (size_t i = 0; i < diff_probed_count; i++)
        if (!strcmp(diff_probed[i], name))
            return true;
    return false;
}

static bool sfr_same(const m50734 *a, const m50734 *b, char *why, size_t n)
{
    for (int p = 0; p < 4; p++) {
        if (a->port_latch[p] != b->port_latch[p] || a->port_dir[p] != b->port_dir[p]) {
            snprintf(why, n, "P%d latch/dir: ROM %02X/%02X, C %02X/%02X", p, a->port_latch[p], a->port_dir[p],
                     b->port_latch[p], b->port_dir[p]);
            return false;
        }
    }
    if (a->icr1 != b->icr1 || ((a->icr2 ^ b->icr2) & ICR2_ENABLES) || a->icr3 != b->icr3) {
        snprintf(why, n, "ICR1/ICR2/ICR3: ROM %02X/%02X/%02X, C %02X/%02X/%02X", a->icr1, a->icr2, a->icr3, b->icr1,
                 b->icr2, b->icr3);
        return false;
    }
    if (a->uart_ctrl != b->uart_ctrl || a->uart_rx != b->uart_rx || a->uart_rx_full != b->uart_rx_full) {
        snprintf(why, n, "UART ctrl/rx/full: ROM %02X/%02X/%d, C %02X/%02X/%d", a->uart_ctrl, a->uart_rx,
                 a->uart_rx_full, b->uart_ctrl, b->uart_rx, b->uart_rx_full);
        return false;
    }
    if (a->uart_tx_count != b->uart_tx_count || a->uart_tx_last != b->uart_tx_last) {
        snprintf(why, n, "UART TX: ROM %u/%02X, C %u/%02X", (unsigned)a->uart_tx_count, a->uart_tx_last,
                 (unsigned)b->uart_tx_count, b->uart_tx_last);
        return false;
    }
    return true;
}

bool diff_same(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n)
{
    if (a->cpu.pc != b->cpu.pc || a->cpu.s != b->cpu.s) {
        snprintf(why, n, "exit PC/S: ROM %04X/%02X, C %04X/%02X", a->cpu.pc, a->cpu.s, b->cpu.pc, b->cpu.s);
        return false;
    }
    if ((outputs & FW_OUT_A) && a->cpu.a != b->cpu.a) {
        snprintf(why, n, "A: ROM %02X, C %02X", a->cpu.a, b->cpu.a);
        return false;
    }
    if ((outputs & FW_OUT_X) && a->cpu.x != b->cpu.x) {
        snprintf(why, n, "X: ROM %02X, C %02X", a->cpu.x, b->cpu.x);
        return false;
    }
    if ((outputs & FW_OUT_Y) && a->cpu.y != b->cpu.y) {
        snprintf(why, n, "Y: ROM %02X, C %02X", a->cpu.y, b->cpu.y);
        return false;
    }
    if ((outputs & FW_OUT_Z) && ((a->cpu.p ^ b->cpu.p) & CPU740_Z)) {
        snprintf(why, n, "Z: ROM %d, C %d", (a->cpu.p & CPU740_Z) != 0, (b->cpu.p & CPU740_Z) != 0);
        return false;
    }
    if (!sfr_same(&a->sfr, &b->sfr, why, n))
        return false;
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
            continue;
        if (addr >= 0x100 && addr <= 0x100u + a->cpu.s)
            continue;   /* the dead stack below the exit SP */
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return false;
        }
    }
    return true;
}
