#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "render/render.h"
#include "render/render_native.h"

static machine g_m;
static const char *const k_corpus[] = { CORPUS_NAMES };

static int peak_of(const render_result *r)
{
    int peak = 0;
    for (size_t i = 0; i < r->count; i++)
        if (abs(r->samples[i]) > peak)
            peak = abs(r->samples[i]);
    return peak;
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* text through render_text with the hybrid engine and through render_text_native (M5a spec 4.5): the same status,
   samples and clipping. Returns the samples compared. */
static size_t same_on_native(const uint8_t *rom, size_t len, const char *name, const char *text,
                             const render_options *opt)
{
    render_options hopt = *opt;
    render_result h = { 0 }, n = { 0 };
    hopt.engine = RENDER_ENGINE_HYBRID;
    machine_status hs = render_text(&g_m, rom, len, text, &hopt, &h);
    engine_status ns = render_text_native(text, opt, &n);
    bool same = (int)hs == (int)ns && h.count == n.count && h.frames == n.frames && h.clipped == n.clipped &&
                (h.count == 0 || !memcmp(h.samples, n.samples, h.count * sizeof h.samples[0]));
    if (!same) {
        fprintf(stderr, "  render %s: hybrid %d, %zu samples; native %d, %zu samples (or they differ)\n", name, (int)hs,
                h.count, (int)ns, n.count);
        g_test_failures++;
    }
    size_t count = h.count;
    render_result_free(&h);
    render_result_free(&n);
    return count;
}

/* Every corpus text, the raw stream, and the M5a plan's Review Focus 5: the second voice with the prosody extremes
   @R0 and @R7, and a mute (Ctrl-X) in the middle of a text. */
static void test_native(const uint8_t *rom, size_t len)
{
    render_options opt;
    size_t texts = 0, samples = 0;
    render_default_options(&opt);
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        samples += same_on_native(rom, len, k_corpus[c], text, &opt);
        texts++;
        free(text);
    }
    printf("  native: %zu corpus texts, %zu samples the same as the hybrid's\n", texts, samples);
    opt.stream = RENDER_STREAM_RAW;
    CHECK(same_on_native(rom, len, "raw", "Hello world.", &opt) > 0);
    render_default_options(&opt);
    opt.voice = 'b';
    for (int r = 0; r <= 7; r += 7) {
        opt.prosody = r;
        CHECK(same_on_native(rom, len, r ? "b @R7" : "b @R0", "Is this the second voice? It is.", &opt) > 0);
    }
    render_default_options(&opt);
    CHECK(same_on_native(rom, len, "mute", "One two three four \030 five six seven.", &opt) > 0);
}

void suite_render(void)
{
    render_options opt;
    render_result nat = { 0 };
    render_default_options(&opt);
    CHECK_EQ(render_text_native("Hello world.", &opt, &nat), ENGINE_IDLE);   /* no ROM needed */
    CHECK_EQ(nat.frames, 105);                               /* tests/golden/hello.frm */
    CHECK_EQ(nat.count, 105 * SYNTH_FRAME_SAMPLES);
    CHECK_EQ(nat.clipped, 0);
    CHECK(peak_of(&nat) > 1000);
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom) {
        render_result_free(&nat);
        SKIP("ROM not found (set APOLLO2_ROM)");
    }
    render_result a = { 0 }, b = { 0 }, voice_b = { 0 }, raw = { 0 }, r3 = { 0 }, plain = { 0 };

    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &a), MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK_EQ(a.frames, 105);                                 /* tests/golden/hello.frm */
    CHECK_EQ(a.count, 105 * SYNTH_FRAME_SAMPLES);
    CHECK_EQ(a.clipped, 0);
    CHECK(peak_of(&a) > 1000);                               /* audible speech, not silence */

    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &b), MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(a.count == b.count && memcmp(a.samples, b.samples, a.count * sizeof a.samples[0]) == 0);

    opt.voice = 'b';
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &voice_b), MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(voice_b.count != a.count || memcmp(a.samples, voice_b.samples, a.count * sizeof a.samples[0]) != 0);

    opt.voice = 'a';
    opt.stream = RENDER_STREAM_RAW;
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &raw), MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(raw.frames > 0);
    CHECK_EQ(raw.count, raw.frames * SYNTH_FRAME_SAMPLES);

    render_default_options(&opt);
    CHECK_EQ(opt.prosody, 4);                                /* the generator's unscaled contour */
    CHECK(opt.synth.peak_filter && opt.synth.lowpass);
    opt.prosody = 3;                                         /* the device default @R3 */
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &r3), MACHINE_IDLE);
    CHECK_EQ(r3.frames, 105);                                /* prosody changes pitch, not timing */
    CHECK(memcmp(a.samples, r3.samples, a.count * sizeof a.samples[0]) != 0);

    render_default_options(&opt);
    opt.synth.peak_filter = false;
    opt.synth.lowpass = false;
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &plain), MACHINE_IDLE);
    CHECK_EQ(plain.frames, 105);
    CHECK(memcmp(a.samples, plain.samples, a.count * sizeof a.samples[0]) != 0);
    CHECK_EQ(plain.clipped, 0);

    /* render_text applies the stream's pitch centre: rebuild the pipeline by hand with and without it */
    {
        frame_list fl = { 0 };
        CHECK_EQ(rom_engine_speak_streams(&g_m, rom, len, NULL, "@R4 Hello world.", &fl, NULL), MACHINE_IDLE);
        CHECK_EQ(fl.count, 105);
        size_t n = fl.count * SYNTH_FRAME_SAMPLES;
        int16_t *with_c = malloc(n * sizeof *with_c), *no_c = malloc(n * sizeof *no_c);
        CHECK(with_c && no_c);
        if (with_c && no_c) {
            for (int pass = 0; pass < 2; pass++) {
                synth s;
                synth_options so;
                synth_default_options(&so);
                so.f0_centre_hz = pass == 0 ? params_f0_centre(&fl.frames[0][0], fl.count) : 0.0f;
                synth_init(&s, &so);
                for (size_t i = 0; i < fl.count; i++)
                    synth_frame(&s, fl.frames[i], (pass == 0 ? with_c : no_c) + i * SYNTH_FRAME_SAMPLES);
            }
            CHECK_EQ(a.count, n);
            CHECK(memcmp(a.samples, with_c, n * sizeof *with_c) == 0);
            CHECK(memcmp(a.samples, no_c, n * sizeof *no_c) != 0);
        }
        free(with_c);
        free(no_c);
        frame_list_free(&fl);
    }

    CHECK(a.count == nat.count && !memcmp(a.samples, nat.samples, a.count * sizeof a.samples[0]));
    test_native(rom, len);
    render_result_free(&nat);
    render_result_free(&a);
    render_result_free(&b);
    render_result_free(&voice_b);
    render_result_free(&raw);
    render_result_free(&r3);
    render_result_free(&plain);
    CHECK(a.samples == NULL && a.count == 0);
}
