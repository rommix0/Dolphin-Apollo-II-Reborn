#ifndef SYNTH_PARAMS_H
#define SYNTH_PARAMS_H
/* Holmes-style parallel-formant synthesizer: structural constants and frame -> parameter mapping (spec §5). */
#include <stdbool.h>
#include <stdint.h>

#define SYNTH_RATE          10000   /* Hz: the firmware's DSP tick; spec R2, never resampled */
#define SYNTH_FRAME_SAMPLES 100     /* one 10 ms frame */
#define SYNTH_FRAME_BYTES   20

#endif
