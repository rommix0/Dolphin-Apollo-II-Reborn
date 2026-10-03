/* The hybrid engine reproduces every golden (M4.0 spec section 5.2): with all ported routines active, and with
   each alone. The rendered audio of hybrid equals the rom engine's (criterion 8). Also the fault path of a
   thunk: a firmware fault stops the run instead of continuing on wrong data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/tn_rules.h"
#include "render/render.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
static machine g_m, g_other;

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* Compares the hybrid frames of one corpus file with its golden. */
static void check(const uint8_t *rom, size_t len, const char *only, const char *name, const char *text, bool boot)
{
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return;
    frame_list fl = { 0 };
    machine_status st = hybrid_engine_speak(&g_m, rom, len, NULL, only, text, boot, &fl, NULL);
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
}

static void check_corpus(const uint8_t *rom, size_t len, const char *only)
{
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        check(rom, len, only, k_corpus[c], text, false);
        free(text);
    }
    check(rom, len, only, "boot", "", true);
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
   the bridge's trap restored. */
static void check_stopped(const machine *m, const fw_bridge *b, uint16_t entry, uint16_t fault_addr)
{
    CHECK_EQ(m->fault, MFAULT_FIRMWARE);
    CHECK_EQ(m->fault_addr, fault_addr);
    CHECK_EQ(m->cpu.pc, entry);
    CHECK(b->f.trap == NULL);
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
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, 0x00E1);

    /* (2) a ROM call from the C that does not return */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, TN_APPEND_PHONETIC, stuck_hook, NULL));
    r = fw_replacement_find("tn_say_char");
    CHECK(r != NULL);
    if (!r)
        return;
    g_m.cpu.a = 'a';
    g_m.cpu.pc = r->addr;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, TN_APPEND_PHONETIC);

    /* (3) a thunk run on another machine than its bridge's: refused */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(machine_init(&g_other, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    r = fw_replacement_find("lts_sym_vowel");
    CHECK(r != NULL);
    if (!r)
        return;
    g_other.cpu.pc = r->addr;
    CHECK(r->thunk(&g_other, &b));
    check_stopped(&g_other, &b, r->addr, r->addr);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
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
    CHECK_EQ(hybrid_engine_speak(&g_m, rom, len, NULL, "no_such_routine", "hello", false, NULL, NULL),
             MACHINE_FAULT);
    test_render_matches_rom(rom, len);
}
