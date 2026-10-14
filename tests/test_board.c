/* The CPU-less board (M5a spec 4.1), on hand-built states and no ROM: each step's cycles against the emulated CPU's,
   interrupt entry and dispatch, the four waits, delivery and the idle test, the DSP outputs and frame capture, the
   timer scale, the unmodelled log and the faults. The handlers that run are the firmware's own C (isr_timer_dsp,
   whose CLB5 ICR2 drops timer 3's line; isr_unused), on a firmware context whose host reads and writes the board's
   SFRs; the vectors come from the converted image. */
#include <string.h>
#include "test.h"
#include "board/board.h"
#include "machine/machine.h"
#include "cpu740/cpu740.h"
#include "firmware/fw.h"
#include "firmware/boot_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/ser_addr.h"
#include "firmware/bank_addr.h"
#include "firmware/cmd_addr.h"
#include "firmware/data/fw_data.h"

#define T2_PRE      0xDE
#define T2_RELOAD   0xDF
#define T3_PRE      0xE0
#define T3_RELOAD   0xE1
#define ICR1        0xFF
#define ICR2        0xFE
#define ICR2_T3     0x30   /* timer 3's enable (bit 4) and request (bit 5): line 2 */
#define P0          0xF6
#define P0_DIR      0xF7
#define P0_FUNC     0xF5
#define P2          0xF0
#define P2_DIR      0xF1
#define UART_RX     0xE5
#define UART_CTRL   0xE6

static const uint8_t k_pins[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };   /* machine_default_config's */
static board g_b;
static fw g_f;
static fw_host g_host;
static uint8_t g_frame[BOARD_FRAME_BYTES];
static unsigned g_frames, g_raws;

static uint8_t host_read_sfr(void *ctx, uint8_t addr) { return board_read_sfr(ctx, addr); }
static void host_write_sfr(void *ctx, uint8_t addr, uint8_t v) { board_write_sfr(ctx, addr, v); }

/* A fresh board with the converted image's vectors and a firmware whose host is the board's SFRs. */
static void setup(void)
{
    board_init(&g_b, k_pins, 1, 1);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_b;
    g_host.read_sfr = host_read_sfr;
    g_host.write_sfr = host_write_sfr;
    fw_init(&g_f, g_b.ram, fw_image_get(), &g_host);
    g_b.rom = fw_image_get()->rom;
    g_b.fw = &g_f;
    g_b.i = false;
}

static void on_frame(void *user, const uint8_t frame[BOARD_FRAME_BYTES])
{
    (void)user;
    memcpy(g_frame, frame, BOARD_FRAME_BYTES);
    g_frames++;
}

static void on_raw(void *user, const uint8_t frame[BOARD_FRAME_BYTES])
{
    (void)user;
    memcpy(g_frame, frame, BOARD_FRAME_BYTES);
    g_raws++;
}

/* The board's constants are the machine's and the firmware's (M5a spec 3.2). */
static void test_constants(void)
{
    CHECK_EQ(BOARD_RAM_SIZE, MACHINE_RAM_SIZE);
    CHECK_EQ(BOARD_QUEUE_SIZE, MACHINE_QUEUE_SIZE);
    CHECK_EQ(BOARD_MAX_UNMODELLED, MACHINE_MAX_UNMODELLED);
    CHECK_EQ(BOARD_FRAME_BYTES, APOLLO_FRAME_BYTES);
    CHECK_EQ(BOARD_PRODUCER_PTR, APOLLO_PRODUCER_PTR);
    CHECK_EQ(BOARD_P2_BANK_MASK, P2_BANK_BITS);
    CHECK_EQ(BOARD_P2_MAIN_BANK, P2_MAIN_BANK);
    CHECK_EQ(BOARD_ICR2, M50734_ICR2);
    CHECK_EQ(BOARD_UART_STATUS, M50734_UART_STATUS);
    CHECK_EQ(BOARD_VEC_FIRST, FW_VECTORS);
}

