/* Unit checks of the serial code (M4.6a spec 4.5) on the converted data, with a stub host that keeps an ordered log
   of the SFR reads and writes and of the waits, so that the order of every access is pinned, and that fails every
   ROM call (the serial code makes none). Each expected value is worked out from the dump of the routine
   (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests (diff_lts,
   diff_serial) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/cmd_imm.h"
#include "firmware/isr_serial.h"
#include "firmware/ser.h"
#include "firmware/ser_addr.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG     256

typedef enum { LOG_READ, LOG_WRITE, LOG_WAIT } log_kind;
typedef struct { log_kind kind; uint8_t addr, value; } log_entry;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static uint8_t g_sfr[0x100];     /* the stub's SFRs: a read returns g_sfr[addr], a write sets it */
static log_entry g_log[MAX_LOG];
static unsigned g_nlog, g_ncalls;
static bool g_wait_ok;           /* what the stub's wait returns */
static uint16_t g_wait_at;       /* the ROM loop the last wait stood for */
static uint8_t g_script_addr;    /* reads of this SFR take the next g_script value while any is left (a level that */
static uint8_t g_script[8];      /* changes between two reads), storing it in g_sfr; 0 = no script */
static unsigned g_script_n, g_script_pos;

static void log_add(log_kind kind, uint8_t addr, uint8_t value)
{
    if (g_nlog < MAX_LOG)
        g_log[g_nlog] = (log_entry){ kind, addr, value };
    g_nlog++;
}

static uint8_t stub_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    if (g_script_addr && addr == g_script_addr && g_script_pos < g_script_n)
        g_sfr[addr] = g_script[g_script_pos++];
    log_add(LOG_READ, addr, g_sfr[addr]);
    return g_sfr[addr];
}

static void stub_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    g_sfr[addr] = value;
    log_add(LOG_WRITE, addr, value);
}

static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(LOG_WAIT, (uint8_t)kind, 0);
    g_wait_at = at;
    return g_wait_ok ? FW_WAIT_RETURNED : FW_WAIT_FAILED;
}

/* Counts and fails every ROM call: the serial code makes none. */
static fw_call_result stub_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    g_ncalls++;
    return FW_CALL_FAILED;
}

/* Zeroed RAM and SFRs, the stub host, the converted image, nothing logged. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    g_host.ctx = &g_f;
    g_host.rom_call = stub_rom_call;
    g_host.set_low_window = NULL;   /* the serial code does not drive P2 */
    g_host.read_sfr = stub_read_sfr;
    g_host.write_sfr = stub_write_sfr;
    g_host.wait = stub_wait;
    g_nlog = 0;
    g_ncalls = 0;
    g_wait_ok = true;
    g_wait_at = 0;
    g_script_addr = 0;
    g_script_n = g_script_pos = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* Log entry i is kind at addr with value (0 for a wait, whose addr is the kind). */
static void check_log(unsigned i, log_kind kind, uint8_t addr, uint8_t value)
{
    CHECK(i < g_nlog && i < MAX_LOG);
    if (i >= g_nlog || i >= MAX_LOG)
        return;
    CHECK_EQ(g_log[i].kind, kind);
    CHECK_EQ(g_log[i].addr, addr);
    CHECK_EQ(g_log[i].value, value);
}

/* The SFR helpers (M4.6a spec 3.1): fw_sfr8 one read; fw_wr_sfr8 one write; fw_sfr_seb and fw_sfr_clb a read, then a
   write of the changed value (also when the bit already has its value, as the CPU's SEB/CLB); fw_sfr_bit one read per
   test (a level that changes between two tests is seen changing). */
static void test_sfr_helpers(void)
{
    setup();
    g_sfr[SFR_P0] = 0x24;
    CHECK_EQ(fw_sfr8(&g_f, SFR_P0), 0x24);
    fw_wr_sfr8(&g_f, 0xE4, 0x41);                 /* UART_TX */
    fw_sfr_seb(&g_f, SFR_P0, 5);                  /* already set: written again */
    fw_sfr_clb(&g_f, SFR_P0, 2);
    fw_sfr_seb(&g_f, SFR_ICR1, 0);
    CHECK_EQ(g_nlog, 8);
    check_log(0, LOG_READ, SFR_P0, 0x24);
    check_log(1, LOG_WRITE, 0xE4, 0x41);
    check_log(2, LOG_READ, SFR_P0, 0x24);
    check_log(3, LOG_WRITE, SFR_P0, 0x24);
    check_log(4, LOG_READ, SFR_P0, 0x24);
    check_log(5, LOG_WRITE, SFR_P0, 0x20);
    check_log(6, LOG_READ, SFR_ICR1, 0x00);
    check_log(7, LOG_WRITE, SFR_ICR1, 0x01);

    setup();
    g_script_addr = SFR_P0;
    g_script[0] = 0x04;
    g_script[1] = 0x00;
    g_script_n = 2;
    CHECK(fw_sfr_bit(&g_f, SFR_P0, 2));
    CHECK(!fw_sfr_bit(&g_f, SFR_P0, 2));
    CHECK(!fw_sfr_bit(&g_f, SFR_P0, 0));
    CHECK_EQ(g_nlog, 3);
    check_log(1, LOG_READ, SFR_P0, 0x00);
    CHECK(!g_f.fault);
    CHECK_EQ(g_ncalls, 0);
}

/* Without a host, below $DA (RAM, not an SFR), or with a host lacking write_sfr (SEB/CLB: lacking read_sfr too), the
   helpers fault at the address and touch nothing: SEB/CLB neither read nor write. */
