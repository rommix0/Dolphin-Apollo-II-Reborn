#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3). The options, the result and the
   shared input and synthesis are render_native.h's. */
#include <stddef.h>
#include <stdint.h>
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
#include "render/render_native.h"

/* Runs text through the chosen engine (prefixed with @K1 for voice b and @Rn for the prosody) and
   synthesizes the chosen stream, with the stream's median pitch as the pitch-range centre. Samples
   are produced only when the engine reaches MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out);

#endif
