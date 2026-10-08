/* Unit checks of the frame builder (M4.5 spec 4.6) on the converted data, with a mock host that fails every ROM
   call: the frame builder makes none. Each expected value is worked out from the dump of the routine
   (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests (diff_lts,
   diff_play) and the hybrid goldens (golden_hybrid), which compare the thunks' register outputs against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/frm_build.h"
#include "firmware/par_addr.h"
#include "firmware/data/fw_data.h"

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static uint8_t g_before[FW_RAM_SIZE];
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

/* The hook of name sits at addr, declares A, X and Y and does not wait (pinned here; diff_lts compares the outputs
   against the ROM). */
static void check_row(const char *name, uint16_t addr)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    CHECK(!r->waits);
}

/* The RAM as it is now, for check_written. */
static void snapshot(void) { memcpy(g_before, g_ram, sizeof g_before); }

/* Every RAM byte except the n listed is as snapshot() saw it (the listed ones are checked by the caller). */
static void check_written(const uint16_t *addrs, size_t n)
{
    for (uint16_t a = 0; a < FW_RAM_SIZE; a++) {
        bool listed = false;
        for (size_t i = 0; i < n; i++)
            listed |= addrs[i] == a;
        if (!listed && g_ram[a] != g_before[a]) {
            fprintf(stderr, "  RAM $%04X written: %02X -> %02X\n", a, g_before[a], g_ram[a]);
            CHECK(g_ram[a] == g_before[a]);
        }
    }
}

/* No ROM call was made and nothing faulted. */
static void check_no_calls(void)
{
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The producer frame (isr_fill_ptr) is isr_frame_b. */
static void fill_frame_b(void) { fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B); }

/* ---- the leaves (M4.5 Task 2) ---- */

static void test_leaf_rows(void)
{
    check_row("frm_build_rest_frame", ROM_FRM_BUILD_REST_FRAME);
    check_row("frm_copy_rest_frame", ROM_FRM_COPY_REST_FRAME);
    check_row("frm_snapshot", ROM_FRM_SNAPSHOT);
    check_row("frm_clamp_mark_space", ROM_FRM_CLAMP_MARK_SPACE);
    check_row("frm_raise_formants", ROM_FRM_RAISE_FORMANTS);
}

/* frm_build_rest_frame after setup() and the caller's set-up: frm_rest_frame ($EE before) = frm_template with FN,
   F1, F2, F3 = want[0..3]; par_row_ptr = par_params_ptr; A = its high byte, X = 9 ($B436 CPX #8 after X = 1, 3, 5,
   7), Y = $3B; only frm_rest_frame and par_row_ptr written, no call. The entry registers are not read. */
static void check_build(uint16_t params, const uint8_t want[4])
{
    static const uint16_t w[] = {
        FRM_REST_FRAME + 0, FRM_REST_FRAME + 1, FRM_REST_FRAME + 2, FRM_REST_FRAME + 3, FRM_REST_FRAME + 4,
        FRM_REST_FRAME + 5, FRM_REST_FRAME + 6, FRM_REST_FRAME + 7, FRM_REST_FRAME + 8, FRM_REST_FRAME + 9,
        FRM_REST_FRAME + 10, FRM_REST_FRAME + 11, FRM_REST_FRAME + 12, FRM_REST_FRAME + 13, FRM_REST_FRAME + 14,
        FRM_REST_FRAME + 15, FRM_REST_FRAME + 16, FRM_REST_FRAME + 17, FRM_REST_FRAME + 18, FRM_REST_FRAME + 19,
        PAR_ROW_PTR, PAR_ROW_PTR + 1,
    };
    memset(g_ram + FRM_REST_FRAME, 0xEE, FRAME_SIZE);
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    frm_build_rest_frame(&g_f, &r);
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        uint8_t v = fw_frm_template[b];
        if (b == FRAME_FN || b == FRAME_F1 || b == FRAME_F2 || b == FRAME_F3)
            v = want[b >> 1];
        CHECK_EQ(g_ram[FRM_REST_FRAME + b], v);
    }
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), params);
    CHECK_EQ(r.a, params >> 8);
    CHECK_EQ(r.x, 9);
    CHECK_EQ(r.y, FRM_PAUSE_PHONEME);
    check_written(w, sizeof w / sizeof w[0]);
    check_no_calls();
}

/* Voice A as par_setup_voice leaves it (par_row_ptr = par_params_ptr = the table + 2N, P1's target row; stride
   5N = 430): the pause phoneme's FN 32, F1 32, F2 23, F3 31 from the ROM table. Then a RAM table whose par_row_ptr
   ($1000) is not par_params_ptr ($3010), stride $0100: the four rows' bytes at $103B, $113B, $123B, $133B, and
   par_row_ptr set to par_params_ptr, not back to where it started. */
