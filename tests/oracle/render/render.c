#include <stdlib.h>
#include <string.h>
#include "render/render.h"

machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text,
                           const render_options *opt, render_result *out)
{
    char *input = render_input(text, opt);
    frame_list produced = { 0 }, raw = { 0 };
    memset(out, 0, sizeof *out);
    frame_list *raw_out = opt->stream == RENDER_STREAM_RAW ? &raw : NULL;
    machine_status st = engine == RENDER_ENGINE_HYBRID
        ? hybrid_engine_speak(m, rom, len, NULL, NULL, input, false, &produced, raw_out, NULL)
        : rom_engine_speak_streams(m, rom, len, NULL, input, &produced, raw_out);
    if (st == MACHINE_IDLE)
        render_synthesize(opt->stream == RENDER_STREAM_RAW ? &raw : &produced, opt, out);
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
