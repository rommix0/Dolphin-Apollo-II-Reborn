/* The native host and engine (M5a spec 4.2), with no ROM: each host call on a board, read_bank's $FF, the failing ROM
   calls, the waits' bounds and ends; then whole runs of the native engine: a boot to the first idle and a short
   utterance to the second, a bank entry run afresh, input overflow, the NUL bug ending as a fault at $A893, and the
   M5a plan's Review Focus 1 (several runs in one process, faults among them) and 3 (input that just fits the serial
   queue, and one byte more; compared with the ROM engine when the ROM is present). */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/native_host.h"
#include "engine/native_engine.h"
#include "engine/rom_engine.h"
#include "firmware/cmd_addr.h"
#include "firmware/bank_addr.h"

#define T2_PRE      0xDE
#define T2_RELOAD   0xDF
#define ICR2        0xFE
#define P2          0xF0
#define P2_DIR      0xF1

static const uint8_t k_pins[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };
static board g_b;
static native_host g_h;
static native_engine g_e, g_e2;

static void setup(void)
{
    board_init(&g_b, k_pins, 1, 1);
    CHECK(native_host_init(&g_h, &g_b));
    g_b.i = false;
}

/* The binding: the firmware's RAM is the board's, the board's vectors the converted image's, its handlers' firmware
   the host's. */
static void test_init(void)
{
    setup();
    CHECK(g_h.f.ram == g_b.ram);
    CHECK(g_h.f.host == &g_h.host);
    CHECK(g_b.fw == &g_h.f);
    CHECK(g_b.rom != NULL);
    CHECK_EQ(board_vector(&g_b, BOARD_VEC_RESET), 0xA61C);
    CHECK_EQ(g_h.deadline, 0);
    CHECK(!g_h.entered);
}

/* The SFRs, the window, the DSP port and the frame event through the firmware's own calls. */
static void test_ports(void)
{
    setup();
    fw_wr_sfr8(&g_h.f, P2_DIR, 0xFD);
    fw_wr_sfr8(&g_h.f, P2, 0x71);
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0x71);
    CHECK_EQ(g_b.dsp_resets, 1);                 /* P2.0's rising edge */
    CHECK_EQ(fw_sfr8(&g_h.f, P2), 0x71);
    fw_set_low_window(&g_h.f, true);             /* SEB7 P2: a read, then a write */
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0xF1);
    CHECK(g_h.f.low_window);
    fw_set_low_window(&g_h.f, false);
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0x71);
    fw_dsp_write(&g_h.f, 0x12);
    CHECK_EQ(g_b.port_writes, 1);
    CHECK_EQ(g_b.raw_buf[0], 0x12);
    fw_frame_done(&g_h.f);
    CHECK_EQ(g_b.frames_produced, 1);
    CHECK(!g_h.f.fault);
}

