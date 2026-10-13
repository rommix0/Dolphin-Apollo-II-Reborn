/* Live differential test of the routines that wait on the timer interrupt (M4.4b spec 4.1): par_play $B877,
   boot_loop_play $A7D4 and the frame waits isr_speech_start $B323, isr_wait_frames $B356 and isr_wait_frame $B465
   (M4.6b spec 2.4), the replacements marked `waits`, which diff_lts skips (it holds interrupts). Every corpus
   file and the boot run go through the ROM engine; at every call (no sampling) the machine is cloned twice with
   interrupts live. Copy A runs the ROM routine to its return, copy B the C thunk. Both record, at every buffer swap
   ($B306, the machine's frame callback), the produced frame and a snapshot: all RAM but the SFRs ($DA-$FF) and the
   stack page, the P2 latch and the UART transmit state. At a swap both main programs wait for isr_frame_flag, so
   the snapshots must agree. They must also agree at exit: PC, SP, A, X, Y, the P2 latch, the UART transmit state
   and all RAM but the SFRs, the dead stack below the exit SP and the timer interrupt's per-word counters
   isr_tick_div $59, isr_word_ofs $5A and isr_words_left $5B (they depend on how far into a frame each copy
   returns; every swap has compared them already).
   par_play re-enters $B877 by its own JMP par_play ($B892, $B8AE, $B914) with the same stack, so only a real call
   counts: the hook on the JSR par_play at $B2C7 (boot_play_body) arms the next $B877 entry. So do the JSRs of
   isr_wait_frames ($B352) and isr_wait_frame ($B2BF, $B2CD, $B8D2, and $B356, the first instruction of
   isr_wait_frames), whose own loops re-enter them (BNE $B356, BBC0 $B465). boot_loop_play and isr_speech_start are
   entered only by JSR. In a corpus file the boot's own call (from boot_selftest_sound $A7BA) and the calls inside it
   are the boot run's; they are checked there only.
   The receive interrupt's two effects that no corpus text reaches are injected into both copies at the frame event
   (M4.6b spec 4.2), on the first call of each routine in each run whose ROM copy swapped the buffers at least
   twice. The frame event is the machine's frame callback in both copies: the ROM timer interrupt reaches $B306, the
   C one (isr_timer_dsp, hooked in copy B once it is a replacement) calls frame_done, which the bridge turns into
   machine_capture_frame. A reply due: ser_status_due = 1 at the second frame event (ser_tx_service from $B89C at
   every pass after it). A mute: due at the second frame event, ser_abort bit 0 is set on entering the timer
   interrupt ABORT_TICKS times after it ($B2D9, as the CPU enters it: a hook in both copies, which in copy B runs the
   C handler after the injection once it is a replacement). By then the ROM has seen the frame flag and is building
   the frame (thousands of cycles) and the C, which builds it in no time, is waiting at its back edge, so both end
   at par_play's next abort test ($B8A2) with that one frame built (the end path from $B8A5). Not at the frame event
   itself: there the ROM sees the mute before or after the frame flag depending on the instruction the interrupt
   lands on (at $B8A8, between the two tests, it builds one more frame), which the C, taking no time between its
   tests, cannot reproduce. Nor at an isr_par_ready entry ($C5D6), the point used until M4.6b: the C par_play calls
   the C isr_par_ready (M4.6b), so its copy never executes $C5D6. A hook on copy A counts the calls that executed
   $B8A5 and $B89C. No copy runs past the routine's return: after a ser_status_due injection the ROM never reaches
   idle again (boot_get_clause waits for the host at $A6D4).
   A self-check runs the ROM routine in copy B too, on hello, both injections included: it proves the comparison
   and the injection machinery whatever the replacement table holds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/isr_addr.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
#define PLAY_CALL_SITE     0xB2C7          /* boot_play_body: JSR par_play */
#define PLAY_REPLY         0xB89C          /* par_play: JSR ser_tx_service (a reply due) */
#define PLAY_ABORT         0xB8A5          /* par_play: JMP $B8D2 (ser_abort bit 0: the end) */
#define BOOT_SOUND_RETURN  0xA7BD          /* after boot_selftest_sound's JSR boot_loop_play at $A7BA */
#define FRAMES_CALL_SITE   0xB352          /* isr_speech_start: JSR isr_wait_frames */
#define INJECT_SWAP        1               /* the second swap of a call */
#define ABORT_TICKS        2               /* the mute: at the second timer interrupt after that swap */
#define WATCH_MAX          5

