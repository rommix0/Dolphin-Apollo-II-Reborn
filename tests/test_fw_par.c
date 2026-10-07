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
#include "firmware/par_start.h"
#include "firmware/pros_play.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 64

/* One ROM call: its address, registers and budget, and the frame buffer pointers as the C left them at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint16_t play, fill;
    uint8_t phase;
} rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static uint8_t g_before[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) return the registers the ROM ones
   leave; any other call fails (no other routine of the playback set-up makes one). */
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
        c->play = fw_rd16(&g_f, ISR_PLAY_PTR);
        c->fill = fw_rd16(&g_f, ISR_FILL_PTR);
        c->phase = g_ram[ISR_SWAP_PHASE];
    }
    g_ncalls++;
    switch (addr) {
    case ROM_FRM_BUILD_REST_FRAME:   /* $B428-$B444: LDY #$3B; LDX #1, INX / INX until X >= 8 (9); LDA par_params_ptr+1 */
        r->a = g_ram[PAR_PARAMS_PTR + 1];
        r->x = 9;
        r->y = 0x3B;
        return FW_CALL_RETURNED;
    case ROM_FRM_COPY_REST_FRAME:    /* $B445-$B44F: LDA $2000,Y for Y = $13..0, DEY / BPL: Y = $FF; X untouched */
        r->a = g_ram[FRM_REST_FRAME];
        r->y = 0xFF;
        return FW_CALL_RETURNED;
    case ROM_FRM_CLAMP_MARK_SPACE:   /* $B930-$B95C: LDY #$0A, INY; LDA #$0B at $B954; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
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

/* ---- the boundary pass (M4.4a Task 5) ---- */

/* A voice table in RAM (fw_ind_y reads RAM below $4000) with N = PT_N phonemes per row: parameter p's five rows
   (target, fixed contribution, proportion, external duration, internal duration) start at PT_BASE + 5 * PT_N * p, as
   they do from par_params_ptr; the ranks at PT_RANKS (par_rank_ptr). */
#define PT_BASE   0x3000
#define PT_N      8
#define PT_RANKS  0x2F00
enum { ROW_TARGET, ROW_FIXED, ROW_PROP, ROW_EXT_DUR, ROW_INT_DUR };

static void pt_set(unsigned p, unsigned row, uint8_t phoneme, uint8_t v)
{
    g_ram[PT_BASE + 5 * PT_N * p + PT_N * row + phoneme] = v;
}

/* Zeroed RAM; par_row_ptr on P1's target row, par_row_len N, par_param_stride 5N, par_rank_ptr on the ranks. */
static void pt_setup(void)
{
    setup();
    g_ram[PAR_ROW_LEN] = PT_N;
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 5 * PT_N);
    fw_wr16(&g_f, PAR_ROW_PTR, PT_BASE);
    fw_wr16(&g_f, PAR_RANK_PTR, PT_RANKS);
}

static void test_boundary_rows(void)
{
    check_row("par_boundary_pass", ROM_PAR_BOUNDARY_PASS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_jump", ROM_PAR_BOUNDARY_JUMP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_store_boundary", ROM_PAR_STORE_BOUNDARY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_cur_dom", ROM_PAR_BOUNDARY_CUR_DOM, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_next_dom", ROM_PAR_BOUNDARY_NEXT_DOM, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_read_dominance", ROM_PAR_READ_DOMINANCE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_value", ROM_PAR_BOUNDARY_VALUE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* $BEB5: par_store_boundary's two cuts ($BEB9 and $BEC9), both unsigned (BCS), the second only while par_trans_dur
   is 0; par_end_dur = par_trans_dur; A = par_trans_dur, X + 1, Y kept */
static void test_store_boundary(void)
{
    fw_regs r;
    setup();
    g_ram[PAR_FRAMES_LEFT] = 5;                     /* no start transition: par_trans_dur 9 is cut to 5 */
    g_ram[PAR_TRANS_DUR] = 9;
    r = (fw_regs){ 0x2A, 2, 0x77, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 3], 0x2A);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 5);
    CHECK_EQ(g_ram[PAR_END_DUR + 3], 5);
    CHECK_EQ(g_ram[PAR_START_DUR + 3], 0);          /* the second cut: par_trans_dur is not 0 */
    CHECK_EQ(r.a, 5);
    CHECK_EQ(r.x, 3);
    CHECK_EQ(r.y, 0x77);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 0);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 0);

    g_ram[PAR_TRANS_DUR] = 0xF0;                    /* unsigned: 5 < $F0 cuts (a signed compare would not) */
    r = (fw_regs){ 0x11, 0, 0, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 5);
    CHECK_EQ(g_ram[PAR_END_DUR + 1], 5);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 0x11);

    g_ram[PAR_TRANS_DUR] = 6;                       /* par_frames_left 6, equal: no cut (BCS) */
    g_ram[PAR_FRAMES_LEFT] = 6;
    r = (fw_regs){ 0x12, 0, 0, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 6);

    g_ram[PAR_START_DUR + 5] = 12;                  /* a start transition and no end one: the start one is cut */
    g_ram[PAR_FRAMES_LEFT] = 7;
    g_ram[PAR_TRANS_DUR] = 0;
    r = (fw_regs){ 0x13, 4, 0x01, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_START_DUR + 5], 7);
    CHECK_EQ(g_ram[PAR_END_DUR + 5], 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 5);

    g_ram[PAR_START_DUR + 2] = 12;                  /* both: neither is cut (the first needs par_start_dur 0, */
    g_ram[PAR_FRAMES_LEFT] = 3;                     /* the second par_trans_dur 0) */
    g_ram[PAR_TRANS_DUR] = 6;
    r = (fw_regs){ 0x14, 1, 0, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_START_DUR + 2], 12);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 6);
    CHECK_EQ(g_ram[PAR_END_DUR + 2], 6);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* One par_read_dominance at X = p, @B voicing, with par_target_phoneme 3 and par_dominant_phoneme 5: their rows of
   parameter p hold target, fixed and prop; par_row_ptr starts on p's target row. */