/* Another bank's sockets are empty: $FF, no fault. A ROM call, hooked or not, fails: a fault at its address. */
static void test_bank_and_rom_calls(void)
{
    setup();
    g_h.f.bank = 0x6C;
    CHECK_EQ(fw_rom8(&g_h.f, 0xFFE0), 0xFF);
    CHECK_EQ(fw_rom8(&g_h.f, 0x8000), 0xFF);
    CHECK(!g_h.f.fault);
    g_h.f.bank = P2_MAIN_BANK;
    fw_regs r = { 1, 2, 3, 0 };
    CHECK(!fw_rom_call(&g_h.f, 0xC37E, &r, FW_ROM_CALL_BUDGET));
    CHECK(g_h.f.fault);
    CHECK_EQ(g_h.f.fault_addr, 0xC37E);
    setup();
    CHECK(!fw_rom_call_unhooked(&g_h.f, ROM_CMD_RUN_NEXT, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(g_h.f.fault_addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_b.cycles, 0);
}

/* The CPU calls and the bank entry: the I flag; enter records the target, A and X, sets I, and the C is abandoned. */
static void test_cpu_calls(void)
{
    setup();
    fw_reset_cpu(&g_h.f);
    CHECK(g_b.i);
    fw_set_interrupts(&g_h.f, true, 0xA66E);
    CHECK(!g_b.i);
    fw_set_interrupts(&g_h.f, false, 0xA6E7);
    CHECK(g_b.i);
    g_b.i = false;
    fw_enter(&g_h.f, BANK_ENTRY_WARM, 0x31, 0xFF);
    CHECK(g_h.entered);
    CHECK_EQ(g_h.entry, BANK_ENTRY_WARM);
    CHECK_EQ(g_h.entry_a, 0x31);
    CHECK_EQ(g_h.entry_x, 0xFF);
    CHECK(g_b.i);
    CHECK(g_h.f.unwound);
    CHECK(!g_h.f.fault);
}

/* The waits through the host: each kind's cycles (as test_board's), the window and the bank read back after one;
   without a deadline FW_ROM_CALL_BUDGET bounds a wait and its end is a fault. */
static void test_waits(void)
{
    setup();
    fw_wr_sfr8(&g_h.f, T2_PRE, 0);
    fw_wr_sfr8(&g_h.f, T2_RELOAD, 4);
    g_h.f.bank = 0x6C;                           /* a stale view: the wait reads the pins back */
    g_h.f.low_window = true;
    fw_wait(&g_h.f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(!g_h.f.fault);
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + BOARD_BBC_EXIT);
    CHECK_EQ(g_h.f.bank, P2_MAIN_BANK);
    CHECK(!g_h.f.low_window);
    fw_wait(&g_h.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + BOARD_BBC_EXIT + BOARD_BBC_EXIT + BOARD_RTS_CYCLES);
    CHECK(!g_h.f.fault);
    fw_wait(&g_h.f, FW_WAIT_INTERRUPT, 0xB465);  /* no line will rise: the budget ends it, a fault */
    CHECK(g_h.f.fault);
    CHECK_EQ(g_h.f.fault_addr, 0xB465);
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + 2 * BOARD_BBC_EXIT + BOARD_RTS_CYCLES + FW_ROM_CALL_BUDGET);
    setup();
    fw_wait(&g_h.f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);   /* no idle handler: a stop at the first idle */
    CHECK(g_h.f.stopped);
    CHECK(!g_h.f.fault);
    CHECK(g_b.waiting && g_b.idle_point);
    setup();
    fw_wait(&g_h.f, (fw_wait_kind)99, 0x1234);
    CHECK_EQ(g_h.f.fault_addr, 0x1234);
}

/* The phase deadline: a wait that reaches it stops the run (f->stopped, not a fault); one called after it has passed
   stops at once, no cycle spent; the transmit wait is bounded by it too. */
static void test_deadline(void)
{
    setup();
    g_h.deadline = 100;
    fw_wait(&g_h.f, FW_WAIT_INTERRUPT, 0xB465);
    CHECK(g_h.f.stopped);
    CHECK(!g_h.f.fault);
    CHECK_EQ(g_b.cycles, 100);
    g_h.f.stopped = false;
    fw_wait(&g_h.f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(g_h.f.stopped);
    CHECK_EQ(g_b.cycles, 100);
    setup();
    g_h.deadline = 3;
    fw_wait(&g_h.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(g_h.f.stopped);
    CHECK_EQ(g_b.cycles, BOARD_BBC_EXIT);
    setup();
    g_h.deadline = 1000;
    g_b.i = true;
    fw_wr_sfr8(&g_h.f, ICR2, 0x30);              /* a line with I set: the wait fails before the deadline */
    fw_wait(&g_h.f, FW_WAIT_INTERRUPT, 0xB465);
    CHECK(g_h.f.fault);
    CHECK(!g_h.f.stopped);
}

/* ---- whole runs ---- */

static unsigned g_idles, g_frames, g_boot_frames;

static void count_frame(void *user, board *b)
{
    (void)user;
    (void)b;
    g_frames++;
}

static void count_idle(void *user, board *b)
{
    (void)user;
    (void)b;
    if (g_idles++ == 0)
        g_boot_frames = g_frames;
}

/* A boot to the first idle (the announcement's frame events), then "Hello world." to the second: the frames of
   tests/golden/hello.frm. */
static void test_boot_and_speak(void)
{
    native_watch w = { count_frame, count_idle, NULL };
    engine_input in = { (const uint8_t *)"Hello world.", 12, true };
    frame_list fl = { 0 };
    g_idles = g_frames = g_boot_frames = 0;
    CHECK_EQ(native_engine_run_input(&g_e, NULL, &in, false, &fl, NULL, &w), ENGINE_IDLE);
    CHECK_EQ(g_idles, 2);
    CHECK_EQ(g_boot_frames, 41);
    CHECK_EQ(g_frames, 41 + 105);
    CHECK_EQ(fl.count, 105);
    CHECK_EQ(g_e.b.frames_produced, 105);
    CHECK_EQ(g_e.b.fault, BOARD_FAULT_NONE);
    CHECK_EQ(g_e.entries, 1);
    CHECK_EQ(g_e.unwinds, 0);
    CHECK(g_e.b.waiting && g_e.b.idle_point);   /* left at the idle point */
    char *got = frame_list_hex(&fl);
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(want != NULL && !strcmp(got, want));
    free(got);
    free(want);
    frame_list_free(&fl);
    CHECK_EQ(native_engine_speak(&g_e, NULL, "Hello world.", true, &fl, NULL), ENGINE_IDLE);
    CHECK_EQ(fl.count, 41 + 105);                 /* with the boot frames */
    frame_list_free(&fl);
}

/* @=1 twice: the main bank's warm entry, the second from under its own C, run afresh. */
static void test_bank_entry(void)
{
    frame_list fl = { 0 };
    CHECK_EQ(native_engine_speak(&g_e, NULL, "@=1 One. @=1 Two. Three.", false, &fl, NULL), ENGINE_IDLE);
    CHECK_EQ(g_e.unwinds, 2);
    CHECK_EQ(g_e.entries, 3);
    CHECK(fl.count > 0);
    frame_list_free(&fl);
}

/* The NUL bug (M4.6c1 spec 2.5): a handler address with no C, the ROM cmd_run_next the C hands it to: a firmware fault
   at $A893, no frame. */
static void test_nul_bug(void)
{
    static const char *const k_in[] = { "@A", "@M5" };
    for (int i = 0; i < 2; i++) {
        uint8_t bytes[8];
        size_t n = strlen(k_in[i]);
        memcpy(bytes, k_in[i], n);
        bytes[n] = 0;
        engine_input in = { bytes, n + 1, true };
        frame_list fl = { 0 };
        CHECK_EQ(native_engine_run_input(&g_e, NULL, &in, false, &fl, NULL, NULL), ENGINE_FAULT);
        CHECK_EQ(g_e.b.fault, BOARD_FAULT_FIRMWARE);
        CHECK_EQ(g_e.b.fault_addr, ROM_CMD_RUN_NEXT);
        CHECK_EQ(fl.count, 0);
        frame_list_free(&fl);
    }
}

/* Review Focus 3: MACHINE_QUEUE_SIZE - 1 bytes with the CR are accepted (spaces: nothing spoken); one more is input
   overflow. With the ROM the ROM engine ends the same way, with the same frames. */
static void test_queue_fit(void)
{
    static uint8_t spaces[BOARD_QUEUE_SIZE];
    memset(spaces, ' ', sizeof spaces);
    size_t len;
    const uint8_t *rom = test_rom(&len);
    for (size_t n = BOARD_QUEUE_SIZE - 2; n <= BOARD_QUEUE_SIZE - 1; n++) {
        engine_input in = { spaces, n, true };
        frame_list fl = { 0 }, rl = { 0 };
        engine_status st = native_engine_run_input(&g_e, NULL, &in, false, &fl, NULL, NULL);
        bool fits = n + 1 < BOARD_QUEUE_SIZE;
        CHECK_EQ(st, fits ? ENGINE_IDLE : ENGINE_FAULT);
        CHECK_EQ(g_e.b.fault, fits ? BOARD_FAULT_NONE : BOARD_FAULT_INPUT_OVERFLOW);
        if (rom) {
            static machine m;
            machine_status ms = rom_engine_run_input(&m, rom, len, NULL, NULL, NULL, &in, false, &rl, NULL, NULL);
            CHECK_EQ(ms, (int)st);
            CHECK_EQ(m.fault, fits ? MFAULT_NONE : MFAULT_INPUT_OVERFLOW);
            CHECK(rl.count == fl.count && !memcmp(rl.frames, fl.frames, fl.count * ENGINE_FRAME_BYTES));
        }
        frame_list_free(&fl);
        frame_list_free(&rl);
    }
}

/* Review Focus 1: runs in one process, faults among them, each as from a fresh process: "Hello world." on a used
   engine after an overflow and both NUL faults gives the frames, cycles and RAM of the first, and of a run on an
   engine never used. */
static void test_runs_in_one_process(void)
{
    frame_list a = { 0 }, b = { 0 }, c = { 0 };
    static uint8_t ram[BOARD_RAM_SIZE];
    CHECK_EQ(native_engine_speak(&g_e, NULL, "Hello world.", false, &a, NULL), ENGINE_IDLE);
    uint64_t cycles = g_e.b.cycles;
    memcpy(ram, g_e.b.ram, sizeof ram);
    test_nul_bug();
    test_queue_fit();
    CHECK_EQ(native_engine_speak(&g_e, NULL, "Hello world.", false, &b, NULL), ENGINE_IDLE);
    CHECK_EQ(g_e.b.cycles, cycles);
    CHECK(!memcmp(g_e.b.ram, ram, sizeof ram));
    CHECK_EQ(native_engine_speak(&g_e2, NULL, "Hello world.", false, &c, NULL), ENGINE_IDLE);
    CHECK_EQ(g_e2.b.cycles, cycles);
    CHECK(!memcmp(g_e2.b.ram, ram, sizeof ram));
    CHECK(a.count == 105 && b.count == 105 && c.count == 105);
    CHECK(!memcmp(a.frames, b.frames, a.count * ENGINE_FRAME_BYTES));
    CHECK(!memcmp(a.frames, c.frames, a.count * ENGINE_FRAME_BYTES));
    frame_list_free(&a);
    frame_list_free(&b);
    frame_list_free(&c);
}

/* The engine's constants are the ROM engine's. */
static void test_constants(void)
{
    CHECK_EQ(NATIVE_BOOT_CYCLES, ROM_ENGINE_BOOT_CYCLES);
    CHECK_EQ(NATIVE_SPEAK_CYCLES, ROM_ENGINE_SPEAK_CYCLES);
    CHECK_EQ(ENGINE_FRAME_BYTES, APOLLO_FRAME_BYTES);
    CHECK_EQ(ENGINE_IDLE, MACHINE_IDLE);
    CHECK_EQ(ENGINE_FAULT, MACHINE_FAULT);
    CHECK_EQ(ENGINE_CYCLE_LIMIT, MACHINE_CYCLE_LIMIT);
    native_config cfg;
    machine_config mc;
    native_default_config(&cfg);
    machine_default_config(&mc);
    CHECK(!memcmp(cfg.port_in, mc.port_in, sizeof cfg.port_in));
    CHECK(cfg.cycle_num == mc.cycle_num && cfg.cycle_den == mc.cycle_den);
}

void suite_native(void)
{
    test_init();
    test_ports();
    test_bank_and_rom_calls();
    test_cpu_calls();
    test_waits();
    test_deadline();
    test_constants();
    test_boot_and_speak();
    test_bank_entry();
    test_nul_bug();
    test_queue_fit();
    test_runs_in_one_process();
}