static void test_sfr_faults(void)
{
    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wr_sfr8(&f, 0xE4, 1);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E4);
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_sfr_seb(&f, SFR_P0, 5);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, SFR_P0);
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_sfr_clb(&f, SFR_ICR1, 1);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, SFR_ICR1);
    fw_init(&f, g_ram, fw_image_get(), NULL);
    CHECK(fw_sfr_bit(&f, SFR_P0, 0));             /* fw_sfr8's $FF */
    CHECK(f.fault);

    setup();
    fw_wr_sfr8(&g_f, 0xD9, 1);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0x00D9);
    setup();
    fw_sfr_seb(&g_f, 0xD9, 0);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0x00D9);
    setup();
    fw_sfr_clb(&g_f, 0x00, 7);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0x0000);
    CHECK_EQ(g_nlog, 0);
    CHECK_EQ(g_ram[0xD9], 0);

    setup();
    g_host.write_sfr = NULL;                      /* a host that reads SFRs only */
    fw_wr_sfr8(&g_f, 0xE4, 1);
    CHECK(g_f.fault);
    setup();
    g_host.write_sfr = NULL;
    fw_sfr_seb(&g_f, SFR_P0, 3);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, SFR_P0);
    CHECK_EQ(g_nlog, 0);                          /* not even the read */

    setup();
    g_host.read_sfr = NULL;                       /* a host that writes SFRs only */
    fw_sfr_clb(&g_f, SFR_P0, 3);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, SFR_P0);
    CHECK_EQ(g_nlog, 0);                          /* not even the write */
}

/* fw_wait (M4.6a spec 3.3, M4.6b spec 3.2): one host wait of the kind, with the ROM loop it stands for; no host, a
   host without wait, or a wait that fails is a fault at that loop, here ser_tx_wait $C500. */
static void test_wait(void)
{
    setup();
    fw_wait(&g_f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_nlog, 1);
    check_log(0, LOG_WAIT, FW_WAIT_UART_TX, 0);
    CHECK_EQ(g_wait_at, 0xC500);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, FW_WAIT_UART_TX_LOOP);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC500);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wait(&f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC500);
}

/* ser_hex_chars as the C reads it, through fw_rom8 at its ROM address (M4.6a spec 4.6). */
static void test_data(void)
{
    setup();
    for (unsigned n = 0; n < 16; n++)
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(SER_HEX_CHARS + n)), (uint8_t)"0123456789ABCDEF"[n]);
    CHECK(!g_f.fault);
    fw_rom8(&g_f, SER_HEX_CHARS_END);             /* ser_tx_hex's code: not data */
    CHECK(g_f.fault);
}

/* The output ring's pointers, count and pending flag. */
static void tx_ring(uint16_t read, uint16_t write, uint8_t count, uint8_t pending)
{
    fw_wr16(&g_f, SER_TX_READ, read);
    fw_wr16(&g_f, SER_TX_WRITE, write);
    g_ram[SER_TX_COUNT] = count;
    g_ram[SER_TX_PENDING] = pending;
}

static void check_regs(const fw_regs *r, uint8_t a, uint8_t x, uint8_t y)
{
    CHECK_EQ(r->a, a);
    CHECK_EQ(r->x, x);
    CHECK_EQ(r->y, y);
}

/* ser_tx_next_page $C5F5: Y + 1, $3A wrapping to $39 (the ring's one page). Both ring pointers stay in $3900-$39FF,
   so the firmware only ever steps from page $39 and its CPY #$3A always finds equal: the other way ($C5F8 BEQ not
   taken) is pinned here, $20 to $21. A and X kept. */
static void test_tx_next_page(void)
{
    setup();
    fw_regs r = { 0x11, 0x22, 0x39, 0 };
    ser_tx_next_page(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x39);
    r.y = 0x20;
    ser_tx_next_page(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x21);
    CHECK_EQ(g_nlog, 0);
}

/* ser_tx_put $C4DD: the byte at ser_tx_write, which moves on (from $39FF to $3900: the page step, $C4EE);
   ser_tx_count + 1 (8 bits: $FF wraps to 0), ser_tx_pending = 1; A, X and Y kept; no SFR is touched. */
