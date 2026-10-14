/* The native engine against the all-C hybrid (M5a spec 4.3), state for state and cycle for cycle. Each run goes
   through the hybrid engine first, whose watch records each event in order (an input idle or a frame event, the
   injection due at it made first) and a snapshot: its kind; all RAM but the SFR bytes and the stack page $0100-$01FF
   (the native board pushes no return frames: the C never reads them); the whole m50734 state (memcmp of the struct,
   the first differing field named); the cycle count; the DSP outputs (the port writes, the raw position and bytes,
   the reset pulses, the partial bytes they dropped and the reset level); frames_produced and raw_frames; the I flag;
   the serial queue (its bytes, head and tail) and the busy level (known, and its value). The same input then goes
   through the native engine, whose watch makes the same injection and compares its k-th event with the hybrid's k-th:
   the same kind and the same snapshot; the first difference ends the comparison of that run. At the end both have the
   same status, the same number of events and the same final snapshot (one more event), and the native engine made as
   many bank entries as the hybrid unwound C call chains.
   The runs: every corpus text, the extra runs diff_main makes (diff_runs.c: their configurations and injections; their
   ROM targets are diff_main's to check, as the all-C hybrid runs none of those addresses as ROM), and the Review
   Focus runs of the M5a plan: the longest text the CLI accepts (1500 characters, no full stop), and bank commands with
   re-entries. No run here reaches the NUL bug's fallback (no corpus file holds a NUL), where the two engines end
   differently by design (suite_native pins the native end). Needs the ROM for the hybrid. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_runs.h"
#include "engine/hybrid_engine.h"
#include "engine/native_engine.h"

/* One event of a run. */
typedef struct {
    bool idle, frame;                 /* an input idle, a frame event, or neither: the end of the run */
    uint8_t ram[BOARD_RAM_SIZE];
    m50734 sfr;
    uint64_t cycles;
    uint64_t port_writes, dsp_resets, raw_discarded, frames_produced, raw_frames;
    unsigned raw_pos;
    uint8_t raw_buf[BOARD_FRAME_BYTES];
    bool dsp_reset_level, i;
    uint8_t queue[BOARD_QUEUE_SIZE];
    size_t q_head, q_tail;
    bool busy_level_known;
    uint8_t busy_idle_level;
} snapshot;

/* A run of this suite alone: its input, the status both engines end with and the bank entries the native engine
   makes. */
typedef struct {
    const char *name, *text;
    engine_status want;
    unsigned entries;
} focus_run;

static machine g_m;
static native_engine g_e;
static snapshot *g_hyb;               /* the hybrid run's events: g_nhyb of them, room for g_cap */
static size_t g_nhyb, g_cap;
static snapshot g_cur, g_end;         /* the native event being compared; the hybrid's end */
static const extra_run *g_extra;      /* the run's injection, or NULL */
static unsigned g_idles, g_frames, g_speech_frames;
static size_t g_nevents;
static bool g_differs;
static char g_why[240];
static unsigned g_mismatches, g_runs, g_total_idles, g_total_frames;

static void take_machine(snapshot *s, const machine *m, bool idle, bool frame)
{
    s->idle = idle;
    s->frame = frame;
    memcpy(s->ram, m->ram, sizeof s->ram);
    s->sfr = m->sfr;
    s->cycles = m->cpu.cycles;
    s->port_writes = m->port_writes;
    s->dsp_resets = m->dsp_resets;
    s->raw_discarded = m->raw_discarded;
    s->frames_produced = m->frames_produced;
    s->raw_frames = m->raw_frames;
    s->raw_pos = m->raw_pos;
    memcpy(s->raw_buf, m->raw_buf, sizeof s->raw_buf);
    s->dsp_reset_level = m->dsp_reset_level;
    s->i = (m->cpu.p & CPU740_I) != 0;
    memcpy(s->queue, m->queue, sizeof s->queue);
    s->q_head = m->q_head;
    s->q_tail = m->q_tail;
    s->busy_level_known = m->busy_level_known;
    s->busy_idle_level = m->busy_idle_level;
}