static uint8_t g_mem[0x10000];
static uint8_t mem_read(void *ctx, uint16_t a) { (void)ctx; return g_mem[a]; }
static void mem_write(void *ctx, uint16_t a, uint8_t v) { (void)ctx; g_mem[a] = v; }

/* The board's cycles are cpu740_step's on the instructions the waits mirror ($B372 BBC3 ICR2,$B372; $C500 BBC1
   UART_STATUS,$C500; $C503 RTS), a WIT step and an interrupt entry. */
static void test_cycles_match_cpu(void)
{
    cpu740 c;
    cpu740_bus bus = { NULL, mem_read, mem_write };
    memset(g_mem, 0, sizeof g_mem);
    g_mem[0xFFFE] = 0x72;
    g_mem[0xFFFF] = 0xB3;
    memcpy(&g_mem[FW_WAIT_TIMER2_LOOP], (const uint8_t[]){ 0x77, BOARD_ICR2, 0xFD }, 3);
    memcpy(&g_mem[FW_WAIT_UART_TX_LOOP], (const uint8_t[]){ 0x37, BOARD_UART_STATUS, 0xFD, 0x60 }, 4);
    cpu740_reset(&c, &bus);
    CHECK_EQ(c.pc, FW_WAIT_TIMER2_LOOP);
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_PASS);   /* bit 3 clear: taken, back to itself */
    CHECK_EQ(c.pc, FW_WAIT_TIMER2_LOOP);
    g_mem[BOARD_ICR2] = 0x08;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_EXIT);   /* set: on to $B375 */
    CHECK_EQ(c.pc, FW_WAIT_TIMER2_EXIT);
    c.pc = FW_WAIT_UART_TX_LOOP;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_PASS);
    g_mem[BOARD_UART_STATUS] = 0x02;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_EXIT);
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_RTS_CYCLES);
    c.waiting = true;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_WIT_CYCLES);
    cpu740_set_irq(&c, 2, true);
    c.p &= (uint8_t)~CPU740_I;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_IRQ_CYCLES);
}

/* board_init as machine_init: I set, 0 cycles, the scale, the pins, the P2.0 level. */
static void test_init(void)
{
    static const uint8_t pins[5] = { 0x00, 0x00, 0x71, 0x00, 0x00 };
    board_init(&g_b, pins, 0, 3);
    CHECK(g_b.i);
    CHECK_EQ(g_b.cycles, 0);
    CHECK_EQ(g_b.cycle_num, 1);
    CHECK_EQ(g_b.cycle_den, 3);
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0x71);
    CHECK(g_b.dsp_reset_level);
    CHECK(g_b.rom == NULL && g_b.fw == NULL);
}

/* A WIT step is 4 cycles, an interrupt entry 7; the handler (isr_timer_dsp, its CLB5 ICR2) runs in 0 cycles and its RTI
   restores I. Timer 3 at prescaler 0 and reload 9 expires after 40 cycles: ten WIT steps. */
static void test_wait_interrupt(void)
{
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK(board_wait_interrupt(&g_b, 1000));
    CHECK_EQ(g_b.cycles, 10 * BOARD_WIT_CYCLES + BOARD_IRQ_CYCLES);
    CHECK(!g_b.i);                         /* restored */
    CHECK(!g_b.waiting && !g_b.handler_due);
    CHECK_EQ(g_b.handler, ROM_ISR_TIMER_DSP);
    CHECK_EQ(g_b.sfr.icr2 & 0x20, 0);      /* the handler cleared the request */
}

/* The bound: at 40 the wait ends before the interrupt; at 41 the entry is made (7 cycles) but the bound has passed
   before its handler runs: I stays set, the handler never runs. */
static void test_wait_interrupt_bound(void)
{
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK(!board_wait_interrupt(&g_b, 40));
    CHECK_EQ(g_b.cycles, 40);
    CHECK(!g_b.i && !g_b.waiting);
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK(!board_wait_interrupt(&g_b, 41));
    CHECK_EQ(g_b.cycles, 47);
    CHECK(g_b.i);                          /* entered, never returned */
    CHECK(!g_b.handler_due && !g_b.waiting);
    CHECK_EQ(g_b.sfr.icr2 & 0x20, 0x20);   /* the handler did not run */
}

