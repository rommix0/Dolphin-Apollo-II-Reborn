/* Unit checks of tn_convert_clause and tn_append_phonetic on the converted data, with a mock host that records
   every ROM call. tn_convert_clause runs the deferred commands through the C cmd_run_due; the tests queue
   commands whose handler stays in ROM (bank_cmd_select), so the mock sees each one as a cmd_run_next call. The
   full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

typedef struct { uint16_t addr; uint8_t a, x, y, phon_len; uint64_t budget; } rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r->a;
        c->x = r->x;
        c->y = r->y;
        c->phon_len = fw_rd8(f, TN_PHON_LEN);
        c->budget = budget;
    }
    g_ncalls++;
    if (addr == ROM_CMD_RUN_NEXT) {
        uint8_t n = (uint8_t)(fw_rd8(f, CMD_NEXT) + 1);
        fw_wr8(f, CMD_NEXT, n);
        fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, (uint16_t)(CMD_POSITIONS + n)));
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_ISR_PAR_READY || addr == ROM_PROS_MAKE_SEGMENTS || addr == ROM_BOOT_LOOP_PLAY)
        return FW_CALL_RETURNED;
    return FW_CALL_FAILED;
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
    g_window = false;
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
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

/* Every helper call has the strict budget. */
static void check_helper_budgets(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_ISR_PAR_READY || g_calls[i].addr == ROM_CMD_RUN_NEXT)
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

static void test_flush(void)
{
    char text[255];
    for (int i = 0; i < 255; i++)
        text[i] = (char)('A' + i % 26);
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_f.tn_flushes, 1);
    CHECK_EQ(g_ram[TN_RESUME_POS], 250);
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);            /* the second pass converted text[250..254] */
    CHECK(memcmp(g_ram + TN_PHONETIC, text + 250, 5) == 0);
    CHECK_EQ(g_ram[TN_POS], 255);
    CHECK_EQ(count(ROM_PROS_MAKE_SEGMENTS), 1);
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 1);
    for (unsigned i = 0; i + 1 < g_ncalls && i + 1 < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_PROS_MAKE_SEGMENTS)
            continue;
        CHECK_EQ(g_calls[i].a, TN_PHONETIC_FLUSH);   /* A = tn_phon_len, X = tn_pos, as at $B201 */
        CHECK_EQ(g_calls[i].x, 250);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_SPEECH_BUDGET);
        CHECK_EQ(g_calls[i + 1].addr, ROM_BOOT_LOOP_PLAY);
        CHECK_EQ(g_calls[i + 1].budget, FW_ROM_CALL_SPEECH_BUDGET);
    }
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
