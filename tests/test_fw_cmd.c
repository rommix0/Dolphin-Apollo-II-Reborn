/* Unit checks of the command parser on the converted data (M4.2 spec 5.4), with a mock host that records every
   ROM call. The full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_match.h"
#include "firmware/cmd_scan.h"
#include "firmware/cmd_addr.h"
#include "firmware/cmd_apply.h"
#include "firmware/cmd_handlers.h"
#include "firmware/cmd_run.h"
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

/* One ROM call, with the dispatcher's RAM as the C left it at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint8_t next, next_pos;
    uint16_t vector;
} rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_before[FW_RAM_SIZE];   /* RAM as a test left it: the C must not touch it before a ROM cmd_run_next */
static unsigned g_touched;              /* ROM cmd_run_next calls that found RAM changed since g_before */
static bool g_clobber;                  /* ser_tx_put returns X ^ $40 and Y = 2 */

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
        c->next = g_ram[CMD_NEXT];
        c->next_pos = g_ram[CMD_NEXT_POS];
        c->vector = (uint16_t)(g_ram[CMD_VECTOR] | g_ram[CMD_VECTOR + 1] << 8);
    }
    g_ncalls++;
    if (addr == ROM_CMD_RUN_NEXT) {
        /* cmd_run_next: count it if the C changed RAM since g_before, then play the ROM's bookkeeping
           (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]) so that cmd_run_due moves on */
        g_touched += memcmp(g_ram, g_before, sizeof g_ram) != 0;
        uint8_t n = (uint8_t)(g_ram[CMD_NEXT] + 1);
        g_ram[CMD_NEXT] = n;
        g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS + n];
        memcpy(g_before, g_ram, sizeof g_ram);
        return FW_CALL_RETURNED;   /* X back as it was (cmd_run_done) */
    }
    if (addr == ROM_PAR_SETUP_VOICE) {
        /* par_setup_voice reads none of A, X, Y and leaves X = 9 and Y = $0B, as the ROM's does
           (frm_build_rest_frame's loop ends at X = 9, frm_clamp_mark_space's at Y = $0B) */
        r->x = 9;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_SER_TX_PUT || addr == ROM_SER_TX_HEX || addr == ROM_SER_TX_RECORDS) {
        /* the ser_tx_* routines push and pull A, X and Y: the registers come back as they went, unless g_clobber
           proves that cmd_reply_setting carries on with the X and Y that ser_tx_put returns */
        if (g_clobber && addr == ROM_SER_TX_PUT) {
            r->x ^= 0x40;
            r->y = 2;                              /* the 'B' entry of cmd_query_table */
        }
        return FW_CALL_RETURNED;
    }
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
    g_touched = 0;
    g_clobber = false;
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

/* Queues command n as cmd_scan_clause does: due at cmd_text position pos, handler address h, argument at
   boot_clause[arg]; cmd_count = n + 1, cmd_next_pos = cmd_positions[0]. */
static void queue(uint8_t n, uint8_t pos, uint16_t h, uint8_t arg)
{
    g_ram[CMD_POSITIONS + n] = pos;
    g_ram[CMD_HANDLERS + 2 * n] = (uint8_t)h;
    g_ram[CMD_HANDLERS + 2 * n + 1] = (uint8_t)(h >> 8);
    g_ram[CMD_ARG_INDEX + n] = arg;
    g_ram[CMD_COUNT] = (uint8_t)(n + 1);
    g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS];
}

static void test_run_due_nothing_queued(void)
{
    setup();                                    /* cmd_count = 0; cmd_next_pos = 0 matches X = 0 */
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(memcmp(g_ram, g_before, sizeof g_ram) == 0);
    CHECK(!g_f.fault);
}

static void test_run_due_position(void)
{
    setup();
    queue(0, 3, ROM_BANK_CMD_SELECT, 5);
    g_ram[CMD_POSITIONS + 1] = 0xFF;            /* the scan leaves it stale */
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 2);                       /* not due at 2 */
    CHECK_EQ(g_ncalls, 0);
    cmd_run_due(&g_f, 3);
    CHECK_EQ(g_ncalls, 1);
    CHECK_EQ(g_calls[0].addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_calls[0].x, 3);                  /* X = the position, as at $A88D */
    CHECK_EQ(g_calls[0].y, 0);                  /* Y = cmd_next ($A880 LDY): bank_load_ramcode saves it */
    CHECK_EQ(g_calls[0].budget, FW_ROM_CALL_BUDGET);
    CHECK_EQ(g_touched, 0);                     /* the C left the bookkeeping to the ROM */
    CHECK_EQ(g_calls[0].vector, 0);
    CHECK_EQ(g_ram[CMD_NEXT], 1);               /* the mock's (the ROM's) bookkeeping */
    cmd_run_due(&g_f, 3);                       /* cmd_next = cmd_count: nothing more */
    CHECK_EQ(g_ncalls, 1);
    CHECK(!g_f.fault);
}

