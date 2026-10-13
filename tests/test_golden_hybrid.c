/* The hybrid engine reproduces every golden (M4.0 spec section 5.2): with all ported routines active, and with
   each alone. The rendered audio of hybrid equals the rom engine's (criterion 8). The C tn_convert_clause must
   take its mid-clause flush on long_clause (all-routines mode and tn_convert_clause alone; M4.1 spec 5.2).
   On bank_select, @=1 enters a bank: where the C runs the dispatch or bank_enter, its call chain must unwind (M4.2
   spec 5.3, M4.6c1 spec 4.5). A routine no corpus text enters (diff_probed, M4.6a spec 4.4) runs no thunk alone;
   diff_serial compares it; nor does a row no corpus text or probe reaches (diff_is_unreached, M4.6c2 spec 4.4).
   Also the fault path of a thunk: a firmware fault stops the run instead of continuing on wrong data; the C entering
   a bank; both engines' loop through the idle handler and the phase deadline (M4.6c2 spec 3.1, 3.2;
   test_engine_loop); and the M4.6c1 plan's Review Focus inputs on both engines (test_review_focus). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/cmd_addr.h"
#include "render/render.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
static machine g_m, g_other;
static uint64_t g_thunk_calls;   /* thunks run, summed over the hybrid_engine_speak calls of check() */

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* Compares the hybrid frames of one corpus file with its golden; returns what the C did. */
static hybrid_stats check(const uint8_t *rom, size_t len, const char *only, const char *name, const char *text,
                          bool boot)
{
    hybrid_stats stats = { 0, 0, 0 };
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return stats;
    frame_list fl = { 0 };
    machine_status st = hybrid_engine_speak(&g_m, rom, len, NULL, only, text, boot, &fl, NULL, &stats);
    g_thunk_calls += stats.thunk_calls;
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    CHECK_EQ(g_m.unmodelled_count, 0);
    char *got = frame_list_hex(&fl);
    if (strcmp(got, want) != 0) {
        size_t line = 1;
        for (size_t i = 0; got[i] && got[i] == want[i]; i++)
            line += got[i] == '\n';
        fprintf(stderr, "  hybrid (%s) %s: frames differ from the golden at frame %zu\n", only ? only : "all", name,
                line);
        g_test_failures++;
    }
    free(got);
    free(want);
    frame_list_free(&fl);
    return stats;
}

/* The modes in which the C reaches cmd_run_due's dispatch or bank_enter, so @= entering a bank resets the stack
   under C, or the C enters the bank, and the C call chain unwinds (M4.2 spec 5.3, M4.6c1 spec 4.5). */
static bool runs_c_dispatch(const char *only)
{
    return !only || !strcmp(only, "tn_convert_clause") || !strcmp(only, "cmd_run_due") ||
           !strcmp(only, "cmd_run_next") || !strcmp(only, "bank_enter");
}

/* The whole corpus in one mode. Matching the goldens proves nothing if no C ever ran, so the mode must have run
   its thunks at least once over the corpus (unless it is a routine the corpus never enters); and where the C
   tn_convert_clause runs, long_clause must have taken its mid-clause flush (M4.1 spec 5.2). */
static void check_corpus(const uint8_t *rom, size_t len, const char *only)
{
    g_thunk_calls = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        hybrid_stats st = check(rom, len, only, k_corpus[c], text, false);
        if (!strcmp(k_corpus[c], "bank_select") && runs_c_dispatch(only) && st.unwinds == 0) {
            fprintf(stderr, "  hybrid (%s) bank_select: the C never unwound at the bank entry\n", only ? only : "all");
            g_test_failures++;
        }
        if (!strcmp(k_corpus[c], "long_clause") && (!only || !strcmp(only, "tn_convert_clause")) &&
            st.tn_flushes == 0) {
            fprintf(stderr, "  hybrid (%s) long_clause: the C tn_convert_clause never flushed\n",
                    only ? only : "all");
            g_test_failures++;
        }
        free(text);
    }
    check(rom, len, only, "boot", "", true);
    if (g_thunk_calls == 0 && !(only && (diff_is_probed(only) || diff_is_unreached(only)))) {
        fprintf(stderr, "  hybrid (%s): no thunk ran over the whole corpus\n", only ? only : "all");
        g_test_failures++;
    }
}

