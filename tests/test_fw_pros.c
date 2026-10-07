/* Unit checks of the segment generator on the converted data (M4.3a spec 4.4), with a mock host that records every
   ROM call. They reach what no corpus text reaches reliably (a page wrap at every append point, the phrase pause
   that computes 0). The full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid);
   the thunks' register outputs are compared there, against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/ph_expand.h"
#include "firmware/pros_addr.h"
#include "firmware/pros_decl.h"
#include "firmware/pros_flags.h"
#include "firmware/pros_gen.h"
#include "firmware/pros_make.h"
#include "firmware/pros_phrase.h"
#include "firmware/pros_seg.h"
#include "firmware/pros_stress.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 64

typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call; isr_par_ready returns the registers unchanged (it preserves A, X and Y), any other call
   fails. */
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
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the segment generator drives P2 */
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

/* pros_seg_count = index; the four pointers at the arrays plus `pages` pages, as `pages` wraps leave them. */
static void seg_state(uint16_t index, uint8_t pages)
{
    fw_wr16(&g_f, PROS_SEG_COUNT, index);
    fw_wr16(&g_f, PROS_PHONEME_PTR, (uint16_t)(PROS_SEG_PHONEME + 0x100 * pages));
    fw_wr16(&g_f, PROS_DURATION_PTR, (uint16_t)(PROS_SEG_DURATION + 0x100 * pages));
    fw_wr16(&g_f, PROS_PITCH_PTR, (uint16_t)(PROS_SEG_PITCH + 0x100 * pages));
    fw_wr16(&g_f, PROS_FLAGS_PTR, (uint16_t)(PROS_SEG_FLAGS + 0x100 * pages));
}

/* The segment at offset ofs of each array is (phoneme, duration, pitch, $80). */
static void check_segment(uint16_t ofs, uint8_t phoneme, uint8_t duration, uint8_t pitch)
{
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + ofs], phoneme);
    CHECK_EQ(g_ram[PROS_SEG_DURATION + ofs], duration);
    CHECK_EQ(g_ram[PROS_SEG_PITCH + ofs], pitch);
    CHECK_EQ(g_ram[PROS_SEG_FLAGS + ofs], PROS_SEG_FLAGS_GEN);
}

/* The four pointers are at the arrays plus `pages` pages, low bytes 0, and the bytes either side untouched. */
static void check_pages(uint8_t pages)
{
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME + 0x100 * pages);
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION + 0x100 * pages);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH + 0x100 * pages);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS + 0x100 * pages);
    CHECK_EQ(g_ram[PROS_PHONEME_PTR - 1], 0);
    CHECK_EQ(g_ram[PROS_FLAGS_PTR + 2], 0);
}

static void test_seg_rows(void)
{
    check_row("ph_append_phrase_pause", ROM_PH_APPEND_PHRASE_PAUSE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("ph_append_pause", ROM_PH_APPEND_PAUSE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_append_segment", ROM_PROS_APPEND_SEGMENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_reset_seg_ptrs", ROM_PROS_RESET_SEG_PTRS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

static void test_reset_seg_ptrs(void)
{
    setup();
    memset(g_ram + PROS_PHONEME_PTR, 0x5A, 8);
    pros_reset_seg_ptrs(&g_f);
    check_pages(0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_append_segment(void)
{
    setup();
    seg_state(0x0105, 1);   /* segment $105: Y = 5 through pointers one page on */
    g_ram[PH_PHONEME] = 0x12;
    g_ram[PH_DURATION] = 0x09;
    g_ram[PROS_TARGET] = 0x70;
    g_ram[CMD_PITCH_STEP] = 0x08;
    pros_append_segment(&g_f, 0xA1, 0xB2, 0xC3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_PAR_READY, 0xA1, 0xB2, 0xC3);   /* first, with the entry registers */
    check_segment(0x105, 0x12, 0x09, 0x78);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x78);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0106);
    check_pages(1);
    CHECK_EQ(g_ram[PH_PHONEME], 0x12);   /* read, not written */
    CHECK(!g_f.fault);

    /* the pitch sum is 8 bits: $FC + 8 = $04, the carry dropped */
    setup();
    seg_state(0x0000, 0);
    g_ram[PROS_TARGET] = 0xFC;
    g_ram[CMD_PITCH_STEP] = 0x08;
    pros_append_segment(&g_f, 0, 0, 0);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x04);
    CHECK_EQ(g_ram[PROS_SEG_PITCH], 0x04);

    /* (ptr),Y is a 16-bit sum: pros_phoneme_ptr = $02F0 with Y = $20 stores at $0310 */
    setup();
    seg_state(0x0020, 0);
    fw_wr16(&g_f, PROS_PHONEME_PTR, 0x02F0);
    g_ram[PH_PHONEME] = 0x33;
    pros_append_segment(&g_f, 0, 0, 0);
    CHECK_EQ(g_ram[0x0310], 0x33);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + 0x20], 0x00);
}

/* $CA62: the index wraps at a segment: both bytes of pros_seg_count and all four pointers move on */
static void test_append_segment_wrap(void)
{
    setup();
    seg_state(0x01FF, 1);
    g_ram[PH_PHONEME] = 0x21;
    g_ram[PH_DURATION] = 0x05;
    g_ram[PROS_TARGET] = 0x60;
    pros_append_segment(&g_f, 0x11, 0x22, 0x33);
    check_call(0, ROM_ISR_PAR_READY, 0x11, 0x22, 0x33);
    check_segment(0x1FF, 0x21, 0x05, 0x60);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0200);
    check_pages(2);
    CHECK(!g_f.fault);
}

static void test_append_pause(void)
{
    setup();
    seg_state(0x0010, 0);
    g_ram[PROS_LAST_TARGET] = 0x66;
    g_ram[PROS_TARGET] = 0x99;                  /* a pause repeats pros_last_target, not pros_target */
    CHECK_EQ(ph_append_pause(&g_f, 7), 0x10);   /* Y = the old index low byte */
    check_segment(0x10, PH_PAUSE_PHONEME, 7, 0x66);
    CHECK_EQ(g_ram[PH_PHONEME], PH_PAUSE_PHONEME);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x66);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0011);
    check_pages(0);
    CHECK_EQ(g_ncalls, 0);                      /* no isr_par_ready in a pause */
    CHECK(!g_f.fault);
}

/* $CA2F: the index wraps at a word pause */
static void test_append_pause_wrap(void)
{
    setup();
    seg_state(0x00FF, 0);
    g_ram[PROS_LAST_TARGET] = 0x70;
    CHECK_EQ(ph_append_pause(&g_f, 4), 0xFF);
    check_segment(0xFF, PH_PAUSE_PHONEME, 4, 0x70);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0100);
    check_pages(1);
    CHECK(!g_f.fault);
}

/* One phrase pause after symbol with cmd_phrase_pause = d, at index 0: returns its duration. */
static uint8_t phrase_pause(char symbol, uint8_t d)
{
    setup();
    seg_state(0x0000, 0);
    g_ram[PROS_SYMBOL] = (uint8_t)symbol;
    g_ram[CMD_PHRASE_PAUSE] = d;
    g_ram[PROS_LAST_TARGET] = 0x55;
    ph_append_phrase_pause(&g_f);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME], PH_PAUSE_PHONEME);
    CHECK_EQ(g_ram[PH_PHONEME], PH_PAUSE_PHONEME);
    CHECK_EQ(g_ram[PROS_SEG_PITCH], 0x55);
    CHECK_EQ(g_ram[PROS_SEG_FLAGS], PROS_SEG_FLAGS_GEN);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0001);
    CHECK_EQ(g_ram[CMD_PHRASE_PAUSE], d);       /* read, not written */
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
    return g_ram[PROS_SEG_DURATION];
}

/* . ? ! in full (no mask: $F6 stays $F6); , ; : (d >> 1) & $0F; any other symbol (d >> 2) & $0F */
static void test_phrase_pause_durations(void)
{
    static const char full[] = ".?!", half[] = ",;:", other[] = "()-\"[] X";
    for (const char *s = full; *s; s++)
        CHECK_EQ(phrase_pause(*s, 0xF6), 0xF6);
    for (const char *s = half; *s; s++)
        CHECK_EQ(phrase_pause(*s, 0xF6), 0x0B);   /* $7B & $0F */
    for (const char *s = other; *s; s++)
        CHECK_EQ(phrase_pause(*s, 0xF6), 0x0D);   /* $3D & $0F */
    CHECK_EQ(phrase_pause(',', 0x09), 0x04);
    CHECK_EQ(phrase_pause('-', 0x09), 0x02);
}