/* Commands due at one position run in queue order; an unknown handler address (the latent NUL bug of the scan,
   spec 2.6) goes to the ROM like bank_cmd_select. */
static void test_run_due_queue_order(void)
{
    setup();
    queue(0, 2, ROM_BANK_CMD_SELECT, 4);
    queue(1, 2, 0x1234, 6);
    queue(2, 5, ROM_BANK_CMD_SELECT, 8);
    g_ram[CMD_POSITIONS + 3] = 0xFF;
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 2);
    CHECK_EQ(g_ncalls, 2);
    cmd_run_due(&g_f, 5);
    CHECK_EQ(g_ncalls, 3);
    for (unsigned i = 0; i < 3; i++) {
        CHECK_EQ(g_calls[i].addr, ROM_CMD_RUN_NEXT);
        CHECK_EQ(g_calls[i].x, i < 2 ? 2 : 5);
        CHECK_EQ(g_calls[i].y, i);
        CHECK_EQ(g_calls[i].next, i);
    }
    CHECK_EQ(g_touched, 0);
    CHECK_EQ(g_ram[CMD_NEXT], 3);
    CHECK(!g_f.fault);
}

/* A C handler gets cmd_run_next's writes from the C (cmd_vector, cmd_next, cmd_next_pos) and runs with
   A = boot_clause[cmd_arg_index]; a ROM handler after it in the queue still goes to the ROM. */
static void test_run_due_c_handler(void)
{
    setup();
    g_ram[BOOT_CLAUSE + 6] = '1';
    queue(0, 0, ROM_CMD_SET_SPELL, 6);
    g_ram[CMD_POSITIONS + 1] = 0x42;
    cmd_run_due(&g_f, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(g_ram[CMD_SPELL], 1);
    CHECK_EQ(fw_rd16(&g_f, CMD_VECTOR), ROM_CMD_SET_SPELL);
    CHECK_EQ(g_ram[CMD_NEXT], 1);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 0x42);

    setup();
    g_ram[BOOT_CLAUSE + 6] = '1';
    queue(0, 4, ROM_CMD_SET_SPELL, 6);
    queue(1, 4, ROM_BANK_CMD_SELECT, 9);
    g_ram[CMD_POSITIONS + 2] = 0x77;
    cmd_run_due(&g_f, 4);
    CHECK_EQ(g_ram[CMD_SPELL], 1);
    CHECK_EQ(g_ncalls, 1);                      /* only bank_cmd_select goes to the ROM */
    CHECK_EQ(g_calls[0].addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_calls[0].x, 4);
    CHECK_EQ(g_calls[0].y, 1);
    CHECK_EQ(g_calls[0].next, 1);               /* the C's bookkeeping for the first command */
    CHECK_EQ(g_calls[0].next_pos, 4);
    CHECK_EQ(g_calls[0].vector, ROM_CMD_SET_SPELL);
    CHECK_EQ(g_ram[CMD_NEXT], 2);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 0x77);
    CHECK(!g_f.fault);
}

