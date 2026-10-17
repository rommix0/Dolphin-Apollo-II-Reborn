#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "orpheus/orp_data.h"
#include "orpheus/orp_synth.h"
#include "orpheus/orp_x87.h"

/* Ported from a3s.dll in double, keeping each x87 expression's operand order and the points where it stores a
   double (spec 2026-10-16-orpheus-synth-analysis.md; the addresses are the routines'). Where the DLL truncates or
   compares an x87 result, the port computes it at 80 bits (orp_x87) once the x87 runs at 64-bit precision
   (s->x87_ext): a double there can land on the other side of an integer. _ftol truncates; the output's fistp rounds
   half to even (lrint). */

#define ORP_FS        10000
#define ORP_STEP_THR  (ORP_FS / 72)   /* the source table steps when its accumulator passes fs/72 */
#define CFG(i)        (orp_cfg[i].value)
#define CFGI(i)       (orp_cfg_int[i])
#define X(d)          x80_from_double(d)

static orp_filter filter_at(int k)
{
    const orp_f64 *f = orp_filters + k * ORP_F_FIELDS;
    orp_filter r = { f[ORP_FA].value, f[ORP_FB].value, f[ORP_FC].value, f[ORP_FE].value, f[ORP_FD].value,
                     f[ORP_FY1].value, f[ORP_FY2].value };
    return r;
}

void orp_init(orp_synth *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < ORP_PARAMS; i++) {
        s->cur[i] = orp_init_params[i];
        s->prev[i] = orp_init_params[ORP_PARAMS + i];
        s->thr[i] = orp_init_params[2 * ORP_PARAMS + i];
        s->jump[i] = orp_init_params[3 * ORP_PARAMS + i];
    }
    s->unvoiced = s->prev_unvoiced = true;   /* 2320c510 */
    s->lcg = CFGI(ORP_I_LCG_X);
    s->noise_f = filter_at(ORP_F_NOISE);
    s->final_f = filter_at(ORP_F_FINAL);
    s->casc = filter_at(ORP_F_CASC);
    for (int k = 0; k < 4; k++)
        s->ch[k] = filter_at(ORP_F_CH0 + k);
    for (int k = 0; k < 6; k++)
        s->fix[k] = filter_at(ORP_F_FIX0 + k);
    for (int k = 0; k < ORP_TRACKS; k++)
        s->smooth[k] = filter_at(ORP_F_SMOOTH0 + k);
}

uint64_t orp_clipped(const orp_synth *s)
{
    return s->clipped;
}

/* The resonator of the bank and the smoothers: y = a x + b y1 + c y2, out = d y1 + y, added to out when acc. */
static void run_two_pole(orp_filter *f, const double *x, double *out, int n, bool acc)
{
    double y1 = f->y1, y2 = f->y2;
    for (int i = 0; i < n; i++) {
        double y = f->a * x[i] + f->b * y1 + f->c * y2;
        double o = f->d * y1 + y;
        out[i] = acc ? o + out[i] : o;
        y2 = y1;
        y1 = y;
    }
    f->y1 = y1;
    f->y2 = y2;
}

/* run_two_pole at 80 bits over ORP_SUBBLOCKS copies of x: the x87 keeps y1 and y2 on its stack in between. */
static void run_two_pole_x87(orp_filter *f, double x, double *out)
{
    orp_x80 a = X(f->a), b = X(f->b), c = X(f->c), d = X(f->d), xv = X(x), y1 = X(f->y1), y2 = X(f->y2);
    for (int i = 0; i < ORP_SUBBLOCKS; i++) {
        orp_x80 y = x80_add(x80_add(x80_mul(xv, a), x80_mul(b, y1)), x80_mul(c, y2));
        out[i] = x80_to_double(x80_add(x80_mul(d, y1), y));
        y2 = y1;
        y1 = y;
    }
    f->y1 = x80_to_double(y1);
    f->y2 = x80_to_double(y2);
}