/* A line raised with I set wakes the CPU without the interrupt: the wait fails at once. */
static void test_wait_interrupt_i_set(void)
{
    setup();
    g_b.i = true;
    board_write_sfr(&g_b, ICR2, ICR2_T3);
    CHECK(!board_wait_interrupt(&g_b, 1000));
    CHECK_EQ(g_b.cycles, 0);
    CHECK(!g_b.waiting);
}

/* The lowest line first: lines 0 and 3 raised (ICR1 bits 2/3 and 4/5); a test image whose vectors are unmapped
   addresses shows which vector was read: line 0's ($FFFC). */
static void test_lowest_line_first(void)
{
    static uint8_t img[0x10000];
    setup();
    img[0xFFFC] = 0x34;
    img[0xFFFD] = 0x12;
    img[0xFFF6] = 0x78;
    img[0xFFF7] = 0x56;
    g_b.rom = img;
    board_write_sfr(&g_b, ICR1, 0x3C);
    CHECK(!board_wait_interrupt(&g_b, 1000));
    CHECK_EQ(g_b.cycles, BOARD_IRQ_CYCLES);
    CHECK_EQ(g_b.fault, BOARD_FAULT_VECTOR);
    CHECK_EQ(g_b.fault_addr, 0x1234);
}

/* Each timer loop: BBC3 ICR2 (timer 2) and BBC5 ICR2 (timer 3), with I set, 7 cycles a pass and 5 for the exit; I
   restored. Timer 2 at prescaler 0 and reload 4 expires after 20 cycles: three passes, then the exit. */
static void test_wait_timer(void)
{
    setup();
    board_write_sfr(&g_b, T2_PRE, 0);
    board_write_sfr(&g_b, T2_RELOAD, 4);
    board_write_sfr(&g_b, ICR2, ICR2_T3);   /* timer 3's line raised: not taken under the loop's SEI */
    CHECK(board_wait_timer(&g_b, 3, 1000));
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + BOARD_BBC_EXIT);
    CHECK(!g_b.i);
    CHECK_EQ(g_b.sfr.icr2 & 0x08, 0x08);
    setup();
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 1);    /* 8 cycles: two passes */
    CHECK(board_wait_timer(&g_b, 5, 1000));
    CHECK_EQ(g_b.cycles, 2 * BOARD_BBC_PASS + BOARD_BBC_EXIT);
    setup();
    CHECK(!board_wait_timer(&g_b, 5, 20));  /* no timer running: the bound ends it after three passes */
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS);
    CHECK(!g_b.i);
}

/* $C500 with an interrupt in the middle: the entry (7), the handler (0), BBC1 UART_STATUS not taken (the transmitter
   is always ready, 5), the RTS (6). */
static void test_wait_uart_tx(void)
{
    setup();
    board_write_sfr(&g_b, ICR2, ICR2_T3);
    CHECK(board_wait_uart_tx(&g_b, 1000));
    CHECK_EQ(g_b.cycles, BOARD_IRQ_CYCLES + BOARD_BBC_EXIT + BOARD_RTS_CYCLES);
    CHECK(!g_b.i);
    CHECK_EQ(g_b.sfr.icr2 & 0x20, 0);
    setup();
    CHECK(board_wait_uart_tx(&g_b, 1000));
    CHECK_EQ(g_b.cycles, BOARD_BBC_EXIT + BOARD_RTS_CYCLES);
    setup();
    CHECK(!board_wait_uart_tx(&g_b, 5));    /* the bound passed before the RTS */
    CHECK_EQ(g_b.cycles, BOARD_BBC_EXIT);
}

static unsigned g_idle_calls;
static board_idle_action g_idle_answer;
static board_idle_action idle_fn(board *b, void *user)
{
    (void)b;
    (void)user;
    g_idle_calls++;
    return g_idle_answer;
}