static void test_tx_put(void)
{
    setup();
    tx_ring(SER_TX_RING, 0x3910, 3, 0);
    fw_regs r = { 'A', 0x22, 0x33, 0 };
    ser_tx_put(&g_f, &r);
    check_regs(&r, 'A', 0x22, 0x33);
    CHECK_EQ(g_ram[0x3910], 'A');
    CHECK_EQ(fw_rd16(&g_f, SER_TX_WRITE), 0x3911);
    CHECK_EQ(g_ram[SER_TX_COUNT], 4);
    CHECK_EQ(g_ram[SER_TX_PENDING], 1);

    tx_ring(SER_TX_RING, 0x39FF, 0xFF, 1);
    r.a = 'B';
    ser_tx_put(&g_f, &r);
    check_regs(&r, 'B', 0x22, 0x33);
    CHECK_EQ(g_ram[0x39FF], 'B');
    CHECK_EQ(fw_rd16(&g_f, SER_TX_WRITE), 0x3900);
    CHECK_EQ(g_ram[SER_TX_COUNT], 0);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* ser_tx_get $C4B5. Nothing pending (ser_tx_pending bit 0 clear, here $02): A = NAK, Y kept, ser_tx_pending = 0 (the
   LDM writes the whole byte). Two bytes (count 2): the first, count 1, still pending, Y = 1; then the second, count 0,
   ser_tx_pending 0, Y = 0. Pending with count 0 (tested before the DEY, $C4CD): the byte, the count stays 0, pending
   0, Y = 0; from $39FF the read pointer steps to $3900. X kept; no SFR is touched. */
static void test_tx_get(void)
{
    setup();
    tx_ring(0x3905, 0x3905, 0, 0x02);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    ser_tx_get(&g_f, &r);
    check_regs(&r, SER_NAK, 0x22, 0x33);
    CHECK_EQ(g_ram[SER_TX_PENDING], 0);
    CHECK_EQ(fw_rd16(&g_f, SER_TX_READ), 0x3905);

    g_ram[0x3905] = 'x';
    g_ram[0x3906] = 'y';
    tx_ring(0x3905, 0x3907, 2, 1);
    ser_tx_get(&g_f, &r);
    check_regs(&r, 'x', 0x22, 1);
    CHECK_EQ(g_ram[SER_TX_COUNT], 1);
    CHECK_EQ(g_ram[SER_TX_PENDING], 1);
    ser_tx_get(&g_f, &r);
    check_regs(&r, 'y', 0x22, 0);
    CHECK_EQ(g_ram[SER_TX_COUNT], 0);
    CHECK_EQ(g_ram[SER_TX_PENDING], 0);
    CHECK_EQ(fw_rd16(&g_f, SER_TX_READ), 0x3907);

    g_ram[0x39FF] = 'z';
    tx_ring(0x39FF, 0x3900, 0, 1);
    r.y = 0x33;
    ser_tx_get(&g_f, &r);
    check_regs(&r, 'z', 0x22, 0);
    CHECK_EQ(fw_rd16(&g_f, SER_TX_READ), 0x3900);
    CHECK_EQ(g_ram[SER_TX_COUNT], 0);
    CHECK_EQ(g_ram[SER_TX_PENDING], 0);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* ser_tx_send $C504: the wait for the UART (the host's FW_WAIT_UART_TX), then A to UART_TX; A, X and Y kept. */
static void test_tx_send(void)
{
    setup();
    fw_regs r = { 'Z', 0x22, 0x33, 0 };
    ser_tx_send(&g_f, &r);
    check_regs(&r, 'Z', 0x22, 0x33);
    CHECK_EQ(g_nlog, 2);
    check_log(0, LOG_WAIT, FW_WAIT_UART_TX, 0);
    check_log(1, LOG_WRITE, SFR_UART_TX, 'Z');
    CHECK(!g_f.fault);
}

/* ser_hex_digit $C55A: the digit of the low nibble, Y = the nibble. ser_hex_digits $C53A: the two bytes made digits
   in place, the first (its caller's last push) by its low nibble, the second by its high nibble, each read for
   itself; A = X. ser_tx_hex $C571: A queued low digit first (volume 6 answers "60", $A5 "5A"); A, X and Y kept. */
static void test_hex(void)
{
    setup();
    fw_regs r = { 0x3C, 0x22, 0x33, 0 };
    ser_hex_digit(&g_f, &r);
    check_regs(&r, 'C', 0x22, 0x0C);
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    uint8_t pair[2] = { 0x5A, 0x5A };
    ser_hex_digits(&g_f, &r, pair);
    check_regs(&r, 0x22, 0x22, 0x33);
    CHECK_EQ(pair[0], 'A');
    CHECK_EQ(pair[1], '5');
    pair[0] = 0x07;
    pair[1] = 0xE0;
    ser_hex_digits(&g_f, &r, pair);
    CHECK_EQ(pair[0], '7');
    CHECK_EQ(pair[1], 'E');

    tx_ring(SER_TX_RING, SER_TX_RING, 0, 0);
    r = (fw_regs){ 6, 0x22, 0x33, 0 };
    ser_tx_hex(&g_f, &r);
    check_regs(&r, 6, 0x22, 0x33);
    r.a = 0xA5;
    ser_tx_hex(&g_f, &r);
    check_regs(&r, 0xA5, 0x22, 0x33);
    CHECK(memcmp(g_ram + SER_TX_RING, "605A", 4) == 0);
    CHECK_EQ(g_ram[SER_TX_COUNT], 4);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* ser_tx_service $C581, with every SFR access and wait in the ROM's order. Parallel mode (boot_dip bit 0): nothing.
   The host line P0.2 low: one read of P0 ($C585 BBC2), nothing sent. High with two bytes pending: P0 read (BBC2),
   P0 read and written with bit 5 cleared (CLB5), the wait and the byte to UART_TX (ser_tx_send); ser_status_due =
   ser_tx_pending = 1, so nothing more. Then the last byte: the same, ser_status_due = 0, the wait ($C596), P0 read
   (BBC2: still high) and P0.5 set again (SEB5). Last, the line dropping before the second test ($C59B BBC2 taken),
   pinned here: neither the machine (no host line) nor diff_serial (interrupts held, no time passing between two
   reads of an input) can drop it, the ROM's own time between $C585 and $C59B could: no SEB5. A and X kept, Y as
   ser_tx_get leaves it. It returns whether it sent a byte (M4.6c2 spec 2.5). */
static void test_tx_service(void)
{
    setup();
    g_ram[BOOT_DIP] = 1;
    g_sfr[SFR_P0] = 0x04;
    tx_ring(0x3900, 0x3901, 1, 1);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    CHECK(!ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 0);
    CHECK_EQ(g_ram[SER_TX_COUNT], 1);

    setup();
    g_sfr[SFR_P0] = 0x20;                         /* busy, the line low */
    tx_ring(0x3900, 0x3901, 1, 1);
    g_ram[SER_STATUS_DUE] = 1;
    CHECK(!ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 1);
    check_log(0, LOG_READ, SFR_P0, 0x20);
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);

    setup();
    g_sfr[SFR_P0] = 0x24;                         /* busy, the line high */
    g_ram[0x3900] = 'a';
    g_ram[0x3901] = 'b';
    tx_ring(0x3900, 0x3902, 2, 1);
    CHECK(ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 1);
    CHECK_EQ(g_nlog, 5);
    check_log(0, LOG_READ, SFR_P0, 0x24);
    check_log(1, LOG_READ, SFR_P0, 0x24);
    check_log(2, LOG_WRITE, SFR_P0, 0x04);
    check_log(3, LOG_WAIT, FW_WAIT_UART_TX, 0);
    check_log(4, LOG_WRITE, SFR_UART_TX, 'a');
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);
    CHECK(ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 0);
    CHECK_EQ(g_nlog, 14);
    check_log(5, LOG_READ, SFR_P0, 0x04);
    check_log(6, LOG_READ, SFR_P0, 0x04);
    check_log(7, LOG_WRITE, SFR_P0, 0x04);
    check_log(8, LOG_WAIT, FW_WAIT_UART_TX, 0);
    check_log(9, LOG_WRITE, SFR_UART_TX, 'b');
    check_log(10, LOG_WAIT, FW_WAIT_UART_TX, 0);
    check_log(11, LOG_READ, SFR_P0, 0x04);
    check_log(12, LOG_READ, SFR_P0, 0x04);
    check_log(13, LOG_WRITE, SFR_P0, 0x24);
    CHECK_EQ(g_ram[SER_STATUS_DUE], 0);
    CHECK_EQ(g_ram[SER_TX_PENDING], 0);

    setup();
    g_script_addr = SFR_P0;                       /* high, high, then low at the second BBC2 */
    g_script[0] = 0x24;
    g_script[1] = 0x24;
    g_script[2] = 0x00;
    g_script_n = 3;
    g_ram[0x3900] = 'c';
    tx_ring(0x3900, 0x3901, 1, 1);
    CHECK(ser_tx_service(&g_f, &r));
    CHECK_EQ(g_nlog, 7);
    check_log(5, LOG_WAIT, FW_WAIT_UART_TX, 0);
    check_log(6, LOG_READ, SFR_P0, 0x00);
    CHECK_EQ(g_sfr[SFR_P0], 0x00);                /* no SEB5 */
    CHECK(!g_f.fault);
    CHECK_EQ(g_ncalls, 0);
}

/* ser_tx_records $C5A4: ser_reply_id, then ser_reply_count and ser_reply_size in hex, the records (3 of 2 bytes),
   then CR; ser_reply_count counted down to 0; A, X and Y kept. */
static void test_tx_records(void)
{
    setup();
    tx_ring(SER_TX_RING, SER_TX_RING, 0, 0);
    g_ram[SER_REPLY_ID] = 'R';
    g_ram[SER_REPLY_COUNT] = 3;
    g_ram[SER_REPLY_SIZE] = 2;
    fw_wr16(&g_f, SER_REPLY_PTR, 0x2100);
    memcpy(g_ram + 0x2100, "abcdef", 6);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    ser_tx_records(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK(memcmp(g_ram + SER_TX_RING, "R3020abcdef\r", 12) == 0);
    CHECK_EQ(g_ram[SER_TX_COUNT], 12);
    CHECK_EQ(g_ram[SER_REPLY_COUNT], 0);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* The input ring's pointers. */
static void rx_ring(uint16_t read, uint16_t write)
{
    fw_wr16(&g_f, SER_RX_READ, read);
    fw_wr16(&g_f, SER_RX_WRITE, write);
}

/* ser_rx_next_page $C5EC: Y + 1, $39 wrapping to $21; A and X kept. */
static void test_rx_next_page(void)
{
    setup();
    fw_regs r = { 0x11, 0x22, 0x21, 0 };
    ser_rx_next_page(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x22);
    r.y = 0x38;
    ser_rx_next_page(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x21);
    CHECK_EQ(g_nlog, 0);
}

/* ser_rx_put $C3CC: the byte at ser_rx_write, which moves on; ser_rx_idle bit 0 cleared (its other bits kept); Y = 0,
   or at a page end the new page: from $21FF to $2200 (Y = $22), from $38FF to $2100 (the wrap, Y = $21). A and X
   kept; no SFR is touched. */
static void test_rx_put(void)
{
    setup();
    rx_ring(0x2100, 0x2105);
    g_ram[SER_RX_IDLE] = 0x03;
    fw_regs r = { 'h', 0x22, 0x33, 0 };
    ser_rx_put(&g_f, &r);
    check_regs(&r, 'h', 0x22, 0);
    CHECK_EQ(g_ram[0x2105], 'h');
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2106);
    CHECK_EQ(g_ram[SER_RX_IDLE], 0x02);
    rx_ring(0x2100, 0x21FF);
    ser_rx_put(&g_f, &r);
    check_regs(&r, 'h', 0x22, 0x22);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2200);
    rx_ring(0x2100, 0x38FF);
    ser_rx_put(&g_f, &r);
    check_regs(&r, 'h', 0x22, 0x21);
    CHECK_EQ(g_ram[0x38FF], 'h');
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2100);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* ser_rx_flush_cmd $C3FA: the command character, then ser_cmd_buf up to its first 0, as input; A and X kept, Y as
   the last ser_rx_put leaves it. */
static void test_rx_flush_cmd(void)
{
    setup();
    rx_ring(0x2100, 0x2100);
    g_ram[SER_CMD_CHAR] = '@';
    memcpy(g_ram + SER_CMD_BUF, "I+\0Z", 4);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    ser_rx_flush_cmd(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0);
    CHECK(memcmp(g_ram + 0x2100, "@I+", 3) == 0);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2103);
    CHECK(!g_f.fault);
}

/* ser_rx_byte $C37E with the mute bytes $01 and $18: ser_abort and cmd_index_reset bit 0 set (their other bits
   kept), cmd_char = ser_cmd_char, ser_rx_idle and ser_rx_idle_saved bit 0 set, ser_cmd_mode bit 0 cleared, the input
   ring emptied (read = write), A = the write pointer's high byte; X and Y kept; nothing queued. */
static void test_rx_mute(void)
{
    static const uint8_t k_mutes[] = { SER_CTRL_A, SER_CTRL_X };
    for (size_t i = 0; i < sizeof k_mutes; i++) {
        setup();
        rx_ring(0x2105, 0x2230);
        g_ram[SER_CMD_CHAR] = '@';
        g_ram[CMD_CHAR] = '#';
        g_ram[SER_ABORT] = 0x40;
        g_ram[CMD_INDEX_RESET] = 0x40;
        g_ram[SER_RX_IDLE] = 0x40;
        g_ram[SER_RX_IDLE_SAVED] = 0x40;
        g_ram[SER_CMD_MODE] = 0x41;
        fw_regs r = { k_mutes[i], 0x55, 0x33, 0 };
        ser_rx_byte(&g_f, &r);
        check_regs(&r, 0x22, 0x55, 0x33);
        CHECK_EQ(g_ram[SER_ABORT], 0x41);
        CHECK_EQ(g_ram[CMD_INDEX_RESET], 0x41);
        CHECK_EQ(g_ram[CMD_CHAR], '@');
        CHECK_EQ(g_ram[SER_RX_IDLE], 0x41);
        CHECK_EQ(g_ram[SER_RX_IDLE_SAVED], 0x41);
        CHECK_EQ(g_ram[SER_CMD_MODE], 0x40);
        CHECK_EQ(fw_rd16(&g_f, SER_RX_READ), 0x2230);
        CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2230);
        CHECK_EQ(g_ram[0x2230], 0);
        CHECK_EQ(g_nlog, 0);
    }
}