/* 23202940: start a smoother at its steady state for v. */
static void filter_settle(orp_filter *f, double v, bool ext)
{
    if (!ext) {
        double den = (f->d + f->e) - -1.0;
        f->y1 = f->y2 = den == 0.0 ? 0.0 : v / den;
        return;
    }
    orp_x80 den = x80_sub(x80_add(X(f->d), X(f->e)), X(-1.0));
    f->y1 = f->y2 = den.m == 0 ? 0.0 : x80_to_double(x80_div(X(v), den));
}

/* 2320ce70: Hz -> centi-Bark (Traunmüller, with both corrections), truncated. */
static int16_t centi_bark(int16_t hz, bool ext)
{
    if (ext) {
        orp_x80 f = x80_from_int(hz);
        orp_x80 z = x80_sub(x80_div(x80_mul(f, X(26.81)), x80_sub(f, X(-1960.0))), X(0.53));
        if (x80_cmp(z, X(2.0)) < 0)
            z = x80_sub(X(0.3), x80_mul(z, X(-0.85)));
        if (x80_cmp(z, X(20.1)) > 0)
            z = x80_sub(z, x80_mul(x80_sub(z, X(20.1)), X(-0.22)));
        return (int16_t)x80_trunc(x80_mul(z, X(100.0)));
    }
    double f = hz;
    double z = f * 26.81 / (f - -1960.0) - 0.53;
    if (z < 2.0)
        z = 0.3 - z * -0.85;
    if (z > 20.1)
        z = z - (z - 20.1) * -0.22;
    return (int16_t)(int32_t)(z * 100.0);
}

static void load(orp_synth *s, const int16_t p[ORP_PARAMS])
{
    for (int i = 0; i < ORP_PARAMS; i++) {
        s->prev[i] = s->cur[i];
        s->cur[i] = p[i];
    }
    for (int i = 0; i < 4; i++)
        s->cur[i] = centi_bark(s->cur[i], s->x87_ext);
}

static int16_t jumped(const orp_synth *s, int i, bool first)
{
    return first || abs(s->cur[i] - s->prev[i]) > s->thr[i];
}

