#ifndef MACHINE_H
#define MACHINE_H
/* The Apollo 2 board: M50734 + 64 KB EPROM + RAM + DSP port (spec §1.2, §4). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cpu740/cpu740.h"
#include "machine/m50734.h"

#define MACHINE_ROM_SIZE        0x10000
#define MACHINE_RAM_SIZE        0x4000   /* $0000-$3FFF; $00DA-$00FF are SFRs instead */
#define MACHINE_HISTORY         64
#define MACHINE_MAX_UNMODELLED  64
#define MACHINE_QUEUE_SIZE      4096

/* Firmware landmarks in UK V2.01 4.04 (spec §1.3) */
#define APOLLO_SWAP_PC          0xB306   /* timer ISR, about to swap DSP frame buffers */
#define APOLLO_IDLE_WIT_PC      0xC602   /* the firmware's only WIT: idle, waiting for input */
#define APOLLO_PRODUCER_PTR     0x2C     /* zero-page pointer to the buffer the main loop fills */
#define APOLLO_FRAME_BYTES      20

typedef void (*machine_frame_fn)(void *user, const uint8_t frame[APOLLO_FRAME_BYTES]);

/* port_in: P0..P4 board input levels. cycle_num/cycle_den: TEST-ONLY scale applied to the CPU
   cycles fed to the peripheral timers (ticks = cycles * num / den, remainder carried); 0 means 1.
   machine_default_config sets 1/1. cpu.cycles and the max_cycles budget stay in CPU cycles. The
   produced-frame stream must not depend on this scale (spec section 4). */
typedef struct { uint8_t port_in[5]; uint32_t cycle_num, cycle_den; } machine_config;

typedef enum { MACHINE_IDLE, MACHINE_FAULT, MACHINE_CYCLE_LIMIT } machine_status;
typedef enum { MFAULT_NONE, MFAULT_CPU, MFAULT_DME, MFAULT_ESCAPED_PC } machine_fault;

typedef struct { uint16_t addr, pc; bool write; } machine_access;

typedef struct machine {
    cpu740 cpu;
    cpu740_bus bus;
    m50734 sfr;
    uint8_t rom[MACHINE_ROM_SIZE];
    uint8_t ram[MACHINE_RAM_SIZE];
    uint32_t cycle_num, cycle_den;         /* peripheral timer scale (see machine_config) */
    uint64_t cycle_rem;                    /* remainder of the scaled ticks, < cycle_den */
    machine_fault fault;
    uint16_t op_pc;                        /* address of the instruction being executed */
    uint8_t queue[MACHINE_QUEUE_SIZE];     /* serial input ring buffer */
    size_t q_head, q_tail;
    bool busy_level_known;
    uint8_t busy_idle_level;               /* P0.5 level at the first idle = "not busy" */
    machine_frame_fn on_frame;
    void *user;
    uint64_t frames_produced;
    uint64_t port_writes;                  /* bytes written to the DSP port at $8000 */
    uint64_t lower_window_reads;           /* $4000-$7FFF reads served from file 0x0000-0x3FFF */
    uint16_t history[MACHINE_HISTORY];     /* PCs of the most recent instructions */
    unsigned history_pos;
    machine_access unmodelled[MACHINE_MAX_UNMODELLED];
    unsigned unmodelled_count;
} machine;

void machine_default_config(machine_config *cfg);
bool machine_init(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg);
void machine_set_outputs(machine *m, machine_frame_fn on_frame, void *user);
bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n);
machine_status machine_run(machine *m, uint64_t max_cycles);
uint8_t machine_peek(const machine *m, uint16_t addr);
void machine_dump(const machine *m, FILE *out);

#endif