static void test_render_matches_rom(const uint8_t *rom, size_t len)
{
    render_options opt;
    render_result a, b;
    render_default_options(&opt);
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &a), MACHINE_IDLE);
    opt.engine = RENDER_ENGINE_HYBRID;
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &b), MACHINE_IDLE);
    CHECK(a.count > 0);
    CHECK_EQ(a.count, b.count);
    CHECK(a.count == b.count && memcmp(a.samples, b.samples, a.count * sizeof a.samples[0]) == 0);
    render_result_free(&a);
    render_result_free(&b);
}

/* A hook that returns true without moving the PC: the machine faults at its address. */
static bool stuck_hook(machine *m, void *user)
{
    (void)m;
    (void)user;
    return true;
}

/* A thunk that cannot finish stops the run: MFAULT_FIRMWARE with the address, the PC still at the hooked entry,
   the SP as it was on entry (a failed ROM call leaves the sentinel frame), the bridge's trap restored. */
static void check_stopped(const machine *m, const fw_bridge *b, uint16_t entry, uint16_t fault_addr, uint8_t sp)
{
    CHECK_EQ(m->fault, MFAULT_FIRMWARE);
    CHECK_EQ(m->fault_addr, fault_addr);
    CHECK_EQ(m->cpu.pc, entry);
    CHECK_EQ(m->cpu.s, sp);
    CHECK(b->f.trap == NULL);
}

#define BOOT_RESET_ADDR 0xA61C   /* any ROM address: where reset_stack_hook "jumps" */
#define NUL_BUG_ADDR    0xD300   /* a handler address of the scan's NUL bug ("@A" then a zero, M4.6c1 spec 2.5) */

/* One command queued, due at position 0, with the handler handler and its argument at boot_clause[1] = arg: the C
   cmd_run_due with X = 0 runs it through the C cmd_run_next (an unknown address: the ROM cmd_run_next, unhooked). */
static void queue_command(machine *m, uint16_t handler, char arg)
{
    m->ram[CMD_COUNT] = 1;
    m->ram[CMD_NEXT] = 0;
    m->ram[CMD_NEXT_POS] = 0;
    m->ram[CMD_HANDLERS] = (uint8_t)handler;
    m->ram[CMD_HANDLERS + 1] = (uint8_t)(handler >> 8);
    m->ram[CMD_ARG_INDEX] = 1;
    m->ram[BOOT_CLAUSE + 1] = (uint8_t)arg;
    m->cpu.x = 0;
}

static unsigned g_run_next_hooks;

/* An observer at cmd_run_next's own address: counts and lets the ROM run (the unhooked call must skip it). */
static bool count_hook(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_run_next_hooks++;
    return false;
}

/* Stands for bank_enter's stack reset: SP = $FF and a jump elsewhere. */
static bool reset_stack_hook(machine *m, void *user)
{
    (void)user;
    m->cpu.s = 0xFF;
    m->cpu.pc = BOOT_RESET_ADDR;
    return true;
}