static void take_board(snapshot *s, const board *b, bool idle, bool frame)
{
    s->idle = idle;
    s->frame = frame;
    memcpy(s->ram, b->ram, sizeof s->ram);
    s->sfr = b->sfr;
    s->cycles = b->cycles;
    s->port_writes = b->port_writes;
    s->dsp_resets = b->dsp_resets;
    s->raw_discarded = b->raw_discarded;
    s->frames_produced = b->frames_produced;
    s->raw_frames = b->raw_frames;
    s->raw_pos = b->raw_pos;
    memcpy(s->raw_buf, b->raw_buf, sizeof s->raw_buf);
    s->dsp_reset_level = b->dsp_reset_level;
    s->i = b->i;
    memcpy(s->queue, b->queue, sizeof s->queue);
    s->q_head = b->q_head;
    s->q_tail = b->q_tail;
    s->busy_level_known = b->busy_level_known;
    s->busy_idle_level = b->busy_idle_level;
}

/* Every field of m50734, to name the first one that differs. */
#define SFR_FIELD(f) { #f, offsetof(m50734, f), sizeof ((m50734 *)0)->f }
static const struct { const char *name; size_t off, size; } k_sfr_fields[] = {
    SFR_FIELD(port_latch), SFR_FIELD(port_dir), SFR_FIELD(port_in), SFR_FIELD(p0_func), SFR_FIELD(p23_func),
    SFR_FIELD(uart_baud), SFR_FIELD(ad_ctrl), SFR_FIELD(icr1), SFR_FIELD(icr2), SFR_FIELD(icr3),
    SFR_FIELD(timer_pre), SFR_FIELD(timer_reload), SFR_FIELD(timer_count), SFR_FIELD(timer_running),
    SFR_FIELD(uart_ctrl), SFR_FIELD(uart_rx), SFR_FIELD(uart_tx_last), SFR_FIELD(uart_rx_full),
    SFR_FIELD(uart_tx_count), SFR_FIELD(other),
};
#undef SFR_FIELD

