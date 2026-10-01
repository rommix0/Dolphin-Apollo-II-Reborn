#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "synth/synth.h"
#include "synth/calib.h"

#define TEST_PI 3.14159265358979

static synth g_s;
static const char k_silence[] = "012001200117011F013F201417201924824AFFFF";
static const char k_ar[] = "3220321935092A222A3F231417000000824AFFFF";
static const char k_ee[] = "3020290828282E28323F231417000000824AFFFF";
static const char k_glide_onset[] = "30202C072B0518181C2E231417900000824AFFFF";   /* W of "We" after silence, b13 $90 */
static const char k_glide[]       = "30202C072B0518181C2E231417000000824AFFFF";   /* the same glide, steady */
static const char k_closure[]     = "1420140101050118013E231417C00000824AFFFF";   /* voice bar of a G closure, b13 $C0 */

static int peak_between(const int16_t *x, int from, int to)
{
    int peak = 0;
    for (int i = from; i < to; i++)
        if (abs(x[i]) > peak)
            peak = abs(x[i]);
    return peak;
}

static void frame(const char *hex, uint8_t f[SYNTH_FRAME_BYTES])
{
    CHECK(test_parse_hex(hex, f, SYNTH_FRAME_BYTES));
}

static void test_silence_is_zero(void)
{
    uint8_t f[SYNTH_FRAME_BYTES];
    int16_t out[SYNTH_FRAME_SAMPLES];
    bool zero = true;
    frame(k_silence, f);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 5; i++) {
        synth_frame(&g_s, f, out);
        for (int n = 0; n < SYNTH_FRAME_SAMPLES; n++)
            if (out[n] != 0)
                zero = false;
    }
    CHECK(zero);
}

static void test_deterministic(void)
{
    static int16_t a[20 * SYNTH_FRAME_SAMPLES], b[20 * SYNTH_FRAME_SAMPLES];
    uint8_t f[SYNTH_FRAME_BYTES];
    frame(k_ar, f);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 20; i++)
        synth_frame(&g_s, f, a + i * SYNTH_FRAME_SAMPLES);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 20; i++)
        synth_frame(&g_s, f, b + i * SYNTH_FRAME_SAMPLES);
    CHECK(memcmp(a, b, sizeof a) == 0);
}

/* Spec §7.2: a steady vowel frame produces spectral peaks at the calibrated F1-F3 within 5 %.
   Noise excitation (V = 1) and 77 averaged 256-sample segments give a smooth spectrum. FN and the
   fixed fourth formant are switched off. A neighbouring loud formant can tilt the spectrum across
   the search window, so each formant's peak is the local maximum nearest its target, not the
   window maximum. */
static void check_peaks(const char *hex)
{
    enum { FRAMES = 200, SEG = 256, SEGS = FRAMES * SYNTH_FRAME_SAMPLES / SEG, MAXSTEPS = 400 };
    static int16_t pcm[FRAMES * SYNTH_FRAME_SAMPLES];
    static double pw[MAXSTEPS + 1];
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    float target[3];
    frame(hex, f);
    f[0] = 1;   /* ALF off */
    f[8] = 1;   /* AHF off */
    f[9] = 1;   /* V = 1: noise only */
    synth_init(&g_s, NULL);
    for (int i = 0; i < FRAMES; i++)
        synth_frame(&g_s, f, pcm + i * SYNTH_FRAME_SAMPLES);
    params_from_frame(f, &off, &p);
    target[0] = p.f1_hz;
    target[1] = p.f2_hz;
    target[2] = p.f3_hz;
    for (int k = 0; k < 3; k++) {
        float lo = 0.85f * target[k];
        int steps = (int)(0.30f * target[k] / 5.0f);
        if (steps > MAXSTEPS)
            steps = MAXSTEPS;
        for (int s = 0; s <= steps; s++) {
            float fr = lo + 5.0f * (float)s;
            pw[s] = 0.0;
            for (int seg = 1; seg < SEGS; seg++) {   /* skip the onset segment */
                double re = 0.0, im = 0.0;
                for (int n = 0; n < SEG; n++) {
                    double w = 0.5 - 0.5 * cos(2.0 * TEST_PI * n / (SEG - 1));
                    double x = w * pcm[seg * SEG + n];
                    double ph = 2.0 * TEST_PI * fr * n / SYNTH_RATE;
                    re += x * cos(ph);
                    im -= x * sin(ph);
                }
                pw[s] += re * re + im * im;
            }
        }
        float best_f = -1.0f;
        for (int s = 1; s < steps; s++) {
            float fr = lo + 5.0f * (float)s;
            if (pw[s] > pw[s - 1] && pw[s] >= pw[s + 1] &&
                (best_f < 0.0f || fabsf(fr - target[k]) < fabsf(best_f - target[k])))
                best_f = fr;
        }
        bool ok = best_f > 0.0f && fabsf(best_f - target[k]) <= 0.05f * target[k];
        if (!ok)
            fprintf(stderr, "  F%d: nearest spectral peak %.0f Hz, calibrated %.0f Hz\n", k + 1, best_f, target[k]);
        CHECK(ok);
    }
}

