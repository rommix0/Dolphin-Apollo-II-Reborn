/* Unit checks of the playback set-up on the converted data (M4.4a spec 4.4), with a mock host that records every
   ROM call. They reach what no corpus text reaches (par_clamp at its edges, every @B with v = 0, 1, 8, the
   R = 0 path of pros_scale_target below the limit, a microprosody step that wraps). Each expected value is worked
   out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. The full proof is the
   live differential test (diff_lts) and the hybrid goldens (golden_hybrid); the thunks' register outputs are
   compared there, against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/cmd_addr.h"
#include "firmware/cmd_play_event.h"
#include "firmware/fw_ops.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_play.h"
#include "firmware/par_segment.h"
#include "firmware/par_start.h"
#include "firmware/par_track.h"
#include "firmware/pros_play.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

/* One ROM call: its address, registers and budget, and the frame buffer pointers as the C left them at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint16_t play, fill;
    uint8_t phase;
    uint8_t frames;   /* par_frames_left at the call */
} rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static uint8_t g_before[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p3;             /* the port P3 the mock isr_set_volume reads and writes (an SFR: not in g_ram) */
static unsigned g_ready_calls;   /* isr_par_ready calls so far */
static unsigned g_flag_every;    /* the mock timer interrupt sets isr_frame_flag at every n-th of them (0: never) */
static unsigned g_abort_at;      /* the mock receive interrupt sets ser_abort bit 0 at this one (0: never) */
static uint8_t g_uart_status;    /* UART_STATUS as the mock reads it */

/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) and isr_set_volume (M4.6, called by
   cmd_ev_set_volume) return the registers the ROM ones leave; so do the interrupt, serial and frame routines that
   par_play and boot_loop_play call (M4.5, M4.6), with what the timer and receive interrupts would do between the
   frame loop's tests (g_flag_every, g_abort_at); any other call fails. */
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
        c->frames = g_ram[PAR_FRAMES_LEFT];
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
    case ROM_ISR_VOLUME_HALF:        /* $B46C-$B472: LDA cmd_volume / LSR / JSR isr_set_volume: on into its case */
        r->a = (uint8_t)(g_ram[CMD_VOLUME] >> 1);
        /* fall through */
    case ROM_ISR_SET_VOLUME:         /* $ACE8-$ACF9: EOR #$F / AND #$F / JSR util_shl3 / STA $75 / LDA #$87 / AND P3 /
                                        ORA $75 / STA P3: A = the new P3; X and Y untouched ($75, the ROM's scratch,
                                        is not modelled) */
        g_p3 = (uint8_t)((g_p3 & 0x87) | util_shl3((uint8_t)((r->a ^ 0x0F) & 0x0F)));
        r->a = g_p3;
        return FW_CALL_RETURNED;
    case ROM_ISR_PAR_READY:          /* $C5D6-$C5D9, $C5EA-$C5EB: PHA, BBC0 boot_dip (serial mode), PLA: A, X, Y kept.
                                        Meanwhile the timer interrupt swaps the buffers and sets the flag ($B306
                                        LDM #1), and the receive interrupt sets ser_abort bit 0 (a mute) */
        g_ready_calls++;
        if (g_flag_every && g_ready_calls % g_flag_every == 0)
            g_ram[ISR_FRAME_FLAG] = 1;
        if (g_ready_calls == g_abort_at)
            g_ram[SER_ABORT] |= 1;
        return FW_CALL_RETURNED;
    case ROM_ISR_PAR_HOLD:           /* $C5DE-$C5DF, $C5EA-$C5EB: PHA, BBC0 boot_dip, PLA: A, X, Y kept */
    case ROM_SER_TX_SERVICE:         /* $C581-$C585, $C5A2-$C5A3: PHA, BBS0 boot_dip, BBC2 P0 (the host line low, as
                                        in the machine), PLA: nothing sent, A, X, Y kept */
    case ROM_FRM_FINISH_FRAME:       /* $B9B5-$B9B9 PHA / TXA / PHA / TYA / PHA ... $BA39-$BA3E PLA / TAY / PLA / TAX /
                                        PLA: A, X, Y restored */
    case ROM_ISR_SPEECH_STOP:        /* $B398-$B3A3: LDM, LDM, isr_dsp_reset (PHP ... PLP), CLB0: A, X, Y kept */
        return FW_CALL_RETURNED;
    case ROM_ISR_WAIT_FRAME:         /* $B465-$B46B: BBC0 isr_frame_flag (the frame is due), LDM #0: A, X, Y kept */
        g_ram[ISR_FRAME_FLAG] = 0;
        return FW_CALL_RETURNED;
    case ROM_ISR_SPEECH_START:       /* $B323-$B355: LDA #0 at $B344 (isr_dsp_reset, isr_timer_start and
                                        isr_wait_frames keep A), LDX #1 / isr_wait_frames' DEX: A = X = 0, Y kept; its
                                        isr_wait_frame clears the flag */
        r->a = 0;
        r->x = 0;
        g_ram[ISR_FRAME_FLAG] = 0;
        return FW_CALL_RETURNED;
    case ROM_FRM_SNAPSHOT:           /* $B917-$B92F: LDY #$0A / DEY loop, then LDX #$0A: LDY $B772,X / LDA ($2C),Y /
                                        DEX: the last at X = 1, Y = frm_snapshot_map[0] = 10: A = the fill frame's b10,
                                        X = 0, Y = 10 */
        r->y = FRAME_PITCH;
        r->a = fw_ind_y(&g_f, ISR_FILL_PTR, r->y);
        r->x = 0;
        return FW_CALL_RETURNED;
    case ROM_FRM_TRANSITION_FLAGS:   /* $BAE4-$BAEB: LDA frm_flags / LDY #$0D / STA ($2C),Y / RTS; X not used. The
                                        flags themselves are not modelled: A = frm_flags as it is */
        r->a = g_ram[FRM_FLAGS];
        r->y = 0x0D;
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}

/* UART_STATUS ($E7) as g_uart_status says; any other SFR reads 0. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    return addr == 0xE7 ? g_uart_status : 0;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
    g_host.read_sfr = mock_read_sfr;
    g_ncalls = 0;
    g_ready_calls = 0;
    g_flag_every = 0;
    g_abort_at = 0;
    g_uart_status = 0x02;           /* the transmitter ready, nothing received (as the machine has it) */
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

/* ---- the tracks (M4.4b Task 2) ---- */