/* The first difference between the hybrid's snapshot a and the native one b, or false. */
static bool differs(const snapshot *a, const snapshot *b, char *why, size_t n)
{
    if (a->idle != b->idle || a->frame != b->frame) {
        snprintf(why, n, "event kind: hybrid %s, native %s", a->idle ? "idle" : a->frame ? "frame" : "end",
                 b->idle ? "idle" : b->frame ? "frame" : "end");
        return true;
    }
    if (a->cycles != b->cycles) {
        snprintf(why, n, "cycles: hybrid %llu, native %llu", (unsigned long long)a->cycles,
                 (unsigned long long)b->cycles);
        return true;
    }
    if (a->i != b->i) {
        snprintf(why, n, "I: hybrid %d, native %d", a->i, b->i);
        return true;
    }
    if (memcmp(&a->sfr, &b->sfr, sizeof a->sfr) != 0) {
        const char *field = "padding";
        for (size_t k = 0; k < sizeof k_sfr_fields / sizeof k_sfr_fields[0]; k++) {
            if (memcmp((const uint8_t *)&a->sfr + k_sfr_fields[k].off, (const uint8_t *)&b->sfr + k_sfr_fields[k].off,
                       k_sfr_fields[k].size) != 0) {
                field = k_sfr_fields[k].name;
                break;
            }
        }
        snprintf(why, n, "m50734 %s", field);
        return true;
    }
    if (a->port_writes != b->port_writes || a->raw_pos != b->raw_pos ||
        memcmp(a->raw_buf, b->raw_buf, sizeof a->raw_buf) != 0 || a->dsp_resets != b->dsp_resets ||
        a->raw_discarded != b->raw_discarded || a->dsp_reset_level != b->dsp_reset_level) {
        snprintf(why, n, "DSP port writes/raw position/resets: hybrid %llu/%u/%llu, native %llu/%u/%llu (or the raw "
                 "bytes, the dropped bytes or the reset level)", (unsigned long long)a->port_writes, a->raw_pos,
                 (unsigned long long)a->dsp_resets, (unsigned long long)b->port_writes, b->raw_pos,
                 (unsigned long long)b->dsp_resets);
        return true;
    }
    if (a->frames_produced != b->frames_produced || a->raw_frames != b->raw_frames) {
        snprintf(why, n, "frames produced/raw: hybrid %llu/%llu, native %llu/%llu",
                 (unsigned long long)a->frames_produced, (unsigned long long)a->raw_frames,
                 (unsigned long long)b->frames_produced, (unsigned long long)b->raw_frames);
        return true;
    }
    if (a->q_head != b->q_head || a->q_tail != b->q_tail || memcmp(a->queue, b->queue, sizeof a->queue) != 0 ||
        a->busy_level_known != b->busy_level_known || a->busy_idle_level != b->busy_idle_level) {
        snprintf(why, n, "serial queue head/tail: hybrid %zu/%zu, native %zu/%zu (or its bytes or the busy level)",
                 a->q_head, a->q_tail, b->q_head, b->q_tail);
        return true;
    }
    for (uint32_t addr = 0; addr < BOARD_RAM_SIZE; addr++) {
        if ((addr >= M50734_SFR_FIRST && addr <= 0xFF) || (addr >= 0x100 && addr <= 0x1FF))
            continue;
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: hybrid %02X, native %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    return false;
}

/* The injection due at this event (the k-th idle, or the n-th frame event after the first idle). */
static void inject(bool idle, unsigned k, uint8_t *ram, m50734 *sfr, uint8_t *queue, size_t *q_head, size_t *q_tail)
{
    unsigned at = idle ? k : g_idles ? g_speech_frames++ : ~0u;
    if (g_extra && g_extra->inject && (g_extra->at == AT_IDLE) == idle && g_extra->n == at) {
        diff_view v = { ram, sfr, queue, q_head, q_tail };
        g_extra->inject(&v);
    }
}

static void hybrid_event(machine *m, bool idle)
{
    unsigned k = idle ? g_idles++ : g_frames++;
    inject(idle, k, m->ram, &m->sfr, m->queue, &m->q_head, &m->q_tail);
    if (g_nhyb == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 1024;
        g_hyb = realloc(g_hyb, g_cap * sizeof *g_hyb);
        if (!g_hyb)
            abort();
    }
    take_machine(&g_hyb[g_nhyb++], m, idle, !idle);
}

static void native_event(board *b, bool idle)
{
    unsigned k = idle ? g_idles++ : g_frames++;
    inject(idle, k, b->ram, &b->sfr, b->queue, &b->q_head, &b->q_tail);
    size_t i = g_nevents++;
    if (g_differs)
        return;
    take_board(&g_cur, b, idle, !idle);
    char why[200];
    if (i >= g_nhyb) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu: native has more events than the hybrid (%zu)", i, g_nhyb);
    } else if (differs(&g_hyb[i], &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu (%s %u): %s", i, idle ? "idle" : "frame", k, why);
    }
}

static void hybrid_frame(void *user, machine *m)
{
    (void)user;
    hybrid_event(m, false);
}

static void hybrid_idle(void *user, machine *m)
{
    (void)user;
    hybrid_event(m, true);
}

static void native_frame(void *user, board *b)
{
    (void)user;
    native_event(b, false);
}

static void native_idle(void *user, board *b)
{
    (void)user;
    native_event(b, true);
}

static const char *status_name(int st)
{
    return st == ENGINE_IDLE ? "idle" : st == ENGINE_CYCLE_LIMIT ? "cycle limit" : "fault";
}

/* One input on both engines; x: the extra run whose configuration and injection it has (NULL: none); label: the line
   to print for it (NULL: a corpus text, nothing printed). Returns the native status. */
static engine_status run_pair(const uint8_t *rom, size_t len, const char *name, const char *text, const extra_run *x,
                              const char *label)
{
    machine_config cfg;
    native_config ncfg;
    machine_default_config(&cfg);
    native_default_config(&ncfg);
    if (x) {
        const uint8_t or_in[5] = { x->p0, 0, x->p2, x->p3, x->p4 };
        for (int p = 0; p < 5; p++) {
            cfg.port_in[p] |= or_in[p];
            ncfg.port_in[p] |= or_in[p];
        }
    }
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    engine_watch hw = { hybrid_frame, hybrid_idle, NULL };
    native_watch nw = { native_frame, native_idle, NULL };
    hybrid_stats stats;
    g_extra = x;

    g_nhyb = 0;
    g_idles = g_frames = g_speech_frames = 0;
    machine_status h_st = hybrid_engine_run_input(&g_m, rom, len, &cfg, NULL, &in, false, NULL, NULL, &stats, &hw);
    take_machine(&g_end, &g_m, false, false);   /* the end of the run, one more event */
    unsigned h_idles = g_idles, h_frames = g_frames;

    g_nevents = 0;
    g_idles = g_frames = g_speech_frames = 0;
    g_differs = false;
    engine_status n_st = native_engine_run_input(&g_e, &ncfg, &in, false, NULL, NULL, &nw);
    char why[200];
    if (!g_differs && (int)n_st != (int)h_st) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "status: hybrid %s, native %s (fault %d at $%04X)", status_name((int)h_st),
                 status_name((int)n_st), (int)g_e.b.fault, g_e.b.fault_addr);
    }
    if (!g_differs && g_nevents != g_nhyb) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "events: hybrid %zu, native %zu", g_nhyb, g_nevents);
    }
    take_board(&g_cur, &g_e.b, false, false);
    if (!g_differs && differs(&g_end, &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "the end: %s", why);
    }
    if (!g_differs && g_e.unwinds != stats.unwinds) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "bank entries: hybrid %u, native %u", stats.unwinds, g_e.unwinds);
    }
    if (g_differs && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s\n", name, g_why);
    g_runs++;
    g_total_idles += h_idles;
    g_total_frames += h_frames;
    if (label)
        printf("  %-16s %s, %u idles, %u frame events, %u bank entries, %llu cycles\n", label,
               status_name((int)n_st), h_idles, h_frames, g_e.unwinds, (unsigned long long)g_e.b.cycles);
    return n_st;
}