/* ser_rx_byte with the command character (ser_cmd_char, '#' here, not a constant): command mode on,
   ser_rx_idle_saved = ser_rx_idle (the whole byte), ser_cmd_ptr = ser_cmd_buf, ser_cmd_table_ofs = 0, ser_cmd_buf's
   5 bytes cleared (the sixth kept); Y = 0, A and X kept; nothing queued. Then a byte in command mode off: queued
   (ser_rx_put). */
static void test_rx_command_char(void)
{
    setup();
    rx_ring(0x2100, 0x2100);
    g_ram[SER_CMD_CHAR] = '#';
    g_ram[SER_RX_IDLE] = 0x05;
    memset(g_ram + SER_CMD_BUF, 'x', 6);
    g_ram[SER_CMD_TABLE_OFS] = 2;
    fw_regs r = { '#', 0x55, 0x33, 0 };
    ser_rx_byte(&g_f, &r);
    check_regs(&r, '#', 0x55, 0);
    CHECK_EQ(g_ram[SER_CMD_MODE], 1);
    CHECK_EQ(g_ram[SER_RX_IDLE_SAVED], 0x05);
    CHECK_EQ(fw_rd16(&g_f, SER_CMD_PTR), SER_CMD_BUF);
    CHECK_EQ(g_ram[SER_CMD_TABLE_OFS], 0);
    for (unsigned i = 0; i < 5; i++)
        CHECK_EQ(g_ram[SER_CMD_BUF + i], 0);
    CHECK_EQ(g_ram[SER_CMD_BUF + 5], 'x');
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2100);

    g_ram[SER_CMD_MODE] = 0;
    r.a = '@';
    ser_rx_byte(&g_f, &r);
    check_regs(&r, '@', 0x55, 0);
    CHECK_EQ(g_ram[0x2100], '@');
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2101);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* The zero page cmd_imm_match restores: $44-$49 and $4C. */
static const uint8_t k_zp[] = {
    CMD_TABLE_PTR, CMD_TABLE_PTR + 1, CMD_TEXT_PTR, CMD_TEXT_PTR + 1, CMD_PAT_PTR, CMD_PAT_PTR + 1, CMD_TABLE_OFS,
};