/* The JSR isr_wait_frame sites but the one at isr_wait_frames' entry: boot_play_body's two and par_play's */
static const uint16_t k_frame_call_sites[] = { 0xB2BF, 0xB2CD, 0xB8D2 };

static const char *const k_corpus[] = { CORPUS_NAMES };

typedef enum { INJECT_NONE, INJECT_ABORT, INJECT_REPLY } inject_kind;
static const char *const k_inject[] = { "", " (ser_abort injected)", " (ser_status_due injected)" };

/* The state of one copy at one buffer swap. */
typedef struct {
    uint8_t frame[APOLLO_FRAME_BYTES];
    uint8_t ram[MACHINE_RAM_SIZE];
    uint8_t p2;
    uint32_t tx_count;
    uint8_t tx_last;
} swap_state;

/* The frame callback's context: copy A records every swap, copy B compares each with copy A's. */
typedef struct {
    machine *m;
    inject_kind inject;
    bool ref;
    size_t swaps;
    unsigned abort_in; /* the abort injection: at the abort_in-th timer interrupt entry from now (0: none due) */
    bool differs;      /* copy B: the first difference is in why */
    char why[160];
    const fw_replacement *isr;   /* copy B: the timer interrupt's replacement, run by the $B2D9 hook (NULL: ROM) */
    fw_bridge *bridge;           /* ... on this bridge, the one the watched routine's thunk runs on */
} recorder;

/* A routine watched at its ROM entry; copy B runs it through run_b. */
typedef struct watch {
    const char *name;
    uint16_t addr;
    bool (*run_b)(machine *m, const struct watch *w);
    const fw_replacement *r;   /* the replacement whose thunk copy B runs (NULL: the ROM routine) */
    unsigned calls, injected;
    bool done_injecting;       /* per run */
} watch;

static machine g_m, g_a, g_b;
static fw_bridge g_bridge;     /* copy B's */
static swap_state *g_ref;     /* copy A's swaps: g_ref_n of them, room for g_ref_cap */
static size_t g_ref_n, g_ref_cap;
static swap_state g_cur;
static watch g_watch[WATCH_MAX];
static size_t g_nwatch;
static const char *g_source;
static bool g_boot_run, g_blp_checked, g_play_armed, g_frames_armed, g_frame_armed;
static unsigned g_mismatches, g_reached_abort, g_reached_reply;
static uint64_t g_isr_c;       /* timer interrupts copy B ran in C */
static bool g_hit_abort, g_hit_reply;

static void take(swap_state *s, const machine *m, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    memcpy(s->frame, frame, APOLLO_FRAME_BYTES);
    memcpy(s->ram, m->ram, MACHINE_RAM_SIZE);
    s->p2 = m->sfr.port_latch[2];
    s->tx_count = m->sfr.uart_tx_count;
    s->tx_last = m->sfr.uart_tx_last;
}

/* The first difference between two swap states (the SFRs and the stack page excluded), or false. */
static bool swap_differs(const swap_state *a, const swap_state *b, char *why, size_t n)
{
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        if (a->frame[i] != b->frame[i]) {
            snprintf(why, n, "frame byte %d: ROM %02X, C %02X", i, a->frame[i], b->frame[i]);
            return true;
        }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if ((addr >= M50734_SFR_FIRST && addr <= 0xFF) || (addr >= 0x100 && addr <= 0x1FF))
            continue;
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    if (a->p2 != b->p2) {
        snprintf(why, n, "P2 latch: ROM %02X, C %02X", a->p2, b->p2);
        return true;
    }
    if (a->tx_count != b->tx_count || a->tx_last != b->tx_last) {
        snprintf(why, n, "UART TX: ROM %u/%02X, C %u/%02X", (unsigned)a->tx_count, a->tx_last, (unsigned)b->tx_count,
                 b->tx_last);
        return true;
    }
    return false;
}