static void test_track_rows(void)
{
    check_row("par_step_tracks", ROM_PAR_STEP_TRACKS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_sequential", ROM_PAR_TRACK_SEQUENTIAL, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("util_interp_step", ROM_UTIL_INTERP_STEP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_overlap", ROM_PAR_TRACK_OVERLAP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_start", ROM_PAR_TRACK_START, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_end", ROM_PAR_TRACK_END, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* One util_interp_step call: A = a, Y = y, X = $5A, util_interp_cur = cur, util_interp_steps = steps,
   util_interp_target = $77 (so a return of it shows), util_interp_quot = $EE. Out: A, Y, and util_interp_steps and
   util_interp_quot as the ROM leaves them ($EE: not written); X untouched, nothing else written, no fault. */
static void interp_case(uint8_t a, uint8_t y, uint8_t cur, uint8_t steps, uint8_t want_a, uint8_t want_y,
                        uint8_t want_steps, uint8_t want_quot)
{
    static const uint16_t w[] = { UTIL_INTERP_STEPS, UTIL_INTERP_QUOT };
    setup();
    g_ram[UTIL_INTERP_CUR] = cur;
    g_ram[UTIL_INTERP_STEPS] = steps;
    g_ram[UTIL_INTERP_TARGET] = 0x77;
    g_ram[UTIL_INTERP_QUOT] = 0xEE;
    snapshot();
    fw_regs r = { a, 0x5A, y, 0 };
    util_interp_step(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, want_y);
    CHECK_EQ(g_ram[UTIL_INTERP_STEPS], want_steps);
    CHECK_EQ(g_ram[UTIL_INTERP_QUOT], want_quot);
    check_written(w, 2);
    check_no_calls();
}

static void test_interp_step(void)
{
    /* Y = 1: DEY; BNE not taken; RTS: A unchanged, Y = 0, nothing written */
    interp_case(0x37, 1, 0x10, 1, 0x37, 0, 1, 0xEE);
    /* Y = 2: CLC; ADC cur; ROR A. $F0 + $90 = $180: the carry comes back as bit 7: $C0. $20 + $11 = $31: $18 */
    interp_case(0xF0, 2, 0x90, 2, 0xC0, 0, 2, 0xEE);
    interp_case(0x20, 2, 0x11, 2, 0x18, 0, 2, 0xEE);
    /* equal values: SEC; SBC gives 0 ($C1B2 BNE not taken): A = util_interp_target, Y = Y - 2 */
    interp_case(0x40, 5, 0x40, 5, 0x77, 3, 5, 0xEE);
    /* equal values with Y = 0 (and util_interp_steps 0): Y = $FE, no fault */
    interp_case(0x40, 0, 0x40, 0, 0x77, 0xFE, 0, 0xEE);
    /* up, d = 16, steps 5: SBC leaves 11, 6, 1, then $FC with the borrow, Y = 4. LSR: steps = 2 in RAM. $FC + 2 =
       $FE has bit 7 ($C1CA BPL not taken): DEY, q = 3 (16 / 5 = 3.2). A = $10 + 3 */
    interp_case(0x20, 5, 0x10, 5, 0x13, 3, 2, 3);
    /* up, d = 18: 13, 8, 3, $FE with the borrow, Y = 4; $FE + 2 = 0, bit 7 clear (BPL taken): q = 4 (3.6) */
    interp_case(0x22, 5, 0x10, 5, 0x14, 4, 2, 4);
    /* up, d = 6, steps 4 (even): 2, then $FE, Y = 2; half 2: $FE + 2 = 0: q = 2 (1.5 rounds up) */
    interp_case(0x16, 4, 0x10, 4, 0x12, 2, 2, 2);
    /* down, d = -16 ($F0), steps 5: ADC leaves $F5, $FA, $FF, then 4 with the carry, Y = $FC. Not 0 ($C1E1 BEQ not
       taken): LSR: steps 2 in RAM; 4 - 2 = 2 is above 0 ($C1EA BMI, $C1EC BEQ not taken): INY ($C1EE), q = $FD (-3,
       -3.2). A = $40 + $FD = $3D */
    interp_case(0x30, 5, 0x40, 5, 0x3D, 0xFD, 2, 0xFD);
    /* down, d = -15 ($F1): $F6, $FB, then 0 with the carry, Y = $FD: $C1E1 BEQ taken: q = $FD (-3 exactly), and
       util_interp_steps is NOT halved (it stays 5) */
    interp_case(0x31, 5, 0x40, 5, 0x3D, 0xFD, 5, 0xFD);
    /* down, d = -18 ($EE): $F3, $F8, $FD, then 2, Y = $FC; 2 - 2 = 0: $C1EC BEQ taken: q = $FC (-3.6 -> -4) */
    interp_case(0x2E, 5, 0x40, 5, 0x3C, 0xFC, 2, 0xFC);
    /* down, d = -19 ($ED): $F2, $F7, $FC, then 1, Y = $FC; 1 - 2 = $FF: $C1EA BMI taken: q = $FC (-3.8 -> -4) */
    interp_case(0x2D, 5, 0x40, 5, 0x3C, 0xFC, 2, 0xFC);
    /* a long way down, and the 8-bit add of the quotient wraps: cur $F0, A = $10: SBC gives $20 with the borrow (the
       down path, d = -224). 75 ADCs of 3 carry ($20 + 225 = $101): A = 1, Y = -75 = $B5. LSR: steps 1; 1 - 1 = 0
       ($C1EC BEQ taken): q = $B5 (-74.67 -> -75). A = $F0 + $B5 = $A5 (8 bits) */
    interp_case(0x10, 3, 0xF0, 3, 0xA5, 0xB5, 1, 0xB5);
}
/* util_interp_step with 0 steps and different values: the ROM loops forever ($C1BC SBC #0 with the carry set, or
   $C1DA ADC #0 with it clear); the C faults at ROM_UTIL_INTERP_STEP and writes nothing. */
static void test_interp_zero_steps(void)
{
    static const uint8_t a[2] = { 0x30, 0x10 };    /* up (no borrow) and down (a borrow) from cur $20 */
    for (unsigned i = 0; i < 2; i++) {
        setup();
        g_ram[UTIL_INTERP_CUR] = 0x20;
        g_ram[UTIL_INTERP_TARGET] = a[i];
        g_ram[UTIL_INTERP_QUOT] = 0xEE;
        snapshot();
        fw_regs r = { a[i], 0x5A, 0, 0 };
        util_interp_step(&g_f, &r);
        CHECK(g_f.fault);
        CHECK_EQ(g_f.fault_addr, ROM_UTIL_INTERP_STEP);
        CHECK_EQ(g_ncalls, 0);
        check_written(NULL, 0);
    }
}

/* The interpolation state a track step leaves: util_interp_steps, util_interp_cur, util_interp_target. */
static void check_interp_state(uint8_t steps, uint8_t cur, uint8_t target)
{
    CHECK_EQ(g_ram[UTIL_INTERP_STEPS], steps);
    CHECK_EQ(g_ram[UTIL_INTERP_CUR], cur);
    CHECK_EQ(g_ram[UTIL_INTERP_TARGET], target);
}

static void test_track_start(void)
{
    const uint8_t x = 4;
    static const uint16_t w[] = {
        UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, UTIL_INTERP_QUOT, PAR_START_DUR + 4, PAR_START_VAL + 4,
    };
    /* par_start_dur[X] = 0 ($C24D BNE not taken): A = par_seg_target[X], Y = 0 (the LDY), nothing written */
    setup();
    g_ram[PAR_SEG_TARGET + x] = 0x2B;
    g_ram[PAR_START_VAL + x] = 0x10;
    snapshot();
    fw_regs r = { 0x11, x, 0x33, 0 };
    par_track_start(&g_f, &r);
    CHECK_EQ(r.a, 0x2B);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, 0);
    check_written(NULL, 0);
    check_no_calls();
    /* par_start_dur[X] = 3: util_interp_steps = 3, par_start_dur = 2, cur = par_start_val $10, target =
       par_seg_target $20; d = 16: 13, 10, 7, 4, 1, then $FE with the borrow (Y = 6); LSR: steps 1; $FE + 1 = $FF has
       bit 7: DEY, q = 5 (16 / 3 = 5.33). par_start_val = A = $15; Y = 5 */
    setup();
    g_ram[PAR_SEG_TARGET + x] = 0x20;
    g_ram[PAR_START_VAL + x] = 0x10;
    g_ram[PAR_START_DUR + x] = 3;
    snapshot();
    r = (fw_regs){ 0x11, x, 0x33, 0 };
    par_track_start(&g_f, &r);
    CHECK_EQ(r.a, 0x15);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, 5);
    CHECK_EQ(g_ram[PAR_START_VAL + x], 0x15);
    CHECK_EQ(g_ram[PAR_START_DUR + x], 2);
    CHECK_EQ(g_ram[UTIL_INTERP_QUOT], 5);
    check_interp_state(1, 0x10, 0x20);
    check_written(w, 6);
    check_no_calls();
}

/* One par_track_end run with X = 7: par_end_wait, par_end_dur, par_end_val and par_boundary in, par_seg_target $2B,
   util_interp_quot $EE. Out: A, Y, and the three track bytes; no case reaches the quotient. */
static void track_end_case(uint8_t wait, uint8_t dur, uint8_t val, uint8_t boundary, uint8_t want_a, uint8_t want_y,
                           uint8_t want_wait, uint8_t want_dur, uint8_t want_val)
{
    const uint8_t x = 7;
    static const uint16_t w[] = {
        UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_END_WAIT + 7, PAR_END_DUR + 7, PAR_END_VAL + 7,
    };
    setup();
    g_ram[PAR_END_WAIT + x] = wait;
    g_ram[PAR_END_DUR + x] = dur;
    g_ram[PAR_END_VAL + x] = val;
    g_ram[PAR_BOUNDARY + x] = boundary;
    g_ram[PAR_SEG_TARGET + x] = 0x2B;
    g_ram[UTIL_INTERP_QUOT] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, x, 0x33, 0 };
    par_track_end(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, want_y);
    CHECK_EQ(g_ram[PAR_END_WAIT + x], want_wait);
    CHECK_EQ(g_ram[PAR_END_DUR + x], want_dur);
    CHECK_EQ(g_ram[PAR_END_VAL + x], want_val);
    check_written(w, 6);
    check_no_calls();
}

