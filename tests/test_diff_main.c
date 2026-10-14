/* The end-to-end differential test (M4.6c2 spec 4.1): the ROM engine and the hybrid engine with every replacement
   ("the all-C hybrid") on the same input, compared at every input idle and at every frame event. Each run goes through
   the ROM engine first, whose watch (engine_watch) records each event in order: its kind (an idle or a frame) and a
   snapshot of the machine: all RAM but the SFR bytes and the stack page $0100-$01FF (boot_stack: with the main loop
   in C the machine stack no longer holds the ROM's return addresses, so interrupts push onto other bytes; user
   decision), the SFR state (diff_same_sfr), the DSP outputs (the port writes, the raw frame position and bytes, the
   reset pulses and level), the frame counts (frames_produced, raw_frames) and the I flag. The same input then goes
   through the hybrid engine, whose watch compares each of its events with the ROM's event of the same index: the same
   kind and the same snapshot; the first difference ends the comparison of that run. At the end of a run both engines
   must have the same status (MACHINE_IDLE, or MACHINE_CYCLE_LIMIT where the ROM never idles again) and the same number
   of events, and their final states are compared as one more event.
   The runs: every corpus text, and the extra runs (diff_runs.c, shared with diff_native) whose configuration (the P4
   DIP switches, the P2 and P0 input pins) or injection (made to both engines at the same event: an idle or a frame
   event, before the snapshot is taken) reaches what no corpus text does; each names a ROM address its ROM run must
   execute (a hook on the ROM engine only), so that every extra run is seen to reach its path. Parallel mode (DIP bit
   0) never idles: the run ends at the boot phase's deadline in both engines, after the boot announcement's frame
   events. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_state.h"
#include "diff_runs.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"

/* One event of a run. */
typedef struct {
    bool idle, frame;                 /* an input idle, a frame event, or neither: the end of the run */
    uint8_t ram[MACHINE_RAM_SIZE];
    m50734 sfr;
    uint64_t port_writes, dsp_resets, frames_produced, raw_frames;
    unsigned raw_pos;
    uint8_t raw_buf[APOLLO_FRAME_BYTES];
    bool dsp_reset_level, i;
} snapshot;

static machine g_m;
static snapshot *g_rom_events;        /* the ROM run's events: g_nrom of them, room for g_cap */
static size_t g_nrom, g_cap;
static snapshot g_cur;
static const extra_run *g_extra;      /* the run's injection, or NULL */
static unsigned g_idles, g_frames;    /* the events of the engine running */
static unsigned g_speech_frames;      /* its frame events after the first idle */
static bool g_rom_side;               /* recording (the ROM engine) or comparing (the hybrid) */
static size_t g_nevents;              /* the hybrid run's events so far */
static bool g_differs;
static char g_why[200];
static unsigned g_target_hits, g_mismatches, g_runs, g_total_idles, g_total_frames;

static void take(snapshot *s, const machine *m, bool idle, bool frame)
{
    s->idle = idle;
    s->frame = frame;
    memcpy(s->ram, m->ram, sizeof s->ram);
    s->sfr = m->sfr;
    s->port_writes = m->port_writes;
    s->dsp_resets = m->dsp_resets;
    s->frames_produced = m->frames_produced;
    s->raw_frames = m->raw_frames;
    s->raw_pos = m->raw_pos;
    memcpy(s->raw_buf, m->raw_buf, sizeof s->raw_buf);
    s->dsp_reset_level = m->dsp_reset_level;
    s->i = (m->cpu.p & CPU740_I) != 0;
}

/* The first difference between the ROM's snapshot a and the hybrid's b, or false. */
static bool differs(const snapshot *a, const snapshot *b, char *why, size_t n)
{
    if (a->idle != b->idle) {
        snprintf(why, n, "event kind: ROM %s, C %s", a->idle ? "idle" : "frame", b->idle ? "idle" : "frame");
        return true;
    }
    if (a->i != b->i) {
        snprintf(why, n, "I: ROM %d, C %d", a->i, b->i);
        return true;
    }
    if (!diff_same_sfr(&a->sfr, &b->sfr, why, n))
        return true;
    if (a->port_writes != b->port_writes || a->raw_pos != b->raw_pos ||
        memcmp(a->raw_buf, b->raw_buf, sizeof a->raw_buf) != 0 || a->dsp_resets != b->dsp_resets ||
        a->dsp_reset_level != b->dsp_reset_level) {
        snprintf(why, n, "DSP port writes/raw position/resets: ROM %llu/%u/%llu, C %llu/%u/%llu (or the raw bytes or "
                 "the reset level)", (unsigned long long)a->port_writes, a->raw_pos,
                 (unsigned long long)a->dsp_resets, (unsigned long long)b->port_writes, b->raw_pos,
                 (unsigned long long)b->dsp_resets);
        return true;
    }
    if (a->frames_produced != b->frames_produced || a->raw_frames != b->raw_frames) {
        snprintf(why, n, "frames produced/raw: ROM %llu/%llu, C %llu/%llu", (unsigned long long)a->frames_produced,
                 (unsigned long long)a->raw_frames, (unsigned long long)b->frames_produced,
                 (unsigned long long)b->raw_frames);
        return true;
    }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if ((addr >= M50734_SFR_FIRST && addr <= 0xFF) || (addr >= 0x100 && addr <= 0x1FF))
            continue;
        if (!a->frame && addr >= ISR_TICK_DIV && addr <= ISR_WORDS_LEFT)
            continue;   /* the timer interrupt's per-word counters: how far it got before isr_speech_stop */
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    return false;
}

