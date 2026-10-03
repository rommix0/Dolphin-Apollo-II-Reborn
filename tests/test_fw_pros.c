/* Unit checks of the segment generator on the converted data (M4.3a spec 4.4), with a mock host that records every
   ROM call. They reach what no corpus text reaches reliably (a page wrap at every append point, the phrase pause
   that computes 0). The full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid);
   the thunks' register outputs are compared there, against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/pros_addr.h"
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
}
