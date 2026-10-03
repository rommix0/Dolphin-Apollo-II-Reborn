/* Unit checks of the command parser on the converted data (M4.2 spec 5.4), with a mock host that records every
   ROM call. The full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_match.h"
#include "firmware/cmd_scan.h"
#include "firmware/cmd_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

/* cmd_table entries (match order, spec 4) */
enum {
    P_VERSION = 0,          /* ?    */
    P_QUERY = 1,            /* *?   */
    P_VOLUME = 2,           /* A*   */
    P_HYPER = 6,            /* H#   */
    P_MARK_SPACE = 7,       /* M**  */
    P_PROSODY = 10,         /* R#   */
    P_PROSODY_LETTER = 11,  /* R*** */
    P_SPEED = 14,           /* W#   */
    P_IGNORE_G = 27,        /* G#, listed twice (27 and 28) */
    P_END = 31              /* the $FF end marker */
};

typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call and answers the routines the C calls as the ROM would; any other call fails. */
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
    /* isr_par_ready leaves A, X and Y as they were: the registers come back unchanged */
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* no command routine in C drives P2 (bank_cmd_select stays in ROM) */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* Entry i of a pointer table (cmd_table, cmd_imm_table) in the converted data: the address of its pattern. */
static uint16_t entry(uint16_t table, unsigned i)
{
    return (uint16_t)(fw_rom8(&g_f, (uint16_t)(table + 2 * i)) | fw_rom8(&g_f, (uint16_t)(table + 2 * i + 1)) << 8);
}

/* cmd_text_ptr = boot_clause + 2, holding text and then zeros; cmd_pat_ptr = pat. */
static void point(const char *text, uint16_t pat)
{
    memset(g_ram + BOOT_CLAUSE, 0, 256);
    memcpy(g_ram + BOOT_CLAUSE + 2, text, strlen(text));
    fw_wr16(&g_f, CMD_TEXT_PTR, BOOT_CLAUSE + 2);
    fw_wr16(&g_f, CMD_PAT_PTR, pat);
}

/* point(text), with cmd_table_ptr = table and cmd_table_ofs = ofs. */
static void table_setup(uint16_t table, uint8_t ofs, const char *text)
{
    point(text, 0);
    fw_wr16(&g_f, CMD_TABLE_PTR, table);
    g_ram[CMD_TABLE_OFS] = ofs;
}

static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Every ROM call is isr_par_ready with the strict budget. */
static void check_par_ready_calls(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++) {
        CHECK_EQ(g_calls[i].addr, ROM_ISR_PAR_READY);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
    }
}

/* boot_clause = s (the bytes after it unchanged). */
static void put_clause(const char *s) { memcpy(g_ram + BOOT_CLAUSE, s, strlen(s)); }

/* setup(), then boot_clause = clause (then zeros), boot_clause_len = its length, cmd_char = '@'. */
static void scan_setup(const char *clause)
{
    setup();
    put_clause(clause);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)strlen(clause);
    g_ram[CMD_CHAR] = '@';
}

static void test_table_layout(void)  /* the entries the tests below rely on */
{
    setup();
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_VERSION)), '?');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_QUERY)), '*');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_VOLUME)), 'A');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_HYPER)), 'H');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_MARK_SPACE)), 'M');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_PROSODY)), 'R');
    CHECK_EQ(fw_rom8(&g_f, (uint16_t)(entry(CMD_TABLE, P_PROSODY_LETTER) + 3)), '*');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_SPEED)), 'W');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_IGNORE_G)), 'G');
    CHECK_EQ(entry(CMD_TABLE, P_IGNORE_G + 1), entry(CMD_TABLE, P_IGNORE_G));
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_END)), 0xFF);
    CHECK(!g_f.fault);
}

