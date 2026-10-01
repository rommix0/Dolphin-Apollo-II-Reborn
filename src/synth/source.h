#ifndef SYNTH_SOURCE_H
#define SYNTH_SOURCE_H
/* Excitation for the parallel-formant bank (spec §5). Voicing (impulse train, or a Rosenberg
   pulse when the b11 hypothesis is on) and noise are mixed by the degree of voicing. In the
   mid range (voiced fricatives) the noise is pitch-modulated. Every excitation has unit
   average power at the source, so V changes the voiced/noise balance there; the output loudness
   also depends on where the formants put the spectrum. */
#include <stdint.h>

typedef struct {
    uint32_t noise_state;   /* LCG state */
    float phase;            /* position in the current pitch period, 0..1 */
    float prev_flow;        /* Rosenberg glottal flow at the previous sample */
} source;

typedef struct {
    float f0_hz;
    float voicing;          /* 0 = noise only .. 1 = voiced only */
    float open_quotient;    /* 0 = unit impulse train; > 0 = Rosenberg pulse with this open quotient */
} source_params;

void source_init(source *s);
float source_step(source *s, const source_params *p, float fs);

#endif
