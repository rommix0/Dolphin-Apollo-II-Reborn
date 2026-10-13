#ifndef MACHINE_H
#define MACHINE_H
/* The Apollo 2 board: M50734 + 64 KB EPROM + RAM + DSP port (spec §1.2, §4). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cpu740/cpu740.h"
#include "machine/m50734.h"
#include "machine/coverage.h"

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
#define APOLLO_DSP_PORT         0x8000   /* DSP data register (write only) */

typedef void (*machine_frame_fn)(void *user, const uint8_t frame[APOLLO_FRAME_BYTES]);

/* port_in: P0..P4 board input levels. cycle_num/cycle_den: TEST-ONLY scale applied to the CPU
   cycles fed to the peripheral timers (ticks = cycles * num / den, remainder carried); 0 means 1.
   machine_default_config sets 1/1. cpu.cycles and the max_cycles budget stay in CPU cycles. The
   produced-frame stream must not depend on this scale (spec section 4).
   coverage: when not NULL, every executed instruction and every indirect JMP/JSR target is recorded
   there (M3 spec §5.1). machine_default_config sets NULL. */
typedef struct { uint8_t port_in[5]; uint32_t cycle_num, cycle_den; coverage *coverage; } machine_config;

typedef enum { MACHINE_IDLE, MACHINE_FAULT, MACHINE_CYCLE_LIMIT } machine_status;
typedef enum { MFAULT_NONE, MFAULT_CPU, MFAULT_DME, MFAULT_ESCAPED_PC, MFAULT_INPUT_OVERFLOW,
               MFAULT_FIRMWARE } machine_fault;

typedef struct { uint16_t addr, pc; bool write; } machine_access;

#define MACHINE_MAX_HOOKS          256
#define MACHINE_ROM_CALL_SENTINEL  0x0000   /* machine_rom_call's return address: the ROM never runs code at $0000 */
#define MACHINE_RAMCODE_FIRST      0x0200   /* the RAM copy of bank_ramcode_image (machine_hook_ramcode) ... */
#define MACHINE_RAMCODE_LAST       0x03FE   /* ... to its last byte */

struct machine;
/* A C replacement or observer for the ROM routine at a hooked address (M4.0 spec section 3.2). It returns
   true when it performed the routine (leaving the machine as the routine's RTS would, e.g. through
   machine_return), false to let the ROM routine run. Setting m->fault stops the run. Returning true with the PC
   still at the hooked address and no fault set is MFAULT_FIRMWARE at that address (it would run forever). */
typedef bool (*machine_hook_fn)(struct machine *m, void *user);
typedef struct { uint16_t addr; machine_hook_fn fn; void *user; bool ramcode; } machine_hook_entry;
typedef struct { uint8_t a, x, y, p; } machine_regs;

/* Where a CPU read lands under the current P2 (machine_map_read). */
typedef enum { MACHINE_MAP_RAM, MACHINE_MAP_SFR, MACHINE_MAP_ROM, MACHINE_MAP_EMPTY } machine_map_region;

