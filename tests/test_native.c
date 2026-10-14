/* The native host (M5a spec 4.2), with no ROM: each host call on a board, read_bank's $FF, the failing ROM calls, the
   waits' bounds and ends. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "engine/native_host.h"
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

void suite_native(void)
{
    test_init();
    test_ports();
    test_bank_and_rom_calls();
    test_cpu_calls();
    test_waits();
    test_deadline();
}