/* Command mode on with buf in ser_cmd_buf and ser_cmd_ptr after it; both rings empty; marks in the zero page that
   cmd_imm_match restores. */
static void imm_setup(const char *buf)
{
    setup();
    rx_ring(0x2100, 0x2100);
    tx_ring(SER_TX_RING, SER_TX_RING, 0, 0);
    g_ram[SER_CMD_CHAR] = '@';
    g_ram[SER_CMD_MODE] = 1;
    memcpy(g_ram + SER_CMD_BUF, buf, strlen(buf));
    fw_wr16(&g_f, SER_CMD_PTR, (uint16_t)(SER_CMD_BUF + strlen(buf)));
    for (size_t i = 0; i < sizeof k_zp; i++)
        g_ram[k_zp[i]] = (uint8_t)(0xA0 + i);
}

static void check_zp(void)
{
    for (size_t i = 0; i < sizeof k_zp; i++)
        CHECK_EQ(g_ram[k_zp[i]], 0xA0 + i);
}

/* ser_rx_byte in command mode: cmd_imm_match $C413. "I" alone is a partial match ($C450): the byte at ser_cmd_ptr,
   ser_cmd_table_ofs = the entry matched (0, "I+"), Y = it, ser_cmd_ptr + 1, still command mode, nothing queued; from
   $20FF the pointer steps to $2100 ($C458). No match ("X"): command mode off, "@X" queued (ser_rx_flush_cmd), Y = 0.
   A and X kept, zero page restored. */
static void test_imm_partial_and_none(void)
{
    imm_setup("");
    fw_regs r = { 'I', 0x55, 0x33, 0 };
    ser_rx_byte(&g_f, &r);
    check_regs(&r, 'I', 0x55, 0);
    CHECK_EQ(g_ram[SER_CMD_BUF], 'I');
    CHECK_EQ(fw_rd16(&g_f, SER_CMD_PTR), SER_CMD_BUF + 1);
    CHECK_EQ(g_ram[SER_CMD_TABLE_OFS], 0);
    CHECK_EQ(g_ram[SER_CMD_MODE], 1);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2100);
    check_zp();

    imm_setup("I");
    fw_wr16(&g_f, SER_CMD_PTR, 0x20FF);
    ser_rx_byte(&g_f, &r);
    CHECK_EQ(g_ram[0x20FF], 'I');
    CHECK_EQ(fw_rd16(&g_f, SER_CMD_PTR), 0x2100);
    CHECK_EQ(g_ram[SER_CMD_MODE], 1);
    check_zp();

    imm_setup("");
    r = (fw_regs){ 'X', 0x55, 0x33, 0 };
    ser_rx_byte(&g_f, &r);
    check_regs(&r, 'X', 0x55, 0);
    CHECK_EQ(g_ram[SER_CMD_MODE], 0);
    CHECK(memcmp(g_ram + 0x2100, "@X", 2) == 0);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2102);
    check_zp();
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* @I+ (cmd_imm_index_add, through cmd_imm_vector): the first after a mute (cmd_index_reset bit 0) zeroes
   cmd_index_count and clears the bit, then cmd_index_count + 1; command mode off and "@I+" queued; Y = 0 (the last
   ser_rx_put's). Without the reset the count goes on (9 to 10). A and X kept, zero page restored. */
static void test_imm_index_add(void)
{
    imm_setup("I");
    g_ram[CMD_INDEX_RESET] = 0x41;
    g_ram[CMD_INDEX_COUNT] = 9;
    fw_regs r = { '+', 0x55, 0x33, 0 };
    ser_rx_byte(&g_f, &r);
    check_regs(&r, '+', 0x55, 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_IMM_VECTOR), ROM_CMD_IMM_INDEX_ADD);
    CHECK_EQ(g_ram[CMD_INDEX_COUNT], 1);
    CHECK_EQ(g_ram[CMD_INDEX_RESET], 0x40);
    CHECK_EQ(g_ram[SER_CMD_MODE], 0);
    CHECK(memcmp(g_ram + 0x2100, "@I+", 3) == 0);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2103);
    check_zp();

    imm_setup("I");
    g_ram[CMD_INDEX_COUNT] = 9;
    ser_rx_byte(&g_f, &r);
    CHECK_EQ(g_ram[CMD_INDEX_COUNT], 10);
    CHECK_EQ(g_ram[CMD_INDEX_RESET], 0);
    CHECK_EQ(g_nlog, 0);
    CHECK(!g_f.fault);
}

