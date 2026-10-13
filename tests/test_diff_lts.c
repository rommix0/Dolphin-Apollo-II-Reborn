/* Live differential test of the ported routines (M4.0 spec section 5.1). Every corpus file runs on the ROM
   engine; at each entry of a ported routine (sampled, but every call of the speech-timing routines, k_every_call)
   scratch A runs the ROM routine with interrupts held. If it returns, scratch B is cloned and runs the C thunk, and
   the two must agree on the exit PC and SP, the routine's declared outputs, the SFR model state, the DSP outputs
   and all RAM except the SFRs and the dead stack below the exit SP (diff_same, diff_state.h; M4.6a spec 4.1, M4.6b
   spec 4.1). An interrupt handler (a replacement marked isr) is compared the same
   way: entered through its vector, its ROM copy runs to its RTI (SP rises 3 above the hook's) and its thunk exits
   like RTI, and A, X and Y are compared too (the interrupted code reads all three; the row declares none). The
   routines no corpus text enters (diff_probed, M4.6a spec 4.4) print 0 calls: diff_serial's probes compare them.
   A call whose ROM copy reaches boot_loop_play ($A7D4, tn_convert_clause's mid-clause flush) cannot finish with
   interrupts held: it is skipped and counted (golden_hybrid covers it, M4.1 spec 5.1), in tn_convert_clause only. A
   call whose ROM copy resets the stack (@= entering a bank) runs on in both copies to the bank entry it jumps to,
   $FFD4 or $FFD0, and is compared there: PC, SP, A, X, the I flag, the SFR and DSP state and RAM (M4.6c1 spec 4.1,
   diff_same_at_entry); it is counted: bank_select must produce some (M4.2 spec 5.5). The replacements
   marked waits (the frame loop, which waits on the timer interrupt) cannot run with interrupts held: they are not
   observed here but in diff_play (M4.4b spec 4.1, 4.2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "diff_state.h"

#define DIFF_FULL_CALLS 1000      /* every call up to this many per routine and corpus file ... */
#define DIFF_STRIDE     64        /* ... then every 64th (but the routines of k_every_call) */
#define DIFF_BUDGET     10000000ULL
#define DIFF_MAX        112

static const char *const k_corpus[] = { CORPUS_NAMES };
/* Extra text, for this test only (not goldens): words whose rules use the % & @ context symbols; and the M4.6c1
   plan's Review Focus 1, 2 and 4: @= with an argument outside 1-7, several @= with text after a warm re-entry,
   speech before @=. */
static const char *const k_extra[] = {
    "Judges ruled the churches; loose rules, sugar, the nation's fixes, washing, rejoicing and lunches.",
    "@=0 Zero. @=8 Eight. @=9 Nine. @=F Eff. @=G Gee.",
    "@=1 One. @=5 Two. @=1 Three.",
    "This sentence is spoken first, and its segments overwrite the bank code. @=5 Then more speech follows the bank "
    "command.",
};

/* The speech-timing routines that do not wait (M4.6b spec 4.1), the timer interrupt's too, and the bank code (M4.6c1
   spec 4.1): compared at every call. */
static const char *const k_every_call[] = {
    "isr_set_volume", "isr_volume_half", "isr_par_ready", "isr_par_hold", "isr_dsp_reset", "isr_speech_stop",
    "isr_timer_start", "isr_timer_dsp", "bank_load_ramcode", "bank_enter", "bank_scan", "bank_select", "bank_probe",
    "bank_read_id", "bank_put_id_char", "bank_safe_char", "bank_find_current", "cmd_run_next",
};

typedef struct { const fw_replacement *r; unsigned calls; bool every; } observer;

static machine g_m, g_ref, g_c;
static observer g_obs[DIFF_MAX];
static unsigned g_checked[DIFF_MAX], g_mismatches;
static const char *g_source;
static unsigned g_flush_skips;   /* calls abandoned at a mid-clause flush (M4.1 spec 5.1) */
static bool g_flushed;
static unsigned g_unwound;       /* compared calls whose ROM copy reset the stack (M4.2 spec 5.5) */

/* boot_loop_play reached in scratch A: tn_convert_clause flushing mid-clause, which cannot finish with
   interrupts held. Returning true without moving the PC stops the scratch machine (MFAULT_FIRMWARE). */
static bool stop_at_play(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_flushed = true;
    return true;
}