static void test_track_end(void)
{
    /* waiting: par_end_wait 2 ($C277 BNE taken): DEC to 1; A = par_seg_target, Y = 2 (the LDY); no step */
    track_end_case(2, 5, 0x10, 0x30, 0x2B, 2, 1, 5, 0x10);
    check_interp_state(0, 0, 0);
    /* the wait over (0): the step: steps = par_end_dur 2 (DEC to 1), cur $10, target $31: (CLC; ADC; ROR) $41 / 2 =
       $20 into par_end_val; Y = 0 */
    track_end_case(0, 2, 0x10, 0x31, 0x20, 0, 0, 1, 0x20);
    check_interp_state(2, 0x10, 0x31);
    /* a negative par_end_wait ($80; $C26F BPL not taken, unreachable in the ROM): LDY #0; TYA; STA zeroes it, and the
       Z of the TYA takes the step: par_end_dur 1 (DEC to 0): Y = 1 returns the target $30 */
    track_end_case(0x80, 1, 0x10, 0x30, 0x30, 0, 0, 0, 0x30);
    check_interp_state(1, 0x10, 0x30);
    /* par_end_dur 0 wraps (DEC to $FF); the values equal: Y = 0 - 2 = $FE, A = util_interp_target, no fault */
    track_end_case(0, 0, 0x30, 0x30, 0x30, 0xFE, 0, 0xFF, 0x30);
    check_interp_state(0, 0x30, 0x30);
}

/* The fill frame of the frame-byte tests: isr_fill_ptr = isr_frame_b. */
static void fill_frame_b(void) { fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B); }

static void test_track_sequential(void)
{
    /* the start phase, X = 2 (F1: par_slot_map[1] = b3): par_start_dur 2 (DEC to 1): par_start_val $10 to
       par_seg_target $31: (CLC; ADC; ROR) $41 / 2 = $20 into par_start_val and b3; Y = 3 (LDY $B796,X) */
    {
        static const uint16_t w[] = {
            UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_START_DUR + 2, PAR_START_VAL + 2,
            ISR_FRAME_B + FRAME_F1,
        };
        setup();
        fill_frame_b();
        g_ram[PAR_START_DUR + 2] = 2;
        g_ram[PAR_START_VAL + 2] = 0x10;
        g_ram[PAR_SEG_TARGET + 2] = 0x31;
        g_ram[PAR_STEADY + 2] = 9;                  /* not reached */
        snapshot();
        fw_regs r = { 0x11, 2, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x20);
        CHECK_EQ(r.x, 2);
        CHECK_EQ(r.y, FRAME_F1);
        CHECK_EQ(g_ram[PAR_START_DUR + 2], 1);
        CHECK_EQ(g_ram[PAR_START_VAL + 2], 0x20);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F1], 0x20);
        check_interp_state(2, 0x10, 0x31);
        check_written(w, 6);
        check_no_calls();
    }
    /* the hold, X = 5 (ALF: b0): par_start_dur 0, par_steady 4 ($C172 BEQ not taken): DEC to 3; b0 = par_seg_target;
       A = par_seg_target, Y = 0; util_interp_* untouched */
    {
        static const uint16_t w[] = { PAR_STEADY + 5, ISR_FRAME_B + FRAME_ALF };
        setup();
        fill_frame_b();
        g_ram[PAR_STEADY + 5] = 4;
        g_ram[PAR_SEG_TARGET + 5] = 0x27;
        g_ram[ISR_FRAME_B + FRAME_ALF] = 0xEE;
        snapshot();
        fw_regs r = { 0x11, 5, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x27);
        CHECK_EQ(r.x, 5);
        CHECK_EQ(r.y, FRAME_ALF);
        CHECK_EQ(g_ram[PAR_STEADY + 5], 3);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_ALF], 0x27);
        check_written(w, 2);
        check_no_calls();
    }
    /* the end phase, X = 10 (voicing: b9): par_start_dur 0, par_steady 0: steps = par_end_dur 3 (DEC to 2), cur =
       par_seg_target $30 itself, target = par_boundary $24: d = -12 ($F4): $F7, $FA, $FD, then 0 with the carry
       (Y = $FC): $C1E1 BEQ: q = -4 exactly, util_interp_steps not halved (3). A = $2C into par_seg_target and b9 */
    {
        static const uint16_t w[] = {
            UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, UTIL_INTERP_QUOT, PAR_END_DUR + 10,
            PAR_SEG_TARGET + 10, ISR_FRAME_B + FRAME_V,
        };
        setup();
        fill_frame_b();
        g_ram[PAR_END_DUR + 10] = 3;
        g_ram[PAR_SEG_TARGET + 10] = 0x30;
        g_ram[PAR_BOUNDARY + 10] = 0x24;
        snapshot();
        fw_regs r = { 0x11, 10, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x2C);
        CHECK_EQ(r.x, 10);
        CHECK_EQ(r.y, FRAME_V);
        CHECK_EQ(g_ram[PAR_END_DUR + 10], 2);
        CHECK_EQ(g_ram[PAR_SEG_TARGET + 10], 0x2C);
        CHECK_EQ(g_ram[UTIL_INTERP_QUOT], 0xFC);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_V], 0x2C);
        check_interp_state(3, 0x30, 0x24);
        check_written(w, 7);
        check_no_calls();
    }
    /* the end phase with par_end_dur 0, X = 1 (FN: b1): the DEC wraps it to $FF; Y = 0 with par_seg_target =
       par_boundary ($22): the equal return (util_interp_target), no fault */
    {
        static const uint16_t w[] = {
            UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_END_DUR + 1, PAR_SEG_TARGET + 1,
            ISR_FRAME_B + FRAME_FN,
        };
        setup();
        fill_frame_b();
        g_ram[PAR_SEG_TARGET + 1] = 0x22;
        g_ram[PAR_BOUNDARY + 1] = 0x22;
        g_ram[UTIL_INTERP_STEPS] = 0xEE;
        snapshot();
        fw_regs r = { 0x11, 1, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x22);
        CHECK_EQ(r.x, 1);
        CHECK_EQ(r.y, FRAME_FN);
        CHECK_EQ(g_ram[PAR_END_DUR + 1], 0xFF);
        CHECK_EQ(g_ram[PAR_SEG_TARGET + 1], 0x22);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_FN], 0x22);
        check_interp_state(0, 0x22, 0x22);
        check_written(w, 6);
        check_no_calls();
    }
}

/* One par_track_overlap case, X = 3 (F2: b5), the fill frame isr_frame_b, par_end_dur 1 and par_end_val 1. The start
   track: par_seg_target t when sd = 0, else (sd = 2) the average of sv and t. The end track: par_boundary b when
   ew = 0 (par_end_dur 1: Y = 1 returns the target), t when ew = 1 (the wait). The line: lv to b over fl frames (1:
   b; 2: the average). s, e, l: the three values; want: the median the ROM writes. */
typedef struct {
    uint8_t sd, sv, t, ew, b, lv, fl;
    uint8_t s, e, l, want;
} overlap_row;

static void overlap_case(const overlap_row *k)
{
    const uint8_t x = 3;
    static const uint16_t w[] = {
        UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_START_DUR + 3, PAR_START_VAL + 3, PAR_END_WAIT + 3,
        PAR_END_DUR + 3, PAR_END_VAL + 3, PAR_LINE_VAL + 3, PAR_START_TRACK, PAR_END_TRACK, ISR_FRAME_B + FRAME_F2,
    };
    setup();
    fill_frame_b();
    g_ram[PAR_START_DUR + x] = k->sd;
    g_ram[PAR_START_VAL + x] = k->sv;
    g_ram[PAR_SEG_TARGET + x] = k->t;
    g_ram[PAR_END_WAIT + x] = k->ew;
    g_ram[PAR_END_DUR + x] = 1;
    g_ram[PAR_END_VAL + x] = 1;
    g_ram[PAR_BOUNDARY + x] = k->b;
    g_ram[PAR_LINE_VAL + x] = k->lv;
    g_ram[PAR_FRAMES_LEFT] = k->fl;
    g_ram[UTIL_INTERP_QUOT] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, x, 0x33, 0 };
    par_track_overlap(&g_f, &r);
    CHECK_EQ(g_ram[PAR_START_TRACK], k->s);
    CHECK_EQ(g_ram[PAR_END_TRACK], k->e);
    CHECK_EQ(g_ram[PAR_LINE_VAL + x], k->l);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F2], k->want);
    CHECK_EQ(r.a, k->want);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, FRAME_F2);
    CHECK_EQ(g_ram[UTIL_INTERP_QUOT], 0xEE);
    check_interp_state(k->fl, k->lv, k->b);         /* the line's step comes last */
    check_written(w, 12);
    check_no_calls();
}

