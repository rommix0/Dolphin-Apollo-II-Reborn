/* Hooks, ROM calls, the interrupt hold and machine_clone (M4.0 spec sections 3.2, 3.3, 5.1), on small
   programs in a synthetic ROM; one test calls a real ROM routine. */
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "machine/machine.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/boot_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"

static uint8_t g_rom[MACHINE_ROM_SIZE];
static machine g_m, g_c;
static int g_calls;

static void put(uint16_t at, const uint8_t *b, size_t n) { memcpy(g_rom + at, b, n); }
static void vector(uint16_t at, uint16_t to) { g_rom[at] = (uint8_t)to; g_rom[at + 1] = (uint8_t)(to >> 8); }

/* Steps until the PC reaches stop (at most 200 steps). */
static void run_to(machine *m, uint16_t stop)
{
    for (int i = 0; i < 200 && m->cpu.pc != stop; i++)
        CHECK(machine_step(m));
    CHECK_EQ(m->cpu.pc, stop);
}

/* $9000: LDA #$05 / JSR $C100 / STA $10 / JMP $9007;  $C100: ASL A / ASL A / RTS  (A = $14) */
static void load_main(uint16_t callee)
{
    static const uint8_t main_code[] = { 0xA9, 0x05, 0x20, 0x00, 0xC1, 0x85, 0x10, 0x4C, 0x07, 0x90 };
    static const uint8_t shl2[] = { 0x0A, 0x0A, 0x60 };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, main_code, sizeof main_code);
    g_rom[0x9003] = (uint8_t)callee;
    g_rom[0x9004] = (uint8_t)(callee >> 8);
    put(0xC100, shl2, sizeof shl2);
    vector(0xFFFE, 0x9000);
    g_calls = 0;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
}

static bool hook_replace(machine *m, void *user)
{
    (void)user;
    g_calls++;
    m->cpu.a = 0x77;
    machine_return(m);
    return true;
}

static bool hook_observe(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_calls++;
    return false;
}

static void test_hook_replaces_routine(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_replace, NULL));
    CHECK(!machine_hook(&g_m, 0xC100, hook_replace, NULL));   /* already hooked */
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x77);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_m.cpu.s, 0xFF);   /* the emulated RTS popped the return address */
}

static void test_observer_lets_rom_run(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_observe, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x14);
    CHECK_EQ(g_calls, 1);
    machine_clear_hooks(&g_m);
    CHECK_EQ(g_m.hook_count, 0);
}

/* The hook table holds exactly MACHINE_MAX_HOOKS hooks (M4.4a spec 2.3: 256, enough for every routine left through
   M4.6; diff_lts installs one observer per replacement plus its boot_loop_play stop). MACHINE_MAX_HOOKS - 1
   observers at $8000 up (never executed here) and a replacement at $C100 fill it: the next distinct address is
   refused, and the last entry still fires. */
static void test_hook_table_full(void)
{
    CHECK_EQ(MACHINE_MAX_HOOKS, 256);
    load_main(0xC100);
    for (unsigned i = 0; i < MACHINE_MAX_HOOKS - 1; i++)
        CHECK(machine_hook(&g_m, (uint16_t)(0x8000 + i), hook_observe, NULL));
    CHECK(machine_hook(&g_m, 0xC100, hook_replace, NULL));
    CHECK_EQ(g_m.hook_count, MACHINE_MAX_HOOKS);
    CHECK(!machine_hook(&g_m, 0xC200, hook_replace, NULL));   /* the table is full */
    CHECK_EQ(g_m.hook_count, MACHINE_MAX_HOOKS);
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x77);   /* the last entry replaced $C100 */
    CHECK_EQ(g_calls, 1);            /* and no observer fired */
}

static bool hook_stuck(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_calls++;
    return true;   /* claims the routine but neither returns nor faults */
}

/* A hook that claims the routine without moving the PC would run again forever: the machine faults instead. */
static void test_stuck_hook_faults(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_stuck, NULL));
    run_to(&g_m, 0xC100);
    CHECK(!machine_step(&g_m));
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, 0xC100);
    CHECK_EQ(g_calls, 1);
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_stuck, NULL));
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_FAULT);
}

/* A hook fires only where the address maps to the main ROM file: not in the lower-ROM window. */
static void test_hook_only_in_main_rom(uint8_t p2_latch, uint8_t want, int want_calls)
{
    static const uint8_t code[] = {
        0x3C, 0x00, 0xF0,         /* LDM #p2,$F0  (patched below) */
        0x3C, 0xFF, 0xF1,         /* LDM #$FF,$F1: P2 all outputs */
        0xA9, 0x05,               /* LDA #$05 */
        0x20, 0x00, 0x41,         /* JSR $4100 */
        0x85, 0x10,               /* STA $10 */
        0x4C, 0x0D, 0x90,         /* JMP $900D */
    };
    static const uint8_t shl2[] = { 0x0A, 0x0A, 0x60 }, shl1[] = { 0x0A, 0x60 };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    g_rom[0x9001] = p2_latch;
    put(0x0100, shl2, sizeof shl2);   /* $4100 with the window on (file 0x0100) */
    put(0x4100, shl1, sizeof shl1);   /* $4100 with the window off */
    vector(0xFFFE, 0x9000);
    g_calls = 0;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(machine_hook(&g_m, 0x4100, hook_replace, NULL));
    run_to(&g_m, 0x900D);
    CHECK_EQ(g_m.ram[0x10], want);
    CHECK_EQ(g_calls, want_calls);
}

static bool hook_calls_rom(machine *m, void *user)
{
    (void)user;
    machine_regs r = { m->cpu.a, 0, 0, m->cpu.p };
    CHECK_EQ(machine_rom_call(m, 0xC100, &r, 1000), MCALL_RETURNED);
    m->cpu.a = (uint8_t)(r.a + 1);
    machine_return(m);
    return true;
}

static bool hook_fault(machine *m, void *user)
{
    (void)user;
    m->fault = MFAULT_FIRMWARE;
    m->fault_addr = 0x1234;
    return true;
}

static void test_rom_call(void)
{
    load_main(0xC100);
    run_to(&g_m, 0x9007);
    uint8_t s = g_m.cpu.s;
    machine_regs r = { 3, 0, 0, g_m.cpu.p };
    CHECK_EQ(machine_rom_call(&g_m, 0xC100, &r, 1000), MCALL_RETURNED);
    CHECK_EQ(r.a, 12);
    CHECK_EQ(g_m.cpu.pc, 0x9007);
    CHECK_EQ(g_m.cpu.s, s);

    load_main(0xC200);   /* a hook at $C200 calls the ROM routine at $C100 */
    CHECK(machine_hook(&g_m, 0xC200, hook_calls_rom, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x15);

    static const uint8_t spin[] = { 0x4C, 0x00, 0xC3 };   /* $C300: JMP $C300 */
    load_main(0xC100);
    put(0xC300, spin, sizeof spin);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    machine_regs r2 = { 0, 0, 0, g_m.cpu.p };
    CHECK_EQ(machine_rom_call(&g_m, 0xC300, &r2, 1000), MCALL_FAILED);   /* never returns: the budget ends it */

    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_fault, NULL));
    CHECK_EQ(machine_run(&g_m, 10000), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
}

static bool hook_finish(machine *m, void *user)
{
    (void)user;
    machine_clone(&g_c, m);
    CHECK(machine_finish_routine(&g_c, 1000));
    g_calls++;
    return false;
}

