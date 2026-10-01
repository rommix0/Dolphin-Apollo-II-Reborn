#include <math.h>
#include "synth/calib.h"
#include "synth/params.h"

static float map_hz(int log_map, float a, float b, int code)
{
    return log_map ? a * expf(b * (float)code) : a + b * (float)code;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Keeps every resonator below 0.45 * fs. */
static float freq(int log_map, float a, float b, int code)
{
    return clampf(map_hz(log_map, a, b, code), 50.0f, 0.45f * (float)SYNTH_RATE);
}

static float gain(int code, float offset_db)
{
    if (code <= 1)
        return 0.0f;   /* the firmware clamps amplitudes to 1..63 and uses 1 for silence */
    return powf(10.0f, (CAL_DB_PER_CODE * (float)(code - CAL_REF_CODE) + offset_db) / 20.0f);
}

void params_from_frame(const uint8_t f[SYNTH_FRAME_BYTES], const synth_options *opt, synth_params *p)
{
    p->fn_hz = freq(CAL_FN_LOG, CAL_FN_A, CAL_FN_B, f[1]);
    p->f1_hz = freq(CAL_F1_LOG, CAL_F1_A, CAL_F1_B, f[3]);
    p->f2_hz = freq(CAL_F2_LOG, CAL_F2_A, CAL_F2_B, f[5]);
    p->f3_hz = freq(CAL_F3_LOG, CAL_F3_A, CAL_F3_B, f[7]);
    p->alf = gain(f[0], CAL_G_ALF_DB);
    p->a1 = gain(f[2], CAL_G_A1_DB);
    p->a2 = gain(f[4], CAL_G_A2_DB);
    p->a3 = gain(f[6], CAL_G_A3_DB);
    p->ahf = gain(f[8], CAL_G_AHF_DB);
    p->voicing = clampf((float)(f[9] - 1) / 62.0f, 0.0f, 1.0f);
    p->f0_hz = clampf(expf(CAL_F0_LNA + CAL_F0_LNB * (float)f[10]), CAL_F0_MIN_HZ, CAL_F0_MAX_HZ);
    p->open_quotient = opt->b11_pulse_width ? clampf((float)f[11] / 64.0f, 0.1f, 0.9f) : 0.0f;
    p->reset = opt->b13_reset && (f[13] & 0x80) != 0;
}
