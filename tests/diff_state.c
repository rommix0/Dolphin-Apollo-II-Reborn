#include <stdio.h>
#include <string.h>
#include "diff_state.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"

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
    if (a->p23_func != b->p23_func) {
        snprintf(why, n, "P23_FUNC: ROM %02X, C %02X", a->p23_func, b->p23_func);
        return false;
    }
    for (int t = 0; t < 3; t++) {
        if (a->timer_pre[t] != b->timer_pre[t] || a->timer_reload[t] != b->timer_reload[t] ||
            a->timer_running[t] != b->timer_running[t]) {
            snprintf(why, n, "timer %d prescaler/reload/running: ROM %02X/%02X/%d, C %02X/%02X/%d", t + 1,
                     a->timer_pre[t], a->timer_reload[t], a->timer_running[t], b->timer_pre[t], b->timer_reload[t],
                     b->timer_running[t]);
            return false;
        }
    }
    return true;
}

/* The DSP outputs (M4.6b spec 4.1): the port bytes, the reset pulses and the raw frame grouping, the frames
   produced. */
static bool dsp_same(const machine *a, const machine *b, char *why, size_t n)
{
    if (a->port_writes != b->port_writes || a->raw_pos != b->raw_pos ||
        memcmp(a->raw_buf, b->raw_buf, sizeof a->raw_buf) != 0) {
        snprintf(why, n, "DSP port writes/raw position: ROM %llu/%u, C %llu/%u (or the raw frame bytes)",
                 (unsigned long long)a->port_writes, a->raw_pos, (unsigned long long)b->port_writes, b->raw_pos);
        return false;
    }
    if (a->dsp_resets != b->dsp_resets || a->dsp_reset_level != b->dsp_reset_level) {
        snprintf(why, n, "DSP resets/reset level: ROM %llu/%d, C %llu/%d", (unsigned long long)a->dsp_resets,
                 a->dsp_reset_level, (unsigned long long)b->dsp_resets, b->dsp_reset_level);
        return false;
    }
    if (a->frames_produced != b->frames_produced) {
        snprintf(why, n, "frames produced: ROM %llu, C %llu", (unsigned long long)a->frames_produced,
                 (unsigned long long)b->frames_produced);
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
    if (!sfr_same(&a->sfr, &b->sfr, why, n) || !dsp_same(a, b, why, n))
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

static bool at_bank_entry(const machine *m) { return m->cpu.pc == BANK_ENTRY_WARM || m->cpu.pc == BANK_ENTRY_COLD; }

bool diff_run_to_bank_entry(machine *m)
{
    for (int i = 0; i < DIFF_ENTRY_STEPS && !at_bank_entry(m); i++)
        if (!machine_step(m))
            return false;
    return at_bank_entry(m);
}

bool diff_same_at_entry(const machine *a, const machine *b, char *why, size_t n)
{
    if ((a->cpu.p ^ b->cpu.p) & CPU740_I) {
        snprintf(why, n, "I: ROM %d, C %d", (a->cpu.p & CPU740_I) != 0, (b->cpu.p & CPU740_I) != 0);
        return false;
    }
    return diff_same(a, b, FW_OUT_A | FW_OUT_X, why, n);
}
