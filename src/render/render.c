#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render/render.h"

void render_default_options(render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->voice = 'a';
    opt->stream = RENDER_STREAM_PRODUCED;
    opt->prosody = 4;
    synth_default_options(&opt->synth);
}

void render_result_free(render_result *r)
{
    free(r->samples);
    memset(r, 0, sizeof *r);
}

machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out)
{
    char prefix[16];   /* at most "@K1 @R7 " */
    int plen = snprintf(prefix, sizeof prefix, "%s", opt->voice == 'b' ? "@K1 " : "");   /* manual §6 @Kb */
    if (opt->prosody >= 0 && opt->prosody <= 7)                                       /* manual §6 @Ro */
        plen += snprintf(prefix + plen, sizeof prefix - (size_t)plen, "@R%d ", opt->prosody);
    frame_list produced = { 0 }, raw = { 0 };
    size_t n = strlen(text);
    char *input = malloc((size_t)plen + n + 1);
    if (!input)
        abort();
    memcpy(input, prefix, (size_t)plen);
    memcpy(input + plen, text, n + 1);
    memset(out, 0, sizeof *out);
    machine_status st = rom_engine_speak_streams(m, rom, len, NULL, input, &produced,
                                                 opt->stream == RENDER_STREAM_RAW ? &raw : NULL);
    const frame_list *chosen = opt->stream == RENDER_STREAM_RAW ? &raw : &produced;
    if (st == MACHINE_IDLE && chosen->count > 0) {
        synth s;
        synth_options so = opt->synth;
        so.f0_centre_hz = params_f0_centre(&chosen->frames[0][0], chosen->count);   /* pitch-range centre */
        synth_init(&s, &so);
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