static void dominance_case(uint8_t p, uint8_t voicing, uint8_t ofs, uint8_t target, uint8_t fixed, uint8_t prop,
                           fw_regs *r)
{
    pt_setup();
    fw_wr16(&g_f, PAR_ROW_PTR, (uint16_t)(PT_BASE + 5 * PT_N * p));
    g_ram[CMD_VOICING] = voicing;
    g_ram[PAR_TARGET_PHONEME] = 3;
    g_ram[PAR_DOMINANT_PHONEME] = 5;
    g_ram[PAR_OFS] = ofs;
    pt_set(p, ROW_TARGET, 3, target);
    pt_set(p, ROW_FIXED, 5, fixed);
    pt_set(p, ROW_PROP, 5, prop);
    *r = (fw_regs){ 0xA5, p, 0xB6, 0 };
    par_read_dominance(&g_f, r);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* $BF6C: the dominated target clamped; the fixed contribution plus the weighted @u offset: 8 - prop adds of par_ofs
   in 8 bits, >> 3, negative only when the last add carried ($BFA6-$BFBA); the weight wraps for a proportion above 8
   (latent: the tables hold 0-8) */
static void test_read_dominance(void)
{
    fw_regs r;
    uint16_t row3 = PT_BASE + 5 * PT_N * 2 + 3 * PT_N;
    dominance_case(2, 8, 0xFE, 40, 20, 7, &r);      /* weight 1: 0 + $FE, no carry: $FE >> 3 = $1F, so -2 adds +31 */
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 38);      /* 40 + $FE, clamped (1..$3F: kept) */
    CHECK_EQ(g_ram[PAR_PROP], 7);
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 1);             /* (7 - 9) ^ $FF */
    CHECK_EQ(g_ram[PAR_FIXED], 20 + 0x1F);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), row3);     /* three rows on: the external durations */
    CHECK_EQ(r.a, row3 & 0xFF);                     /* par_next_row's low byte */
    CHECK_EQ(r.x, 2);
    CHECK_EQ(r.y, 5);                               /* the dominant phoneme, for its duration rows */
    CHECK_EQ(g_ram[FW_TMP_76], 0);                  /* no par_scale_voicing below P10 */

    dominance_case(2, 8, 0xFE, 40, 20, 6, &r);      /* weight 2: $FE + $FE carries: $FC >> 3 | $E0 = $FF (-1) */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 2);
    CHECK_EQ(g_ram[PAR_FIXED], 19);

    dominance_case(2, 8, 0x50, 40, 20, 4, &r);      /* weight 4: $50 x 4 = $140, the last add carries: +40 */
    CHECK_EQ(g_ram[PAR_FIXED], (uint8_t)(20 + 0xE8));   /* becomes ($40 >> 3) | $E0 = $E8 (-24) */
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 0x3F);    /* 40 + $50 = $78: par_clamp gives $3F */

    dominance_case(2, 8, 0x90, 40, 20, 5, &r);      /* weight 3: $90, $120 (carries), $B0 (does not): only the last */
    CHECK_EQ(g_ram[PAR_FIXED], 20 + 0x16);          /* counts, so $B0 >> 3 = $16 stays positive */

    dominance_case(2, 8, 0x50, 40, 20, 8, &r);      /* weight 0 (LDX; BEQ): par_fixed as read */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0);
    CHECK_EQ(g_ram[PAR_FIXED], 20);
    CHECK_EQ(r.x, 2);

    dominance_case(2, 8, 0xFF, 40, 20, 9, &r);      /* prop 9: weight (9 - 9) ^ $FF = 255 adds of $FF; the last is */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0xFF);          /* 2 + $FF, which carries: 1 >> 3 | $E0 = $E0 */
    CHECK_EQ(g_ram[PAR_FIXED], (uint8_t)(20 + 0xE0));
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 39);      /* 40 + $FF */

    dominance_case(9, 6, 0, 2, 46, 8, &r);          /* P10 (CPX #9): @B6 scales 46 to 46/2 + 46/8 = 28 ... */
    CHECK_EQ(g_ram[PAR_FIXED], 28);
    CHECK_EQ(g_ram[FW_TMP_76], 23);                 /* ... with its scratch */
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 2);       /* the target is not scaled */
    CHECK_EQ(r.x, 9);
    dominance_case(8, 6, 0, 2, 46, 8, &r);          /* P9: not scaled */
    CHECK_EQ(g_ram[PAR_FIXED], 46);
}

/* One par_boundary_value at X = x, Y = $66, with par_prop, par_dominated_target and par_fixed as given;
   par_start_dur[x + 1] = 1 and par_trans_dur = 7, so par_store_boundary cuts nothing and leaves A = 7. */
