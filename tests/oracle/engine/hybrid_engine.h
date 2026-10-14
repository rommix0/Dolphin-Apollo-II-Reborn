#ifndef HYBRID_ENGINE_H
#define HYBRID_ENGINE_H
/* The "hybrid" engine (spec section 3; M4.0 spec section 3): the rom engine with the routines of the
   replacement table (tests/oracle/engine/thunks.c) running as C. */
#include "engine/rom_engine.h"

typedef struct {
    uint64_t thunk_calls;   /* times a C routine ran instead of the ROM's */
    unsigned tn_flushes;    /* mid-clause flushes by the C tn_convert_clause */
    unsigned unwinds;       /* C call chains abandoned at a stack reset (@= entering a bank, M4.2 spec 3) */
} hybrid_stats;

/* rom_engine_run with every replacement active, or only the one named only. MACHINE_FAULT (MFAULT_FIRMWARE)
   when the data image does not build or only names no replacement. produced may be NULL (frames dropped).
   Every call binds a fresh firmware bridge: a bridge that faulted is dead, and no state carries over.
   stats (may be NULL) receives what the C did. */
machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats);
/* hybrid_engine_speak with the input and the watch of rom_engine_run_input. */
machine_status hybrid_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                       const char *only, const engine_input *in, bool include_boot,
                                       frame_list *produced, frame_list *raw, hybrid_stats *stats,
                                       const engine_watch *watch);

#endif