/* The one-digit setters: accepted and rejected values (cmd_hex_value, through the case map). */
static void test_set_ranges(void)
{
    static const struct { cmd_handler *h; uint16_t setting; uint8_t ok, want, bad; } k[] = {
        { cmd_set_phrase_pause, CMD_PHRASE_PAUSE, 'f', 15, 'G' },   /* 0-$F */
        { cmd_set_word_pause,   CMD_WORD_PAUSE,   '9', 9,  'A' },   /* 0-9 */
        { cmd_set_spell,        CMD_SPELL,        '1', 1,  '2' },   /* 0-1 */
        { cmd_set_punct,        CMD_PUNCT,        '1', 1,  '2' },
        { cmd_set_hyper,        CMD_HYPER,        '1', 1,  '2' },
        { cmd_set_phonetic,     CMD_PHONETIC,     '1', 1,  '2' },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        setup();
        g_ram[k[i].setting] = 0x5A;
        k[i].h(&g_f, 3, k[i].bad);
        CHECK_EQ(g_ram[k[i].setting], 0x5A);
        k[i].h(&g_f, 3, '+');                   /* cmd_hex_value('+') = $FB */
        CHECK_EQ(g_ram[k[i].setting], 0x5A);
        k[i].h(&g_f, 3, '0');
        CHECK_EQ(g_ram[k[i].setting], 0);
        k[i].h(&g_f, 3, k[i].ok);
        CHECK_EQ(g_ram[k[i].setting], k[i].want);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

static void test_set_mark_space(void)
{
    setup();
    put_clause(" @m2a");                        /* M**: X = 4, the last digit; lower case through the map */
    cmd_set_mark_space(&g_f, 4, 'a');
    CHECK_EQ(g_ram[CMD_MARK_SPACE], 0x2A);
    CHECK_EQ(g_ram[FW_TMP_75], 0x20);           /* the first digit times 16 */

    setup();
    put_clause(" @M3G");                        /* the second digit out of range */
    g_ram[CMD_MARK_SPACE] = 0x11;
    cmd_set_mark_space(&g_f, 4, 'G');
    CHECK_EQ(g_ram[CMD_MARK_SPACE], 0x11);
    CHECK_EQ(g_ram[FW_TMP_75], 0x30);

    setup();
    put_clause(" @MM5");                        /* re-anchors on the second M: the first digit is the 5 */
    g_ram[CMD_MARK_SPACE] = 0x11;
    cmd_set_mark_space(&g_f, 4, '5');
    CHECK_EQ(g_ram[CMD_MARK_SPACE], 0x11);
    CHECK_EQ(g_ram[FW_TMP_75], 'M');            /* cmd_find_back's letter, left there */
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void check_formant_clear(void)
{
    for (int i = 0; i < 10; i++)
        CHECK_EQ(g_ram[CMD_FORMANT_OFS + i], 0);
}

static void test_set_formant(void)
{
    setup();
    put_clause(" @u312+");                      /* U#**+: X = 6, the sign */
    cmd_set_formant(&g_f, 6, '+');
    CHECK_EQ(g_ram[CMD_FORMANT_OFS + 3], 0x12);
    CHECK_EQ(g_ram[FW_TMP_75], 0x12);

    setup();
    put_clause(" @U9A0-");
    cmd_set_formant(&g_f, 6, '-');
    CHECK_EQ(g_ram[CMD_FORMANT_OFS + 9], 0x60);    /* 0 - $A0 */
    CHECK_EQ(g_ram[FW_TMP_75], 0xA0);

    setup();
    put_clause(" @U312x");                      /* neither sign: nothing stored */
    cmd_set_formant(&g_f, 6, 'x');
    check_formant_clear();
    CHECK_EQ(g_ram[FW_TMP_75], 0x12);

    setup();
    put_clause(" @U31+");                       /* U#*+: matched, but the sign fails as the low digit */
    cmd_set_formant(&g_f, 5, '+');
    check_formant_clear();
    CHECK_EQ(g_ram[FW_TMP_75], 0x10);

    setup();
    put_clause(" @U3U+");                       /* re-anchors on the second U: the parameter is the '+' */
    cmd_set_formant(&g_f, 5, '+');
    check_formant_clear();
    CHECK_EQ(g_ram[FW_TMP_75], 'U');
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static const char *version_text(const char *name)
{
    for (size_t i = 0; i < fw_phonetic_text_count; i++)
        if (!strcmp(fw_phonetic_texts[i].name, name))
            return fw_phonetic_texts[i].text;
    return NULL;
}

static void test_say_version(void)
{
    const char *e = version_text("cmd_version_engine_text"), *l = version_text("cmd_version_lang_text");
    CHECK(e != NULL && l != NULL);
    if (!e || !l)
        return;
    size_t ne = strlen(e), nl = strlen(l);
    setup();
    g_ram[CMD_VOICE] = 1;
    g_ram[TN_PHON_LEN] = 2;
    cmd_say_version(&g_f, 1, '?');
    CHECK_EQ(g_ram[TN_PHON_LEN], 2 + ne + nl);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2, e, ne) == 0);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2 + ne, l, nl) == 0);
    CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), 0xF75B);    /* left at the language text */
    CHECK_EQ(g_ram[FW_TMP_78], 0);              /* tn_append_phonetic counted down */
    CHECK_EQ(g_ncalls, 0);

    setup();                                    /* A is not '?': nothing */
    g_ram[CMD_VOICE] = 1;
    cmd_say_version(&g_f, 1, '!');
    CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), 0);
    CHECK_EQ(g_ram[TN_PHON_LEN], 0);
    CHECK(!g_f.fault);
}