/* $C9EC: a duration that computes 0 becomes 1, on every path (@D0 itself never gets here: pros_phrase_gen appends
   no phrase pause when cmd_phrase_pause is 0, $D1D8) */
static void test_phrase_pause_zero(void)
{
    CHECK_EQ(phrase_pause('.', 0x00), 1);
    CHECK_EQ(phrase_pause(',', 0x01), 1);   /* 1 >> 1 */
    CHECK_EQ(phrase_pause('(', 0x03), 1);   /* 3 >> 2 */
    CHECK_EQ(phrase_pause(';', 0x20), 1);   /* $10 & $0F: the mask makes 0 */
    CHECK_EQ(phrase_pause('"', 0x40), 1);   /* $10 & $0F */
    CHECK_EQ(phrase_pause(',', 0x02), 1);   /* 1, not bumped */
    CHECK_EQ(phrase_pause('(', 0x08), 2);
}

/* $CA07: the index wraps at a phrase pause */
static void test_phrase_pause_wrap(void)
{
    setup();
    seg_state(0x02FF, 2);
    g_ram[PROS_SYMBOL] = '.';
    g_ram[CMD_PHRASE_PAUSE] = 0x0C;
    g_ram[PROS_LAST_TARGET] = 0x81;
    ph_append_phrase_pause(&g_f);
    check_segment(0x2FF, PH_PAUSE_PHONEME, 0x0C, 0x81);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0300);
    check_pages(3);
    CHECK(!g_f.fault);
}

/* A word pause wraps the index, then a segment lands on the next page through the moved pointers, at Y = 0 */
static void test_append_across_wrap(void)
{
    setup();
    seg_state(0x00FF, 0);
    g_ram[PROS_LAST_TARGET] = 0x40;
    g_ram[PH_DURATION] = 0x03;
    g_ram[PROS_TARGET] = 0x50;
    CHECK_EQ(ph_append_pause(&g_f, 9), 0xFF);
    g_ram[PH_PHONEME] = 0x07;                   /* the pause left $3B there; the decoder stores each phoneme */
    pros_append_segment(&g_f, 0x80, 1, 2);
    check_segment(0xFF, PH_PAUSE_PHONEME, 9, 0x40);
    check_segment(0x100, 0x07, 0x03, 0x50);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0101);
    check_pages(1);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1);
    check_call(0, ROM_ISR_PAR_READY, 0x80, 1, 2);
    CHECK(!g_f.fault);
}

static void test_decl_rows(void)
{
    check_row("pros_declination", ROM_PROS_DECLINATION, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_declination_step", ROM_PROS_DECLINATION_STEP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("ph_lengthen_count", ROM_PH_LENGTHEN_COUNT, FW_OUT_A | FW_OUT_Y);
}

/* Below 31 the table at $D44A, whose entry 0 is the RTS opcode ($60) of ph_lengthen_count itself; from 31 on,
   a >> 1 */
static void test_lengthen_count(void)
{
    static const uint8_t steps[31] = { 0x60, 1, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9, 9, 10,
                                       10, 10, 11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15 };
    setup();
    for (unsigned a = 0; a < 31; a++)
        CHECK_EQ(ph_lengthen_count(&g_f, (uint8_t)a), steps[a]);
    CHECK_EQ(ph_lengthen_count(&g_f, 0), 96);    /* $D44A: a 96-step entry */
    CHECK_EQ(ph_lengthen_count(&g_f, 31), 15);   /* $D442: LSR */
    CHECK_EQ(ph_lengthen_count(&g_f, 32), 16);
    CHECK_EQ(ph_lengthen_count(&g_f, 61), 30);
    CHECK_EQ(ph_lengthen_count(&g_f, 0x80), 0x40);
    CHECK_EQ(ph_lengthen_count(&g_f, 0xFF), 0x7F);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* setup(); pros_pos = pos with pros_sym_lengthen[pos] = len (and other counts either side), the baseline from
   base to end, and stale pros_phrase_len and pros_decl_step. */
static void decl_setup(uint8_t pos, uint8_t len, uint8_t base, uint8_t end)
{
    setup();
    g_ram[PROS_POS] = pos;
    g_ram[PROS_SYM_LENGTHEN + (uint8_t)(pos - 1)] = 0x7F;
    g_ram[PROS_SYM_LENGTHEN + (uint8_t)(pos + 1)] = 0x7F;
    g_ram[PROS_SYM_LENGTHEN + pos] = len;
    g_ram[PROS_BASELINE] = base;
    g_ram[PROS_BASELINE_END] = end;
    g_ram[PROS_PHRASE_LEN] = 0x33;
    g_ram[PROS_DECL_STEP] = 0x44;
}

/* Falling: (base - end) / len, bit 7 of the stored count masked off; the count is stored in pros_phrase_len */
static void test_declination_falling(void)
{
    decl_setup(5, 0x8A, 0xC0, 0x40);          /* $80 / 10 = 12, remainder 8 */
    CHECK_EQ(pros_declination(&g_f), 12);     /* the tail-branch: A = the step */
    CHECK_EQ(g_ram[PROS_DECL_STEP], 12);
    CHECK_EQ(pros_declination_y(&g_f), 12);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 10);
    CHECK_EQ(g_ram[PROS_BASELINE], 0xC0);     /* the baselines are only read */
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x40);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 5], 0x8A);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);

    decl_setup(200, 10, 120, 0);              /* an exact multiple counts its last subtraction (BCC, not BEQ) */
    CHECK_EQ(pros_declination(&g_f), 12);
    decl_setup(200, 10, 119, 0);
    CHECK_EQ(pros_declination(&g_f), 11);
    decl_setup(255, 9, 9, 0);                 /* pros_pos 255 reads $1FFF */
    CHECK_EQ(pros_declination(&g_f), 1);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 9);
}

/* Level (BEQ): the step is 0, and pros_phrase_len is still stored */
static void test_declination_level(void)
{
    decl_setup(3, 7, 0x80, 0x80);
    CHECK_EQ(pros_declination(&g_f), 0);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0);
    CHECK_EQ(pros_declination_y(&g_f), 0);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 7);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x80);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x80);
}

/* Rising: swap, the step of the swapped values, negate, swap back; A = the restored pros_baseline, Y = the step */
static void test_declination_rising(void)
{
    decl_setup(5, 10, 0x40, 0xC0);
    CHECK_EQ(pros_declination(&g_f), 0x40);   /* not the step */
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xF4);    /* 0 - 12 */
    CHECK_EQ(pros_declination_y(&g_f), 12);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 10);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x40);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0xC0);

    decl_setup(5, 1, 0x00, 0xFF);             /* capped at 25 before the negation: 0 - 25 */
    CHECK_EQ(pros_declination(&g_f), 0x00);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xE7);
    CHECK_EQ(pros_declination_y(&g_f), 25);

    decl_setup(5, 0x81, 0x7F, 0x80);          /* rising by 1 */
    CHECK_EQ(pros_declination(&g_f), 0x7F);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xFF);
    CHECK_EQ(pros_declination_y(&g_f), 1);
}

/* The cap at 25 (CPY #$19) and the counts either side of it */
static void test_declination_cap(void)
{
    decl_setup(1, 1, 24, 0);
    CHECK_EQ(pros_declination_step(&g_f), 24);
    decl_setup(1, 1, 25, 0);
    CHECK_EQ(pros_declination_step(&g_f), 25);
    decl_setup(1, 1, 26, 0);
    CHECK_EQ(pros_declination_step(&g_f), 25);
    decl_setup(1, 1, 0xFF, 0);                /* 255 subtractions, Y = 255, then the cap */
    CHECK_EQ(pros_declination_step(&g_f), 25);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 25);
}

/* $D41A: a stored count of 0 (or $80) becomes 1, and the 1 is stored */
static void test_declination_divisor_bump(void)
{
    decl_setup(9, 0x00, 20, 0);
    CHECK_EQ(pros_declination_step(&g_f), 20);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 1);
    decl_setup(9, 0x80, 20, 0);
    CHECK_EQ(pros_declination(&g_f), 20);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 1);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 9], 0x80);   /* read, not written */
}

/* The step alone (the entry the tail-branch reaches): A = Y = pros_decl_step; the difference is 8 bits even when
   pros_baseline_end is above pros_baseline (never so from pros_declination, which swaps first) */