/* Delivery: at the idle point only, one byte while the UART is empty, at the first idle's busy level. */
static void test_delivery(void)
{
    setup();
    CHECK(board_feed_serial(&g_b, (const uint8_t *)"AB", 2));
    g_b.waiting = true;
    board_prepare(&g_b);                    /* waiting, but not at the idle point */
    CHECK(!g_b.sfr.uart_rx_full);
    CHECK(!g_b.busy_level_known);
    g_b.idle_point = true;
    board_prepare(&g_b);
    CHECK(g_b.sfr.uart_rx_full);
    CHECK_EQ(g_b.sfr.uart_rx, 'A');
    CHECK(g_b.busy_level_known);
    CHECK_EQ(g_b.busy_idle_level, 0);
    board_prepare(&g_b);                    /* the UART still full */
    CHECK_EQ(g_b.q_head, 1);
    CHECK_EQ(board_read_sfr(&g_b, UART_RX), 'A');
    g_b.sfr.port_in[0] |= BOARD_P0_BUSY;    /* busy: not the first idle's level */
    board_prepare(&g_b);
    CHECK(!g_b.sfr.uart_rx_full);
    g_b.sfr.port_in[0] &= (uint8_t)~BOARD_P0_BUSY;
    board_prepare(&g_b);
    CHECK_EQ(g_b.sfr.uart_rx, 'B');
    CHECK_EQ(g_b.q_head, g_b.q_tail);
    uint8_t big[BOARD_QUEUE_SIZE];
    memset(big, 'x', sizeof big);
    CHECK(board_feed_serial(&g_b, big, BOARD_QUEUE_SIZE - 1));   /* the ring holds one byte less than its size */
    CHECK(!board_feed_serial(&g_b, big, 1));
}

/* The idle test's five conditions: at the idle point, no queued byte, the UART empty, timer 3 off, no line. */
static void test_idle(void)
{
    setup();
    g_b.waiting = g_b.idle_point = true;
    board_prepare(&g_b);
    CHECK(board_idle(&g_b));
    g_b.idle_point = false;
    CHECK(!board_idle(&g_b));
    g_b.idle_point = true;
    g_b.q_tail = 1;
    CHECK(!board_idle(&g_b));
    g_b.q_tail = 0;
    g_b.sfr.uart_rx_full = true;
    CHECK(!board_idle(&g_b));
    g_b.sfr.uart_rx_full = false;
    board_write_sfr(&g_b, ICR2, 0x10);
    CHECK(!board_idle(&g_b));
    board_write_sfr(&g_b, ICR2, 0x00);
    board_write_sfr(&g_b, ICR1, 0x0C);
    board_prepare(&g_b);
    CHECK(!board_idle(&g_b));
    board_write_sfr(&g_b, ICR1, 0x00);
    board_prepare(&g_b);
    CHECK(board_idle(&g_b));
}

/* The input wait: the idle handler at every idle (a feed goes on and the byte is delivered; with the UART receive
   interrupt off it is never read, so the next idle never comes and the bound ends the wait); a stop leaves the board
   waiting at the idle point; an interrupt handled ends it. */
static void test_wait_input(void)
{
    setup();
    g_idle_calls = 0;
    g_idle_answer = BOARD_STOP;
    CHECK_EQ(board_wait_input(&g_b, 1000, idle_fn, NULL), BOARD_INPUT_STOPPED);
    CHECK_EQ(g_idle_calls, 1);
    CHECK_EQ(g_b.cycles, 0);
    CHECK(g_b.waiting && g_b.idle_point);
    setup();
    CHECK_EQ(board_wait_input(&g_b, 1000, NULL, NULL), BOARD_INPUT_STOPPED);   /* no handler: a stop */
    setup();
    g_idle_calls = 0;
    g_idle_answer = BOARD_FEED;
    CHECK(board_feed_serial(&g_b, (const uint8_t *)"A", 1));   /* a byte to deliver: no idle */
    CHECK_EQ(board_wait_input(&g_b, 40, idle_fn, NULL), BOARD_INPUT_FAILED);
    CHECK_EQ(g_idle_calls, 0);
    CHECK(g_b.sfr.uart_rx_full);
    CHECK_EQ(g_b.cycles, 40);
    CHECK(!g_b.waiting && !g_b.idle_point);
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK_EQ(board_wait_input(&g_b, 1000, idle_fn, NULL), BOARD_INPUT_HANDLED);
    CHECK_EQ(g_b.cycles, 47);
    CHECK(!g_b.waiting && !g_b.idle_point && !g_b.i);
}

