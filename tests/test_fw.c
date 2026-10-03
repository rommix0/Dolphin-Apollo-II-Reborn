/* The firmware context (M4.0 spec section 3.1): RAM access, the converted-data reads and their fault, the
   lower-ROM window and the host calls. Uses a hand-made image, not the converted tables. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"

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

static bool host_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    r->a = 0x42;
    g_budget = budget;
    return true;
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
    CHECK(!fw_rom_call(&f, 0xB154, &r, FW_ROM_CALL_BUDGET));  /* the host has no rom_call */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB154);

    fw_host caller = { NULL, host_call, host_window };   /* the budget reaches the host unchanged */
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(fw_rom_call(&f, 0xCA92, &r, FW_ROM_CALL_SPEECH_BUDGET));
    CHECK_EQ(g_budget, FW_ROM_CALL_SPEECH_BUDGET);
    CHECK_EQ(r.a, 0x42);
    CHECK(fw_rom_call(&f, 0xC5D6, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(g_budget, FW_ROM_CALL_BUDGET);
    CHECK(!f.fault);

    CHECK_EQ(TN_POS, 0x0051);
    CHECK_EQ(CMD_TEXT, 0x1A00);
    CHECK_EQ(LTS_MATCH_POS, 0x004F);
}