static void test_declination_step(void)
{
    decl_setup(2, 4, 0x10, 0x20);             /* $10 - $20 = $F0: 60 steps, capped */
    CHECK_EQ(pros_declination_step(&g_f), 25);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x10);     /* the step never swaps */
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x20);
    decl_setup(2, 0x7F, 0xFE, 0x00);          /* $FE / $7F = 2 */
    CHECK_EQ(pros_declination_step(&g_f), 2);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0x7F);
}

/* ---- the phrase generator (Task 5) ---- */

#define GEN_SPEED_MAP 0x3000   /* a speed map in free RAM: map[i] = i, so ph_duration = (min + range) / 8 */

/* tn_phonetic = text; the page pointers at the arrays; the identity speed map; a level baseline ($80 to $80, so
   pros_decl_step 0); every count, flag, parameter and setting 0. */
static void gen_setup(const char *text)
{
    setup();
    size_t n = strlen(text);
    memcpy(g_ram + TN_PHONETIC, text, n);
    g_ram[TN_PHON_LEN] = (uint8_t)n;
    pros_reset_seg_ptrs(&g_f);
    for (unsigned i = 0; i < 32; i++)
        g_ram[GEN_SPEED_MAP + i] = (uint8_t)i;
    fw_wr16(&g_f, CMD_SPEED_MAP, GEN_SPEED_MAP);
    g_ram[PROS_BASELINE] = 0x80;
    g_ram[PROS_BASELINE_END] = 0x80;
}

/* Runs the phrase generator; *a, *x, *y = the registers it leaves. */
static void gen_run(uint8_t *a, uint8_t *x, uint8_t *y) { *a = pros_phrase_gen(&g_f, x, y); }

static uint16_t gen_list(uint8_t symbol)   /* the address of a symbol's phoneme list in the converted data */
{
    uint16_t p = (uint16_t)(PH_LIST_PTRS + 2 * symbol);
    return (uint16_t)(fw_rom8(&g_f, p) | fw_rom8(&g_f, (uint16_t)(p + 1)) << 8);
}

static void test_gen_row(void)
{
    check_row("pros_phrase_gen", ROM_PROS_PHRASE_GEN, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

static void test_gen_lists(void)   /* the symbols, lists and table entries the tests below rely on */
{
    static const struct { char sym; uint8_t cls, features, items[10]; } k[] = {
        { ' ', 0x00, 0x00, { 0x5F, 0xFF } },                    /* the word-pause item */
        { '.', 0x0B, 0x00, { 0xFF } },                          /* punctuation (class bit 0) */
        { '~', 0x00, 0x00, { 0xFF } },                          /* a marker (class 0) */
        { '8', 0x80, 0x00, { 0xFF } },                          /* phonetic, no features, an empty list */
        { 'X', 0x80, 0x00, { 0x3B, 10, 10, 0xFF } },            /* phonetic, no features, one triple */
        { 'M', 0x80, 0x49, { 0x2A, 80, 60, 0xFF } },            /* a consonant */
        { 'E', 0xC0, 0x83, { 0x10, 150, 50, 0xFF } },           /* a vowel */
        { 'S', 0x80, 0x50, { 0x3E, 125, 80, 0xFF } },
        { 'T', 0x80, 0x60, { 0x40, 80, 40, 0x41, 20, 20, 0x42, 30, 20, 0xFF } },   /* a stop: three triples */
    };
    setup();
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint8_t s = (uint8_t)k[i].sym;
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_SYMBOL_CLASS + s)), k[i].cls);
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_SYMBOL_FEATURES + s)), k[i].features);
        uint16_t p = gen_list(s);
        for (unsigned j = 0;; j++) {
            CHECK_EQ(fw_rom8(&g_f, (uint16_t)(p + j)), k[i].items[j]);
            if (k[i].items[j] == 0xFF)
                break;
        }
    }
    CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_DUR_SHORTEN + 20)), 18);
    CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_DUR_LENGTHEN + 100)), 110);
    CHECK(!g_f.fault);
}

static void test_gen_clause(void)   /* " ME." end to end */
{
    uint8_t a, x, y;
    gen_setup(" ME.");
    g_ram[PROS_SYM_FLAGS + 0] = 2;            /* the space: the next word has 2 symbols */
    g_ram[PROS_SYM_FLAGS + 2] = 0x21;         /* E: accent, nucleus */
    g_ram[PROS_SYM_SHORTEN + 1] = 1;          /* M: one shortening step */
    g_ram[PROS_SYM_LENGTHEN + 2] = 2;         /* E: ph_lengthen_count(1) = 1 step */
    g_ram[PROS_SYM_LENGTHEN + 0] = 4;         /* the phrase's count, read by pros_declination at pros_pos 0 */
    g_ram[PROS_BASELINE_END] = 0x70;          /* falling: (0x80 - 0x70) / 4 = 4 */
    g_ram[PROS_PARAMS + 20] = 3;              /* U */
    g_ram[PROS_PARAMS + 21] = 2;              /* V */
    g_ram[PROS_PARAMS + 22] = 10;             /* W */
    g_ram[PROS_PARAMS + 24] = 5;              /* Y */
    g_ram[PROS_PARAMS + 25] = 1;              /* Z */
    g_ram[PROS_STRESS_PARAM] = 7;
    g_ram[PROS_NUCLEUS_PARAM] = 9;
    g_ram[PROS_FINAL_PARAM] = 6;
    g_ram[CMD_PHRASE_PAUSE] = 8;              /* @D8: two pauses of 8 after '.' */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ncalls, 7);
    check_call(0, ROM_ISR_PAR_READY, 0xFF, 0, 4);       /* position 0; X and Y as pros_declination leaves them */
    check_call(1, ROM_ISR_PAR_READY, 0, 0, 0);          /* position 1: the space's list ended at its item (@Q0) */
    check_call(2, ROM_ISR_PAR_READY, 9, 1, 9);          /* M's segment: (60 + 18) / 8 */
    check_call(3, ROM_ISR_PAR_READY, 0xFF, 1, 3);       /* position 2: M's list end */
    check_call(4, ROM_ISR_PAR_READY, 20, 100, 20);      /* E's segment: X = the last lengthening index */
    check_call(5, ROM_ISR_PAR_READY, 0xFF, 100, 3);
    check_call(6, ROM_ISR_PAR_READY, 0, 3, '.');        /* position 4 (the end), after the punctuation */
    /* M: $80 / 8 + U 3 + 105 = $7C. E: ($7C >> 3 = 15, carry 1) + W 10 + Z 1 + final 6 + 105 = $8A */
    check_segment(0, 0x2A, 9, 0x7C);
    check_segment(1, 0x10, 20, 0x8A);
    check_segment(2, 0x3B, 8, 0x8A);                    /* the phrase pauses, at the last target */
    check_segment(3, 0x3B, 8, 0x8A);
    check_segment(4, 0x3B, 4, 0x8A);                    /* the end: 4 frames ... */
    check_segment(5, 0x3B, 0x80, 0x8A);                 /* ... then $80, the A the first pause leaves */
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 5);
    CHECK_EQ(a, 6);
    CHECK_EQ(x, 4);
    CHECK_EQ(y, 5);
    CHECK_EQ(g_ram[PROS_POS], 4);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 4);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x78);               /* two symbols with features: $80 - 2 * 4 */
    CHECK_EQ(g_ram[PROS_TARGET], 0x8A);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x8A);
    CHECK_EQ(g_ram[PROS_TERM_ACCENT], 10);              /* not reset by the punctuation */
    CHECK_EQ(g_ram[PROS_TERM_STRESS], 0);
    CHECK_EQ(g_ram[PROS_TERM_TAIL], 0);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);
    CHECK_EQ(g_ram[FW_PROS_WORD_U], 3);
    CHECK_EQ(g_ram[PROS_WORD_POS], 0);
    CHECK_EQ(g_ram[PROS_WORD_LEN], 0);                  /* the '.' flags */
    CHECK_EQ(g_ram[PROS_SYMBOL], '.');
    CHECK_EQ(g_ram[PROS_CLASS], 0x0B);
    CHECK_EQ(g_ram[PH_PHONEME], 0x3B);
    CHECK_EQ(g_ram[PH_MAX_DUR], 150);
    CHECK_EQ(g_ram[PH_MIN_DUR], 50);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 110);                 /* 100, lengthened once */
    CHECK_EQ(g_ram[PH_DURATION], 20);
    CHECK_EQ(fw_rd16(&g_f, PH_LIST_PTR), gen_list('E'));
    CHECK_EQ(g_ram[FW_TMP_78], 2);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    CHECK(!g_f.fault);
}

