#ifndef ORPHEUS_ORP_MAP_H
#define ORPHEUS_ORP_MAP_H
/* Apollo frame -> Orpheus parameters: the orpheus_bal mapping chosen by listening (spec
   2026-10-16-orpheus-synth-analysis.md, verdict). xtras/orpheus/orpheus_map.py is its Python twin. */
#include <stdint.h>
#include "orpheus/orp_synth.h"
#include "synth/params.h"

/* centre_hz is params_f0_centre of the utterance (0: no pitch-range gain). */
void orp_map_frame(const uint8_t f[SYNTH_FRAME_BYTES], float centre_hz, int16_t p[ORP_PARAMS]);

#endif
