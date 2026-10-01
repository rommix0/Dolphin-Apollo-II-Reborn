#include <stdio.h>
#include <string.h>
#include "test.h"
#include "machine/machine.h"

static uint8_t g_rom[MACHINE_ROM_SIZE];
static machine g_m;
static uint8_t g_first_frame[APOLLO_FRAME_BYTES];
static int g_frame_calls;

static void put(uint16_t addr, const uint8_t *bytes, size_t n) { memcpy(g_rom + addr, bytes, n); }

static void set_vector(uint16_t at, uint16_t target)
{
    g_rom[at] = (uint8_t)target;
    g_rom[at + 1] = (uint8_t)(target >> 8);
}

static void boot(void) { CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL)); }
static uint8_t rd(uint16_t a) { return g_m.bus.read(&g_m, a); }
static void wr(uint16_t a, uint8_t v) { g_m.bus.write(&g_m, a, v); }

static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    (void)user;
    if (g_frame_calls++ == 0)
        memcpy(g_first_frame, frame, APOLLO_FRAME_BYTES);
}

static void test_memory_map(void)
{
    memset(g_rom, 0, sizeof g_rom);
    g_rom[0x0010] = 0x22; g_rom[0x4010] = 0x11; g_rom[0x9000] = 0x33;
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(g_m.cpu.pc, 0x9000);                  /* P2 pulls select the main ROM at reset */
    CHECK_EQ(rd(0x4010), 0x11);                    /* P2.7 = 0: upper half */
    wr(M50734_P2, 0xF0);
    wr(M50734_P2_DIR, 0xFD);                       /* drive bank $70 with bit 7 set */
    CHECK_EQ(rd(0x4010), 0x22);                    /* P2.7 = 1: file 0x0000-0x3FFF */
    CHECK_EQ(g_m.lower_window_reads, 1);
    CHECK_EQ(rd(0x9000), 0x33);                    /* main ROM still selected */
    wr(M50734_P2, 0x50);                           /* another bank: empty option socket */
    CHECK_EQ(rd(0x9000), 0xFF);
    wr(0x0100, 0xAB); wr(0x3FFF, 0xCD);
    CHECK_EQ(rd(0x0100), 0xAB);
    CHECK_EQ(rd(0x3FFF), 0xCD);
    wr(0x8000, 0x42);                              /* DSP data port */
    CHECK_EQ(g_m.port_writes, 1);
    CHECK_EQ(g_m.unmodelled_count, 0);
    wr(0x9000, 0x00);                              /* write into ROM space: logged */
    rd(0x00DA);                                    /* Timer X: not modelled, logged */
    rd(0x00DA);                                    /* each address/direction is logged once */
    CHECK_EQ(g_m.unmodelled_count, 2);
    CHECK_EQ(g_m.unmodelled[0].addr, 0x9000);
    CHECK(g_m.unmodelled[0].write);
    CHECK_EQ(g_m.unmodelled[1].addr, 0x00DA);
    CHECK(!g_m.unmodelled[1].write);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    wr(M50734_P0_DIR, 0x20);
    wr(M50734_P0_FUNC, 0x20);                      /* P05 as DME output: data-memory mode */
    CHECK_EQ(g_m.fault, MFAULT_DME);
}

static void test_frame_capture(void)
{
    static const uint8_t code[] = { 0x3C, 0x00, 0x2C,     /* $9000 LDM #$00,$2C */
                                    0x3C, 0x03, 0x2D,     /*       LDM #$03,$2D */
                                    0x4C, 0x06, 0xB3 };   /*       JMP $B306 */
    static const uint8_t loop[] = { 0x4C, 0x06, 0xB3 };   /* $B306 JMP $B306 */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    put(APOLLO_SWAP_PC, loop, sizeof loop);
    set_vector(0xFFFE, 0x9000);
    boot();
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        g_m.ram[0x0300 + i] = (uint8_t)(i + 1);
    g_frame_calls = 0;
    machine_set_outputs(&g_m, on_frame, NULL);
    CHECK_EQ(machine_run(&g_m, 100), MACHINE_CYCLE_LIMIT);
    CHECK(g_frame_calls >= 1);
    CHECK_EQ(g_m.frames_produced, (uint64_t)g_frame_calls);
    CHECK_EQ(g_first_frame[0], 1);
    CHECK_EQ(g_first_frame[19], 20);
}