static void test_clone_and_finish(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_finish, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_c.cpu.pc, 0x9005);      /* returned to the STA after the JSR */
    CHECK_EQ(g_c.cpu.a, 0x14);
    CHECK_EQ(g_c.hook_count, 0);        /* a clone has no hooks */
    g_c.bus.write(g_c.bus.ctx, 0x20, 0x99);
    CHECK_EQ(g_c.ram[0x20], 0x99);
    CHECK_EQ(g_m.ram[0x20], 0x00);      /* the clone's bus writes the clone */
}

/* $9000: CLI / NOP / NOP / JMP $9003; timer IRQ vector -> $A000: JMP $A000 */
static void test_irq_hold(bool hold)
{
    static const uint8_t code[] = { 0x58, 0xEA, 0xEA, 0x4C, 0x03, 0x90 };
    static const uint8_t isr[] = { 0x4C, 0x00, 0xA0 };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    put(0xA000, isr, sizeof isr);
    vector(0xFFFE, 0x9000);
    vector(0xFFF8, 0xA000);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.sfr.icr2 = 0x30;   /* timer 3 interrupt enabled and requested */
    machine_hold_irqs(&g_m, hold);
    CHECK(machine_step(&g_m));   /* CLI */
    CHECK(machine_step(&g_m));   /* held: NOP; not held: the interrupt is taken */
    CHECK_EQ(g_m.cpu.pc, hold ? 0x9002 : 0xA000);
}

static void test_rom_call_real_rom(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(machine_init(&g_m, rom, len, NULL));
    machine_regs r = { 0x1F, 0, 0, g_m.cpu.p };
    CHECK_EQ(machine_rom_call(&g_m, 0xC656, &r, 1000), MCALL_RETURNED);   /* util_shl4: A = A * 16 */
    CHECK_EQ(r.a, 0xF0);
    CHECK(r.p & 0x01);                                   /* the last shift carried */
}

/* Boots a blank ROM with the P2 pins pulled to p2 (all inputs at reset, so the pins are p2). */
static void boot_p2(uint8_t p2)
{
    machine_config cfg;
    machine_default_config(&cfg);
    cfg.port_in[2] = p2;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, &cfg));
    CHECK_EQ(m50734_port_pins(&g_m.sfr, 2), p2);
}

static void check_map(uint16_t addr, machine_map_region want, uint32_t want_off)
{
    uint32_t off = 0xDEADBEEFu;
    machine_map_region got = machine_map_read(&g_m, addr, &off);
    if (got != want || ((want == MACHINE_MAP_RAM || want == MACHINE_MAP_ROM) && off != want_off))
        fprintf(stderr, "  map $%04X under P2=$%02X: region %d offset 0x%X\n", addr,
                m50734_port_pins(&g_m.sfr, 2), (int)got, (unsigned)off);
    CHECK_EQ(got, want);
    if (want == MACHINE_MAP_RAM || want == MACHINE_MAP_ROM)
        CHECK_EQ(off, want_off);
}

/* The shared address mapper (M4.0 spec sections 2.2, 5.3) under the P2 states of spec section 1.2: bank bits
   $7C select the main ROM ($70) or an option socket, bit 7 maps $4000-$7FFF to file 0x0000-0x3FFF. */
static void test_mapper(void)
{
    static const uint8_t main_states[] = { 0x70, 0x73 };   /* bits 0-1 (DSP reset, ...) do not bank */
    static const uint8_t option_states[] = { 0x6C, 0x3C, 0x00 };
    memset(g_rom, 0, sizeof g_rom);
    g_rom[0x0100] = 0x11;
    g_rom[0x4100] = 0x22;
    g_rom[0xC100] = 0x33;
    for (size_t i = 0; i < sizeof main_states; i++) {
        boot_p2(main_states[i]);   /* main ROM, window off */
        check_map(0x0000, MACHINE_MAP_RAM, 0x0000);
        check_map(0x00D9, MACHINE_MAP_RAM, 0x00D9);
        check_map(0x00DA, MACHINE_MAP_SFR, 0);
        check_map(0x00FF, MACHINE_MAP_SFR, 0);
        check_map(0x0100, MACHINE_MAP_RAM, 0x0100);
        check_map(0x3FFF, MACHINE_MAP_RAM, 0x3FFF);
        check_map(0x4000, MACHINE_MAP_ROM, 0x4000);
        check_map(0x7FFF, MACHINE_MAP_ROM, 0x7FFF);
        check_map(0x8000, MACHINE_MAP_ROM, 0x8000);
        check_map(0xFFFF, MACHINE_MAP_ROM, 0xFFFF);
        CHECK_EQ(machine_peek(&g_m, 0x4100), 0x22);
        CHECK_EQ(machine_peek(&g_m, 0xC100), 0x33);

        boot_p2((uint8_t)(main_states[i] | 0x80));   /* main ROM, lower-ROM window on */
        check_map(0x3FFF, MACHINE_MAP_RAM, 0x3FFF);
        check_map(0x4000, MACHINE_MAP_ROM, 0x0000);
        check_map(0x4100, MACHINE_MAP_ROM, 0x0100);
        check_map(0x7FFF, MACHINE_MAP_ROM, 0x3FFF);
        check_map(0x8000, MACHINE_MAP_ROM, 0x8000);
        CHECK_EQ(machine_peek(&g_m, 0x4100), 0x11);
        CHECK_EQ(g_m.lower_window_reads, 0);    /* the mapper and peek count nothing */
        CHECK_EQ(g_m.bus.read(g_m.bus.ctx, 0x4100), 0x11);
        CHECK_EQ(g_m.lower_window_reads, 1);    /* a bus read through the window does */
    }
    for (size_t i = 0; i < sizeof option_states; i++) {
        for (int window = 0; window < 2; window++) {
            boot_p2((uint8_t)(option_states[i] | (window ? 0x80 : 0)));   /* an (empty) option socket */
            check_map(0x0100, MACHINE_MAP_RAM, 0x0100);
            check_map(0x00F0, MACHINE_MAP_SFR, 0);
            check_map(0x4100, MACHINE_MAP_ROM, window ? 0x0100 : 0x4100);
            check_map(0x8000, MACHINE_MAP_EMPTY, 0);
            check_map(0xC100, MACHINE_MAP_EMPTY, 0);
            check_map(0xFFFF, MACHINE_MAP_EMPTY, 0);
            CHECK_EQ(machine_peek(&g_m, 0xC100), 0xFF);
            CHECK_EQ(g_m.bus.read(g_m.bus.ctx, 0xC100), 0xFF);
        }
    }
}

static machine_call_result g_outer, g_inner;

/* $C100 hooked: calls $C200 in ROM; $C200: JSR $C300 / RTS. */
static bool hook_outer(machine *m, void *user)
{
    (void)user;
    machine_regs r = { 0, 0, 0, m->cpu.p };
    g_outer = machine_rom_call(m, 0xC200, &r, 1000);
    if (g_outer == MCALL_RETURNED)
        machine_return(m);
    return true;   /* unwound: the machine goes on where the ROM went */
}

/* $C300 hooked: calls $C400 in ROM, which resets the stack: LDX #$FF / TXS / JMP $9007. */
static bool hook_inner(machine *m, void *user)
{
    (void)user;
    machine_regs r = { 0, 0, 0, m->cpu.p };
    g_inner = machine_rom_call(m, 0xC400, &r, 1000);
    if (g_inner == MCALL_RETURNED)
        machine_return(m);
    return true;
}