static void test_build_rest_frame(void)
{
    static const uint8_t voice_a[4] = { 32, 32, 23, 31 };
    static const uint8_t ram[4] = { 0x11, 0x22, 0x33, 0x44 };
    const uint16_t params = PAR_VOICE_A + 2 * PAR_PHONEMES;
    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, params);
    fw_wr16(&g_f, PAR_PARAMS_PTR, params);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 5 * PAR_PHONEMES);
    for (unsigned k = 0; k < 4; k++)
        CHECK_EQ(fw_par_voice_a[FRM_PAUSE_PHONEME].p[k].target, voice_a[k]);
    check_build(params, voice_a);

    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, 0x1000);
    fw_wr16(&g_f, PAR_PARAMS_PTR, 0x3010);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 0x0100);
    for (unsigned k = 0; k < 4; k++)
        g_ram[0x103B + 0x100 * k] = ram[k];
    check_build(0x3010, ram);
}

/* frm_copy_rest_frame: the 20 bytes of frm_rest_frame ($40 + b) into the producer frame (isr_frame_b, $EE before),
   Y = $13 down to 0: A = frm_rest_frame[0], X untouched, Y = $FF; only the frame written. */
static void test_copy_rest_frame(void)
{
    uint16_t w[FRAME_SIZE];
    setup();
    fill_frame_b();
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        g_ram[FRM_REST_FRAME + b] = (uint8_t)(0x40 + b);
        g_ram[ISR_FRAME_B + b] = 0xEE;
        w[b] = (uint16_t)(ISR_FRAME_B + b);
    }
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_copy_rest_frame(&g_f, &r);
    for (unsigned b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0x40 + b);
    CHECK_EQ(r.a, 0x40);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xFF);
    check_written(w, FRAME_SIZE);
    check_no_calls();
}

/* frm_snapshot: frm_snap ($30 + i) moves to frm_snap_prev ($EE before); frm_snap[i] = the producer frame's byte
   frm_snapshot_map[i] (frame byte b = $50 + b; the map is 10 8 6 4 2 0 1 3 5 7). A = b10 ($5A), X = 0, Y = 10; the
   bytes around frm_snap_prev untouched. */
static void test_snapshot(void)
{
    static const uint8_t map[10] = { 10, 8, 6, 4, 2, 0, 1, 3, 5, 7 };
    uint16_t w[20];
    setup();
    fill_frame_b();
    for (unsigned b = 0; b < FRAME_SIZE; b++)
        g_ram[ISR_FRAME_B + b] = (uint8_t)(0x50 + b);
    memset(g_ram + FRM_SNAP_PREV - 1, 0xEE, 12);
    for (unsigned i = 0; i < 10; i++) {
        g_ram[FRM_SNAP + i] = (uint8_t)(0x30 + i);
        w[i] = (uint16_t)(FRM_SNAP + i);
        w[10 + i] = (uint16_t)(FRM_SNAP_PREV + i);
    }
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    frm_snapshot(&g_f, &r);
    for (unsigned i = 0; i < 10; i++) {
        CHECK_EQ(fw_frm_snapshot_map[i], map[i]);
        CHECK_EQ(g_ram[FRM_SNAP_PREV + i], 0x30 + i);
        CHECK_EQ(g_ram[FRM_SNAP + i], 0x50 + map[i]);
    }
    CHECK_EQ(g_ram[FRM_SNAP_PREV - 1], 0xEE);
    CHECK_EQ(g_ram[FRM_SNAP_PREV + 10], 0xEE);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, FRAME_PITCH);
    check_written(w, 20);
    check_no_calls();
}

/* One frm_clamp_mark_space call: b10 = c and @M = m, the producer frame's b11 $EE before. Out: b11 and FW_TMP_75
   (2c, 8 bits) as the ROM leaves them; A = $0B, X untouched, Y = 11; nothing else written. */
static void clamp_case(uint8_t c, uint8_t m, uint8_t want_b11, uint8_t want_75)
{
    static const uint16_t w[] = { ISR_FRAME_B + FRAME_MARK_SPACE, FW_TMP_75 };
    setup();
    fill_frame_b();
    g_ram[ISR_FRAME_B + FRAME_PITCH] = c;
    g_ram[ISR_FRAME_B + FRAME_MARK_SPACE] = 0xEE;
    g_ram[CMD_MARK_SPACE] = m;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_clamp_mark_space(&g_f, &r);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_MARK_SPACE], want_b11);
    CHECK_EQ(g_ram[FW_TMP_75], want_75);
    CHECK_EQ(r.a, FRM_MARK_SPACE_FLOOR);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, FRAME_MARK_SPACE);
    check_written(w, 2);
    check_no_calls();
}

