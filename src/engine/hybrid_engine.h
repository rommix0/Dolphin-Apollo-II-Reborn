#ifndef HYBRID_ENGINE_H
#define HYBRID_ENGINE_H
/* The "hybrid" engine (spec section 3; M4.0 spec section 3): the rom engine with the routines of the
   replacement table (src/engine/thunks.c) running as C. */
#include "engine/rom_engine.h"

/* rom_engine_run with every replacement active, or only the one named only. MACHINE_FAULT (MFAULT_FIRMWARE)
   when the data image does not build or only names no replacement. produced may be NULL (frames dropped).
   Every call binds a fresh firmware bridge: a bridge that faulted is dead, and no state carries over. */
machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw);

#endif