static void test_track_overlap(void)
{
    static const overlap_row k[] = {
        /* S = t, E = b, L = (lv + b) / 2 (fl = 2). The six outcomes, by CMP and BPL / BMI:
           L >= S, L < E ($C21C BPL taken, $C229 BPL not taken): the line */
        { 0, 0, 0x10, 0, 0x30, 0x10, 2, 0x10, 0x30, 0x20, 0x20 },
        /* L >= S, L >= E, S < E ($C229 BPL taken, $C245 BMI taken): the end */
        { 0, 0, 0x10, 0, 0x18, 0x28, 2, 0x10, 0x18, 0x20, 0x18 },
        /* L >= S, L >= E, S >= E ($C245 BMI not taken): the start */
        { 0, 0, 0x18, 0, 0x10, 0x30, 2, 0x18, 0x10, 0x20, 0x18 },
        /* L < S, L >= E ($C21C BPL not taken, $C221 BMI not taken): the line */
        { 0, 0, 0x20, 0, 0x10, 0x20, 2, 0x20, 0x10, 0x18, 0x18 },
        /* L < S, L < E, S >= E ($C221 BMI taken, $C234 BPL taken): the end */
        { 0, 0, 0x20, 0, 0x18, 0x08, 2, 0x20, 0x18, 0x10, 0x18 },
        /* L < S, L < E, S < E ($C234 BPL not taken): the start */
        { 0, 0, 0x18, 0, 0x20, 0x00, 2, 0x18, 0x20, 0x10, 0x18 },
        /* the sign of the 8-bit difference: L = $10, S = $90, E = $20. $10 - $90 = $80 is "minus" (L < S), $10 - $20 is
           minus, $90 - $20 = $70 is not: the end, $20. A true signed median is the line $10 (S = -112) */
        { 0, 0, 0x90, 0, 0x20, 0x00, 2, 0x90, 0x20, 0x10, 0x20 },
        /* L = $90 (fl = 1: b), S = $10 (sd = 2: (0 + $20) / 2), E = $20 (ew = 1: t): $90 - $10 = $80 is minus (L < S),
           $90 - $20 = $70 is not: the line, $90. A true signed median is S ($10; L = -112), an unsigned one E ($20) */
        { 2, 0, 0x20, 1, 0x90, 0x00, 1, 0x10, 0x20, 0x90, 0x90 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        overlap_case(&k[i]);
}

/* par_step_tracks: P10..P1, odd X sequential (a hold of par_seg_target), even X overlapping (start = end =
   par_seg_target, the line par_boundary $3F over 1 frame, so the median is par_seg_target): each writes
   par_seg_target[X] = $10 + X into frame byte par_slot_map[X - 1]. The pitch: par_frames_left 1: pros_play_pitch =
   pros_play_target $A2, b10 = $A2 >> 2 = $28. The fill frame is isr_frame_a; its bytes b11-b19 stay $EE. */
static void test_step_tracks(void)
{
    uint16_t w[64];
    size_t n = 0;
    setup();
    fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_A);
    memset(g_ram + ISR_FRAME_A, 0xEE, 20);
    for (uint8_t x = 1; x <= 10; x++) {
        g_ram[PAR_SEG_TARGET + x] = (uint8_t)(0x10 + x);
        g_ram[PAR_SEQUENTIAL + x] = x & 1;
        if (x & 1) {
            g_ram[PAR_STEADY + x] = 1;
            w[n++] = (uint16_t)(PAR_STEADY + x);
        } else {
            g_ram[PAR_END_WAIT + x] = 1;
            g_ram[PAR_BOUNDARY + x] = 0x3F;
            w[n++] = (uint16_t)(PAR_END_WAIT + x);
            w[n++] = (uint16_t)(PAR_LINE_VAL + x);
        }
    }
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_ram[PROS_PLAY_PITCH] = 0x80;
    g_ram[PROS_PLAY_TARGET] = 0xA2;
    for (uint16_t b = 0; b <= FRAME_PITCH; b++)
        w[n++] = (uint16_t)(ISR_FRAME_A + b);
    w[n++] = UTIL_INTERP_STEPS;
    w[n++] = UTIL_INTERP_CUR;
    w[n++] = UTIL_INTERP_TARGET;
    w[n++] = PAR_START_TRACK;
    w[n++] = PAR_END_TRACK;
    w[n++] = PROS_PLAY_PITCH;
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_step_tracks(&g_f, &r);
    for (uint8_t x = 1; x <= 10; x++) {
        CHECK_EQ(g_ram[ISR_FRAME_A + fw_par_slot_map[x - 1]], 0x10 + x);
        if (x & 1) {
            CHECK_EQ(g_ram[PAR_STEADY + x], 0);
        } else {
            CHECK_EQ(g_ram[PAR_END_WAIT + x], 0);
            CHECK_EQ(g_ram[PAR_LINE_VAL + x], 0x3F);
        }
    }
    /* the routing in full: b0 = P5 (ALF), b1 = P1 (FN), b2 = P6 (A1), b3 = P2 (F1), ..., b8 = P9 (AHF), b9 = P10 (V) */
    static const uint8_t frame[11] = { 0x15, 0x11, 0x16, 0x12, 0x17, 0x13, 0x18, 0x14, 0x19, 0x1A, 0x28 };
    for (unsigned b = 0; b < 11; b++)
        CHECK_EQ(g_ram[ISR_FRAME_A + b], frame[b]);
    for (unsigned b = 11; b < 20; b++)
        CHECK_EQ(g_ram[ISR_FRAME_A + b], 0xEE);
    CHECK_EQ(g_ram[PROS_PLAY_PITCH], 0xA2);
    CHECK_EQ(g_ram[PAR_START_TRACK], 0x12);         /* the last overlapping track is P2's */
    CHECK_EQ(g_ram[PAR_END_TRACK], 0x12);
    check_interp_state(1, 0x80, 0xA2);              /* the pitch's step comes last */
    CHECK_EQ(r.a, 0x28);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, FRAME_PITCH);
    check_written(w, n);
    check_no_calls();
}

/* ---- the command events (M4.4b Task 3) ---- */

static void test_event_rows(void)
{
    check_row("cmd_run_event", ROM_CMD_RUN_EVENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_index", ROM_CMD_EV_INDEX, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_sound_start", ROM_CMD_EV_SOUND_START, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_sound_end", ROM_CMD_EV_SOUND_END, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_set_voicing", ROM_CMD_EV_SET_VOICING, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_set_volume", ROM_CMD_EV_SET_VOLUME, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("util_shl4", ROM_UTIL_SHL4, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("util_shl3", ROM_UTIL_SHL3, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* cmd_ev_index with cmd_index_count = n: TST / BEQ $BBA6 (0: nothing written), else DEC. A, X and Y untouched. */
static void ev_index_case(uint8_t n, uint8_t want)
{
    static const uint16_t w[] = { CMD_INDEX_COUNT };
    setup();
    g_ram[CMD_INDEX_COUNT] = n;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xA5, 0 };
    cmd_ev_index(&g_f, &r);
    CHECK_EQ(g_ram[CMD_INDEX_COUNT], want);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    check_no_calls();
}

static void test_ev_index(void)
{
    ev_index_case(0, 0);         /* $BBA2 BEQ taken: only an 8-bit wrap of the count reaches it */
    ev_index_case(1, 0);
    ev_index_case(0xFF, 0xFE);
}

/* cmd_ev_set_voicing: STA cmd_voicing; RTS. */
static void test_set_voicing(void)
{
    static const uint16_t w[] = { CMD_VOICING };
    setup();
    g_ram[CMD_VOICING] = 2;
    snapshot();
    fw_regs r = { 9, 0x5A, 0xA5, 0 };
    cmd_ev_set_voicing(&g_f, &r);
    CHECK_EQ(g_ram[CMD_VOICING], 9);
    CHECK_EQ(r.a, 9);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    check_no_calls();
}

/* cmd_ev_set_volume with volume v and the P3 latch p3: STA cmd_volume; JSR isr_set_volume with the entry A, X and Y;
   out what it leaves: A = (p3 & $87) | ((v ^ $F) & $F) << 3 (the mock's model of $ACE8), X and Y untouched. */
static void set_volume_case(uint8_t v, uint8_t p3, uint8_t want_a)
{
    static const uint16_t w[] = { CMD_VOLUME };
    setup();
    g_p3 = p3;
    g_ram[CMD_VOLUME] = 0xEE;
    snapshot();
    fw_regs r = { v, 0x5A, 0xA5, 0 };
    cmd_ev_set_volume(&g_f, &r);
    CHECK_EQ(g_ram[CMD_VOLUME], v);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_SET_VOLUME, v, 0x5A, 0xA5);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    CHECK(!g_f.fault);
}

