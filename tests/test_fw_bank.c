/* Unit checks of the bank code and the command dispatcher (M4.6c1 spec 4.6): the firmware's selected bank
   (f->bank), ROM reads under another bank, the bank entry and the unhooked ROM call, with a stub host that logs, in
   order, every SFR read and write, every read of another bank, every bank entry and every unhooked ROM call. Each
   expected value is worked out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. */
#include <string.h>
#include "test.h"
#include "firmware/bank_addr.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 512
#define SFR_P2  0xF0
#define SFR_P0  0xF6

typedef enum { EV_READ, EV_WRITE, EV_BANK, EV_ENTER, EV_CALL } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value (a); EV_BANK a read of another bank at addr, the
   value (a) the stub returned; EV_ENTER the bank entry addr with a and x; EV_CALL an unhooked ROM call of addr with
   the registers a, x and y. */
typedef struct { ev_kind kind; uint16_t addr; uint8_t a, x, y; } event;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static uint8_t g_other[0x10000]; /* what read_bank returns: another bank's bytes ($FF: an empty socket) */
static fw g_f;
static fw_host g_host;
static uint8_t g_sfr[0x100];     /* the stub's SFRs: a read returns g_sfr[addr], a write sets it */
static event g_log[MAX_LOG];
static unsigned g_nlog;
static fw_call_result g_call_result;   /* what the stub's rom_call_unhooked returns */

static void log_add(ev_kind kind, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    if (g_nlog < MAX_LOG)
        g_log[g_nlog] = (event){ kind, addr, a, x, y };
    g_nlog++;
}

static uint8_t stub_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    log_add(EV_READ, addr, g_sfr[addr], 0, 0);
    return g_sfr[addr];
}

static void stub_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    g_sfr[addr] = value;
    log_add(EV_WRITE, addr, value, 0, 0);
}

static uint8_t stub_read_bank(void *ctx, uint16_t addr)
{
    (void)ctx;
    log_add(EV_BANK, addr, g_other[addr], 0, 0);
    return g_other[addr];
}

static void stub_enter(void *ctx, uint16_t addr, uint8_t a, uint8_t x)
{
    (void)ctx;
    log_add(EV_ENTER, addr, a, x, 0);
}

/* The unhooked ROM call: logged with its registers; it returns with A, X and Y each + 1 when g_call_result says so. */
static fw_call_result stub_rom_call_unhooked(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    CHECK_EQ(budget, FW_ROM_CALL_BUDGET);
    log_add(EV_CALL, addr, r->a, r->x, r->y);
    r->a++;
    r->x++;
    r->y++;
    return g_call_result;
}

/* Zeroed RAM and SFRs, every other bank empty ($FF), the stub host (no hooked ROM call, no wait: the bank code makes
   neither), nothing logged. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_other, 0xFF, sizeof g_other);
    memset(g_sfr, 0, sizeof g_sfr);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
    g_host.read_sfr = stub_read_sfr;
    g_host.write_sfr = stub_write_sfr;
    g_host.read_bank = stub_read_bank;
    g_host.enter = stub_enter;
    g_host.rom_call_unhooked = stub_rom_call_unhooked;
    g_nlog = 0;
    g_call_result = FW_CALL_RETURNED;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The log is exactly the n events of want. */
static void check_log(const event *want, unsigned n)
{
    CHECK_EQ(g_nlog, n);
    for (unsigned i = 0; i < n && i < g_nlog && i < MAX_LOG; i++) {
        CHECK_EQ(g_log[i].kind, want[i].kind);
        CHECK_EQ(g_log[i].addr, want[i].addr);
        CHECK_EQ(g_log[i].a, want[i].a);
        CHECK_EQ(g_log[i].x, want[i].x);
        CHECK_EQ(g_log[i].y, want[i].y);
    }
}

/* f->bank (M4.6c1 spec 3.1): P2_MAIN_BANK after fw_init; every P2 write through fw_wr_sfr8, fw_sfr_seb and fw_sfr_clb
   sets it to bits 2-6 of the value written (with the window from bit 7, M4.6b spec 3.4); other SFRs leave it. */
static void test_bank_tracking(void)
{
    static const event want[] = {
        { EV_WRITE, SFR_P2, 0xF9, 0, 0 },   /* the window, bank $78 (P2_BANK_5), P2.0 */
        { EV_READ, SFR_P2, 0xF9, 0, 0 },    /* CLB7 P2: a read, then a write */
        { EV_WRITE, SFR_P2, 0x79, 0, 0 },
        { EV_READ, SFR_P2, 0x50, 0, 0 },    /* SEB5 P2 with the pins at $50 (P2_BANK_2) */
        { EV_WRITE, SFR_P2, 0x70, 0, 0 },   /* the main bank */
        { EV_WRITE, SFR_P0, 0x10, 0, 0 },   /* not P2 */
    };
    setup();
    CHECK_EQ(g_f.bank, P2_MAIN_BANK);
    CHECK(!g_f.low_window);
    fw_wr_sfr8(&g_f, SFR_P2, 0xF9);
    CHECK_EQ(g_f.bank, P2_BANK_5);
    CHECK(g_f.low_window);
    fw_sfr_clb(&g_f, SFR_P2, 7);
    CHECK_EQ(g_f.bank, P2_BANK_5);
    CHECK(!g_f.low_window);
    g_sfr[SFR_P2] = 0x50;
    fw_sfr_seb(&g_f, SFR_P2, 5);
    CHECK_EQ(g_f.bank, P2_MAIN_BANK);
    fw_wr_sfr8(&g_f, SFR_P0, 0x10);
    CHECK_EQ(g_f.bank, P2_MAIN_BANK);
    check_log(want, 6);
    CHECK(!g_f.fault);
}

