#ifndef SYNTH_SYNTH_H
#define SYNTH_SYNTH_H
/* Holmes-style parallel-formant synthesizer (spec §5). Five second-order resonators (FN, F1, F2,
   F3 and a fixed fourth formant) share one excitation. Their outputs are summed with
   alternating signs. Amplitudes ramp linearly across each frame; frequencies update once per
   frame. Output is 16-bit at 10 kHz, 100 samples per frame. */
#include <stdint.h>
#include "synth/params.h"
#include "synth/resonator.h"
#include "synth/source.h"

typedef struct {
    synth_options opt;
    source src;
    resonator res[5];   /* FN, F1, F2, F3, F4 */
    float gain[5];      /* gains reached at the end of the previous frame: the start of each ramp */
    uint64_t clipped;   /* output samples clipped to the int16 range */
} synth;

void synth_init(synth *s, const synth_options *opt);   /* opt NULL = all hypotheses off */
void synth_frame(synth *s, const uint8_t frame[SYNTH_FRAME_BYTES], int16_t out[SYNTH_FRAME_SAMPLES]);

#endif