static void test_set_volume(void)
{
    set_volume_case(0x0A, 0xFF, 0xAF);   /* $0A ^ $F = 5, << 3 = $28; $FF & $87 = $87 */
    set_volume_case(0x00, 0x00, 0x78);   /* silent: $F << 3 */
    set_volume_case(0x0F, 0x3C, 0x04);   /* full: 0; $3C & $87 = $04 */
}

/* The playback state cmd_ev_sound_start saves, set to distinct values: the pointers $22-$29 = $10-$17, par_seg_index
   = $1234, pros_seg_count = $0156, cmd_voicing 5, pros_pitch_centre $66, cmd_source_prosody $27, pros_play_target
   $55, par_next_phoneme $3A, par_next_rank $0C; par_rank_ptr = voice A; the save area, the end event's position and
   cmd_ev_pos $EE; cmd_sound_active $80 (bit 7 is not the flag). */
static void sound_state(void)
{
    for (uint8_t i = 0; i < 8; i++)
        g_ram[PROS_PHONEME_PTR + i] = (uint8_t)(0x10 + i);
    fw_wr16(&g_f, PAR_RANK_PTR, PAR_VOICE_A);
    fw_wr16(&g_f, PAR_SEG_INDEX, 0x1234);
    fw_wr16(&g_f, PROS_SEG_COUNT, 0x0156);
    g_ram[CMD_VOICING] = 5;
    g_ram[PROS_PITCH_CENTRE] = 0x66;
    g_ram[CMD_SOURCE_PROSODY] = 0x27;
    g_ram[PROS_PLAY_TARGET] = 0x55;
    g_ram[PAR_NEXT_PHONEME] = 0x3A;
    g_ram[PAR_NEXT_RANK] = 0x0C;
    memset(g_ram + CMD_SOUND_SAVE, 0xEE, 8);
    memset(g_ram + CMD_SOUND_SEG_INDEX, 0xEE, 2);
    memset(g_ram + CMD_SOUND_STATE, 0xEE, 8);
    memset(g_ram + CMD_EV_POSITIONS, 0xEE, 256);
    fw_wr16(&g_f, CMD_EV_POS, 0xEEEE);
    g_ram[CMD_SOUND_ACTIVE] = 0x80;
}

/* cmd_ev_sound_start for effect e with cmd_ev_next = next, on sound_state(): the effect's four pointers from
   cmd_sound_table (phonemes, durations, pitches; the flags are shared, $9FFC), its first phoneme 59 and that
   phoneme's rank, and the end at index len. Then cmd_ev_sound_end restores all but the save area, the end event
   and cmd_ev_pos. */
static void sound_case(uint8_t e, uint8_t next, uint16_t phonemes, uint16_t durations, uint16_t pitches, uint8_t len)
{
    uint16_t at = (uint16_t)(CMD_EV_POSITIONS + (uint8_t)(next << 1));   /* LDX / TXA / ASL / TAX: 8 bits */
    const uint16_t w[] = {
        PROS_PHONEME_PTR, PROS_PHONEME_PTR + 1, PROS_DURATION_PTR, PROS_DURATION_PTR + 1, PROS_PITCH_PTR,
        PROS_PITCH_PTR + 1, PROS_FLAGS_PTR, PROS_FLAGS_PTR + 1,
        CMD_SOUND_SAVE, CMD_SOUND_SAVE + 1, CMD_SOUND_SAVE + 2, CMD_SOUND_SAVE + 3, CMD_SOUND_SAVE + 4,
        CMD_SOUND_SAVE + 5, CMD_SOUND_SAVE + 6, CMD_SOUND_SAVE + 7,
        CMD_SOUND_SEG_INDEX, CMD_SOUND_SEG_INDEX + 1,
        CMD_SOUND_STATE, CMD_SOUND_STATE + 1, CMD_SOUND_STATE + 2, CMD_SOUND_STATE + 3, CMD_SOUND_STATE + 4,
        CMD_SOUND_STATE + 5, CMD_SOUND_STATE + 6, CMD_SOUND_STATE + 7,
        PAR_NEXT_PHONEME, PAR_NEXT_RANK, PROS_PITCH_CENTRE, CMD_SOURCE_PROSODY, CMD_VOICING,
        PAR_SEG_INDEX, PAR_SEG_INDEX + 1, PROS_SEG_COUNT, PROS_SEG_COUNT + 1,
        at, (uint16_t)(at + 1), CMD_EV_POS, CMD_EV_POS + 1, CMD_SOUND_ACTIVE,
    };
    setup();
    sound_state();
    g_ram[CMD_EV_NEXT] = next;
    snapshot();
    fw_regs r = { e, 0x5A, 0xA5, 0 };
    cmd_ev_sound_start(&g_f, &r);
    for (uint8_t i = 0; i < 8; i++)                                    /* the old pointers saved, low byte first */
        CHECK_EQ(g_ram[CMD_SOUND_SAVE + i], 0x10 + i);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), phonemes);
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), durations);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), pitches);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), CMD_SOUND_DATA);       /* fw_cmd_sound_flags lands first, at $9FFC */
    CHECK_EQ(fw_rd16(&g_f, CMD_SOUND_SEG_INDEX), 0x1234);
    static const uint8_t state[8] = { 0x56, 0x01, 5, 0x66, 0x27, 0x55, 0x0C, 0x3A };
    for (unsigned i = 0; i < 8; i++)
        CHECK_EQ(g_ram[CMD_SOUND_STATE + i], state[i]);
    CHECK_EQ(g_ram[PAR_NEXT_PHONEME], 59);                         /* every effect starts with phoneme 59 */
    CHECK_EQ(g_ram[PAR_NEXT_RANK], fw_par_voice_a[59].rank);       /* (par_rank_ptr),59: voice A's rank row */
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], CMD_SOUND_PITCH_CENTRE);
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], CMD_SOUND_SOURCE_PROSODY);
    CHECK_EQ(g_ram[CMD_VOICING], CMD_SOUND_VOICING);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 0);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x00FF);               /* STY pros_seg_count with Y = $FF */
    CHECK_EQ(fw_rd16(&g_f, at), len);                              /* the end event's position: the length */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), len);
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x81);                       /* SEB0 */
    CHECK_EQ(r.a, 0);                                              /* LDA #0 at $BC75 */
    CHECK_EQ(r.x, (uint8_t)(next << 1));
    CHECK_EQ(r.y, len);                                            /* the scan's Y */
    check_written(w, sizeof w / sizeof w[0]);
    check_no_calls();

    fw_regs s = { 0x11, 0x22, 0x33, 0 };
    cmd_ev_sound_end(&g_f, &s);
    CHECK_EQ(s.a, 0x0C);                                           /* the last LDA, cmd_sound_state[6] */
    CHECK_EQ(s.x, 0x22);
    CHECK_EQ(s.y, 0x33);
    for (uint8_t i = 0; i < 8; i++) {                              /* what the end only reads stays as the start */
        g_before[CMD_SOUND_SAVE + i] = (uint8_t)(0x10 + i);       /* wrote it; the end event and cmd_ev_pos too */
        g_before[CMD_SOUND_STATE + i] = state[i];
    }
    g_before[CMD_SOUND_SEG_INDEX] = 0x34;
    g_before[CMD_SOUND_SEG_INDEX + 1] = 0x12;
    g_before[at] = len;
    g_before[at + 1] = 0;
    g_before[CMD_EV_POS] = len;
    g_before[CMD_EV_POS + 1] = 0;
    check_written(NULL, 0);                                        /* the rest restored, cmd_sound_active $80 (CLB0) */
    check_no_calls();
}

