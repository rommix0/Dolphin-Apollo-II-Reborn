/* Several renders through one native engine, as the CLI makes them (M5b spec 4.5; the M5a deferred CLI-level test):
   corpus texts, the NUL bug's fault, an input overflow, then the corpus texts again, voice b and the raw stream among
   them. Each run's result is what a freshly zeroed engine gives for the same input. Then the CLI's reports as exact
   strings: board_print_fault's line for the NUL bug, the overflow and every other fault kind, and
   board_print_unmodelled's lines for injected unmodelled accesses and a full log. Needs no ROM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "render/render_native.h"
#include "firmware/cmd_addr.h"

#define REPORT_PATH "build/test_render_native.txt"
#define OVERFLOW_SPACES 4091   /* "@R4 ", these spaces and the CR: 4096 bytes, one more than the serial queue holds */

static native_engine g_e;       /* every run of the sequence */
static native_engine g_fresh;   /* zeroed before each run: what a fresh process gives */

/* What a report function writes, read back from a file. Caller frees. */
static char *report(void (*print)(const board *b, FILE *out), const board *b)
{
    FILE *f = fopen(REPORT_PATH, "wb");
    CHECK(f != NULL);
    if (!f)
        return NULL;
    print(b, f);
    fclose(f);
    return test_read_file(REPORT_PATH);
}

static void check_report(void (*print)(const board *b, FILE *out), const board *b, const char *want)
{
    char *got = report(print, b);
    CHECK(got != NULL);
    if (got)
        CHECK_STR(got, want);
    free(got);
}

/* One render of text on g_e, then on a zeroed g_fresh: the same status, samples, sample count, frame count and
   clipping, and for a fault the same fault kind and address. Returns g_e's status; a non-NULL count gets g_e's sample
   count. */
static engine_status render_same(const char *name, const char *text, const render_options *opt, size_t *count)
{
    render_result a = { 0 }, b = { 0 };
    engine_status sa = render_text_native(&g_e, text, opt, &a);
    memset(&g_fresh, 0, sizeof g_fresh);
    engine_status sb = render_text_native(&g_fresh, text, opt, &b);
    bool same = sa == sb && a.count == b.count && a.frames == b.frames && a.clipped == b.clipped &&
                (a.count == 0 || !memcmp(a.samples, b.samples, a.count * sizeof a.samples[0])) &&
                g_e.b.fault == g_fresh.b.fault && g_e.b.fault_addr == g_fresh.b.fault_addr;
    if (!same) {
        fprintf(stderr, "  render_native %s: used engine %d, %zu samples, fault %d at $%04X; fresh %d, %zu samples, "
                "fault %d at $%04X\n", name, (int)sa, a.count, (int)g_e.b.fault, g_e.b.fault_addr, (int)sb, b.count,
                (int)g_fresh.b.fault, g_fresh.b.fault_addr);
        g_test_failures++;
    }
    if (count)
        *count = a.count;
    render_result_free(&a);
    render_result_free(&b);
    return sa;
}

/* A corpus text through render_same with voice and stream; it must speak. */
static void corpus_same(const char *name, char voice, render_stream stream)
{
    char path[128];
    snprintf(path, sizeof path, "tests/corpus/%s.txt", name);
    char *text = test_read_file(path);
    CHECK(text != NULL);
    if (!text)
        return;
    char *eol = strpbrk(text, "\r\n");   /* the first line, as the CLI reads a --text-file */
    if (eol)
        *eol = '\0';
    render_options opt;
    render_default_options(&opt);
    opt.voice = voice;
    opt.stream = stream;
    CHECK_EQ(render_same(name, text, &opt, NULL), ENGINE_IDLE);
    CHECK(g_e.b.frames_produced > 0);
    free(text);
}

/* The NUL bug (M4.6c1 spec 2.5): "@A", a NUL and the CR, which no C string (so no render, no CLI text) can carry, run
   on g_e and on a zeroed g_fresh: the same firmware fault at $A893, no frame. Then its report line. */
static void nul_bug_same(void)
{
    static const uint8_t bytes[] = { '@', 'A', 0 };
    engine_input in = { bytes, sizeof bytes, true };
    frame_list fa = { 0 }, fb = { 0 };
    CHECK_EQ(native_engine_run_input(&g_e, NULL, &in, false, &fa, NULL, NULL), ENGINE_FAULT);
    memset(&g_fresh, 0, sizeof g_fresh);
    CHECK_EQ(native_engine_run_input(&g_fresh, NULL, &in, false, &fb, NULL, NULL), ENGINE_FAULT);
    CHECK_EQ(g_e.b.fault, BOARD_FAULT_FIRMWARE);
    CHECK_EQ(g_e.b.fault_addr, ROM_CMD_RUN_NEXT);
    CHECK(g_fresh.b.fault == g_e.b.fault && g_fresh.b.fault_addr == g_e.b.fault_addr);
    CHECK_EQ(g_fresh.b.cycles, g_e.b.cycles);
    CHECK(fa.count == 0 && fb.count == 0);
    check_report(board_print_fault, &g_e.b, "firmware fault at $A893 after 819837 cycles\n");
    frame_list_free(&fa);
    frame_list_free(&fb);
}

