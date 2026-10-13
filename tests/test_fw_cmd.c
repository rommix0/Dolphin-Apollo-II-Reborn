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
#include "firmware/frm_build.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024
#define NUL_BUG_ADDR 0xD300   /* a handler address of the scan's NUL bug: "@A" then a zero (M4.6c1 spec 2.5) */
#define NUL_BUG_ZP   0x002A   /* another: "@M5" then a zero */

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
static uint8_t g_p0;                    /* the port P0 the C isr_par_ready reads and writes in parallel mode */
static uint8_t g_p2;                    /* the port P2 the C bank_cmd_select reads and writes */
static unsigned g_hooked;               /* ROM calls through rom_call (hooked): the C makes none since M4.6c1 */

/* Records every unhooked ROM call and answers the one the C makes, the ROM cmd_run_next of the NUL-bug fallback, as
   the ROM would; any other call fails. The reply handlers call the C ser_tx_* routines (M4.6a), which make no ROM
   call. */
static fw_call_result mock_rom_call_unhooked(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
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
    return FW_CALL_FAILED;
}

/* A hooked ROM call: none is made (isr_par_ready is C since M4.6b, cmd_run_next since M4.6c1); counted, failed. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    g_hooked++;
    return FW_CALL_FAILED;
}

/* Another bank's ROM, for bank_cmd_select: every socket is empty. */
static uint8_t mock_read_bank(void *ctx, uint16_t addr)
{
    (void)ctx;
    (void)addr;
    return 0xFF;
}

/* The C isr_par_ready (M4.6b) in parallel mode (scan_setup sets boot_dip bit 0): SEB3 P0, one read and one write of
   P0 ($F6), recorded as a call of ROM_ISR_PAR_READY without registers (the C passes them to nothing). P2 ($F0) is the
   bank_cmd_select's (g_p2). Any other SFR access fails the test. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    if (addr == 0xF0)
        return g_p2;
    CHECK_EQ(addr, 0xF6);
    return g_p0;
}

static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    if (addr == 0xF0) {
        g_p2 = value;
        return;
    }
    CHECK_EQ(addr, 0xF6);
    CHECK(value & 0x08);
    g_p0 = value;
    if (g_ncalls < MAX_CALLS)
        g_calls[g_ncalls] = (rom_call_record){ ROM_ISR_PAR_READY, 0, 0, 0, 0, 0, 0, 0 };
    g_ncalls++;
}

/* Zeroed RAM with the output ring empty at $3900 (as boot_clear_input_state leaves it, $AC09-$AC18), the mock
   host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* no command routine in C switches the window */
    g_host.read_sfr = mock_read_sfr;
    g_host.write_sfr = mock_write_sfr;
    g_host.read_bank = mock_read_bank;
    g_host.rom_call_unhooked = mock_rom_call_unhooked;
    g_ncalls = 0;
    g_touched = 0;
    g_hooked = 0;
    g_p0 = 0;
    g_p2 = P2_MAIN_BANK;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    fw_wr16(&g_f, SER_TX_READ, SER_TX_RING);
    fw_wr16(&g_f, SER_TX_WRITE, SER_TX_RING);
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

/* Every call recorded is isr_par_ready (its P0 write): no ROM call. */
static void check_par_ready_calls(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        CHECK_EQ(g_calls[i].addr, ROM_ISR_PAR_READY);
}

/* boot_clause = s (the bytes after it unchanged). */
static void put_clause(const char *s) { memcpy(g_ram + BOOT_CLAUSE, s, strlen(s)); }

/* setup(), then boot_clause = clause (then zeros), boot_clause_len = its length, cmd_char = '@', parallel mode
   (boot_dip bit 0: each isr_par_ready is recorded). */
