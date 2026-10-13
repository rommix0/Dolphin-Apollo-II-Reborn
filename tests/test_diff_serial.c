/* Probe-based differential test of the serial code (M4.6a spec 4.2) and of speech timing (M4.6b spec 4.3): the paths no
   corpus text reaches (the reply transmission, parallel mode, the _INT1 latch, the host-line interrupt, the ring page
   ends; the parallel handshake of isr_par_ready and isr_par_hold, a frame boundary of the timer interrupt with the
   frame flag still set) are compared with the ROM through synthetic calls from real states. Every corpus file and the
   boot run go through the ROM engine; at every entry of isr_int1 $C2B4 (a received byte: the CPU has just entered the
   interrupt, PC and P pushed, I set) and of ser_tx_put $C4DD (a reply being queued: the output ring holds the reply's
   bytes before it), the machine is cloned twice for each probe of that point, both copies with interrupts held. The
   probe's injection is made to both copies; then copy A runs the ROM routine and copy B the C (its replacement hooked
   in copy B alone; the ROM routine while there is none), and the two must agree on the exit state with A, X and Y
   (diff_same, diff_state.h).
   An interrupt probe starts at the handler's first instruction with the frame the CPU pushed on entering it: isr_int1's
   own entry; for isr_int4_host_line the same point with the PC set to vec_int4_timerx ($FFF4) and INT4 requested and
   enabled (ICR3 bits 7 and 6), as if the CPU had taken INT4 there instead (take_irq, cpu740.c, pushes the same PC and P
   and sets I for every line; only the vector differs); for isr_timer_dsp likewise with vec_int2_timers ($FFF8) and
   timer 3's request and enable bits (ICR2 bits 5 and 4). Each copy runs to the handler's RTI (machine_finish_routine).
   A subroutine probe is a machine_rom_call of the routine with the copy's registers.
   A hook on copy A records whether the probe's target branch went the probe's way (the hook at the destination fires
   with the branch as the last instruction executed, the machine's history), or for a routine without a branch
   whether it was entered; every probe must reach its target at every call. Every routine diff_lts never sees
   (diff_probed, diff_state.c) is some probe's routine.
   A self-check first runs the ROM routine in copy B too, on queries: it proves the probes and the comparison
   whatever the replacement table holds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_state.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/isr_addr.h"
#include "firmware/ser_addr.h"

#define PROBE_BUDGET  1000000ULL
#define VEC_INT4      0xFFF4   /* vec_int4_timerx */
#define VEC_INT2      0xFFF8   /* vec_int2_timers (timer 3: the DSP tick) */
#define P0_IN_PAR     0x08     /* P0_DIR in parallel mode (boot_init_ports $AC29): P0.0 and P0.4 inputs */
#define P0_HOST_LINE  0x04     /* P0.2, the host's handshake line (an input in both modes) */
#define ICR1_INT1_REQ 0x02
#define ICR3_INT4     0xC0     /* bit 7 request, bit 6 enable */
#define ICR2_T3       0x30     /* timer 3: bit 5 request, bit 4 enable */
#define REGS          (FW_OUT_A | FW_OUT_X | FW_OUT_Y)

static const char *const k_corpus[] = { CORPUS_NAMES };

typedef enum { AT_INT1, AT_TX_PUT } probe_point;

typedef struct {
    const char *name;
    probe_point at;
    const char *routine;      /* the replacement copy B runs */
    uint16_t addr;            /* its ROM entry */
    bool isr;
    void (*inject)(machine *m);
    uint16_t branch, dest;    /* the target: the branch at branch goes to dest (branch 0: dest is reached) */
    unsigned calls, reached;
} probe;

static machine g_m, g_a, g_b;
static fw_bridge g_bridge;
static const char *g_source;
static bool g_self_check;
static unsigned g_mismatches;
static const probe *g_cur;
static bool g_hit;

/* Parallel mode (boot_dip bit 0) with the port directions boot_init_ports sets for it; the host's levels on P0.0
   and P0.4 (p0), isr_par_state bit 0 (busy) and the byte on P1_LATCH (port_in[1], which P1_LATCH reads). */
