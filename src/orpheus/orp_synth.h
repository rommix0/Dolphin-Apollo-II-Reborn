#ifndef ORPHEUS_ORP_SYNTH_H
#define ORPHEUS_ORP_SYNTH_H
/* The Orpheus synthesizer back end, ported from a3s.dll (Orpheus Build 18) at 10 kHz only (spec
   2026-10-16-orpheus-synth-analysis.md). A frame is 16 parameters: p0-p3 channel 0-3 frequencies (Hz), p4-p11
   channel 0-7 amplitudes (dB codes 0..63), p12 voicing 0..100, p13 F0 (Hz), p14 glottal pulse rate, p15 voice
   source. Equal sample for sample to the Unicorn oracle (tests/orpheus/). */
#include <stdbool.h>
#include <stdint.h>

#define ORP_PARAMS        16
#define ORP_FRAME_SAMPLES 100   /* 10 ms */
#define ORP_SUBBLOCKS     10    /* coefficients and smoothed parameters change once per sub-block */
#define ORP_SUB_SAMPLES   (ORP_FRAME_SAMPLES / ORP_SUBBLOCKS)
#define ORP_CHANNELS      8
#define ORP_TRACKS        15    /* p0-p14 are smoothed (p12 is not, but keeps its slot) */

typedef struct {
    double a, b, c, e, d, y1, y2;   /* y = a x + b y1 + c y2; out = y + d y1 (+ e y2 in the output stage) */
} orp_filter;

typedef struct {
    int16_t cur[ORP_PARAMS], prev[ORP_PARAMS], thr[ORP_PARAMS], jump[ORP_PARAMS];
    bool unvoiced, prev_unvoiced, burst;
    bool x87_ext;   /* the x87 runs at 64-bit precision: true once an asm block's fninit ran (before, the CRT's 53) */
    double widen;                                   /* F1-F3 bandwidth widening factor 0..1 */
    double track[ORP_TRACKS][ORP_SUBBLOCKS];        /* smoothed parameters; amplitudes become linear */
    orp_filter smooth[ORP_TRACKS];
    int32_t phase, step_acc, step;                  /* voiced source: pitch phase, table step, table index */
    int32_t step_quarter;
    double held;                                    /* the source value being held */
    int16_t lcg;
    int16_t phase_word[ORP_SUBBLOCKS];              /* pitch-synchronous phase 0..16 per sub-block */
    int16_t ch_word[ORP_CHANNELS][ORP_SUBBLOCKS];   /* each channel's bandwidth phase */
    double voiced[ORP_FRAME_SAMPLES], noise_raw[ORP_FRAME_SAMPLES], noise[ORP_FRAME_SAMPLES];
    double exc[ORP_CHANNELS][ORP_FRAME_SAMPLES], temp[ORP_FRAME_SAMPLES], sum[ORP_FRAME_SAMPLES];
    orp_filter noise_f, final_f, casc, ch[4], fix[6];
    uint64_t clipped;
} orp_synth;

void orp_init(orp_synth *s);
/* One frame: a3s.dll 2320ce70(p, 56, 1) then 2320e170(first, out), at volume 56 (orp_data.c's final gain). first
   is true for an utterance's first frame only: every parameter then steps instead of gliding. */
void orp_frame(orp_synth *s, const int16_t p[ORP_PARAMS], bool first, int16_t out[ORP_FRAME_SAMPLES]);
uint64_t orp_clipped(const orp_synth *s);   /* samples the output limiter saturated */

#endif
