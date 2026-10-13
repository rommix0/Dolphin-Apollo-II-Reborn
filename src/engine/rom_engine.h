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

/* The input an engine run feeds at the end of boot: n bytes (raw serial input: a NUL may be among them), then a CR
   when cr is set. */
typedef struct { const uint8_t *bytes; size_t n; bool cr; } engine_input;

/* Watches an engine run (diff_main, M4.6c2 spec 4.1), each callback when not NULL: on_frame at every frame event (the
   machine's frame callback, whether the frames are kept or not), on_idle at every input idle, before the engine's
   idle handler feeds or stops. Both get the engine's machine, which on_idle may change (an injection). */
typedef struct {
    void (*on_frame)(void *user, machine *m);
    void (*on_idle)(void *user, machine *m);
    void *user;
} engine_watch;

/* The engine run (M4.6c2 spec 3.1): machine_init, setup (may be NULL: it installs hooks), then machine_run in a loop
   with the engine's idle handler on the machine. At the first idle (the end of boot) the handler resets the frame
   counters, keeps the frames from then on and feeds in; at the next it stops the run, and machine_run's MACHINE_IDLE
   is the result. The phase deadline (m->phase_deadline) is ROM_ENGINE_BOOT_CYCLES from the start and
   ROM_ENGINE_SPEAK_CYCLES from the feed; reaching it is MACHINE_CYCLE_LIMIT. Input longer than the serial queue is
   MFAULT_INPUT_OVERFLOW (MACHINE_FAULT). produced gets the produced frames (the boot frames too if include_boot);
   raw, when not NULL, the raw DSP-port frames of the speaking phase; watch (may be NULL) sees every event. */
machine_status rom_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    engine_setup_fn setup, void *user, const engine_input *in, bool include_boot,
                                    frame_list *produced, frame_list *raw, const engine_watch *watch);

/* rom_engine_run_input with the input text + CR and no watch. The rom_engine_speak* functions below are this with
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