static void test_ignore(void)
{
    setup();
    put_clause(" @G1");
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_ignore(&g_f, 3, '1');
    CHECK(memcmp(g_ram, g_before, sizeof g_ram) == 0);
    CHECK_EQ(g_ncalls, 0);
}

/* Call i went to addr with A, X, Y and the strict budget. */
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

static void test_apply_pitch(void)
{
    setup();
    g_ram[CMD_SOURCE] = 7;
    g_ram[CMD_PROSODY] = 2;
    g_ram[CMD_PITCH] = 9;
    uint8_t x = 0x31, y = 0x42;
    cmd_apply_pitch(&g_f, &x, &y);
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], 0x72);
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0x90);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0x90, 0x31, 0x42);   /* A = pros_pitch_centre, as at $A845 */
    CHECK_EQ(x, 9);                             /* as par_setup_voice leaves them */
    CHECK_EQ(y, 0x0B);
    CHECK(!g_f.fault);
}

static void test_apply_speed(void)
{
    static const uint8_t k_limits[9] = { 7, 7, 7, 7, 4, 2, 0, 0, 0 };   /* cmd_speed_limits */
    for (uint8_t s = 1; s <= 9; s++) {
        setup();
        g_ram[CMD_SPEED] = s;
        cmd_apply_speed(&g_f);
        CHECK_EQ(g_ram[CMD_SPEED_LIMIT], k_limits[s - 1]);
        CHECK_EQ(fw_rd16(&g_f, CMD_SPEED_MAP), 0xC898 + 32 * (s - 1));    /* the 32-byte maps from $C898 */
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

static void test_apply_speaker(void)
{
    setup();
    g_ram[CMD_SPEED] = 3;
    fw_wr16(&g_f, PAR_SPARE, 0xBEEF);
    uint8_t x = 1, y = 2;
    CHECK_EQ(cmd_apply_speaker(&g_f, &x, &y), 0);
    CHECK_EQ(g_ram[CMD_SPEAKER], 0);
    CHECK_EQ(g_ram[PAR_COUNT], 86);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xD46A);    /* par_voice_a */
    CHECK_EQ(fw_rd16(&g_f, PAR_SPARE), 0);
    CHECK_EQ(g_ram[CMD_SPEED_LIMIT], 7);
    CHECK_EQ(count(ROM_PAR_SETUP_VOICE), 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 1, 2);
    CHECK_EQ(x, 9);
    CHECK_EQ(y, 0x0B);

    setup();
    g_ram[CMD_SPEED] = 3;
    g_ram[CMD_SPEAKER] = 5;                     /* any non-zero speaker selects voice B */
    CHECK_EQ(cmd_apply_speaker(&g_f, &x, &y), 5);    /* A = the cmd_speaker read at $A78C */
    CHECK_EQ(g_ram[CMD_SPEAKER], 1);
    CHECK_EQ(g_ram[PAR_COUNT], 86);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xE5E3);    /* par_voice_b */
    CHECK(!g_f.fault);
}

static void test_clear_formant(void)
{
    setup();
    memset(g_ram + CMD_FORMANT_OFS, 0x55, 12);
    cmd_clear_formant(&g_f);
    for (int i = 0; i <= 10; i++)
        CHECK_EQ(g_ram[CMD_FORMANT_OFS + i], 0);    /* $2014-$201E: the ten offsets and par_ofs */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0x55);
}

static void test_set_pitch(void)
{
    setup();
    g_ram[CMD_NEXT] = 3;                        /* the handler's Y is cmd_next - 1 */
    g_ram[CMD_PITCH] = 0x0C;
    cmd_set_pitch(&g_f, 7, '+');
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 0x10);
    cmd_set_pitch(&g_f, 7, '-');
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 0);
    cmd_set_pitch(&g_f, 7, 'G');
    CHECK_EQ(g_ram[CMD_PITCH], 0x0C);
    CHECK_EQ(g_ncalls, 0);                      /* + - and a rejected digit do not apply */
    cmd_set_pitch(&g_f, 7, 'b');
    CHECK_EQ(g_ram[CMD_PITCH], 0x0B);
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 8);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0xB0, 7, 2);
    CHECK(!g_f.fault);
}

