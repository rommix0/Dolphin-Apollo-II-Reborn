#include <math.h>
#include "test.h"
#include "rom_fixture.h"
#include "synth/calib.h"
#include "synth/params.h"

/* Steady voice-A frames of _AR (hard) and _EE (heed), recorded from the ROM (plan "Facts"). */
static const char k_ar[] = "3220321935092A222A3F231417000000824AFFFF";
static const char k_ee[] = "3020290828282E28323F231417000000824AFFFF";

static void map(const char *hex, const synth_options *opt, synth_params *p, uint8_t f[SYNTH_FRAME_BYTES])
{
    CHECK(test_parse_hex(hex, f, SYNTH_FRAME_BYTES));
    params_from_frame(f, opt, p);
}

/* The calibrated maps stay near Wells (1962) RP male means: hard 677/1083/2540, heed 285/2373/3088 Hz. */
static void test_frequencies(void)
{
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    map(k_ar, &off, &p, f);
    CHECK(p.f1_hz > 600.0f && p.f1_hz < 760.0f);
    CHECK(p.f2_hz > 950.0f && p.f2_hz < 1220.0f);
    CHECK(p.f3_hz > 2300.0f && p.f3_hz < 2800.0f);
    CHECK(p.fn_hz > 200.0f && p.fn_hz < 320.0f);   /* H-FN: a low resonance near 256 Hz */
    map(k_ee, &off, &p, f);
    CHECK(p.f1_hz > 250.0f && p.f1_hz < 350.0f);
    CHECK(p.f2_hz > 2100.0f && p.f2_hz < 2650.0f);
    CHECK(p.f3_hz > 2600.0f && p.f3_hz < 3300.0f);
}

static void test_amplitudes(void)
{
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    /* C4127 suppression: constant float comparison is intentional for calibration bounds checking */
#pragma warning(push)
#pragma warning(disable: 4127)
    CHECK(CAL_DB_PER_CODE > 0.8f && CAL_DB_PER_CODE < 1.1f);   /* spec §5: about 1 dB per code */
#pragma warning(pop)
    map(k_ar, &off, &p, f);
    f[2] = 30;
    params_from_frame(f, &off, &p);
    float a30 = p.a1;
    f[2] = 40;
    params_from_frame(f, &off, &p);
    float want = powf(10.0f, 10.0f * CAL_DB_PER_CODE / 20.0f);
    CHECK(fabsf(p.a1 / a30 - want) < 1e-3f * want);
    f[2] = (uint8_t)CAL_REF_CODE;
    params_from_frame(f, &off, &p);
    CHECK(fabsf(p.a1 - powf(10.0f, CAL_G_A1_DB / 20.0f)) < 1e-4f);
    f[2] = 1;                                   /* the firmware uses 1 for silence */
    params_from_frame(f, &off, &p);
    CHECK(p.a1 == 0.0f);
}

static void test_voicing_and_f0(void)
{
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    map(k_ar, &off, &p, f);
    CHECK(p.voicing == 1.0f);                   /* V = 63 */
    f[9] = 1;
    params_from_frame(f, &off, &p);
    CHECK(p.voicing == 0.0f);
    f[9] = 32;
    params_from_frame(f, &off, &p);
    CHECK(fabsf(p.voicing - 0.5f) < 1e-6f);
    f[10] = 32;                                 /* the median F0 code of the plain corpus */
    params_from_frame(f, &off, &p);
    float f32 = p.f0_hz;
    CHECK(f32 > 80.0f && f32 < 180.0f);
    f[10] = 33;
    params_from_frame(f, &off, &p);
    CHECK(p.f0_hz > f32);
    f[10] = 0;
    params_from_frame(f, &off, &p);
    CHECK(p.f0_hz >= CAL_F0_MIN_HZ);
}

static void test_hypothesis_switches(void)
{
    static const synth_options off = { false, false };
    static const synth_options b11 = { true, false };
    static const synth_options b13 = { false, true };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    map(k_ar, &off, &p, f);
    CHECK(p.open_quotient == 0.0f);
    CHECK(!p.reset);
    params_from_frame(f, &b11, &p);
    CHECK(fabsf(p.open_quotient - 20.0f / 64.0f) < 1e-6f);   /* b11 = $14, the default @M setting */
    f[11] = 0x3F;
    params_from_frame(f, &b11, &p);
    CHECK(fabsf(p.open_quotient - 0.9f) < 1e-6f);            /* clamped */
    f[13] = 0x90;                                            /* bit 7: spectral jump */
    params_from_frame(f, &b13, &p);
    CHECK(p.reset);
    params_from_frame(f, &off, &p);
    CHECK(!p.reset);
    f[13] = 0x40;
    params_from_frame(f, &b13, &p);
    CHECK(!p.reset);
}

void suite_params(void)
{
    test_frequencies();
    test_amplitudes();
    test_voicing_and_f0();
    test_hypothesis_switches();
}