/* A ROM call whose routine lifts SP above the call's frame (a stack reset) can never return: it reports
   MCALL_UNWOUND at once, with PC and SP where the routine left them (M4.2 spec 3). A hook inside an outer
   rom_call that unwinds unwinds the outer call too. */
static void test_rom_call_unwound(void)
{
    static const uint8_t reset[] = { 0xA2, 0xFF, 0x9A, 0x4C, 0x07, 0x90 };   /* LDX #$FF / TXS / JMP $9007 */
    static const uint8_t call_c300[] = { 0x20, 0x00, 0xC3, 0x60 };            /* JSR $C300 / RTS */

    /* direct: a hook at $C100 calls $C400 */
    load_main(0xC100);
    put(0xC400, reset, sizeof reset);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_inner = MCALL_FAILED;
    CHECK(machine_hook(&g_m, 0xC100, hook_inner, NULL));
    run_to(&g_m, 0xC100);
    CHECK(machine_step(&g_m));                   /* the hook runs; its ROM call unwinds */
    CHECK_EQ(g_inner, MCALL_UNWOUND);
    CHECK_EQ(g_m.cpu.pc, 0xC403);                /* right after the TXS */
    CHECK_EQ(g_m.cpu.s, 0xFF);
    run_to(&g_m, 0x9007);                        /* and the machine runs on from there */
    CHECK_EQ(g_m.ram[0x10], 0x00);               /* the STA after the JSR never ran */

    /* nested: hook $C100 -> ROM $C200 -> JSR $C300 hooked -> ROM $C400 resets the stack */
    load_main(0xC100);
    put(0xC200, call_c300, sizeof call_c300);
    put(0xC400, reset, sizeof reset);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_outer = g_inner = MCALL_FAILED;
    CHECK(machine_hook(&g_m, 0xC100, hook_outer, NULL));
    CHECK(machine_hook(&g_m, 0xC300, hook_inner, NULL));
    run_to(&g_m, 0xC100);
    CHECK(machine_step(&g_m));
    CHECK_EQ(g_inner, MCALL_UNWOUND);
    CHECK_EQ(g_outer, MCALL_UNWOUND);            /* the outer call sees the same reset */
    CHECK_EQ(g_m.cpu.pc, 0xC403);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The emulator answers the firmware's SFR reads and writes (M4.6a spec 3.1) through the CPU's bus, side effects
   included: UART_STATUS $E7 is transmitter ready (bit 1, always) and receive full (bit 0), and reading it changes
   nothing; a read of UART_RX $E5 clears receive full; a write of UART_TX $E4 is a transmitted byte. SEB and CLB on P0
   read the pins, so an input pin's level reaches the latch, as the CPU's SEB and CLB do; BBS reads the pin. */
static void test_host_sfr(void)
{
    static fw_bridge b;
    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x02);
    CHECK(m50734_uart_receive(&g_m.sfr, 0x41));
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x03);
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x03);   /* still full: reading the status cleared nothing */
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_RX), 0x41);
    CHECK(!g_m.sfr.uart_rx_full);                        /* the read of the byte did */
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x02);
    fw_wr_sfr8(&b.f, M50734_UART_TX, 0x4D);
    CHECK_EQ(g_m.sfr.uart_tx_count, 1);
    CHECK_EQ(g_m.sfr.uart_tx_last, 0x4D);
    g_m.sfr.port_dir[0] = 0x78;                          /* serial mode (boot_init_ports): P0.0-2 and P0.7 inputs */
    g_m.sfr.port_in[0] = 0x04;                           /* the host line P0.2 high */
    fw_sfr_seb(&b.f, M50734_P0, 5);
    CHECK_EQ(g_m.sfr.port_latch[0], 0x24);               /* the pins ($04) read, bit 5 set, written to the latch */
    fw_sfr_clb(&b.f, M50734_P0, 2);
    CHECK_EQ(g_m.sfr.port_latch[0], 0x20);
    CHECK(fw_sfr_bit(&b.f, M50734_P0, 2));               /* an input: the pin, not the latch */
    CHECK(fw_sfr_bit(&b.f, M50734_P0, 5));               /* an output: the latch */
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(!b.f.fault);
}

#define SYN_ISR   0xC600   /* the synthetic timer interrupt handler */
#define SYN_LOOP  0xC700   /* a synthetic BBC3 ICR2 loop */

/* Timer 2 running with a 400-cycle period (prescaler 0, reload 99: 4 x 1 x 100), its interrupt enabled (ICR2 bit 2)
   or not, written through the bus as the CPU's LDM writes them; its request bit 3 clear. */
static void timer2_on(machine *m, bool irq)
{
    m->bus.write(m->bus.ctx, 0xDE, 0);
    m->bus.write(m->bus.ctx, 0xDF, 99);
    m->bus.write(m->bus.ctx, M50734_ICR2, irq ? 0x04 : 0x00);
}

/* The firmware's hardware wait (M4.6a spec 3.3, M4.6b spec 3.2): FW_WAIT_UART_TX runs ser_tx_wait at $C500 (BBC1
   UART_STATUS,$C500 / RTS, here in the synthetic ROM) on the emulated CPU through a ROM call: A, X, Y, SP and the PC
   as they were, the loop's cycles spent (BBC1 not taken, the transmitter being ready, then RTS). Once the bridge
   deadline has passed it fails, and fw_wait faults at $C500. FW_WAIT_TIMER2 and FW_WAIT_TIMER3 run the synthetic
   loops at $B372 (BBC3 ICR2) and $B37F (BBC5 ICR2) until the timer's request bit; FW_WAIT_INTERRUPT returns once
   the timer interrupt's handler has run; each with the PC and SP as they were. A deadline that passes in the wait
   ends it with a fault at the loop given. */
static void test_host_wait(void)
{
    static fw_bridge b;
    static const uint8_t tx_wait[] = { 0x37, 0xE7, 0xFD, 0x60 };   /* $C500: BBC1 $E7,$C500 / RTS */
    load_main(0xC100);
    put(FW_WAIT_UART_TX_LOOP, tx_wait, sizeof tx_wait);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    g_m.cpu.a = 0x11;
    g_m.cpu.x = 0x22;
    g_m.cpu.y = 0x33;
    uint64_t c0 = g_m.cpu.cycles;
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.a, 0x11);
    CHECK_EQ(g_m.cpu.x, 0x22);
    CHECK_EQ(g_m.cpu.y, 0x33);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.cycles - c0, 11);   /* BBC1 zp not taken (5), RTS (6) */

    b.deadline = g_m.cpu.cycles;         /* passed: the wait fails without running an instruction */
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_m.cpu.cycles - c0, 11);

    static const uint8_t t2_loop[] = { 0x77, 0xFE, 0xFD };   /* $B372: BBC3 ICR2,$B372 */
    static const uint8_t t3_loop[] = { 0xB7, 0xFE, 0xFD };   /* $B37F: BBC5 ICR2,$B37F */
    static const uint8_t isr[] = { 0x7F, 0xFE, 0xE6, 0x20, 0x40 };
    put(FW_WAIT_TIMER2_LOOP, t2_loop, sizeof t2_loop);
    put(FW_WAIT_TIMER3_LOOP, t3_loop, sizeof t3_loop);
    put(SYN_ISR, isr, sizeof isr);
    vector(0xFFF8, SYN_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    timer2_on(&g_m, false);
    fw_wait(&b.f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(!b.f.fault);
    CHECK(g_m.sfr.icr2 & 0x08);
    g_m.bus.write(g_m.bus.ctx, 0xE0, 0);         /* timer 3: prescaler 0, reload $28 (164 cycles) */
    g_m.bus.write(g_m.bus.ctx, 0xE1, 0x28);
    fw_wait(&b.f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP);
    CHECK(!b.f.fault);
    CHECK(g_m.sfr.icr2 & 0x20);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    g_m.bus.write(g_m.bus.ctx, M50734_ICR2, 0x04);   /* timer 2's interrupt alone */
    g_m.cpu.p = 0;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xB877);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.ram[0x20], 1);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);

    g_m.sfr.timer_running[1] = false;            /* no interrupt comes: the deadline ends the wait */
    b.deadline = g_m.cpu.cycles + 500;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xB877);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, 0xB877);
    CHECK(g_m.cpu.cycles >= b.deadline);
    CHECK(g_m.cpu.cycles < b.deadline + 8);
}

