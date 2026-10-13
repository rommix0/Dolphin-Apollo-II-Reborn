/* The hybrid engine reproduces every golden (M4.0 spec section 5.2): with all ported routines active, and with
   each alone. The rendered audio of hybrid equals the rom engine's (criterion 8). The C tn_convert_clause must
   take its mid-clause flush on long_clause (all-routines mode and tn_convert_clause alone; M4.1 spec 5.2).
   On bank_select, @=1 enters a bank: where the C runs the dispatch, its call chain must unwind (M4.2 spec 5.3).
   A routine no corpus text enters (diff_probed, M4.6a spec 4.4) runs no thunk alone; diff_serial compares it.
   Also the fault path of a thunk: a firmware fault stops the run instead of continuing on wrong data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
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
    return !only || !strcmp(only, "tn_convert_clause") || !strcmp(only, "cmd_run_due") || !strcmp(only, "bank_enter");
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
    if (g_thunk_calls == 0 && !(only && diff_is_probed(only))) {
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

/* One command queued, due at position 0, whose handler stays in ROM (bank_cmd_select): the C cmd_run_due with X = 0
   hands it to the ROM cmd_run_next, its one ROM call. */
static void queue_rom_command(machine *m)
{
    m->ram[CMD_COUNT] = 1;
    m->ram[CMD_NEXT] = 0;
    m->ram[CMD_NEXT_POS] = 0;
    m->ram[CMD_HANDLERS] = ROM_BANK_CMD_SELECT & 0xFF;
    m->ram[CMD_HANDLERS + 1] = ROM_BANK_CMD_SELECT >> 8;
    m->cpu.x = 0;
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

    /* (2) a ROM call from the C that does not return: cmd_run_next, cmd_run_due's call for a ROM handler */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_CMD_RUN_NEXT, stuck_hook, NULL));
    r = fw_replacement_find("cmd_run_due");
    CHECK(r != NULL);
    if (!r)
        return;
    queue_rom_command(&g_m);
    g_m.cpu.pc = r->addr;
    sp = g_m.cpu.s;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, ROM_CMD_RUN_NEXT, sp);

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

    /* (4) a ROM call from the C that resets the stack (bank_enter's LDX #$FF / TXS): the C is abandoned, no
       fault; the machine goes on where the ROM went (M4.2 spec 3) */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_CMD_RUN_NEXT, reset_stack_hook, NULL));
    r = fw_replacement_find("cmd_run_due");
    CHECK(r != NULL);
    if (!r)
        return;
    queue_rom_command(&g_m);
    g_m.cpu.pc = r->addr;
    g_m.cpu.s = 0xF0;                            /* a caller's frame below the stack reset */
    CHECK(r->thunk(&g_m, &b));
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    CHECK_EQ(g_m.cpu.pc, BOOT_RESET_ADDR);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(b.unwinds, 1);
    CHECK(!b.f.unwound);
    CHECK(b.f.trap == NULL);
}

void suite_golden_hybrid(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    test_thunk_faults(rom, len);
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