static void value_case(uint8_t x, uint8_t prop, uint8_t target, uint8_t fixed, fw_regs *r)
{
    setup();
    g_ram[PAR_PROP] = prop;
    g_ram[PAR_DOMINATED_TARGET] = target;
    g_ram[PAR_FIXED] = fixed;
    g_ram[PAR_START_DUR + 1 + x] = 1;
    g_ram[PAR_TRANS_DUR] = 7;
    g_ram[PAR_FRAMES_LEFT] = 200;
    *r = (fw_regs){ 0xA5, x, 0x66, 0 };
    par_boundary_value(&g_f, r);
    CHECK_EQ(r->a, 7);
    CHECK_EQ(r->x, (uint8_t)(x + 1));
    CHECK_EQ(g_ram[PAR_END_DUR + 1 + x], 7);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* $C039: par_target_bias is 18, 4, 10, 26, then 0 for P5-P10 (ROM $B7A7). Each path: prop 0, 8, a multiple of 4,
   and the halves (with the 9-bit sum the ROR at $C09B takes, and the carry chained through a halves loop); the
   limit (0 and $40 up become 1, unsigned); the halves state; Y = 0 on the halves path only. */
static void test_boundary_value(void)
{
    fw_regs r;
    value_case(1, 0, 33, 0x45, &r);                 /* prop 0: par_clamp($45) = $3F, no 0 / $40 test */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 0x3F);
    CHECK_EQ(r.y, 0x66);
    CHECK_EQ(g_ram[PAR_TARGET_X2], 66);             /* the set-up runs on every path: t = 33 */
    CHECK_EQ(g_ram[PAR_TARGET_HALF], 16);
    CHECK_EQ(g_ram[PAR_BIAS_X2], 8);                /* b = 4 */
    CHECK_EQ(g_ram[PAR_BIAS_HALF], 2);
    value_case(1, 0, 33, 0, &r);                    /* prop 0: par_clamp(0) = 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 1);

    value_case(3, 8, 33, 0xF8, &r);                 /* prop 8: 33 + 26 - 8 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 51);
    CHECK_EQ(r.y, 0x66);
    value_case(3, 8, 0xF0, 0, &r);                  /* latent: $F0 + 26 = $10A, the carry lost: 10 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 10);

    value_case(0, 4, 33, 3, &r);                    /* prop 4: 18 + 33 = 51, >> 1 rounded = 26, + 3 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 29);
    CHECK_EQ(r.y, 0x66);
    value_case(2, 12, 21, 0, &r);                   /* prop 12, & 3 = 0: the same path, 10 + 21 = 31 -> 16 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 3], 16);

    value_case(1, 3, 33, 5, &r);                    /* prop 3: 3 halves from 0: target 16 x 3 = 48, bias 2 x 3 = 6; */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 19);          /* 54 >> 2 = 13, + the bit out (1) = 14, + 5 */
    CHECK_EQ(g_ram[PAR_TARGET_SUM], 48);
    CHECK_EQ(g_ram[PAR_BIAS_SUM], 0);               /* the bias's sum stays in A */
    CHECK_EQ(g_ram[PAR_PROP_LOW], 3);
    CHECK_EQ(r.y, 0);

    value_case(3, 7, 63, 0xF0, &r);                 /* prop 7: from x2: target 126 + 31 x 3 = 219, bias 52 + 13 x 3 = */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 62);          /* 91; 310 is 9 bits: ROR gives $9B, LSR $4D + 1 = $4E; + $F0 */
    CHECK_EQ(g_ram[PAR_TARGET_SUM], 219);           /* = $13E -> $3E (with 8 bits only it would be 1) */
    CHECK_EQ(g_ram[PAR_BIAS_SUM], 52);              /* x2, not the sum */
    CHECK_EQ(g_ram[PAR_PROP_LOW], 3);
    CHECK_EQ(r.y, 0);

    value_case(4, 6, 0x7F, 0, &r);                  /* latent: prop 6 from x2 $FE: $FE + $3F carries into the */
    CHECK_EQ(g_ram[PAR_TARGET_SUM], 0x7D);          /* second add: $3D + $3F + 1 = $7D */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 0x1F);        /* bias 0: $7D >> 2 = $1F, the bit out 0 */

    value_case(4, 8, 16, 0xF0, &r);                 /* the limit: 16 + 0 - 16 = 0 becomes 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 1);
    value_case(4, 8, 20, 0xC0, &r);                 /* $D4: $40 or more, unsigned: 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 1);
    value_case(4, 8, 20, 43, &r);                   /* $3F is kept */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 0x3F);
    value_case(4, 8, 20, 44, &r);                   /* $40: 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 1);
}

/* Phonemes 2 and 6 in the RAM table: per parameter p, targets 30 + p and 40 + p, fixed contributions 10 + p and
   20 + p, proportion 0 (the boundary is par_clamp(par_fixed)), external durations 3 + p and 50 + p, internal 7 + p
   and 70 + p; ranks 5 and 9. cmd_formant_ofs[1] = 2 (weight 8: 16 >> 3 = +2 on P2's fixed contribution), @B5,
   speed 0 (par_scale_duration passes), par_frames_left 200 (no cut). */
static void pair_setup(uint8_t cur, uint8_t next)
{
    pt_setup();
    for (unsigned p = 0; p < 10; p++) {
        pt_set(p, ROW_TARGET, 2, (uint8_t)(30 + p));
        pt_set(p, ROW_TARGET, 6, (uint8_t)(40 + p));
        pt_set(p, ROW_FIXED, 2, (uint8_t)(10 + p));
        pt_set(p, ROW_FIXED, 6, (uint8_t)(20 + p));
        pt_set(p, ROW_EXT_DUR, 2, (uint8_t)(3 + p));
        pt_set(p, ROW_EXT_DUR, 6, (uint8_t)(50 + p));
        pt_set(p, ROW_INT_DUR, 2, (uint8_t)(7 + p));
        pt_set(p, ROW_INT_DUR, 6, (uint8_t)(70 + p));
    }
    g_ram[PT_RANKS + 2] = 5;
    g_ram[PT_RANKS + 6] = 9;
    g_ram[CMD_FORMANT_OFS + 1] = 2;
    g_ram[CMD_VOICING] = 5;
    g_ram[PAR_FRAMES_LEFT] = 200;
    g_ram[PAR_CUR_PHONEME] = cur;
    g_ram[PAR_CUR_RANK] = g_ram[PT_RANKS + cur];
    g_ram[PAR_NEXT_PHONEME] = next;
}

/* The pass picks the dominant phoneme by rank, and the two dominance routines take its durations the other way
   round: next_dom row 3 (external) into par_trans_dur and row 4 into par_next_start_dur, cur_dom the reverse */
