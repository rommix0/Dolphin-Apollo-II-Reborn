/* Unit checks of tn_convert_clause and tn_append_phonetic on the converted data, with a mock host that records
   every ROM call, every wait and, in parallel mode (boot_dip bit 0, set by setup_n), every isr_par_ready and
   isr_par_hold (C since M4.6b) by its write of P0. tn_convert_clause runs the deferred commands through the C
   cmd_run_due; the tests queue commands whose handler stays in ROM (bank_cmd_select), so the mock sees each one as
   a cmd_run_next call. The full proof is the live differential test (diff_lts) and the hybrid goldens
   (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/pros_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 2048

typedef struct { uint16_t addr; uint8_t a, x, y, phon_len; uint64_t budget; } rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_sfr[0x100];         /* the SFRs the C speech timing reads and writes, as last written */

static void record(fw *f, uint16_t addr, const fw_regs *r, uint64_t budget)
{
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r ? r->a : 0;
        c->x = r ? r->x : 0;
        c->y = r ? r->y : 0;
        c->phon_len = fw_rd8(f, TN_PHON_LEN);
        c->budget = budget;
    }
    g_ncalls++;
}

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); any other call fails (speech timing is C since
   M4.6b). */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
    record(f, addr, r, budget);
    if (addr == ROM_CMD_RUN_NEXT) {
        uint8_t n = (uint8_t)(fw_rd8(f, CMD_NEXT) + 1);
        fw_wr8(f, CMD_NEXT, n);
        fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, (uint16_t)(CMD_POSITIONS + n)));
        return FW_CALL_RETURNED;
    }
    return FW_CALL_FAILED;
}

/* Records every wait (addr = its ROM loop). Each wait for an interrupt is one handled: the timer interrupt sets
   isr_frame_flag bit 0 (a frame is due at every wait). isr_timer_start's first wait sets ser_abort bit 0 (a mute as
   the DSP starts), so the C boot_loop_play of the mid-clause flush plays no segment: par_play goes from its first
   pass to the three rest frames. */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw *f = ctx;
    record(f, at, NULL, 0);
    if (kind == FW_WAIT_INTERRUPT)
        fw_wr8(f, ISR_FRAME_FLAG, 1);
    if (kind == FW_WAIT_TIMER2)
        fw_wr8(f, SER_ABORT, 1);
    return true;
}

/* UART_STATUS: the transmitter ready (bit 1), nothing received; any other SFR as last written. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    return addr == 0xE7 ? 0x02 : g_sfr[addr];
}

/* Every SFR write is kept; a write of P0 ($F6) is also recorded: isr_par_ready sets bit 3 (a call of
   ROM_ISR_PAR_READY), isr_par_hold clears it (ROM_ISR_PAR_HOLD), neither with registers (the C passes them to
   nothing). */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    fw *f = ctx;
    g_sfr[addr] = value;
    if (addr == 0xF6)
        record(f, (value & 0x08) ? ROM_ISR_PAR_READY : ROM_ISR_PAR_HOLD, NULL, 0);
}

static void mock_window(void *ctx, bool on)
{
    (void)ctx;
    g_window = on;
}

static void setup_n(const char *text, size_t n)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_host.read_sfr = mock_read_sfr;
    g_host.write_sfr = mock_write_sfr;
    g_host.wait = mock_wait;
    g_window = false;
    g_ncalls = 0;
    memset(g_sfr, 0, sizeof g_sfr);
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[BOOT_DIP] = 1;                        /* parallel mode: isr_par_ready and isr_par_hold are recorded */
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
}

static void setup(const char *text) { setup_n(text, strlen(text)); }

/* Queues command n as cmd_scan_clause does, due at cmd_text position pos, with a handler that stays in ROM. */
static void queue(uint8_t n, uint8_t pos)
{
    g_ram[CMD_POSITIONS + n] = pos;
    fw_wr16(&g_f, (uint16_t)(CMD_HANDLERS + 2 * n), ROM_BANK_CMD_SELECT);
    g_ram[CMD_ARG_INDEX + n] = n;
    g_ram[CMD_COUNT] = (uint8_t)(n + 1);
    g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS];
}

static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Every cmd_run_next call has the strict budget. */
static void check_helper_budgets(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_CMD_RUN_NEXT)
            CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}

/* the spoken name of c: the output of the first rule of its list */
static const char *name_of(uint8_t c) { return tn_char_lists[c]->rules[0].output; }

static void test_no_commands_queued(void)
{
    setup("Ab c");                              /* cmd_count = 0, though cmd_next_pos = 0 matches */
    tn_convert_clause(&g_f);
    CHECK_EQ(count(ROM_CMD_RUN_NEXT), 0);
    CHECK_EQ(g_ram[CMD_NEXT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_VECTOR), 0);
    CHECK(!g_f.fault);
}

