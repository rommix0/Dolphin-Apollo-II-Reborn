#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render/render_native.h"
#include "engine/native_engine.h"
#include "orpheus/orp_map.h"
#include "orpheus/orp_synth.h"

_Static_assert(ORP_FRAME_SAMPLES == SYNTH_FRAME_SAMPLES, "both synths make 10 ms frames at 10 kHz");

void render_default_options(render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->voice = 'a';
    opt->stream = RENDER_STREAM_PRODUCED;
    opt->prosody = 4;
    opt->synth_kind = RENDER_SYNTH_ORPHEUS;
    synth_default_options(&opt->synth);
}

void render_result_free(render_result *r)
{
    free(r->samples);
    memset(r, 0, sizeof *r);
}

char *render_input(const char *text, const render_options *opt)
{
    char prefix[16];   /* at most "@K1 @R7 " */
    int plen = snprintf(prefix, sizeof prefix, "%s", opt->voice == 'b' ? "@K1 " : "");   /* manual §6 @Kb */
    if (opt->prosody >= 0 && opt->prosody <= 7)                                       /* manual §6 @Ro */
        plen += snprintf(prefix + plen, sizeof prefix - (size_t)plen, "@R%d ", opt->prosody);
    size_t n = strlen(text);
    char *input = malloc((size_t)plen + n + 1);
    if (!input)
        abort();
    memcpy(input, prefix, (size_t)plen);
    memcpy(input + plen, text, n + 1);
    return input;
}

void render_synthesize(const frame_list *fl, const render_options *opt, render_result *out)
{
    memset(out, 0, sizeof *out);
    if (fl->count == 0)
        return;
    float centre = params_f0_centre(&fl->frames[0][0], fl->count);   /* pitch-range centre */
    out->samples = malloc(fl->count * SYNTH_FRAME_SAMPLES * sizeof out->samples[0]);
    if (!out->samples)
        abort();
    if (opt->synth_kind == RENDER_SYNTH_ORPHEUS) {
        static orp_synth os;   /* about 13 KB */
        orp_init(&os);
        for (size_t i = 0; i < fl->count; i++) {
            int16_t p[ORP_PARAMS];
            orp_map_frame(fl->frames[i], centre, p);
            orp_frame(&os, p, i == 0, out->samples + i * SYNTH_FRAME_SAMPLES);
        }
        out->clipped = orp_clipped(&os);
    } else {
        synth s;
        synth_options so = opt->synth;
        so.f0_centre_hz = centre;
        synth_init(&s, &so);
        for (size_t i = 0; i < fl->count; i++)
            synth_frame(&s, fl->frames[i], out->samples + i * SYNTH_FRAME_SAMPLES);
        out->clipped = s.clipped;
    }
    out->frames = fl->count;
    out->count = fl->count * SYNTH_FRAME_SAMPLES;
}

engine_status render_text_native(native_engine *e, const char *text, const render_options *opt, render_result *out)
{
    char *input = render_input(text, opt);
    frame_list produced = { 0 }, raw = { 0 };
    bool want_raw = opt->stream == RENDER_STREAM_RAW;
    engine_status st = native_engine_speak(e, NULL, input, false, &produced, want_raw ? &raw : NULL);
    memset(out, 0, sizeof *out);
    if (st == ENGINE_IDLE)
        render_synthesize(want_raw ? &raw : &produced, opt, out);
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
