/* The firmware context (M4.0 spec section 3.1): RAM access, the converted-data reads and their fault, the
   lower-ROM window and the host calls. Uses a hand-made image, not the converted tables. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/fw_ops.h"

static fw_image g_img;
static uint8_t g_ram[FW_RAM_SIZE];
static int g_window_calls;
static bool g_window;

static void mark(uint8_t *ok, uint32_t i) { ok[i >> 3] |= (uint8_t)(1u << (i & 7)); }

static void host_window(void *ctx, bool on)
{
    (void)ctx;
    g_window_calls++;
    g_window = on;
}

static uint64_t g_budget;

static fw_call_result g_call_result = FW_CALL_RETURNED;

static fw_call_result host_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    r->a = 0x42;
    g_budget = budget;
    return g_call_result;
}

static void *g_sfr_ctx;
static uint8_t g_sfr_addr;
static int g_sfr_calls;

static uint8_t host_sfr(void *ctx, uint8_t addr)
{
    g_sfr_ctx = ctx;
    g_sfr_addr = addr;
    g_sfr_calls++;
    return (uint8_t)(addr ^ 0x5A);
}

/* fw_sfr8 (M4.4b spec 2.4): an SFR byte through the host's read_sfr; no host, no read_sfr or an address below
   $DA is a fault, reads $FF and does not reach the host. */
static void test_sfr8(void)
{
    fw f;
    int ctx;
    fw_init(&f, g_ram, &g_img, NULL);
    CHECK_EQ(fw_sfr8(&f, 0xE7), 0xFF);                     /* no host */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E7);

    fw_host no_sfr = { NULL, host_call, host_window };   /* a host without read_sfr */
    fw_init(&f, g_ram, &g_img, &no_sfr);
    CHECK_EQ(fw_sfr8(&f, 0xE7), 0xFF);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E7);

    fw_host sfr = { &ctx, host_call, host_window, host_sfr };
    g_sfr_calls = 0;
    fw_init(&f, g_ram, &g_img, &sfr);
    CHECK_EQ(fw_sfr8(&f, 0xD9), 0xFF);                     /* RAM, not an SFR */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00D9);
    CHECK_EQ(g_sfr_calls, 0);

    fw_init(&f, g_ram, &g_img, &sfr);
    CHECK_EQ(fw_sfr8(&f, 0xE7), 0xE7 ^ 0x5A);              /* UART_STATUS: the host's answer */
    CHECK_EQ(fw_sfr8(&f, 0xDA), 0xDA ^ 0x5A);              /* the first SFR */
    CHECK_EQ(fw_sfr8(&f, 0xFF), 0xFF ^ 0x5A);              /* the last */
    CHECK_EQ(g_sfr_calls, 3);
    CHECK_EQ(g_sfr_addr, 0xFF);
    CHECK(g_sfr_ctx == &ctx);
    CHECK(!f.fault);
}

/* fw_wr_ind_y (M4.4b spec 2.6, STA (zp),Y): v at the zero-page pointer plus y, the carry reaching the high byte;
   a target of $4000 or above, or an SFR, is a fault and writes nothing. */
static void test_wr_ind_y(void)
{
    static uint8_t before[FW_RAM_SIZE];
    fw f;
    memset(g_ram, 0, sizeof g_ram);
    fw_init(&f, g_ram, &g_img, NULL);
    fw_wr16(&f, ISR_FILL_PTR, 0x13FE);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 5, 0xA5);      /* $13FE + 5 = $1403 */
    CHECK_EQ(g_ram[0x1403], 0xA5);
    CHECK_EQ(g_ram[0x1303], 0x00);                         /* not $1303: the carry went into the high byte */
    CHECK_EQ(fw_ind_y(&f, (uint8_t)ISR_FILL_PTR, 5), 0xA5);
    CHECK(!f.fault);

    fw_wr16(&f, ISR_FILL_PTR, 0x3FFE);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 1, 0x11);      /* $3FFF: the last RAM byte */
    CHECK_EQ(g_ram[0x3FFF], 0x11);
    CHECK(!f.fault);
    memcpy(before, g_ram, sizeof before);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 2, 0x22);      /* $4000: ROM */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x4000);
    CHECK(memcmp(g_ram, before, sizeof before) == 0);

    fw_init(&f, g_ram, &g_img, NULL);
    fw_wr16(&f, ISR_FILL_PTR, 0x00E0);
    memcpy(before, g_ram, sizeof before);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 7, 0x33);      /* $E7: an SFR */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E7);
    CHECK(memcmp(g_ram, before, sizeof before) == 0);
}

