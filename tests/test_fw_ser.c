/* Unit checks of the serial code (M4.6a spec 4.5) on the converted data, with a stub host that keeps an ordered log
   of the SFR reads and writes and of the waits, so that the order of every access is pinned, and that fails every
   ROM call (the serial code makes none). Each expected value is worked out from the dump of the routine
   (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests (diff_lts,
   diff_serial) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/ser.h"
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

static bool stub_wait(void *ctx, fw_wait_kind kind)
{
    (void)ctx;
    log_add(LOG_WAIT, (uint8_t)kind, 0);
    return g_wait_ok;
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

/* Without a host, below $DA (RAM, not an SFR), or with a host lacking write_sfr, the write helpers fault at the
   address and touch nothing: SEB/CLB do not read either. */
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
}

/* fw_wait (M4.6a spec 3.3): one host wait of the kind; no host, a host without wait, or a wait that fails is a fault
   at the kind's ROM loop, ser_tx_wait $C500. */
static void test_wait(void)
{
    setup();
    fw_wait(&g_f, FW_WAIT_UART_TX);
    CHECK_EQ(g_nlog, 1);
    check_log(0, LOG_WAIT, FW_WAIT_UART_TX, 0);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_UART_TX);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, FW_WAIT_UART_TX_LOOP);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_UART_TX);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC500);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wait(&f, FW_WAIT_UART_TX);
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
   ser_tx_get leaves it. */
static void test_tx_service(void)
{
    setup();
    g_ram[BOOT_DIP] = 1;
    g_sfr[SFR_P0] = 0x04;
    tx_ring(0x3900, 0x3901, 1, 1);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 0);
    CHECK_EQ(g_ram[SER_TX_COUNT], 1);

    setup();
    g_sfr[SFR_P0] = 0x20;                         /* busy, the line low */
    tx_ring(0x3900, 0x3901, 1, 1);
    g_ram[SER_STATUS_DUE] = 1;
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 1);
    check_log(0, LOG_READ, SFR_P0, 0x20);
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);

    setup();
    g_sfr[SFR_P0] = 0x24;                         /* busy, the line high */
    g_ram[0x3900] = 'a';
    g_ram[0x3901] = 'b';
    tx_ring(0x3900, 0x3902, 2, 1);
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 1);
    CHECK_EQ(g_nlog, 5);
    check_log(0, LOG_READ, SFR_P0, 0x24);
    check_log(1, LOG_READ, SFR_P0, 0x24);
    check_log(2, LOG_WRITE, SFR_P0, 0x04);
    check_log(3, LOG_WAIT, FW_WAIT_UART_TX, 0);
    check_log(4, LOG_WRITE, SFR_UART_TX, 'a');
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);
    ser_tx_service(&g_f, &r);
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
    ser_tx_service(&g_f, &r);
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
}