/* At $B306 (isr_swap_buffers): at the second swap the reply injection is made and the abort injection made due,
   then the swap's state is recorded (copy A) or compared with copy A's (copy B). */
static void on_swap(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    recorder *rec = user;
    machine *m = rec->m;
    size_t i = rec->swaps++;
    if (i == INJECT_SWAP && rec->inject == INJECT_ABORT)
        rec->abort_in = ABORT_TICKS;
    if (i == INJECT_SWAP && rec->inject == INJECT_REPLY)
        m->ram[SER_STATUS_DUE] = 1;
    if (rec->ref) {
        if (i == g_ref_cap) {
            g_ref_cap = g_ref_cap ? g_ref_cap * 2 : 256;
            g_ref = realloc(g_ref, g_ref_cap * sizeof *g_ref);
            if (!g_ref)
                abort();
        }
        take(&g_ref[i], m, frame);
        return;
    }
    if (rec->differs)
        return;
    take(&g_cur, m, frame);
    char why[140];
    if (i >= g_ref_n) {
        rec->differs = true;
        snprintf(rec->why, sizeof rec->why, "swap %zu: the C swapped more often than the ROM", i);
    } else if (swap_differs(&g_ref[i], &g_cur, why, sizeof why)) {
        rec->differs = true;
        snprintf(rec->why, sizeof rec->why, "swap %zu: %s", i, why);
    }
}

/* $B2D9, isr_timer_dsp entered by the CPU (the hook fires after the interrupt entry): the abort injection, at the
   ABORT_TICKS-th entry after the second swap; then the handler, ROM (false) or the C replacement on copy B's
   bridge. */
static bool on_timer(machine *m, void *user)
{
    recorder *rec = user;
    if (rec->abort_in && --rec->abort_in == 0)
        m->ram[SER_ABORT] |= 1;
    if (!rec->isr)
        return false;
    g_isr_c++;
    return rec->isr->thunk(m, rec->bridge);
}

/* The copy-A hooks: which of the two injected branches the ROM routine executed. */
static bool hit_abort(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_hit_abort = true;
    return false;
}

static bool hit_reply(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_hit_reply = true;
    return false;
}

/* Copy B, the ROM routine (the self-check). */
static bool run_rom(machine *m, const watch *w)
{
    (void)w;
    return machine_finish_routine(m, PLAY_BUDGET);
}

/* Copy B, the C thunk of the replacement, on the bridge bound to this copy (g_bridge, check_call). */
static bool run_thunk_b(machine *m, const watch *w)
{
    w->r->thunk(m, &g_bridge);
    return m->fault == MFAULT_NONE;
}

/* The exit states: PC, SP, A, X, Y, the P2 latch, the UART transmit state and RAM (spec 4.1). */
static bool exit_differs(const machine *a, const machine *b, char *why, size_t n)
{
    if (a->cpu.pc != b->cpu.pc || a->cpu.s != b->cpu.s) {
        snprintf(why, n, "exit PC/S: ROM %04X/%02X, C %04X/%02X", a->cpu.pc, a->cpu.s, b->cpu.pc, b->cpu.s);
        return true;
    }
    if (a->cpu.a != b->cpu.a || a->cpu.x != b->cpu.x || a->cpu.y != b->cpu.y) {
        snprintf(why, n, "exit A/X/Y: ROM %02X/%02X/%02X, C %02X/%02X/%02X", a->cpu.a, a->cpu.x, a->cpu.y, b->cpu.a,
                 b->cpu.x, b->cpu.y);
        return true;
    }
    if (a->sfr.port_latch[2] != b->sfr.port_latch[2]) {
        snprintf(why, n, "exit P2 latch: ROM %02X, C %02X", a->sfr.port_latch[2], b->sfr.port_latch[2]);
        return true;
    }
    if (a->sfr.uart_tx_count != b->sfr.uart_tx_count || a->sfr.uart_tx_last != b->sfr.uart_tx_last) {
        snprintf(why, n, "exit UART TX: ROM %u/%02X, C %u/%02X", (unsigned)a->sfr.uart_tx_count,
                 a->sfr.uart_tx_last, (unsigned)b->sfr.uart_tx_count, b->sfr.uart_tx_last);
        return true;
    }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
            continue;
        if (addr >= 0x100 && addr <= 0x100u + a->cpu.s)
            continue;   /* the dead stack below the exit SP */
        if (addr >= ISR_TICK_DIV && addr <= ISR_WORDS_LEFT)
            continue;   /* the per-word counters: how far into a frame each copy returned */
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "exit RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    return false;
}