static void test_serial_delivery_and_idle(void)
{
    static const uint8_t reset[] = { 0x3C, 0x10, 0xE6,    /* $9000 LDM #$10,$E6  UART RX interrupt on */
                                     0x58,                /*       CLI */
                                     0x4C, 0x02, 0xC6 };  /*       JMP $C602 */
    static const uint8_t idle[] = { 0xC2,                 /* $C602 WIT */
                                    0x4C, 0x02, 0xC6 };   /* $C603 JMP $C602 */
    static const uint8_t isr[] = { 0xA5, 0xE5,            /* $A000 LDA $E5 */
                                   0x9D, 0x00, 0x02,      /*       STA $0200,X */
                                   0xE8,                  /*       INX */
                                   0x40 };                /*       RTI */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, reset, sizeof reset);
    put(APOLLO_IDLE_WIT_PC, idle, sizeof idle);
    put(0xA000, isr, sizeof isr);
    set_vector(0xFFFE, 0x9000);
    set_vector(0xFFFA, 0xA000);
    boot();
    CHECK(machine_feed_serial(&g_m, (const uint8_t *)"HI", 2));
    CHECK_EQ(machine_run(&g_m, 100000), MACHINE_IDLE);
    CHECK_EQ(g_m.ram[0x0200], 'H');
    CHECK_EQ(g_m.ram[0x0201], 'I');
}

static void test_fault_and_dump(void)
{
    static const uint8_t code[] = { 0x89 };              /* illegal opcode */
    char text[4096];
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(machine_run(&g_m, 100), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_CPU);
    CHECK_EQ(g_m.cpu.fault, CPU740_FAULT_ILLEGAL);
    FILE *f = fopen("build/test_dump.txt", "w+");
    CHECK(f != NULL);
    if (!f)
        return;
    machine_dump(&g_m, f);
    rewind(f);
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    CHECK(strstr(text, "illegal opcode") != NULL);
    CHECK(strstr(text, "9000  DB $89") != NULL);
}

static void test_cycle_limit(void)
{
    static const uint8_t code[] = { 0x4C, 0x00, 0x90 };   /* JMP $9000, no WIT */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_CYCLE_LIMIT);
    CHECK(g_m.cpu.cycles >= 1000);
}

static void test_escaped_pc(void)
{
    /* Case (a): JMP into deselected ROM bank */
    static const uint8_t code_a[] = { 0x3C, 0x50, 0xF0,    /* $9000 LDM #$50,$F0  */
                                      0x3C, 0xFF, 0xF1,    /*       LDM #$FF,$F1  */
                                      0xEA };              /*       NOP */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code_a, sizeof code_a);
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(machine_run(&g_m, 100), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_ESCAPED_PC);
    CHECK_EQ(g_m.cpu.pc, 0x9006);
    /* Case (b): JMP into SFR space */
    memset(g_rom, 0, sizeof g_rom);
    static const uint8_t code_b[] = { 0x4C, 0xE0, 0x00 };  /* $9000 JMP $00E0 */
    put(0x9000, code_b, sizeof code_b);
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(machine_run(&g_m, 100), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_ESCAPED_PC);
    CHECK_EQ(g_m.cpu.pc, 0x00E0);
}

/* Test-only cycle scale: with 2/1 the peripheral timers see two ticks per CPU cycle. */
static void test_cycle_scale(void)
{
    static const uint8_t code[] = { 0x3C, 0x00, 0xE0,     /* $9000 LDM #$00,$E0  timer 3 prescaler */
                                    0x3C, 0x31, 0xE1,     /*       LDM #$31,$E1  period 200 timer cycles */
                                    0x4C, 0x06, 0x90 };   /*       JMP $9006 */
    machine_config cfg;
    machine_default_config(&cfg);
    cfg.cycle_num = 2;
    cfg.cycle_den = 1;
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, &cfg));
    CHECK_EQ(machine_run(&g_m, 60), MACHINE_CYCLE_LIMIT);   /* ~120 timer cycles: not yet */
    CHECK_EQ(g_m.sfr.icr2 & 0x20, 0);
    CHECK_EQ(machine_run(&g_m, 50), MACHINE_CYCLE_LIMIT);   /* ~220 timer cycles, ~110 CPU cycles */
    CHECK_EQ(g_m.sfr.icr2 & 0x20, 0x20);
    CHECK(g_m.cpu.cycles < 130);                             /* cpu.cycles stays unscaled */
}

