/* Unit checks of the playback set-up on the converted data (M4.4a spec 4.4), with a mock host that records every
   ROM call. They reach what no corpus text reaches (par_clamp at its edges, every @B with v = 0, 1, 8, the
   R = 0 path of pros_scale_target below the limit, a microprosody step that wraps). Each expected value is worked
   out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. The full proof is the
   live differential test (diff_lts) and the hybrid goldens (golden_hybrid); the thunks' register outputs are
   compared there, against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_segment.h"
#include "firmware/pros_play.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 64

typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static uint8_t g_before[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call, and fails it: none of the leaves makes one. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r->a;
        c->x = r->x;
        c->y = r->y;
        c->budget = budget;
    }
    g_ncalls++;
    return FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Record i is a call of addr with A, X, Y and the strict budget. */
static void check_call(unsigned i, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].addr, addr);
    CHECK_EQ(g_calls[i].a, a);
    CHECK_EQ(g_calls[i].x, x);
    CHECK_EQ(g_calls[i].y, y);
    CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}

/* The hook of name sits at addr and declares outputs (pinned here; diff_lts compares them against the ROM). */
static void check_row(const char *name, uint16_t addr, uint8_t outputs)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, outputs);
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

/* ---- the leaves (M4.4a Task 4) ---- */

static void test_leaf_rows(void)
{
    check_row("pros_microprosody", ROM_PROS_MICROPROSODY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_next_row", ROM_PAR_NEXT_ROW, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_next_param", ROM_PAR_NEXT_PARAM, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_clamp", ROM_PAR_CLAMP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_scale_target", ROM_PROS_SCALE_TARGET, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_scale_voicing", ROM_PAR_SCALE_VOICING, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_scale_duration", ROM_PAR_SCALE_DURATION, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* par_next_row with par_row_ptr = ptr and par_row_len = len: the new pointer and A = its low byte (CLC; ADC; STA;
   BCC; INC hi), X and Y untouched, only par_row_ptr written. */
static void next_row_case(uint16_t ptr, uint8_t len, uint16_t want)
{
    static const uint16_t w[] = { PAR_ROW_PTR, PAR_ROW_PTR + 1 };
    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, ptr);
    g_ram[PAR_ROW_LEN] = len;
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_next_row(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), want);
    CHECK_EQ(r.a, want & 0xFF);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    check_written(w, 2);
    check_no_calls();
}

static void test_next_row(void)
{
    next_row_case(PAR_VOICE_A, 86, PAR_VOICE_A + 86);             /* $D46A + N = $D4C0: no carry */
    next_row_case(0xD4C0, 86, 0xD516);                            /* $C0 + $56 = $116: the carry into the high byte */
    next_row_case(0xFFF0, 0x20, 0x0010);                          /* the high byte wraps: INC $33 is 8 bits */
}

/* par_next_param with par_row_ptr = ptr and par_param_stride = stride: the 16-bit sum with the carry between the
   bytes (CLC once), A = its high byte. */
static void next_param_case(uint16_t ptr, uint16_t stride, uint16_t want)
{
    static const uint16_t w[] = { PAR_ROW_PTR, PAR_ROW_PTR + 1 };
    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, ptr);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, stride);
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_next_param(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), want);
    CHECK_EQ(r.a, want >> 8);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    check_written(w, 2);
    check_no_calls();
}

static void test_next_param(void)
{
    next_param_case(0xD400, 5 * 86, 0xD5AE);   /* the stride of N = 86 is 430 = $01AE: no carry */
    next_param_case(0xD4C0, 5 * 86, 0xD66E);   /* $C0 + $AE = $16E: the carry into $D4 + $01 */
    next_param_case(0xFF80, 0x0080, 0x0000);   /* the sum wraps in 16 bits: A = 0 */
}

/* par_clamp: CMP #1; BPL: the N flag of A - 1 is set for 0 and $81-$FF (1); CMP #$40; BPL: the N flag of A - $40 is
   clear for $40-$80 ($3F); 1-$3F pass. */
