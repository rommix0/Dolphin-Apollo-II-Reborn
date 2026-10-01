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

/* Boots m from rom to idle, then feeds text + CR and runs to idle again. Frames produced
   while speaking are appended to out (boot frames too if include_boot). The counters
   frames_produced / lower_window_reads cover the speaking phase only. m stays inspectable. */
machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text,
                                bool include_boot, frame_list *out);

#endif