/* machine_return_interrupt is the CPU's RTI (M4.6a spec 3.2): from the same interrupt frame (the PC $9007, then P with
   N, Z and C set and B clear, as take_irq pushes them), an RTI executed at $C100 and the call leave the same PC, SP
   and P (B set, I as pulled). */
static void test_return_interrupt(void)
{
    static const uint8_t rti[] = { 0x40 };
    load_main(0xC100);
    put(0xC100, rti, sizeof rti);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.ram[0x1FF] = 0x90;
    g_m.ram[0x1FE] = 0x07;
    g_m.ram[0x1FD] = CPU740_N | CPU740_Z | CPU740_C;
    g_m.cpu.s = 0xFC;
    g_m.cpu.p = CPU740_I;
    g_m.cpu.pc = 0xC100;
    machine_clone(&g_c, &g_m);
    CHECK(machine_step(&g_m));
    machine_return_interrupt(&g_c);
    CHECK_EQ(g_m.cpu.pc, 0x9007);
    CHECK_EQ(g_c.cpu.pc, 0x9007);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_c.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_Z | CPU740_C | CPU740_B);
    CHECK_EQ(g_c.cpu.p, g_m.cpu.p);
}

static unsigned g_frames;
static uint8_t g_frame[APOLLO_FRAME_BYTES];

static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    (void)user;
    g_frames++;
    memcpy(g_frame, frame, APOLLO_FRAME_BYTES);
}

/* machine_capture_frame (M4.6b spec 3.3) is the capture the step loop makes at $B306: the 20 bytes at the $2C/$2D
   pointer to the frame callback, frames_produced counted. */
static void test_capture_frame(void)
{
    static const uint8_t nop[] = { 0xEA };
    load_main(0xC100);
    put(APOLLO_SWAP_PC, nop, sizeof nop);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        g_m.ram[0x16B9 + i] = (uint8_t)(0x40 + i);
    g_m.ram[APOLLO_PRODUCER_PTR] = 0xB9;
    g_m.ram[APOLLO_PRODUCER_PTR + 1] = 0x16;
    machine_clone(&g_c, &g_m);
    machine_set_outputs(&g_m, on_frame, NULL);
    machine_set_outputs(&g_c, on_frame, NULL);
    g_frames = 0;
    g_m.cpu.pc = APOLLO_SWAP_PC;
    CHECK(machine_step(&g_m));                   /* the step at $B306 captures, then runs the NOP */
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_m.frames_produced, 1);
    uint8_t first[APOLLO_FRAME_BYTES];
    memcpy(first, g_frame, sizeof first);
    machine_capture_frame(&g_c);
    CHECK_EQ(g_frames, 2);
    CHECK_EQ(g_c.frames_produced, 1);
    CHECK(memcmp(first, g_frame, sizeof first) == 0);
    CHECK_EQ(first[0], 0x40);
    CHECK_EQ(first[19], 0x53);
    CHECK_EQ(g_c.cpu.pc, 0x9000);                /* nothing executed */
}

/* machine_run_loop (M4.6b spec 3.3): BBC3 ICR2 at $C700 runs with I set until timer 2 sets its request bit, so the
   timer's interrupt, enabled with I clear before, is not taken (SP unchanged); then the PC and P are as they were.
   A loop that cannot end fails after its budget, the PC and P put back all the same. */
static void test_run_loop(void)
{
    static const uint8_t loop[] = { 0x77, 0xFE, 0xFD, 0xEA };   /* $C700: BBC3 ICR2,$C700 / NOP */
    load_main(0xC100);
    put(SYN_LOOP, loop, sizeof loop);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    timer2_on(&g_m, true);
    g_m.cpu.p = CPU740_C;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(machine_run_loop(&g_m, SYN_LOOP, SYN_LOOP + 3, 10000));
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.p, CPU740_C);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK(g_m.sfr.icr2 & 0x08);
    CHECK(g_m.cpu.cycles - c0 >= 400);
    CHECK(g_m.cpu.cycles - c0 < 413);            /* BBC3 taken 7 cycles a pass, not taken 5 */

    g_m.sfr.timer_running[1] = false;            /* nothing sets the bit any more */
    g_m.sfr.icr2 = 0;
    c0 = g_m.cpu.cycles;
    CHECK(!machine_run_loop(&g_m, SYN_LOOP, SYN_LOOP + 3, 1000));
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.p, CPU740_C);
    CHECK(g_m.cpu.cycles - c0 >= 1000);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* A replacement of the synthetic handler: counts, clears the request and exits like RTI. */
static bool hook_isr_c(machine *m, void *user)
{
    (void)user;
    g_calls++;
    m->sfr.icr2 &= (uint8_t)~0x08;
    machine_return_interrupt(m);
    return true;
}

/* machine_wait_interrupt (M4.6b spec 3.3): as a WIT at $9000, the CPU idles until timer 2's interrupt; its handler
   ($C600: CLB3 ICR2 / INC $20 / RTI) runs and the call returns with the PC and SP back, the CPU no longer waiting.
   A byte waiting in the serial queue is not delivered (only the idle WIT $C602 delivers). With the handler replaced
   by a hook the same, the ROM handler not run. Without an interrupt it fails after its budget; with I set it fails
   at once (the interrupt could never be taken). */
static void test_wait_interrupt(void)
{
    static const uint8_t isr[] = { 0x7F, 0xFE, 0xE6, 0x20, 0x40 };   /* CLB3 ICR2 / INC $20 / RTI */
    static const uint8_t byte[] = { 0x41 };
    load_main(0xC100);
    put(SYN_ISR, isr, sizeof isr);
    vector(0xFFF8, SYN_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(machine_feed_serial(&g_m, byte, sizeof byte));
    timer2_on(&g_m, true);
    g_m.cpu.p = 0;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(machine_wait_interrupt(&g_m, 10000));
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.ram[0x20], 1);
    CHECK(g_m.cpu.cycles - c0 >= 400);
    CHECK(!g_m.sfr.uart_rx_full);                /* no byte delivered */
    CHECK(g_m.q_head != g_m.q_tail);

    g_calls = 0;
    CHECK(machine_hook(&g_m, SYN_ISR, hook_isr_c, NULL));
    CHECK(machine_wait_interrupt(&g_m, 10000));
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_m.ram[0x20], 1);                  /* the ROM handler did not run */
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);

    g_m.sfr.timer_running[1] = false;            /* no interrupt comes */
    g_m.sfr.icr2 = 0x04;
    c0 = g_m.cpu.cycles;
    CHECK(!machine_wait_interrupt(&g_m, 1000));
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK(g_m.cpu.cycles - c0 >= 1000);
    CHECK(g_m.cpu.cycles - c0 < 1008);

    g_m.sfr.icr2 = 0x0C;                         /* requested and enabled, but masked */
    g_m.cpu.p = CPU740_I;
    c0 = g_m.cpu.cycles;
    CHECK(!machine_wait_interrupt(&g_m, 1000));
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