/* The DSP port: bytes grouped into raw frames of 20; a rising edge of P2.0 drops the partial frame. */
static void test_dsp(void)
{
    setup();
    board_set_raw_output(&g_b, on_raw, NULL);
    g_raws = 0;
    for (int k = 0; k < 27; k++)
        board_dsp_write(&g_b, (uint8_t)k);
    CHECK_EQ(g_b.port_writes, 27);
    CHECK_EQ(g_b.raw_frames, 1);
    CHECK_EQ(g_raws, 1);
    CHECK_EQ(g_frame[19], 19);
    CHECK_EQ(g_b.raw_pos, 7);
    board_write_sfr(&g_b, P2_DIR, 0x01);    /* P2.0 an output, latch 0: low */
    CHECK_EQ(g_b.dsp_resets, 0);
    board_write_sfr(&g_b, P2, 0x01);        /* rising edge */
    CHECK_EQ(g_b.dsp_resets, 1);
    CHECK_EQ(g_b.raw_discarded, 7);
    CHECK_EQ(g_b.raw_pos, 0);
    board_write_sfr(&g_b, P2, 0x01);        /* still high: no edge */
    CHECK_EQ(g_b.dsp_resets, 1);
}

/* The frame event: the 20 bytes at the producer pointer. */
static void test_capture(void)
{
    setup();
    board_set_outputs(&g_b, on_frame, NULL);
    g_frames = 0;
    g_b.ram[BOARD_PRODUCER_PTR] = 0x34;
    g_b.ram[BOARD_PRODUCER_PTR + 1] = 0x12;
    for (int k = 0; k < BOARD_FRAME_BYTES; k++)
        g_b.ram[0x1234 + k] = (uint8_t)(0xA0 + k);
    board_capture_frame(&g_b);
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_b.frames_produced, 1);
    CHECK_EQ(g_frame[0], 0xA0);
    CHECK_EQ(g_frame[19], 0xB3);
}

/* The timer scale: ticks = cycles * num / den, the remainder carried. At 1/3 two WIT steps (8 cycles) tick 1 + 1. */
static void test_scale(void)
{
    setup();
    g_b.cycle_den = 3;
    board_write_sfr(&g_b, T2_PRE, 0);
    board_write_sfr(&g_b, T2_RELOAD, 99);   /* 400 cycles */
    CHECK(!board_wait_interrupt(&g_b, 8));
    CHECK_EQ(g_b.cycles, 8);
    CHECK_EQ(g_b.cycle_rem, 2);
    CHECK_EQ(g_b.sfr.timer_count[1], 400 - 2);
}

/* The unmodelled log: once per address and direction, a read of write-only UART_TX included; then the dropped
   count. */
static void test_unmodelled(void)
{
    setup();
    board_read_sfr(&g_b, 0xDA);
    board_read_sfr(&g_b, 0xDA);
    board_write_sfr(&g_b, 0xDA, 1);
    board_read_sfr(&g_b, M50734_UART_TX);
    board_read_sfr(&g_b, M50734_UART_STATUS);   /* modelled */
    CHECK_EQ(g_b.unmodelled_count, 3);
    CHECK(g_b.unmodelled[0].addr == 0xDA && !g_b.unmodelled[0].write);
    CHECK(g_b.unmodelled[1].addr == 0xDA && g_b.unmodelled[1].write);
    CHECK(g_b.unmodelled[2].addr == M50734_UART_TX && !g_b.unmodelled[2].write);
    g_b.unmodelled_count = BOARD_MAX_UNMODELLED;
    board_read_sfr(&g_b, 0xDB);
    CHECK_EQ(g_b.unmodelled_dropped, 1);
}