static void test_thunk_faults(const uint8_t *rom, size_t len)
{
    fw_bridge b;
    const fw_replacement *r;

    /* (1) the C reads outside the converted data: a pointer into the SFR bytes */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    r = fw_replacement_find("lts_affix_next");
    CHECK(r != NULL);
    if (!r)
        return;
    fw_wr16(&b.f, LTS_AFFIX_PTR, 0x00E0);
    g_m.cpu.y = 0;
    g_m.cpu.pc = r->addr;
    uint8_t sp = g_m.cpu.s;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, 0x00E1, sp);

    /* (2) a ROM call from the C that does not return: the unhooked ROM cmd_run_next of an unknown handler address
       (the hook at its own address skipped, M4.6c1 spec 2.5), stuck at the address it jumps to */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, NUL_BUG_ADDR, stuck_hook, NULL));
    CHECK(machine_hook(&g_m, ROM_CMD_RUN_NEXT, count_hook, NULL));
    g_run_next_hooks = 0;
    r = fw_replacement_find("cmd_run_due");
    CHECK(r != NULL);
    if (!r)
        return;
    queue_command(&g_m, NUL_BUG_ADDR, 'x');
    g_m.cpu.pc = r->addr;
    sp = g_m.cpu.s;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, NUL_BUG_ADDR, sp);
    CHECK_EQ(g_run_next_hooks, 0);

    /* (3) a thunk run on another machine than its bridge's: refused */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(machine_init(&g_other, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    r = fw_replacement_find("lts_sym_vowel");
    CHECK(r != NULL);
    if (!r)
        return;
    g_other.cpu.pc = r->addr;
    sp = g_other.cpu.s;
    CHECK(r->thunk(&g_other, &b));
    check_stopped(&g_other, &b, r->addr, r->addr, sp);
    CHECK_EQ(g_m.fault, MFAULT_NONE);

    /* (4) a ROM call from the C that resets the stack (the unknown address's code, here a stack reset and a jump):
       the C is abandoned, no fault; the machine goes on where the ROM went (M4.2 spec 3) */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, NUL_BUG_ADDR, reset_stack_hook, NULL));
    r = fw_replacement_find("cmd_run_due");
    CHECK(r != NULL);
    if (!r)
        return;
    queue_command(&g_m, NUL_BUG_ADDR, 'x');
    g_m.cpu.pc = r->addr;
    g_m.cpu.s = 0xF0;                            /* a caller's frame below the stack reset */
    CHECK(r->thunk(&g_m, &b));
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    CHECK_EQ(g_m.cpu.pc, BOOT_RESET_ADDR);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(b.unwinds, 1);
    CHECK(!b.f.unwound);
    CHECK(b.f.trap == NULL);

    /* (5) the C enters a bank (M4.6c1 spec 3.2, 3.4): @=1 dispatched by the C cmd_run_due on a fresh machine (P2's
       pins $70: the main bank). Its version copies are 0, not the header's 2.01: the cold entry, zero page $01-$64
       cleared; with them set, the warm entry. The machine goes on at the entry with SP $FF, I set, A and X as
       bank_enter leaves them; no fault; the chain unwound once. */
    for (int warm = 0; warm <= 1; warm++) {
        CHECK(machine_init(&g_m, rom, len, NULL));
        CHECK(fw_bridge_init(&b, &g_m));
        queue_command(&g_m, ROM_BANK_CMD_SELECT, '1');
        g_m.ram[0x64] = 0x5A;
        if (warm) {
            g_m.ram[BANK_VER_MAJOR] = 2;
            g_m.ram[BANK_VER_MINOR] = 1;
        }
        g_m.cpu.pc = r->addr;
        g_m.cpu.s = 0xF0;
        g_m.cpu.p = 0;
        CHECK(r->thunk(&g_m, &b));
        CHECK_EQ(g_m.fault, MFAULT_NONE);
        CHECK_EQ(g_m.cpu.pc, warm ? BANK_ENTRY_WARM : BANK_ENTRY_COLD);
        CHECK_EQ(g_m.cpu.s, 0xFF);
        CHECK_EQ(g_m.cpu.p & CPU740_I, CPU740_I);
        CHECK_EQ(g_m.cpu.a, warm ? 2 : 0);
        CHECK_EQ(g_m.cpu.x, warm ? 0xFF : 0);
        CHECK_EQ(g_m.ram[0x64], warm ? 0x5A : 0);
        CHECK_EQ(b.unwinds, 1);
        CHECK(!b.f.unwound);
        CHECK(b.f.trap == NULL);
    }
}

/* ---- the M4.6c1 plan's Review Focus: the rom and hybrid engines on the same input ---- */

typedef struct {
    machine_status st;
    machine_fault fault;
    uint16_t fault_addr;     /* MFAULT_FIRMWARE */
    uint8_t cpu_fault, opcode;
    uint16_t cpu_fault_pc;   /* MFAULT_CPU */
    char *frames;            /* frame_list_hex */
    uint8_t reply[256];      /* the output ring from its read pointer to its write pointer: the queued reply */
    unsigned reply_len;
    uint8_t tx_pending, bank_current, bank_ids[98];
    uint8_t clause_len, clause[8];   /* boot_clause_len and boot_clause's first bytes: the last clause read */
    unsigned unwinds;
} outcome;