static void test_phonetic_mode(void)
{
    setup("Ab c");
    g_ram[CMD_PHONETIC] = 1;
    g_ram[CMD_EV_COUNT] = 9;
    for (uint8_t n = 0; n < 5; n++)
        queue(n, n);                            /* one command due at each position 0-4 */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[TN_PHON_LEN], 4);
    CHECK(memcmp(g_ram + TN_PHONETIC, "Ab c", 4) == 0);
    CHECK_EQ(g_ram[TN_POS], 4);
    CHECK_EQ(g_ram[TN_KEY_POS], 3);                /* phonetic mode lags tn_key_pos one behind tn_pos ($B20A-$B215) */
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 4);
    /* each command runs before its character is copied, with X = its position; the one due at the clause end
       never runs, because phonetic mode has no final cmd_run_due */
    CHECK_EQ(count(ROM_CMD_RUN_NEXT), 4);
    unsigned k = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_CMD_RUN_NEXT || k >= 4)
            continue;
        CHECK_EQ(g_calls[i].x, k);
        CHECK_EQ(g_calls[i].y, k);              /* Y = cmd_next */
        CHECK_EQ(g_calls[i].phon_len, k);
        k++;
    }
    CHECK_EQ(g_ram[CMD_NEXT], 4);
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void test_copy_and_final_run_due(void)
{
    setup(".");
    g_ram[FW_TMP_75] = 0xAA;                    /* the copy must overwrite it with X = 0 */
    queue(0, 0);
    queue(1, 1);                                /* due at the clause end */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[TN_PHON_LEN], 1);
    CHECK_EQ(g_ram[TN_PHONETIC], '.');
    CHECK_EQ(g_ram[FW_TMP_75], 0);              /* X saved around the copy */
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK_EQ(count(ROM_CMD_RUN_NEXT), 2);
    CHECK_EQ(g_calls[1].addr, ROM_CMD_RUN_NEXT);   /* after isr_par_ready, before the copy */
    CHECK_EQ(g_calls[1].x, 0);
    CHECK_EQ(g_calls[1].phon_len, 0);
    CHECK_EQ(g_calls[g_ncalls - 1].addr, ROM_CMD_RUN_NEXT);   /* the final cmd_run_due, after the copy */
    CHECK_EQ(g_calls[g_ncalls - 1].x, 1);
    CHECK_EQ(g_calls[g_ncalls - 1].y, 1);
    CHECK_EQ(g_calls[g_ncalls - 1].phon_len, 1);
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void test_punct_spoken(void)
{
    setup(".");
    g_ram[CMD_PUNCT] = 1;                       /* @P1 */
    tn_convert_clause(&g_f);
    const char *name = name_of('.');
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(name));
    CHECK(memcmp(g_ram + TN_PHONETIC, name, strlen(name)) == 0);
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK(!g_f.fault);
}

static void test_vowelless_word_spelled(void)
{
    setup(" BCD");                              /* the leading space puts the word at tn_key_pos 1 */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[CMD_SPELL], 3);              /* automatic spelling */
    char want[64] = " ";
    strcat(want, name_of('B'));
    strcat(want, name_of('C'));
    strcat(want, name_of('D'));
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(want));
    CHECK(memcmp(g_ram + TN_PHONETIC, want, strlen(want)) == 0);
    CHECK_EQ(g_ram[TN_IN_WORD] & 1, 1);
    CHECK_EQ(g_ram[TN_POS], 4);
    CHECK(!g_f.fault);
}

static void test_digit_to_rules(void)
{
    setup("7");
    tn_convert_clause(&g_f);
    CHECK(g_ram[TN_PHON_LEN] > 0);
    CHECK_EQ(g_ram[TN_IN_WORD] & 1, 1);         /* digits set tn_in_word like letters */
    CHECK(!g_window);                           /* CLB7 P2 after the rules */
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK(!g_f.fault);
}

/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls, and the C boot_loop_play (M4.4b) plays them (FW_UTT_CONTENT is $FF): the C par_play_start
   sets up the first segment; the mock's mute at the timer start's first wait ends the C par_play at its first
   pass. Then the conversion goes on from tn_resume_pos with an empty buffer. */