typedef struct machine {
    cpu740 cpu;
    cpu740_bus bus;
    m50734 sfr;
    uint8_t rom[MACHINE_ROM_SIZE];
    uint8_t ram[MACHINE_RAM_SIZE];
    uint32_t cycle_num, cycle_den;         /* peripheral timer scale (see machine_config) */
    coverage *coverage;                    /* execution coverage sink, NULL = off (machine_config) */
    uint64_t cycle_rem;                    /* remainder of the scaled ticks, < cycle_den */
    machine_fault fault;
    uint16_t op_pc;                        /* address of the instruction being executed */
    uint8_t queue[MACHINE_QUEUE_SIZE];     /* serial input ring buffer */
    size_t q_head, q_tail;
    bool busy_level_known;
    uint8_t busy_idle_level;               /* P0.5 level at the first idle = "not busy" */
    machine_frame_fn on_frame;
    void *user;
    machine_frame_fn on_raw;               /* raw DSP-port frames (machine_set_raw_output) */
    void *raw_user;
    uint8_t raw_buf[APOLLO_FRAME_BYTES];
    unsigned raw_pos;                      /* bytes of the current raw frame received so far */
    bool dsp_reset_level;                  /* last P2.0 pin level */
    uint64_t raw_frames;                   /* complete raw frames delivered */
    uint64_t dsp_resets;                   /* P2.0 rising edges: DSP reset pulses from $B38B */
    uint64_t raw_discarded;                /* partial-frame bytes dropped at a DSP reset */
    uint64_t frames_produced;
    uint64_t port_writes;                  /* bytes written to the DSP port at $8000 */
    uint64_t lower_window_reads;           /* $4000-$7FFF reads served from file 0x0000-0x3FFF */
    uint16_t history[MACHINE_HISTORY];     /* PCs of the most recent instructions */
    unsigned history_pos;
    machine_access unmodelled[MACHINE_MAX_UNMODELLED];
    unsigned unmodelled_count;
    unsigned unmodelled_dropped;           /* accesses not logged because the log was full; repeats of a logged access are
                                              not counted, repeats of an unlogged one are counted each time */
    machine_hook_entry hooks[MACHINE_MAX_HOOKS];
    unsigned hook_count;
    uint8_t hook_map[0x10000 / 8];         /* bit set = hooked address (fast test before the table) */
    bool irq_hold;                         /* no IRQ line reaches the CPU (machine_hold_irqs) */
    bool skip_hook;                        /* machine_rom_call_unhooked: the hook at skip_hook_pc does not fire ... */
    uint16_t skip_hook_pc;                 /* ... until the instruction there has run once */
    uint16_t fault_addr;                   /* MFAULT_FIRMWARE: the address the C code could not read or call */
} machine;

void machine_default_config(machine_config *cfg);
bool machine_init(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg);
void machine_set_outputs(machine *m, machine_frame_fn on_frame, void *user);
/* Raw DSP-port capture (spec §4). Bytes written to $8000 are grouped into 20-byte frames the way
   the DSP receives them (even byte of each word first). A DSP reset pulse on P2.0 restarts the
   grouping at byte 0. Unlike the produced frames, this stream depends on CPU timing. */
void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user);
bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n);
machine_status machine_run(machine *m, uint64_t max_cycles);
/* One instruction, interrupt entry or hooked routine, with serial delivery, IRQ sync and frame capture as in
   machine_run. Returns false on a fault (m->fault says which). */
bool machine_step(machine *m);
/* Registers fn for the routine at addr. Before each instruction, when the PC is a hooked address that maps to
   the main ROM file under the current P2, the CPU is not waiting and no interrupt is about to be taken, fn
   runs. False when the table is full or addr is already hooked.
   Limitation: exec treats a hook that returns true with the PC still at the hooked entry as MFAULT_FIRMWARE
   (zero-cycle spin guard). That would misfire if a JSR's return address were itself a hooked entry (the
   "JSR x / x:" idiom); every M4.0 entry is preceded by an RTS, so it is not hit. */
bool machine_hook(machine *m, uint16_t addr, machine_hook_fn fn, void *user);
/* Registers fn for the RAM-code routine at addr ($0200-$03FE; M4.6c1 spec 3.3). It fires as a machine_hook does,
   but where the PC is addr and the address maps to RAM: only while the CPU executes the RAM copy there. False when
   addr is outside $0200-$03FE, the table is full or addr is already hooked. */
bool machine_hook_ramcode(machine *m, uint16_t addr, machine_hook_fn fn, void *user);
void machine_clear_hooks(machine *m);
/* Pops a return address and continues after it, like RTS. */
void machine_return(machine *m);
/* Pulls P, then the PC, and continues there, like RTI (cpu740.c, M_RTI: P | B): the exit of a replaced interrupt
   handler (M4.6a spec 3.2). Unlike M_RTI, it does not fault when the pulled P has T set. */