/* One call of w from m's state, with one injection kind. Returns the swaps copy A made. */
static size_t check_call(machine *m, watch *w, unsigned call, inject_kind inject)
{
    char why[200] = "";
    recorder ra = { &g_a, inject, true, 0, 0, false, "", NULL, NULL };
    recorder rb = { &g_b, inject, false, 0, 0, false, "", NULL, &g_bridge };
    machine_clone(&g_a, m);
    g_hit_abort = g_hit_reply = false;
    CHECK(machine_hook(&g_a, PLAY_ABORT, hit_abort, NULL));
    CHECK(machine_hook(&g_a, PLAY_REPLY, hit_reply, NULL));
    CHECK(machine_hook(&g_a, ROM_ISR_TIMER_DSP, on_timer, &ra));
    machine_set_outputs(&g_a, on_swap, &ra);
    bool a_ok = machine_finish_routine(&g_a, PLAY_BUDGET);
    g_ref_n = ra.swaps;
    g_reached_abort += g_hit_abort;
    g_reached_reply += g_hit_reply;
    machine_clone(&g_b, m);
    CHECK(fw_bridge_init(&g_bridge, &g_b));
    if (w->r)
        rb.isr = fw_replacement_find("isr_timer_dsp");   /* the C handler, once it is a replacement (M4.6b) */
    CHECK(machine_hook(&g_b, ROM_ISR_TIMER_DSP, on_timer, &rb));
    machine_set_outputs(&g_b, on_swap, &rb);
    bool b_ok = w->run_b(&g_b, w);
    if (!a_ok)
        snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_a.fault);
    else if (!b_ok)
        snprintf(why, sizeof why, "copy B did not return (fault %d at $%04X)", (int)g_b.fault, g_b.fault_addr);
    else if (rb.differs)
        snprintf(why, sizeof why, "%s", rb.why);
    else if (ra.swaps != rb.swaps)
        snprintf(why, sizeof why, "swaps: ROM %zu, C %zu", ra.swaps, rb.swaps);
    else
        exit_differs(&g_a, &g_b, why, sizeof why);
    if (why[0] && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s call %u%s: %s\n", g_source, w->name, call, k_inject[inject], why);
    return ra.swaps;
}

/* Every call of w is checked; the first that swaps at least twice gets the two injected pairs too. */
static void check_all(machine *m, watch *w)
{
    unsigned call = w->calls++;
    if (check_call(m, w, call, INJECT_NONE) <= INJECT_SWAP || w->done_injecting)
        return;
    check_call(m, w, call, INJECT_ABORT);
    check_call(m, w, call, INJECT_REPLY);
    w->injected++;
    w->done_injecting = true;
}

static watch *watched(uint16_t addr)
{
    for (size_t i = 0; i < g_nwatch; i++)
        if (g_watch[i].addr == addr)
            return &g_watch[i];
    return NULL;
}

/* The address the routine just entered by JSR returns to (the pushed address + 1). */
static uint16_t return_address(const machine *m)
{
    uint8_t lo = m->ram[0x100 + (uint8_t)(m->cpu.s + 1)], hi = m->ram[0x100 + (uint8_t)(m->cpu.s + 2)];
    return (uint16_t)((lo | hi << 8) + 1);
}

