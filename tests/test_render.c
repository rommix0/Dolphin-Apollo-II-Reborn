#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "render/render.h"

static machine g_m;

static int peak_of(const render_result *r)
{
    int peak = 0;
    for (size_t i = 0; i < r->count; i++)
        if (abs(r->samples[i]) > peak)
            peak = abs(r->samples[i]);
    return peak;
}

void suite_render(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    render_options opt;
    render_result a = { 0 }, b = { 0 }, voice_b = { 0 }, raw = { 0 }, r3 = { 0 }, plain = { 0 };
    render_default_options(&opt);

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

    render_result_free(&a);
    render_result_free(&b);
    render_result_free(&voice_b);
    render_result_free(&raw);
    render_result_free(&r3);
    render_result_free(&plain);
    CHECK(a.samples == NULL && a.count == 0);
}