static const char *const k_corpus[] = { CORPUS_NAMES };

#define WORDS44 "the quick brown fox jumps over the lazy dog "
#define WORDS440 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44

/* The M5a plan's Review Focus runs. */
static const focus_run k_focus_runs[] = {
    /* Review Focus 2: the longest text the CLI accepts, 1500 characters with no full stop (clauses cut at their
       length), close to the speaking phase's budget */
    { "rf2_longest", WORDS440 WORDS440 WORDS440 WORDS44 WORDS44 WORDS44 WORDS44 "dogs", ENGINE_IDLE, 0 },
    /* Review Focus 4: bank commands with re-entries: @=1 enters the main bank's warm entry (twice, the second from
       under the C bank_entry_warm, its C run afresh); @=5 is an empty socket; then every other bank number */
    { "rf4_reentries", "@=1 One. @=5 Two. @=1 Three.", ENGINE_IDLE, 2 },
    { "rf4_banks", "@=0 @=8 @=9 @=F @=G", ENGINE_IDLE, 1 },
};

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void suite_diff_native(void)
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
        CHECK_EQ(run_pair(rom, len, k_corpus[c], text, NULL, NULL), ENGINE_IDLE);
        free(text);
    }
    printf("  corpus: %u texts, %u idles, %u frame events compared\n", g_runs, g_total_idles, g_total_frames);
    for (size_t i = 0; i < diff_extra_run_count; i++) {
        const extra_run *x = &diff_extra_runs[i];
        CHECK_EQ(run_pair(rom, len, x->name, x->text, x, x->name), x->want);
    }
    for (size_t i = 0; i < sizeof k_focus_runs / sizeof k_focus_runs[0]; i++) {
        const focus_run *r = &k_focus_runs[i];
        CHECK_EQ(run_pair(rom, len, r->name, r->text, NULL, r->name), r->want);
        CHECK_EQ(g_e.unwinds, r->entries);
    }
    CHECK_EQ(strlen(k_focus_runs[0].text), 1500);
    printf("  %u runs, %u idles, %u frame events compared in all\n", g_runs, g_total_idles, g_total_frames);
    CHECK_EQ(g_mismatches, 0);
    free(g_hyb);
    g_hyb = NULL;
    g_nhyb = g_cap = 0;
}