static void test_set_speed(void)
{
    setup();
    g_ram[CMD_SPEED] = 3;
    g_ram[CMD_SPEED_LIMIT] = 0xEE;
    cmd_set_speed(&g_f, 3, '0');                /* 1-9 only */
    cmd_set_speed(&g_f, 3, 'A');
    CHECK_EQ(g_ram[CMD_SPEED], 3);
    CHECK_EQ(g_ram[CMD_SPEED_LIMIT], 0xEE);
    cmd_set_speed(&g_f, 3, '9');
    CHECK_EQ(g_ram[CMD_SPEED], 9);
    CHECK_EQ(g_ram[CMD_SPEED_LIMIT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_SPEED_MAP), 0xC998);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_set_source(void)
{
    setup();
    g_ram[CMD_NEXT] = 1;
    g_ram[CMD_PROSODY] = 3;
    cmd_set_source(&g_f, 4, '8');               /* 0-7 only */
    CHECK_EQ(g_ram[CMD_SOURCE], 0);
    CHECK_EQ(g_ncalls, 0);
    cmd_set_source(&g_f, 4, '7');
    CHECK_EQ(g_ram[CMD_SOURCE], 7);
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], 0x73);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 4, 0);
    CHECK(!g_f.fault);
}

static void test_set_speaker(void)
{
    setup();
    g_ram[CMD_SPEED] = 3;
    g_ram[CMD_NEXT] = 1;
    cmd_set_speaker(&g_f, 4, '2');              /* 0 or 1 only */
    CHECK_EQ(g_ram[CMD_SPEAKER], 0);
    CHECK_EQ(g_ncalls, 0);
    cmd_set_speaker(&g_f, 4, '1');
    CHECK_EQ(g_ram[CMD_SPEAKER], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xE5E3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 4, 0);
    CHECK(!g_f.fault);
}

static void test_set_voice(void)
{
    static const struct { uint8_t voicing, mark_space, pitch, speaker, source; } k_want[6] = {
        { 8, 0x14, 8, 0, 0 },    { 8, 0x28, 6, 0, 0 },    { 7, 0x0A, 9, 0, 0 },
        { 7, 0x14, 0x0B, 1, 7 }, { 6, 0x0A, 0x0A, 1, 7 }, { 8, 0x14, 0x0C, 1, 7 },
    };
    for (uint8_t v = 1; v <= 6; v++) {
        setup();
        g_ram[CMD_SPEED] = 3;
        g_ram[CMD_FORMANT_OFS + 4] = 0x33;
        cmd_set_voice(&g_f, 2, (uint8_t)('0' + v));
        CHECK_EQ(g_ram[CMD_VOICE], v);
        CHECK_EQ(g_ram[CMD_VOICING], k_want[v - 1].voicing);
        CHECK_EQ(g_ram[CMD_MARK_SPACE], k_want[v - 1].mark_space);
        CHECK_EQ(g_ram[CMD_PITCH], k_want[v - 1].pitch);
        CHECK_EQ(g_ram[CMD_SPEAKER], k_want[v - 1].speaker);
        CHECK_EQ(g_ram[CMD_SOURCE], k_want[v - 1].source);
        CHECK_EQ(g_ram[CMD_FORMANT_OFS + 4], 0);
        CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), v <= 3 ? 0xD46A : 0xE5E3);
        CHECK_EQ(g_ncalls, 1);
        check_call(0, ROM_PAR_SETUP_VOICE, (uint8_t)(k_want[v - 1].pitch << 4), 2, 0xFF);
        CHECK(!g_f.fault);
    }

    setup();                                    /* @V0: the voice off, no apply ($AA7A BNE on Z) */
    g_ram[CMD_VOICE] = 1;
    g_ram[CMD_PITCH] = 0x0C;
    g_ram[CMD_FORMANT_OFS] = 0x33;
    cmd_set_voice(&g_f, 2, '0');
    CHECK_EQ(g_ram[CMD_VOICE], 0);
    CHECK_EQ(g_ram[CMD_FORMANT_OFS], 0);
    CHECK_EQ(g_ram[CMD_PITCH], 0x0C);
    CHECK_EQ(g_ncalls, 0);

    setup();                                    /* 7: no preset; only the formant offsets are cleared */
    g_ram[CMD_VOICE] = 1;
    g_ram[CMD_FORMANT_OFS] = 0x33;
    cmd_set_voice(&g_f, 2, '7');
    CHECK_EQ(g_ram[CMD_VOICE], 1);
    CHECK_EQ(g_ram[CMD_FORMANT_OFS], 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_set_prosody(void)
{
    setup();
    put_clause(" @R3");                         /* R#: X = 3 */
    g_ram[CMD_NEXT] = 1;
    cmd_set_prosody(&g_f, 3, '3');
    CHECK_EQ(g_ram[CMD_PROSODY], 3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 3, 0);   /* X = cmd_find_back's */

    setup();
    put_clause(" @RR12");                       /* R***: re-anchors on the second R, so the digit path */
    cmd_set_prosody(&g_f, 5, '2');
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 1);

    setup();
    put_clause(" @RA80");                       /* R***: letter A, hex $80 */
    cmd_set_prosody(&g_f, 5, '0');
    CHECK_EQ(g_ram[PROS_PARAMS + 0], 0x80);
    CHECK_EQ(g_ram[FW_TMP_76], 0);
    CHECK_EQ(g_ram[FW_TMP_34], '8');
    CHECK_EQ(g_ram[FW_TMP_35], '0');
    CHECK_EQ(g_ram[FW_TMP_1809], 0x80);         /* cmd_hex_byte's scratch: the high digit times 16 */
    CHECK_EQ(g_ncalls, 0);                      /* the letter path does not apply */

    setup();
    put_clause(" @rz1f");                       /* lower case through cmd_upcase: Z (25), $1F */
    cmd_set_prosody(&g_f, 5, 'f');
    CHECK_EQ(g_ram[PROS_PARAMS + 25], 0x1F);
    CHECK_EQ(g_ram[FW_TMP_35], 'F');

    setup();
    put_clause(" @R9");                         /* 8 and 9 take the letter path and fail it */
    g_ram[FW_TMP_76] = 0x99;
    cmd_set_prosody(&g_f, 3, '9');
    CHECK_EQ(g_ram[CMD_PROSODY], 0);
    CHECK_EQ(g_ram[FW_TMP_76], 0x99);
    CHECK_EQ(g_ram[FW_TMP_75], 'R');
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_sound(void)
{
    setup();
    put_clause(" @T5 ");
    g_ram[TN_PHON_LEN] = 0x2A;
    g_ram[CMD_EV_COUNT] = 1;
    g_ram[CMD_EV_POSITIONS + 3] = 0x77;            /* high bytes of positions 1 and 2: never written */
    g_ram[CMD_EV_POSITIONS + 5] = 0x77;
    cmd_sound(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 3);              /* two events */
    CHECK_EQ(g_ram[CMD_EV_ARGS + 1], 5);
    CHECK_EQ(g_ram[CMD_EV_ARGS + 2], 5);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 2], 0x2A);   /* low byte = tn_phon_len */
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 3], 0x77);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 4], 0x2A);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 5], 0x77);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 2), ROM_CMD_EV_SOUND_START);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 4), ROM_CMD_EV_SOUND_END);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* Accepted arguments queue with the value; rejected ones leave the queue untouched (-1). */
typedef struct { char c; int want; } event_arg;

