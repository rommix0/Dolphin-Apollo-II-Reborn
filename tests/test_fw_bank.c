/* Unit checks of the bank code and the command dispatcher (M4.6c1 spec 4.6): the firmware's selected bank
   (f->bank), ROM reads under another bank, the bank entry and the unhooked ROM call, with a stub host that logs, in
   order, every SFR read and write, every read of another bank, every bank entry and every unhooked ROM call. Each
   expected value is worked out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/bank.h"
#include "firmware/bank_addr.h"
#include "firmware/cmd_addr.h"
#include "firmware/cmd_run.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 512
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

/* ---- the bank code (M4.6c1 Task 4) ---- */

static void check_row(const char *name, uint16_t addr, bool ramcode)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    CHECK(!r->waits && !r->isr);
    CHECK_EQ(r->ramcode, ramcode);
}

/* The nine rows: the RAM-code routines hooked with machine_hook_ramcode, bank_load_ramcode in the main ROM; A, X and
   Y declared (the thunk comments name what the callers read). */
static void test_rows(void)
{
    check_row("bank_load_ramcode", ROM_BANK_LOAD_RAMCODE, false);
    check_row("bank_enter", BANK_ENTER, true);
    check_row("bank_scan", BANK_SCAN, true);
    check_row("bank_select", BANK_SELECT, true);
    check_row("bank_probe", BANK_PROBE, true);
    check_row("bank_read_id", BANK_READ_ID, true);
    check_row("bank_put_id_char", BANK_PUT_ID_CHAR, true);
    check_row("bank_safe_char", BANK_SAFE_CHAR, true);
    check_row("bank_find_current", BANK_FIND_CURRENT, true);
}

/* RAM after setup, then the RAM copy as bank_load_ramcode makes it and the version copies boot_init_settings makes
   ($ADF1-$ADFA: bank_ver_major = $FFE0, bank_ver_minor = $FFE1), P2 = the main bank, nothing logged. */
static void setup_banks(void)
{
    setup();
    for (uint16_t i = 0; i <= BANK_RAMCODE_LAST - BANK_RAMCODE; i++)
        g_ram[BANK_RAMCODE + i] = fw_rom8(&g_f, (uint16_t)(BANK_RAMCODE_IMAGE + i));
    g_ram[BANK_VER_MAJOR] = fw_rom8(&g_f, BANK_HDR_VERSION);
    g_ram[BANK_VER_MINOR] = fw_rom8(&g_f, BANK_HDR_VERSION + 1);
    g_sfr[SFR_P2] = P2_MAIN_BANK;
}

/* bank_load_ramcode $A600: Y, X and P2 (one read) saved in $78, $76, $75; $ADFE-$AFFC copied to $0200-$03FE and
   nothing beyond; A and Y kept, X = 0. */
static void test_load_ramcode(void)
{
    static const event want[] = { { EV_READ, SFR_P2, 0xF1, 0, 0 } };
    setup();
    g_sfr[SFR_P2] = 0xF1;                         /* the window on, the main bank, P2.0 set */
    g_ram[BANK_RAMCODE - 1] = 0xA5;
    g_ram[BANK_RAMCODE_LAST + 1] = 0xA5;
    fw_regs r = { 0x05, 0x12, 0x34, 0 };
    bank_load_ramcode(&g_f, &r);
    check_log(want, 1);
    CHECK_EQ(g_ram[FW_TMP_78], 0x34);
    CHECK_EQ(g_ram[FW_TMP_76], 0x12);
    CHECK_EQ(g_ram[FW_TMP_75], 0xF1);
    unsigned differ = 0;
    for (uint16_t i = 0; i <= BANK_RAMCODE_LAST - BANK_RAMCODE; i++)
        differ += g_ram[BANK_RAMCODE + i] != fw_rom8(&g_f, (uint16_t)(BANK_RAMCODE_IMAGE + i));
    CHECK_EQ(differ, 0);
    CHECK_EQ(g_ram[BANK_P2_TABLE + 5], P2_BANK_5);
    CHECK_EQ(g_ram[BANK_ENTER], 0x48);            /* bank_enter's PHA */
    CHECK_EQ(g_ram[BANK_RAMCODE - 1], 0xA5);
    CHECK_EQ(g_ram[BANK_RAMCODE_LAST + 1], 0xA5);
    CHECK_EQ(r.a, 0x05);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x34);
    CHECK(!g_f.fault);
}

