/* Unit checks of the serial code (M4.6a spec 4.5) on the converted data, with a stub host that keeps an ordered log
   of the SFR reads and writes and of the waits, so that the order of every access is pinned, and that fails every
   ROM call (the serial code makes none). Each expected value is worked out from the dump of the routine
   (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests (diff_lts,
   diff_serial) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG     256
#define SFR_P0      0xF6
#define SFR_ICR1    0xFF

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

void suite_fw_ser(void)
{
    CHECK(fw_image_get() != NULL);
    test_sfr_helpers();
    test_sfr_faults();
    test_wait();
    test_data();
}