/* " E", E accented at the word's end: pros_target = baseline / 8 (+ the carry) + W + U + final + 105 */
static uint8_t gen_target(uint8_t baseline, uint8_t w, uint8_t u, uint8_t final)
{
    uint8_t a, x, y;
    gen_setup(" E");
    g_ram[PROS_SYM_FLAGS + 0] = 1;
    g_ram[PROS_SYM_FLAGS + 1] = 0x21;
    g_ram[PROS_BASELINE] = baseline;
    g_ram[PROS_BASELINE_END] = baseline;
    g_ram[PROS_PARAMS + 22] = w;
    g_ram[PROS_PARAMS + 20] = u;
    g_ram[PROS_FINAL_PARAM] = final;
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PROS_TERM_ACCENT], w);
    CHECK_EQ(g_ram[PROS_TERM_STRESS], u);
    CHECK_EQ(g_ram[PROS_TERM_TAIL], final);
    CHECK_EQ(g_ram[PROS_SEG_PITCH + 0], g_ram[PROS_TARGET]);
    CHECK(!g_f.fault);
    return g_ram[PROS_TARGET];
}

static void test_gen_carry_chain(void)   /* $D2DF-$D2EE: no CLC between the ADCs */
{
    CHECK_EQ(gen_target(0x00, 0, 0, 0), 0x69);
    CHECK_EQ(gen_target(0x04, 0, 0, 0), 0x6A);          /* the LSR's carry (bit 2) into $D2E5 */
    CHECK_EQ(gen_target(0x08, 0xFF, 2, 0), 0x6C);       /* 1 + $FF: a carry into $D2E8 */
    CHECK_EQ(gen_target(0x00, 1, 0xFF, 2), 0x6C);       /* 1 + $FF: a carry into $D2EB */
    CHECK_EQ(gen_target(0x00, 0, 1, 0xFF), 0x6A);       /* 1 + $FF: a carry into $D2EE */
    CHECK_EQ(gen_target(0xFC, 0xE1, 0xFE, 0xFF), 0x6A); /* every carry set; the last one is lost */
}

static void test_gen_duration_loops(void)   /* "S": max 125, min 80, range 45 */
{
    uint8_t a, x, y;
    gen_setup("S");
    g_ram[PROS_SYM_SHORTEN + 0] = 0xFF;                 /* (255 + 1) / 2 = 0 in 8 bits: DEY/BNE runs 256 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 5);                   /* x0.9 down to its fixed point (0 steps would leave 45) */
    check_call(1, ROM_ISR_PAR_READY, 10, 0, 10);        /* (80 + 5) / 8; X = pros_pos (no lengthening) */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 10);

    gen_setup("S");
    g_ram[PROS_SYM_LENGTHEN + 0] = 0xFF;                /* half-count 0: ph_lengthen_count(0) = $60, 96 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 255);                 /* x1.1 up to 255 */
    check_call(1, ROM_ISR_PAR_READY, 31, 255, 31);      /* 80 + 255 capped at 255, / 8; X = the last index */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 31);
    CHECK(!g_f.fault);
}

static void test_gen_events(void)
{
    uint8_t a, x, y;
    gen_setup("ME");
    g_ram[CMD_EV_COUNT] = 3;
    fw_wr16(&g_f, CMD_EV_POS, 0x0500);                  /* only the low byte is compared: due at position 0 */
    fw_wr16(&g_f, CMD_EV_POSITIONS + 0, 0x0000);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 2, 0x0200);        /* due at 0 as well (low byte 0) */
    fw_wr16(&g_f, CMD_EV_POSITIONS + 4, 0x0001);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 6, 0x0077);        /* past cmd_ev_count: still read as the next position */
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 0), 0);   /* now segment indexes */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 2), 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 4), 1);   /* after M's one segment */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 6), 0x0077);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0077);        /* the high byte zeroed */
    CHECK_EQ(g_ram[CMD_EV_NEXT], 3);

    gen_setup("M");                                     /* index 130: TXA/ASL wraps it to entry 2 */
    g_ram[CMD_EV_COUNT] = 131;
    g_ram[CMD_EV_NEXT] = 130;
    fw_wr16(&g_f, CMD_EV_POSITIONS + 4, 0x1234);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 6, 0x0009);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 260, 0x5678);      /* where entry 130 would be */
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 4), 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 9);             /* entry 3's low byte */
    CHECK_EQ(g_ram[CMD_EV_NEXT], 131);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 260), 0x5678);
    CHECK(!g_f.fault);
}

static void test_gen_punctuation(void)   /* @D0: '.' adds no phrase pause */
{
    uint8_t a, x, y;
    gen_setup("M.");
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);         /* M and the two end pauses */
    check_segment(1, 0x3B, 4, g_ram[PROS_SEG_PITCH + 0]);
    check_segment(2, 0x3B, 0x80, g_ram[PROS_SEG_PITCH + 0]);
    check_call(3, ROM_ISR_PAR_READY, 0, 1, '.');
    CHECK(!g_f.fault);
}

static void test_gen_no_features(void)   /* class bit 7, features 0 ('8', 'X'): no declination, no stress reset */
{
    uint8_t a, x, y;
    gen_setup("8X");
    g_ram[PROS_BASELINE_END] = 0x40;                    /* falling: the step is 25 (the cap) */
    g_ram[PROS_PARAMS + 25] = 4;                        /* Z: pros_term_stress of a word's later symbols */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 25);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x80);
    CHECK_EQ(g_ram[PROS_TERM_STRESS], 4);
    CHECK_EQ(g_ram[PROS_TARGET], 0x80 / 8 + 4 + 105);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);         /* '8' has an empty list; X makes one segment */
    check_segment(0, 0x3B, 1, 0x80 / 8 + 4 + 105);      /* (10 + 0) / 8 */
    CHECK(!g_f.fault);
}

static void test_gen_content_flag(void)   /* $6B: $FF at a word pause and at a triple, else unchanged */
{
    uint8_t a, x, y;
    gen_setup(" ");                                     /* @Q0 */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    gen_setup("8");
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    gen_setup(" ");
    g_ram[CMD_WORD_PAUSE] = 5;                          /* @Q5 */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    check_segment(0, 0x3B, 5, 0x80);                    /* pitch: pros_last_target as initialised */
    CHECK_EQ(g_ram[FW_TMP_78], 0);
    check_call(1, ROM_ISR_PAR_READY, 0x80, 0, 0);       /* A as ph_append_pause leaves it, X = pros_pos, Y = $78 */
    gen_setup("X");
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    CHECK(!g_f.fault);
}

static void test_gen_final_count(void)   /* 254 segments and the two pauses: 256, then a 16-bit decrement */
{
    char text[255];
    uint8_t a, x, y;
    memset(text, 'E', 254);
    text[254] = '\0';
    gen_setup(text);
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x00FF);    /* $0100 - 1 */
    CHECK_EQ(a, 0);                                     /* the low byte before the decrement */
    CHECK_EQ(x, 254);
    CHECK_EQ(y, 0xFF);                                  /* the second pause's index */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0xFE], 4);
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0xFF], 0x80);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME + 0x100);   /* the second pause wrapped the page */
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION + 0x100);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH + 0x100);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS + 0x100);
    CHECK_EQ(g_ncalls, 255 + 254);                      /* one per position and at the end, one per segment */
    CHECK(!g_f.fault);
}

static void test_gen_closure_and_hyper(void)
{
    uint8_t a, x, y;
    gen_setup("T");
    g_ram[PROS_SYM_FLAGS + 0] = 0x10;                   /* an unreleased stop: the closure only */
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + 0], 0x40);
    check_call(1, ROM_ISR_PAR_READY, 10, 0, 10);        /* (40 + 40) / 8 */
    check_call(2, ROM_ISR_PAR_READY, 0x10, 0, 10);      /* A = pros_sym_flag at the JMP */

    gen_setup("~M");                                    /* @H1: a function word makes no segment */
    g_ram[CMD_HYPER] = 1;
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);         /* the two end pauses only */
    CHECK_EQ(g_ram[PROS_FUNC_WORD], 1);
    CHECK_EQ(g_ncalls, 3);
    check_call(1, ROM_ISR_PAR_READY, 0, 0, 0);          /* after '~' (class 0): A = Y = pros_class */
    check_call(2, ROM_ISR_PAR_READY, 1, 1, 0x80);       /* after M: A = pros_func_word, Y = pros_class */
    CHECK(!g_f.fault);
}

/* ---- the phrase pass (M4.3b) ---- */

/* tn_phonetic = text, and pros_pos = tn_phon_len = its length, as pros_mark_stress leaves them; every flag, count,
   parameter and state byte 0 unless the test sets it. */