/* bank_select $025E: X = the table entry ($0200,X; with boot_dip bit 1 $0208,X); $77 = P2 & $83; P2 = X | $77 (a read,
   then a write); f->bank follows. A and Y kept. A bank number of 8-15 indexes $0208-$020F (Review Focus 1): 15 reads
   bank_p2_table_alt[7], the main bank. */
static void test_select(void)
{
    static const event want[] = { { EV_READ, SFR_P2, 0xF1, 0, 0 }, { EV_WRITE, SFR_P2, 0xF9, 0, 0 } };
    setup_banks();
    g_sfr[SFR_P2] = 0xF1;
    fw_regs r = { 5, 0x11, 0x22, 0 };
    bank_select(&g_f, &r);
    check_log(want, 2);
    CHECK_EQ(r.x, P2_BANK_5);
    CHECK_EQ(r.a, 5);
    CHECK_EQ(r.y, 0x22);
    CHECK_EQ(g_ram[TN_RESUME_POS], 0x81);
    CHECK_EQ(g_f.bank, P2_BANK_5);
    CHECK(g_f.low_window);

    g_ram[BOOT_DIP] = DIP_ALT_BANKS;              /* bank_p2_table_alt[5]: the main bank */
    r.a = 5;
    bank_select(&g_f, &r);
    CHECK_EQ(r.x, P2_MAIN_BANK);
    CHECK_EQ(g_sfr[SFR_P2], 0xF1);
    r.a = 2;
    bank_select(&g_f, &r);
    CHECK_EQ(r.x, P2_BANK_5);

    g_ram[BOOT_DIP] = 0;
    r.a = 15;
    bank_select(&g_f, &r);
    CHECK_EQ(r.x, P2_MAIN_BANK);
    r.a = 10;                                     /* $020A: bank_p2_table_alt[2] */
    bank_select(&g_f, &r);
    CHECK_EQ(r.x, P2_BANK_5);
    r.a = 0;
    bank_select(&g_f, &r);
    CHECK_EQ(r.x, P2_MAIN_BANK);
    CHECK(!g_f.fault);
}

/* bank_probe $0277: bank_absent bit 0 set, then cleared when $FFD0 reads $78: the main bank's image (no host call), or
   another bank through read_bank ($FF: an empty socket, bit 0 stays set). Its other bits kept. */
static void test_probe(void)
{
    static const event want[] = { { EV_BANK, BANK_ENTRY_COLD, 0xFF, 0, 0 }, { EV_BANK, BANK_ENTRY_COLD, 0x78, 0, 0 } };
    setup_banks();
    g_ram[BANK_ABSENT] = 0xF1;
    fw_regs r = { 1, 2, 3, 0 };
    bank_probe(&g_f, &r);
    CHECK_EQ(g_ram[BANK_ABSENT], 0xF0);
    CHECK_EQ(g_nlog, 0);
    g_f.bank = P2_BANK_7;
    bank_probe(&g_f, &r);
    CHECK_EQ(g_ram[BANK_ABSENT], 0xF1);
    g_other[BANK_ENTRY_COLD] = BANK_SIGNATURE;
    bank_probe(&g_f, &r);
    CHECK_EQ(g_ram[BANK_ABSENT], 0xF0);
    check_log(want, 2);
    CHECK_EQ(r.a, 1);
    CHECK_EQ(r.x, 2);
    CHECK_EQ(r.y, 3);
}

/* bank_safe_char $02E9 and bank_put_id_char $02E1: below $20 becomes '*', the rest is kept; the byte stored at
   bank_id_table + X, X + 1 with an 8-bit wrap. */
static void test_put_id_char(void)
{
    setup();
    fw_regs r = { 0x1F, 0, 0x77, 0 };
    bank_safe_char(&g_f, &r);
    CHECK_EQ(r.a, '*');
    r.a = 0x20;
    bank_safe_char(&g_f, &r);
    CHECK_EQ(r.a, 0x20);
    r.a = 0xFF;
    bank_safe_char(&g_f, &r);
    CHECK_EQ(r.a, 0xFF);
    r.a = 0x00;
    r.x = 0xFF;
    bank_put_id_char(&g_f, &r);
    CHECK_EQ(g_ram[BANK_ID_TABLE + 0xFF], '*');
    CHECK_EQ(r.a, '*');
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x77);
    CHECK_EQ(g_nlog, 0);
}