#define SYN_RX_ISR  0xC680   /* the synthetic receive interrupt handler */

static unsigned g_idles;

/* An idle handler that feeds 'A' at its first call and stops the run at every later one. */
static machine_idle_action idle_feed_once(machine *m, void *user)
{
    static const uint8_t byte[] = { 'A' };
    (void)user;
    if (g_idles++ > 0)
        return MACHINE_STOP;
    CHECK(machine_feed_serial(m, byte, sizeof byte));
    return MACHINE_FEED;
}

/* $9000 as load_main, the UART's receive interrupt enabled (UART_CTRL bit 4) and its handler at $C680 (PHA /
   LDA UART_RX / STA $21 / INC $22 / PLA / RTI) on vec_int1_serial $FFFA, I clear. */
static void load_rx(void)
{
    static const uint8_t rx_isr[] = { 0x48, 0xA5, 0xE5, 0x85, 0x21, 0xE6, 0x22, 0x68, 0x40 };
    load_main(0xC100);
    put(SYN_RX_ISR, rx_isr, sizeof rx_isr);
    vector(0xFFFA, SYN_RX_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.bus.write(g_m.bus.ctx, M50734_UART_CTRL, 0x10);
    g_m.cpu.p = 0;
    g_idles = 0;
}

/* machine_wait_input (M4.6c2 spec 3.1): the CPU waits at the idle point $C603. With nothing left to do there the idle
   handler runs: it feeds a byte, which is delivered there, and the receive interrupt's handler runs; the wait returns
   with the PC and SP back. At the next idle the handler stops the run: the machine stays at the idle point (waiting),
   machine_run returns MACHINE_IDLE there, and the handler is not called again. A machine that never idles (timer 3
   enabled) and takes no interrupt fails after its budget; a line that wakes the CPU with I set fails at once. A clone
   has no idle handler. */
static void test_wait_input(void)
{
    static const uint8_t byte[] = { 'B' };
    load_rx();
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    CHECK_EQ(machine_wait_input(&g_m, 10000), MINPUT_HANDLED);
    CHECK_EQ(g_idles, 1);
    CHECK_EQ(g_m.ram[0x21], 'A');
    CHECK_EQ(g_m.ram[0x22], 1);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK(!g_m.cpu.waiting);
    machine_clone(&g_c, &g_m);
    CHECK(g_c.idle_fn == NULL);

    CHECK_EQ(machine_wait_input(&g_m, 10000), MINPUT_STOPPED);
    CHECK_EQ(g_idles, 2);
    CHECK_EQ(g_m.cpu.pc, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_IDLE);
    CHECK_EQ(machine_idle(&g_m), MACHINE_STOP);
    CHECK_EQ(g_idles, 2);

    load_rx();
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    g_m.sfr.icr2 = 0x10;                         /* timer 3 enabled (not running): never idle */
    uint64_t c0 = g_m.cpu.cycles;
    CHECK_EQ(machine_wait_input(&g_m, 1000), MINPUT_FAILED);
    CHECK(g_m.cpu.cycles - c0 >= 1000);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_idles, 0);

    g_m.sfr.icr2 = 0;
    CHECK(machine_feed_serial(&g_m, byte, sizeof byte));   /* delivered at the idle point: the line wakes the CPU */
    g_m.cpu.p = CPU740_I;
    c0 = g_m.cpu.cycles;
    CHECK_EQ(machine_wait_input(&g_m, 1000), MINPUT_FAILED);
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK(g_m.sfr.uart_rx_full);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The delivery guard (M4.6c2 spec 3.1, the M4.6b deferred item): machine_wait_interrupt refuses the idle WIT $C602
   and the idle point $C603, running nothing, so a byte in the serial queue is never delivered by it. */
static void test_delivery_guard(void)
{
    static const uint8_t byte[] = { 'C' };
    load_rx();
    CHECK(machine_feed_serial(&g_m, byte, sizeof byte));
    for (uint16_t pc = 0xC602; pc <= 0xC603; pc++) {
        uint64_t c0 = g_m.cpu.cycles;
        g_m.cpu.pc = pc;
        CHECK(!machine_wait_interrupt(&g_m, 1000));
        CHECK_EQ(g_m.cpu.cycles, c0);
        CHECK_EQ(g_m.cpu.pc, pc);
        CHECK(!g_m.cpu.waiting);
        CHECK(!g_m.sfr.uart_rx_full);
    }
}

/* The phase deadline in the UART wait and in a ROM call from C (M4.6c2 spec 3.2), under an outermost entry row
   (b->phase): the ROM engine at the same point would just be running when machine_run's budget ends, so reaching the
   deadline ends the run (f->stopped, no fault, the machine left at the ROM loop, or at the call's address when the
   deadline had passed before it started), where without b->phase, or before the deadline, a failed call is a fault. */
static void test_phase_calls(void)
{
    static const uint8_t spin[] = { 0x80, 0xFE };   /* BRA * : ser_tx_wait's loop that never ends */
    static fw_bridge b;
    fw_regs r = { 0, 0, 0, 0 };
    load_main(0xC100);
    put(FW_WAIT_UART_TX_LOOP, spin, sizeof spin);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    b.phase = true;
    run_to(&g_m, 0x9002);   /* cycles > 0: a phase deadline of 0 is none */

    g_m.phase_deadline = g_m.cpu.cycles;   /* passed before the wait: stopped without running */
    g_m.cpu.pc = 0x9000;
    uint64_t c0 = g_m.cpu.cycles;
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(b.f.stopped);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK_EQ(g_m.cpu.pc, FW_WAIT_UART_TX_LOOP);
    CHECK(!g_m.cpu.waiting);

    b.f.stopped = false;                   /* reached during the wait */
    g_m.phase_deadline = g_m.cpu.cycles + 200;
    g_m.cpu.pc = 0x9000;
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(b.f.stopped);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    CHECK(g_m.cpu.cycles >= g_m.phase_deadline);
    CHECK(g_m.cpu.cycles < g_m.phase_deadline + 8);
    CHECK_EQ(g_m.cpu.pc, FW_WAIT_UART_TX_LOOP);

    for (int unhooked = 0; unhooked <= 1; unhooked++) {   /* a ROM call from C, hooked or not */
        fw_call_result (*call)(void *, uint16_t, fw_regs *, uint64_t) =
            unhooked ? b.host.rom_call_unhooked : b.host.rom_call;
        b.f.stopped = false;
        g_m.phase_deadline = g_m.cpu.cycles;
        g_m.cpu.pc = 0x9000;
        c0 = g_m.cpu.cycles;
        CHECK_EQ(call(b.host.ctx, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET), FW_CALL_FAILED);
        CHECK(b.f.stopped);
        CHECK_EQ(g_m.fault, MFAULT_NONE);
        CHECK_EQ(g_m.cpu.cycles, c0);
        CHECK_EQ(g_m.cpu.pc, FW_WAIT_UART_TX_LOOP);   /* not at the hook's address */

        b.f.stopped = false;
        g_m.phase_deadline = g_m.cpu.cycles + 200;
        g_m.cpu.pc = 0x9000;
        CHECK_EQ(call(b.host.ctx, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET), FW_CALL_FAILED);
        CHECK(b.f.stopped);
        CHECK_EQ(g_m.fault, MFAULT_NONE);
        CHECK(g_m.cpu.cycles >= g_m.phase_deadline);
        CHECK_EQ(g_m.cpu.pc, FW_WAIT_UART_TX_LOOP);

        b.f.stopped = false;                   /* the call's own budget ends before the deadline: a plain failure */
        g_m.phase_deadline = g_m.cpu.cycles + 100000;
        CHECK_EQ(call(b.host.ctx, FW_WAIT_UART_TX_LOOP, &r, 100), FW_CALL_FAILED);
        CHECK(!b.f.stopped);
        CHECK(g_m.cpu.cycles < g_m.phase_deadline);
    }

    b.f.stopped = false;                   /* the same failures without b->phase are faults */
    b.phase = false;
    b.deadline = g_m.cpu.cycles + 200;
    g_m.cpu.pc = 0x9000;
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, FW_WAIT_UART_TX_LOOP);
    CHECK(!b.f.stopped);
    b.f.fault = false;
    b.deadline = g_m.cpu.cycles + 200;
    CHECK_EQ(b.host.rom_call(b.host.ctx, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET), FW_CALL_FAILED);
    CHECK(!b.f.stopped);
}