static void test_pass_dominance(void)
{
    fw_regs r;
    pair_setup(2, 6);                               /* next rank 9 >= 5: par_boundary_next_dom, phoneme 6 dominates */
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 9);
    CHECK_EQ(g_ram[PAR_TARGET_PHONEME], 2);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1 + p], 70 + p);    /* row 4 */
        CHECK_EQ(g_ram[PAR_END_DUR + 1 + p], 50 + p);           /* row 3, through par_trans_dur */
        CHECK_EQ(g_ram[PAR_BOUNDARY + 1 + p], p == 1 ? 23 : p == 9 ? 29 / 2 : 20 + p);   /* P10 at @B5: 29 / 2 */
    }
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 59);
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 39);      /* the last parameter's: the target phoneme 2's 30 + 9 */
    CHECK_EQ(g_ram[PAR_FIXED], 14);
    CHECK_EQ(g_ram[PAR_OFS], 0);                    /* cmd_formant_ofs[9] */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 8);
    CHECK_EQ(g_ram[PAR_PARAM_INDEX], 10);
    CHECK_EQ(g_ram[FW_TMP_78], 6);                  /* par_scale_duration's saved Y */
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), PT_BASE + 50 * PT_N);   /* ten parameters of five rows on */
    CHECK_EQ(r.a, 10);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 6);                               /* prop 0 keeps Y */
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR], 0);         /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_BOUNDARY], 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);

    pair_setup(6, 2);                               /* next rank 5 < 9: par_boundary_cur_dom, phoneme 6 dominates */
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 5);
    CHECK_EQ(g_ram[PAR_TARGET_PHONEME], 2);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1 + p], 50 + p);    /* row 3 */
        CHECK_EQ(g_ram[PAR_END_DUR + 1 + p], 70 + p);           /* row 4, through par_trans_dur */
        CHECK_EQ(g_ram[PAR_BOUNDARY + 1 + p], p == 1 ? 23 : p == 9 ? 29 / 2 : 20 + p);
    }
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 79);
    CHECK_EQ(r.a, 10);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 6);
    CHECK(!g_f.fault);

    pair_setup(2, 2);                               /* equal ranks: par_boundary_next_dom (BCC not taken) */
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_END_DUR + 1], 3);            /* row 3 into par_trans_dur: next_dom */
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1], 7);

    pair_setup(2, 6);                               /* the next rank 29 or more, the current one below: no jump */
    g_ram[PT_RANKS + 6] = 31;
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1], 70);
    pair_setup(6, 2);                               /* the current rank 29 or more, the next one below: no jump */
    g_ram[PAR_CUR_RANK] = 31;
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1], 50);
    CHECK(!g_f.fault);
}

/* Both ranks 29 or more: par_boundary_jump stores the next phoneme's raw targets (no @u offset, no clamp) at the
   running X, entering here at 3 (it is 0 in the ROM, but the pass does not set it): par_store_boundary indexes
   par_boundary, par_start_dur and par_end_dur at X + 1 while par_param_index counts 10 down to 1 */
static void test_jump_running_x(void)
{
    static const uint8_t raw[10] = { 0x90, 0, 63, 64, 5, 6, 7, 8, 9, 10 };
    fw_regs r;
    pt_setup();
    for (unsigned p = 0; p < 10; p++) {
        pt_set(p, ROW_TARGET, 4, raw[p]);
        g_ram[CMD_FORMANT_OFS + p] = 0x11;          /* not added on this path */
    }
    for (unsigned i = 0; i < 16; i++) {
        g_ram[PAR_NEXT_START_DUR + i] = 0x55;
        g_ram[PAR_START_DUR + i] = 6;
    }
    g_ram[PT_RANKS + 4] = 31;
    g_ram[PAR_CUR_RANK] = 29;
    g_ram[PAR_NEXT_PHONEME] = 4;
    g_ram[PAR_TRANS_DUR] = 9;
    g_ram[PAR_FRAMES_LEFT] = 4;
    r = (fw_regs){ 0xA5, 3, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    for (unsigned i = 1; i <= 10; i++)
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + i], 0); /* par_next_start_dur[1..10], by Y, not X */
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 0], 0x55);
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 11], 0x55);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 0);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_BOUNDARY + 4 + p], raw[p]);
        CHECK_EQ(g_ram[PAR_START_DUR + 4 + p], 4);  /* par_trans_dur 0: the second cut, 6 to par_frames_left */
        CHECK_EQ(g_ram[PAR_END_DUR + 4 + p], 0);
    }
    for (unsigned i = 1; i <= 3; i++) {
        CHECK_EQ(g_ram[PAR_BOUNDARY + i], 0);
        CHECK_EQ(g_ram[PAR_START_DUR + i], 6);
    }
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 31);
    CHECK_EQ(g_ram[PAR_PARAM_INDEX], 0);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), PT_BASE + 50 * PT_N);  /* ten strides on, not reset */
    CHECK_EQ(r.a, (PT_BASE + 50 * PT_N) >> 8);      /* par_next_param's high byte */
    CHECK_EQ(r.x, 13);
    CHECK_EQ(r.y, 4);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The running X through a dominance pass: par_param_index p indexes cmd_formant_ofs and par_next_start_dur, X = p + 1
   the stores and par_target_bias. At p = 9, X = 10 reads $B7B1, past par_target_bias (unused_B7B1, not converted):
   the C faults there; no ROM path enters the pass with X other than 0. */
static void test_dominance_running_x(void)
{
    fw_regs r;
    pair_setup(6, 2);
    r = (fw_regs){ 0xA5, 1, 0xB6, 0 };
    par_boundary_cur_dom(&g_f, &r);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1 + p], 50 + p);
        CHECK_EQ(g_ram[PAR_END_DUR + 2 + p], 70 + p);
    }
    for (unsigned p = 0; p < 9; p++)
        CHECK_EQ(g_ram[PAR_BOUNDARY + 2 + p], p == 1 ? 23 : 20 + p);   /* P2's offset lands in par_boundary[3] */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 0);
    CHECK_EQ(r.x, 11);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, PAR_TARGET_BIAS + 10);
}