/* bank_read_id $0285: bank 1 present (the main bank's image): its ID "000440" "0201" "0404" at bank_id_table + 0,
   X = 14, Y = 6; bank 7 absent: 14 spaces at + 84, X = 98, Y = 14; another bank present through read_bank, its
   control codes as '*'; bank 0 (the add loop runs 255 times: 14 x 255 = 242 in 8 bits) absent: spaces at + 242-255,
   X = 0. A kept. */
static void test_read_id(void)
{
    setup_banks();
    fw_regs r = { 1, 0x55, 0x66, 0 };
    bank_read_id(&g_f, &r);
    CHECK(memcmp(g_ram + BANK_ID_TABLE, "00044002010404", 14) == 0);
    CHECK_EQ(r.a, 1);
    CHECK_EQ(r.x, 14);
    CHECK_EQ(r.y, 6);
    CHECK_EQ(g_nlog, 0);

    g_ram[BANK_ABSENT] = 1;
    r.a = 7;
    bank_read_id(&g_f, &r);
    CHECK(memcmp(g_ram + BANK_ID_TABLE + 84, "              ", 14) == 0);
    CHECK_EQ(r.a, 7);
    CHECK_EQ(r.x, 98);
    CHECK_EQ(r.y, 14);

    g_ram[BANK_ABSENT] = 0;
    g_f.bank = P2_BANK_3;
    memcpy(g_other + BANK_HDR_LANG_CODE, "AB\x01" "CDE", 6);
    memcpy(g_other + BANK_HDR_ENGINE_VER_TEXT, "1234", 4);
    memcpy(g_other + BANK_HDR_LANG_VER_TEXT, "\x1F" "678", 4);
    r.a = 3;
    bank_read_id(&g_f, &r);
    CHECK(memcmp(g_ram + BANK_ID_TABLE + 28, "AB*CDE1234*678", 14) == 0);
    CHECK_EQ(g_nlog, 14);                          /* every byte through read_bank */
    CHECK_EQ(g_log[6].addr, BANK_HDR_ENGINE_VER_TEXT);
    CHECK_EQ(g_log[13].addr, BANK_HDR_LANG_VER_TEXT + 3);

    g_ram[BANK_ABSENT] = 1;
    r.a = 0;
    bank_read_id(&g_f, &r);
    CHECK_EQ(g_ram[BANK_ID_TABLE + 242], ' ');
    CHECK_EQ(g_ram[BANK_ID_TABLE + 255], ' ');
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 14);
    CHECK(!g_f.fault);
}

/* bank_find_current $02F0: one read of P2; bank_current = the first bank 1-7 whose entry equals P2 & $7C (bank 1 for
   the main bank, though entry 0 holds it too); bank_p2_table_alt with boot_dip bit 1; unchanged when no entry
   matches. A, X and Y kept. */
static void test_find_current(void)
{
    static const event want[] = { { EV_READ, SFR_P2, 0x7B, 0, 0 } };
    setup_banks();
    g_sfr[SFR_P2] = 0x7B;                         /* bank bits $78, P2.0 and P2.1 set */
    fw_regs r = { 1, 2, 3, 0 };
    bank_find_current(&g_f, &r);
    check_log(want, 1);
    CHECK_EQ(g_ram[BANK_CURRENT], 5);
    g_ram[BOOT_DIP] = DIP_ALT_BANKS;
    bank_find_current(&g_f, &r);
    CHECK_EQ(g_ram[BANK_CURRENT], 2);
    g_sfr[SFR_P2] = P2_MAIN_BANK;
    bank_find_current(&g_f, &r);
    CHECK_EQ(g_ram[BANK_CURRENT], 1);
    g_sfr[SFR_P2] = 0x0C;                         /* in neither table */
    g_ram[BANK_CURRENT] = 9;
    bank_find_current(&g_f, &r);
    CHECK_EQ(g_ram[BANK_CURRENT], 9);
    g_ram[BOOT_DIP] = 0;
    bank_find_current(&g_f, &r);
    CHECK_EQ(g_ram[BANK_CURRENT], 9);
    CHECK_EQ(r.a, 1);
    CHECK_EQ(r.x, 2);
    CHECK_EQ(r.y, 3);
    CHECK(!g_f.fault);
}

