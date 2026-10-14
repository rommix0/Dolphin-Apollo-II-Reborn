#ifndef RENDER_RENDER_NATIVE_H
#define RENDER_RENDER_NATIVE_H
/* text -> native engine -> frame stream -> synthesizer -> 10 kHz samples (M5a spec 3.7), and the part render_text
   (render.h) shares: the options, the result, the input with its prefix and the synthesis. No machine/ or cpu740/
   header. */
#include <stddef.h>
#include <stdint.h>
#include "engine/frame_list.h"
#include "synth/synth.h"

typedef enum {
    RENDER_STREAM_PRODUCED,   /* the golden, timing-independent stream (default) */
    RENDER_STREAM_RAW,        /* what the DSP received: one frame late, CPU-timing dependent */
} render_stream;

typedef enum {
    RENDER_ENGINE_ROM,        /* the original firmware, emulated (default) */
    RENDER_ENGINE_HYBRID,     /* the same, with the ported routines running as C (M4) */
} render_engine;

typedef struct {
    char voice;               /* 'a' (default) or 'b': speaker table B, selected with @K1 */
    render_stream stream;
    render_engine engine;     /* render_text's choice; render_text_native ignores it */
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

/* The text an engine gets: prefixed with @K1 for voice b and @Rn for the prosody. Caller frees. */
char *render_input(const char *text, const render_options *opt);

/* Synthesizes fl into out (cleared first) with fl's median pitch as the pitch-range centre; nothing for no frame. */
void render_synthesize(const frame_list *fl, const render_options *opt, render_result *out);

/* render_text on the native engine: the same input, the chosen stream synthesized only at ENGINE_IDLE (never a
   partial stream). */
engine_status render_text_native(const char *text, const render_options *opt, render_result *out);

void render_result_free(render_result *r);

#endif
