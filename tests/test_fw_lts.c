/* Unit checks of the ported letter-to-sound routines on the converted data, with a mock host whose
   tn_append_phonetic follows the ROM routine ($B154). The full proof is the live differential test (diff_lts). */
#include <string.h>
#include "test.h"
#include "firmware/lts.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static bool g_window_was_on;

static bool mock_rom_call(void *ctx, uint16_t addr, fw_regs *r)
{
    fw *f = ctx;
    if (addr != TN_APPEND_PHONETIC)
        return false;
    if (r->a && fw_rd8(f, CMD_VOICE)) {   /* copy the r->a bytes after (tn_rule_ptr),Y to tn_phonetic */
        uint8_t x = fw_rd8(f, TN_PHON_LEN), y = r->y;
        for (uint8_t n = r->a; n; n--) {
            y++;
            fw_wr8(f, (uint16_t)(TN_PHONETIC + x), fw_ind_y(f, (uint8_t)TN_RULE_PTR, y));
            x++;
        }
        fw_wr8(f, TN_PHON_LEN, x);
    }
    r->a = 0xFF;
    return true;
}

static void mock_window(void *ctx, bool on)
{
    (void)ctx;
    if (on)
        g_window_was_on = true;
}

static void setup(const char *text)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_window_was_on = false;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    size_t n = strlen(text);
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
}

static void test_rule_skips(void)
{
    static const uint8_t rule[] = { 'B', 'A', 0x01, 'C', 'D', 0x02, 'x', 'y', 0xFF };
    setup("");
    memcpy(g_ram + 0x0300, rule, sizeof rule);
    fw_wr16(&g_f, TN_RULE_PTR, 0x0300);
    uint8_t y = 0;
    uint8_t a = tn_rule_skip_text(&g_f, &y);
    CHECK_EQ(a, 1);
    CHECK_EQ(y, 2);
    tn_rule_skip_field(&g_f, a, &y);           /* past the count and the one consumed character */
    CHECK_EQ(y, 0);
    CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), 0x0304);
    CHECK_EQ(g_ram[FW_TMP_78], 3);
    CHECK_EQ(tn_rule_skip_text(&g_f, &y), 2);  /* past "D" to the length */
    CHECK_EQ(y, 1);
}

static void test_match_symbol(void)
{
    setup("CAT ");
    g_ram[LTS_MATCH_DIR] = 1;
    g_ram[LTS_MATCH_POS] = 1;
    CHECK_EQ(lts_match_symbol(&g_f, 'A', 0), 'A');
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);
    CHECK_EQ(lts_match_symbol(&g_f, 'O', 0), 0);
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);         /* a failed letter does not step */
    g_ram[LTS_MATCH_POS] = 1;
    CHECK_EQ(lts_match_symbol(&g_f, '#', 0), 0xFF);   /* one or more vowels: A */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);
    CHECK(lts_match_symbol(&g_f, '!', 0) != 0);       /* a consonant: T */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 3);
    CHECK_EQ(lts_match_symbol(&g_f, ' ', 0), 1);      /* a non-letter: the space */
    CHECK_EQ(lts_match_symbol(&g_f, 'C', 0), 0);      /* at or past the clause end: never */
}

static void test_affix_symbols(void)
{
    setup("WALKING ");
    g_ram[LTS_MATCH_DIR] = 1;
    g_ram[LTS_MATCH_POS] = 4;
    CHECK_EQ(lts_match_symbol(&g_f, '%', 9), 1);   /* the suffix ING */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 7);
    CHECK_EQ(g_ram[FW_TMP_78], 9);                 /* Y saved there, as the ROM does */
    CHECK_EQ(fw_rd16(&g_f, LTS_AFFIX_PTR), LTS_SUFFIXES);

    setup("MATCHES");
    g_ram[LTS_MATCH_DIR] = 0;
    g_ram[LTS_MATCH_POS] = 4;                      /* backwards from the H */
    CHECK_EQ(lts_match_symbol(&g_f, '&', 0), 0x0A);   /* the sibilant CH (SH fails first) */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);
}

static void test_say_char(void)
{
    setup("A");
    g_ram[TN_POS] = 5;
    CHECK_EQ(tn_say_char(&g_f, 'A'), 5);
    CHECK_EQ(g_ram[TN_POS], 6);
    const char *name = tn_char_lists['A']->rules[0].output;
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(name));
    CHECK(memcmp(g_ram + TN_PHONETIC, name, strlen(name)) == 0);
    CHECK(!g_f.fault);
}

static void test_convert_char_low_window(void)
{
    int key = -1, second = -1;   /* the first K-Z pair that has a list */
    for (int k = 'K' - 'A'; k < 26 && key < 0; k++)
        for (int s = 0; s < 26 && key < 0; s++)
            if (lts_pair_lists[k][s]) {
                key = k;
                second = s;
            }
    CHECK(key >= 0);
    char text[4] = { (char)('A' + key), (char)('A' + second), ' ', 0 };
    setup(text);
    lts_convert_char(&g_f, (uint8_t)text[0]);
    CHECK(g_window_was_on);                        /* the K-Z lists are read with P2.7 set */
    CHECK_EQ(g_ram[LTS_LOW_BANK] & 1, 1);
    CHECK(g_ram[TN_PHON_LEN] > 0);
    CHECK(!g_f.fault);
}

void suite_fw_lts(void)
{
    CHECK(fw_image_get() != NULL);
    test_rule_skips();
    test_match_symbol();
    test_affix_symbols();
    test_say_char();
    test_convert_char_low_window();
}