static void phrase_setup(const char *text)
{
    setup();
    size_t n = strlen(text);
    memcpy(g_ram + TN_PHONETIC, text, n);
    g_ram[TN_PHON_LEN] = (uint8_t)n;
    g_ram[PROS_POS] = (uint8_t)n;
}

/* Runs the phrase pass with the entry Y = y (A and X are not read); *r = the registers it leaves. */
static void phrase_run(uint8_t y, fw_regs *r)
{
    r->a = 0xA5;
    r->x = 0xB6;
    r->y = y;
    r->p = 0;
    pros_mark_phrases(&g_f, r);
}

static void test_phrase_rows(void)
{
    check_row("pros_mark_phrases", ROM_PROS_MARK_PHRASES, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_store_phrase_len", ROM_PROS_STORE_PHRASE_LEN, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* $D0FD: bit 7 unless the punctuation stored before had no class bit 3; the ORA is in A only */
static void test_store_phrase_len(void)
{
    setup();
    g_ram[PROS_POS] = 7;
    g_ram[PROS_PHRASE_LEN] = 5;
    g_ram[PROS_CLASS] = 0x0B;                           /* '.': class bit 3 */
    CHECK_EQ(pros_store_phrase_len(&g_f), 0x0B);        /* BBS3 taken: A = pros_class */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 7], 0x85);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 7], 0x85);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 0);

    g_ram[PROS_POS] = 3;
    g_ram[PROS_PHRASE_LEN] = 4;
    g_ram[PROS_CLASS] = 0x05;                           /* ',': no class bit 3 */
    CHECK_EQ(pros_store_phrase_len(&g_f), 1);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 3], 0x84);       /* this store still gets bit 7 ... */
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 1);

    g_ram[PROS_POS] = 1;
    g_ram[PROS_PHRASE_LEN] = 6;
    g_ram[PROS_CLASS] = 0x0D;                           /* '?' */
    CHECK_EQ(pros_store_phrase_len(&g_f), 0x0D);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 0x06);       /* ... the next one does not */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 0x06);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 2], 0);          /* only pros_pos is written */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], 0);
    CHECK_EQ(g_ram[PROS_POS], 1);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* " ME, ~TO{." walked from position 9 down to 1, then the exit block at 0: the registers at isr_par_ready ($CF28) on
   each path, the counts, the accent, the nucleus marks and the state left */
static void test_phrases_clause(void)
{
    fw_regs r;
    phrase_setup(" ME, ~TO{.");
    g_ram[PROS_SYM_FLAGS + 2] = PSF_STRESS;            /* E and O stressed (by pros_mark_stress) */
    g_ram[PROS_SYM_FLAGS + 7] = PSF_STRESS;
    g_ram[PROS_SYM_LENGTHEN + 3] = 5;                   /* the syllable counts pros_mark_stress stored: 5 for "ME" */
    g_ram[PROS_SYM_LENGTHEN + 9] = 4;                   /* (a long word from 5 on), 4 for "TO" */
    for (unsigned i = 0; i < 10; i++)
        g_ram[PROS_PARAMS + i] = (uint8_t)(0x10 + i);
    g_ram[PROS_PARAMS + 0] = 0x90;                      /* A, B: '.' falls by 16 over its count of 1 */
    g_ram[PROS_PARAMS + 1] = 0x80;
    g_ram[PROS_PARAMS + 5] = 0x70;                      /* F, G: ',' rises by 12 over its count of 3 */
    g_ram[PROS_PARAMS + 6] = 0x7C;
    g_ram[FW_PROS_WRITE_ONLY_1C1C] = 0x5A;              /* stale state, set at entry */
    g_ram[FW_PROS_NUCLEUS_MASK] = 0x5A;
    g_ram[FW_PROS_ACCENT_PENDING] = 0x5A;
    g_ram[FW_PROS_FINAL] = 0x5A;
    g_ram[PROS_ACCENT_STATE] = 0x5A;
    g_ram[PROS_FEATURES] = 0x5A;
    phrase_run(0x33, &r);
    CHECK_EQ(g_ncalls, 9);
    check_call(0, ROM_ISR_PAR_READY, 0, 9, 0x33);       /* the first: A = pros_features (0 at entry), the entry Y */
    check_call(1, ROM_ISR_PAR_READY, 0, 8, 16);         /* after '.': Y = pros_declination's (falling: the step) */
    check_call(2, ROM_ISR_PAR_READY, 0, 7, '{');        /* after '{': Y = the symbol */
    check_call(3, ROM_ISR_PAR_READY, 0x83, 6, 0x83);    /* after O: Y = its features (LDY at $D068) */
    check_call(4, ROM_ISR_PAR_READY, 0x60, 5, 0x60);    /* after T */
    check_call(5, ROM_ISR_PAR_READY, 0, 4, '~');        /* after '~', a marker: Y = the symbol */
    check_call(6, ROM_ISR_PAR_READY, 0, 3, ' ');        /* after the space */
    check_call(7, ROM_ISR_PAR_READY, 0, 2, 4);          /* after ',': rising, Y = the step before its negation */
    check_call(8, ROM_ISR_PAR_READY, 0x83, 1, 0x83);    /* after E */
    CHECK_EQ(r.a, 2);                                   /* the exit block: A = pros_word_pos, X = pros_pos */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x49);                                /* M's features */
    /* pros_sym_flags: each word's phonetic-symbol count at the space or punctuation before it, the accents with
       the nucleus marks */
    static const uint8_t flags[10] = { 2, 0, 0x67, 0, 2, 0, 0, 0x67, 0, 0 };
    /* the counts: each phrase's at the punctuation before it and at 0 (bit 7: the punctuation after the phrase has
       class bit 3; the first phrase's count goes on from the second's 3, which pros_declination_step stores back),
       the accents (+8), the phrase-final lengthening before '.' (+8), a vowel outside it (+7) and a long word (+4) */
    static const uint8_t lengthen[10] = { 5, 0, 8, 0x83, 0, 0, 0, 16, 0, 0x81 };
    static const uint8_t shorten[10] = { 5, 4, 11, 0x83, 0, 0, 0, 0, 0, 0x81 };
    for (unsigned i = 0; i < 10; i++) {
        CHECK_EQ(g_ram[PROS_SYM_FLAGS + i], flags[i]);
        CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + i], lengthen[i]);
        CHECK_EQ(g_ram[PROS_SYM_SHORTEN + i], shorten[i]);
    }
    CHECK_EQ(g_ram[PROS_STRESS_PARAM], 0x17);           /* the ',' set (F-J), the last loaded */
    CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 0x18);
    CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0x19);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x70);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x7C);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xFC);              /* 0 - 4 */
    CHECK_EQ(g_ram[PROS_POS], 0);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0);
    CHECK_EQ(g_ram[PROS_WORD_POS], 2);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 1);
    CHECK_EQ(g_ram[PROS_SYMBOL], 'M');
    CHECK_EQ(g_ram[PROS_CLASS], 0x80);
    CHECK_EQ(g_ram[PROS_FEATURES], 0x49);
    CHECK_EQ(g_ram[PROS_PREV_FEATURES], 0x49);
    CHECK_EQ(g_ram[PROS_SYM_FLAG], 0);
    CHECK_EQ(g_ram[FW_PROS_SYLLABLES], 5);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);          /* 1 at '{', 0 at the space */
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);                  /* 0 at M, a consonant before a vowel */
    CHECK_EQ(g_ram[FW_PROS_FINAL_LENGTHEN], 0);
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 0);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C1C], 1);
    CHECK_EQ(g_ram[FW_PROS_NUCLEUS_MASK], 0);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 1);        /* the exit block's store: pros_class $80 */
    CHECK(!g_f.fault);
}