/* @I? (cmd_imm_index_query): the reply I, cmd_index_count in hex ($12: "21"), then T, or M when ser_rx_idle_saved
   bit 0 is set and boot_speaking bit 0 clear ($C4A7); command mode off, ser_rx_idle = ser_rx_idle_saved, nothing
   queued; Y = 0, A and X kept, zero page restored. */
static void test_imm_index_query(void)
{
    static const struct { uint8_t idle_saved, speaking; const char *reply; } k[] = {
        { 0x00, 0x01, "I21T" },   /* text waiting */
        { 0x01, 0x00, "I21M" },   /* neither */
        { 0x01, 0x01, "I21T" },   /* speech in progress */
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        imm_setup("I");
        g_ram[CMD_INDEX_COUNT] = 0x12;
        g_ram[SER_RX_IDLE] = 0x80;
        g_ram[SER_RX_IDLE_SAVED] = k[i].idle_saved;
        g_ram[BOOT_SPEAKING] = k[i].speaking;
        fw_regs r = { '?', 0x55, 0x33, 0 };
        ser_rx_byte(&g_f, &r);
        check_regs(&r, '?', 0x55, 0);
        CHECK_EQ(fw_rd16(&g_f, CMD_IMM_VECTOR), ROM_CMD_IMM_INDEX_QUERY);
        CHECK(memcmp(g_ram + SER_TX_RING, k[i].reply, 4) == 0);
        CHECK_EQ(g_ram[SER_TX_COUNT], 4);
        CHECK_EQ(g_ram[SER_CMD_MODE], 0);
        CHECK_EQ(g_ram[SER_RX_IDLE], k[i].idle_saved);
        CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2100);
        check_zp();
        CHECK_EQ(g_nlog, 0);
        CHECK(!g_f.fault);
    }
}

/* A handler address cmd_imm_match does not know (a patched copy of the converted data: "I+"'s record, "I+", 0, then
   the handler, pointing at $1234) is a fault at that address, after cmd_imm_vector is written; no handler runs and
   zero page is still restored. */
static void test_imm_unknown_handler(void)
{
    static fw_image img;
    imm_setup("I");
    img = *fw_image_get();
    img.rom[CMD_IMM_PATTERNS + 3] = 0x34;
    img.rom[CMD_IMM_PATTERNS + 4] = 0x12;
    fw_init(&g_f, g_ram, &img, &g_host);
    fw_regs r = { '+', 0x55, 0x33, 0 };
    ser_rx_byte(&g_f, &r);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0x1234);
    CHECK_EQ(fw_rd16(&g_f, CMD_IMM_VECTOR), 0x1234);
    CHECK_EQ(g_ram[SER_CMD_MODE], 1);
    check_zp();
}

/* The log from entry i on is exactly want[0..n-1], and nothing follows it. */
static void check_seq(unsigned i, const log_entry *want, unsigned n)
{
    CHECK_EQ(g_nlog, i + n);
    for (unsigned k = 0; k < n; k++)
        check_log(i + k, want[k].kind, want[k].addr, want[k].value);
}

#define R(a, v) { LOG_READ, a, v }
#define W(a, v) { LOG_WRITE, a, v }

/* The SFRs as the machine has them in serial mode after the boot (boot_init_ports, boot_init_host_lines): the _INT1
   enable (ICR1 $01), INT4 enabled with P0.2 low (ICR3 $48), the UART receive interrupt on (UART_CTRL $D1), P0 $00;
   the UART holding received the byte b with no error (UART_STATUS $03). Both rings empty, serial mode. */
static void int1_setup(uint8_t b)
{
    setup();
    rx_ring(0x2100, 0x2100);
    tx_ring(SER_TX_RING, SER_TX_RING, 0, 0);
    g_ram[SER_CMD_CHAR] = '@';
    g_sfr[SFR_ICR1] = 0x01;
    g_sfr[SFR_ICR3] = 0x48;
    g_sfr[SFR_UART_CTRL] = 0xD1;
    g_sfr[SFR_UART_STATUS] = 0x03;
    g_sfr[SFR_UART_RX] = b;
}

/* isr_int1 $C2B4 in serial mode, every SFR access in the ROM's order. A clean UART byte ($C33C): ICR1 read (BBS1),
   UART_CTRL bit 4 cleared, P0.5 set, ICR3 bits 6 and 7 cleared (each a read and a write), UART_STATUS read (no error
   bit), UART_RX read (the byte, to ser_rx_byte: queued), ICR3 bit 6 set, UART_RX read and written back, UART_CTRL
   bit 4 set, P0.5 cleared, P0.3 cleared. A, X and Y as they came. */
static void test_int1_serial(void)
{
    static const log_entry k[] = {
        R(SFR_ICR1, 0x01),
        R(SFR_UART_CTRL, 0xD1), W(SFR_UART_CTRL, 0xC1),
        R(SFR_P0, 0x00), W(SFR_P0, 0x20),
        R(SFR_ICR3, 0x48), W(SFR_ICR3, 0x08),
        R(SFR_ICR3, 0x08), W(SFR_ICR3, 0x08),
        R(SFR_UART_STATUS, 0x03),
        R(SFR_UART_RX, 'h'),
        R(SFR_ICR3, 0x08), W(SFR_ICR3, 0x48),
        R(SFR_UART_RX, 'h'), W(SFR_UART_RX, 'h'),
        R(SFR_UART_CTRL, 0xC1), W(SFR_UART_CTRL, 0xD1),
        R(SFR_P0, 0x20), W(SFR_P0, 0x00),
        R(SFR_P0, 0x00), W(SFR_P0, 0x00),
    };
    int1_setup('h');
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_int1(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    check_seq(0, k, sizeof k / sizeof k[0]);
    CHECK_EQ(g_ram[0x2100], 'h');
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2101);
    CHECK(!g_f.fault);
}