/* The bridge's input wait (M4.6c2 spec 3.1): isr_idle's PHA of the machine's A, the wait at the idle point, the PLA:
   SP and the PC back. Stopped: f->stopped, no fault, the machine at the idle point with A pushed, as the ROM's isr_idle
   leaves it. Under an outermost entry row (b->phase) a wait lasts until the engine's phase deadline, past
   FW_ROM_CALL_BUDGET, and one that reaches it stops the run (no fault; M4.6c2 spec 3.2), the machine left at the wait's
   loop (not waiting); so does one made after it has passed. Without phase, the bridge deadline is a fault as before.
   The CPU calls: reset_cpu sets I and SP $FF and clears T, D, C and V; set_interrupts clears or sets I. */
static void test_host_input(void)
{
    static fw_bridge b;
    load_rx();
    CHECK(fw_bridge_init(&b, &g_m));
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    g_m.cpu.a = 0x5A;
    fw_wait(&b.f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(!b.f.fault);
    CHECK(!b.f.stopped);
    CHECK_EQ(g_m.ram[0x21], 'A');
    CHECK_EQ(g_m.ram[0x1FF], 0x5A);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    fw_wait(&b.f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(b.f.stopped);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.pc, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(g_m.cpu.s, 0xFE);
    CHECK_EQ(g_m.ram[0x1FF], 0x5A);

    load_rx();
    CHECK(fw_bridge_init(&b, &g_m));
    b.phase = true;
    g_m.phase_deadline = g_m.cpu.cycles + 2 * FW_ROM_CALL_BUDGET;   /* no interrupt comes: the deadline ends the wait */
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xA6C2);
    CHECK(b.f.stopped);
    CHECK(!b.f.fault);
    CHECK(g_m.cpu.cycles >= g_m.phase_deadline);
    CHECK(g_m.cpu.cycles < g_m.phase_deadline + 8);
    CHECK_EQ(g_m.cpu.pc, 0xA6C2);
    CHECK(!g_m.cpu.waiting);
    b.f.stopped = false;
    g_m.cpu.pc = 0x9000;
    uint64_t c0 = g_m.cpu.cycles;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xA6C2);   /* passed: stopped without running */
    CHECK(b.f.stopped);
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK_EQ(g_m.cpu.pc, 0xA6C2);
    g_m.cpu.pc = 0x9000;
    b.f.stopped = false;
    b.phase = false;
    b.deadline = g_m.cpu.cycles + 500;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xA6C2);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, 0xA6C2);
    CHECK(!b.f.stopped);

    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    g_m.cpu.p = 0xFF & ~CPU740_I;
    g_m.cpu.s = 0x80;
    fw_reset_cpu(&b.f);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_B | CPU740_I | CPU740_Z);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    fw_set_interrupts(&b.f, true, 0xA66E);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_B | CPU740_Z);
    fw_set_interrupts(&b.f, false, 0xA6E7);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_B | CPU740_I | CPU740_Z);
    CHECK(!b.f.fault);
}

/* The isr_idle thunk (M4.6c2 spec 3.1) entered by $9002 JSR $C5FE on a synthetic machine (load_rx's receive
   interrupt, boot_dip $10 = 0: serial mode). The C waits for input at the idle point; fed, the byte's interrupt is
   handled and the thunk returns as the RTS would, A kept (the STA $10 after it stores 5). Stopped: the run ends there:
   the machine stays at the idle point $C603, waiting, the JSR's return address and A pushed (SP $FC), one stop
   counted, the bridge's trap and deadline clear; machine_run then returns MACHINE_IDLE. */
static void test_isr_idle_thunk(void)
{
    static fw_bridge b;
    static const uint8_t rx_isr[] = { 0x48, 0xA5, 0xE5, 0x85, 0x21, 0xE6, 0x22, 0x68, 0x40 };
    load_main(ROM_ISR_IDLE);
    put(SYN_RX_ISR, rx_isr, sizeof rx_isr);
    vector(0xFFFA, SYN_RX_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.bus.write(g_m.bus.ctx, M50734_UART_CTRL, 0x10);
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(fw_install(&g_m, &b, "isr_idle"));
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    g_idles = 0;
    g_m.cpu.p = 0;
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_idles, 1);
    CHECK_EQ(g_m.ram[0x21], 'A');
    CHECK_EQ(g_m.ram[0x10], 0x05);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(b.stops, 0);

    g_m.ram[0x10] = 0;                           /* boot_dip: serial mode again */
    g_m.cpu.pc = 0x9000;
    run_to(&g_m, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(g_idles, 2);
    CHECK_EQ(g_m.cpu.s, 0xFC);
    CHECK_EQ(g_m.ram[0x1FD], 0x05);
    CHECK_EQ(b.stops, 1);
    CHECK(!b.f.stopped);
    CHECK(b.f.trap == NULL);
    CHECK_EQ(b.deadline, 0);
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_IDLE);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
static void test_host_dsp(void)
{
    static fw_bridge b;
    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    machine_set_outputs(&g_m, on_frame, NULL);
    g_frames = 0;
    fw_dsp_write(&b.f, 0x12);
    fw_dsp_write(&b.f, 0x34);
    CHECK_EQ(g_m.port_writes, 2);
    CHECK_EQ(g_m.raw_pos, 2);
    CHECK_EQ(g_m.raw_buf[0], 0x12);
    CHECK_EQ(g_m.raw_buf[1], 0x34);
    g_m.ram[0x1700] = 0x77;
    g_m.ram[APOLLO_PRODUCER_PTR] = 0x00;
    g_m.ram[APOLLO_PRODUCER_PTR + 1] = 0x17;
    fw_frame_done(&b.f);
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_m.frames_produced, 1);
    CHECK_EQ(g_frame[0], 0x77);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.unmodelled_count, 0);
}

static int g_count_a, g_count_b;

/* Counts in *user, then performs the routine: A = $77 and RTS. */
static bool hook_count_replace(machine *m, void *user)
{
    (*(int *)user)++;
    m->cpu.a = 0x77;
    machine_return(m);
    return true;
}