static void test_peek_has_no_side_effects(void)
{
    memset(g_rom, 0, sizeof g_rom);
    g_rom[0x0010] = 0x22; g_rom[0x4010] = 0x11; g_rom[0x9000] = 0x33;
    set_vector(0xFFFE, 0x9000);
    boot();
    wr(M50734_P2, 0xF0);
    wr(M50734_P2_DIR, 0xFD);                       /* P2.7 = 1: lower window selected */
    CHECK(m50734_uart_receive(&g_m.sfr, 0x5A));
    uint64_t lower = g_m.lower_window_reads;
    unsigned unmodelled = g_m.unmodelled_count;
    CHECK(g_m.sfr.uart_rx_full);
    CHECK_EQ(machine_peek(&g_m, 0x4010), 0x22);    /* served from the lower window */
    machine_peek(&g_m, 0x00E5);                    /* UART RX: a real read would clear rx_full */
    machine_peek(&g_m, 0x00DA);                    /* unmodelled SFR: a real read would be logged */
    machine_peek(&g_m, 0x9000);
    CHECK_EQ(g_m.lower_window_reads, lower);
    CHECK_EQ(g_m.unmodelled_count, unmodelled);
    CHECK(g_m.sfr.uart_rx_full);
}

static void test_unmodelled_log_limits(void)
{
    memset(g_rom, 0, sizeof g_rom);
    set_vector(0xFFFE, 0x9000);
    boot();
    rd(M50734_UART_TX);                          /* write-only register: a read is unmodelled */
    CHECK_EQ(g_m.unmodelled_count, 1);
    CHECK_EQ(g_m.unmodelled[0].addr, M50734_UART_TX);
    CHECK(!g_m.unmodelled[0].write);
    for (int i = 0; i < 70; i++)
        wr((uint16_t)(0x9000 + i), 0);           /* 70 distinct ROM-space writes */
    CHECK_EQ(g_m.unmodelled_count, MACHINE_MAX_UNMODELLED);
    CHECK_EQ(g_m.unmodelled_dropped, 71 - MACHINE_MAX_UNMODELLED);
}

static uint8_t g_raw[4][APOLLO_FRAME_BYTES];
static int g_raw_calls;

static void on_raw(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    (void)user;
    if (g_raw_calls < 4)
        memcpy(g_raw[g_raw_calls], frame, APOLLO_FRAME_BYTES);
    g_raw_calls++;
}

/* Bytes at $8000 group into 20-byte frames; a rising edge on P2.0 (DSP reset) restarts at byte 0. */
static void test_raw_capture(void)
{
    static const uint8_t code[] = {
        0x3C, 0x01, 0xF1,          /* $9000 LDM #$01,$F1   P2.0 is an output (low) */
        0xA2, 0x00,                /*       LDX #$00 */
        0x8E, 0x00, 0x80,          /* $9005 STX $8000      25 bytes 0..24 to the DSP port */
        0xE8,                      /*       INX */
        0xE0, 0x19,                /*       CPX #$19 */
        0xD0, 0xF8,                /*       BNE $9005 */
        0x0F, 0xF0,                /*       SEB0 $F0        DSP reset pulse */
        0x1F, 0xF0,                /*       CLB0 $F0 */
        0xA2, 0x64,                /*       LDX #$64 */
        0x8E, 0x00, 0x80,          /* $9013 STX $8000      20 bytes 100..119 */
        0xE8,                      /*       INX */
        0xE0, 0x78,                /*       CPX #$78 */
        0xD0, 0xF8,                /*       BNE $9013 */
        0x4C, 0x1B, 0x90,          /* $901B JMP $901B */
    };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    boot();
    g_raw_calls = 0;
    machine_set_raw_output(&g_m, on_raw, NULL);
    CHECK_EQ(machine_run(&g_m, 5000), MACHINE_CYCLE_LIMIT);
    CHECK_EQ(g_raw_calls, 2);
    CHECK_EQ(g_m.raw_frames, 2);
    CHECK_EQ(g_m.dsp_resets, 1);
    CHECK_EQ(g_m.raw_discarded, 5);              /* bytes 20..24 were cut by the reset */
    CHECK_EQ(g_m.port_writes, 45);
    CHECK_EQ(g_raw[0][0], 0);
    CHECK_EQ(g_raw[0][19], 19);
    CHECK_EQ(g_raw[1][0], 100);
    CHECK_EQ(g_raw[1][19], 119);
}

void suite_machine(void)
{
    test_memory_map();
    test_frame_capture();
    test_serial_delivery_and_idle();
    test_fault_and_dump();
    test_cycle_limit();
    test_escaped_pc();
    test_cycle_scale();
    test_peek_has_no_side_effects();
    test_unmodelled_log_limits();
    test_raw_capture();
}