static bool observe(machine *m, void *user)
{
    observer *o = user;
    unsigned call = o->calls++;
    if (!o->every && call >= DIFF_FULL_CALLS && call % DIFF_STRIDE != 0)
        return false;
    machine_clone(&g_ref, m);
    machine_hold_irqs(&g_ref, true);
    g_flushed = false;
    CHECK(machine_hook(&g_ref, ROM_BOOT_LOOP_PLAY, stop_at_play, NULL));
    char why[160] = "states differ";
    bool same = false;
    fw_bridge b;
    if (!machine_finish_routine(&g_ref, DIFF_BUDGET)) {
        if (g_flushed && !strcmp(o->r->name, "tn_convert_clause")) {
            g_flush_skips++;   /* golden_hybrid covers this call end to end */
            return false;
        }
        g_checked[o - g_obs]++;
        if (g_flushed)
            snprintf(why, sizeof why, "a mid-clause flush outside tn_convert_clause");
        else
            snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_ref.fault);
    } else {
        g_checked[o - g_obs]++;
        /* SP above the return address (RTI: P too): the routine reset the stack (bank_enter) */
        bool unwound = g_ref.cpu.s > m->cpu.s + (o->r->isr ? 3 : 2);
        g_unwound += unwound;
        machine_clone(&g_c, m);
        machine_hold_irqs(&g_c, true);
        if (unwound && !diff_run_to_bank_entry(&g_ref)) {
            snprintf(why, sizeof why, "the ROM copy reset the stack and reached no bank entry (PC $%04X)",
                     g_ref.cpu.pc);
        } else if (!fw_bridge_init(&b, &g_c)) {
            snprintf(why, sizeof why, "no firmware data image");
        } else {
            o->r->thunk(&g_c, &b);
            if (g_c.fault != MFAULT_NONE)
                snprintf(why, sizeof why, "the C faulted (fault %d at $%04X)", (int)g_c.fault, g_c.fault_addr);
            else if (unwound && !diff_run_to_bank_entry(&g_c))
                snprintf(why, sizeof why, "the C copy reached no bank entry (PC $%04X)", g_c.cpu.pc);
            else if (unwound)
                same = diff_same_at_entry(&g_ref, &g_c, why, sizeof why);
            else
                same = diff_same(&g_ref, &g_c, o->r->isr ? (uint8_t)(FW_OUT_A | FW_OUT_X | FW_OUT_Y) : o->r->outputs,
                                 why, sizeof why);
        }
    }
    if (!same && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s call %u (A=%02X X=%02X Y=%02X): %s\n", g_source, o->r->name, call, m->cpu.a,
                m->cpu.x, m->cpu.y, why);
    return false;   /* the ROM routine runs on in m */
}

static void install(machine *m, void *user)
{
    (void)user;
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits)
            continue;   /* diff_play's */
        g_obs[i].r = &fw_replacements[i];
        g_obs[i].calls = 0;   /* the full-call budget is per corpus file */
        g_obs[i].every = false;
        for (size_t k = 0; k < sizeof k_every_call / sizeof k_every_call[0]; k++)
            g_obs[i].every |= !strcmp(fw_replacements[i].name, k_every_call[k]);
        CHECK(fw_hook(m, &fw_replacements[i], observe, &g_obs[i]));
    }
}

static void run(const uint8_t *rom, size_t len, const char *source, const char *text, bool boot)
{
    frame_list fl = { 0 };
    g_source = source;
    machine_status st = rom_engine_run(&g_m, rom, len, NULL, install, NULL, text, boot, &fl, NULL);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    machine_clear_hooks(&g_m);
    frame_list_free(&fl);
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void suite_diff_lts(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(fw_replacement_count <= DIFF_MAX);
    if (fw_replacement_count > DIFF_MAX)
        return;   /* install would overflow g_obs */
    for (size_t k = 0; k < sizeof k_every_call / sizeof k_every_call[0]; k++)
        CHECK(fw_replacement_find(k_every_call[k]) != NULL);   /* a misspelled name would be sampled, silently */
    memset(g_checked, 0, sizeof g_checked);
    g_mismatches = 0;
    g_flush_skips = 0;
    g_unwound = 0;
    bool has_bank_select = false;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        has_bank_select |= !strcmp(k_corpus[c], "bank_select");
        run(rom, len, k_corpus[c], text, false);
        free(text);
    }
    run(rom, len, "boot", "", true);
    for (size_t i = 0; i < sizeof k_extra / sizeof k_extra[0]; i++)
        run(rom, len, "extra", k_extra[i], false);
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits)
            continue;
        printf("  %-20s %u calls checked\n", fw_replacements[i].name, g_checked[i]);
        CHECK(g_checked[i] > 0 || diff_is_probed(fw_replacements[i].name));   /* every ported routine is exercised */
    }
    printf("  %u mid-clause flush calls skipped (golden_hybrid covers them)\n", g_flush_skips);
    CHECK(g_flush_skips > 0);   /* long_clause reaches the flush */
    printf("  %u unwound calls compared\n", g_unwound);
    CHECK(!has_bank_select || g_unwound > 0);   /* bank_select's @=1 resets the stack under a ported routine */
    CHECK_EQ(g_mismatches, 0);
}