/* The end of a @T effect: par_next_phoneme $FF, read past the 86 phonemes of the converted voice tables: rank[$FF]
   is 32 and the pause (rank 31) is current, so par_boundary_jump stores the raw bytes at index $FF of P1..P10's
   target rows, 4, 4, 0, 4, 8, 8, 8, 8, 8, 8 in both voices (the 0 as it is) */
static void test_jump_end_marker(void)
{
    static const uint8_t raw[10] = { 4, 4, 0, 4, 8, 8, 8, 8, 8, 8 };
    static const uint16_t voice[2] = { PAR_VOICE_A, PAR_VOICE_B };
    for (unsigned v = 0; v < 2; v++) {
        fw_regs r = { 0, 0, 0, 0 };
        setup();
        g_ram[PAR_ROW_LEN] = PAR_PHONEMES;
        fw_wr16(&g_f, PAR_PARAM_STRIDE, 5 * PAR_PHONEMES);
        fw_wr16(&g_f, PAR_RANK_PTR, voice[v]);
        fw_wr16(&g_f, PAR_ROW_PTR, (uint16_t)(voice[v] + 2 * PAR_PHONEMES));   /* par_params_ptr */
        g_ram[PAR_NEXT_PHONEME] = 0xFF;
        g_ram[PAR_CUR_RANK] = 31;
        g_ram[PAR_FRAMES_LEFT] = 3;
        par_boundary_pass(&g_f, &r);
        CHECK_EQ(g_ram[PAR_NEXT_RANK], 32);
        for (unsigned p = 0; p < 10; p++) {
            CHECK_EQ(g_ram[PAR_BOUNDARY + 1 + p], raw[p]);
            CHECK_EQ(g_ram[PAR_END_DUR + 1 + p], 0);
        }
        CHECK_EQ(r.x, 10);
        CHECK_EQ(r.y, 0xFF);
        CHECK(!g_f.fault);
    }
}

/* ---- the top of the playback set-up (M4.4a Task 6) ---- */

#define T6_TABLE   0x3000   /* a voice table built in RAM: one parameter block every T6_STRIDE bytes */
#define T6_STRIDE  0x10
#define T6_RANKS   0x3800   /* its rank row */
#define T6_CUR     5        /* the phoneme that becomes the current one */
#define T6_NEXT    0x2A     /* the phoneme at the next index: in no microprosody list */