static void test_flush(void)
{
    char text[255];
    for (int i = 0; i < 255; i++)
        text[i] = (char)('A' + i % 26);
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B);   /* where par_setup_voice leaves it: the C rest frames go there */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_f.tn_flushes, 1);
    CHECK_EQ(g_ram[TN_RESUME_POS], 250);
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);            /* the second pass converted text[250..254] */
    CHECK(memcmp(g_ram + TN_PHONETIC, text + 250, 5) == 0);
    CHECK_EQ(g_ram[TN_POS], 255);
    CHECK_EQ(count(ROM_PROS_MAKE_SEGMENTS), 0);           /* the C pros_make_segments, not the ROM's */
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 0);               /* the C boot_loop_play, not the ROM's */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    CHECK_EQ(g_ram[PROS_POS], TN_PHONETIC_FLUSH);         /* pros_phrase_gen walked the 250 symbols ... */
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 368);         /* ... into 369 segments (the index of the last) */
    /* the conversion 251 + 5 (one per character, the flush's included), pros_mark_stress 251 (the start and one per
       position), pros_mark_phrases 249 (positions 249 to 1), pros_phrase_gen 251 (positions 0 to 250) + 367 (one per
       segment but the two end pauses), par_play 8 (its pass, two per rest frame around its wait, its last call) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1382);
    CHECK(g_ncalls <= MAX_CALLS);
    /* boot_loop_play, all C (M4.6b): isr_par_hold, the waits of isr_speech_start (the timer start's two, a frame),
       the frame after the half volume, par_play's pass and end, the frame after the half volume again; then
       isr_par_ready at text[250..254] */
    static const uint16_t k_play[] = {
        ROM_ISR_PAR_HOLD, FW_WAIT_TIMER2_LOOP, FW_WAIT_TIMER3_LOOP, ROM_ISR_WAIT_FRAME, ROM_ISR_WAIT_FRAME,
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,                              /* the rest frames */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY,                                                         /* $B8F7 */
        ROM_ISR_WAIT_FRAME,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
    };
    unsigned n = sizeof k_play / sizeof k_play[0];
    CHECK(g_ncalls >= n && g_ncalls <= MAX_CALLS);
    if (g_ncalls < n || g_ncalls > MAX_CALLS)
        return;
    unsigned first = g_ncalls - n;
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_calls[first + i].addr, k_play[i]);
    CHECK_EQ(g_calls[first].phon_len, TN_PHONETIC_FLUSH);
    CHECK_EQ(g_sfr[0xEE], 0x79);                          /* P3: isr_set_volume(0) ($B2D0): $F << 3, and bit 0 set
                                                             by isr_timer_start */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xFFFF);          /* par_play_start: no event queued */
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);                  /* the three rest frames counted down */
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                   /* cleared after the last rest frame ($B8EC) */
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void put_rule_text(void)   /* a length byte 3, then "abc", at $0300 = tn_rule_ptr */
{
    static const uint8_t text[] = { 3, 'a', 'b', 'c' };
    memcpy(g_ram + 0x0300, text, sizeof text);
    fw_wr16(&g_f, TN_RULE_PTR, 0x0300);
}

static void test_append_voice_on(void)
{
    setup("");
    put_rule_text();
    g_ram[TN_PHON_LEN] = 2;
    uint8_t x = 0x55, y = 0;
    CHECK_EQ(tn_append_phonetic(&g_f, 3, &x, &y), 0xFF);
    CHECK_EQ(x, 5);
    CHECK_EQ(y, 3);                             /* on the last byte copied */
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2, "abc", 3) == 0);
    CHECK_EQ(g_ram[FW_TMP_78], 0);              /* counted down */
    CHECK_EQ(g_ncalls, 0);                      /* no ROM call */
    CHECK(!g_f.fault);
}

static void test_append_voice_off(void)
{
    setup("");
    put_rule_text();
    g_ram[CMD_VOICE] = 0;
    g_ram[TN_PHON_LEN] = 2;
    uint8_t x = 0x55, y = 0;
    CHECK_EQ(tn_append_phonetic(&g_f, 3, &x, &y), 0xFF);
    CHECK_EQ(x, 2);                             /* X = tn_phon_len */
    CHECK_EQ(y, 0);
    CHECK_EQ(g_ram[TN_PHON_LEN], 2);
    CHECK_EQ(g_ram[TN_PHONETIC + 2], 0);
    CHECK_EQ(g_ram[FW_TMP_78], 3);              /* the length stays */
    CHECK(!g_f.fault);
}

static void test_append_empty(void)
{
    setup("");
    put_rule_text();
    g_ram[FW_TMP_78] = 0x99;
    uint8_t x = 0x55, y = 7;
    CHECK_EQ(tn_append_phonetic(&g_f, 0, &x, &y), 0xFF);
    CHECK_EQ(x, 0x55);
    CHECK_EQ(y, 7);
    CHECK_EQ(g_ram[FW_TMP_78], 0x99);
    CHECK_EQ(g_ram[TN_PHON_LEN], 0);
    CHECK(!g_f.fault);
}

void suite_fw_tn(void)
{
    CHECK(fw_image_get() != NULL);
    test_append_voice_on();
    test_append_voice_off();
    test_append_empty();
    test_no_commands_queued();
    test_phonetic_mode();
    test_copy_and_final_run_due();
    test_punct_spoken();
    test_vowelless_word_spelled();
    test_digit_to_rules();
    test_flush();
}
