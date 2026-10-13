#ifndef DIFF_STATE_H
#define DIFF_STATE_H
/* The exit state the live differential tests compare (M4.0 spec 5.1, M4.6a spec 4.1): diff_lts and diff_serial. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "machine/machine.h"

/* True when a (the ROM copy) and b (the C copy) agree on the exit PC and SP, the outputs declared (FW_OUT_* of
   engine/thunks.h), the SFR model state and all RAM except the SFR bytes and the dead stack below a's exit SP. The
   SFR state is these m50734 fields: port_latch[0-3] and port_dir[0-3] (P0-P3), icr1, icr3 and the enable bits
   (0, 2, 4) of icr2, uart_ctrl, uart_rx and uart_rx_full (UART_STATUS bit 0), uart_tx_count and uart_tx_last,
   p23_func, and timer_pre, timer_reload and timer_running of the three timers (M4.6b spec 4.1). icr2's request bits
   (1, 3, 5) and timer_count are left out: the timers set and move them as cycles pass (m50734_tick), and the C takes
   none. The DSP outputs too: port_writes, raw_pos and raw_buf, dsp_resets and dsp_reset_level, frames_produced.
   Otherwise false, with the first difference in why. */
bool diff_same(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n);
/* The SFR model state of diff_same alone (diff_main, M4.6c2 spec 4.1): true when a (the ROM's) and b (the C's) agree,
   else false with the first difference in why. */
bool diff_same_sfr(const m50734 *a, const m50734 *b, char *why, size_t n);

/* A routine whose copy reset the stack (bank_enter's LDX #$FF / TXS) is compared where bank_enter jumps, at the bank
   entry BANK_ENTRY_WARM $FFD4 or BANK_ENTRY_COLD $FFD0 (M4.6c1 spec 4.1). diff_run_to_bank_entry steps m (at most
   DIFF_ENTRY_STEPS instructions) until its PC is one of them: at once when the C left it there (fw_enter); between
   the TXS and the jump the ROM only reads (warm) or clears $01-$64 (cold). False on a fault or when it does not get
   there. */
#define DIFF_ENTRY_STEPS 1000
bool diff_run_to_bank_entry(machine *m);
/* At a bank entry: diff_same with A and X, and the I flag (M4.6c1 spec 4.1). Y and the other flags are not compared:
   the warm and cold re-entries ($A66B, $A665) write each before reading it. */
bool diff_same_at_entry(const machine *a, const machine *b, char *why, size_t n);

/* The serial routines no corpus text enters (M4.6a spec 4.4: the machine never raises the host line P0.2, so no
   reply is sent): diff_lts sees no call of them and golden_hybrid runs no thunk of them alone; diff_serial's probes
   compare them, each one the routine of a probe. */
extern const char *const diff_probed[];
extern const size_t diff_probed_count;
bool diff_is_probed(const char *name);

/* The main loop's rows that wait for input or never return (M4.6c2 spec 4.1): the entries and the waits rows of boot,
   which diff_main compares end to end, not diff_lts or diff_play. */
bool diff_is_main_row(const char *name);
/* The rows no corpus text enters and no probe compares (M4.6c2 spec 4.1, 4.4): the unreachable cmd_say_phonetic and
   isr_unused, pinned by unit tests, and the cold re-entry, which diff_main's injected run compares. golden_hybrid
   runs no thunk of them alone. */
bool diff_is_unreached(const char *name);

#endif