static void test_top_rows(void)
{
    check_row("par_setup_voice", ROM_PAR_SETUP_VOICE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_play_start", ROM_PAR_PLAY_START, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_reset_tracks", ROM_PAR_RESET_TRACKS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_next_segment", ROM_PAR_NEXT_SEGMENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_read_segment", ROM_PAR_READ_SEGMENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_plan_tracks", ROM_PAR_PLAN_TRACKS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* The pitch settings under which pros_scale_target keeps a target below $80: R = cmd_source_prosody & 7 = 4 (below
   cmd_speed_limit 7) adds the whole deviation d = |target - $80| back to the centre $80 ($BE08 BPL taken, k = 0:
   A = Y = d at $BE58), and $80 - d >= 4 gets no ORA #4. */
static void pitch_identity(void)
{
    g_ram[CMD_SOURCE_PROSODY] = 4;
    g_ram[CMD_SPEED_LIMIT] = 7;
    g_ram[PROS_PITCH_CENTRE] = 0x80;
}

/* par_cur_phoneme-to-be T6_CUR's ten targets in the RAM table, with their @u offsets, one per case of $BD83-$BD99,
   and the segment arrays at index 4 with the duration at index 3; the tracks' boundaries and next start durations
   to copy; pros_seg_count = $0100. */
static void read_setup(void)
{
    setup();
    pitch_identity();
    fw_wr16(&g_f, PAR_PARAMS_PTR, T6_TABLE);
    fw_wr16(&g_f, PAR_ROW_PTR, T6_TABLE);           /* par_next_segment's job ($BCF3) */
    fw_wr16(&g_f, PAR_PARAM_STRIDE, T6_STRIDE);
    static const uint8_t target[10] = { 0x85, 2, 3, 0x30, 0x7F, 0x70, 0x20, 4, 0x3F, 0x10 };
    static const uint8_t ofs[10] = { 0xC0, 5, 5, 0x20, 1, 0x20, 0xFE, 0xFC, 0, 0 };
    for (unsigned p = 0; p < 10; p++) {
        g_ram[T6_TABLE + T6_CUR + T6_STRIDE * p] = target[p];
        g_ram[CMD_FORMANT_OFS + p] = ofs[p];
    }
    g_ram[CMD_VOICING] = 3;
    g_ram[PAR_NEXT_PHONEME] = T6_CUR;
    g_ram[PAR_NEXT_RANK] = 0x21;
    g_ram[PROS_PLAY_TARGET] = 0x55;
    fw_wr16(&g_f, PAR_SEG_INDEX, 3);
    fw_wr16(&g_f, PROS_PHONEME_PTR, PROS_SEG_PHONEME);
    fw_wr16(&g_f, PROS_DURATION_PTR, PROS_SEG_DURATION);
    fw_wr16(&g_f, PROS_PITCH_PTR, PROS_SEG_PITCH);
    fw_wr16(&g_f, PROS_FLAGS_PTR, PROS_SEG_FLAGS);
    g_ram[PROS_SEG_PHONEME + 4] = T6_NEXT;
    g_ram[PROS_SEG_PITCH + 4] = 0x70;
    g_ram[PROS_SEG_DURATION + 3] = 9;
    g_ram[PROS_SEG_DURATION + 4] = 0xEE;              /* the duration at the index itself, not read */
    for (unsigned y = 0; y <= 10; y++) {
        g_ram[PAR_BOUNDARY + y] = (uint8_t)(0x10 + y);
        g_ram[PAR_NEXT_START_DUR + y] = (uint8_t)(0x20 + y);
        g_ram[PAR_START_DUR + y] = g_ram[PAR_START_VAL + y] = g_ram[PAR_LINE_VAL + y] = 0x5A;
        g_ram[PAR_SEG_TARGET + y] = 0x5A;
    }
    fw_wr16(&g_f, PROS_SEG_COUNT, 0x0100);
}

/* par_read_segment: the index moves on (no wrap); the next phoneme and its scaled target; the duration at index - 1;
   the ten targets: P1 $85 >= $80 gets no @u ($BD85 BMI taken: with the offset $C0 it would be $45 -> $3F) and is
   clamped to 1; P2 2 < 3 gets none (2, not 7); P3 3 + 5 = 8; P4 $30 + $20 = $50 -> $3F; P5 $7F + 1 = $80 -> $3F
   (N of $80 - $40 clear); P6 $70 + $20 = $90 -> 1 (N of $90 - 1 set); P7 $20 + $FE = $1E (8 bits); P8 4 + $FC = 0
   -> 1; P9 $3F; P10 $10 clamped, then @B3: $10 / 4 = 4 into $76, + 4 / 2 = 6. The tracks start from the
   boundaries; pros_seg_count $0100 -> $00FF (the DEC of the high byte). A = par_boundary[1], X = Y = 0. */
static void test_read_segment(void)
{
    read_setup();
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_read_segment(&g_f, &r);
    CHECK_EQ(g_ram[PAR_CUR_RANK], 0x21);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], T6_CUR);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME);   /* no wrap: the pointers stay */
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS);
    CHECK_EQ(g_ram[PAR_NEXT_PHONEME], T6_NEXT);
    CHECK_EQ(g_ram[PROS_PLAY_PITCH], 0x55);                        /* the old target */
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], 0x70);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 9);
    static const uint8_t want[11] = { 9, 1, 2, 8, 0x3F, 0x3F, 1, 0x1E, 1, 0x3F, 6 };
    for (unsigned x = 0; x <= 10; x++)
        CHECK_EQ(g_ram[PAR_SEG_TARGET + x], want[x]);
    CHECK_EQ(g_ram[FW_TMP_76], 4);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), T6_TABLE);                 /* back on P1's target row */
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_START_DUR + y], 0x20 + y);
        CHECK_EQ(g_ram[PAR_START_VAL + y], 0x10 + y);
        CHECK_EQ(g_ram[PAR_LINE_VAL + y], 0x10 + y);
    }
    CHECK_EQ(g_ram[PAR_START_DUR], 0x5A);                          /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_START_VAL], 0x5A);
    CHECK_EQ(g_ram[PAR_LINE_VAL], 0x5A);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x00FF);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The 16-bit index wrap: $01FF -> $0200 moves the index's high byte and the four pointers on a page (from one page
   on to two). At index low byte 0, the duration is read at index - 1 through the DEC / INC of pros_duration_ptr+1:
   ($0800 - $100) + $FF = $07FF, not $08FF; the pointer is back at $0800 afterwards. */
static void test_read_segment_wrap(void)
{
    read_setup();
    fw_wr16(&g_f, PAR_SEG_INDEX, 0x01FF);
    fw_wr16(&g_f, PROS_PHONEME_PTR, PROS_SEG_PHONEME + 0x100);
    fw_wr16(&g_f, PROS_DURATION_PTR, PROS_SEG_DURATION + 0x100);
    fw_wr16(&g_f, PROS_PITCH_PTR, PROS_SEG_PITCH + 0x100);
    fw_wr16(&g_f, PROS_FLAGS_PTR, PROS_SEG_FLAGS + 0x100);
    g_ram[PROS_SEG_PHONEME + 0x200] = T6_NEXT;
    g_ram[PROS_SEG_PITCH + 0x200] = 0x70;
    g_ram[PROS_SEG_DURATION + 0x1FF] = 7;
    g_ram[PROS_SEG_DURATION + 0x2FF] = 0xEE;          /* what Y = $FF without the DEC would read */
    fw_wr16(&g_f, PROS_SEG_COUNT, 5);
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_read_segment(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 0x0200);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME + 0x200);
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION + 0x200);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH + 0x200);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS + 0x200);
    CHECK_EQ(g_ram[PAR_NEXT_PHONEME], T6_NEXT);
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], 0x70);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 7);
    CHECK_EQ(g_ram[PAR_SEG_TARGET], 7);
    CHECK_EQ(g_ram[PAR_SEG_TARGET + 10], 6);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 4);       /* no borrow: the high byte stays */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK(!g_f.fault);
}

/* par_next_segment on the jump path: par_row_ptr is set to par_params_ptr first; par_read_segment's X = 0 is the
   running index of par_boundary_jump's stores, so the next phoneme's ten raw targets (P1 $85: no clamp) land in
   par_boundary[1..10]; with par_trans_dur 0 each par_start_dur ($21..$2A, from par_next_start_dur) is cut to
   par_frames_left 9 and par_end_dur = 0; then par_plan_tracks: par_end_wait = 9 - 0, 9 - 9 = 0 is not negative,
   so par_steady = 0 and par_sequential = 1. Both ranks are >= 29 ($21 and $1E). Out: A = 1 (par_plan_tracks' last
   LDA #1), X = 10 (ten INX in par_store_boundary), Y = 0; par_row_ptr ten strides on. */