/* The five pros_params of each punctuation's phrase type ($CF9A-$D04A); other punctuation loads none */
static void test_phrases_params(void)
{
    static const struct { char p; uint8_t first; } k[] = {
        { '?', 10 }, { '!', 15 }, { ',', 5 }, { ':', 5 }, { '.', 0 }, { ';', 0 },
    };
    fw_regs r;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        char text[3] = { ' ', k[i].p, '\0' };
        phrase_setup(text);
        for (unsigned j = 0; j < 20; j++)
            g_ram[PROS_PARAMS + j] = (uint8_t)(0xC0 - j);   /* every set falls by 1 */
        phrase_run(0, &r);
        uint8_t s = k[i].first;
        CHECK_EQ(g_ram[PROS_BASELINE], 0xC0 - s);
        CHECK_EQ(g_ram[PROS_BASELINE_END], 0xC0 - s - 1);
        CHECK_EQ(g_ram[PROS_STRESS_PARAM], 0xC0 - s - 2);
        CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 0xC0 - s - 3);
        CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0xC0 - s - 4);
        CHECK_EQ(g_ram[PROS_DECL_STEP], 1);             /* 1 over the count 1 */
        CHECK_EQ(g_ncalls, 1);
        check_call(0, ROM_ISR_PAR_READY, 0, 1, 0);
        CHECK(!g_f.fault);
    }
    phrase_setup(" -");                                 /* '-' (class 1) keeps the current set */
    g_ram[PROS_BASELINE] = 0x66;
    g_ram[PROS_BASELINE_END] = 0x44;
    g_ram[PROS_STRESS_PARAM] = 0xE1;
    g_ram[PROS_NUCLEUS_PARAM] = 0xE2;
    g_ram[PROS_FINAL_PARAM] = 0xE3;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x66);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x44);
    CHECK_EQ(g_ram[PROS_STRESS_PARAM], 0xE1);
    CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 0xE2);
    CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0xE3);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 25);                /* still computed: $22 over 1, capped */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 0x81);       /* still stored; '-' has no class bit 3 ... */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 0x01);       /* ... so the count before it gets no bit 7 */

    /* ... and keeps the nucleus mask: the E before '-' gets no nucleus marks, the one before '.' does */
    phrase_setup(" E-E");
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    g_ram[PROS_SYM_FLAGS + 3] = PSF_STRESS;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 3], 0x67);          /* the accent and the nucleus marks */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], PSF_STRESS);    /* the mask is still 0 */
    phrase_setup(" E.E");
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    g_ram[PROS_SYM_FLAGS + 3] = PSF_STRESS;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], PSF_STRESS | PSF_NUCLEUS);
    CHECK(!g_f.fault);
}

/* $D0C8-$D0CF: a pending accent goes to a stressed vowel ($D0CB BBS7) or long symbol ($D0CD BBS1). BRA $D0CF (a
   stressed symbol with neither: no accent) is unreachable from pros_mark_stress, which sets the stress flag only
   with features bit 7 or 1. */
static void test_phrases_accent(void)
{
    fw_regs r;
    phrase_setup(" AME");
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    g_ram[PROS_SYM_FLAGS + 2] = PSF_STRESS;             /* M (features $49) stressed: BRA $D0CF */
    g_ram[PROS_SYM_LENGTHEN + 1] = 0xFA;                /* the accent's +8 wraps mod 256 */
    g_ram[PROS_SYM_LENGTHEN + 2] = 0x30;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 3], PSF_NUCLEUS);   /* E, unstressed after the last stress: nucleus marks */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 2], PSF_STRESS | PSF_NUCLEUS);   /* no accent, but it ends the nucleus marks */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 2], 0x30);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], 0x60);          /* the accent still pending reaches A; the mask is 0 */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 0x02);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 3], 7);           /* E in the word's last syllable */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 7 + 3);       /* A outside it (M, a consonant before a vowel, ends it) */
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 3);              /* A's word position */
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);
    CHECK_EQ(g_ncalls, 3);
    check_call(1, ROM_ISR_PAR_READY, 0x83, 2, 0x83);
    check_call(2, ROM_ISR_PAR_READY, 0x49, 1, 0x49);

    phrase_setup(" 7");                                 /* a syllabic consonant (features $4B: bit 1, not bit 7) */
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], 0x67);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 8);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 7);           /* long: shortened as a vowel */
    CHECK(!g_f.fault);
}

/* pros_pos 1 on entry: DEC to 0 and straight to the exit block: no isr_par_ready, the entry Y kept, and the state
   as the entry ($CEF6-$CF17) sets it */
static void test_phrases_exit_only(void)
{
    static const uint16_t state[] = {
        FW_PROS_FINAL_LENGTHEN, PROS_ACCENT_STATE, FW_PROS_STRESS_TOGGLE, PROS_WORD_POS, PROS_FEATURES,
        FW_PROS_ACCENT_PENDING, PROS_PHRASE_LEN, FW_PROS_WRITE_ONLY_1C1C, FW_PROS_FINAL, FW_PROS_NUCLEUS_MASK,
    };
    fw_regs r;
    phrase_setup(" ");
    for (size_t i = 0; i < sizeof state / sizeof state[0]; i++)
        g_ram[state[i]] = 0x5A;                         /* stale */
    g_ram[PROS_SYM_FLAGS + 0] = 0x5A;
    phrase_run(0x77, &r);
    CHECK_EQ(g_ram[FW_PROS_FINAL_LENGTHEN], 0);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);
    CHECK_EQ(g_ram[PROS_FEATURES], 0);
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 1);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C1C], 1);        /* the dead store at $CF0F */
    CHECK_EQ(g_ram[FW_PROS_FINAL], 1);
    CHECK_EQ(g_ram[FW_PROS_NUCLEUS_MASK], PSF_NUCLEUS);
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x77);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], 0);             /* pros_word_pos */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 0x81);       /* pros_phrase_len 1 from the entry, with bit 7 */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 0], 0x81);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 1);        /* pros_class 0: no bit 3 */
    CHECK_EQ(g_ram[PROS_PREV_FEATURES], 0);
    CHECK(!g_f.fault);
}

/* Empty tn_phonetic (latent): pros_mark_stress leaves pros_pos 0, so the pass walks positions 255 down to 1 of stale
   RAM. Here '.' at 255, a stressed E at 254, M at 1, and symbol 0 (class 0, a marker) between. */
static void test_phrases_empty(void)
{
    fw_regs r;
    setup();
    g_ram[TN_PHONETIC + 255] = '.';
    g_ram[TN_PHONETIC + 254] = 'E';
    g_ram[TN_PHONETIC + 1] = 'M';
    g_ram[PROS_SYM_FLAGS + 254] = PSF_STRESS;
    phrase_run(0x5A, &r);
    CHECK_EQ(g_ncalls, 255);
    check_call(0, ROM_ISR_PAR_READY, 0, 255, 0x5A);
    check_call(1, ROM_ISR_PAR_READY, 0, 254, 0);        /* after '.': a level baseline (0 to 0), step 0 */
    check_call(2, ROM_ISR_PAR_READY, 0x83, 253, 0x83);
    check_call(3, ROM_ISR_PAR_READY, 0, 252, 0);        /* after symbol 0 */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 254], 0x67);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 254], 16);       /* the accent, and the lengthening before '.' */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 8);          /* still before '.': symbol 0 has no features */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 255], 0x81);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 0x83);       /* 1 (stored back by the step) + E + M, bit 7 ('.') */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], 2);
    CHECK_EQ(g_ram[PROS_POS], 0);
    CHECK_EQ(r.a, 2);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x49);
    CHECK(!g_f.fault);
}

/* ---- the stress pass (M4.3b Task 3) ---- */

/* setup(); tn_phonetic = text; every count, flag and state byte 0 (the caller adds stale bytes past the text). */
static void stress_setup(const char *text)
{
    setup();
    size_t n = strlen(text);
    memcpy(g_ram + TN_PHONETIC, text, n);
    g_ram[TN_PHON_LEN] = (uint8_t)n;
}

/* Runs the stress pass with the entry registers a, x, y; returns the registers it leaves. */
static fw_regs stress_run(uint8_t a, uint8_t x, uint8_t y)
{
    fw_regs r = { a, x, y, 0 };
    pros_mark_stress(&g_f, &r);
    return r;
}

static void check_bytes(uint16_t addr, const uint8_t *want, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_ram[addr + i], want[i]);
}