static void parallel(machine *m, uint8_t p0, bool busy, uint8_t latch)
{
    m->ram[BOOT_DIP] |= 1;
    m->sfr.port_dir[0] = P0_IN_PAR;
    m->sfr.port_in[0] = (uint8_t)((m->sfr.port_in[0] & ~0x11) | p0);
    m->ram[ISR_PAR_STATE] = (uint8_t)((m->ram[ISR_PAR_STATE] & ~1) | (busy ? 1 : 0));
    m->sfr.port_in[1] = latch;
}

/* isr_int1, serial mode: an _INT1 request (ICR1 bit 1) with the received byte on P1_LATCH */
static void inj_latch(machine *m)
{
    m->sfr.icr1 |= ICR1_INT1_REQ;
    m->sfr.port_in[1] = m->sfr.uart_rx;
}
static void inj_mute_01(machine *m) { m->sfr.uart_rx = 0x01; }   /* Ctrl-A received */
static void inj_par_ack(machine *m) { parallel(m, 0x01, false, m->sfr.uart_rx); }
static void inj_par_idle(machine *m) { parallel(m, 0x00, false, m->sfr.uart_rx); }
static void inj_par_done(machine *m) { parallel(m, 0x00, true, m->sfr.uart_rx); }
static void inj_par_reply(machine *m) { parallel(m, 0x10, false, 5); }
static void inj_par_code(machine *m) { parallel(m, 0x10, false, 3); }
static void inj_par_data(machine *m)
{
    parallel(m, 0x10, false, m->sfr.uart_rx);
    m->ram[SER_TX_PENDING] &= (uint8_t)~1;
}
static void inj_par_data_pending(machine *m)
{
    parallel(m, 0x10, false, m->sfr.uart_rx);
    m->ram[SER_TX_PENDING] |= 1;
}
static void inj_par_busy(machine *m) { parallel(m, 0x10, true, m->sfr.uart_rx); }

/* isr_int4_host_line: INT4 taken instead, the host line high or low */
static void int4(machine *m, bool high)
{
    m->sfr.icr3 |= ICR3_INT4;
    m->sfr.port_in[0] = (uint8_t)(high ? m->sfr.port_in[0] | P0_HOST_LINE : m->sfr.port_in[0] & ~P0_HOST_LINE);
    m->cpu.pc = (uint16_t)(machine_peek(m, VEC_INT4) | machine_peek(m, VEC_INT4 + 1) << 8);
}
static void inj_int4_high(machine *m) { int4(m, true); }
static void inj_int4_low(machine *m) { int4(m, false); }

/* isr_timer_dsp: timer 3's interrupt taken instead, at a frame boundary (the 10th tick of the frame's last word:
   isr_tick_div and isr_words_left 1, isr_word_ofs 18) with isr_frame_flag bit 0 still set, so the frame repeats */
static void inj_timer_repeat(machine *m)
{
    m->sfr.icr2 |= ICR2_T3;
    m->ram[ISR_TICK_DIV] = 1;
    m->ram[ISR_WORDS_LEFT] = 1;
    m->ram[ISR_WORD_OFS] = 18;
    m->ram[ISR_FRAME_FLAG] |= 1;
    m->cpu.pc = (uint16_t)(machine_peek(m, VEC_INT2) | machine_peek(m, VEC_INT2 + 1) << 8);
}

/* isr_par_ready and isr_par_hold in parallel mode (boot_dip bit 0, P0's parallel directions); for isr_par_hold an
   _INT1 request pending too (ICR1 bit 1), which it clears */
static void inj_par_ready(machine *m)
{
    m->ram[BOOT_DIP] |= 1;
    m->sfr.port_dir[0] = P0_IN_PAR;
}
static void inj_par_hold(machine *m)
{
    inj_par_ready(m);
    m->sfr.icr1 |= ICR1_INT1_REQ;
}

/* ser_rx_put of the received byte with the input ring's write pointer at $38FF, its last byte */
static void inj_rx_wrap(machine *m)
{
    m->ram[SER_RX_WRITE] = 0xFF;
    m->ram[SER_RX_WRITE + 1] = 0x38;
    m->cpu.a = m->sfr.uart_rx;
}

