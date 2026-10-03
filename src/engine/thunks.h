#ifndef ENGINE_THUNKS_H
#define ENGINE_THUNKS_H
/* The replacement table: ROM routines that run as C in the hybrid engine (M4.0 spec section 3.2). Each thunk
   maps the routine's registers to its C function, emulates the RTS and takes no emulated time. */
#include "machine/machine.h"
#include "firmware/fw.h"

#define FW_ROM_CALL_BUDGET 10000000ULL   /* cycles a ROM routine called from C may take (spec 3.3) */

/* The registers and flags a routine's callers use: the thunk sets them, the differential test compares them. */
enum { FW_OUT_A = 0x01, FW_OUT_X = 0x02, FW_OUT_Y = 0x04, FW_OUT_Z = 0x08 };

/* The firmware context of one machine: its RAM, the converted data image, the emulator as host. */
typedef struct fw_bridge { machine *m; fw f; fw_host host; uint64_t thunk_calls; } fw_bridge;   /* thunk_calls: thunks run so far */

typedef struct {
    const char *name;
    uint16_t addr;
    uint8_t outputs;          /* FW_OUT_* */
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
} fw_replacement;

extern const fw_replacement fw_replacements[];
extern const size_t fw_replacement_count;

/* Binds b to m. False when the data image does not build (the reason is printed). */
bool fw_bridge_init(fw_bridge *b, machine *m);
const fw_replacement *fw_replacement_find(const char *name);
/* Hooks every replacement into m with user b, or only the one named only (NULL = all). False if only names
   none or a hook cannot be added. */
bool fw_install(machine *m, fw_bridge *b, const char *only);

#endif