static void test_clamp(void)
{
    static const struct { uint8_t in, out; } k[] = {
        { 0x00, 1 }, { 0x01, 0x01 }, { 0x3F, 0x3F }, { 0x40, 0x3F }, { 0x80, 0x3F }, { 0x81, 1 }, { 0xFF, 1 },
        { 0x20, 0x20 }, { 0x7F, 0x3F }, { 0xC0, 1 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        setup();
        snapshot();
        fw_regs r = { k[i].in, 0x5A, 0xA5, 0 };
        par_clamp(&g_f, &r);
        CHECK_EQ(r.a, k[i].out);
        CHECK_EQ(r.x, 0x5A);
        CHECK_EQ(r.y, 0xA5);
        check_written(NULL, 0);
        check_no_calls();
    }
}

/* par_scale_voicing at every @B 0-9 with v = 0, 1, 8 and 61 (a P10 target: 61/2 = 30, /4 = 15, /8 = 7, /16 = 3).
   FW_TMP_76 starts at $EE: the term the ROM stores there, or $EE where it stores nothing (v = 1 and 8 at every @B,
   @B0, @B1, @B5, @B8, @B9, and v = 0 at @B2 and @B3, which give 2 at the BEQ $BFD1 first). */
static void test_scale_voicing(void)
{
    static const uint8_t v[4] = { 0, 1, 8, 61 };
    static const struct { uint8_t out[4], tmp[4]; } k[10] = {
        { { 2, 1, 8, 2 }, { 0xEE, 0xEE, 0xEE, 0xEE } },     /* @B0: LDA #2 */
        { { 2, 1, 8, 15 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B1: v/4 */
        { { 2, 1, 8, 19 }, { 0xEE, 0xEE, 0xEE, 15 } },      /* @B2: 3 + 15 + 1 (SEC; ADC); $76 = v/4 */
        { { 2, 1, 8, 22 }, { 0xEE, 0xEE, 0xEE, 15 } },      /* @B3: 7 + 15; $76 = v/4 */
        { { 0, 1, 8, 25 }, { 0, 0xEE, 0xEE, 22 } },         /* @B4: 3 + (7 + 15); $76 = v/8 + v/4; 0 gives 0 */
        { { 0, 1, 8, 30 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B5: v/2 */
        { { 0, 1, 8, 37 }, { 0, 0xEE, 0xEE, 30 } },         /* @B6: 7 + 30; $76 = v/2 */
        { { 0, 1, 8, 45 }, { 0, 0xEE, 0xEE, 30 } },         /* @B7: 15 + 30; $76 = v/2 */
        { { 0, 1, 8, 61 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B8: PLA, v */
        { { 0, 1, 8, 61 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B9 (the corpus never sets it): as @B8 */
    };
    static const uint16_t w[] = { FW_TMP_76 };
    for (uint8_t b = 0; b < 10; b++) {
        for (unsigned i = 0; i < 4; i++) {
            setup();
            g_ram[CMD_VOICING] = b;
            g_ram[FW_TMP_76] = 0xEE;
            snapshot();
            fw_regs r = { v[i], 0x5A, 0xA5, 0 };
            par_scale_voicing(&g_f, &r);
            CHECK_EQ(r.a, k[b].out[i]);
            CHECK_EQ(g_ram[FW_TMP_76], k[b].tmp[i]);
            CHECK_EQ(r.x, 0x5A);
            CHECK_EQ(r.y, 0xA5);
            check_written(w, 1);
            check_no_calls();
        }
    }
    setup();                                            /* each term truncates: $FF at @B2 is 15 + 63 + 1 */
    g_ram[CMD_VOICING] = 2;
    fw_regs r = { 0xFF, 0, 0, 0 };
    par_scale_voicing(&g_f, &r);
    CHECK_EQ(r.a, 79);
}

/* par_scale_duration at speed `speed` with A = a and Y = $C3 (X = $5A): A out, FW_TMP_78 (starting at $EE). */
static void scale_duration_case(uint8_t speed, uint8_t a, uint8_t want_a, uint8_t want_78)
{
    static const uint16_t w[] = { FW_TMP_78 };
    g_ram[CMD_SPEED] = speed;
    g_ram[FW_TMP_78] = 0xEE;
    snapshot();
    fw_regs r = { a, 0x5A, 0xC3, 0 };
    par_scale_duration(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(g_ram[FW_TMP_78], want_78);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xC3);                                /* LDY $78: the entry Y, on every path */
    check_written(w, 1);
    check_no_calls();
}

static void test_scale_duration(void)
{
    setup();                                            /* a map in RAM: index i holds $80 + i */
    fw_wr16(&g_f, CMD_SPEED_MAP, 0x3000);
    for (unsigned i = 0; i < 64; i++)
        g_ram[0x3000 + i] = (uint8_t)(0x80 + i);
    scale_duration_case(9, 0, 0, 0xEE);                 /* CMP #0; BEQ: 0 passes, $78 is not written */
    scale_duration_case(1, 0, 0, 0xEE);
    scale_duration_case(4, 40, 40, 0xC3);               /* below speed 5 (CPY #5; BCC): $78 = Y, A unchanged */
    scale_duration_case(1, 0xFF, 0xFF, 0xC3);
    scale_duration_case(5, 30, 0x80 + 30, 0xC3);        /* speed 5 and up: map[A] ... */
    scale_duration_case(5, 31, 0x80 + 31, 0xC3);
    scale_duration_case(9, 32, 0x80 + 31, 0xC3);        /* ... the index capped at 31 (CMP #$1F; BCC; LDA #$1F) */
    scale_duration_case(7, 0xFF, 0x80 + 31, 0xC3);
    scale_duration_case(6, 1, 0x80 + 1, 0xC3);

    setup();                                            /* the speed 5 map in ROM, through the pointer (fw_ind_y) */
    fw_wr16(&g_f, CMD_SPEED_MAP, CMD_SPEED_MAP_DATA + 32 * 4);
    scale_duration_case(5, 10, fw_cmd_speed_map_data[4][10], 0xC3);
    scale_duration_case(5, 40, fw_cmd_speed_map_data[4][31], 0xC3);
}

/* One pros_scale_target run. In: pros_play_target, cmd_source_prosody, cmd_speed_limit, pros_pitch_centre; the four
   scratch bytes start at $EE. Out: the new target, and $1811 (FW_PROS_SCALE_DEV, $EE when not written); A = the
   entry Y ($C3), X ($5A) and Y preserved. $1812-$1814 are checked by the caller. */
static void scale_target_case(uint8_t target, uint8_t prosody, uint8_t limit, uint8_t centre, uint8_t want,
                              uint8_t want_dev)
{
    static const uint16_t w[] = {
        PROS_PLAY_TARGET, FW_PROS_SCALE_DEV, FW_PROS_SCALE_FRAC, FW_PROS_DEV_FRAC, FW_PROS_DEV_QUARTER,
    };
    setup();
    g_ram[PROS_PLAY_TARGET] = target;
    g_ram[CMD_SOURCE_PROSODY] = prosody;
    g_ram[CMD_SPEED_LIMIT] = limit;
    g_ram[PROS_PITCH_CENTRE] = centre;
    memset(g_ram + FW_PROS_SCALE_DEV, 0xEE, 4);
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xC3, 0 };
    pros_scale_target(&g_f, &r);
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], want);
    CHECK_EQ(g_ram[FW_PROS_SCALE_DEV], want_dev);
    CHECK_EQ(r.a, 0xC3);                                /* PLA; TAY last: A = the entry Y */
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xC3);
    check_written(w, 5);
    check_no_calls();
}

/* The split of d = |target - $80| (written on every path): FW_PROS_DEV_QUARTER = d >> 2, FW_PROS_DEV_FRAC =
   (d & 3) << 6, and FW_PROS_SCALE_FRAC as the adds and the ASL leave it. */
static void check_split(uint8_t quarter, uint8_t frac, uint8_t scale_frac)
{
    CHECK_EQ(g_ram[FW_PROS_DEV_QUARTER], quarter);
    CHECK_EQ(g_ram[FW_PROS_DEV_FRAC], frac);
    CHECK_EQ(g_ram[FW_PROS_SCALE_FRAC], scale_frac);
}

static void test_scale_target(void)
{
    /* R = 0 below the limit: the BEQ at $BE06 sees the Z of CMP cmd_speed_limit (clear), so it goes on: Y = 0
       (R < 4), k = 0, s = TYA = 0, $1811 = 0 at $BE31; centre + 0 */
    scale_target_case(0x90, 0x08, 7, 0x60, 0x60, 0);   /* @R bits 0-2 only: $08 & 7 = 0 */
    check_split(4, 0, 0);                               /* d = $10 */
    /* R = 0 at the limit: R = limit & 7 = 0, BEQ $BE52: the centre, $1811 not written */
    scale_target_case(0x90, 0, 0, 0x60, 0x60, 0xEE);
    check_split(4, 0, 0);
    /* R above the limit becomes the limit: R = 5, limit 0 (speeds 7-9): the centre, $1811 not written */
    scale_target_case(0x90, 5, 0, 0x61, 0x61, 0xEE);
    /* R = 7, limit 7 (not below it): R = 7 & 7. d = $40: Y = d (R >= 4), $1811 = $40 + 3 x $10 = $70, frac 0; centre
       $F0 + $70 carries: $FF ($BE4D), then CMP #4 is negative: ORA #4 keeps $FF */
    scale_target_case(0xC0, 7, 7, 0xF0, 0xFF, 0x70);
    check_split(0x10, 0, 0);
    /* the same deviation below $80: centre $20 - $70 borrows: 4 ($BE65), and CMP #4 passes 4 */
    scale_target_case(0x40, 7, 7, 0x20, 4, 0x70);
    /* R = 3 (Y = 0, the signed CMP #4 / BPL): d = $10, s = 3 x 4 = $0C; centre $0E - $0C = 2: ORA #4 gives 6 */
    scale_target_case(0x70, 3, 7, 0x0E, 6, 0x0C);
    /* R = 3, d = 1: the fraction $40 three times: $1812 = $C0, ASL carries 1 into ADC #0: s = 1, $1812 = $80;
       centre $87 + 1 = $88 is negative after CMP #4: ORA #4 gives $8C */
    scale_target_case(0x81, 3, 7, 0x87, 0x8C, 1);
    check_split(0, 0x40, 0x80);
    /* R = 7, d = 3: Y = 3; the fraction $C0 chains its carry into $1811: $1812 $C0, $80 (C), $40 (C); $1811 3, 4, 5;
       ASL $40: no carry: s = 5 (3 x 7 / 4 = 5.25); centre $40 + 5 */
    scale_target_case(0x83, 7, 7, 0x40, 0x45, 5);
    check_split(0, 0xC0, 0x80);
    /* R = 4 (k = 0): s = Y = d = $10 through TYA at $BE58 */
    scale_target_case(0x90, 4, 7, 0x50, 0x60, 0x10);
    /* R = 5 limited from 7 by limit 5 (R & 7 = 5): d = $13, quarter 4, frac $C0; s = $13 + 4 = $17, $1812 $C0, ASL
       carries: $18 (19 x 5 / 4 = 23.75); centre $40 + $18 */
    scale_target_case(0x93, 7, 5, 0x40, 0x58, 0x18);
    check_split(4, 0xC0, 0x80);
    /* target 0: d = $80 (EOR #$FF; ADC #1 of $80); R = 3: s = 3 x $20 = $60; centre $70 - $60 = $10 */
    scale_target_case(0x00, 3, 7, 0x70, 0x10, 0x60);
    check_split(0x20, 0, 0);
    /* a result of $80-$83: CMP #4 positive, no ORA (centre $83, R = 0 at limit 0) */
    scale_target_case(0x10, 0, 0, 0x83, 0x83, 0xEE);
    /* a centre of 0-3 on the R = 0 path still gets bit 2 */
    scale_target_case(0x10, 0, 0, 0x01, 0x05, 0xEE);
}

/* pros_microprosody with par_next_phoneme = ph and pros_play_target = t: A and the target left; X and Y untouched,
   only the target may be written. */
static void micro_case(uint8_t ph, uint8_t t, uint8_t want_a, uint8_t want_t)
{
    static const uint16_t w[] = { PROS_PLAY_TARGET };
    setup();
    g_ram[PAR_NEXT_PHONEME] = ph;
    g_ram[PROS_PLAY_TARGET] = t;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xA5, 0 };
    pros_microprosody(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], want_t);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    check_no_calls();
}

static void test_microprosody(void)
{
    static const uint8_t up[] = { 0x15, 0x18, 0x20, 0x41 }, down[] = { 0x1F, 0x0D };
    for (size_t i = 0; i < sizeof up; i++) {
        micro_case(up[i], 0x50, 0x55, 0x55);            /* CLC; ADC #5; BCC: stored */
        micro_case(up[i], 0xFA, 0xFF, 0xFF);
        micro_case(up[i], 0xFB, 0x00, 0xFB);            /* carries: not stored, A = the wrapped sum */
        micro_case(up[i], 0xFF, 0x04, 0xFF);
    }
    for (size_t i = 0; i < sizeof down; i++) {
        micro_case(down[i], 0x50, 0x4B, 0x4B);          /* SEC; SBC #5; BCS: stored */
        micro_case(down[i], 0x05, 0x00, 0x00);
        micro_case(down[i], 0x04, 0xFF, 0x04);          /* borrows: not stored, A = the wrapped difference */
        micro_case(down[i], 0x00, 0xFB, 0x00);
    }
    micro_case(0x3B, 0x50, 0x3B, 0x50);                 /* any other phoneme: RTS with A = the phoneme */
    micro_case(0x00, 0xFB, 0x00, 0xFB);
    micro_case(0x14, 0x50, 0x14, 0x50);
    micro_case(0xFF, 0x50, 0xFF, 0x50);
}

void suite_fw_par(void)
{
    CHECK(fw_image_get() != NULL);
    test_leaf_rows();
    test_next_row();
    test_next_param();
    test_clamp();
    test_scale_voicing();
    test_scale_duration();
    test_scale_target();
    test_microprosody();
}