/* util_shl3 $C657 (ASL A x 3) and util_shl4 $C656 (ASL A, then util_shl3): 8-bit, the high bits shifted out. */
static void test_shl(void)
{
    CHECK_EQ(util_shl3(0x01), 0x08);
    CHECK_EQ(util_shl3(0x10), 0x80);
    CHECK_EQ(util_shl3(0x21), 0x08);   /* $108: bit 8 lost */
    CHECK_EQ(util_shl4(0x01), 0x10);
    CHECK_EQ(util_shl4(0x0F), 0xF0);
    CHECK_EQ(util_shl4(0x11), 0x10);   /* $110: bit 8 lost */
    CHECK_EQ(util_shl4(0x80), 0x00);   /* the first ASL already loses it */
}

/* fw_fault: a fault at a ROM address where the ROM would never return (Tasks 2 and 3). Without a trap it returns
   with the fault set; the first fault's address is kept. */
static void test_fault(void)
{
    fw f;
    fw_init(&f, g_ram, &g_img, NULL);
    CHECK(!f.fault);
    fw_fault(&f, 0xC1A1);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC1A1);
    fw_fault(&f, 0xBBA7);
    CHECK_EQ(f.fault_addr, 0xC1A1);
}

void suite_fw(void)
{
    fw f;
    memset(&g_img, 0, sizeof g_img);
    memset(g_ram, 0, sizeof g_ram);
    g_img.rom[0x9AB4] = 0xC0;
    mark(g_img.rom_ok, 0x9AB4);
    g_img.rom[0x4102] = 0x11;
    mark(g_img.rom_ok, 0x4102);
    g_img.low[0x0102] = 0x22;   /* $4102 with the window on */
    mark(g_img.low_ok, 0x0102);

    fw_init(&f, g_ram, &g_img, NULL);
    CHECK_EQ(fw_rom8(&f, 0x9AB4), 0xC0);
    CHECK(!f.fault);
    CHECK_EQ(fw_rom8(&f, 0x4102), 0x11);
    fw_set_low_window(&f, true);          /* no host: only the flag */
    CHECK_EQ(fw_rom8(&f, 0x4102), 0x22);
    CHECK_EQ(fw_rom8(&f, 0x9AB4), 0xC0);  /* the window covers only $4000-$7FFF */
    CHECK(!f.fault);
    CHECK_EQ(fw_rom8(&f, 0x9000), 0xFF);  /* not converted */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x9000);

    fw_init(&f, g_ram, &g_img, NULL);
    fw_wr16(&f, TN_RULE_PTR, 0x4100);
    CHECK_EQ(fw_rd8(&f, TN_RULE_PTR), 0x00);
    CHECK_EQ(fw_rd8(&f, TN_RULE_PTR + 1), 0x41);
    CHECK_EQ(fw_rd16(&f, TN_RULE_PTR), 0x4100);
    CHECK_EQ(fw_ind_y(&f, (uint8_t)TN_RULE_PTR, 2), 0x11);   /* ($20),Y */
    fw_wr8(&f, CMD_TEXT + 3, 'A');
    CHECK_EQ(g_ram[0x1A03], 'A');
    CHECK_EQ(fw_mem8(&f, 0x1A03), 'A');
    CHECK(!f.fault);
    fw_mem8(&f, 0x00F0);                  /* an SFR is not data */
    CHECK(f.fault);

    fw_host host = { NULL, NULL, host_window };
    fw_init(&f, g_ram, &g_img, &host);
    fw_set_low_window(&f, true);
    CHECK_EQ(g_window_calls, 1);
    CHECK(g_window);
    CHECK(f.low_window);
    fw_regs r = { 0, 0, 0, 0 };
    CHECK(!fw_rom_call(&f, 0xCA92, &r, FW_ROM_CALL_BUDGET));  /* the host has no rom_call */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xCA92);

    fw_host caller = { NULL, host_call, host_window };   /* the budget reaches the host unchanged */
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(fw_rom_call(&f, 0xCA92, &r, FW_ROM_CALL_SPEECH_BUDGET));
    CHECK_EQ(g_budget, FW_ROM_CALL_SPEECH_BUDGET);
    CHECK_EQ(r.a, 0x42);
    CHECK(fw_rom_call(&f, 0xC5D6, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(g_budget, FW_ROM_CALL_BUDGET);
    CHECK(!f.fault);
    g_call_result = FW_CALL_UNWOUND;   /* the ROM reset the stack: abandoned, not a fault (M4.2 spec 3) */
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(!f.unwound);
    CHECK(!fw_rom_call(&f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(f.unwound);
    CHECK(!f.fault);
    g_call_result = FW_CALL_FAILED;
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(!fw_rom_call(&f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(f.fault);
    CHECK(!f.unwound);
    g_call_result = FW_CALL_RETURNED;

    CHECK_EQ(TN_POS, 0x0051);
    CHECK_EQ(CMD_TEXT, 0x1A00);
    CHECK_EQ(LTS_MATCH_POS, 0x004F);

    test_sfr8();
    test_wr_ind_y();
    test_shl();
    test_fault();
}