/* One engine (hybrid: every replacement) fed a CR, then the n raw bytes of in (a NUL included: no CLI text can hold
   one), at the end of boot through the engine's own idle handler (M4.6c2 spec 3.1), and run to the next idle, a fault
   or the speaking budget; the frames from the end of boot on. The CR's empty clause first, as before M4.6c2 (a boot
   run fed it, then the raw bytes followed): the NUL bug's ROM jumps return through what it left on the stack. */
static void run_raw(const uint8_t *rom, size_t len, bool hybrid, const char *in, size_t n, outcome *o)
{
    frame_list fl = { 0 };
    hybrid_stats stats = { 0, 0, 0 };
    uint8_t bytes[256] = { '\r' };
    CHECK(n < sizeof bytes);
    if (n >= sizeof bytes)
        n = sizeof bytes - 1;
    memcpy(bytes + 1, in, n);
    engine_input input = { bytes, n + 1, false };
    memset(o, 0, sizeof *o);
    o->st = hybrid ? hybrid_engine_run_input(&g_m, rom, len, NULL, NULL, &input, false, &fl, NULL, &stats, NULL)
                   : rom_engine_run_input(&g_m, rom, len, NULL, NULL, NULL, &input, false, &fl, NULL, NULL);
    o->fault = g_m.fault;
    o->fault_addr = g_m.fault_addr;
    o->cpu_fault = (uint8_t)g_m.cpu.fault;
    o->opcode = g_m.cpu.fault_opcode;
    o->cpu_fault_pc = g_m.cpu.fault_pc;
    o->frames = frame_list_hex(&fl);
    frame_list_free(&fl);
    uint16_t rd = (uint16_t)(g_m.ram[SER_TX_READ] | g_m.ram[SER_TX_READ + 1] << 8);
    uint16_t wr = (uint16_t)(g_m.ram[SER_TX_WRITE] | g_m.ram[SER_TX_WRITE + 1] << 8);
    for (uint16_t a = rd; a != wr && o->reply_len < sizeof o->reply; a = (uint16_t)(a == 0x39FF ? 0x3900 : a + 1))
        o->reply[o->reply_len++] = g_m.ram[a];
    o->tx_pending = g_m.ram[SER_TX_PENDING];
    o->bank_current = g_m.ram[BANK_CURRENT];
    memcpy(o->bank_ids, g_m.ram + BANK_ID_TABLE, sizeof o->bank_ids);
    o->clause_len = g_m.ram[BOOT_CLAUSE_LEN];
    memcpy(o->clause, g_m.ram + BOOT_CLAUSE, sizeof o->clause);
    o->unwinds = stats.unwinds;
}

/* Both engines on in: the same status, fault, frames, queued reply, bank state and last clause. Returns the hybrid's
   unwinds. */
static unsigned check_same(const uint8_t *rom, size_t len, const char *name, const char *in, size_t n, outcome *a)
{
    outcome b;
    run_raw(rom, len, false, in, n, a);
    run_raw(rom, len, true, in, n, &b);
    bool same = a->st == b.st && a->fault == b.fault && a->fault_addr == b.fault_addr &&
                a->cpu_fault == b.cpu_fault && a->opcode == b.opcode && a->cpu_fault_pc == b.cpu_fault_pc &&
                !strcmp(a->frames, b.frames) && a->reply_len == b.reply_len &&
                !memcmp(a->reply, b.reply, a->reply_len) && a->tx_pending == b.tx_pending &&
                a->bank_current == b.bank_current && !memcmp(a->bank_ids, b.bank_ids, sizeof a->bank_ids) &&
                a->clause_len == b.clause_len && !memcmp(a->clause, b.clause, sizeof a->clause);
    if (!same) {
        fprintf(stderr, "  review %s: rom status %d fault %d, hybrid status %d fault %d (or frames, reply, banks, "
                "clause)\n",
                name, (int)a->st, (int)a->fault, (int)b.st, (int)b.fault);
        g_test_failures++;
    }
    free(b.frames);
    return b.unwinds;
}

#define TEXT(s) s "\r", sizeof s   /* the text and its CR: the bytes the CLI feeds */

