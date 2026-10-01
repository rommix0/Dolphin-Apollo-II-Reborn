#include <math.h>
#include "test.h"
#include "synth/params.h"
#include "synth/resonator.h"

#define TEST_PI 3.14159265358979

enum { N = 2048 };

/* |H(f)| measured from the impulse response by a direct DFT. */
static double response(const float *h, double f)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < N; i++) {
        re += h[i] * cos(2.0 * TEST_PI * f * i / SYNTH_RATE);
        im -= h[i] * sin(2.0 * TEST_PI * f * i / SYNTH_RATE);
    }
    return sqrt(re * re + im * im);
}

/* Spec §7.2: the impulse response peaks at the target within 2 %, with the expected bandwidth. */
static void check(float f_hz, float bw_hz)
{
    static float h[N];
    resonator r;
    resonator_reset(&r);
    resonator_set(&r, f_hz, bw_hz, (float)SYNTH_RATE);
    for (int i = 0; i < N; i++)
        h[i] = resonator_step(&r, i == 0 ? 1.0f : 0.0f);

    double best_f = 0.0, best = 0.0;
    for (double f = 0.8 * f_hz; f <= 1.2 * f_hz && f < SYNTH_RATE / 2.0; f += 1.0) {
        double m = response(h, f);
        if (m > best) {
            best = m;
            best_f = f;
        }
    }
    double lo = best_f, hi = best_f;
    while (lo > 1.0 && response(h, lo) > best / sqrt(2.0))
        lo -= 1.0;
    while (hi < SYNTH_RATE / 2.0 && response(h, hi) > best / sqrt(2.0))
        hi += 1.0;
    double gain_db = 20.0 * log10(best);
    bool ok = fabs(best_f - f_hz) <= 0.02 * f_hz && fabs(gain_db) <= 0.3 && fabs((hi - lo) - bw_hz) <= 0.15 * bw_hz;
    if (!ok)
        fprintf(stderr, "  resonator %.0f/%.0f Hz: peak %.0f Hz at %.2f dB, -3 dB width %.0f Hz\n",
                f_hz, bw_hz, best_f, gain_db, hi - lo);
    CHECK(ok);
}

static void test_state_kept_across_set(void)
{
    resonator r;
    resonator_reset(&r);
    resonator_set(&r, 500.0f, 80.0f, (float)SYNTH_RATE);
    resonator_step(&r, 1.0f);
    float y1 = r.y1;
    resonator_set(&r, 700.0f, 80.0f, (float)SYNTH_RATE);   /* frequencies change once per frame */
    CHECK(r.y1 == y1);
    resonator_reset(&r);
    CHECK(r.y1 == 0.0f && r.y2 == 0.0f);
}

void suite_resonator(void)
{
    check(256.0f, 80.0f);     /* FN */
    check(310.0f, 70.0f);     /* F1 */
    check(1500.0f, 90.0f);    /* F2 */
    check(2500.0f, 140.0f);   /* F3 */
    check(3500.0f, 300.0f);   /* fixed fourth formant */
    test_state_kept_across_set();
}