void machine_return_interrupt(machine *m);
/* While hold is set, no IRQ line reaches the CPU; requests stay pending in the SFRs. */
void machine_hold_irqs(machine *m, bool hold);
/* Calls the routine at addr with A/X/Y/P from regs, as if by JSR from MACHINE_ROM_CALL_SENTINEL, and steps
   until it returns there. Hooks, interrupts (unless held) and frame capture work normally. MCALL_RETURNED: regs
   holds the exit registers, the PC is restored and the other registers keep their exit values. MCALL_UNWOUND:
   an instruction lifted SP above the call's frame without returning to it (a stack reset, M4.2 spec 3), so the
   routine can never return; the call stops right after that instruction, with PC and SP where the routine left
   them. MCALL_FAILED: a fault, or max_cycles passed first. Precondition: it does not clear cpu.waiting, so it
   must not be called while the CPU is waiting at WIT (hooks never fire then). */
typedef enum { MCALL_FAILED = 0, MCALL_RETURNED, MCALL_UNWOUND } machine_call_result;
machine_call_result machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles);
/* machine_rom_call with the hook at addr skipped once, at the routine's own first instruction (M4.6c1 spec 3.3): it
   runs as ROM there; any later entry of addr, and every other hook, fires as usual. */
machine_call_result machine_rom_call_unhooked(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles);
/* Runs the routine the PC has just entered (by JSR) until the stack pointer rises above its value now, i.e.
   the routine returned, also two levels up. False on a fault or when max_cycles pass first. */
bool machine_finish_routine(machine *m, uint64_t max_cycles);
/* The frame capture the step loop makes at APOLLO_SWAP_PC ($B306): the 20 bytes at the APOLLO_PRODUCER_PTR
   pointer, frames_produced counted, the frame callback called (M4.6b spec 3.3: the hybrid host's frame event). */
void machine_capture_frame(machine *m);
/* Waits as a WIT at the current PC would (M4.6b spec 3.3): the CPU idles until an IRQ line wakes it, takes the
   interrupt (its handler ROM or a hooked replacement) and returns from it. True once the PC and SP are back where
   they were. The PC is a hook's, never the idle WIT's ($C602): no serial byte is delivered. False when max_cycles
   pass first, on a fault, or when a line wakes the CPU with I set (the interrupt could never be taken); the CPU is
   then no longer waiting, and the PC and SP may be inside the handler (the caller faults: run_frame puts them back). */
bool machine_wait_interrupt(machine *m, uint64_t max_cycles);
/* Runs the ROM loop at loop with I set (inside the caller's PHP/SEI ... PLP; M4.6b spec 3.3) until the PC reaches
   exit, then puts the PC and P back as they were. No stack frame. False when max_cycles pass first or on a fault
   (the PC and P put back all the same). */
bool machine_run_loop(machine *m, uint16_t loop, uint16_t exit, uint64_t max_cycles);
/* Copies src's whole state into dst for a differential run, without hooks (a pending skip of
   machine_rom_call_unhooked included), outputs, coverage or the hold. */
void machine_clone(machine *dst, const machine *src);
/* The shared address mapper (M4.0 spec section 2.2) behind bus reads, machine_peek and the hook check: where a
   read of addr lands under the current P2 (spec section 1.2). *offset indexes ram for MACHINE_MAP_RAM and the
   ROM file for MACHINE_MAP_ROM (the lower-ROM window maps $4000-$7FFF to file 0x0000-0x3FFF, so main ROM is
   MACHINE_MAP_ROM with *offset == addr); it is left unchanged otherwise. The DSP port at $8000 is write-only
   and reads as main ROM. Side-effect free. */
machine_map_region machine_map_read(const machine *m, uint16_t addr, uint32_t *offset);
uint8_t machine_peek(const machine *m, uint16_t addr);
void machine_dump(const machine *m, FILE *out);
/* Lists every logged unmodelled access and the count of dropped ones (nothing if there are none). */
void machine_print_unmodelled(const machine *m, FILE *out);

#endif