/* bank_scan $0241 with $75/$76/$78 as bank_load_ramcode leaves them: banks 7 down to 2 selected (a P2 read and write
   each) and probed through read_bank (empty sockets: 14 spaces each), bank 1 the main bank (its ID from the image);
   then P2 = $75 written back, X = $76, Y = $78, A kept. */
static void test_scan(void)
{
    setup_banks();
    g_ram[FW_TMP_75] = P2_MAIN_BANK;
    g_ram[FW_TMP_76] = 0x11;
    g_ram[FW_TMP_78] = 0x22;
    fw_regs r = { 0x33, 0, 0, 0 };
    bank_scan(&g_f, &r);
    CHECK_EQ(g_nlog, 6 * 3 + 2 + 1);
    CHECK_EQ(g_log[0].kind, EV_READ);             /* bank 7: select, probe */
    CHECK_EQ(g_log[1].kind, EV_WRITE);
    CHECK_EQ(g_log[1].a, P2_BANK_7);
    CHECK_EQ(g_log[2].kind, EV_BANK);
    CHECK_EQ(g_log[2].addr, BANK_ENTRY_COLD);
    CHECK_EQ(g_log[16].a, P2_BANK_2);             /* bank 2's P2 write */
    CHECK_EQ(g_log[19].a, P2_MAIN_BANK);          /* bank 1's */
    CHECK_EQ(g_log[20].kind, EV_WRITE);           /* bank_restore */
    CHECK_EQ(g_log[20].a, P2_MAIN_BANK);
    CHECK(memcmp(g_ram + BANK_ID_TABLE, "00044002010404", 14) == 0);
    for (unsigned i = 14; i < 98; i++)
        CHECK_EQ(g_ram[BANK_ID_TABLE + i], ' ');
    CHECK_EQ(g_ram[BANK_ABSENT] & 1, 0);          /* bank 1, probed last */
    CHECK_EQ(r.a, 0x33);
    CHECK_EQ(r.x, 0x11);
    CHECK_EQ(r.y, 0x22);
    CHECK_EQ(g_f.bank, P2_MAIN_BANK);
    CHECK(!g_f.fault);
}

/* bank_enter $0210, an absent bank (5): select, probe, then bank_restore: P2 = $75, A kept, X = $76, Y = $78; no
   entry. */
static void test_enter_absent(void)
{
    static const event want[] = {
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 }, { EV_WRITE, SFR_P2, P2_BANK_5, 0, 0 },
        { EV_BANK, BANK_ENTRY_COLD, 0xFF, 0, 0 }, { EV_WRITE, SFR_P2, P2_MAIN_BANK, 0, 0 },
    };
    setup_banks();
    g_ram[FW_TMP_75] = P2_MAIN_BANK;
    g_ram[FW_TMP_76] = 0x44;
    g_ram[FW_TMP_78] = 0x55;
    fw_regs r = { 5, 0, 0, 0 };
    bank_enter(&g_f, &r);
    check_log(want, 4);
    CHECK_EQ(r.a, 5);
    CHECK_EQ(r.x, 0x44);
    CHECK_EQ(r.y, 0x55);
    CHECK(!g_f.unwound);
    CHECK(!g_f.fault);
}

/* bank_enter, the main bank present: warm ($FFD4, A = $FFE0, X = $FF) when $FFE1 and $FFE0 equal bank_ver_minor and
   bank_ver_major, zero page untouched; else cold ($FFD0, A = 0, X = 0) with $01-$64 cleared ($00 and $65 kept): the
   minor differing (the major not read), or the major alone. Bank 0 is the main bank too (Review Focus 1). Another
   present bank's header is read through read_bank, $FFE1 then $FFE0. */