/* cmd_imm_match in command mode with "I" in ser_cmd_buf and a byte in A, matching from cmd_imm_table's start */
static void imm(machine *m, uint16_t ptr, uint8_t a)
{
    m->ram[SER_CMD_MODE] |= 1;
    memset(m->ram + SER_CMD_BUF, 0, 5);
    m->ram[SER_CMD_BUF] = 'I';
    m->ram[SER_CMD_PTR] = (uint8_t)ptr;
    m->ram[SER_CMD_PTR + 1] = (uint8_t)(ptr >> 8);
    m->ram[SER_CMD_TABLE_OFS] = 0;
    m->cpu.a = a;
}
/* ser_cmd_ptr at $20FF: the byte goes there and "I" alone is a partial match, so the pointer steps into page $21 */
static void inj_imm_page(machine *m) { imm(m, 0x20FF, 'I'); }
/* "I?": the reply's last letter T with ser_rx_idle_saved clear (text waiting); with it set, M (boot_speaking clear)
   or T (set) */
static void inj_imm_query_text(machine *m)
{
    imm(m, SER_CMD_BUF + 1, '?');
    m->ram[SER_RX_IDLE_SAVED] &= (uint8_t)~1;
}
static void inj_imm_query_m(machine *m)
{
    imm(m, SER_CMD_BUF + 1, '?');
    m->ram[SER_RX_IDLE_SAVED] |= 1;
    m->ram[BOOT_SPEAKING] &= (uint8_t)~1;
}
static void inj_imm_query_t(machine *m)
{
    imm(m, SER_CMD_BUF + 1, '?');
    m->ram[SER_RX_IDLE_SAVED] |= 1;
    m->ram[BOOT_SPEAKING] |= 1;
}

/* ser_tx_service with the host line high (the ring holding the reply so far: empty, one byte or several), low, or
   high in parallel mode */
static void inj_svc_high(machine *m) { m->sfr.port_in[0] |= P0_HOST_LINE; }
static void inj_svc_low(machine *m) { m->sfr.port_in[0] &= (uint8_t)~P0_HOST_LINE; }
static void inj_svc_dip(machine *m)
{
    m->sfr.port_in[0] |= P0_HOST_LINE;
    m->ram[BOOT_DIP] |= 1;
}
/* no injection: the routine called as it is */
static void inj_none(machine *m) { (void)m; }
/* ser_tx_next_page from the ring's page $39 */
static void inj_tx_page(machine *m) { m->cpu.y = 0x39; }
/* the output ring's write pointer at $39FF, then ser_tx_put; its read pointer there with a byte pending, then
   ser_tx_get */
static void inj_tx_wrap(machine *m)
{
    m->ram[SER_TX_WRITE] = 0xFF;
    m->ram[SER_TX_WRITE + 1] = 0x39;
}
static void inj_tx_get_wrap(machine *m)
{
    m->ram[SER_TX_READ] = 0xFF;
    m->ram[SER_TX_READ + 1] = 0x39;
    m->ram[SER_TX_PENDING] |= 1;
}

