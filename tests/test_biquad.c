#include <math.h>
#include "test.h"
#include "synth/biquad.h"
#include "synth/params.h"

#define TEST_PI 3.14159265358979

/* |H| at hz measured from biquad_step's impulse response: checks the difference equation against
   the coefficients. */
static double measured_db(biquad *f, double hz)
{
    enum { N = 4096 };
    double re = 0.0, im = 0.0;
    biquad_reset(f);
    for (int n = 0; n < N; n++) {
        double y = biquad_step(f, n == 0 ? 1.0f : 0.0f);
        re += y * cos(2.0 * TEST_PI * hz * n / SYNTH_RATE);
        im -= y * sin(2.0 * TEST_PI * hz * n / SYNTH_RATE);
    }
    return 10.0 * log10(re * re + im * im);
}

/* The voiced-source low-end peak the user chose: 110 Hz, Q 0.6, +10 dB. */
static void test_peaking(void)
{
    biquad f;
    biquad_peaking(&f, 110.0f, 0.6f, 10.0f, (float)SYNTH_RATE);
    CHECK(fabs(biquad_gain_db(&f, 110.0, SYNTH_RATE) - 10.0) < 0.05);
    CHECK(fabs(biquad_gain_db(&f, 0.0, SYNTH_RATE)) < 0.05);       /* unity at DC ... */
    CHECK(fabs(biquad_gain_db(&f, 5000.0, SYNTH_RATE)) < 0.05);    /* ... and at Nyquist */
    CHECK(fabs(biquad_gain_db(&f, 1000.0, SYNTH_RATE) - 0.38) < 0.1);
    CHECK(fabs(biquad_gain_db(&f, 2000.0, SYNTH_RATE)) < 0.2);
    CHECK(fabs(measured_db(&f, 110.0) - biquad_gain_db(&f, 110.0, SYNTH_RATE)) < 0.05);
}

/* Two RBJ low-pass sections with the Butterworth Q pair form a 4th-order Butterworth low-pass. */
static void test_butterworth_lowpass(void)
{
    biquad a, b;
    biquad_lowpass(&a, 3800.0f, 0.5411961f, (float)SYNTH_RATE);
    biquad_lowpass(&b, 3800.0f, 1.3065630f, (float)SYNTH_RATE);
#define TOTAL(hz) (biquad_gain_db(&a, (hz), SYNTH_RATE) + biquad_gain_db(&b, (hz), SYNTH_RATE))
    CHECK(fabs(TOTAL(3800.0) + 3.01) < 0.1);   /* -3 dB at the corner */
    CHECK(TOTAL(1000.0) > -0.1);
    CHECK(TOTAL(3000.0) > -0.2);
    CHECK(TOTAL(4500.0) < -25.0);
    CHECK(fabs(TOTAL(0.0)) < 0.01);
#undef TOTAL
    CHECK(fabs(measured_db(&b, 3000.0) - biquad_gain_db(&b, 3000.0, SYNTH_RATE)) < 0.05);
}

static void test_reset(void)
{
    biquad f;
    biquad_lowpass(&f, 1000.0f, 0.7071f, (float)SYNTH_RATE);
    CHECK(f.z1 == 0.0f && f.z2 == 0.0f);
    biquad_step(&f, 1.0f);
    CHECK(f.z1 != 0.0f);
    biquad_reset(&f);
    CHECK(f.z1 == 0.0f && f.z2 == 0.0f);
}

void suite_biquad(void)
{
    test_peaking();
    test_butterworth_lowpass();
    test_reset();
}
