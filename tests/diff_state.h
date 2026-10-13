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

/* The serial routines no corpus text enters (M4.6a spec 4.4: the machine never raises the host line P0.2, so no
   reply is sent): diff_lts sees no call of them and golden_hybrid runs no thunk of them alone; diff_serial's probes
   compare them, each one the routine of a probe. */
extern const char *const diff_probed[];
extern const size_t diff_probed_count;
bool diff_is_probed(const char *name);

#endif