static void check_event_args(cmd_handler *h, const event_arg *k, size_t n, uint8_t events, uint16_t handler)
{
    for (size_t i = 0; i < n; i++) {
        setup();
        g_ram[TN_PHON_LEN] = 0x10;
        h(&g_f, 3, (uint8_t)k[i].c);
        if (k[i].want < 0) {
            CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
            CHECK_EQ(g_ram[CMD_EV_ARGS], 0);
            CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0);
            CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), 0);
        } else {
            CHECK_EQ(g_ram[CMD_EV_COUNT], events);
            CHECK_EQ(g_ram[CMD_EV_ARGS], k[i].want);
            CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0x10);
            CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), handler);
        }
        CHECK(!g_f.fault);
    }
}

static void test_event_args(void)
{
    /* @T: 0-$10 and 'X' ($21 -> $10); 'G' is 16 too; 'H' (17), ':' (35) and '+' ($FB) are rejected */
    static const event_arg k_sound[] = { { '0', 0 }, { 'f', 15 }, { 'x', 16 }, { 'X', 16 }, { 'G', 16 },
                                         { 'H', -1 }, { ':', -1 }, { '+', -1 } };
    /* @A: 0-F */
    static const event_arg k_volume[] = { { '0', 0 }, { '6', 6 }, { 'f', 15 }, { 'G', -1 }, { ':', -1 } };
    /* @B: 0-9 */
    static const event_arg k_voicing[] = { { '0', 0 }, { '9', 9 }, { 'A', -1 }, { 'x', -1 } };
    check_event_args(cmd_sound, k_sound, sizeof k_sound / sizeof k_sound[0], 2, ROM_CMD_EV_SOUND_START);
    check_event_args(cmd_set_volume, k_volume, sizeof k_volume / sizeof k_volume[0], 1, ROM_CMD_EV_SET_VOLUME);
    check_event_args(cmd_set_voicing, k_voicing, sizeof k_voicing / sizeof k_voicing[0], 1, ROM_CMD_EV_SET_VOICING);
}

