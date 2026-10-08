/* Unit checks of the frame builder (M4.5 spec 4.6) on the converted data, with a mock host that fails every ROM
   call: the frame builder makes none. Each expected value is worked out from the dump of the routine
   (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests (diff_lts,
   diff_play) and the hybrid goldens (golden_hybrid), which compare the thunks' register outputs against the ROM. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static unsigned g_ncalls;

/* Counts and fails every ROM call. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    g_ncalls++;
    return FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls made. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* the frame builder does not drive P2 */
    g_host.read_sfr = NULL;         /* nor read an SFR */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The frame builder's tables as the C reads them, through fw_rom8 at their ROM addresses (M4.5 spec 3): the
   template by byte name, both code tables, and the two formula tables at their edges. */
static void test_data(void)
{
    setup();
    static const uint8_t k_template[FRAME_SIZE] = {
        1, 32, 1, 32, 1, 32, 1, 32, 1, 63,   /* ALF FN A1 F1 A2 F2 A3 F3 AHF V */
        32, 32, 0x17, 0x20, 0x19, 0x24,      /* pitch code, b11, b12, b13, b14, b15 */
        0x82, 0x4A, 0xFF, 0xFF,              /* b16 source, b17 filter, the end marker */
    };
    for (unsigned b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(FRM_TEMPLATE + b)), k_template[b]);
    CHECK_EQ(fw_rom8(&g_f, FRM_SOURCE_CODES), 0x82);       /* default */
    CHECK_EQ(fw_rom8(&g_f, FRM_SOURCE_CODES + 3), 0xBA);   /* reduced HF */
    CHECK_EQ(fw_rom8(&g_f, FRM_FILTER_CODES), 0x4A);       /* male */
    CHECK_EQ(fw_rom8(&g_f, FRM_FILTER_CODES + 1), 0x54);   /* female */
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT), FRM_PRODUCT_LIMIT_0);
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 1), 255);   /* e = 1: 255, negative as a signed byte */
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 2), 127);
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 30), 8);    /* the largest e the frame can give (F1 63 - 33) */
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 255), 1);
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_LO + 1), 82);        /* 82 * 1 */
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_HI + 1), 0);
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_LO + 4), 0x48);      /* 82 * 4 = 328 = $0148 */
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_HI + 4), 0x01);
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_LO + 255), 0xAE);    /* 82 * 255 = 20910 = $51AE */
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_HI + 255), 0x51);
    CHECK(!g_f.fault);
    CHECK_EQ(g_ncalls, 0);
}

void suite_fw_frm(void)
{
    CHECK(fw_image_get() != NULL);
    test_data();
}