static void test_upcase(void)
{
    setup();
    CHECK_EQ(cmd_upcase(&g_f, 'a'), 'A');
    CHECK_EQ(cmd_upcase(&g_f, 'Q'), 'Q');
    CHECK_EQ(cmd_upcase(&g_f, '['), '(');
    CHECK_EQ(cmd_upcase(&g_f, 0x05), ' ');     /* control codes become spaces ... */
    CHECK_EQ(cmd_upcase(&g_f, 0x0D), 0x0D);    /* ... except CR and ESC */
    CHECK_EQ(cmd_upcase(&g_f, 0x87), 0x80);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The value mapping, with the result-0 case ('0' -> 0, where the ROM exits with Z set) and non-zero ones. The
   thunk's Z output (FW_OUT_Z: Z = result == 0) is not visible here; diff_lts compares it against the ROM. */
static void test_hex_value(void)
{
    static const struct { uint8_t c, v; } k[] = {
        { '0', 0 }, { '9', 9 }, { 'a', 10 }, { 'A', 10 }, { 'Z', 35 }, { 'z', 35 },
        { ':', 35 }, { '@', 35 },              /* $C52B: ':'..'@' count as 'Z' */
        { '+', 0xFB }, { '[', 0xF8 },          /* below '0' (after the case map: '[' is '(') it wraps */
        { 0x87, 0x49 }, { 0xE0, 0xB0 },        /* BMI on c - 'A': the case map's $80 gives $49, $E0 gives $B0 */
    };
    setup();
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        CHECK_EQ(cmd_hex_value(&g_f, k[i].c), k[i].v);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_hex_byte(void)
{
    setup();
    g_ram[FW_TMP_34] = '8';
    g_ram[FW_TMP_35] = '0';
    CHECK_EQ(cmd_hex_byte(&g_f), 0x80);
    CHECK_EQ(g_ram[FW_TMP_1809], 0x80);        /* the high nibble, kept in $1809 */
    g_ram[FW_TMP_34] = 'a';
    g_ram[FW_TMP_35] = 'f';
    CHECK_EQ(cmd_hex_byte(&g_f), 0xAF);
    CHECK_EQ(g_ram[FW_TMP_1809], 0xA0);
    g_ram[FW_TMP_34] = 'G';                    /* 16: shifted out of the byte, as in the ROM */
    g_ram[FW_TMP_35] = '1';
    CHECK_EQ(cmd_hex_byte(&g_f), 0x01);
    CHECK_EQ(g_ram[FW_TMP_1809], 0x00);
    CHECK(!g_f.fault);
}

static void test_find_back(void)
{
    setup();
    memcpy(g_ram + BOOT_CLAUSE, " @m45", 5);
    CHECK_EQ(cmd_find_back(&g_f, 'M', 4), 3);  /* the lower-case letter matches through the case map; X = after it */
    CHECK_EQ(g_ram[FW_TMP_75], 'M');
    memcpy(g_ram + BOOT_CLAUSE, " @MM5", 5);
    CHECK_EQ(cmd_find_back(&g_f, 'M', 4), 4);  /* an argument holding the letter re-anchors on it (@MM5) */
    memcpy(g_ram + BOOT_CLAUSE, " Mab", 4);
    CHECK_EQ(cmd_find_back(&g_f, 'M', 3), 2);  /* index 1 is still searched */
    memcpy(g_ram + BOOT_CLAUSE, "M@ab", 4);
    g_ram[FW_TMP_75] = 0;
    CHECK_EQ(cmd_find_back(&g_f, 'M', 3), 1);  /* not found down to index 1: X = 1; index 0 is never read */
    CHECK_EQ(g_ram[FW_TMP_75], 'M');
    CHECK(!g_f.fault);
}

static void test_match_pattern(void)
{
    setup();
    point("Ax", entry(CMD_TABLE, P_VOLUME));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* '*' takes anything */
    point("a!", entry(CMD_TABLE, P_VOLUME));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* the text is case-mapped */
    point("h5", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* '#' takes a digit */
    point("Hx", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);
    point("H:", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* '9' + 1 */
    point("H/", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* '0' - 1 */
    point("Q5", entry(CMD_TABLE, P_SPEED));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* mismatch */
    point("M12345", entry(CMD_TABLE, P_MARK_SPACE));
    CHECK_EQ(cmd_match_pattern(&g_f), 3);      /* the pattern's zero ends a full match */
    point("M1", entry(CMD_TABLE, P_MARK_SPACE));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* a zero in the text: partial, Y = the index of the zero */
    point("R", entry(CMD_TABLE, P_PROSODY_LETTER));
    CHECK_EQ(cmd_match_pattern(&g_f), 1);
    point("", entry(CMD_TABLE, P_VOLUME));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* a zero at once: 0, as a mismatch */
    point("?", entry(CMD_TABLE, P_VERSION));
    CHECK_EQ(cmd_match_pattern(&g_f), 1);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_match_table(void)
{
    setup();
    table_setup(CMD_TABLE, 0, "W5 ");
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_SPEED);           /* left at the matching entry */
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_SPEED));

    table_setup(CMD_TABLE, 0, "r5AB");                      /* R# and R*** both match: the first wins */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_PROSODY);

    table_setup(CMD_TABLE, 0, "w?");                        /* *? comes before W#: a query */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_QUERY);

    table_setup(CMD_TABLE, 0, "g1");                        /* the duplicate G#: the first copy */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_IGNORE_G);

    table_setup(CMD_TABLE, 0, "R");                         /* a zero after the letter: *? matches partially */
    CHECK_EQ(cmd_match_table(&g_f), 1);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_QUERY);

    table_setup(CMD_TABLE, 0, "Zq");                        /* nothing matches: Y = 0 at the $FF end */
    CHECK_EQ(cmd_match_table(&g_f), 0);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_END);
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_END));
    CHECK_EQ(fw_rom8(&g_f, fw_rd16(&g_f, CMD_PAT_PTR)), 0xFF);

    table_setup(CMD_IMM_TABLE, 0, "I+");                    /* the immediate table */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 0);
    table_setup(CMD_IMM_TABLE, 0, "I?");
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2);
    table_setup(CMD_IMM_TABLE, 0, "I");                     /* partial: cmd_imm_match waits for the next byte */
    CHECK_EQ(cmd_match_table(&g_f), 1);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 0);
    table_setup(CMD_IMM_TABLE, 2, "I+");                    /* resumed from entry 1 (ser_cmd_table_ofs) */
    CHECK_EQ(cmd_match_table(&g_f), 0);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 4);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_scan_command(void)
{
    scan_setup(" @W5Hi");
    g_ram[CMD_COUNT] = 7;                       /* both reset by the scan */
    g_ram[CMD_NEXT] = 7;
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 3);        /* the cmd_text length */
    CHECK(memcmp(g_ram + CMD_TEXT, " HI", 3) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 1);
    CHECK_EQ(g_ram[CMD_NEXT], 0);
    CHECK_EQ(g_ram[CMD_POSITIONS], 1);          /* cut out at cmd_text position 1 */
    CHECK_EQ(g_ram[CMD_NEXT_POS], 1);
    CHECK_EQ(g_ram[CMD_ARG_INDEX], 3);          /* the boot_clause index of '5' */
    CHECK_EQ(fw_rd16(&g_f, CMD_HANDLERS), ROM_CMD_SET_SPEED);
    CHECK_EQ(g_ram[FW_TMP_76], 4);              /* the resume index */
    CHECK_EQ(g_ram[FW_TMP_78], 2);              /* last written by the case map of 'i' (Y = 2) */
    CHECK_EQ(fw_rd16(&g_f, CMD_TEXT_PTR), BOOT_CLAUSE + 2);
    CHECK_EQ(fw_rd16(&g_f, CMD_TABLE_PTR), CMD_TABLE);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_SPEED);
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_SPEED));
    /* isr_par_ready once per character read (X = 0, 1, 4, 5), with the registers the ROM has at $AD09: A = $FF,
       then the last byte stored (' ', the handler's high byte, 'H') */
    static const uint8_t want_a[] = { 0xFF, ' ', ROM_CMD_SET_SPEED >> 8, 'H' };
    static const uint8_t want_x[] = { 0, 1, 4, 5 };
    static const uint8_t want_y[] = { 0, 1, 1, 2 };
    CHECK_EQ(g_ncalls, 4);
    for (unsigned i = 0; i < 4 && i < g_ncalls; i++) {
        CHECK_EQ(g_calls[i].a, want_a[i]);
        CHECK_EQ(g_calls[i].x, want_x[i]);
        CHECK_EQ(g_calls[i].y, want_y[i]);
    }
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_two_commands(void)
{
    scan_setup(" @W5@A3Go");
    g_ram[CMD_POSITIONS + 2] = 0x77;            /* cmd_positions[cmd_count] is not reset */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 3);
    CHECK(memcmp(g_ram + CMD_TEXT, " GO", 3) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 2);
    CHECK_EQ(g_ram[CMD_POSITIONS], 1);          /* both due at position 1, in queue order */
    CHECK_EQ(g_ram[CMD_POSITIONS + 1], 1);
    CHECK_EQ(g_ram[CMD_POSITIONS + 2], 0x77);
    CHECK_EQ(g_ram[CMD_ARG_INDEX], 3);
    CHECK_EQ(g_ram[CMD_ARG_INDEX + 1], 6);
    CHECK_EQ(fw_rd16(&g_f, CMD_HANDLERS), ROM_CMD_SET_SPEED);
    CHECK_EQ(fw_rd16(&g_f, CMD_HANDLERS + 2), ROM_CMD_SET_VOLUME);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 1);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 5);      /* X = 0, 1, 4, 7, 8 */
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_unmatched(void)
{
    scan_setup(" @Zq");                         /* no pattern matches "Zq" */
    g_ram[FW_TMP_76] = 0x99;
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 4);
    CHECK(memcmp(g_ram + CMD_TEXT, " @ZQ", 4) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 0);
    CHECK_EQ(g_ram[CMD_POSITIONS], 0xFF);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 0xFF);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_END);
    CHECK_EQ(g_ram[FW_TMP_76], 0x99);           /* not written on this path */
    CHECK_EQ(g_ram[FW_TMP_78], 3);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 4);

    scan_setup(" zZq");                         /* the cmd_char is stored raw, not case-mapped */
    g_ram[CMD_CHAR] = 'z';
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 4);
    CHECK(memcmp(g_ram + CMD_TEXT, " zZQ", 4) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 0);
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_case_map(void)
{
    scan_setup(" a[b]\x05\x7F");
    cmd_scan_clause(&g_f);
    static const uint8_t want[] = { ' ', 'A', '(', 'B', ')', 0x05, ' ' };   /* codes below $21 stay raw */
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], sizeof want);
    CHECK(memcmp(g_ram + CMD_TEXT, want, sizeof want) == 0);
    CHECK_EQ(g_ram[FW_TMP_78], 6);              /* the Y of the last mapped character (DEL) */
    CHECK(!g_f.fault);
}