static void test_next_segment(void)
{
    read_setup();
    fw_wr16(&g_f, PAR_ROW_PTR, 0x1234);
    fw_wr16(&g_f, PAR_RANK_PTR, T6_RANKS);
    g_ram[T6_RANKS + T6_NEXT] = 0x1E;
    for (unsigned p = 0; p < 10; p++)
        g_ram[T6_TABLE + T6_NEXT + T6_STRIDE * p] = (uint8_t)(p == 0 ? 0x85 : 0x30 + p);
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_next_segment(&g_f, &r);
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 0x1E);
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_BOUNDARY + y], y == 1 ? 0x85 : 0x30 + y - 1);
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + y], 0);
        CHECK_EQ(g_ram[PAR_START_DUR + y], 9);
        CHECK_EQ(g_ram[PAR_END_DUR + y], 0);
        CHECK_EQ(g_ram[PAR_END_WAIT + y], 9);
        CHECK_EQ(g_ram[PAR_STEADY + y], 0);
        CHECK_EQ(g_ram[PAR_SEQUENTIAL + y], 1);
        CHECK_EQ(g_ram[PAR_END_VAL + y], g_ram[PAR_SEG_TARGET + y]);
    }
    CHECK_EQ(g_ram[PAR_BOUNDARY], 0x10);             /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 0);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), T6_TABLE + 10 * T6_STRIDE);
    CHECK_EQ(r.a, 1);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* par_plan_tracks with par_frames_left = 200: for each parameter, par_end_dur and par_start_dur, and what follows
   from $C0DF-$C0F8 (SEC / SBC, BPL and BMI on bit 7 of the 8-bit difference). */
static void test_plan_tracks(void)
{
    static const struct { uint8_t end_dur, start_dur, end_wait, steady, sequential; } k[11] = {
        { 0 },
        { 5, 1, 0, 0x5A, 0 },          /* 195 = $C3 is negative: 0; 0 - 1 = $FF: overlap, par_steady stale */
        { 0xC8, 5, 0, 0x5A, 0 },       /* 0; 0 - 5 = $FB: overlap */
        { 0x49, 0xFF, 0x7F, 0x5A, 0 }, /* $7F; $7F - $FF = $80 (bit 7): overlap */
        { 0xC0, 3, 8, 5, 1 },
        { 0xD0, 0, 0, 0, 1 },          /* 200 - 208 = $F8: 0 */
        { 0x50, 0x78, 0x78, 0, 1 },
        { 0x48, 0, 0, 0, 1 },          /* 200 - 72 = 128 = $80, a difference >= $80: 0 */
        { 0xC7, 0, 1, 1, 1 },
        { 0x4A, 0x7E, 0x7E, 0, 1 },
        { 0x4A, 0, 0x7E, 0x7E, 1 },
    };
    setup();
    g_ram[PAR_FRAMES_LEFT] = 200;
    for (unsigned y = 0; y <= 10; y++) {
        g_ram[PAR_END_DUR + y] = k[y].end_dur;
        g_ram[PAR_START_DUR + y] = k[y].start_dur;
        g_ram[PAR_SEG_TARGET + y] = (uint8_t)(0x30 + y);
        g_ram[PAR_STEADY + y] = 0x5A;
        g_ram[PAR_SEQUENTIAL + y] = g_ram[PAR_END_VAL + y] = g_ram[PAR_END_WAIT + y] = 0xA5;
    }
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_plan_tracks(&g_f, &r);
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_END_VAL + y], 0x30 + y);
        CHECK_EQ(g_ram[PAR_END_WAIT + y], k[y].end_wait);
        CHECK_EQ(g_ram[PAR_STEADY + y], k[y].steady);
        CHECK_EQ(g_ram[PAR_SEQUENTIAL + y], k[y].sequential);
    }
    CHECK_EQ(g_ram[PAR_SEQUENTIAL], 0xA5);           /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_END_VAL], 0xA5);
    CHECK_EQ(g_ram[PAR_END_WAIT], 0xA5);
    CHECK_EQ(g_ram[PAR_STEADY], 0x5A);
    CHECK_EQ(r.a, 0xFF);                             /* P1's overlap difference, the last one computed */
    CHECK_EQ(r.x, 0x34);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* par_reset_tracks: frm_snap[i] = frm_rest_frame[frm_snapshot_map[i]], the map being 10 8 6 4 2 0 1 3 5 7 (b10, AHF,
   A3, A2, A1, ALF, FN, F1, F2, F3); every entry 0 and the byte before frm_snap (isr_frame_b's last) untouched;
   A, X and Y kept. */
static void test_reset_tracks(void)
{
    static const uint8_t map[10] = { 10, 8, 6, 4, 2, 0, 1, 3, 5, 7 };
    setup();
    fw_wr16(&g_f, PAR_SEG_INDEX, 0x1234);
    g_ram[PAR_FRAMES_LEFT] = 0x77;
    memset(g_ram + FRM_SNAP - 1, 0x5A, 12);
    for (unsigned i = 0; i <= 10; i++)
        g_ram[PAR_NEXT_START_DUR + i] = g_ram[PAR_BOUNDARY + i] = 0x5A;
    for (unsigned i = 0; i < 20; i++)
        g_ram[FRM_REST_FRAME + i] = (uint8_t)(0x40 + i);
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_reset_tracks(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 0);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 1);
    for (unsigned i = 0; i < 10; i++)
        CHECK_EQ(g_ram[FRM_SNAP + i], 0x40 + map[i]);
    CHECK_EQ(g_ram[FRM_SNAP - 1], 0x5A);
    CHECK_EQ(g_ram[FRM_SNAP + 10], 0x5A);
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + y], 0);
        CHECK_EQ(g_ram[PAR_BOUNDARY + y], y <= 4 ? 0x20 : 1);
    }
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR], 0x5A);
    CHECK_EQ(g_ram[PAR_BOUNDARY], 0x5A);
    CHECK_EQ(r.a, 0x12);
    CHECK_EQ(r.x, 0x34);
    CHECK_EQ(r.y, 0x56);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* par_play_start on voice A: the pause phoneme $3B first, whose rank (par_voice_a's rank row, $D46A + $3B) is 31;
   pitch[0] $70 scaled by pros_scale_target (identity here), no microprosody; stale pointers reset; the first event's
   position, or $FFFF without events. Out: A = 0, X = the entry X, Y = the phoneme. */
static void test_play_start(void)
{
    for (int events = 0; events <= 1; events++) {
        setup();
        pitch_identity();
        fw_wr16(&g_f, PAR_RANK_PTR, PAR_VOICE_A);
        memset(g_ram + PROS_PHONEME_PTR, 0x5A, 8);
        g_ram[PROS_SEG_PHONEME] = 0x3B;
        g_ram[PROS_SEG_PITCH] = 0x70;
        g_ram[PROS_PLAY_PITCH] = 0x5A;
        g_ram[CMD_EV_COUNT] = (uint8_t)(events ? 2 : 0);
        fw_wr16(&g_f, CMD_EV_POSITIONS, 0x1234);
        g_ram[CMD_EV_NEXT] = 7;
        g_ram[PROS_PITCH_ERR] = 0x33;
        fw_regs r = { 0x12, 0x34, 0x56, 0 };
        par_play_start(&g_f, &r);
        CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME);
        CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION);
        CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH);
        CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS);
        CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 1);                    /* par_reset_tracks ran */
        CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 0x20);
        CHECK_EQ(g_ram[PROS_PLAY_TARGET], 0x70);
        CHECK_EQ(g_ram[PROS_PLAY_PITCH], 0x5A);                 /* stale until the first par_read_segment */
        CHECK_EQ(g_ram[PAR_NEXT_PHONEME], 0x3B);
        CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);
        CHECK_EQ(g_ram[PAR_NEXT_RANK], 31);
        CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
        CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), events ? 0x1234 : 0xFFFF);
        CHECK_EQ(g_ram[CMD_EV_NEXT], 0);
        CHECK_EQ(g_ram[PROS_PITCH_ERR], 0);
        CHECK_EQ(r.a, 0);
        CHECK_EQ(r.x, 0x34);
        CHECK_EQ(r.y, 0x3B);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