static probe g_probes[] = {
    { "int1_latch",       AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_latch,       0xC341, 0xC367 },
    { "int1_mute_01",     AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_mute_01,     0xC380, 0xC391 },
    { "par_ack",          AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_ack,     0xC2BA, 0xC2BD },
    { "par_idle",         AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_idle,    0xC2C3, 0xC2C6 },
    { "par_done",         AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_done,    0xC2C3, 0xC2D6 },
    { "par_reply",        AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_reply,   0xC304, 0xC306 },
    { "par_code",         AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_code,    0xC304, 0xC331 },
    { "par_data",         AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_data,    0xC31E, 0xC326 },
    { "par_data_pending", AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_data_pending, 0xC31E, 0xC321 },
    { "par_busy",         AT_INT1,   "isr_int1",           ROM_ISR_INT1,           true,  inj_par_busy,    0xC315, 0xC331 },
    { "int4_high",        AT_INT1,   "isr_int4_host_line", ROM_ISR_INT4_HOST_LINE, true,  inj_int4_high,   0xC3E0, 0xC3E7 },
    { "int4_low",         AT_INT1,   "isr_int4_host_line", ROM_ISR_INT4_HOST_LINE, true,  inj_int4_low,    0xC3E0, 0xC3E3 },
    { "host_line_high",   AT_INT1,   "isr_host_line_high", ROM_ISR_HOST_LINE_HIGH, false, inj_none,        0, ROM_ISR_HOST_LINE_HIGH },
    { "timer_repeat",     AT_INT1,   "isr_timer_dsp",      ROM_ISR_TIMER_DSP,      true,  inj_timer_repeat, 0xB300, 0xB303 },
    { "par_ready",        AT_INT1,   "isr_par_ready",      ROM_ISR_PAR_READY,      false, inj_par_ready,   0xC5D7, 0xC5DA },
    { "par_hold",         AT_INT1,   "isr_par_hold",       ROM_ISR_PAR_HOLD,       false, inj_par_hold,    0xC5DF, 0xC5E2 },
    { "rx_wrap",          AT_INT1,   "ser_rx_put",         ROM_SER_RX_PUT,         false, inj_rx_wrap,     0xC5EF, 0xC5F2 },
    { "imm_page",         AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_page,    0xC456, 0xC458 },
    { "imm_query_text",   AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_query_text, 0xC4A1, 0xC4A9 },
    { "imm_query_m",      AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_query_m, 0xC4A4, 0xC4A7 },
    { "imm_query_t",      AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_query_t, 0xC4A4, 0xC4A9 },
    { "svc_high",         AT_TX_PUT, "ser_tx_service",     ROM_SER_TX_SERVICE,     false, inj_svc_high,    0xC585, 0xC588 },
    { "svc_low",          AT_TX_PUT, "ser_tx_service",     ROM_SER_TX_SERVICE,     false, inj_svc_low,     0xC585, 0xC5A2 },
    { "svc_dip",          AT_TX_PUT, "ser_tx_service",     ROM_SER_TX_SERVICE,     false, inj_svc_dip,     0xC582, 0xC5A2 },
    { "tx_wrap",          AT_TX_PUT, "ser_tx_put",         ROM_SER_TX_PUT,         false, inj_tx_wrap,     0xC4EC, 0xC4EE },
    { "tx_get_wrap",      AT_TX_PUT, "ser_tx_get",         ROM_SER_TX_GET,         false, inj_tx_get_wrap, 0xC4C2, 0xC4C4 },
    { "tx_send",          AT_TX_PUT, "ser_tx_send",        ROM_SER_TX_SEND,        false, inj_none,        0, ROM_SER_TX_SEND },
    { "tx_next_page",     AT_TX_PUT, "ser_tx_next_page",   ROM_SER_TX_NEXT_PAGE,   false, inj_tx_page,     0xC5F8, 0xC5FB },
};
#define PROBE_COUNT (sizeof g_probes / sizeof g_probes[0])

/* Copy A, at the probe's destination: the branch was the last instruction executed (or there is none). */
static bool on_dest(machine *m, void *user)
{
    (void)user;
    if (!g_cur->branch || m->history[(m->history_pos - 1) % MACHINE_HISTORY] == g_cur->branch)
        g_hit = true;
    return false;
}

/* Runs the probe's routine in m: an interrupt handler to its RTI, a subroutine through machine_rom_call. With b, the
   routine's replacement is hooked first (copy B). */
static bool run_copy(machine *m, const probe *p, fw_bridge *b)
{
    if (b && (!fw_bridge_init(b, m) || !fw_install(m, b, p->routine)))
        return false;
    if (p->isr)
        return machine_finish_routine(m, PROBE_BUDGET);
    machine_regs r = { m->cpu.a, m->cpu.x, m->cpu.y, m->cpu.p };
    return machine_rom_call(m, p->addr, &r, PROBE_BUDGET) == MCALL_RETURNED;
}

static void check_probe(machine *m, probe *p)
{
    char why[160] = "";
    unsigned call = p->calls++;
    machine_clone(&g_a, m);
    machine_hold_irqs(&g_a, true);
    p->inject(&g_a);
    machine_clone(&g_b, m);
    machine_hold_irqs(&g_b, true);
    p->inject(&g_b);
    g_cur = p;
    g_hit = false;
    CHECK(machine_hook(&g_a, p->dest, on_dest, NULL));
    bool a_ok = run_copy(&g_a, p, NULL);
    p->reached += g_hit;
    bool use_c = !g_self_check && fw_replacement_find(p->routine) != NULL;
    bool b_ok = run_copy(&g_b, p, use_c ? &g_bridge : NULL);
    if (!a_ok)
        snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_a.fault);
    else if (!b_ok)
        snprintf(why, sizeof why, "copy B did not return (fault %d at $%04X)", (int)g_b.fault, g_b.fault_addr);
    else if (diff_same(&g_a, &g_b, REGS, why, sizeof why))
        why[0] = '\0';
    if (why[0] && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s call %u (A=%02X X=%02X Y=%02X): %s\n", g_source, p->name, call, m->cpu.a, m->cpu.x,
                m->cpu.y, why);
}

