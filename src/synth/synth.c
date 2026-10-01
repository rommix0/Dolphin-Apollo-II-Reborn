#include <math.h>
#include <string.h>
#include "synth/calib.h"
#include "synth/synth.h"

static const float k_sign[5] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };   /* spec §5: alternating signs */

void synth_init(synth *s, const synth_options *opt)
{
    memset(s, 0, sizeof *s);
    if (opt)
        s->opt = *opt;
    source_init(&s->src);
}

void synth_frame(synth *s, const uint8_t frame[SYNTH_FRAME_BYTES], int16_t out[SYNTH_FRAME_SAMPLES])
{
    synth_params p;
    source_params sp;
    float freq[5], bw[5], target[5];
    params_from_frame(frame, &s->opt, &p);
    freq[0] = p.fn_hz;
    freq[1] = p.f1_hz;
    freq[2] = p.f2_hz;
    freq[3] = p.f3_hz;
    freq[4] = CAL_F4_HZ;
    bw[0] = CAL_BN_HZ;
    bw[1] = CAL_B1_HZ;
    bw[2] = CAL_B2_HZ;
    bw[3] = CAL_B3_HZ;
    bw[4] = CAL_B4_HZ;
    target[0] = p.alf;
    target[1] = p.a1;
    target[2] = p.a2;
    target[3] = p.a3;
    target[4] = p.ahf;
    for (int k = 0; k < 5; k++) {
        if (p.reset)
            resonator_reset(&s->res[k]);
        resonator_set(&s->res[k], freq[k], bw[k], (float)SYNTH_RATE);
    }
    sp.f0_hz = p.f0_hz;
    sp.voicing = p.voicing;
    sp.open_quotient = p.open_quotient;
    for (int n = 0; n < SYNTH_FRAME_SAMPLES; n++) {
        float x = source_step(&s->src, &sp, (float)SYNTH_RATE);
        float t = (float)(n + 1) / (float)SYNTH_FRAME_SAMPLES;   /* the ramp reaches the target at the frame end */
        float y = 0.0f;
        for (int k = 0; k < 5; k++) {
            float g = s->gain[k] + (target[k] - s->gain[k]) * t;
            y += k_sign[k] * g * resonator_step(&s->res[k], x);
        }
        y *= CAL_MASTER_GAIN * 32767.0f;
        if (y > 32767.0f) {
            y = 32767.0f;
            s->clipped++;
        } else if (y < -32768.0f) {
            y = -32768.0f;
            s->clipped++;
        }
        out[n] = (int16_t)lrintf(y);
    }
    memcpy(s->gain, target, sizeof s->gain);
}