static void test_sound_start_end(void)
{
    sound_case(0, 3, 0xA00E, 0xA012, 0xA015, 3);    /* @T0: 59 77 59, $FF at index 3; the end event at $120C */
    sound_case(16, 3, 0xA183, 0xA185, 0xA186, 1);   /* @TG/@TX: 59, $FF at index 1 */
    sound_case(0, 0x80, 0xA00E, 0xA012, 0xA015, 3); /* cmd_ev_next = 128: ASL wraps, X = 0: positions[0] */
}

/* A = $15 (never queued: @T takes 0-16): util_shl3 gives X = $A8, past the 17 entries, and the bytes there (the @T
   arrays at $A01C) point the phonemes at $2805, RAM, all zero here. No end in 256 bytes: the ROM would loop
   forever; the C faults at ROM_CMD_EV_SOUND_START before the end event. */
static void test_sound_start_no_end(void)
{
    setup();
    sound_state();
    g_ram[CMD_EV_NEXT] = 3;
    fw_regs r = { 0x15, 0x5A, 0xA5, 0 };
    cmd_ev_sound_start(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), 0x2805);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, ROM_CMD_EV_SOUND_START);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xEEEE);                   /* the end event is not written */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 6), 0xEEEE);
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x80);
    CHECK_EQ(g_ncalls, 0);
}

/* cmd_ev_sound_end with cmd_sound_active bit 0 clear: BBS0 not taken, RTS: nothing written, A, X, Y untouched. */
static void test_sound_end_inactive(void)
{
    setup();
    sound_state();
    g_ram[CMD_SOUND_ACTIVE] = 0xFE;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xA5, 0 };
    cmd_ev_sound_end(&g_f, &r);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(NULL, 0);
    check_no_calls();
}

/* cmd_run_event with cmd_ev_next = next: the handler word at $1306 + (next << 1) and the next position at $1206 +
   (next << 1) + 2 (each 8 bits), the argument at $1406 + next; the entry A and X are not read, Y reaches the
   handler. Sets those and runs it; the caller checks cmd_ev_pos (a @T start rewrites it) and the handler's work. */
static void run_event(uint8_t next, uint16_t handler, uint16_t pos, uint8_t arg, fw_regs *r)
{
    uint8_t x = (uint8_t)(next << 1);
    fw_wr16(&g_f, (uint16_t)(CMD_EV_HANDLERS + x), handler);
    fw_wr16(&g_f, (uint16_t)(CMD_EV_POSITIONS + (uint8_t)(x + 2)), pos);
    g_ram[CMD_EV_ARGS + next] = arg;
    g_ram[CMD_EV_NEXT] = next;
    g_ram[CMD_EV_COUNT] = 0x7F;                                    /* not read by cmd_run_event */
    snapshot();
    cmd_run_event(&g_f, r);
    CHECK_EQ(g_ram[CMD_EV_NEXT], (uint8_t)(next + 1));
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_VECTOR), handler);
}

static void test_run_event(void)
{
    static const uint16_t w_voicing[] = { CMD_EV_NEXT, CMD_EV_VECTOR, CMD_EV_VECTOR + 1, CMD_EV_POS, CMD_EV_POS + 1,
                                          CMD_VOICING };
    setup();                                                       /* @B7 queued third: X = 4, then 6 */
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    run_event(2, ROM_CMD_EV_SET_VOICING, 0x0123, 7, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0123);
    CHECK_EQ(g_ram[CMD_VOICING], 7);
    CHECK_EQ(r.a, 7);                                              /* the handler's A, X, Y: A = the argument, */
    CHECK_EQ(r.x, 2);                                              /* X = cmd_ev_next - 1, */
    CHECK_EQ(r.y, 0x33);                                           /* Y as par_play had it */
    check_written(w_voicing, sizeof w_voicing / sizeof w_voicing[0]);
    check_no_calls();

    static const uint16_t w_index[] = { CMD_EV_NEXT, CMD_EV_VECTOR, CMD_EV_VECTOR + 1, CMD_EV_POS, CMD_EV_POS + 1,
                                        CMD_INDEX_COUNT };
    setup();                                                       /* the last of 128: X = $FE, INX / INX wrap to 0 */
    g_ram[CMD_INDEX_COUNT] = 5;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(127, ROM_CMD_EV_INDEX, 0x0456, 0x99, &r);           /* handler at $1404, position at $1206 */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0456);
    CHECK_EQ(g_ram[CMD_INDEX_COUNT], 4);
    CHECK_EQ(r.a, 0x99);                                           /* LDA $1406,X with X = 127: $1485 */
    CHECK_EQ(r.x, 127);
    CHECK_EQ(r.y, 0x33);
    check_written(w_index, sizeof w_index / sizeof w_index[0]);
    check_no_calls();

    setup();                                                       /* @A3, queued first: isr_set_volume in ROM */
    g_p3 = 0;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(0, ROM_CMD_EV_SET_VOLUME, 0x0010, 3, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0010);
    CHECK_EQ(g_ram[CMD_VOLUME], 3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_SET_VOLUME, 3, 0, 0x33);
    CHECK_EQ(r.a, 0x60);                                           /* $C << 3 */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);

    setup();                                                       /* @T5, start then end, as cmd_sound queues them */
    sound_state();
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(4, ROM_CMD_EV_SOUND_START, 0x0040, 5, &r);           /* the end event's position, cmd_sound's own */
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), 0xA040);
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x81);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 10), 3);            /* the start rewrote the next event's position */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 3);                        /* (X = cmd_ev_next << 1 = 10) and cmd_ev_pos */
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 3);
    fw_wr16(&g_f, CMD_EV_HANDLERS + 10, ROM_CMD_EV_SOUND_END);
    g_ram[CMD_EV_ARGS + 5] = 5;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    cmd_run_event(&g_f, &r);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 6);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xEEEE);                   /* positions[6], still sound_state()'s */
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x80);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), 0x1110);
    CHECK_EQ(r.a, 0x0C);
    CHECK_EQ(r.x, 5);
    CHECK_EQ(r.y, 0x33);
    check_no_calls();
}

/* A handler word that is none of the five (the producers queue none): the same bookkeeping, then fw_rom_call at
   that address with A = the argument, X = cmd_ev_next - 1 and the entry Y, as JMP (cmd_ev_vector) hands them over.
   The mock fails the call, so the C faults there. */
static void test_run_event_rom(void)
{
    static const uint16_t w[] = { CMD_EV_NEXT, CMD_EV_VECTOR, CMD_EV_VECTOR + 1, CMD_EV_POS, CMD_EV_POS + 1 };
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    run_event(1, 0xABCD, 0x0200, 0x42, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0200);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, 0xABCD, 0x42, 1, 0x33);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xABCD);
    check_written(w, sizeof w / sizeof w[0]);
}

/* ---- the frame loop (M4.4b Task 5) ---- */

