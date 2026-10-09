/* Hooks, ROM calls, the interrupt hold and machine_clone (M4.0 spec sections 3.2, 3.3, 5.1), on small
   programs in a synthetic ROM; one test calls a real ROM routine. */
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "machine/machine.h"
#include "engine/thunks.h"
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

/* The firmware's hardware wait (M4.6a spec 3.3): FW_WAIT_UART_TX runs ser_tx_wait at $C500 (BBC1 UART_STATUS,$C500 /
   RTS, here in the synthetic ROM) on the emulated CPU through a ROM call: A, X, Y, SP and the PC as they were, the
   loop's cycles spent (BBC1 not taken, the transmitter being ready, then RTS). Once the bridge deadline has passed
   it fails, and fw_wait faults at $C500. */
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
    fw_wait(&b.f, FW_WAIT_UART_TX);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.a, 0x11);
    CHECK_EQ(g_m.cpu.x, 0x22);
    CHECK_EQ(g_m.cpu.y, 0x33);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.cycles - c0, 11);   /* BBC1 zp not taken (5), RTS (6) */

    b.deadline = g_m.cpu.cycles;         /* passed: the wait fails without running an instruction */
    fw_wait(&b.f, FW_WAIT_UART_TX);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_m.cpu.cycles - c0, 11);
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

/* An observer at isr_par_ready $C5D6: records the bridge's deadline at its first call and sets ser_abort bit 0. */
static bool hook_par_ready_abort(machine *m, void *user)
{
    const fw_bridge *b = user;
    if (!g_seen_deadline)
        g_seen_deadline = b->deadline;
    m->ram[SER_ABORT] |= 1;
    return false;
}

/* A fresh real-ROM machine (the timer off: isr_frame_flag is never set) with the par_play thunk hooked, entered at
   $B877 as a JSR from $9000 would enter it. */
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
   cycles + FW_ROM_CALL_SPEECH_BUDGET) and clears it when it ends: here par_play aborts at its first isr_par_ready and
   isr_wait_frame $B465 waits out its own budget. A deadline already set (as by an outer thunk) is left alone, and
   par_play's isr_par_ready loop, every call of which returns, ends at it with a fault at isr_par_ready. */
static void test_waits_thunk_deadline(void)
{
    static fw_bridge b;
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");

    enter_par_play(rom, len, &b);
    CHECK(machine_hook(&g_m, ROM_ISR_PAR_READY, hook_par_ready_abort, &b));
    g_seen_deadline = 0;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(!machine_step(&g_m));
    CHECK_EQ(g_seen_deadline, c0 + FW_ROM_CALL_SPEECH_BUDGET);
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
    CHECK_EQ(g_m.fault_addr, ROM_ISR_PAR_READY);
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
    test_bridge_deadline();
    test_waits_thunk_deadline();
}