/* The Review Focus inputs (M4.6c1 plan): 1. @= with 0, 8, 9 (bank_select indexes $0200,X past the seven real
   entries: $0200 and $0208-$0209 hold the main bank, so each enters warm) and with F and G (no digit: "=#" does not
   match); 2. several @= and text after a warm re-entry; 3. @L after the banks change (the reply queued for the host:
   the machine never raises the host line, so it stays in the output ring); 4. speech, then @= (the segments
   overwrite the RAM copy first); 5. a NUL after a command prefix, raw serial input: no frames on either engine.
   "@M5" NUL: the same CPU fault on both (a BRK at $2300, the zero page address the scan queued). "@A" NUL: the ROM
   cmd_run_next jumps to $D300 inside pros_phrase_gen, whose code returns through the stack: the ROM engine's into
   $8F02 (an illegal opcode $54), the hybrid's elsewhere, since the C dispatcher's frames are not on the machine
   stack; the hybrid's unhooked ROM call then never returns, a firmware fault at $A893 (the C cannot follow the
   jump; M4.6c1 spec 2.5, unchanged from M4.2). */
static void test_review_focus(const uint8_t *rom, size_t len)
{
    outcome a;
    CHECK_EQ(check_same(rom, len, "1", TEXT("@=0 Zero. @=8 Eight. @=9 Nine. @=F Eff. @=G Gee."), &a), 3);
    CHECK_EQ(a.st, MACHINE_IDLE);
    free(a.frames);
    CHECK_EQ(check_same(rom, len, "2", TEXT("@=1 One. @=5 Two. @=1 Three."), &a), 2);
    CHECK_EQ(a.st, MACHINE_IDLE);
    CHECK(strlen(a.frames) > 0);
    free(a.frames);
    static const char *const k_l[] = { "@=5 @L\r", "@=1 @L\r", "@=1 One. @L @=5 Two. @L\r" };
    static const unsigned k_reply[] = { 104, 0, 208 };   /* "L70E0", 7 x 14 ID characters, CR; none: the warm
                                                            re-entry abandons the rest of its clause; two */
    for (int i = 0; i < 3; i++) {
        check_same(rom, len, "3", k_l[i], strlen(k_l[i]), &a);
        CHECK_EQ(a.reply_len, k_reply[i]);
        CHECK(a.reply_len == 0 || !memcmp(a.reply, "L70E000044002010404", 19));
        free(a.frames);
    }
    CHECK_EQ(check_same(rom, len, "4", TEXT("This sentence is spoken first, and its segments overwrite the bank code. "
                                            "@=5 Then more speech follows the bank command."), &a), 0);
    CHECK_EQ(a.st, MACHINE_IDLE);
    free(a.frames);

    check_same(rom, len, "5", "@M5\0\r", 5, &a);
    CHECK_EQ(a.st, MACHINE_FAULT);
    CHECK_EQ(a.fault, MFAULT_CPU);
    CHECK_EQ(a.opcode, 0x00);
    CHECK_EQ(a.cpu_fault_pc, 0x2300);
    CHECK_STR(a.frames, "");
    free(a.frames);
    outcome b;
    run_raw(rom, len, false, "@A\0\r", 4, &a);
    run_raw(rom, len, true, "@A\0\r", 4, &b);
    CHECK_EQ(a.st, MACHINE_FAULT);
    CHECK_EQ(a.fault, MFAULT_CPU);
    CHECK_EQ(a.opcode, 0x54);
    CHECK_EQ(a.cpu_fault_pc, 0x8F02);
    CHECK_EQ(b.st, MACHINE_FAULT);
    CHECK_EQ(b.fault, MFAULT_FIRMWARE);
    CHECK_EQ(b.fault_addr, ROM_CMD_RUN_NEXT);
    CHECK_STR(a.frames, "");
    CHECK_STR(b.frames, "");
    free(a.frames);
    free(b.frames);
}

/* The M4.6c2 plan's Review Focus 3: the CR alone (an empty input) on both engines, the C boot_get_clause taking it
   as the clause's last character: both idle again with no frame. The clause is three spaces (boot_clause_len 3): the
   one boot_clause[0] always holds, the one the self-test string left in the ring at boot (boot_get_clause read it
   before the end of boot's idle), and the one the CR becomes. */