/* machine_hook_ramcode (M4.6c1 spec 3.3): a hook at a RAM-code address ($0200-$03FE) fires while the CPU executes
   the RAM there ($9000 JSR $0250, $0250: ASL A / ASL A / RTS in RAM); a main-ROM hook (machine_hook) at a RAM address
   never fires; addresses outside $0200-$03FE are refused, as is a second hook at a hooked address. */
static void test_hook_ramcode(void)
{
    static const uint8_t shl2[] = { 0x0A, 0x0A, 0x60 };
    load_main(0x0250);
    memcpy(g_m.ram + 0x0250, shl2, sizeof shl2);
    CHECK(machine_hook_ramcode(&g_m, 0x0250, hook_replace, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x77);
    CHECK_EQ(g_calls, 1);

    load_main(0x0250);
    memcpy(g_m.ram + 0x0250, shl2, sizeof shl2);
    CHECK(machine_hook(&g_m, 0x0250, hook_replace, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x14);               /* the RAM routine ran: the main-ROM hook did not fire */
    CHECK_EQ(g_calls, 0);

    load_main(0xC100);
    CHECK(!machine_hook_ramcode(&g_m, 0x01FF, hook_replace, NULL));
    CHECK(!machine_hook_ramcode(&g_m, 0x03FF, hook_replace, NULL));
    CHECK(!machine_hook_ramcode(&g_m, 0xC100, hook_replace, NULL));
    CHECK(machine_hook_ramcode(&g_m, MACHINE_RAMCODE_FIRST, hook_replace, NULL));
    CHECK(machine_hook_ramcode(&g_m, MACHINE_RAMCODE_LAST, hook_replace, NULL));
    CHECK(!machine_hook_ramcode(&g_m, MACHINE_RAMCODE_LAST, hook_replace, NULL));
    CHECK(!machine_hook(&g_m, MACHINE_RAMCODE_FIRST, hook_replace, NULL));
    CHECK_EQ(g_m.hook_count, 2);
}

/* machine_rom_call_unhooked (M4.6c1 spec 3.3): the hook at the called address is skipped at the routine's first
   instruction only. $C200: JSR $C100 / RTS; $C300: DEX / BEQ $C306 / JSR $C300 / RTS (it enters itself while X is
   not 0). The skipped hook does not fire, the hook at $C100 does; $C300's own hook fires on its second entry; the
   skip is gone after the call, and a plain machine_rom_call fires the hook. */
static void test_rom_call_unhooked(void)
{
    static const uint8_t call_c100[] = { 0x20, 0x00, 0xC1, 0x60 };
    static const uint8_t self[] = { 0xCA, 0xF0, 0x03, 0x20, 0x00, 0xC3, 0x60 };
    load_main(0xC100);
    put(0xC200, call_c100, sizeof call_c100);
    put(0xC300, self, sizeof self);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_count_a = g_count_b = 0;
    CHECK(machine_hook(&g_m, 0xC100, hook_count_replace, &g_count_a));
    CHECK(machine_hook(&g_m, 0xC200, hook_count_replace, &g_count_b));
    machine_regs r = { 3, 0, 0, g_m.cpu.p };
    CHECK_EQ(machine_rom_call_unhooked(&g_m, 0xC100, &r, 1000), MCALL_RETURNED);
    CHECK_EQ(r.a, 12);                           /* ASL A / ASL A: the ROM routine ran */
    CHECK_EQ(g_count_a, 0);
    CHECK(!g_m.skip_hook);
    r.a = 3;
    CHECK_EQ(machine_rom_call_unhooked(&g_m, 0xC200, &r, 1000), MCALL_RETURNED);
    CHECK_EQ(g_count_b, 0);                      /* $C200's hook skipped ... */
    CHECK_EQ(g_count_a, 1);                      /* ... $C100's fired */
    CHECK_EQ(r.a, 0x77);
    r.a = 3;
    CHECK_EQ(machine_rom_call(&g_m, 0xC100, &r, 1000), MCALL_RETURNED);
    CHECK_EQ(g_count_a, 2);
    CHECK_EQ(r.a, 0x77);

    CHECK(machine_hook(&g_m, 0xC300, hook_count_replace, &g_count_b));
    machine_regs s = { 3, 2, 0, g_m.cpu.p };
    CHECK_EQ(machine_rom_call_unhooked(&g_m, 0xC300, &s, 1000), MCALL_RETURNED);
    CHECK_EQ(g_count_b, 1);                      /* the second entry of $C300 */
    CHECK_EQ(s.x, 1);
    CHECK_EQ(s.a, 0x77);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The bridge's bank calls (M4.6c1 spec 3.4): read_bank reads the bus as the CPU sees it (the main ROM while it is
   selected, $FF for an empty socket); fw_rom8 under another bank goes there; enter leaves the machine at the bank
   entry with SP $FF, I set, A and X; rom_call_unhooked runs the ROM routine past its own hook. */
static void test_host_bank(void)
{
    static fw_bridge b;
    load_main(0xC100);
    g_rom[0x9800] = 0x30;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK_EQ(b.host.read_bank(b.host.ctx, 0x9800), 0x30);   /* P2 pulled to $70: the main ROM */
    fw_wr_sfr8(&b.f, M50734_P2_DIR, 0xFF);
    fw_wr_sfr8(&b.f, M50734_P2, P2_BANK_5);
    CHECK_EQ(b.f.bank, P2_BANK_5);
    CHECK_EQ(b.host.read_bank(b.host.ctx, 0x9800), 0xFF);   /* an empty socket */
    CHECK_EQ(fw_rom8(&b.f, 0xC561), 0xFF);                  /* converted, but another bank is selected */
    fw_wr_sfr8(&b.f, M50734_P2, P2_MAIN_BANK);
    CHECK_EQ(fw_rom8(&b.f, 0xC561), '0');                   /* the converted image again */
    CHECK(!b.f.fault);

    g_m.cpu.s = 0xE0;
    g_m.cpu.p = 0;
    fw_enter(&b.f, BANK_ENTRY_WARM, 0x02, 0xFF);
    CHECK(b.f.unwound);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.pc, BANK_ENTRY_WARM);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.p, CPU740_I);
    CHECK_EQ(g_m.cpu.a, 0x02);
    CHECK_EQ(g_m.cpu.x, 0xFF);

    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    g_calls = 0;
    CHECK(machine_hook(&g_m, 0xC100, hook_replace, NULL));
    fw_regs r = { 3, 0, 0, 0 };
    CHECK(fw_rom_call_unhooked(&b.f, 0xC100, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(r.a, 12);
    CHECK_EQ(g_calls, 0);
    r.a = 3;
    CHECK(fw_rom_call(&b.f, 0xC100, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(r.a, 0x77);
    CHECK_EQ(g_calls, 1);
}

/* After a wait and after a ROM call the bridge reads the window and the bank back from the machine's P2 pins
   (M4.6b spec 3.4, M4.6c1 spec 3.1; the M4.6b deferred item: the read-back after a wait was untested). The timer
   interrupt's handler at $0300 (LDM #$F8,P2 / CLB3 ICR2 / RTI) selects bank $78 with the window on; a routine at
   $0310 (LDM #$70,P2 / RTS) selects the main bank with the window off. Both are in RAM and the wait is at $0320:
   an instruction in $8000-$FFFF under another bank would be a runaway PC. */
static void test_host_readback(void)
{
    static fw_bridge b;
    static const uint8_t isr_78[] = { 0x3C, 0xF8, 0xF0, 0x7F, 0xFE, 0x40 };
    static const uint8_t select_main[] = { 0x3C, 0x70, 0xF0, 0x60 };
    load_main(0xC100);
    vector(0xFFF8, 0x0300);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    memcpy(g_m.ram + 0x0300, isr_78, sizeof isr_78);
    memcpy(g_m.ram + 0x0310, select_main, sizeof select_main);
    CHECK(fw_bridge_init(&b, &g_m));
    fw_wr_sfr8(&b.f, M50734_P2_DIR, 0xFF);
    fw_wr_sfr8(&b.f, M50734_P2, P2_MAIN_BANK);
    CHECK(!b.f.low_window);
    CHECK_EQ(b.f.bank, P2_MAIN_BANK);
    timer2_on(&g_m, true);
    g_m.cpu.p = 0;
    g_m.cpu.pc = 0x0320;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0x0320);
    CHECK(!b.f.fault);
    CHECK(b.f.low_window);
    CHECK_EQ(b.f.bank, P2_BANK_5);

    fw_regs r = { 0, 0, 0, 0 };
    CHECK(fw_rom_call(&b.f, 0x0310, &r, FW_ROM_CALL_BUDGET));
    CHECK(!b.f.low_window);
    CHECK_EQ(b.f.bank, P2_MAIN_BANK);
}

/* The bridge's cycle deadline (thunks.h): a ROM call from C gets at most the cycles left before b->deadline, and
   none once it has passed; the call fails and fw_rom_call faults, as a call that does not return within its own
   budget does. 0 = no deadline. */
static void test_bridge_deadline(void)
{
    static fw_bridge b;
    static const uint8_t spin[] = { 0x4C, 0x00, 0xC3 };   /* $C300: JMP $C300 */
    load_main(0xC100);
    put(0xC300, spin, sizeof spin);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK_EQ(b.deadline, 0);
    fw_regs r = { 3, 0, 0, 0 };
    CHECK(fw_rom_call(&b.f, 0xC100, &r, FW_ROM_CALL_BUDGET));   /* no deadline: it returns */
    CHECK_EQ(r.a, 12);
    CHECK(!b.f.fault);

    uint64_t c0 = g_m.cpu.cycles;
    b.deadline = c0;   /* passed: the call fails without running an instruction */
    r.a = 3;
    CHECK(!fw_rom_call(&b.f, 0xC100, &r, FW_ROM_CALL_BUDGET));
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, 0xC100);
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK_EQ(g_m.fault, MFAULT_NONE);

    b.f.fault = false;
    b.deadline = g_m.cpu.cycles + 5000;   /* a spin with the speech budget ends at the deadline */
    CHECK(!fw_rom_call(&b.f, 0xC300, &r, FW_ROM_CALL_SPEECH_BUDGET));
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, 0xC300);
    CHECK(g_m.cpu.cycles >= b.deadline);
    CHECK(g_m.cpu.cycles < b.deadline + 8);   /* only the JMP that crossed it */
}

static uint64_t g_seen_deadline;

/* An observer at util_shl3 $C657, here an event handler in ROM: records the bridge's deadline at its first call. */
static bool hook_record_deadline(machine *m, void *user)
{
    const fw_bridge *b = user;
    (void)m;
    if (!g_seen_deadline)
        g_seen_deadline = b->deadline;
    return false;
}

/* A fresh real-ROM machine (the timer off: no interrupt comes and isr_frame_flag is never set) with the par_play thunk
   hooked, entered at $B877 as a JSR from $9000 would enter it. */
static void enter_par_play(const uint8_t *rom, size_t len, fw_bridge *b)
{
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(b, &g_m));
    CHECK(fw_install(&g_m, b, "par_play"));
    g_m.ram[0x100 + g_m.cpu.s--] = 0x90;   /* return address $9002 (pushed as $9002 - 1) */
    g_m.ram[0x100 + g_m.cpu.s--] = 0x01;
    g_m.cpu.pc = ROM_PAR_PLAY;
}