static void scan_setup(const char *clause)
{
    setup();
    g_ram[BOOT_DIP] = 1;
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
    CHECK_EQ(g_ncalls, 4);                      /* isr_par_ready once per character read (X = 0, 1, 4, 5) */
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
    queue(0, 3, NUL_BUG_ADDR, 5);
    g_ram[CMD_POSITIONS + 1] = 0xFF;            /* the scan leaves it stale */
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 2);                       /* not due at 2 */
    CHECK_EQ(g_ncalls, 0);
    cmd_run_due(&g_f, 3);
    CHECK_EQ(g_ncalls, 1);
    CHECK_EQ(g_calls[0].addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_calls[0].a, 3);                  /* A = X (cmd_run_next's TXA) */
    CHECK_EQ(g_calls[0].x, 3);                  /* X = the position, as at $A88D */
    CHECK_EQ(g_calls[0].y, 0);                  /* Y = cmd_next ($A880 LDY) */
    CHECK_EQ(g_calls[0].budget, FW_ROM_CALL_BUDGET);
    CHECK_EQ(g_touched, 0);                     /* the C left the bookkeeping to the ROM */
    CHECK_EQ(g_calls[0].vector, 0);
    CHECK_EQ(g_ram[CMD_NEXT], 1);               /* the mock's (the ROM's) bookkeeping */
    cmd_run_due(&g_f, 3);                       /* cmd_next = cmd_count: nothing more */
    CHECK_EQ(g_ncalls, 1);
    CHECK_EQ(g_hooked, 0);                      /* the fallback is unhooked (M4.6c1 spec 2.5) */
    CHECK(!g_f.fault);
}

/* Commands due at one position run in queue order; an unknown handler address (the latent NUL bug of the scan,
   spec 2.6) goes to the ROM cmd_run_next, through an unhooked call. */
static void test_run_due_queue_order(void)
{
    setup();
    queue(0, 2, NUL_BUG_ADDR, 4);
    queue(1, 2, 0x1234, 6);
    queue(2, 5, NUL_BUG_ZP, 8);
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
    CHECK_EQ(g_hooked, 0);
    CHECK_EQ(g_ram[CMD_NEXT], 3);
    CHECK(!g_f.fault);
}

/* A C handler gets cmd_run_next's writes from the C (cmd_vector, cmd_next, cmd_next_pos) and runs with
   A = boot_clause[cmd_arg_index]; an unknown address after it in the queue still goes to the ROM. */
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
    queue(1, 4, NUL_BUG_ADDR, 9);
    g_ram[CMD_POSITIONS + 2] = 0x77;
    cmd_run_due(&g_f, 4);
    CHECK_EQ(g_ram[CMD_SPELL], 1);
    CHECK_EQ(g_ncalls, 1);                      /* only the unknown address goes to the ROM */
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

/* The reply the C ser_tx_* routines queued (M4.6a): its n bytes in the output ring from $3900, ser_tx_write after
   them, ser_tx_read still at $3900, ser_tx_count = n, ser_tx_pending = 1, and no ROM call. */
static void check_reply(const char *reply, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_ram[SER_TX_RING + i], (uint8_t)reply[i]);
    CHECK_EQ(fw_rd16(&g_f, SER_TX_WRITE), SER_TX_RING + n);
    CHECK_EQ(fw_rd16(&g_f, SER_TX_READ), SER_TX_RING);
    CHECK_EQ(g_ram[SER_TX_COUNT], n);
    CHECK_EQ(g_ram[SER_TX_PENDING], 1);
    CHECK_EQ(g_ncalls, 0);
}

/* The C par_setup_voice ran (M4.4a; with the C frame builder since M4.5, so no ROM call): the voice pointers and the
   stride it set from par_count (N) and par_table_ptr; frm_rest_frame = frm_template with b1, b3, b5, b7 = row $3B of
   the four target rows from par_params_ptr (5N apart); both frame buffers = the rest frame with b11 = @M clamped at
   the template's pitch code 32 (at most 160 - 64 = 96, at least 11; 47 - 64 is skipped). */