static void test_stress_row(void)
{
    check_row("pros_mark_stress", ROM_PROS_MARK_STRESS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* $CABD-$CB1C with tn_phon_len 0 (empty tn_phonetic: one isr_par_ready, then the RTS): the state bytes it clears and
   sets, the ones it leaves; the event setup without and with events */
static void test_stress_start(void)
{
    static const uint8_t want[32] = {   /* $1C00-$1C1F after the pass; $55 = not written */
        0x00, 0x55, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x00, 0x00, 0xFF, 0x00, 0x00,
        0x00, 0x01, 0x55, 0x55, 0x00, 0x00, 0x55, 0x55, 0x55, 0x01, 0x00, 0xFF, 0x55, 0x55, 0x00, 0x55,
    };
    stress_setup("");
    memset(g_ram + PROS_POS, 0x55, 32);
    g_ram[PROS_SYM_FLAGS] = g_ram[PROS_SYM_SHORTEN] = g_ram[PROS_SYM_LENGTHEN] = 0xAA;
    g_ram[CMD_EV_NEXT] = 9;
    fw_wr16(&g_f, CMD_EV_POS, 0x7766);
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);            /* read only when cmd_ev_count is not 0 */
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_PAR_READY, 0, 0, 0x33);       /* A = cmd_ev_count (BEQ), X = 0 (TAX), Y = the caller's */
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);                                   /* pros_pos: $FF + 1 */
    CHECK_EQ(r.y, 0x33);
    check_bytes(PROS_POS, want, 32);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS], 0);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN], 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN], 0);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x77FF);        /* the low byte only */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    CHECK(!g_f.fault);

    stress_setup("");
    g_ram[CMD_EV_COUNT] = 2;
    fw_wr16(&g_f, CMD_EV_POS, 0x7766);
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);
    r = stress_run(0x11, 0x22, 0x33);
    check_call(0, ROM_ISR_PAR_READY, 0x03, 0, 0x33);    /* A = the high byte, the last one read */
    CHECK_EQ(r.a, 0x03);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0304);        /* both bytes of the first position */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);              /* LDM #$FF */
    CHECK_EQ(g_ram[CMD_EV_NEXT], 0);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 2);
    CHECK(!g_f.fault);
}

/* Every way back to isr_par_ready at $CB1F, with its registers, and the three tn_phonetic rewrites */
static void test_stress_paths(void)
{
    static const uint8_t flags[12] = { 0, PSF_FUNC_WORD, 0, 0, 0, 0, 0, 0, 0, 0, PSF_STRESS, 0 };
    static const uint8_t shorten[12] = { 0, 0, 0, 0, 0, 0, 8, 8, 8, 6, 3, 1 };
    static const uint8_t lengthen[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 1, 1 };
    stress_setup(" ~}{`|MMA|E?");
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 13);
    check_call(0, ROM_ISR_PAR_READY, 0x00, 0, 0x33);    /* the start */
    check_call(1, ROM_ISR_PAR_READY, 0xFF, 0, 0x00);    /* ' ': the word end, A = $FF, Y = features 0 */
    check_call(2, ROM_ISR_PAR_READY, PSF_FUNC_WORD, 1, 0x00);   /* '~' */
    check_call(3, ROM_ISR_PAR_READY, 0xFF, 2, 0x00);    /* '}' */
    check_call(4, ROM_ISR_PAR_READY, 0x00, 3, 0x00);    /* '{' */
    check_call(5, ROM_ISR_PAR_READY, 0x01, 4, 0x00);    /* '`' */
    check_call(6, ROM_ISR_PAR_READY, 0x49, 5, 0x00);    /* '|' before M: A = M's features ($1C09) */
    check_call(7, ROM_ISR_PAR_READY, '|', 6, 0x49);     /* M before M: becomes | */
    check_call(8, ROM_ISR_PAR_READY, 0x49, 7, 0x49);    /* the consonant tail: A = Y = its features */
    check_call(9, ROM_ISR_PAR_READY, 0x83, 8, 0x83);    /* the vowel tail: Y = the next symbol's features */
    check_call(10, ROM_ISR_PAR_READY, 0x45, 9, 0x45);   /* '|' before E: R, a consonant */
    check_call(11, ROM_ISR_PAR_READY, 0x83, 10, 0x00);  /* E before the end: Y = 0 */
    check_call(12, ROM_ISR_PAR_READY, 0xFF, 11, 0x00);  /* '?': the word end */
    CHECK_EQ(r.a, 0xFF);
    CHECK_EQ(r.x, 12);
    CHECK_EQ(r.y, 0x00);
    CHECK(memcmp(g_ram + TN_PHONETIC, " ~}{`||MARE.", 12) == 0);   /* MM -> |M, |E -> RE, `...? -> . */
    check_bytes(PROS_SYM_FLAGS, flags, 12);
    check_bytes(PROS_SYM_SHORTEN, shorten, 12);
    check_bytes(PROS_SYM_LENGTHEN, lengthen, 12);
    CHECK_EQ(g_ram[PROS_POS], 12);
    CHECK_EQ(g_ram[PROS_SYMBOL], '?');                  /* read before the rewrite */
    CHECK_EQ(g_ram[PROS_CLASS], 0x0D);
    CHECK_EQ(g_ram[PROS_PREV_FEATURES], 0x83);
    CHECK_EQ(g_ram[FW_PROS_BACKQUOTE], 0);
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 1);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0xFF);
    CHECK_EQ(g_ram[FW_PROS_NEXT_SYM], 0x0D);            /* no next symbol */
    CHECK_EQ(g_ram[FW_PROS_NEXT_PHON], 0x0D);
    CHECK(!g_f.fault);
}

/* A vowel before | and a vowel counts no syllable, so the R gets the toggle's accent state 0 with the syllable count
   still 0: BEQ $CCBA taken (twice: at R and at the last A). The spec calls it unreachable; syllabic_stress has it. */
static void test_stress_no_syllable(void)
{
    static const uint8_t shorten[3] = { 0, 5 + 3 + 12 + 3, 5 + 3 };   /* R: the $0C at $CCD5 (carry 0) */
    static const uint8_t lengthen[3] = { 8, 4, 5 };
    stress_setup("A|A");
    fw_regs r = stress_run(0, 0, 0);
    CHECK_EQ(g_ncalls, 4);
    check_call(1, ROM_ISR_PAR_READY, 0x83, 0, 0x83);
    check_call(2, ROM_ISR_PAR_READY, 0x45, 1, 0x45);
    check_call(3, ROM_ISR_PAR_READY, 0x83, 2, 0x00);
    CHECK_EQ(r.x, 3);
    CHECK(memcmp(g_ram + TN_PHONETIC, "ARA", 3) == 0);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], PSF_STRESS);
    check_bytes(PROS_SYM_SHORTEN, shorten, 3);
    check_bytes(PROS_SYM_LENGTHEN, lengthen, 3);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[FW_PROS_SYLLABLES], 1);
    CHECK(!g_f.fault);
}

/* The block $CC60-$CCD7 alone at pros_pos 5: a liquid before a vowel with the accent state 0 */
static uint8_t stress_counts(uint8_t shorten)
{
    setup();
    g_ram[PROS_POS] = 5;
    g_ram[PROS_FEATURES] = 0x45;                        /* R: a consonant, liquid, voiced */
    g_ram[FW_PROS_STRESS_TOGGLE] = 0xFF;
    g_ram[FW_PROS_NEXT_FEATURES] = 0x83;                /* a vowel next */
    g_ram[PROS_SYM_SHORTEN + 5] = shorten;
    pros_stress_counts(&g_f);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 5], 0);             /* accent state 0: no stress flag */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 5], 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
    return g_ram[PROS_SYM_SHORTEN + 5];
}

static void test_stress_carry(void)   /* $CCD5: ADC #$0C with no CLC, after the CLC; ADC #3 at $CCB2 */
{
    CHECK_EQ(stress_counts(0x10), 0x10 + 3 + 12);
    CHECK_EQ(stress_counts(0xFC), 0x0B);                /* $FC + 3 = $FF, no carry; + 12 wraps to $0B */
    CHECK_EQ(stress_counts(0xFE), 0x0E);                /* $FE + 3 = $01, carry; + 12 + 1 = $0E (not $0D) */
}

/* $CDD9: the consonant scan after a vowel has no bound; X wraps from $FF to 0 inside page $19 */
static void test_stress_scan_wrap(void)
{
    stress_setup(".E");
    memset(g_ram + TN_PHONETIC + 2, 'M', 254);          /* consonants from position 2 to 255 */
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 1);                  /* the scan stopped at position 0, '.' (class bit 0) */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4 + 4);      /* the first word: + 4; before punctuation: + 4 */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], PSF_STRESS);
    CHECK(!g_f.fault);

    stress_setup(" E");
    memset(g_ram + TN_PHONETIC + 2, 'M', 254);
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);                  /* position 0 is a space this time */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4 + 1);
    CHECK(!g_f.fault);
}

/* Reads past tn_phon_len: after A, L makes the scan start at position 3 and the next phonetic symbol position 2's
   ($CDBA, $1C12 = 1); at L, $CC37 reads position 2: a vowel there toggles the stress */