/* $A7D4: a boot_loop_play call. The boot's own call is checked in the boot run only, with the par_play inside it. */
static bool on_loop_play(machine *m, void *user)
{
    (void)user;
    g_blp_checked = g_boot_run || return_address(m) != BOOT_SOUND_RETURN;
    watch *w = watched(ROM_BOOT_LOOP_PLAY);
    if (w && g_blp_checked)
        check_all(m, w);
    return false;   /* the ROM routine runs on in m */
}

/* $B2C7: boot_play_body's JSR par_play arms the next $B877 entry. */
static bool on_play_call(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_play_armed = g_blp_checked;
    return false;
}

/* $B877: par_play, a real call only when armed (its own JMP par_play re-enters here with the same stack). */
static bool on_play(machine *m, void *user)
{
    (void)user;
    if (!g_play_armed)
        return false;
    g_play_armed = false;
    watch *w = watched(ROM_PAR_PLAY);
    if (w)
        check_all(m, w);
    return false;
}

/* The watch of the routine at addr checks this call, when it is watched. */
static void check_watched(machine *m, uint16_t addr)
{
    watch *w = watched(addr);
    if (w)
        check_all(m, w);
}

/* $B323: isr_speech_start, inside a boot_loop_play call checked here. */
static bool on_speech_start(machine *m, void *user)
{
    (void)user;
    if (g_blp_checked)
        check_watched(m, ROM_ISR_SPEECH_START);
    return false;
}

/* $B352: isr_speech_start's JSR isr_wait_frames arms the next $B356 entry. */
static bool on_frames_call(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_frames_armed = g_blp_checked;
    return false;
}

/* $B356: isr_wait_frames, a real call only when armed (its BNE $B356 re-enters here); its own JSR isr_wait_frame
   arms the next $B465 entry, at every pass. */
static bool on_frames(machine *m, void *user)
{
    (void)user;
    if (g_frames_armed) {
        g_frames_armed = false;
        check_watched(m, ROM_ISR_WAIT_FRAMES);
    }
    g_frame_armed = g_blp_checked;
    return false;
}

/* $B2BF, $B2CD, $B8D2: a JSR isr_wait_frame arms the next $B465 entry. */
static bool on_frame_call(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_frame_armed = g_blp_checked;
    return false;
}

/* $B465: isr_wait_frame, a real call only when armed (its BBC0 $B465 re-enters here). */
static bool on_frame(machine *m, void *user)
{
    (void)user;
    if (g_frame_armed) {
        g_frame_armed = false;
        check_watched(m, ROM_ISR_WAIT_FRAME);
    }
    return false;
}

static void install(machine *m, void *user)
{
    (void)user;
    CHECK(machine_hook(m, ROM_BOOT_LOOP_PLAY, on_loop_play, NULL));
    CHECK(machine_hook(m, PLAY_CALL_SITE, on_play_call, NULL));
    CHECK(machine_hook(m, ROM_PAR_PLAY, on_play, NULL));
    CHECK(machine_hook(m, ROM_ISR_SPEECH_START, on_speech_start, NULL));
    CHECK(machine_hook(m, FRAMES_CALL_SITE, on_frames_call, NULL));
    CHECK(machine_hook(m, ROM_ISR_WAIT_FRAMES, on_frames, NULL));
    for (size_t i = 0; i < sizeof k_frame_call_sites / sizeof k_frame_call_sites[0]; i++)
        CHECK(machine_hook(m, k_frame_call_sites[i], on_frame_call, NULL));
    CHECK(machine_hook(m, ROM_ISR_WAIT_FRAME, on_frame, NULL));
}

static void run(const uint8_t *rom, size_t len, const char *source, const char *text, bool boot)
{
    frame_list fl = { 0 };
    g_source = source;
    g_boot_run = boot;
    g_blp_checked = g_play_armed = g_frames_armed = g_frame_armed = false;
    for (size_t i = 0; i < g_nwatch; i++)
        g_watch[i].done_injecting = false;
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

static void run_corpus(const uint8_t *rom, size_t len)
{
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        run(rom, len, k_corpus[c], text, false);
        free(text);
    }
    run(rom, len, "boot", "", true);
}