static void check_point(machine *m, probe_point at)
{
    for (size_t i = 0; i < PROBE_COUNT; i++)
        if (g_probes[i].at == at)
            check_probe(m, &g_probes[i]);
}

static bool on_int1(machine *m, void *user)
{
    (void)user;
    check_point(m, AT_INT1);
    return false;   /* the ROM routine runs on in m */
}

static bool on_tx_put(machine *m, void *user)
{
    (void)user;
    check_point(m, AT_TX_PUT);
    return false;
}

static void install(machine *m, void *user)
{
    (void)user;
    CHECK(machine_hook(m, ROM_ISR_INT1, on_int1, NULL));
    CHECK(machine_hook(m, ROM_SER_TX_PUT, on_tx_put, NULL));
}

static void run(const uint8_t *rom, size_t len, const char *source, const char *text, bool boot)
{
    frame_list fl = { 0 };
    g_source = source;
    machine_status st = rom_engine_run(&g_m, rom, len, NULL, install, NULL, text, boot, &fl, NULL);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    machine_clear_hooks(&g_m);
    frame_list_free(&fl);
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

static void run_file(const uint8_t *rom, size_t len, const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "tests/corpus/%s.txt", name);
    char *text = test_read_file(path);
    CHECK(text != NULL);
    if (!text)
        return;
    strip_newline(text);
    run(rom, len, name, text, false);
    free(text);
}

/* The probes' routines that are not C yet (M4.6b): their copy B runs the ROM until Tasks 3 and 5 of the M4.6b plan
   make them C. */
static const char *const k_not_c_yet[] = { "isr_par_ready", "isr_par_hold", "isr_timer_dsp" };

static bool not_c_yet(const char *routine)
{
    for (size_t i = 0; i < sizeof k_not_c_yet / sizeof k_not_c_yet[0]; i++)
        if (!strcmp(k_not_c_yet[i], routine))
            return true;
    return false;
}

/* Prints each probe's counts; every probe compared and its target reached at every call. */
static void report(void)
{
    for (size_t i = 0; i < PROBE_COUNT; i++) {
        const probe *p = &g_probes[i];
        bool c = !g_self_check && fw_replacement_find(p->routine) != NULL;
        if (p->branch)
            printf("  %-16s %-18s %5u calls compared (%s), $%04X -> $%04X in %u\n", p->name, p->routine, p->calls,
                   c ? "C" : "ROM", p->branch, p->dest, p->reached);
        else
            printf("  %-16s %-18s %5u calls compared (%s), entered in %u\n", p->name, p->routine, p->calls,
                   c ? "C" : "ROM", p->reached);
        CHECK(p->calls > 0);
        CHECK_EQ(p->reached, p->calls);
        CHECK(c || g_self_check || not_c_yet(p->routine));   /* every probe's routine is C (M4.6a, M4.6b) */
    }
}

static void reset_counts(void)
{
    for (size_t i = 0; i < PROBE_COUNT; i++)
        g_probes[i].calls = g_probes[i].reached = 0;
}

void suite_diff_serial(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    g_mismatches = 0;
    g_self_check = true;   /* ROM against ROM: the probes and the comparison */
    reset_counts();
    run_file(rom, len, "queries");
    printf("  self-check (ROM against ROM, queries):\n");
    report();
    g_self_check = false;
    reset_counts();
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++)
        run_file(rom, len, k_corpus[c]);
    run(rom, len, "boot", "", true);
    printf("  corpus and boot:\n");
    report();
    for (size_t i = 0; i < diff_probed_count; i++) {   /* every routine diff_lts never sees is compared here */
        bool probed = false;
        for (size_t k = 0; k < PROBE_COUNT; k++)
            probed |= !strcmp(g_probes[k].routine, diff_probed[i]);
        CHECK(probed);
    }
    CHECK_EQ(g_mismatches, 0);
}