static void test_clamp_mark_space(void)
{
    /* the upper cap: c = 63, 2c = 126: 160 - 126 = 34 is below @M $3F ($B943 BCS not taken): b11 = 34; 47 - 126 =
       $B1 has bit 7 set ($B94C BMI taken); 11 is below 34 */
    clamp_case(63, 0x3F, 34, 126);
    /* @M at the cap is kept: c = 32, 2c = 64: 96 is not below @M 96 (BCS taken) */
    clamp_case(32, 96, 96, 64);
    /* 47 - 2c positive and raising b11: c = 10: 47 - 20 = 27 is not below @M 5 ($B950 BCC not taken): b11 = 27 */
    clamp_case(10, 5, 27, 20);
    /* 47 - 2c positive, below @M 30 (BCC taken): b11 stays 30 */
    clamp_case(10, 30, 30, 20);
    /* 47 - 2c negative (skipped) and the floor: c = 32: 47 - 64 = $EF; @M 3 is not above 11 ($B958 BCC not taken):
       b11 = 11 */
    clamp_case(32, 3, 11, 64);
    /* above the floor (BCC taken): @M 12 stays */
    clamp_case(32, 12, 12, 64);
    /* the 8-bit wraps: c = 90, 2c = 180 ($B4): 160 - 180 = $EC is not below @M 63 (BCS taken: no cap), and
       47 - 180 = $7B has bit 7 clear and is not below 63: b11 = 123 */
    clamp_case(90, 63, 0x7B, 0xB4);
    /* the ASL loses bit 7: c = $81, 2c = 2: 158 is not below @M 63; 47 - 2 = 45 is below 63: b11 = 63 */
    clamp_case(0x81, 63, 63, 2);
}

/* One frm_raise_formants call: b10 = c, F1 = f1, F2 = f2 in the producer frame, FW_TMP_75 = $EE before. Out: F1, F2
   and FW_TMP_75 as the ROM leaves them; A and Y as given, X untouched; nothing else written. */
static void raise_case(uint8_t c, uint8_t f1, uint8_t f2, uint8_t want_f1, uint8_t want_f2, uint8_t want_75,
                       uint8_t want_a, uint8_t want_y)
{
    static const uint16_t w[] = { ISR_FRAME_B + FRAME_F1, ISR_FRAME_B + FRAME_F2, FW_TMP_75 };
    setup();
    fill_frame_b();
    g_ram[ISR_FRAME_B + FRAME_PITCH] = c;
    g_ram[ISR_FRAME_B + FRAME_F1] = f1;
    g_ram[ISR_FRAME_B + FRAME_F2] = f2;
    g_ram[FW_TMP_75] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_raise_formants(&g_f, &r);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F1], want_f1);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F2], want_f2);
    CHECK_EQ(g_ram[FW_TMP_75], want_75);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, want_y);
    check_written(w, 3);
    check_no_calls();
}

static void test_raise_formants(void)
{
    /* b10 = 31: 31 - 32 = $FF, bit 7 set: the RTS at $B966, A = $FF, Y = 10, nothing written */
    raise_case(31, 20, 40, 20, 40, 0xEE, 0xFF, FRAME_PITCH);
    /* b10 = $A0: $A0 - 32 = $80 has bit 7 set too (BPL is bit 7 of the 8-bit difference): it returns at once */
    raise_case(0xA0, 20, 40, 20, 40, 0xEE, 0x80, FRAME_PITCH);
    /* b10 = 32: d = 0: FW_TMP_75 = 0, F1 and F2 written back unchanged through par_clamp; A = F2, Y = 5 */
    raise_case(32, 20, 40, 20, 40, 0, 40, FRAME_F2);
    /* b10 = 63: (63 - 32) >> 2 = 7: F1 20 + 7 = 27, F2 40 + (7 >> 1) = 43 */
    raise_case(63, 20, 40, 27, 43, 7, 43, FRAME_F2);
    /* at the clamp: F1 60 + 7 = 67 -> 63, F2 62 + 3 = 65 -> 63 */
    raise_case(63, 60, 62, 63, 63, 7, 63, FRAME_F2);
    /* b10 = $9F: $7F >> 2 = $1F: F1 0 + 31 = 31, F2 0 + 15 = 15 */
    raise_case(0x9F, 0, 0, 31, 15, 0x1F, 15, FRAME_F2);
}

void suite_fw_frm(void)
{
    CHECK(fw_image_get() != NULL);
    test_data();
    test_leaf_rows();
    test_build_rest_frame();
    test_copy_rest_frame();
    test_snapshot();
    test_clamp_mark_space();
    test_raise_formants();
}