/* 2320d080: jumps, the burst, frequency holds, the widening factor, smoothing, amplitude lookup. */
static void params_step(orp_synth *s, bool first)
{
    for (int i = 0; i < 12; i++)
        s->jump[i] = jumped(s, i, first);
    s->jump[13] = jumped(s, 13, first);
    s->jump[14] = jumped(s, 14, first);
    s->prev_unvoiced = s->unvoiced;
    s->unvoiced = s->cur[12] <= 1;
    s->burst = false;
    if (s->unvoiced) {
        int rising = 0;
        for (int i = 4; i < 12; i++)
            if (s->jump[i] && s->cur[i] > s->prev[i])
                rising++;
        s->burst = rising >= 2;
    }
    for (int j = 0; j < 4; j++) {   /* a channel whose amplitude falls away keeps its frequency */
        int a = s->cur[4 + j], pa = s->prev[4 + j];
        if (!s->jump[4 + j])
            continue;
        bool hold = a > 0 ? pa >= a + CFGI(ORP_I_HOLD_DROP) : a == 0 && pa >= CFGI(ORP_I_HOLD_SILENT);
        if (hold) {
            s->cur[j] = s->prev[j];
            s->jump[j] = 0;
        }
    }
    if (s->x87_ext) {
        orp_x80 t = x80_sub(x80_from_int(s->cur[4] - s->cur[5]), X(CFG(ORP_C_WIDEN_DB)));
        if (x80_cmp(t, X(0.0)) < 0)
            t = X(0.0);
        orp_x80 u = x80_sub(x80_from_int(s->cur[1]), X(CFG(ORP_C_WIDEN_F1)));
        if (x80_cmp(u, X(0.0)) < 0)
            u = X(0.0);
        orp_x80 w = x80_div(x80_mul(t, u), X(CFG(ORP_C_WIDEN_DIV)));
        s->widen = x80_cmp(w, X(1.0)) > 0 ? 1.0 : x80_to_double(w);
    } else {
        double t = (double)(s->cur[4] - s->cur[5]) - CFG(ORP_C_WIDEN_DB);
        if (t < 0.0)
            t = 0.0;
        double u = (double)s->cur[1] - CFG(ORP_C_WIDEN_F1);
        if (u < 0.0)
            u = 0.0;
        s->widen = t * u / CFG(ORP_C_WIDEN_DIV);
        if (s->widen > 1.0)
            s->widen = 1.0;
    }
    for (int k = 0; k < ORP_TRACKS; k++) {
        if (k == 12)
            continue;
        double v = s->cur[k];
        if (s->jump[k]) {
            for (int b = 0; b < ORP_SUBBLOCKS; b++)
                s->track[k][b] = v;
            filter_settle(&s->smooth[k], v, s->x87_ext);
        } else if (s->x87_ext) {
            run_two_pole_x87(&s->smooth[k], v, s->track[k]);
        } else {   /* the first asm block ever run, at 53 bits; its fninit sets 64 */
            double step[ORP_SUBBLOCKS];
            for (int b = 0; b < ORP_SUBBLOCKS; b++)
                step[b] = v;
            run_two_pole(&s->smooth[k], step, s->track[k], ORP_SUBBLOCKS, false);
            s->x87_ext = true;
        }
    }
    for (int k = 4; k < 12; k++)
        for (int b = 0; b < ORP_SUBBLOCKS; b++) {
            int16_t i = (int16_t)(s->x87_ext ? x80_trunc(x80_sub(X(s->track[k][b]), X(-0.5)))
                                             : (int32_t)(s->track[k][b] - -0.5));
            int idx = i <= 0 ? 0 : i >= ORP_AMP_N - 1 ? ORP_AMP_N - 1 : i;
            s->track[k][b] = orp_amp[idx].value;
        }
}

static void reset_source(orp_synth *s)
{
    s->phase = s->step_acc = s->step = s->step_quarter = 0;
    s->held = 0.0;
}

/* 2320d480: the pitch phase starts the source table each period; the table advances at p14 * 72 / fs. */
static void voiced_source(orp_synth *s)
{
    int src = s->cur[15] == 1 ? 1 : 0;
    const int16_t *table = orp_glottal + src * ORP_GLOTTAL_N;
    const int16_t *phase_map = orp_phase_map + src * ORP_PHASE_N;
    for (int b = 0; b < ORP_SUBBLOCKS; b++) {
        int32_t f0 = (int32_t)s->track[13][b];
        uint32_t rate = (uint32_t)(int32_t)s->track[14][b];
        if (rate > ORP_RATE_MAX)
            rate = ORP_RATE_MAX;
        if (rate < ORP_RATE_MIN)
            rate = ORP_RATE_MIN;
        double mean = orp_glottal_mean[src * ORP_MEAN_N + (rate - ORP_RATE_MIN) / 10].value;
        for (int n = 0; n < ORP_SUB_SAMPLES; n++) {
            s->step_acc += (int32_t)rate;
            s->phase += f0;
            if (s->phase > ORP_FS) {
                if (s->step >= ORP_GLOTTAL_N)
                    reset_source(s);
                else
                    s->phase = 0;
            }
            while (s->step_acc > ORP_STEP_THR && s->step < ORP_GLOTTAL_N) {
                s->step_acc -= ORP_STEP_THR;
                s->step++;
                s->held = (double)table[s->step - 1] - mean;
            }
            s->step_quarter = s->step >> 2;
            s->voiced[b * ORP_SUB_SAMPLES + n] = s->held;
            if (s->step >= ORP_GLOTTAL_N)
                s->held = 0.0;
        }
        s->phase_word[b] = s->step_quarter < ORP_PHASE_N ? phase_map[s->step_quarter] : 0;
    }
}