/* A waits thunk whose flag never comes faults instead of hanging. The outermost thunk sets the deadline (entry
   cycles + FW_ROM_CALL_SPEECH_BUDGET), seen by an event handler run in ROM ($C657, an event due at segment 0), and
   clears it when it ends: here par_play, with a mute pending, then goes straight to its end, and isr_wait_frame
   $B465 waits out its budget (FW_ROM_CALL_BUDGET, before the deadline). A deadline already set (as by an outer thunk)
   is left alone, and par_play's wait for an interrupt at its back edge (M4.6b) ends at it with a fault at $B877. */
static void test_waits_thunk_deadline(void)
{
    static fw_bridge b;
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");

    enter_par_play(rom, len, &b);
    CHECK(machine_hook(&g_m, 0xC657, hook_record_deadline, &b));
    g_m.ram[CMD_EV_COUNT] = 1;                    /* an event due at par_seg_index 0, its handler $C657 */
    g_m.ram[CMD_EV_HANDLERS] = 0x57;
    g_m.ram[CMD_EV_HANDLERS + 1] = 0xC6;
    g_m.ram[SER_ABORT] |= 1;
    g_seen_deadline = 0;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(!machine_step(&g_m));
    CHECK_EQ(g_seen_deadline, c0 + FW_ROM_CALL_SPEECH_BUDGET);
    CHECK(g_m.cpu.cycles >= c0 + FW_ROM_CALL_BUDGET);
    CHECK(g_m.cpu.cycles < c0 + FW_ROM_CALL_BUDGET + 100);
    CHECK_EQ(b.deadline, 0);   /* cleared by the outermost thunk */
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, ROM_ISR_WAIT_FRAME);
    CHECK_EQ(g_m.cpu.pc, ROM_PAR_PLAY);

    enter_par_play(rom, len, &b);
    uint64_t deadline = g_m.cpu.cycles + 300000;
    b.deadline = deadline;
    CHECK(!machine_step(&g_m));
    CHECK_EQ(b.deadline, deadline);   /* not the outermost: left alone */
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, ROM_PAR_PLAY);
    CHECK(g_m.cpu.cycles >= deadline);
    CHECK(g_m.cpu.cycles < deadline + 100);
    CHECK_EQ(g_m.cpu.pc, ROM_PAR_PLAY);
    CHECK(b.thunk_calls >= 1);
}

void suite_machine_hooks(void)
{
    test_mapper();
    test_hook_replaces_routine();
    test_observer_lets_rom_run();
    test_hook_table_full();
    test_stuck_hook_faults();
    test_hook_only_in_main_rom(0x70, 0x77, 1);   /* window off: $4100 is main ROM, the hook fires */
    test_hook_only_in_main_rom(0xF0, 0x14, 0);   /* window on: $4100 is file 0x0100, no hook */
    test_rom_call();
    test_rom_call_unwound();
    test_clone_and_finish();
    test_irq_hold(true);
    test_irq_hold(false);
    test_rom_call_real_rom();
    test_host_sfr();
    test_host_wait();
    test_return_interrupt();
    test_capture_frame();
    test_run_loop();
    test_wait_interrupt();
    test_host_dsp();
    test_bridge_deadline();
    test_waits_thunk_deadline();
    test_hook_ramcode();
    test_rom_call_unhooked();
    test_host_bank();
    test_host_readback();
    test_wait_input();
    test_phase_calls();
    test_delivery_guard();
    test_host_input();
    test_isr_idle_thunk();
}
