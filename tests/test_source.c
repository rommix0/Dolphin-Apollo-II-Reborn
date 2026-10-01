#include <math.h>
#include "test.h"
#include "synth/params.h"
#include "synth/source.h"

static source g_src;

static double mean_square(const source_params *p, int n, double *mean)
{
    double sum = 0.0, sq = 0.0;
    source_init(&g_src);
    for (int i = 0; i < n; i++) {
        double x = source_step(&g_src, p, (float)SYNTH_RATE);
        sum += x;
        sq += x * x;
    }
    if (mean)
        *mean = sum / n;
    return sq / n;
}

static void test_deterministic(void)
{
    static const source_params p = { 125.0f, 0.5f, 0.0f };
    source a, b;
    bool same = true;
    source_init(&a);
    source_init(&b);
    for (int i = 0; i < 1000; i++)
        if (source_step(&a, &p, (float)SYNTH_RATE) != source_step(&b, &p, (float)SYNTH_RATE))
            same = false;
    CHECK(same);
}

static void test_noise(void)
{
    static const source_params p = { 100.0f, 0.0f, 0.0f };
    double mean;
    double ms = mean_square(&p, 100000, &mean);
    CHECK(ms > 0.95 && ms < 1.05);
    CHECK(fabs(mean) < 0.02);
}

static void test_impulse_train(void)
{
    static const source_params p = { 100.0f, 1.0f, 0.0f };
    int pulses = 0;
    bool heights_ok = true;
    source_init(&g_src);
    for (int i = 0; i < 10000; i++) {
        float x = source_step(&g_src, &p, (float)SYNTH_RATE);
        if (x != 0.0f) {
            pulses++;
            if (fabsf(x - 10.0f) > 1e-4f)        /* sqrt(period in samples) */
                heights_ok = false;
        }
    }
    CHECK(pulses >= 99 && pulses <= 101);
    CHECK(heights_ok);
    double ms = mean_square(&p, 10000, NULL);
    CHECK(ms > 0.98 && ms < 1.02);
}

static void test_rosenberg(void)
{
    static const source_params p = { 125.0f, 1.0f, 0.3125f };
    double mean;
    double ms = mean_square(&p, 10000, &mean);  /* exactly 125 pitch periods */
    CHECK(ms > 0.85 && ms < 1.15);
    CHECK(fabs(mean * 10000.0) < 0.5);           /* flow derivative sums to ~0 over whole periods */
}

/* Mid-range voicing (voiced fricatives): noise is louder in the first half of the pitch period. */
static void test_pitch_modulated_noise(void)
{
    source_params p = { 100.0f, 0.5f, 0.0f };
    double first = 0.0, second = 0.0;
    source_init(&g_src);
    for (int i = 0; i < 100000; i++) {
        float x = source_step(&g_src, &p, (float)SYNTH_RATE);
        int pos = i % 100;
        if (pos >= 1 && pos < 50)
            first += x * x;
        else if (pos >= 50)
            second += x * x;
    }
    CHECK(first / second > 8.0);
    p.voicing = 0.1f;                            /* mostly noise: no modulation */
    first = second = 0.0;
    source_init(&g_src);
    for (int i = 0; i < 100000; i++) {
        float x = source_step(&g_src, &p, (float)SYNTH_RATE);
        int pos = i % 100;
        if (pos >= 1 && pos < 50)
            first += x * x;
        else if (pos >= 51)
            second += x * x;
    }
    CHECK(first / second > 0.8 && first / second < 1.25);
}

void suite_source(void)
{
    test_deterministic();
    test_noise();
    test_impulse_train();
    test_rosenberg();
    test_pitch_modulated_noise();
}