/* 2320d720: LCG noise (a burst first zeroes the frame's start and puts one impulse in it), then a one-pole. */
static void noise_source(orp_synth *s)
{
    int n = 0;
    if (s->burst) {
        for (; n < CFGI(ORP_I_BURST_LEN); n++)
            s->noise_raw[n] = 0.0;
        s->noise_raw[CFGI(ORP_I_BURST_POS)] = CFG(ORP_C_BURST_AMP);
        s->burst = false;
    }
    int half = (uint16_t)(CFGI(ORP_I_LCG_MOD) >> 1);
    for (; n < ORP_FRAME_SAMPLES; n++) {
        int32_t x = (int32_t)s->lcg * CFGI(ORP_I_LCG_MUL) + 1;
        s->lcg = (int16_t)(x % CFGI(ORP_I_LCG_MOD));
        s->noise_raw[n] = (double)(s->lcg - half) * CFG(ORP_C_NOISE_SCALE);
    }
    orp_filter *f = &s->noise_f;
    double y1 = f->y1;
    for (int i = 0; i < ORP_FRAME_SAMPLES; i++) {
        double y = f->a * s->noise_raw[i] + f->b * y1;
        s->noise[i] = f->d * y1 + y;
        y1 = y;
    }
    f->y1 = y1;
    s->x87_ext = true;   /* this asm block ends with fninit */
}

/* 2320d870: each channel's excitation, voiced * sqrt(v/31) + noise * sqrt(1 - v/31), times its amplitude. */
static void mix(orp_synth *s)
{
    for (int k = 0; k < ORP_CHANNELS; k++) {
        int16_t vi = (int16_t)(CFGI(ORP_I_VOFF0 + k) + s->cur[12]);
        if (vi < 0)
            vi = 0;
        if (vi > ORP_VMIX_N - 1)
            vi = ORP_VMIX_N - 1;
        double wv = orp_vmix[vi].value, wn = orp_vmix[ORP_VMIX_N - 1 - vi].value;
        int32_t bwmod = x80_trunc(x80_mul(X(CFG(ORP_C_BWMOD)), X(wn)));
        for (int b = 0; b < ORP_SUBBLOCKS; b++) {
            s->ch_word[k][b] = (int16_t)(x80_trunc(x80_mul(x80_from_int(s->phase_word[b]), X(wv))) + bwmod);
            double an = s->track[4 + k][b] * wn, av = s->track[4 + k][b] * wv;
            for (int n = b * ORP_SUB_SAMPLES; n < (b + 1) * ORP_SUB_SAMPLES; n++)
                s->exc[k][n] = s->voiced[n] * av + s->noise[n] * an;
        }
    }
}

/* One sub-block's coefficients of channel k (0-3) from the frequency and the pitch-synchronous bandwidth: b and c,
   and with gain nonzero a = (1 - b - c) * gain, from the x87's unrounded b. */
static void tune(const orp_synth *s, int k, int b, int16_t widen, double gain, orp_filter *f)
{
    int16_t fi = (int16_t)(int32_t)s->track[k][b];
    int16_t bi = (int16_t)(orp_bwtab[k * ORP_BWTAB_N + s->ch_word[k][b]] + widen) >> 3;
    orp_x80 bx = x80_mul(X(orp_cos2[fi].value), X(orp_r[bi].value));
    f->b = x80_to_double(bx);
    f->c = orp_negr2[bi].value;
    if (gain != 0.0)
        f->a = x80_to_double(x80_mul(x80_sub(x80_sub(X(1.0), bx), X(f->c)), X(gain)));
}

static int16_t widening(const orp_synth *s, int k)
{
    return (int16_t)x80_trunc(x80_mul(x80_from_int(CFGI(ORP_I_WIDEN0 + k)), X(s->widen)));
}