/* One event of the engine running: the injection due at it, then the snapshot recorded (ROM) or compared (C). */
static void event(machine *m, bool idle)
{
    unsigned k = idle ? g_idles++ : g_frames++;
    unsigned at = idle ? k : g_idles ? g_speech_frames++ : ~0u;
    if (g_extra && g_extra->inject && (g_extra->at == AT_IDLE) == idle && g_extra->n == at) {
        diff_view v = { m->ram, &m->sfr, m->queue, &m->q_head, &m->q_tail };
        g_extra->inject(&v);
    }
    if (g_rom_side) {
        if (g_nrom == g_cap) {
            g_cap = g_cap ? g_cap * 2 : 1024;
            g_rom_events = realloc(g_rom_events, g_cap * sizeof *g_rom_events);
            if (!g_rom_events)
                abort();
        }
        take(&g_rom_events[g_nrom++], m, idle, !idle);
        return;
    }
    size_t i = g_nevents++;
    if (g_differs)
        return;
    take(&g_cur, m, idle, !idle);
    char why[160];
    if (i >= g_nrom) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu: the C has more events than the ROM (%zu)", i, g_nrom);
    } else if (differs(&g_rom_events[i], &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu (%s %u): %s", i, idle ? "idle" : "frame", k, why);
    }
}

static void on_frame(void *user, machine *m)
{
    (void)user;
    event(m, false);
}

static void on_idle(void *user, machine *m)
{
    (void)user;
    event(m, true);
}

static bool count_target(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_target_hits++;
    return false;
}

/* The ROM run's hook at the extra run's target. */
static void install_target(machine *m, void *user)
{
    const extra_run *x = user;
    CHECK(x->target_ramcode ? machine_hook_ramcode(m, x->target, count_target, NULL)
                            : machine_hook(m, x->target, count_target, NULL));
}

/* One input on both engines; x: the extra run (NULL: a corpus text). */
static void run_pair(const uint8_t *rom, size_t len, const char *name, const char *text, const extra_run *x)
{
    machine_config cfg;
    machine_default_config(&cfg);
    if (x) {
        cfg.port_in[4] |= x->p4;
        cfg.port_in[2] |= x->p2;
        cfg.port_in[0] |= x->p0;
        cfg.port_in[3] |= x->p3;
    }
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    engine_watch w = { on_frame, on_idle, NULL };
    frame_list fl = { 0 };
    g_extra = x;

    g_rom_side = true;
    g_nrom = 0;
    g_idles = g_frames = g_speech_frames = 0;
    g_target_hits = 0;
    machine_status rom_st = rom_engine_run_input(&g_m, rom, len, &cfg, x ? install_target : NULL, (void *)x, &in,
                                                 false, &fl, NULL, &w);
    machine_clear_hooks(&g_m);
    take(&g_cur, &g_m, false, false);   /* the end of the run, one more event */
    snapshot rom_end = g_cur;
    unsigned rom_idles = g_idles, rom_frames = g_frames;
    frame_list_free(&fl);

    g_rom_side = false;
    g_nevents = 0;
    g_idles = g_frames = g_speech_frames = 0;
    g_differs = false;
    machine_status c_st = hybrid_engine_run_input(&g_m, rom, len, &cfg, NULL, &in, false, NULL, NULL, NULL, &w);
    char why[160];
    if (!g_differs && c_st != rom_st) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "status: ROM %d, C %d", (int)rom_st, (int)c_st);
    }
    if (!g_differs && g_nevents != g_nrom) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "events: ROM %zu, C %zu", g_nrom, g_nevents);
    }
    take(&g_cur, &g_m, false, false);
    if (!g_differs && differs(&rom_end, &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "the end: %s", why);
    }
    if (g_differs && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s\n", name, g_why);
    g_runs++;
    g_total_idles += rom_idles;
    g_total_frames += rom_frames;
    if (x) {
        printf("  %-14s %s, %u idles, %u frame events, $%04X reached %u\n", x->name,
               rom_st == MACHINE_IDLE ? "idle" : rom_st == MACHINE_CYCLE_LIMIT ? "cycle limit" : "fault", rom_idles,
               rom_frames, x->target, g_target_hits);
        CHECK_EQ(rom_st, x->want);
        CHECK(g_target_hits > 0);
    } else {
        CHECK_EQ(rom_st, MACHINE_IDLE);
    }
}

static const char *const k_corpus[] = { CORPUS_NAMES };

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void suite_diff_main(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    g_mismatches = g_runs = g_total_idles = g_total_frames = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        run_pair(rom, len, k_corpus[c], text, NULL);
        free(text);
    }
    printf("  corpus: %u texts, %u idles, %u frame events compared\n", g_runs, g_total_idles, g_total_frames);
    for (size_t i = 0; i < diff_extra_run_count; i++)
        run_pair(rom, len, diff_extra_runs[i].name, diff_extra_runs[i].text, &diff_extra_runs[i]);
    printf("  %u runs, %u idles, %u frame events compared in all\n", g_runs, g_total_idles, g_total_frames);
    CHECK_EQ(g_mismatches, 0);
    free(g_rom_events);
    g_rom_events = NULL;
    g_nrom = g_cap = 0;
}