static void test_stress_past_end(void)
{
    stress_setup("AL");
    g_ram[TN_PHONETIC + 2] = 'E';
    g_ram[TN_PHONETIC + 3] = '.';
    g_ram[FW_PROS_WRITE_ONLY_1C12] = 0x55;
    fw_regs r = stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C12], 1);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 1);                  /* position 3, not 2 */
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);          /* toggled at L by the E past the end */
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 5 + 3 + 3);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 4);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4);
    CHECK_EQ(g_ram[FW_PROS_NEXT_PHON], 0x0D);           /* the look-ahead at L overwrote $CDBF's 'E' */
    check_call(1, ROM_ISR_PAR_READY, 0x83, 0, 0x45);    /* Y = L's features ($1C07) */
    CHECK_EQ(r.y, 0x45);
    CHECK(!g_f.fault);

    stress_setup("AL");
    g_ram[TN_PHONETIC + 2] = 'M';
    g_ram[TN_PHONETIC + 3] = '.';
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0xFF);       /* no vowel after L */
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0xFF);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 3);

    stress_setup("AE");                                 /* no L: $1C12 = 0 */
    g_ram[FW_PROS_WRITE_ONLY_1C12] = 0x55;
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C12], 0);
    CHECK(!g_f.fault);
}

/* $CCFB: the class table read at a position, not at a symbol; the count lengthened is position pos + 1, here
   tn_phon_len, which no position clears: it wraps mod 256 */
static void test_stress_count_wrap(void)
{
    char text[34];
    memset(text, ' ', 31);
    strcpy(text + 31, "EM");                            /* M at position 32: ph_symbol_class[33] = '!' = 1 */
    stress_setup(text);
    g_ram[PROS_SYM_LENGTHEN + 33] = 0xFC;
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 33], 0x04);      /* $FC + 8 */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 32], 3);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 31], 1);          /* E before a nasal */
    CHECK_EQ(g_ncalls, 34);
    CHECK(!g_f.fault);

    memset(text, ' ', 31);
    text[31] = 'E';
    text[32] = 'M';
    text[33] = '\0';
    stress_setup(text + 1);                             /* M at position 31: ph_symbol_class[32] = ' ' = 0 */
    g_ram[PROS_SYM_LENGTHEN + 32] = 0xFC;
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 32], 0xFC);
    CHECK(!g_f.fault);
}

/* ---- pros_make_segments (M4.3b Task 4) ---- */

static void test_make_row(void)
{
    check_row("pros_make_segments", ROM_PROS_MAKE_SEGMENTS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* " EM" (no punctuation, so no pass loads another parameter set): stale state that the copies and
   pros_reset_seg_ptrs replace; the statement's parameters @R A-E falling from $90 to $80 */
static void make_setup(void)
{
    setup();
    memcpy(g_ram + TN_PHONETIC, " EM", 3);
    g_ram[TN_PHON_LEN] = 3;
    for (unsigned i = 0; i < 32; i++)
        g_ram[GEN_SPEED_MAP + i] = (uint8_t)i;
    fw_wr16(&g_f, CMD_SPEED_MAP, GEN_SPEED_MAP);
    static const uint8_t params[5] = { 0x90, 0x80, 7, 9, 6 };   /* A, B: the baseline and its end; C, D, E */
    memcpy(g_ram + PROS_PARAMS, params, sizeof params);
    g_ram[PROS_BASELINE] = g_ram[PROS_BASELINE_END] = 0x5A;
    g_ram[PROS_STRESS_PARAM] = g_ram[PROS_NUCLEUS_PARAM] = g_ram[PROS_FINAL_PARAM] = 0x5A;
    seg_state(0x5A5A, 3);
}

/* The parameter copies, then pros_reset_seg_ptrs, pros_mark_stress, pros_mark_phrases and pros_phrase_gen in that
   order, each pass's first isr_par_ready with the Y the step before left; the registers out are pros_phrase_gen's */
static void test_make_segments(void)
{
    make_setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    pros_make_segments(&g_f, &r);
    CHECK_EQ(g_ncalls, 12);
    check_call(0, ROM_ISR_PAR_READY, 0, 0, 0x33);       /* pros_mark_stress, the start: Y = the entry Y */
    check_call(1, ROM_ISR_PAR_READY, 0xFF, 0, 0);       /* after the space */
    check_call(2, ROM_ISR_PAR_READY, 0x83, 1, 0x49);    /* after E: Y = the next symbol's features */
    check_call(3, ROM_ISR_PAR_READY, 0x49, 2, 0x49);    /* after M; it leaves A = Y = $49, X = 3 */
    check_call(4, ROM_ISR_PAR_READY, 0, 2, 0x49);       /* pros_mark_phrases, the first: Y = pros_mark_stress's */
    check_call(5, ROM_ISR_PAR_READY, 0x49, 1, 0x49);    /* after M; it leaves A = 2, X = 0, Y = E's features */
    check_call(6, ROM_ISR_PAR_READY, 0xFF, 0, 5);       /* pros_phrase_gen, the first: X and Y as its
                                                           pros_declination leaves them ($10 over the count 3) */
    check_call(7, ROM_ISR_PAR_READY, 0, 0, 0);
    check_call(8, ROM_ISR_PAR_READY, 0x13, 0x61, 0x13); /* E's segment */
    check_call(9, ROM_ISR_PAR_READY, 0xFF, 0x61, 3);
    check_call(10, ROM_ISR_PAR_READY, 0x0A, 0x12, 0x0A);   /* M's segment */
    check_call(11, ROM_ISR_PAR_READY, 0xFF, 0x12, 3);   /* the end */
    CHECK_EQ(r.a, 4);                                   /* pros_phrase_gen's A, X and Y */
    CHECK_EQ(r.x, 3);
    CHECK_EQ(r.y, 3);
    CHECK_EQ(g_ram[PROS_STRESS_PARAM], 7);              /* the copies of C, D and E (no punctuation reloads them) */
    CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 9);
    CHECK_EQ(g_ram[PROS_FINAL_PARAM], 6);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x80);           /* of B (falling: no swap) */
    CHECK_EQ(g_ram[PROS_BASELINE], 0x90 - 2 * 5);       /* of A, less the step at E and at M */
    CHECK_EQ(g_ram[PROS_DECL_STEP], 5);
    /* pros_mark_stress: E stressed ($40), E and M lengthened by 4, E shortened by 1 (before a nasal), M by 3 (after a
       vowel in the word); pros_mark_phrases: the word's count 2 at the space, E's accent ($60, + 8) and the nucleus
       marks (7), E shortened by 7, the phrase's count 3 at 0 with bit 7 */
    static const uint8_t flags[3] = { 2, 0x67, 0x07 };
    static const uint8_t lengthen[3] = { 0x83, 4 + 8, 4 };
    static const uint8_t shorten[3] = { 0x83, 1 + 7, 3 };
    for (unsigned i = 0; i < 3; i++) {
        CHECK_EQ(g_ram[PROS_SYM_FLAGS + i], flags[i]);
        CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + i], lengthen[i]);
        CHECK_EQ(g_ram[PROS_SYM_SHORTEN + i], shorten[i]);
    }
    check_pages(0);                                     /* pros_reset_seg_ptrs replaced the stale pointers */
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 3);
    check_segment(0, 0x10, 0x13, 0x84);                 /* E */
    check_segment(1, 0x2A, 0x0A, 0x80);                 /* M */
    check_segment(2, 0x3B, 4, 0x80);                    /* the end pauses */
    check_segment(3, 0x3B, 0x80, 0x80);
    CHECK(!g_f.fault);
}

void suite_fw_pros(void)
{
    CHECK(fw_image_get() != NULL);
    test_seg_rows();
    test_reset_seg_ptrs();
    test_append_segment();
    test_append_segment_wrap();
    test_append_pause();
    test_append_pause_wrap();
    test_phrase_pause_durations();
    test_phrase_pause_zero();
    test_phrase_pause_wrap();
    test_append_across_wrap();
    test_decl_rows();
    test_lengthen_count();
    test_declination_falling();
    test_declination_level();
    test_declination_rising();
    test_declination_cap();
    test_declination_divisor_bump();
    test_declination_step();
    test_gen_row();
    test_gen_lists();
    test_gen_clause();
    test_gen_carry_chain();
    test_gen_duration_loops();
    test_gen_events();
    test_gen_punctuation();
    test_gen_no_features();
    test_gen_content_flag();
    test_gen_final_count();
    test_gen_closure_and_hyper();
    test_phrase_rows();
    test_store_phrase_len();
    test_phrases_clause();
    test_phrases_params();
    test_phrases_accent();
    test_phrases_exit_only();
    test_phrases_empty();
    test_stress_row();
    test_stress_start();
    test_stress_paths();
    test_stress_no_syllable();
    test_stress_carry();
    test_stress_scan_wrap();
    test_stress_past_end();
    test_stress_count_wrap();
    test_make_row();
    test_make_segments();
}
