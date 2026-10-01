#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3). */
#include <stddef.h>
#include <stdint.h>
#include "engine/rom_engine.h"
#include "synth/synth.h"

typedef enum {
    RENDER_STREAM_PRODUCED,   /* the golden, timing-independent stream (default) */
    RENDER_STREAM_RAW,        /* what the DSP received: one frame late, CPU-timing dependent */
} render_stream;

typedef struct {
    char voice;               /* 'a' (default) or 'b': speaker table B, selected with @K1 */
    render_stream stream;
    int prosody;              /* 0-7: sent as @Rn before the text (default 4, the firmware's unscaled
                                 contour; the device default is 3); -1 = send nothing */
    synth_options synth;
} render_options;

typedef struct {
    int16_t *samples;         /* malloc'ed: frames * SYNTH_FRAME_SAMPLES samples at SYNTH_RATE */
    size_t count;
    size_t frames;
    uint64_t clipped;
} render_result;

void render_default_options(render_options *opt);

/* Runs text through the rom engine (prefixed with @K1 for voice b and @Rn for the prosody) and
   synthesizes the chosen stream, with the stream's median pitch as the pitch-range centre. Samples
   are produced only when the engine reaches MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out);

void render_result_free(render_result *r);

#endif