static void test_mark_index(void)
{
    setup();
    put_clause(" @I+ ");
    g_ram[TN_PHON_LEN] = 0x12;
    g_ram[CMD_EV_COUNT] = 2;
    g_ram[CMD_EV_ARGS + 2] = 0x99;                 /* stale: @I+ writes no argument */
    cmd_mark_index(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 3);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 4], 0x12);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 4), ROM_CMD_EV_INDEX);
    CHECK_EQ(g_ram[CMD_EV_ARGS + 2], 0x99);
    CHECK_EQ(g_ncalls, 0);
    setup();
    cmd_mark_index(&g_f, 3, '^');                  /* only a raw '+' queues */
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), 0);
    CHECK(!g_f.fault);
}

static void test_event_index_wrap(void)
{
    /* @A at n = 128: the argument goes to $1406 + 128 = $1486 = cmd_count; position and handler wrap to entry 0 */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 128;
    g_ram[CMD_COUNT] = 5;
    cmd_set_volume(&g_f, 3, '7');
    CHECK_EQ(g_ram[CMD_COUNT], 7);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0x31);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), ROM_CMD_EV_SET_VOLUME);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 129);

    /* @T at n = 127: X = $FE. The end event's offset is in the instruction ($1208,X / $1308,X), so it does not
       wrap: its position lands on cmd_ev_handlers[0]'s low byte ($1306), its handler on cmd_ev_args[0..1] */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 127;
    cmd_sound(&g_f, 3, '3');
    CHECK_EQ(g_ram[CMD_EV_ARGS + 127], 3);         /* $1485 */
    CHECK_EQ(g_ram[CMD_COUNT], 3);                 /* $1486 */
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 0xFE], 0x31);                             /* $1304 */
    CHECK_EQ(g_ram[CMD_EV_HANDLERS], 0x31);                                     /* $1306 = $1208 + $FE */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 0xFE), ROM_CMD_EV_SOUND_START);    /* $1404 */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_ARGS), ROM_CMD_EV_SOUND_END);                 /* $1406 = $1308 + $FE */
    CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0);          /* entry 0's position untouched */
    CHECK_EQ(g_ram[CMD_EV_COUNT], 129);

    /* @B at n = 255: the argument at $1406 + 255 = $1505 (inside cmd_positions); X = $FE; the count wraps to 0 */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 255;
    cmd_set_voicing(&g_f, 3, '4');
    CHECK_EQ(g_ram[CMD_EV_ARGS + 255], 4);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 0xFE], 0x31);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 0xFE), ROM_CMD_EV_SET_VOICING);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);

    /* @I+ at n = 128: entry 0, and no argument (cmd_count stays) */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 128;
    g_ram[CMD_COUNT] = 5;
    cmd_mark_index(&g_f, 3, '+');
    CHECK_EQ(g_ram[CMD_COUNT], 5);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0x31);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), ROM_CMD_EV_INDEX);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 129);
    CHECK(!g_f.fault);
}

static void test_echo(void)
{
    setup();
    put_clause(" @E! ");
    g_ram[CMD_NEXT] = 1;                           /* the dispatcher advanced it: Y = cmd_next - 1 = 0 */
    cmd_echo(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_SER_TX_PUT, '!', 3, 0);
    CHECK(!g_f.fault);
}

static void test_query_known(void)
{
    setup();
    put_clause(" @A? ");
    g_ram[CMD_NEXT] = 1;
    g_ram[CMD_VOLUME] = 6;
    g_ram[TN_RESUME_POS] = 0x99;
    cmd_query(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);    /* X = the '?'; the letter is boot_clause[X - 1] */
    CHECK_EQ(g_ram[FW_TMP_75], 'A');
    CHECK_EQ(g_ram[TN_RESUME_POS], 3);             /* X parked in $77 */
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, 'A', 3, 0);      /* Y = the table offset */
    check_call(1, ROM_SER_TX_HEX, 6, 3, 0);        /* cmd_volume ($14) */

    setup();
    put_clause(" @$? ");                           /* the last entry: offset 34, cmd_source ($1C) */
    g_ram[CMD_SOURCE] = 0x42;
    cmd_query(&g_f, 3, '?');
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, '$', 3, 34);
    check_call(1, ROM_SER_TX_HEX, 0x42, 3, 34);

    setup();                                       /* X = 0 reads $1AFF = cmd_text[255] */
    g_ram[CMD_TEXT + 255] = 'W';
    g_ram[CMD_SPEED] = 3;
    cmd_query(&g_f, 0, 0);
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, 'W', 0, 26);
    check_call(1, ROM_SER_TX_HEX, 3, 0, 26);
    CHECK(!g_f.fault);
}