static void test_scan_phonetic(void)
{
    scan_setup(" ab[@W5c");
    g_ram[CMD_PHONETIC] = 1;                    /* @X1: copied raw, commands still cut out */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 5);
    CHECK(memcmp(g_ram + CMD_TEXT, " ab[c", 5) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 1);
    CHECK_EQ(g_ram[CMD_POSITIONS], 4);

    scan_setup(" ab[");
    g_ram[CMD_PHONETIC] = 2;                    /* BBS0: only bit 0 counts */
    cmd_scan_clause(&g_f);
    CHECK(memcmp(g_ram + CMD_TEXT, " AB(", 4) == 0);
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_nul_partial(void)
{
    scan_setup(" @R");                          /* a zero after the letter: *? matches partially (Y = 1) */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[CMD_COUNT], 1);              /* queued as if it had matched (the latent ROM bug) */
    CHECK_EQ(g_ram[CMD_POSITIONS], 1);
    CHECK_EQ(g_ram[CMD_ARG_INDEX], 2);
    CHECK_EQ(g_ram[FW_TMP_76], 3);
    uint16_t pat = entry(CMD_TABLE, P_QUERY);   /* the handler bytes come from inside "*?" */
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), pat);
    CHECK_EQ(g_ram[CMD_HANDLERS], fw_rom8(&g_f, (uint16_t)(pat + 2)));
    CHECK_EQ(g_ram[CMD_HANDLERS + 1], fw_rom8(&g_f, (uint16_t)(pat + 3)));
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 1);
    CHECK(!g_f.fault);
}

static void test_scan_empty(void)
{
    scan_setup(" ");
    g_ram[BOOT_CLAUSE_LEN] = 0;                 /* do-while: one character is still read */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 1);
    CHECK_EQ(g_ram[CMD_TEXT], ' ');
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1);
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

void suite_fw_cmd(void)
{
    CHECK(fw_image_get() != NULL);
    test_table_layout();
    test_upcase();
    test_hex_value();
    test_hex_byte();
    test_find_back();
    test_match_pattern();
    test_match_table();
    test_scan_command();
    test_scan_two_commands();
    test_scan_unmatched();
    test_scan_case_map();
    test_scan_phonetic();
    test_scan_nul_partial();
    test_scan_empty();
}