static void check_setup_voice(void)
{
    uint8_t n = g_ram[PAR_COUNT], m = g_ram[CMD_MARK_SPACE];
    uint16_t table = fw_rd16(&g_f, PAR_TABLE_PTR), params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        uint8_t v = fw_frm_template[b];
        if (b == FRAME_FN || b == FRAME_F1 || b == FRAME_F2 || b == FRAME_F3)
            v = fw_mem8(&g_f, (uint16_t)(params + (b >> 1) * stride + FRM_PAUSE_PHONEME));
        CHECK_EQ(g_ram[FRM_REST_FRAME + b], v);
        if (b == FRAME_MARK_SPACE)
            v = m > 96 ? 96 : m < FRM_MARK_SPACE_FLOOR ? FRM_MARK_SPACE_FLOOR : m;
        CHECK_EQ(g_ram[ISR_FRAME_A + b], v);
        CHECK_EQ(g_ram[ISR_FRAME_B + b], v);
    }
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
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();                        /* the C par_setup_voice: A (pros_pitch_centre), X and Y not read */
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
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();                        /* voice A: N = 86, stride 430 */
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
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0xB0);   /* the A that cmd_apply_pitch hands to par_setup_voice */
    check_setup_voice();
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
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();
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
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();
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
        CHECK_EQ(g_ncalls, 0);
        CHECK_EQ(g_ram[PROS_PITCH_CENTRE], (uint8_t)(k_want[v - 1].pitch << 4));
        check_setup_voice();
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
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();

    setup();
    put_clause(" @RR12");                       /* R***: re-anchors on the second R, so the digit path */
    cmd_set_prosody(&g_f, 5, '2');
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();

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
    check_reply("!", 1);
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
    check_reply("A60", 3);                         /* the letter, then cmd_volume ($14) in hex, low digit first */

    setup();
    put_clause(" @$? ");                           /* the last entry: offset 34, cmd_source ($1C) */
    g_ram[CMD_SOURCE] = 0x42;
    cmd_query(&g_f, 3, '?');
    check_reply("$24", 3);

    setup();                                       /* X = 0 reads $1AFF = cmd_text[255] */
    g_ram[CMD_TEXT + 255] = 'W';
    g_ram[CMD_SPEED] = 3;
    cmd_query(&g_f, 0, 0);
    check_reply("W30", 3);
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
        CHECK_EQ(fw_rd16(&g_f, SER_TX_WRITE), SER_TX_RING);   /* no reply */
        CHECK_EQ(g_ram[SER_TX_PENDING], 0);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

static void test_list_languages(void)
{
    setup();
    put_clause(" @L ");
    g_ram[CMD_NEXT] = 1;
    char want[104] = "L70E0";                      /* 'L', 7 and 14 in hex (low digit first) */
    for (unsigned i = 0; i < 98; i++)
        want[5 + i] = g_ram[BANK_ID_TABLE + i] = (char)('a' + i % 26);   /* 7 bank ids of 14 characters */
    want[103] = '\r';
    cmd_list_languages(&g_f, 2, g_ram[BOOT_CLAUSE + 2]);
    CHECK_EQ(g_ram[SER_REPLY_ID], 'L');
    CHECK_EQ(g_ram[SER_REPLY_COUNT], 0);           /* counted down by ser_tx_records */
    CHECK_EQ(g_ram[SER_REPLY_SIZE], 14);
    CHECK_EQ(fw_rd16(&g_f, SER_REPLY_PTR), BANK_ID_TABLE);
    check_reply(want, 104);
    CHECK(!g_f.fault);
}

/* Every pattern's handler runs as C through the dispatcher (a dropped k_handlers row would fall back to the ROM
   cmd_run_next, which behaves the same and would go unnoticed), bank_cmd_select too since M4.6c1: "@A5" makes it
   select bank 5, an empty socket, and return. */
static void test_run_due_all_rows(void)
{
    for (size_t i = 0; i < fw_cmd_pattern_count; i++) {
        uint16_t handler = fw_cmd_patterns[i].handler;
        setup();
        put_clause(" @A5 ");
        g_ram[CMD_SPEED] = 3;                      /* the speaker/voice/pitch/speed handlers apply it (a 0 would index past the tables) */
        g_ram[CMD_COUNT] = 1;                      /* one queued command at position 0, argument index 3 */
        g_ram[CMD_NEXT_POS] = 0;
        g_ram[CMD_POSITIONS + 1] = 0xFF;
        fw_wr16(&g_f, CMD_HANDLERS, handler);
        g_ram[CMD_ARG_INDEX] = 3;
        cmd_run_due(&g_f, 0);
        CHECK_EQ(g_ram[CMD_NEXT], 1);
        CHECK_EQ(g_ram[CMD_NEXT_POS], 0xFF);
        CHECK_EQ(count(ROM_CMD_RUN_NEXT), 0);
        CHECK_EQ(g_hooked, 0);
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
    test_list_languages();
    test_run_due_all_rows();
}
