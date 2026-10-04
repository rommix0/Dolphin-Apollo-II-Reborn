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
#include "firmware/pros_gen.h"
#include "firmware/pros_seg.h"
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
}
