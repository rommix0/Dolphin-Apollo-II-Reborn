#ifndef ENGINE_THUNKS_H
#define ENGINE_THUNKS_H
/* The replacement table: ROM routines that run as C in the hybrid engine (M4.0 spec section 3.2). Each thunk
   maps the routine's registers to its C function and emulates the RTS, unless a ROM call from the C reset the
   stack (M4.2 spec 3): then the machine goes on where the ROM went. The C part takes 0 emulated cycles and
   cannot be interrupted; a ROM routine it calls through rom_call runs normally, with its cycles and any
   interrupt that falls due. */
#include "machine/machine.h"
#include "firmware/fw.h"

/* The registers and flags a routine's callers use (or more: tn_append_phonetic declares A, X and Y, M4.1 spec 7):
   the thunk sets them, the differential test compares them. */
enum { FW_OUT_A = 0x01, FW_OUT_X = 0x02, FW_OUT_Y = 0x04, FW_OUT_Z = 0x08 };

/* The firmware context of one machine: its RAM, the converted data image, the emulator as host.
   thunk_calls: thunks run so far; unwinds: C call chains abandoned at a stack reset (M4.2 spec 3).
   deadline: the machine cycle count by which the outermost thunk must end (0 = none). The outermost thunk (none
   active) sets it to its entry cycles + FW_ROM_CALL_SPEECH_BUDGET and clears it when it returns or unwinds; nested
   thunks leave it alone. Each ROM call from C gets at most the cycles left before it and fails once it has passed,
   so a C wait that never ends (a whole utterance runs inside one hook call, where machine_run's budget is never
   checked) faults instead of hanging. An outermost entry row (phase set; M4.6c2 spec 3.2) runs instead under the
   engine's phase deadline, m->phase_deadline, which each feed moves: a wait that reaches it stops the run
   (FW_WAIT_STOPPED), so that machine_run returns MACHINE_CYCLE_LIMIT as the ROM engine's does. stops: C call chains
   abandoned at a stopped wait (M4.6c2 spec 3.1). */
typedef struct fw_bridge {
    machine *m;
    fw f;
    fw_host host;
    uint64_t thunk_calls, unwinds, stops, deadline;
    bool phase;
} fw_bridge;

typedef struct {
    const char *name;
    uint16_t addr;
    uint8_t outputs;          /* FW_OUT_* */
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
    bool waits;               /* waits on the timer interrupt (the frame loop, the frame waits): checked by diff_play,
                                 not diff_lts */
    bool isr;                 /* an interrupt handler, entered through its vector: the thunk exits like RTI
                                 (machine_return_interrupt) and declares no outputs (M4.6a spec 3.2) */
    bool ramcode;             /* a routine of the RAM copy of bank_ramcode_image ($0200-$03FE): hooked with
                                 machine_hook_ramcode (M4.6c1 spec 3.3) */
    bool entry;               /* an entry (boot_reset, the re-entries, the bank entries; M4.6c2 spec 2.3): it never
                                 returns; an outermost one runs under the engine's phase deadline; checked by diff_main,
                                 not diff_lts */
} fw_replacement;

extern const fw_replacement fw_replacements[];
extern const size_t fw_replacement_count;

/* Binds b to m. False when the data image does not build (the reason is printed). */
bool fw_bridge_init(fw_bridge *b, machine *m);
const fw_replacement *fw_replacement_find(const char *name);
/* Hooks every replacement into m with user b, or only the one named only (NULL = all). False if only names
   none or a hook cannot be added. */
bool fw_install(machine *m, fw_bridge *b, const char *only);
/* Hooks fn with user at r's address: machine_hook_ramcode for a ramcode row, else machine_hook. */
bool fw_hook(machine *m, const fw_replacement *r, machine_hook_fn fn, void *user);

#endif