static void test_query_unknown(void)
{
    static const char *const k_texts[] = { " @N? ", " @a? ", " @?? " };   /* not listed; lower case is raw */
    for (size_t i = 0; i < sizeof k_texts / sizeof k_texts[0]; i++) {
        setup();
        put_clause(k_texts[i]);
        g_ram[TN_RESUME_POS] = 0x99;
        cmd_query(&g_f, 3, '?');
        CHECK_EQ(g_ram[FW_TMP_75], (uint8_t)k_texts[i][2]);
        CHECK_EQ(g_ram[TN_RESUME_POS], 0x99);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

/* cmd_reply_setting carries on with the X and Y that ser_tx_put returns (the ROM relies on it preserving them) */
static void test_query_returned_regs(void)
{
    setup();
    put_clause(" @A? ");
    g_clobber = true;
    g_ram[CMD_VOLUME] = 6;
    g_ram[CMD_VOICING] = 9;
    cmd_query(&g_f, 3, '?');
    CHECK_EQ(g_ram[TN_RESUME_POS], 0x43);          /* STX $77 after the call */
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, 'A', 3, 0);
    check_call(1, ROM_SER_TX_HEX, 9, 0x43, 2);     /* LDA $9990,Y with the returned Y = 2: cmd_voicing ($13) */
    CHECK(!g_f.fault);
}

static void test_list_languages(void)
{
    setup();
    put_clause(" @L ");
    g_ram[CMD_NEXT] = 1;
    cmd_list_languages(&g_f, 2, g_ram[BOOT_CLAUSE + 2]);
    CHECK_EQ(g_ram[SER_REPLY_ID], 'L');
    CHECK_EQ(g_ram[SER_REPLY_COUNT], 7);
    CHECK_EQ(g_ram[SER_REPLY_SIZE], 14);
    CHECK_EQ(fw_rd16(&g_f, SER_REPLY_PTR), BANK_ID_TABLE);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_SER_TX_RECORDS, 'L', 2, 0);
    CHECK(!g_f.fault);
}

/* Every handler of this task runs as C through the dispatcher; bank_cmd_select still falls back to ROM. */
static void test_run_due_all_rows(void)
{
    static const uint16_t k_addrs[] = { ROM_CMD_SOUND, ROM_CMD_SET_VOLUME, ROM_CMD_SET_VOICING, ROM_CMD_MARK_INDEX,
                                        ROM_CMD_ECHO, ROM_CMD_QUERY, ROM_CMD_LIST_LANGUAGES, ROM_BANK_CMD_SELECT };
    for (size_t i = 0; i < sizeof k_addrs / sizeof k_addrs[0]; i++) {
        setup();
        put_clause(" @A5 ");
        g_ram[CMD_COUNT] = 1;                      /* one queued command at position 0, argument index 3 */
        g_ram[CMD_NEXT_POS] = 0;
        g_ram[CMD_POSITIONS + 1] = 0xFF;
        fw_wr16(&g_f, CMD_HANDLERS, k_addrs[i]);
        g_ram[CMD_ARG_INDEX] = 3;
        cmd_run_due(&g_f, 0);
        CHECK_EQ(g_ram[CMD_NEXT], 1);
        CHECK_EQ(g_ram[CMD_NEXT_POS], 0xFF);
        CHECK_EQ(count(ROM_CMD_RUN_NEXT), k_addrs[i] == ROM_BANK_CMD_SELECT ? 1u : 0u);
        CHECK(!g_f.fault);
    }
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
    test_run_due_nothing_queued();
    test_run_due_position();
    test_run_due_queue_order();
    test_run_due_c_handler();
    test_set_ranges();
    test_set_mark_space();
    test_set_formant();
    test_say_version();
    test_ignore();
    test_apply_pitch();
    test_apply_speed();
    test_apply_speaker();
    test_clear_formant();
    test_set_pitch();
    test_set_speed();
    test_set_source();
    test_set_speaker();
    test_set_voice();
    test_set_prosody();
    test_sound();
    test_event_args();
    test_mark_index();
    test_event_index_wrap();
    test_echo();
    test_query_known();
    test_query_unknown();
    test_query_returned_regs();
    test_list_languages();
    test_run_due_all_rows();
}