static void add_watch(const char *name, uint16_t addr, const fw_replacement *r)
{
    watch *w = &g_watch[g_nwatch++];
    memset(w, 0, sizeof *w);
    w->name = name;
    w->addr = addr;
    w->r = r;
    w->run_b = r ? run_thunk_b : run_rom;
}

/* ROM against ROM on hello: the five routines, both injections; every pair must agree and both branches be
   reached. */
static void self_check(const uint8_t *rom, size_t len)
{
    char *text = test_read_file("tests/corpus/hello.txt");
    CHECK(text != NULL);
    if (!text)
        return;
    strip_newline(text);
    g_nwatch = 0;
    add_watch("par_play", ROM_PAR_PLAY, NULL);
    add_watch("boot_loop_play", ROM_BOOT_LOOP_PLAY, NULL);
    add_watch("isr_speech_start", ROM_ISR_SPEECH_START, NULL);
    add_watch("isr_wait_frames", ROM_ISR_WAIT_FRAMES, NULL);
    add_watch("isr_wait_frame", ROM_ISR_WAIT_FRAME, NULL);
    g_reached_abort = g_reached_reply = 0;
    run(rom, len, "self-check hello", text, false);
    free(text);
    printf("  self-check (ROM against ROM, hello): par_play %u calls, boot_loop_play %u calls, %u injected; $B8A5 "
           "reached %u, $B89C reached %u\n", g_watch[0].calls, g_watch[1].calls, g_watch[0].injected +
           g_watch[1].injected, g_reached_abort, g_reached_reply);
    printf("  self-check (ROM against ROM, hello): isr_speech_start %u calls, isr_wait_frames %u, isr_wait_frame %u\n",
           g_watch[2].calls, g_watch[3].calls, g_watch[4].calls);
    for (size_t i = 0; i < g_nwatch; i++)
        CHECK(g_watch[i].calls > 0);
    CHECK_EQ(g_watch[0].injected, 1);
    CHECK_EQ(g_watch[1].injected, 1);
    CHECK(g_reached_abort > 0);
    CHECK(g_reached_reply > 0);
}

void suite_diff_play(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    g_mismatches = 0;
    self_check(rom, len);
    g_nwatch = 0;
    for (size_t i = 0; i < fw_replacement_count; i++) {
        const fw_replacement *r = &fw_replacements[i];
        if (!r->waits)
            continue;
        CHECK(g_nwatch < WATCH_MAX && (r->addr == ROM_PAR_PLAY || r->addr == ROM_BOOT_LOOP_PLAY ||
                                       r->addr == ROM_ISR_SPEECH_START || r->addr == ROM_ISR_WAIT_FRAMES ||
                                       r->addr == ROM_ISR_WAIT_FRAME));
        if (g_nwatch < WATCH_MAX)
            add_watch(r->name, r->addr, r);
    }
    printf("  %zu routines wait on the timer interrupt\n", g_nwatch);
    CHECK_EQ(g_nwatch, 5);   /* par_play and boot_loop_play (M4.4b spec 2.5), the frame waits (M4.6b spec 2.4) */
    if (g_nwatch > 0) {
        g_reached_abort = g_reached_reply = 0;
        g_isr_c = 0;
        run_corpus(rom, len);
        for (size_t i = 0; i < g_nwatch; i++) {
            printf("  %-20s %u calls checked, %u with both injections\n", g_watch[i].name, g_watch[i].calls,
                   g_watch[i].injected);
            CHECK(g_watch[i].calls > 0);
        }
        printf("  $B8A5 (abort) reached in %u calls, $B89C (reply due) in %u\n", g_reached_abort, g_reached_reply);
        CHECK(g_reached_abort > 0);
        CHECK(g_reached_reply > 0);
        printf("  %llu timer interrupts run in C in copy B\n", (unsigned long long)g_isr_c);
        CHECK(g_isr_c > 0);   /* isr_timer_dsp is a replacement (M4.6b): copy B's interrupts are C */
    }
    CHECK_EQ(g_mismatches, 0);
    free(g_ref);
    g_ref = NULL;
    g_ref_n = g_ref_cap = 0;
}