static void test_b11_switch_changes_voicing(void)
{
    static const synth_options b11 = { true, false };
    static int16_t a[10 * SYNTH_FRAME_SAMPLES], b[10 * SYNTH_FRAME_SAMPLES];
    uint8_t f[SYNTH_FRAME_BYTES];
    frame(k_ar, f);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 10; i++)
        synth_frame(&g_s, f, a + i * SYNTH_FRAME_SAMPLES);
    synth_init(&g_s, &b11);
    for (int i = 0; i < 10; i++)
        synth_frame(&g_s, f, b + i * SYNTH_FRAME_SAMPLES);
    CHECK(memcmp(a, b, sizeof a) != 0);
}

/* Every golden stream synthesizes without clipping. Returns the loudest peak. This uses only
   committed goldens, so no ROM is needed. */
static int corpus_peak(const synth_options *opt, const char *label)
{
    static const char *const names[] = { CORPUS_NAMES, "boot" };
    int peak = 0;
    for (size_t c = 0; c < sizeof names / sizeof names[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/golden/%s.frm", names[c]);
        char *hex = test_read_file(path);
        CHECK(hex != NULL);
        if (!hex)
            continue;
        size_t frames = strlen(hex) / (2 * SYNTH_FRAME_BYTES + 1);
        synth_init(&g_s, opt);
        for (size_t i = 0; i < frames; i++) {
            uint8_t f[SYNTH_FRAME_BYTES];
            int16_t out[SYNTH_FRAME_SAMPLES];
            frame(hex + i * (2 * SYNTH_FRAME_BYTES + 1), f);
            synth_frame(&g_s, f, out);
            for (int n = 0; n < SYNTH_FRAME_SAMPLES; n++)
                if (abs(out[n]) > peak)
                    peak = abs(out[n]);
        }
        if (g_s.clipped)
            fprintf(stderr, "  %s %s: %llu samples clipped\n", label, names[c], (unsigned long long)g_s.clipped);
        CHECK_EQ(g_s.clipped, 0);
        free(hex);
    }
    printf("  corpus peak %s %d (%.1f dBFS)\n", label, peak, 20.0 * log10(peak / 32767.0));
    return peak;
}

/* The plain corpus peaks between -12 and -1 dBFS; the b11 hypothesis is level-matched to it. */
static void test_corpus_level(void)
{
    static const synth_options b11 = { true, false };
    int peak = corpus_peak(NULL, "plain");
    CHECK(peak >= 8231);    /* -12 dBFS */
    CHECK(peak <= 29204);   /* -1 dBFS */
    corpus_peak(&b11, "b11");   /* no clipping is asserted inside */
}

/* b13 bit 7 marks a transition: the amplitudes reach their targets within CAL_STEP_RAMP_SAMPLES. */
static void test_step_ramp(void)
{
    uint8_t f[SYNTH_FRAME_BYTES];
    int16_t out[SYNTH_FRAME_SAMPLES];
    synth_init(&g_s, NULL);
    frame(k_ar, f);
    for (int i = 0; i < 5; i++)
        synth_frame(&g_s, f, out);
    frame(k_silence, f);
    f[13] = 0x80;
    synth_frame(&g_s, f, out);
    CHECK(peak_between(out, 0, CAL_STEP_RAMP_SAMPLES - 1) > 0);
    CHECK_EQ(peak_between(out, CAL_STEP_RAMP_SAMPLES - 1, SYNTH_FRAME_SAMPLES), 0);
}

/* A stop closure settles at once: no low-frequency thump from the vowel's energy (measured 0.34
   with the full-frame ramp, 0.02 with the fast ramp). */
static void test_closure_settles(void)
{
    uint8_t f[SYNTH_FRAME_BYTES];
    int16_t out[SYNTH_FRAME_SAMPLES];
    int vowel = 0;
    synth_init(&g_s, NULL);
    frame(k_ar, f);
    for (int i = 0; i < 10; i++) {
        synth_frame(&g_s, f, out);
        vowel = peak_between(out, 0, SYNTH_FRAME_SAMPLES);
    }
    frame(k_closure, f);
    synth_frame(&g_s, f, out);
    int ring = peak_between(out, 20, SYNTH_FRAME_SAMPLES);
    if (ring * 10 > vowel)
        fprintf(stderr, "  closure ring %d vs vowel peak %d\n", ring, vowel);
    CHECK(ring * 10 <= vowel);
}

/* A glide after silence starts no louder than its steady state: a silent channel holds no energy
   (measured 2.56x without mute-reset at the fast ramp, 0.93x with it). */
static void test_glide_onset(void)
{
    uint8_t f[SYNTH_FRAME_BYTES];
    int16_t out[SYNTH_FRAME_SAMPLES];
    synth_init(&g_s, NULL);
    frame(k_silence, f);
    for (int i = 0; i < 5; i++)
        synth_frame(&g_s, f, out);
    frame(k_glide_onset, f);
    synth_frame(&g_s, f, out);
    int onset = peak_between(out, 0, SYNTH_FRAME_SAMPLES), steady = 0;
    frame(k_glide, f);
    for (int i = 0; i < 3; i++) {
        synth_frame(&g_s, f, out);
        steady = peak_between(out, 0, SYNTH_FRAME_SAMPLES);
    }
    if (onset * 10 > steady * 13)
        fprintf(stderr, "  glide onset peak %d vs steady %d\n", onset, steady);
    CHECK(onset * 10 <= steady * 13);
}

void suite_synth(void)
{
    test_silence_is_zero();
    test_deterministic();
    check_peaks(k_ar);
    check_peaks(k_ee);
    test_b11_switch_changes_voicing();
    test_step_ramp();
    test_closure_settles();
    test_glide_onset();
    test_corpus_level();
}
