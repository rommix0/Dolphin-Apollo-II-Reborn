#ifndef ROM_ENGINE_H
#define ROM_ENGINE_H
/* The "rom" engine: the original firmware running on the emulated machine (spec §3). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "machine/machine.h"

#define ROM_ENGINE_BOOT_CYCLES  100000000ULL   /* 50 s of emulated time at 2 MHz */
#define ROM_ENGINE_SPEAK_CYCLES 400000000ULL   /* 200 s */

typedef struct {
    uint8_t (*frames)[APOLLO_FRAME_BYTES];
    size_t count, cap;
} frame_list;

void frame_list_free(frame_list *fl);

/* One frame per line, 40 uppercase hex digits + '\n'. Caller frees. */
char *frame_list_hex(const frame_list *fl);

/* Called once after machine_init, before boot: installs hooks (hybrid engine, differential tests). */
typedef void (*engine_setup_fn)(machine *m, void *user);

/* The engine run: boot to idle, then feed text + CR and run to idle again. setup (may be NULL) runs after
   machine_init. produced gets the produced frames (the boot frames too if include_boot); raw, when not NULL,
   the raw DSP-port frames of the speaking phase. The rom_engine_speak* functions below are this with
   setup = NULL. */
machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                              engine_setup_fn setup, void *user, const char *text, bool include_boot,
                              frame_list *produced, frame_list *raw);

/* Boots m from rom to idle, then feeds text + CR and runs to idle again. Frames produced
   while speaking are appended to out (boot frames too if include_boot). The counters
   frames_produced / lower_window_reads cover the speaking phase only. m stays inspectable. */
machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text,
                                bool include_boot, frame_list *out);

/* Same, with an explicit machine configuration (NULL = defaults); used by timing-invariance tests. */
machine_status rom_engine_speak_cfg(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    const char *text, bool include_boot, frame_list *out);

/* Like rom_engine_speak_cfg without boot frames. It also collects the raw DSP-port stream (frames
   as the DSP received them, re-aligned at every DSP reset, ISR replays included) into raw when
   raw != NULL. Both lists cover the speaking phase only, as do the raw_* counters of m. */
machine_status rom_engine_speak_streams(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                        const char *text, frame_list *produced, frame_list *raw);

#endif