static void test_review_focus_empty(const uint8_t *rom, size_t len)
{
    outcome a;
    check_same(rom, len, "rf3", "", 0, &a);
    CHECK_EQ(a.st, MACHINE_IDLE);
    CHECK_STR(a.frames, "");
    CHECK_EQ(a.clause_len, 3);
    CHECK(!memcmp(a.clause, "   ", 3));
    free(a.frames);
}

/* ---- the engines' loop (M4.6c2 spec 3.1, 3.2) ---- */

typedef struct { unsigned idles, frames, frames_at_idle[4]; } engine_count;

static void count_frame(void *user, machine *m)
{
    (void)m;
    ((engine_count *)user)->frames++;
}

static void count_idle(void *user, machine *m)
{
    engine_count *c = user;
    (void)m;
    if (c->idles < 4)
        c->frames_at_idle[c->idles] = c->frames;
    c->idles++;
}

/* One engine (hybrid: every replacement) on in with a watch counting its events. */
static machine_status run_counted(const uint8_t *rom, size_t len, const machine_config *cfg, bool hybrid,
                                  const char *in, engine_count *c, frame_list *fl)
{
    engine_watch w = { count_frame, count_idle, c };
    engine_input input = { (const uint8_t *)in, strlen(in), true };
    memset(c, 0, sizeof *c);
    return hybrid ? hybrid_engine_run_input(&g_m, rom, len, cfg, NULL, &input, false, fl, NULL, NULL, &w)
                  : rom_engine_run_input(&g_m, rom, len, cfg, NULL, NULL, &input, false, fl, NULL, &w);
}

/* Both engines run through the idle handler: on hello each idles twice (the end of boot, the end of the text) and
   runs on from the first, the boot's frame events before it (the self-test sound) and the text's after it, the
   frames kept from the first idle on. In parallel mode (DIP bit 0: isr_idle returns at once) neither idles: the run
   ends at the boot phase's deadline, MACHINE_CYCLE_LIMIT, after the boot's frames. */
static void test_engine_loop(const uint8_t *rom, size_t len)
{
    for (int hybrid = 0; hybrid <= 1; hybrid++) {
        engine_count c;
        frame_list fl = { 0 };
        CHECK_EQ(run_counted(rom, len, NULL, hybrid, "hello", &c, &fl), MACHINE_IDLE);
        CHECK_EQ(c.idles, 2);
        CHECK_EQ(c.frames_at_idle[0], 41);
        CHECK_EQ(c.frames_at_idle[1], c.frames);
        CHECK_EQ(fl.count, c.frames - 41);
        CHECK(fl.count > 0);
        CHECK_EQ(g_m.frames_produced, fl.count);
        frame_list_free(&fl);

        if (hybrid && fw_replacement_find("boot_get_clause") && !fw_replacement_find("boot_reset"))
            continue;   /* until boot_reset is C, the C main loop's waits run under no phase deadline */
        machine_config cfg;
        machine_default_config(&cfg);
        cfg.port_in[4] |= 1;   /* DIP bit 0: the parallel interface */
        CHECK_EQ(run_counted(rom, len, &cfg, hybrid, "hello", &c, &fl), MACHINE_CYCLE_LIMIT);
        CHECK_EQ(c.idles, 0);
        CHECK_EQ(c.frames, 41);
        CHECK_EQ(fl.count, 0);
        CHECK(g_m.cpu.cycles >= ROM_ENGINE_BOOT_CYCLES);
        CHECK_EQ(g_m.fault, MFAULT_NONE);
        frame_list_free(&fl);
    }
}

void suite_golden_hybrid(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    test_thunk_faults(rom, len);
    test_engine_loop(rom, len);
    test_review_focus(rom, len);
    test_review_focus_empty(rom, len);
    check_corpus(rom, len, NULL);
    for (size_t i = 0; i < fw_replacement_count; i++)
        check_corpus(rom, len, fw_replacements[i].name);
    printf("  %zu corpus files x %zu modes (all, then each routine alone)\n",
           sizeof k_corpus / sizeof k_corpus[0] + 1, fw_replacement_count + 1);
    CHECK_EQ(hybrid_engine_speak(&g_m, rom, len, NULL, "no_such_routine", "hello", false, NULL, NULL, NULL),
             MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    test_render_matches_rom(rom, len);
}