static void test_play_rows(void)
{
    check_row("boot_loop_play", ROM_BOOT_LOOP_PLAY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_play", ROM_PAR_PLAY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    size_t waits = 0;
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (fw_replacements[i].waits) {
            waits++;
            CHECK(fw_replacements[i].addr == ROM_BOOT_LOOP_PLAY || fw_replacements[i].addr == ROM_PAR_PLAY);
        }
    CHECK_EQ(waits, 2);   /* only the two loops wait on the timer interrupt: diff_play checks them, diff_lts not */
}

/* Record i is a call of addr with A, X and Y (the strict budget), made with par_frames_left = frames. */
static void check_play_call(unsigned i, uint16_t addr, uint8_t a, uint8_t x, uint8_t y, uint8_t frames)
{
    check_call(i, addr, a, x, y);
    if (i < g_ncalls && i < MAX_CALLS)
        CHECK_EQ(g_calls[i].frames, frames);
}

/* The calls i, i + 1, ... are exactly the n addresses given. */
static void check_order(unsigned i, const uint16_t *addrs, unsigned n)
{
    CHECK(i + n <= g_ncalls && i + n <= MAX_CALLS);
    for (unsigned k = 0; k < n && i + k < g_ncalls && i + k < MAX_CALLS; k++)
        CHECK_EQ(g_calls[i + k].addr, addrs[k]);
}

/* The end ($B8D2-$B8FA) from call i on, with `waits` isr_par_ready calls per wait for the flag: isr_wait_frame,
   then per rest frame frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space, frm_transition_flags and the wait,
   with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then the last isr_par_ready ($B8F7;
   cmd_ev_sound_end is C and returns at once, cmd_sound_active being 0). Returns the number of calls. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    static const uint16_t k_frame[] = {
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
    };
    uint16_t k[1 + 3 * (4 + 2) + 1];
    unsigned n = 0, per = 4 + waits;
    k[n++] = ROM_ISR_WAIT_FRAME;
    for (unsigned j = 0; j < 3; j++) {
        for (unsigned m = 0; m < 4; m++)
            k[n++] = k_frame[m];
        for (unsigned m = 0; m < waits; m++)
            k[n++] = ROM_ISR_PAR_READY;
    }
    k[n++] = ROM_ISR_PAR_READY;
    check_order(i, k, n);
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < per; m++)
            if (i + 1 + per * j + m < MAX_CALLS)
                CHECK_EQ(g_calls[i + 1 + per * j + m].frames, 3 - j);
    return n;
}

/* No segment left (pros_seg_count = 0): two passes (the flag comes at the second isr_par_ready, every 2nd call),
   then the tight loop ($B8BB-$B8CF): par_frames_left 3 -> 2 -> 1 gives two frames, each par_step_tracks (C) and
   frm_finish_frame, the flag cleared and waited for again; 0 ends it. The rest frames, the flag every 2nd call. No
   event (cmd_ev_count 0) and no reply (ser_status_due 0). The registers flow: the entry A and Y and X =
   cmd_ev_next ($B877 LDX) reach the passes' isr_par_ready; Y = 0 from $B8B6 (pros_seg_count + 1); par_step_tracks
   leaves A = pros_play_pitch >> 2 = 0, X = 0, Y = 10; the rest frames' calls leave A = frm_flags, X = 0 and Y = $0D
   (frm_transition_flags' LDY #$0D, frm_snapshot's X = 0), which par_play returns. */
static void test_play_tail(void)
{
    setup();
    fill_frame_b();
    memset(g_ram + ISR_FRAME_B, 0xEE, 20);
    g_ram[PAR_FRAMES_LEFT] = 3;
    g_ram[FRM_REST_FRAME] = 0x77;
    g_ram[FRM_FLAGS] = 0x5A;
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                       /* pass 1: $B89F, $B8AB (the flag comes) */
        ROM_ISR_PAR_READY,                                          /* pass 2: $B89F, then $B8B1-$B8B9 */
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY,                    /* frame 1: $B8C3, $B8C9 */
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, /* frame 2: $B8C3, $B8C9 twice */
    };
    check_order(0, k, 8);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(2, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(3, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 2);   /* par_step_tracks' A, X, Y */
    check_play_call(5, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(g_calls[4].frames, 2);
    CHECK_EQ(check_rest_frames(8, 2), 20);                             /* the flag every 2nd call */
    check_play_call(8, ROM_ISR_WAIT_FRAME, 0, 0, FRAME_PITCH, 0);
    check_play_call(9, ROM_FRM_COPY_REST_FRAME, 3, 0, FRAME_PITCH, 3); /* $B8D5 LDA #3 */
    check_play_call(10, ROM_FRM_SNAPSHOT, 0x77, 0, 0xFF, 3);           /* frm_copy_rest_frame's A and Y */
    check_play_call(11, ROM_FRM_CLAMP_MARK_SPACE, 0, 0, FRAME_PITCH, 3);   /* frm_snapshot's: b10 (0), X = 0 */
    check_play_call(12, ROM_FRM_TRANSITION_FLAGS, 0x0B, 0, 0x0B, 3);
    check_play_call(13, ROM_ISR_PAR_READY, 0x5A, 0, 0x0D, 3);
    check_play_call(15, ROM_FRM_COPY_REST_FRAME, 0x5A, 0, 0x0D, 2);
    check_play_call(21, ROM_FRM_COPY_REST_FRAME, 0x5A, 0, 0x0D, 1);
    check_play_call(27, ROM_ISR_PAR_READY, 0x5A, 0, 0x0D, 0);           /* $B8F7 */
    CHECK_EQ(g_ncalls, 28);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 13);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                  /* cleared after the last rest frame */
    for (int b = 0; b <= FRAME_PITCH; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0);                             /* par_step_tracks wrote b0-b10 */
    CHECK_EQ(g_ram[ISR_FRAME_B + 11], 0xEE);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
}

/* One segment's frames in the main loop, with an event and the reply service. read_setup's segment (index 3 to 4),
   whose duration byte (index 3) is 2, and whose next phoneme's rank $1E takes the boundary jump (test_next_segment);
   pros_seg_count 1, so it is the last. An @B6 is due at segment 3 (cmd_ev_pos = par_seg_index = 3, cmd_ev_count 1).
   A reply is due (ser_status_due 1) and the transmitter ready (UART_STATUS bit 1): ser_tx_service at every pass.
   The flag every 2nd isr_par_ready.
   Pass 1: the event (cmd_run_event with A = par_seg_index's high byte 0, X = cmd_ev_next 0): cmd_voicing = 6,
   cmd_ev_next = 1; the test again: X = 1, not below cmd_ev_count. ser_tx_service gets the handler's A = 6 and Y
   and X = 1; isr_par_ready twice (the flag comes). Pass 2: par_frames_left 1 -> 0 ($B8FB): par_next_segment (C):
   par_frames_left = 2, pros_seg_count 0, par_seg_index 4; A = 2 ($B903). $B908: isr_par_ready, par_step_tracks,
   frm_finish_frame. Pass 3: no flag, pass 4: the flag; no segment left: the tight loop, 2 -> 1: one frame; 1 -> 0:
   the end. */
static void test_play_segment(void)
{
    read_setup();
    fill_frame_b();
    fw_wr16(&g_f, PAR_RANK_PTR, T6_RANKS);
    g_ram[T6_RANKS + T6_NEXT] = 0x1E;
    g_ram[PROS_SEG_DURATION + 3] = 2;
    fw_wr16(&g_f, PROS_SEG_COUNT, 1);
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_ram[CMD_EV_COUNT] = 1;
    fw_wr16(&g_f, CMD_EV_POS, 3);
    fw_wr16(&g_f, CMD_EV_HANDLERS, ROM_CMD_EV_SET_VOICING);
    g_ram[CMD_EV_ARGS] = 6;
    g_ram[SER_STATUS_DUE] = 1;
    g_ram[FRM_FLAGS] = 0x5A;
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                      /* pass 1 */
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY,                                         /* pass 2 */
        ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME,                                       /* $B908-$B90E */
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                      /* pass 3 */
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY,                                         /* pass 4 */
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY,                                       /* the last frame */
    };
    check_order(0, k, 14);
    check_play_call(0, ROM_SER_TX_SERVICE, 6, 1, 0x33, 1);       /* the handler's A, cmd_ev_next from $B877 */
    check_play_call(4, ROM_ISR_PAR_READY, 6, 1, 0x33, 1);
    CHECK_EQ(g_calls[5].frames, 2);                              /* par_next_segment's duration */
    check_play_call(5, ROM_ISR_PAR_READY, 2, 10, 0, 2);          /* A = par_frames_left ($B903), par_next_segment's
                                                                    X = 10 and Y = 0 */
    check_play_call(6, ROM_FRM_FINISH_FRAME, 0x18, 0, FRAME_PITCH, 2);   /* the pitch $55 to $70 over 2 frames:
                                                                       ($70 + $55) >> 1 = $62, b10 = $62 >> 2 */
    check_play_call(12, ROM_FRM_FINISH_FRAME, 0x1C, 0, FRAME_PITCH, 1);  /* then over 1: $70, b10 = $1C */
    CHECK_EQ(check_rest_frames(14, 2), 20);
    CHECK_EQ(g_ncalls, 34);
    CHECK_EQ(g_ram[CMD_VOICING], 6);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
}

/* A mute: ser_abort bit 0 arrives with the flag at the first isr_par_ready. The abort test ($B8A2) comes before the
   flag test, so par_play goes straight to the end: no frame is built (the fill frame is untouched, par_frames_left
   is only set to 3 and counted down), pros_seg_count is kept, and ser_abort stays set (boot_get_clause clears it).
   With UART_STATUS bit 1 clear (the transmitter busy, $B895 BBC1) the due reply is not served. */