/* A UART byte with an error bit (UART_STATUS bit 2, here $07): no read of the byte and no ser_rx_byte, then the same
   tail ($C35B). The machine's UART never sets an error bit (m50734.c: UART_STATUS is $02 or $03), so this side of the
   BNE at $C350 is pinned here. */
static void test_int1_uart_error(void)
{
    static const log_entry k[] = {
        R(SFR_ICR1, 0x01),
        R(SFR_UART_CTRL, 0xD1), W(SFR_UART_CTRL, 0xC1),
        R(SFR_P0, 0x00), W(SFR_P0, 0x20),
        R(SFR_ICR3, 0x48), W(SFR_ICR3, 0x08),
        R(SFR_ICR3, 0x08), W(SFR_ICR3, 0x08),
        R(SFR_UART_STATUS, 0x07),
        R(SFR_ICR3, 0x08), W(SFR_ICR3, 0x48),
        R(SFR_UART_RX, 'h'), W(SFR_UART_RX, 'h'),
        R(SFR_UART_CTRL, 0xC1), W(SFR_UART_CTRL, 0xD1),
        R(SFR_P0, 0x20), W(SFR_P0, 0x00),
        R(SFR_P0, 0x00), W(SFR_P0, 0x00),
    };
    int1_setup('h');
    g_sfr[SFR_UART_STATUS] = 0x07;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_int1(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    check_seq(0, k, sizeof k / sizeof k[0]);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2100);   /* nothing queued */
    CHECK(!g_f.fault);
}

/* The _INT1 latch path ($C367): ICR1 bit 1 set; P1_LATCH read (to ser_rx_byte: queued), ICR1 bits 0 and 1 cleared,
   then bit 0 set again, P0.3 cleared; the UART untouched. */
