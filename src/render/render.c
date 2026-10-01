#include <stdlib.h>
#include <string.h>
#include "render/render.h"

void render_default_options(render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->voice = 'a';
    opt->stream = RENDER_STREAM_PRODUCED;
}

void render_result_free(render_result *r)
{
    free(r->samples);
    memset(r, 0, sizeof *r);
}

machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out)
{
    static const char k_voice_b[] = "@K1 ";   /* manual §6 @Kb: 1 = non-male speaker table */
    frame_list produced = { 0 }, raw = { 0 };
    size_t n = strlen(text), prefix = opt->voice == 'b' ? sizeof k_voice_b - 1 : 0;
    char *input = malloc(prefix + n + 1);
    if (!input)
        abort();
    memcpy(input, k_voice_b, prefix);
    memcpy(input + prefix, text, n + 1);
    memset(out, 0, sizeof *out);
    machine_status st = rom_engine_speak_streams(m, rom, len, NULL, input, &produced,
                                                 opt->stream == RENDER_STREAM_RAW ? &raw : NULL);
    const frame_list *chosen = opt->stream == RENDER_STREAM_RAW ? &raw : &produced;
    if (st == MACHINE_IDLE && chosen->count > 0) {
        synth s;
        synth_init(&s, &opt->synth);
        out->samples = malloc(chosen->count * SYNTH_FRAME_SAMPLES * sizeof out->samples[0]);
        if (!out->samples)
            abort();
        for (size_t i = 0; i < chosen->count; i++)
            synth_frame(&s, chosen->frames[i], out->samples + i * SYNTH_FRAME_SAMPLES);
        out->frames = chosen->count;
        out->count = chosen->count * SYNTH_FRAME_SAMPLES;
        out->clipped = s.clipped;
    }
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
