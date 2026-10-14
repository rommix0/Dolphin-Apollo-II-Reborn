#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> emulated firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3), for the tests: the ROM
   engine or the hybrid. The options, the result and the shared input and synthesis are render_native.h's. */
#include <stddef.h>
#include <stdint.h>
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
#include "render/render_native.h"

typedef enum {
    RENDER_ENGINE_ROM,        /* the original firmware, emulated */
    RENDER_ENGINE_HYBRID,     /* the same, with the ported routines running as C (M4) */
} render_engine;

/* Runs text through engine (prefixed with @K1 for voice b and @Rn for the prosody) and synthesizes the chosen stream,
   with the stream's median pitch as the pitch-range centre. Samples are produced only when the engine reaches
   MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text,
                           const render_options *opt, render_result *out);

#endif