/* Record i's frame buffer pointers: isr_play_ptr, isr_fill_ptr and isr_swap_phase as the C set them (LDM) before
   the call. */
static void check_buffers(unsigned i, uint16_t play, uint16_t fill, uint8_t phase)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].play, play);
    CHECK_EQ(g_calls[i].fill, fill);
    CHECK_EQ(g_calls[i].phase, phase);
}

/* par_setup_voice on a table at `table` with n phonemes: the pointers and the stride, then the five ROM calls in
   order with the registers the ROM has at each JSR and the buffers set before each; out, the last call's A and Y and
   frm_build_rest_frame's X. The entry A and X are not read; the entry Y reaches frm_build_rest_frame. */
static void setup_voice_case(uint16_t table, uint8_t n)
{
    setup();
    g_ram[PAR_COUNT] = n;
    fw_wr16(&g_f, PAR_TABLE_PTR, table);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 0x5A5A);
    g_ram[FRM_REST_FRAME] = 0x77;                     /* frm_rest_frame[0]: frm_copy_rest_frame's exit A */
    fw_regs r = { 0x90, 0x31, 0x42, 0 };
    par_setup_voice(&g_f, &r);
    uint16_t params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_COPY), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW2_PTR), table + n);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), params);     /* left on P1's target row, as frm_build_rest_frame needs */
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
    CHECK_EQ(g_ncalls, 5);
    check_call(0, ROM_FRM_BUILD_REST_FRAME, (uint8_t)(stride >> 8), 0, 0x42);   /* A = the stride's high byte
                                                                                   ($B3F2), X = 0 (the shift loop) */
    check_buffers(0, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(1, ROM_FRM_COPY_REST_FRAME, (uint8_t)(params >> 8), 9, 0x3B);    /* frm_build_rest_frame's exit */
    check_buffers(1, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(2, ROM_FRM_CLAMP_MARK_SPACE, 0x77, 9, 0xFF);                     /* frm_copy_rest_frame's exit */
    check_buffers(2, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(3, ROM_FRM_COPY_REST_FRAME, 0x0B, 9, 0x0B);                      /* frm_clamp_mark_space's exit */
    check_buffers(3, ISR_FRAME_B, ISR_FRAME_A, 1);
    check_call(4, ROM_FRM_CLAMP_MARK_SPACE, 0x77, 9, 0xFF);
    check_buffers(4, ISR_FRAME_B, ISR_FRAME_A, 1);
    CHECK_EQ(r.a, 0x0B);
    CHECK_EQ(r.x, 9);
    CHECK_EQ(r.y, 0x0B);
    CHECK(!g_f.fault);
}

/* Voice A (N = 86: stride 430 = $01AE, 4N = 344 sets the high byte through ROL) and a RAM table of 255 phonemes
   ($10F0: both par_next_row calls carry into the high byte; 4N = $3FC, + N carries: stride $04FB). */
static void test_setup_voice(void)
{
    setup_voice_case(PAR_VOICE_A, PAR_PHONEMES);
    setup_voice_case(0x10F0, 0xFF);
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
    test_boundary_rows();
    test_store_boundary();
    test_read_dominance();
    test_boundary_value();
    test_pass_dominance();
    test_jump_running_x();
    test_dominance_running_x();
    test_jump_end_marker();
    test_top_rows();
    test_read_segment();
    test_read_segment_wrap();
    test_next_segment();
    test_plan_tracks();
    test_reset_tracks();
    test_play_start();
    test_setup_voice();
}