static void test_play_abort(void)
{
    setup();
    fill_frame_b();
    memset(g_ram + ISR_FRAME_B, 0xEE, 20);
    fw_wr16(&g_f, PROS_SEG_COUNT, 5);
    g_ram[PAR_FRAMES_LEFT] = 4;
    g_ram[SER_STATUS_DUE] = 1;
    g_uart_status = 0;
    g_flag_every = 1;
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 4);
    CHECK_EQ(check_rest_frames(1, 1), 17);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);
    CHECK_EQ(g_ncalls, 18);
    CHECK_EQ(count(ROM_SER_TX_SERVICE), 0);
    CHECK_EQ(count(ROM_FRM_FINISH_FRAME), 0);
    for (int b = 0; b < 20; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0xEE);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 5);
    CHECK_EQ(g_ram[SER_ABORT], 1);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK(!g_f.fault);
}

/* A segment of 0 frames (spec 2.6; unreachable, every duration is at least 1). With segments left, the DEC at $B8FB
   from 0 gives $FF and a frame is built ($B908); then the mute ends it. Entered with par_frames_left = 0 and
   pros_seg_count 1. */
static void test_play_zero_frames_main(void)
{
    setup();
    fill_frame_b();
    fw_wr16(&g_f, PROS_SEG_COUNT, 1);
    g_flag_every = 1;
    g_abort_at = 3;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY };
    check_order(0, k, 4);
    check_play_call(1, ROM_ISR_PAR_READY, 0x11, 0, 1, 0xFF);   /* $B908 after the DEC; Y = pros_seg_count ($B8B1) */
    CHECK_EQ(check_rest_frames(4, 1), 17);
    CHECK_EQ(g_ncalls, 21);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);   /* par_next_segment was not called */
    CHECK(!g_f.fault);
}

/* par_next_segment leaving par_frames_left = 0 ($B906 BEQ back to $B8B1): the T6 segment with the duration byte 0;
   its current and next phonemes have the same targets $11-$1A (@u offsets 0, @B8: P10 unscaled), so every track
   is at its boundary and the 0-step interpolations of par_step_tracks return their targets. pros_seg_count 2 -> 1:
   segments are left, so the DEC at $B8FB makes $FF and a frame is built; the mute at the 3rd isr_par_ready ends
   it. */
static void test_play_zero_frames_segment(void)
{
    read_setup();
    fill_frame_b();
    fw_wr16(&g_f, PAR_RANK_PTR, T6_RANKS);
    g_ram[T6_RANKS + T6_NEXT] = 0x1E;
    for (unsigned p = 0; p < 10; p++) {
        g_ram[T6_TABLE + T6_CUR + T6_STRIDE * p] = g_ram[T6_TABLE + T6_NEXT + T6_STRIDE * p] = (uint8_t)(0x11 + p);
        g_ram[CMD_FORMANT_OFS + p] = 0;
    }
    g_ram[CMD_VOICING] = 8;
    g_ram[PROS_SEG_DURATION + 3] = 0;
    fw_wr16(&g_f, PROS_SEG_COUNT, 2);
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_flag_every = 1;
    g_abort_at = 3;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY };
    check_order(0, k, 4);
    check_play_call(1, ROM_ISR_PAR_READY, 0, 10, 1, 0xFF);   /* A = 0 ($B903), X = 10 (par_next_segment), Y = 1:
                                                               pros_seg_count again at $B8B1 */
    CHECK_EQ(check_rest_frames(4, 1), 17);
    CHECK_EQ(g_ncalls, 21);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK(!g_f.fault);
}

/* No segment left and par_frames_left = 0: the DEC at $B8BB gives $FF, and the tight loop builds 255 frames
   ($FF ... 1), each frm_finish_frame and one isr_par_ready (the flag at every call), before the end. */
static void test_play_zero_frames_tail(void)
{
    setup();
    fill_frame_b();
    g_flag_every = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 0);
    check_play_call(1, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 0xFF);
    check_play_call(509, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(count(ROM_FRM_FINISH_FRAME), 255);
    CHECK_EQ(check_rest_frames(511, 1), 17);
    CHECK_EQ(g_ncalls, 528);
    CHECK(!g_f.fault);
}

/* boot_loop_play that does not play: FW_UTT_CONTENT ($6B) 0 ($A7D6 BEQ), or the host's mute pending (ser_abort bit 0,
   $A7D8 BBS0): A = $6B, X and Y untouched, no call, nothing written. */
static void test_loop_play_idle(void)
{
    for (int abort = 0; abort <= 1; abort++) {
        setup();
        g_ram[FW_UTT_CONTENT] = abort ? 0xFF : 0;
        g_ram[SER_ABORT] = (uint8_t)abort;
        g_flag_every = 1;   /* a call that played by mistake would still end */
        snapshot();
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_loop_play(&g_f, &r);
        CHECK_EQ(r.a, abort ? 0xFF : 0);
        CHECK_EQ(r.x, 0x22);
        CHECK_EQ(r.y, 0x33);
        check_written(NULL, 0);
        check_no_calls();
    }
}

/* boot_loop_play that plays (FW_UTT_CONTENT $FF): isr_par_hold with A = $FF and the entry X and Y; par_play_start
   (C, test_play_start's voice A set-up: Y = the pause phoneme $3B, A = 0); isr_speech_start (A = X = 0 out);
   isr_volume_half; isr_wait_frame; isr_set_volume with A = cmd_volume ($B2C2); par_play (C), here ended by a mute
   at its first isr_par_ready; isr_volume_half; isr_wait_frame; isr_set_volume with A = 0 ($B2D0);
   isr_speech_stop, whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3. */
static void test_loop_play(void)
{
    setup();
    pitch_identity();
    fw_wr16(&g_f, PAR_RANK_PTR, PAR_VOICE_A);
    g_ram[PROS_SEG_PHONEME] = 0x3B;
    g_ram[PROS_SEG_PITCH] = 0x70;
    g_ram[FW_UTT_CONTENT] = 0xFF;
    g_ram[CMD_VOLUME] = 9;
    g_ram[FRM_FLAGS] = 0x5A;
    g_p3 = 0xFF;
    g_flag_every = 1;
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_loop_play(&g_f, &r);
    check_call(0, ROM_ISR_PAR_HOLD, 0xFF, 0x22, 0x33);
    check_call(1, ROM_ISR_SPEECH_START, 0, 0x22, 0x3B);          /* par_play_start's A and Y */
    check_call(2, ROM_ISR_VOLUME_HALF, 0, 0, 0x3B);              /* isr_speech_start's A and X */
    check_call(3, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x3B);            /* P3: $87 | (4 ^ $F) << 3 = $87 | $58 */
    check_call(4, ROM_ISR_SET_VOLUME, 9, 0, 0x3B);               /* cmd_volume */
    check_call(5, ROM_ISR_PAR_READY, 0xB7, 0, 0x3B);             /* par_play: P3 $87 | (9 ^ $F) << 3 = $87 | $30;
                                                                    X = cmd_ev_next 0 */
    CHECK_EQ(check_rest_frames(6, 1), 17);
    check_call(23, ROM_ISR_VOLUME_HALF, 0x5A, 0, 0x0D);          /* par_play's exit registers */
    check_call(24, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);
    check_call(25, ROM_ISR_SET_VOLUME, 0, 0, 0x0D);
    check_call(26, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);          /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 27);
    CHECK_EQ(g_p3, 0xFF);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);                      /* par_play_start ran */
    CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
    CHECK_EQ(r.a, 0xFF);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
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
    test_track_rows();
    test_interp_step();
    test_interp_zero_steps();
    test_track_start();
    test_track_end();
    test_track_sequential();
    test_track_overlap();
    test_step_tracks();
    test_event_rows();
    test_ev_index();
    test_set_voicing();
    test_set_volume();
    test_sound_start_end();
    test_sound_start_no_end();
    test_sound_end_inactive();
    test_run_event();
    test_run_event_rom();
    test_play_rows();
    test_play_tail();
    test_play_segment();
    test_play_abort();
    test_play_zero_frames_main();
    test_play_zero_frames_segment();
    test_play_zero_frames_tail();
    test_loop_play_idle();
    test_loop_play();
}