/* Input one byte longer than the serial queue holds, through render: input overflow, no samples. Then its line. */
static void overflow_same(void)
{
    static char spaces[OVERFLOW_SPACES + 1];
    memset(spaces, ' ', OVERFLOW_SPACES);
    render_options opt;
    render_default_options(&opt);
    size_t count = 1;
    CHECK_EQ(render_same("overflow", spaces, &opt, &count), ENGINE_FAULT);
    CHECK_EQ(count, 0);   /* the fault ends the run with no sample */
    CHECK_EQ(g_e.b.fault, BOARD_FAULT_INPUT_OVERFLOW);
    check_report(board_print_fault, &g_e.b, "firmware fault: input longer than the serial queue after 819809 cycles\n");
}

/* Every other fault kind's line, from a board with the fault set by hand. */
static void fault_lines(void)
{
    static const struct { board_fault fault; const char *want; } k_lines[] = {
        { BOARD_FAULT_NONE, "no firmware fault after 12345678 cycles\n" },
        { BOARD_FAULT_DME, "firmware fault: DME (data-memory strobe) enabled after 12345678 cycles\n" },
        { BOARD_FAULT_ESCAPED, "firmware fault: code run from a deselected ROM bank after 12345678 cycles\n" },
        { BOARD_FAULT_VECTOR, "firmware fault: unmapped vector $D300 after 12345678 cycles\n" },
        { BOARD_FAULT_INPUT_OVERFLOW, "firmware fault: input longer than the serial queue after 12345678 cycles\n" },
        { BOARD_FAULT_FIRMWARE, "firmware fault at $D300 after 12345678 cycles\n" },
    };
    static board b;
    for (size_t i = 0; i < sizeof k_lines / sizeof k_lines[0]; i++) {
        memset(&b, 0, sizeof b);
        b.cycles = 12345678;
        b.fault = k_lines[i].fault;
        b.fault_addr = 0xD300;
        check_report(board_print_fault, &b, k_lines[i].want);
    }
}

/* The unmodelled log: none after a corpus run; accesses injected on the used engine's board (a read and a write of
   $DA, a read of write-only UART_TX), each listed once; a full log's line. */
static void unmodelled_lines(void)
{
    CHECK_EQ(g_e.b.unmodelled_count, 0);
    check_report(board_print_unmodelled, &g_e.b, "");
    board_read_sfr(&g_e.b, 0xDA);
    board_write_sfr(&g_e.b, 0xDA, 0);
    board_read_sfr(&g_e.b, M50734_UART_TX);
    board_read_sfr(&g_e.b, 0xDA);   /* logged once */
    check_report(board_print_unmodelled, &g_e.b,
                 "unmodelled read $00DA\nunmodelled write $00DA\nunmodelled read $00E4\n");
    g_e.b.unmodelled_dropped = 5;
    check_report(board_print_unmodelled, &g_e.b,
                 "unmodelled read $00DA\nunmodelled write $00DA\nunmodelled read $00E4\n"
                 "unmodelled log full: 5 more accesses not listed\n");
}

/* The default synth is Orpheus: tests/orpheus/hello.frm through it gives the Unicorn oracle's hello.pcm, sample for
   sample. */
static void orpheus_synth(void)
{
    render_options opt;
    render_default_options(&opt);
    CHECK_EQ(opt.synth_kind, RENDER_SYNTH_ORPHEUS);
    char *hex = test_read_file("tests/orpheus/hello.frm");
    CHECK(hex != NULL);
    if (!hex)
        return;
    frame_list fl = { 0 };
    for (char *line = strtok(hex, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        uint8_t f[ENGINE_FRAME_BYTES];
        if (test_parse_hex(line, f, sizeof f))
            frame_list_add(&fl, f);
    }
    free(hex);
    static int16_t want[105 * SYNTH_FRAME_SAMPLES];
    FILE *pcm = fopen("tests/orpheus/hello.pcm", "rb");
    CHECK(pcm != NULL);
    size_t n = pcm ? fread(want, sizeof want[0], sizeof want / sizeof want[0], pcm) : 0;
    if (pcm)
        fclose(pcm);
    CHECK_EQ(fl.count, 105);
    opt.synth_kind = RENDER_SYNTH_ORPHEUS;
    render_result r;
    render_synthesize(&fl, &opt, &r);
    CHECK_EQ(r.count, n);
    CHECK(r.samples && n == r.count && memcmp(r.samples, want, n * sizeof want[0]) == 0);
    render_result_free(&r);
    frame_list_free(&fl);
}

void suite_render_native(void)
{
    orpheus_synth();
    corpus_same("hello", 'a', RENDER_STREAM_PRODUCED);
    corpus_same("numbers", 'b', RENDER_STREAM_PRODUCED);
    corpus_same("prose", 'a', RENDER_STREAM_RAW);
    nul_bug_same();
    overflow_same();
    corpus_same("hello", 'a', RENDER_STREAM_PRODUCED);
    corpus_same("numbers", 'b', RENDER_STREAM_PRODUCED);
    corpus_same("prose", 'a', RENDER_STREAM_RAW);
    unmodelled_lines();
    fault_lines();
    remove(REPORT_PATH);
}