/* fw_rom8 (M4.6c1 spec 3.1): under the main bank $8000-$FFFF read the converted image; under another bank they go to
   the host's read_bank (an empty socket: $FF); $4000-$7FFF read the image under any bank. Without read_bank, or
   without a host, a read of $8000-$FFFF under another bank is a fault at the address. */
static void test_rom8_bank(void)
{
    static const event want[] = { { EV_BANK, 0xC561, 0x5A, 0, 0 }, { EV_BANK, 0xFFE0, 0xFF, 0, 0 } };
    setup();
    CHECK_EQ(fw_rom8(&g_f, 0xC561), '0');       /* ser_hex_chars[0], converted */
    CHECK_EQ(fw_rom8(&g_f, 0x4000), fw_image_get()->rom[0x4000]);
    CHECK_EQ(g_nlog, 0);
    g_f.bank = P2_BANK_5;
    g_other[0xC561] = 0x5A;
    CHECK_EQ(fw_rom8(&g_f, 0xC561), 0x5A);
    CHECK_EQ(fw_rom8(&g_f, 0xFFE0), 0xFF);      /* the empty socket */
    CHECK_EQ(fw_rom8(&g_f, 0x4000), fw_image_get()->rom[0x4000]);   /* not banked: no host call */
    check_log(want, 2);
    CHECK_EQ(fw_mem8(&g_f, 0x9800), 0xFF);      /* fw_mem8 reaches it too */
    CHECK_EQ(g_nlog, 3);
    CHECK(!g_f.fault);

    setup();
    g_host.read_bank = NULL;
    g_f.bank = P2_BANK_7;
    CHECK_EQ(fw_rom8(&g_f, 0xC561), 0xFF);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC561);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    f.bank = P2_BANK_7;
    CHECK_EQ(fw_rom8(&f, 0x9800), 0xFF);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x9800);
}

/* fw_enter (M4.6c1 spec 3.2): the host's enter with the address, A and X, then f->unwound (not a fault). Without
   enter, or without a host, a fault at the address and nothing entered. */
static void test_enter(void)
{
    static const event want[] = { { EV_ENTER, BANK_ENTRY_WARM, 0x02, 0xFF, 0 } };
    setup();
    fw_enter(&g_f, BANK_ENTRY_WARM, 0x02, 0xFF);
    check_log(want, 1);
    CHECK(g_f.unwound);
    CHECK(!g_f.fault);

    setup();
    g_host.enter = NULL;
    fw_enter(&g_f, BANK_ENTRY_COLD, 0, 0);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, BANK_ENTRY_COLD);
    CHECK(!g_f.unwound);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_enter(&f, BANK_ENTRY_COLD, 0, 0);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, BANK_ENTRY_COLD);
}

/* fw_rom_call_unhooked (M4.6c1 spec 3.2): the host's rom_call_unhooked with the registers, never its rom_call; a
   returned call gives the exit registers; a failed one is a fault at the address; an unwound one sets f->unwound.
   Without rom_call_unhooked, or without a host, a fault. */
static void test_rom_call_unhooked(void)
{
    static const event want[] = { { EV_CALL, 0xA893, 0x03, 0x03, 0x01 } };
    setup();
    fw_regs r = { 3, 3, 1, 0 };
    CHECK(fw_rom_call_unhooked(&g_f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    check_log(want, 1);
    CHECK_EQ(r.a, 4);
    CHECK_EQ(r.x, 4);
    CHECK_EQ(r.y, 2);
    CHECK(!g_f.fault);

    setup();
    g_call_result = FW_CALL_FAILED;
    CHECK(!fw_rom_call_unhooked(&g_f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xA893);

    setup();
    g_call_result = FW_CALL_UNWOUND;
    CHECK(!fw_rom_call_unhooked(&g_f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(g_f.unwound);
    CHECK(!g_f.fault);

    setup();
    g_host.rom_call_unhooked = NULL;
    CHECK(!fw_rom_call_unhooked(&g_f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xA893);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    CHECK(!fw_rom_call_unhooked(&f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xA893);
}

void suite_fw_bank(void)
{
    CHECK(fw_image_get() != NULL);
    test_bank_tracking();
    test_rom8_bank();
    test_enter();
    test_rom_call_unhooked();
}