static void test_int1_latch(void)
{
    static const log_entry k[] = {
        R(SFR_ICR1, 0x03),
        R(SFR_P1_LATCH, 'L'),
        R(SFR_ICR1, 0x03), W(SFR_ICR1, 0x02),
        R(SFR_ICR1, 0x02), W(SFR_ICR1, 0x00),
        R(SFR_ICR1, 0x00), W(SFR_ICR1, 0x01),
        R(SFR_P0, 0x00), W(SFR_P0, 0x00),
    };
    int1_setup('h');
    g_sfr[SFR_ICR1] = 0x03;
    g_sfr[SFR_P1_LATCH] = 'L';
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_int1(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    check_seq(0, k, sizeof k / sizeof k[0]);
    CHECK_EQ(g_ram[0x2100], 'L');
    CHECK(!g_f.fault);
}

/* Parallel mode (boot_dip bit 0) with the host's P0 level p0, isr_par_state and the P1_LATCH byte; isr_int1 run with
   A, X, Y = $11, $22, $33, which must come back. */
static void int1_parallel_run(uint8_t p0, uint8_t state, uint8_t latch)
{
    int1_setup('h');
    g_ram[BOOT_DIP] = 1;
    g_ram[ISR_PAR_STATE] = state;
    g_sfr[SFR_P0] = p0;
    g_sfr[SFR_P1_LATCH] = latch;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_int1(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK(!g_f.fault);
}

/* isr_int1's parallel paths ($C2BA-$C33B), every SFR access in the ROM's order. P0.0 high: the _INT1 request
   cleared, nothing else ($C2BD). P0.4 low: P1 released (P1 = 0 with P1_DIR $FF, then P1_DIR 0); with isr_par_state
   bit 0 clear P0.3 pulsed low and high around the request's clearing ($C2C6), with it set the bit cleared, P0.3
   cleared and P1_LATCH written 0 ($C2D6). P0.4 high: P0.3 and the request cleared, P1 = 1 out, the _INT1 enable off
   while P1_LATCH is read, then on again ($C331); 5: isr_par_state bit 1 cleared and bit 0 set, the next reply byte
   (ser_tx_get) onto P1; 3 (below 5): nothing more; 7 or more with isr_par_state bit 0 clear: P1 = the byte with bit 0
   flipped, isr_par_state = 1 (bits 0 and 1 flipped and 3 when ser_tx_pending bit 0 is set), P0.3 set, the byte to
   ser_rx_byte; with isr_par_state bit 0 set: nothing more. */
static void test_int1_parallel(void)
{
    static const log_entry k_ack[] = { R(SFR_P0, 0x01), R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01) };
    int1_parallel_run(0x01, 0, 'A');
    check_seq(0, k_ack, 3);

    static const log_entry k_idle[] = {
        R(SFR_P0, 0x00), R(SFR_P0, 0x00),
        W(SFR_P1, 0x00), W(SFR_P1_DIR, 0xFF),
        R(SFR_P0, 0x00), W(SFR_P0, 0x00),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01),
        R(SFR_P0, 0x00), W(SFR_P0, 0x08),
        W(SFR_P1_DIR, 0x00),
    };
    int1_parallel_run(0x00, 0x02, 'A');
    check_seq(0, k_idle, sizeof k_idle / sizeof k_idle[0]);
    CHECK_EQ(g_ram[ISR_PAR_STATE], 0x02);

    static const log_entry k_done[] = {
        R(SFR_P0, 0x00), R(SFR_P0, 0x00),
        W(SFR_P1, 0x00), W(SFR_P1_DIR, 0xFF),
        R(SFR_P0, 0x00), W(SFR_P0, 0x00),
        W(SFR_P1_LATCH, 0x00),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01),
        W(SFR_P1_DIR, 0x00),
    };
    int1_parallel_run(0x00, 0x03, 'A');
    check_seq(0, k_done, sizeof k_done / sizeof k_done[0]);
    CHECK_EQ(g_ram[ISR_PAR_STATE], 0x02);

    static const log_entry k_reply[] = {
        R(SFR_P0, 0x10), R(SFR_P0, 0x10),
        R(SFR_P0, 0x10), W(SFR_P0, 0x10),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01),
        W(SFR_P1, 0x01), W(SFR_P1_DIR, 0xFF),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x00),
        R(SFR_P1_LATCH, 5),
        W(SFR_P1, 'r'), W(SFR_P1_DIR, 0xFF),
        R(SFR_ICR1, 0x00), W(SFR_ICR1, 0x01),
    };
    int1_setup('h');
    g_ram[SER_TX_RING] = 'r';
    tx_ring(SER_TX_RING, SER_TX_RING + 1, 1, 1);
    g_ram[BOOT_DIP] = 1;
    g_ram[ISR_PAR_STATE] = 0x02;
    g_sfr[SFR_P0] = 0x10;
    g_sfr[SFR_P1_LATCH] = 5;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_int1(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    check_seq(0, k_reply, sizeof k_reply / sizeof k_reply[0]);
    CHECK_EQ(g_ram[ISR_PAR_STATE], 0x01);
    CHECK_EQ(g_ram[SER_TX_PENDING], 0);   /* the byte taken */

    static const log_entry k_code[] = {
        R(SFR_P0, 0x10), R(SFR_P0, 0x10),
        R(SFR_P0, 0x10), W(SFR_P0, 0x10),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01),
        W(SFR_P1, 0x01), W(SFR_P1_DIR, 0xFF),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x00),
        R(SFR_P1_LATCH, 3),
        R(SFR_ICR1, 0x00), W(SFR_ICR1, 0x01),
    };
    int1_parallel_run(0x10, 0x02, 3);
    check_seq(0, k_code, sizeof k_code / sizeof k_code[0]);
    CHECK_EQ(g_ram[ISR_PAR_STATE], 0x02);

    static const log_entry k_data[] = {
        R(SFR_P0, 0x10), R(SFR_P0, 0x10),
        R(SFR_P0, 0x10), W(SFR_P0, 0x10),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01),
        W(SFR_P1, 0x01), W(SFR_P1_DIR, 0xFF),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x00),
        R(SFR_P1_LATCH, 'A'),
        W(SFR_P1, 'A' ^ 0x01), W(SFR_P1_DIR, 0xFF),
        R(SFR_P0, 0x10), W(SFR_P0, 0x18),
        R(SFR_ICR1, 0x00), W(SFR_ICR1, 0x01),
    };
    int1_parallel_run(0x10, 0x02, 'A');
    check_seq(0, k_data, sizeof k_data / sizeof k_data[0]);
    CHECK_EQ(g_ram[ISR_PAR_STATE], 0x01);
    CHECK_EQ(g_ram[0x2100], 'A');

    int1_setup('h');                      /* the same with a reply pending: bits 0 and 1 flipped, state 3 */
    g_ram[BOOT_DIP] = 1;
    g_ram[SER_TX_PENDING] = 1;
    g_sfr[SFR_P0] = 0x10;
    g_sfr[SFR_P1_LATCH] = 'A';
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    isr_int1(&g_f, &r);
    check_log(11, LOG_WRITE, SFR_P1, 'A' ^ 0x03);
    CHECK_EQ(g_ram[ISR_PAR_STATE], 0x03);
    CHECK_EQ(g_ram[0x2100], 'A');

    static const log_entry k_busy[] = {
        R(SFR_P0, 0x10), R(SFR_P0, 0x10),
        R(SFR_P0, 0x10), W(SFR_P0, 0x10),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x01),
        W(SFR_P1, 0x01), W(SFR_P1_DIR, 0xFF),
        R(SFR_ICR1, 0x01), W(SFR_ICR1, 0x00),
        R(SFR_P1_LATCH, 'A'),
        R(SFR_ICR1, 0x00), W(SFR_ICR1, 0x01),
    };
    int1_parallel_run(0x10, 0x01, 'A');
    check_seq(0, k_busy, sizeof k_busy / sizeof k_busy[0]);
    CHECK_EQ(fw_rd16(&g_f, SER_RX_WRITE), 0x2100);   /* nothing queued */
}

/* isr_int4_host_line $C3E0: P0.2 high, isr_host_line_high (ICR3 = $40, P0.5 set, ser_status_due = 1); low,
   isr_host_line_low (ICR3 = $48, P0.5 cleared). A, X and Y kept. */
static void test_int4(void)
{
    static const log_entry k_high[] = { R(SFR_P0, 0x04), W(SFR_ICR3, 0x40), R(SFR_P0, 0x04), W(SFR_P0, 0x24) };
    static const log_entry k_low[] = { R(SFR_P0, 0x20), W(SFR_ICR3, 0x48), R(SFR_P0, 0x20), W(SFR_P0, 0x00) };
    setup();
    g_sfr[SFR_P0] = 0x04;
    g_sfr[SFR_ICR3] = 0xC8;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_int4_host_line(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    check_seq(0, k_high, 4);
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);

    setup();
    g_sfr[SFR_P0] = 0x20;
    g_ram[SER_STATUS_DUE] = 7;
    isr_int4_host_line(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    check_seq(0, k_low, 4);
    CHECK_EQ(g_ram[SER_STATUS_DUE], 7);
    CHECK(!g_f.fault);
}

void suite_fw_ser(void)
{
    CHECK(fw_image_get() != NULL);
    test_sfr_helpers();
    test_sfr_faults();
    test_wait();
    test_data();
    test_tx_next_page();
    test_tx_put();
    test_tx_get();
    test_tx_send();
    test_hex();
    test_tx_service();
    test_tx_records();
    test_rx_next_page();
    test_rx_put();
    test_rx_flush_cmd();
    test_rx_mute();
    test_rx_command_char();
    test_imm_partial_and_none();
    test_imm_index_add();
    test_imm_index_query();
    test_imm_unknown_handler();
    test_int1_serial();
    test_int1_uart_error();
    test_int1_latch();
    test_int1_parallel();
    test_int4();
}