/* 2320da30: the filter bank, summed into s->sum. */
static void bank(orp_synth *s)
{
    memset(s->sum, 0, sizeof s->sum);
    for (int k = 2; k < 4; k++) {   /* F2, F3: fixed gains */
        int16_t w = widening(s, k);
        s->ch[k].a = CFG(ORP_C_GAIN0 + k);
        for (int b = 0; b < ORP_SUBBLOCKS; b++) {
            tune(s, k, b, w, 0.0, &s->ch[k]);
            run_two_pole(&s->ch[k], &s->exc[k][b * ORP_SUB_SAMPLES], &s->sum[b * ORP_SUB_SAMPLES], ORP_SUB_SAMPLES,
                         true);
        }
    }
    int16_t w1 = widening(s, 1);   /* F1, DC-normalised, then the fixed shaping section */
    for (int b = 0; b < ORP_SUBBLOCKS; b++) {
        orp_filter *f = &s->ch[1];
        tune(s, 1, b, w1, CFG(ORP_C_GAIN0 + 1), f);
        run_two_pole(f, &s->exc[1][b * ORP_SUB_SAMPLES], &s->temp[b * ORP_SUB_SAMPLES], ORP_SUB_SAMPLES, false);
        run_two_pole(&s->casc, &s->temp[b * ORP_SUB_SAMPLES], &s->sum[b * ORP_SUB_SAMPLES], ORP_SUB_SAMPLES, true);
    }
    int16_t w0 = widening(s, 0);   /* FN: excited by channel 0 minus channel 1 */
    for (int n = 0; n < ORP_FRAME_SAMPLES; n++)
        s->exc[0][n] = s->exc[0][n] - s->exc[1][n];
    for (int b = 0; b < ORP_SUBBLOCKS; b++) {
        orp_filter *f = &s->ch[0];
        tune(s, 0, b, w0, CFG(ORP_C_GAIN0), f);
        run_two_pole(f, &s->exc[0][b * ORP_SUB_SAMPLES], &s->sum[b * ORP_SUB_SAMPLES], ORP_SUB_SAMPLES, true);
    }
    for (int j = 0; j < 2; j++) {   /* the fixed high cascades (two at fs <= 20000) */
        orp_filter *r = &s->fix[3 * j];
        r[0].a = CFG(ORP_C_GAIN0 + 4 + j);
        run_two_pole(&r[0], s->exc[4 + j], s->temp, ORP_FRAME_SAMPLES, false);
        run_two_pole(&r[1], s->temp, s->temp, ORP_FRAME_SAMPLES, false);
        run_two_pole(&r[2], s->temp, s->sum, ORP_FRAME_SAMPLES, true);
    }
}

/* 2320e170's output stage: de-emphasis with the volume's gain, then the limiter. */
static void output(orp_synth *s, int16_t out[ORP_FRAME_SAMPLES])
{
    orp_filter *f = &s->final_f;
    double y1 = f->y1, y2 = f->y2;
    for (int n = 0; n < ORP_FRAME_SAMPLES; n++) {
        double y = f->a * s->sum[n] + f->b * y1 + f->c * y2;
        double v = f->d * y1 + (f->e * y2 + y);
        if (v >= 32767.0) {
            out[n] = 32767;
            s->clipped++;
        } else if (v < -32768.0) {
            out[n] = -32768;
            s->clipped++;
        } else {
            out[n] = (int16_t)lrint(v);
        }
        y2 = y1;
        y1 = y;
    }
    f->y1 = y1;
    f->y2 = y2;
}

void orp_frame(orp_synth *s, const int16_t p[ORP_PARAMS], bool first, int16_t out[ORP_FRAME_SAMPLES])
{
    load(s, p);
    params_step(s, first);
    if (s->unvoiced) {
        memset(s->voiced, 0, sizeof s->voiced);
        memset(s->phase_word, 0, sizeof s->phase_word);
    } else {
        if (s->prev_unvoiced)
            reset_source(s);
        voiced_source(s);
    }
    noise_source(s);
    mix(s);
    bank(s);
    output(s, out);
}