/* The DME strobe (P0_FUNC bit 5 with P0.5 an output), and a wait under another bank (code from a deselected bank). */
static void test_faults(void)
{
    setup();
    board_write_sfr(&g_b, P0_FUNC, 0x20);
    CHECK_EQ(g_b.fault, BOARD_FAULT_NONE);
    board_write_sfr(&g_b, P0_DIR, 0x20);
    CHECK_EQ(g_b.fault, BOARD_FAULT_DME);
    CHECK(!board_wait_interrupt(&g_b, 1000));   /* a step after the fault: the wait fails */
    CHECK_EQ(g_b.cycles, BOARD_WIT_CYCLES);
    setup();
    board_write_sfr(&g_b, P2_DIR, BOARD_P2_BANK_MASK);
    board_write_sfr(&g_b, P2, 0x00);
    CHECK(!board_main_bank(&g_b));
    CHECK(!board_wait_timer(&g_b, 3, 1000));
    CHECK_EQ(g_b.fault, BOARD_FAULT_ESCAPED);
    CHECK_EQ(g_b.cycles, 0);
}

/* The dispatch table: every vector of the converted image and every entry has its C; another address is an
   unmapped vector. With another bank selected a vector reads $FFFF. */
static void test_dispatch(void)
{
    setup();
    CHECK_EQ(board_vector(&g_b, BOARD_VEC_RESET), ROM_BOOT_RESET);
    CHECK_EQ(board_vector(&g_b, 0xFFFC - 2 * 1), ROM_ISR_INT1);
    CHECK_EQ(board_vector(&g_b, 0xFFFC - 2 * 2), ROM_ISR_TIMER_DSP);
    CHECK_EQ(board_vector(&g_b, 0xFFFC - 2 * 4), ROM_ISR_INT4_HOST_LINE);
    for (uint16_t v = BOARD_VEC_FIRST; v != 0; v = (uint16_t)(v + 2))
        CHECK(board_has_handler(board_vector(&g_b, v)));
    CHECK(board_has_handler(BANK_ENTRY_COLD) && board_has_handler(BANK_ENTRY_WARM));
    CHECK(!board_has_handler(ROM_CMD_RUN_NEXT));     /* the NUL bug's fallback has no C entry */
    CHECK(board_call(&g_b, ROM_ISR_UNUSED, 0, 0));   /* an RTI */
    CHECK_EQ(g_b.fault, BOARD_FAULT_NONE);
    CHECK(!board_call(&g_b, 0x1234, 0, 0));
    CHECK_EQ(g_b.fault, BOARD_FAULT_VECTOR);
    CHECK_EQ(g_b.fault_addr, 0x1234);
    setup();
    g_b.fw = NULL;
    CHECK(!board_call(&g_b, ROM_ISR_UNUSED, 0, 0));
    setup();
    board_write_sfr(&g_b, P2_DIR, BOARD_P2_BANK_MASK);
    board_write_sfr(&g_b, P2, 0x00);
    CHECK_EQ(board_vector(&g_b, BOARD_VEC_RESET), BOARD_EMPTY_VECTOR);
    g_b.rom = NULL;
    CHECK_EQ(board_peek(&g_b, 0x9000), 0xFF);
}

void suite_board(void)
{
    test_constants();
    test_cycles_match_cpu();
    test_init();
    test_wait_interrupt();
    test_wait_interrupt_bound();
    test_wait_interrupt_i_set();
    test_lowest_line_first();
    test_wait_timer();
    test_wait_uart_tx();
    test_delivery();
    test_idle();
    test_wait_input();
    test_dsp();
    test_capture();
    test_scale();
    test_unmodelled();
    test_faults();
    test_dispatch();
}