static void test_enter_present(void)
{
    static const event warm[] = {
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 }, { EV_WRITE, SFR_P2, P2_MAIN_BANK, 0, 0 },
        { EV_ENTER, BANK_ENTRY_WARM, 2, 0xFF, 0 },
    };
    static const event cold[] = {
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 }, { EV_WRITE, SFR_P2, P2_MAIN_BANK, 0, 0 },
        { EV_ENTER, BANK_ENTRY_COLD, 0, 0, 0 },
    };
    static const event other[] = {
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 }, { EV_WRITE, SFR_P2, P2_BANK_6, 0, 0 },
        { EV_BANK, BANK_ENTRY_COLD, BANK_SIGNATURE, 0, 0 }, { EV_BANK, BANK_HDR_VERSION + 1, 1, 0, 0 },
        { EV_BANK, BANK_HDR_VERSION, 2, 0, 0 }, { EV_ENTER, BANK_ENTRY_WARM, 2, 0xFF, 0 },
    };
    for (uint8_t bank = 0; bank <= 1; bank++) {
        setup_banks();
        memset(g_ram, 0x5A, 0x66);
        g_ram[BOOT_DIP] = 0;
        fw_regs r = { bank, 0, 0, 0 };
        bank_enter(&g_f, &r);
        check_log(warm, 3);
        CHECK(g_f.unwound);
        CHECK_EQ(g_ram[0x01], 0x5A);
        CHECK_EQ(g_ram[0x64], 0x5A);
    }

    setup_banks();
    memset(g_ram, 0x5A, 0x66);
    g_ram[BOOT_DIP] = 0;
    g_ram[BANK_VER_MINOR]++;
    fw_regs r = { 1, 0, 0, 0 };
    bank_enter(&g_f, &r);
    check_log(cold, 3);
    CHECK(g_f.unwound);
    CHECK_EQ(g_ram[0x00], 0x5A);
    CHECK_EQ(g_ram[0x01], 0);
    CHECK_EQ(g_ram[0x64], 0);
    CHECK_EQ(g_ram[0x65], 0x5A);

    setup_banks();
    g_ram[BANK_VER_MAJOR]++;
    bank_enter(&g_f, &r);
    check_log(cold, 3);

    setup_banks();
    g_other[BANK_ENTRY_COLD] = BANK_SIGNATURE;
    g_other[BANK_HDR_VERSION] = 2;
    g_other[BANK_HDR_VERSION + 1] = 1;
    r.a = 6;
    bank_enter(&g_f, &r);
    check_log(other, 6);
    CHECK(g_f.unwound);
    CHECK(!g_f.fault);
}

/* ---- the dispatcher (M4.6c1 Task 5) ---- */

/* Queues command n at position pos with handler h and its argument at boot_clause[arg] = c; cmd_count = n + 1,
   cmd_next_pos = cmd_positions[0]. */
static void queue(uint8_t n, uint8_t pos, uint16_t h, uint8_t arg, char c)
{
    g_ram[CMD_POSITIONS + n] = pos;
    g_ram[CMD_HANDLERS + 2 * n] = (uint8_t)h;
    g_ram[CMD_HANDLERS + 2 * n + 1] = (uint8_t)(h >> 8);
    g_ram[CMD_ARG_INDEX + n] = arg;
    g_ram[BOOT_CLAUSE + arg] = (uint8_t)c;
    g_ram[CMD_COUNT] = (uint8_t)(n + 1);
    g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS];
}

/* The cmd_run_next row: A and X declared (cmd_run_done), a main-ROM hook. */
static void test_run_next_row(void)
{
    const fw_replacement *r = fw_replacement_find("cmd_run_next");
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(r->outputs, FW_OUT_A | FW_OUT_X);
    CHECK(!r->waits && !r->isr && !r->ramcode);
}

/* cmd_run_next with an address no C handler has (the scan's NUL bug, $D300 after "@A" and a zero; M4.6c1 spec 2.5 and
   4.3): RAM untouched (cmd_vector, cmd_next and cmd_next_pos are the ROM's to write), one unhooked ROM call of $A893
   with X = the position, Y = cmd_next and A = X; a failed call is a fault; an unwound one stops cmd_run_due. */
static void test_run_next_unknown(void)
{
    static const event want[] = { { EV_CALL, ROM_CMD_RUN_NEXT, 4, 4, 1 } };
    setup();
    queue(1, 4, 0xD300, 3, 'x');
    g_ram[CMD_NEXT] = 1;
    static uint8_t before[0x10000];
    memcpy(before, g_ram, sizeof before);
    CHECK(cmd_run_next(&g_f, 4));
    check_log(want, 1);
    CHECK(memcmp(before, g_ram, sizeof before) == 0);
    CHECK(!g_f.fault);

    setup();
    queue(0, 0, 0x002A, 3, 'x');
    g_call_result = FW_CALL_FAILED;
    CHECK(!cmd_run_next(&g_f, 0));
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, ROM_CMD_RUN_NEXT);

    setup();
    queue(0, 0, 0xD300, 3, 'x');
    queue(1, 0, 0xD300, 3, 'x');
    g_call_result = FW_CALL_UNWOUND;
    cmd_run_due(&g_f, 0);
    CHECK_EQ(g_nlog, 1);                          /* the second is not run */
    CHECK(g_f.unwound);
    CHECK(!g_f.fault);
}

/* bank_cmd_select through cmd_run_next (the C dispatch of $ABAF): "@=5" (an empty socket) loads the RAM copy, selects
   and probes bank 5, restores P2 and finds the current bank (P2 read once more), and the next queued command runs;
   "@=1" with the versions equal enters the main bank warm ($FFD4, A = 2, X = $FF) and the next command does not
   run. bank_load_ramcode saved Y = cmd_next before the INC (cmd_handler_y) and X = the argument index. */
static void test_cmd_select(void)
{
    static const event absent[] = {
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 },                                    /* bank_load_ramcode */
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 }, { EV_WRITE, SFR_P2, P2_BANK_5, 0, 0 },   /* bank_select */
        { EV_BANK, BANK_ENTRY_COLD, 0xFF, 0, 0 },                                   /* bank_probe */
        { EV_WRITE, SFR_P2, P2_MAIN_BANK, 0, 0 },                                   /* bank_restore */
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 },                                    /* bank_find_current */
    };
    static const event warm[] = {
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 },
        { EV_READ, SFR_P2, P2_MAIN_BANK, 0, 0 }, { EV_WRITE, SFR_P2, P2_MAIN_BANK, 0, 0 },
        { EV_ENTER, BANK_ENTRY_WARM, 2, 0xFF, 0 },
    };
    setup_banks();
    memset(g_ram + BANK_RAMCODE, 0, BANK_RAMCODE_LAST - BANK_RAMCODE + 1);   /* speech overwrote the copy */
    g_ram[BANK_CURRENT] = 7;
    queue(0, 2, ROM_BANK_CMD_SELECT, 6, '5');
    queue(1, 2, ROM_CMD_SET_SPELL, 9, '1');
    cmd_run_due(&g_f, 2);
    check_log(absent, 6);
    CHECK_EQ(g_ram[FW_TMP_78], 0);                /* Y = cmd_next before the INC */
    CHECK_EQ(g_ram[FW_TMP_76], 6);                /* X = the argument index */
    CHECK_EQ(g_ram[BANK_P2_TABLE + 5], P2_BANK_5);   /* reloaded before bank_select read it (Review Focus 4) */
    CHECK_EQ(g_ram[BANK_CURRENT], 1);
    CHECK_EQ(g_ram[CMD_SPELL], 1);                /* the next command ran */
    CHECK_EQ(g_ram[CMD_NEXT], 2);
    CHECK(!g_f.unwound);
    CHECK(!g_f.fault);

    setup_banks();
    queue(0, 2, ROM_BANK_CMD_SELECT, 6, '1');
    queue(1, 2, ROM_CMD_SET_SPELL, 9, '1');
    cmd_run_due(&g_f, 2);
    check_log(warm, 4);
    CHECK(g_f.unwound);
    CHECK_EQ(g_ram[CMD_SPELL], 0);                /* abandoned with the C */
    CHECK_EQ(g_ram[CMD_NEXT], 1);
    CHECK(!g_f.fault);
}

void suite_fw_bank(void)
{
    CHECK(fw_image_get() != NULL);
    test_bank_tracking();
    test_rom8_bank();
    test_enter();
    test_rom_call_unhooked();
    test_rows();
    test_load_ramcode();
    test_select();
    test_probe();
    test_put_id_char();
    test_read_id();
    test_find_current();
    test_scan();
    test_enter_absent();
    test_enter_present();
    test_run_next_row();
    test_run_next_unknown();
    test_cmd_select();
}
