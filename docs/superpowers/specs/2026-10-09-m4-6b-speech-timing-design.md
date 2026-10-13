# M4.6b — Speech timing and the DSP in C (the timer ISR, the frame waits, volume, the parallel handshake) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-09-m4-6b-speech-timing.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 15): §1.3 (the peripherals), §4 (the machine model, frame capture at `$B306`, serial delivery at the idle WIT), §6 step 3–4, §8 milestones 4 and 5.
- `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md`: SFR access through the host, the wait seam, interrupt handlers as replacements (`isr` rows), `diff_lts`'s SFR comparison, `diff_serial` (§3, §4, §6 "As built").
- `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`: `par_play`, `boot_loop_play`, `waits` rows, `diff_play` and its injections, the bridge deadline.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, `diff_lts` (§8 "As built").

**User decisions:**
- (2026-10-08) M4.6 is three plans: M4.6a serial (done), **M4.6b** speech timing and the DSP (this spec), M4.6c boot, the main loop and the banks. Hardware waits are semantic host calls (`fw_host.wait`); the hybrid host performs them on the emulated CPU, a native host natively. Paths no corpus text reaches are compared through probes injected into both copies.
- (2026-10-09) **Polling loops wait for an interrupt.** Once `isr_par_ready` is C, the C's polling loops (`par_play`'s top loop and frame-flag wait, `isr_wait_frame`) no longer pass emulated time through ROM calls. Each becomes "test; if not yet, `fw_wait(FW_WAIT_INTERRUPT)`; test again". The hybrid host idles the emulated CPU as at a WIT until an interrupt has been handled; a native host sleeps until the next timer tick or serial byte.
- (2026-10-09) **Frames leave the C timer ISR through a host event.** At the point of `$B306` the C `isr_timer_dsp` calls `fw_host.frame_done`; the hybrid host captures the frame through a new `machine_capture_frame`, a native host hands it to the synth. The DSP port bytes go through `fw_host.dsp_write`.

## 1. Goal

Port the firmware's speech timing to C: the 10 kHz timer interrupt that feeds the DSP, the start and stop of DSP output, the frame waits, the volume and the parallel-port handshake (11 hooked routines, 257 bytes). After M4.6b the only ROM code a hybrid run executes is boot, the main loop, `isr_idle`, the banks, `cmd_run_next` (M4.6c) and the host's wait loops (`ser_tx_wait` `$C500`, the two timer-sync loops of `isr_timer_start`).

Success: the goldens stay byte-identical with all 100 replacements and with each alone; `diff_lts`, `diff_play` and `diff_serial` report 0 mismatches; `--engine hybrid` equals `--engine rom` on every corpus text.

## 2. The subsystem (all main ROM)

### 2.1 Ported to C (11 hooked routines, 257 bytes)

| Routine | Addr | Bytes | What it does |
|---|---|---|---|
| `isr_set_volume` | `$ACE8` | 18 | A = volume 0–15: P3 bits 3–6 = the inverted level (via `util_shl3`, scratch `$75`), the other P3 bits kept |
| `isr_timer_dsp` | `$B2D9` | 74 | INT2 (timer 3, `$FFF8`) ISR: `CLB5 ICR2`; every 10th tick writes the next word of the frame at `isr_play_ptr` to `DSP_DATA` (`$8000`, two bytes); after 10 words, at a frame boundary with `isr_frame_flag` clear, sets the flag and swaps the ping-pong buffers (`isr_swap_phase` 2 → 1 → 2: `$A5`/`$B9` exchanged between `isr_play_ptr` and `isr_fill_ptr`); with the flag still set the frame repeats |
| `isr_speech_start` | `$B323` | 51 | `frm_source_index`/`frm_filter_index` from the @$ bits of `cmd_source_prosody`; `isr_tick_div` = `isr_words_left` = 10, `isr_word_ofs` = `isr_frame_flag` = 0; `isr_dsp_reset`, `isr_timer_start`, `isr_wait_frames` with X = 1 |
| `isr_wait_frames` | `$B356` | 7 | X frame boundaries (`isr_wait_frame`); returns X = 0 |
| `isr_timer_start` | `$B35D` | 46 | PHP/SEI; prescalers 0; `isr_tick_div` decremented twice; `SEB0 P3`; T2 reload 4; clears ICR2 bits 3 and 2; **waits for ICR2 bit 3** (`$B372`); T3 reload `$28`; clears ICR2 bits 5 and 4; `P23_FUNC` = 1; **waits for ICR2 bit 5** (`$B37F`); T3 reload `$31`; ICR2 = `$10`; PLP |
| `isr_dsp_reset` | `$B38B` | 13 | PHP/SEI; P2.0 low, high, low (three read-modify-writes); PLP |
| `isr_speech_stop` | `$B398` | 12 | `P23_FUNC` = 0, ICR2 = 0, `isr_dsp_reset`, `CLB0 boot_speaking` |
| `isr_wait_frame` | `$B465` | 7 | until `isr_frame_flag` bit 0 is set (the timer ISR's frame boundary); then the whole byte = 0 |
| `isr_volume_half` | `$B46C` | 7 | `isr_set_volume` with `cmd_volume` >> 1 |
| `isr_par_ready` | `$C5D6` | 8 | parallel mode only (`boot_dip` bit 0): `SEB3 P0`; A preserved (PHA, then the shared PLA/RTS at `$C5EA`) |
| `isr_par_hold` | `$C5DE` | 14 | parallel mode only: PHP/SEI, `CLB3 P0`, `CLB1 ICR1` (a pending `_INT1` request), PLP; A preserved |

Files: `src/firmware/isr_dsp.c`/`.h` (the first nine), `isr_par_ready` and `isr_par_hold` in `src/firmware/isr_serial.c` (the parallel host handshake, beside the M4.6a host-line code). The ROM addresses move into a new `src/firmware/isr_addr.h`, out of `par_addr.h` and `tn_convert.h`. No new converted data (`firmware_data` stays 48,512 bytes).

### 2.2 C calls C

- `boot_loop_play`'s body (`par_play.c`, `$B2B3`–`$B2D8`) calls the C `isr_par_hold`, `isr_speech_start`, `isr_volume_half`, `isr_wait_frame`, `isr_set_volume`, `isr_speech_stop`.
- `par_play` calls the C `isr_wait_frame` (`$B8D2`) and `isr_par_ready`.
- `cmd_play_event.c` (`cmd_ev_set_volume`) calls the C `isr_set_volume`.
- `fw_par_ready` (`fw_ops.h`; the converter, prosody, `cmd_scan`, `par_play`) calls the C `isr_par_ready`.
- `isr_speech_start` → `isr_dsp_reset`, `isr_timer_start`, `isr_wait_frames` → `isr_wait_frame`; `isr_speech_stop` → `isr_dsp_reset`; `isr_volume_half` → `isr_set_volume` → the C `util_shl3`.

No `fw_rom_call` of an `isr_*` routine is left in `src/firmware`.

### 2.3 Polling loops

Every loop that waits for a flag an interrupt sets becomes: test the flag; if not yet, `fw_wait(f, FW_WAIT_INTERRUPT, at)`; test again. Where the ROM calls `isr_par_ready` on every pass, the C calls it on every pass too.
- `par_play`'s top loop, `$B877`–`$B8AE` (UART status and the reply, `isr_par_ready`, the abort test, the frame-flag test, `isr_par_ready`, back to the top): the wait at the back edge (`at` = `$B877`).
- `par_play`'s frame-flag wait, `$B8C9`–`$B8CC`, and the C helper that serves it and `boot_loop_play` (`wait_frame_flag`): `at` = `$B8C9`; the rest-frame wait `$B8E6`–`$B8E9` is the same loop shape and waits with `at` = `$B8E6`. *(Corrected 2026-10-09 from the plan's proof.)*
- `isr_wait_frame`, `$B465`: `at` = `$B465`.

### 2.4 Hooks (11 new replacements; 100 in all, 101 `golden_hybrid` modes)

- `isr_timer_dsp` is an `isr` row: no outputs declared, exit by `machine_return_interrupt` (M4.6a §3.2); `diff_lts` compares A, X and Y for it all the same (M4.6a As built).
- `isr_speech_start`, `isr_wait_frames` and `isr_wait_frame` are `waits` rows (they wait for the timer ISR): `diff_play` and the goldens compare them, not `diff_lts`.
- The other seven declare the registers their ROM callers read, each with a comment checked in the dumps (the plan's "ROM facts checked"; `isr_par_ready`'s and `isr_par_hold`'s callers include ROM boot code, which reads A).

### 2.5 SFRs and outputs the C touches

Through the host only (`fw_sfr*`, M4.6a §3.1): P0 (`$F6`), P2 (`$F0`), P3 (`$EE`), `P23_FUNC` (`$ED`), the timer prescalers `T2_PRE` (`$DE`) and `T3_PRE` (`$E0`), the reloads `T2_RELOAD` (`$DF`) and `T3_RELOAD` (`$E1`), ICR1 (`$FF`), ICR2 (`$FE`). Through the new host calls: the DSP port (`dsp_write`) and the frame event (`frame_done`).

### 2.6 Quirks kept

- `isr_timer_start` decrements `isr_tick_div` twice (10 → 8) before the timers start.
- `isr_set_volume` writes the scratch byte `$75` and returns A = the value written to P3.
- `isr_timer_dsp` reads the frame through `(isr_play_ptr),Y` with Y = `isr_word_ofs`, 8-bit `INY`; at a boundary it writes `isr_frame_flag` = 1 with `LDM` (the whole byte); `isr_wait_frame` clears the whole byte.
- `isr_par_ready` and `isr_par_hold` leave A as it came (PLA), in both modes.
- Every RAM write, the scratch ones included, and every SFR access in the ROM's order, each `SEB`/`CLB` as a read then a write (M4.6a §2.6).

## 3. Infrastructure

### 3.1 The DSP port and the frame event

`fw_host` gains:
- `void (*dsp_write)(void *ctx, uint8_t byte)`, through `fw_dsp_write(f, v)`: the two `STA DSP_DATA` (`$B2E7`, `$B2ED`). The hybrid host writes the byte to `$8000` on the bus, so the machine's port-write count, raw frame stream and reset grouping work as now.
- `void (*frame_done)(void *ctx)`, through `fw_frame_done(f)`: the C `isr_timer_dsp` calls it where the ROM reaches `$B306`, before the `LDM` writes there, which is exactly where the machine captures a frame today. The hybrid host calls `machine_capture_frame(m)`. The C copy never executes `$B306`, so no frame is captured twice.

Both helpers fault without a host or without their function, as the SFR helpers do: `fw_dsp_write` at `$8000`, `fw_frame_done` at `$B306`. *(Corrected 2026-10-09 from the plan's proof.)*

### 3.2 The wait seam

`fw_wait(f, kind, at)` gains `at`, the ROM address of the loop the wait stands for; a wait without a host or its `wait` function faults there. (This replaces M4.6a's single fault address, a deferred item; the two calls in `ser.c` pass `$C500`.) The host's `wait` function receives `at` too. *(Corrected 2026-10-09 from the plan's proof.)*

| Kind | Stands for | Hybrid host | Native host |
|---|---|---|---|
| `FW_WAIT_UART_TX` | `ser_tx_wait` `$C500` | runs `$C500` (M4.6a, unchanged) | waits until the transmitter is free |
| `FW_WAIT_INTERRUPT` | a polling loop on a flag an interrupt sets (§2.3) | `machine_wait_interrupt(m)` | sleeps until the next timer tick or serial byte |
| `FW_WAIT_TIMER2` | `$B372` `BBC3 ICR2` (inside SEI) | `machine_run_loop(m, $B372, $B375)` | nothing |
| `FW_WAIT_TIMER3` | `$B37F` `BBC5 ICR2` (inside SEI) | `machine_run_loop(m, $B37F, $B382)` | nothing |

Every wait is bounded by the bridge deadline (M4.4b): a wait that never ends faults.

### 3.3 Machine API additions (no model change)

- `machine_capture_frame(m)`: what the step loop does at `$B306` (the 20 bytes at the `$2C/$2D` pointer, `frames_produced`, the frame callback).
- `machine_wait_interrupt(m, max_cycles)`: as a WIT at the current PC, which is the hook's PC and never the idle WIT `$C602`, so the machine delivers no serial byte (parent spec §4: bytes arrive only at the idle WIT). The CPU wakes on an IRQ line, takes any unmasked interrupt (ROM or C), and the call returns when the PC and SP are back where they were. If a line wakes the CPU while I is set (no interrupt taken), it returns false at once. *(Corrected 2026-10-09 from the plan's proof.)*
- `machine_run_loop(m, loop, exit, max_cycles)`: saves PC and P, sets I (both loops run inside PHP/SEI), steps from `loop` until the PC reaches `exit`, restores PC and P. No stack frame.

### 3.4 P2 and the low window

`fw_wr_sfr8`, `fw_sfr_seb` and `fw_sfr_clb` on P2 (`$F0`) set `f->low_window` from bit 7 of the value written, so the cache follows the machine (M4.6a deferred item). `isr_dsp_reset` touches only bit 0, so the window does not change here.

### 3.5 PHP/SEI/PLP

In `isr_timer_start`, `isr_dsp_reset` and `isr_par_hold` they are comments: the C takes no emulated time and cannot be interrupted. The one place the mask matters is the two timer waits, which `machine_run_loop` runs masked.

## 4. Verification

### 4.1 `diff_lts`

- Compares the eight routines that do not wait: `isr_set_volume`, `isr_volume_half`, `isr_par_ready`, `isr_par_hold`, `isr_dsp_reset`, `isr_speech_stop`, `isr_timer_start` (its timer waits end with interrupts held: the request bits are set by the clock) and `isr_timer_dsp` (an `isr` row). Every call of these eight (`k_every_call`; `diff_lts` samples the other routines as before): 2,589,942 of `isr_timer_dsp` and 7,959,385 of `isr_par_ready` (the ROM's polling loops call it on every pass) on the corpus; `diff_lts` takes 179 s in the proof, under the 5-minute gate. *(Corrected 2026-10-09 from the plan's proof.)*
- `diff_same` compares more machine state: `p23_func`; each timer's prescaler, reload and running flag; the DSP outputs (`port_writes`, `dsp_resets`, `dsp_reset_level`, `raw_pos`, `raw_buf`, `frames_produced`). Not the timers' remaining counts, which move with the C's zero time as ICR2's request bits do.
- `DIFF_MAX` grows to 104 (100 replacements).

### 4.2 `diff_play`

Its two injection points, `$B306` and `isr_par_ready`, both become C. The reply injection moves to the frame event, which both copies reach (the ROM at `$B306`, the C through `frame_done`). The abort (mute) injection is made at the timer interrupt's entry two ticks after the second frame event: made at the frame event itself it gave 4 mismatches, because the ROM copy can be between its abort test (`$B8A2`) and its frame-flag test (`$B8A8`) while the C copy is waiting, a race the ROM itself has; two ticks later both main programs are past it. *(Corrected 2026-10-09 from the plan's proof.)* The comparison is made at every frame event, as at every swap today: both main programs are waiting, and the ISR has made the same RAM writes in both copies by then. A ROM-against-ROM self-check proves each injection still reaches its target branch (`$B8A5` abort, `$B89C` reply). It watches `par_play` and `boot_loop_play`, and also the three `waits` rows (`isr_speech_start`, `isr_wait_frames`, `isr_wait_frame`), armed at their `JSR` sites. *(Corrected 2026-10-09 from the plan's proof.)*

### 4.3 `diff_serial`

Keeps its name and machinery, and gains probes, proved ROM against ROM before the C exists:
- `isr_par_ready` and `isr_par_hold` with parallel mode injected (`boot_dip` bit 0), as synthetic calls from real states;
- the timer ISR (entered at `$B2D9` as the CPU enters an interrupt, as M4.6a's ISR probes) on the paths the corpus may not reach, e.g. a frame boundary while `isr_frame_flag` is still set (the frame repeats). The branch probe (§4.5) decides which are needed. The proof needed three new probes (the repeated frame at `$B300`, and parallel mode in `isr_par_ready` and `isr_par_hold`): 28 in all. `isr_wait_frames`' `BNE` taken (`$B35A`, X ≥ 2) has no caller that passes X ≥ 2 and is pinned by the unit test `test_wait_frames_two`. *(Corrected 2026-10-09 from the plan's proof.)*

### 4.4 `golden_hybrid`, `test_timing_invariance`, the CLI

`golden_hybrid` runs 48 files × 101 modes (all, then each routine alone); the goldens stay byte-identical. `test_timing_invariance` passes on both engines; the hybrid cycle counts drop again (the timer ISR takes no emulated time, and the polls idle). `--engine hybrid` equals `--engine rom` on all 47 corpus texts.

### 4.5 Coverage

Each conditional branch of the 11 routines is seen both ways on the corpus or under a `diff_serial` probe (a throwaway branch probe, recorded in the plan, not committed). A branch no shared injection can reach is pinned by a named unit test.

### 4.6 Unit tests (new suite `tests/test_fw_dsp.c`, `suite_fw_dsp`)

A stub host logs the SFR reads and writes, the waits (kind and `at`), the DSP writes and the frame events, in order.
- `isr_timer_dsp`: a tick not due; a word written; a frame boundary in each swap phase; a repeated frame.
- `isr_set_volume`: all 16 levels; `isr_volume_half`.
- `isr_par_ready`, `isr_par_hold`: both modes.
- `isr_timer_start`: the whole SFR order around its two waits; `isr_dsp_reset`, `isr_speech_start`, `isr_speech_stop`, `isr_wait_frame(s)` with a scripted flag.
- The P2 rule (§3.4); the new helpers' faults.
- In `tests/test_machine_hooks.c`: `machine_capture_frame` equals a capture at `$B306`; `machine_run_loop` restores PC and P; `machine_wait_interrupt` returns after a ROM and after a C interrupt, delivers no serial byte, and is bounded.

### 4.7 Done criteria

1. 11 routines hooked (100 replacements); `isr_timer_dsp` an `isr` row; the C callers call the C (§2.2).
2. `diff_lts`, `diff_play` and `diff_serial`: 0 mismatches; every probe reaches its target at every call.
3. Every conditional branch of the 11 routines seen both ways or pinned by a unit test.
4. `golden_hybrid` 48 × 101, goldens byte-identical; `test_timing_invariance` on both engines; `suite_fw_dsp` passes.
5. `--engine hybrid` equals `--engine rom` on all 47 corpus texts.
6. `grep -rn "fw_rom_call" src/firmware` shows no `ROM_ISR_*` call.
7. Parent spec rev 16, this spec's status and "As built", the README.

### 4.8 Task outline (for the plan)

1. Infrastructure: `dsp_write`/`frame_done`, the wait kinds with `at`, the three machine calls, the P2 rule, `diff_lts`'s extra state, `isr_addr.h`, `suite_fw_dsp`; `diff_lts` prints exactly what it did before.
2. The tests before the C: `diff_play`'s injections at the frame event and the new `diff_serial` probes, both proved ROM against ROM.
3. Volume and the parallel handshake: `isr_set_volume`, `isr_volume_half`, `isr_par_ready`, `isr_par_hold`, their callers switched, and the polling loops on `FW_WAIT_INTERRUPT` (in the same task: once `isr_par_ready` is C, the polls need the wait).
4. DSP control: `isr_dsp_reset`, `isr_timer_start`, `isr_speech_stop`, `isr_speech_start`, `isr_wait_frames`, `isr_wait_frame`.
5. The timer ISR: `isr_timer_dsp`; `diff_serial` requires the C; the branch probe; the CLI check.
6. Docs: parent spec rev 16, this spec's status and "As built", the README.

## 5. Out of scope

- M4.6c: boot, the main loop, `isr_idle` (its WIT is the input idle, where the machine delivers serial bytes: a wait kind of its own), the banks and their RAM-resident code, `cmd_run_next`, the remaining data.
- M5: the native host (natively performed waits, the frame event feeding the synth).
- No machine-model change: the three machine calls are API additions.

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **The host calls** (§3.1): the hybrid `host_dsp_write` (`thunks.c`) is one bus write of `APOLLO_DSP_PORT` ($8000) and `host_frame_done` is `machine_capture_frame`; `test_host_dsp` writes two bytes (`port_writes` 2, `raw_pos` 2, the bytes in `raw_buf`) and makes one event (the 20 bytes at the `$2C/$2D` pointer to the frame callback, `frames_produced` 1, no unmodelled access). The C `isr_timer_dsp` makes the event after the boundary's resets (`isr_tick_div` 10 at $B2F3, `isr_words_left` 10 at $B2FA, `isr_word_ofs` 0 at $B2FD) and before $B306's `LDM #1`: `test_timer_swap` reads `isr_tick_div` 10, `isr_word_ofs` 0, `isr_words_left` 10 and the flag still 0 at the event. `isr_addr.h` names the point `ROM_ISR_SWAP_BUFFERS`, `fw.h` `FW_FRAME_EVENT` (where `fw_frame_done` faults), both $B306.
- **The wait seam** (§3.2): `fw_wait_kind` is `FW_WAIT_UART_TX` = 0, `FW_WAIT_INTERRUPT`, `FW_WAIT_TIMER2`, `FW_WAIT_TIMER3`; `fw.h` names the timer loops `FW_WAIT_TIMER2_LOOP` $B372 (`_EXIT` $B375) and `FW_WAIT_TIMER3_LOOP` $B37F (`_EXIT` $B382). The `at`s the C passes: $C500 (`FW_WAIT_UART_TX_LOOP`, `ser.c`), $B877 (`ROM_PAR_PLAY`), $B8C9 and $B8E6 (`par_play.c`'s `PAR_WAIT_LAST`, `PAR_WAIT_REST`), $B465 (`ROM_ISR_WAIT_FRAME`), $B372 and $B37F. The hybrid `host_wait` ignores `at` and runs the kind's own loop. For the three new kinds the machine call gets `FW_ROM_CALL_BUDGET` cycles or the cycles left before the bridge deadline, whichever is fewer, and once the deadline has passed the wait fails without a step (as `host_rom_call`); after the call the bridge reads the window back from P2 (`window_on`), since a handler ran. `test_host_wait` runs both timer loops (synthetic copies at $B372 and $B37F) until the request bit, an interrupt wait through a synthetic handler at `$FFF8`'s target with the PC and SP back, and an interrupt wait that no interrupt ends: a fault at $B877 within 8 cycles of a deadline 500 cycles on.
- **The machine calls** (§3.3): `machine_capture_frame` is the step loop's `capture_frame` made public (`test_capture_frame`: the same 20 bytes and count as a step at $B306, nothing executed). `machine_wait_interrupt` sets `cpu.waiting` and runs `prepare`/`exec` until the CPU is no longer waiting with the PC and SP as at the call; before each step a waiting CPU with an IRQ line asserted and I set ends it at once (false, no step); on any false (that, the budget, a fault) `waiting` is cleared. `machine_run_loop` saves the PC and P, sets I, runs `machine_step` from `loop` until the PC is `exit` and puts the PC and P back, whether or not the loop ended. `test_run_loop`: a synthetic `BBC3 ICR2` loop at $C700 with timer 2 at 400 cycles and its interrupt enabled ends in 400–412 cycles with no interrupt taken (SP $FF) and the PC and P back; a loop nothing ends fails after its 1,000 cycles, the PC and P back, no machine fault. `test_wait_interrupt`: the handler `CLB3 ICR2 / INC $20 / RTI` at $C600 through $FFF8, then a hook in its place, each taken once with the PC $9000 and SP $FF back; a queued serial byte stays queued; with no interrupt, false after the 1,000-cycle budget (within 8 cycles); with I set and the request pending, false with no cycle spent.
- **The P2 rule** (§3.4): `fw.c`'s static `p2_written` sets `f->low_window` from bit 7 of the value written, after the host's write, in `fw_wr_sfr8` and in the `SEB`/`CLB` read-modify-write; a P2 write that faults writes nothing and leaves the cache (`test_p2_window`, which also checks that P3 writes leave it).
- **The thunks** (§2.4): all ten subroutine rows declare A, X and Y, the three `waits` rows included (`diff_play` compares the exit A, X and Y), each with a comment saying where the registers go next: a caller that reads them, or one that returns them or hands them to a routine whose row declares them; none declares a flag. The `isr` row is `{ "isr_timer_dsp", 0xB2D9, 0, t_isr_timer_dsp, false, true }` (`ISR_THUNK`); its thunk sets A, X and Y from the C's `r`, which comes back as it went. `isr_par_ready` and `isr_par_hold` use M4.6a's `ser_regs`, the other nine their own `_regs`. In "all" mode only four of the 11 are entered from ROM: `isr_timer_dsp` (its vector), `isr_set_volume` (by `boot_reset` $A642), `isr_par_ready` and `isr_par_hold` (by `boot_reset`, `boot_get_clause` and `boot_queue_string`); every caller of the other seven is C. `util_shl3`'s hook no longer fires in "all" mode (both its callers, `cmd_ev_sound_start` and `isr_set_volume`, are C); its comment says so.
- **The C layout**:
  - `isr_dsp.c`: the nine routines of `isr_dsp.h` and one static helper, `asl_rol` (`ASL A; ROL addr`), for `isr_speech_start`'s four shifts; its own names: `P3_KEPT` $87, `P3_TICK`, `P2_DSP_RESET`, `ICR2_T2_EN`/`_REQ` (bits 2, 3), `ICR2_T3_EN`/`_REQ` (bits 4, 5), `ICR2_T3_ONLY` $10, `T2_RELOAD_SYNC` 4, `T3_RELOAD_SYNC` $28, `T3_RELOAD_TICK` $31 (timer 3's period 4 × 1 × 50 = 200 cycles, `m50734.c`: 10 kHz at 2 MHz), `ISR_TICKS` and `ISR_WORDS` 10, `ISR_FRAME_A_LO` $A5, `ISR_FRAME_B_LO` $B9, `ISR_SWAP_PHASES` 2. `isr_dsp.h` names the SFRs (`SFR_T2_PRE` $DE … `SFR_ICR2` $FE);
  - `isr_serial.c`: `isr_par_ready` and `isr_par_hold`, no helper, with M4.6a's `SFR_P0`, `SFR_ICR1`, `P0_READY` and `ICR1_INT1_REQ`;
  - `isr_addr.h`: the 11 entry addresses with their callers, and `ROM_ISR_SWAP_BUFFERS`; `ROM_ISR_SET_VOLUME`, `_SPEECH_START`, `_SPEECH_STOP`, `_WAIT_FRAME`, `_VOLUME_HALF` and `_PAR_HOLD` moved there from `par_addr.h`, `ROM_ISR_PAR_READY` from `tn_convert.h`;
  - `fw.h`/`fw.c`: the wait kinds and loop addresses, `FW_DSP_DATA`, `FW_FRAME_EVENT`, `fw_dsp_write`, `fw_frame_done`, `fw_wait` with `at`, `p2_written`; `fw_ops.h`'s `fw_par_ready` calls the C (it includes `isr_serial.h`, no longer `tn_convert.h`); `machine.h`/`machine.c`: the three calls only;
  - the callers (sharper than §2.2): `par_play` and the prosody passes (`pros_append_segment`, `pros_phrase_gen`, `pros_mark_phrases`, `pros_mark_stress`) call `isr_par_ready` through `fw_par_ready`; `tn_convert_clause` ($B1F6; its static `rom_call_x` is gone) and `cmd_scan_clause` ($AD09) call it directly with registers of their own; `cmd_ev_set_volume` calls `isr_set_volume` directly.
- **The polling loops** (§2.3), as built:
  - `par_play`'s top loop: with the flag clear at $B8A8, `isr_par_ready` ($B8AB), `fw_wait(FW_WAIT_INTERRUPT, $B877)`, then the next pass, which first returns if a call or a wait has faulted (without a trap);
  - `wait_frame_flag(f, r, at)`, static in `par_play.c`, serves `par_play`'s two frame-flag loops only, $B8C9 (`last_frames`) and $B8E6 (`rest_frames`): `isr_par_ready`, the flag test, and while bit 0 is clear (and no fault) the wait at `at`, then `isr_par_ready` again. `boot_loop_play`'s frame waits ($B2BF, $B2CD) and `par_play`'s at $B8D2 are calls of the C `isr_wait_frame`;
  - `isr_wait_frame` waits at $B465 between tests, and a failed wait ends it without the clear (`test_wait_frame`: one wait, a fault at $B465). With the flag already set it makes no wait, as at $B8D2 reached from the last frames, where the flag has just been seen.
- **`isr_timer_dsp`** (sharper than §2.1 and §2.6): the `LDM`s at $B30D–$B31C write the pointers' low bytes only (`test_timer_swap`: `isr_fill_ptr`'s high byte $55 kept); a repeated frame resets the counters as any boundary does ($B2FA, $B2FD) and leaves the flag byte as it was (`test_timer_repeat`: $03 stays $03, no frame event, the phase and the pointers unchanged); the two DSP bytes are read at Y and Y + 1 in 8 bits (`test_timer_word`: Y $FE reads $16FE and $16FF from a pointer $1600, and `isr_word_ofs` becomes 0).
- **Quirks pinned** (sharper than §2.6): `isr_timer_start`'s `DEC`s wrap (1 → $FF: `test_timer_start`); `isr_wait_frames` with X = 0 waits 256 frames (`test_wait_frames_two`); `isr_set_volume` ignores A's high nibble ($F3 is level 3: `test_set_volume`).
- **`diff_lts`** (§4.1): it prints 95 lines; those of the eight `k_every_call` routines (matched by name in `install`; the others are sampled as before): `isr_set_volume` 772, `isr_volume_half` 360, `isr_par_ready` 7,959,385, `isr_par_hold` 3,318, `isr_timer_start` 180, `isr_dsp_reset` 360, `isr_speech_stop` 180, `isr_timer_dsp` 2,589,942 calls checked; `1 mid-clause flush calls skipped`, `2 unwound calls compared`, 0 mismatches. `diff_same` compares the added state in `sfr_same` (`p23_func`; `timer_pre`, `timer_reload`, `timer_running` of the three timers) and a new `dsp_same` (`port_writes`, `raw_pos`, `raw_buf`, `dsp_resets`, `dsp_reset_level`, `frames_produced`); not `timer_count`, nor `raw_frames` and `raw_discarded`, which follow from the others. `diff_lts` alone took 164 s (179 s in the proof); the full suite 704 s.
- **`diff_play`** (§4.2), as built: the hook `on_timer` at $B2D9 is on both copies: it counts down the mute (made due at the second frame event, `INJECT_SWAP` 1, and set `ABORT_TICKS` = 2 entries later) and, in copy B, runs the C `isr_timer_dsp` on copy B's bridge (`g_bridge`, the one the watched thunk runs on) when copy B runs a replacement; the self-check keeps the ROM handler in both. The reply injection is made in the frame callback (`on_swap`) at the second event. The frame waits are armed by hooks on their `JSR`s (`FRAMES_CALL_SITE` $B352; `k_frame_call_sites` $B2BF, $B2CD, $B8D2; and `isr_wait_frames`' entry $B356, at every pass); a call gets the injections only when its ROM copy swaps at least twice, which no frame wait does. The run: the self-check on hello (`par_play` 1 call, `boot_loop_play` 2, 2 injected, $B8A5 and $B89C reached 2 times each; `isr_speech_start` 1, `isr_wait_frames` 1, `isr_wait_frame` 4), then `boot_loop_play` 179 calls (48 with both injections), `isr_speech_start` 126, `isr_wait_frames` 126, `isr_wait_frame` 504, `par_play` 126 (48); $B8A5 reached in 96 calls, $B89C in 96; 7,770,113 timer interrupts run in C in copy B; 0 mismatches (28 s).
- **`diff_serial`** (§4.3), as built: the three new probes are at the `isr_int1` point. `timer_repeat` (`isr_timer_dsp`, an `isr` probe): ICR2 bits 5 and 4 set, `isr_tick_div` 1, `isr_words_left` 1, `isr_word_ofs` 18, `isr_frame_flag` bit 0 set, the PC from `vec_int2_timers` $FFF8; target $B300 → $B303. `par_ready` and `par_hold` (subroutine probes): `boot_dip` bit 0 and `port_dir[0]` = $08, and for `par_hold` ICR1 bit 1; targets $C5D7 → $C5DA and $C5DF → $C5E2. Each: 23 calls in the self-check (ROM), 2,649 on the corpus and the boot run (C), every target reached at every call, 0 mismatches; 28 probes in all.
- **`tests/test_fw_dsp.c`** (§4.6): its stub host logs every SFR read and write, wait (kind and `at`), DSP byte and frame event in order (`check_log`), can set the flag at every n-th interrupt wait (`g_flag_every`) and keeps the four counters at the frame event (`g_at_event`). Its tests: `test_dsp_write`, `test_frame_done`, `test_wait_kinds` (the three kinds logged with their `at`; a failed wait, no `wait` or no host: a fault at that `at`), `test_p2_window`, `test_rows` (the 11 rows), `test_set_volume` (16 levels and $F3, P3 $A5), `test_volume_half` (11 → 5; $FF → $7F, level 0), `test_par_handshake` (`boot_dip` $00 and $FE: no host call; bit 0 set: `SEB3 P0`, then `CLB3 P0` and `CLB1 ICR1`; A, X and Y kept), `test_dsp_reset` (the six accesses, the window kept), `test_timer_start` (the 17 SFR accesses and the two waits in order, `isr_tick_div` 10 → 8), `test_speech_stop`, `test_speech_start` ($B5: indexes 5 and 1; one wait at $B465; A 0, X 0, Y kept), `test_wait_frame`, `test_wait_frames_two`, `test_timer_word`, `test_timer_swap` (both phases), `test_timer_repeat`.
- **The other unit tests** (§4.6):
  - `tests/test_machine_hooks.c`: beyond the tests above, `test_waits_thunk_deadline` sees the outermost deadline at an event handler run in ROM (`util_shl3` $C657 as the handler of an event due at segment 0; until now at an `isr_par_ready` entry); with a mute pending `par_play` goes to its end and `isr_wait_frame` waits out `FW_ROM_CALL_BUDGET` (a fault at $B465); with a deadline already set, `par_play`'s wait at its back edge ends at it with a fault at $B877;
  - `tests/test_fw_cmd.c` (its clause-scan tests), `tests/test_fw_pros.c` and `tests/test_fw_tn.c` run in parallel mode (`boot_dip` bit 0) and record each `isr_par_ready` by its P0 write (`test_fw_tn.c` each `isr_par_hold` too); a ROM call of it fails in their mocks. The counts and orders stay; the registers at those calls are no longer checked (`test_fw_cmd.c`'s `test_scan_command` keeps its 4 calls; `test_fw_pros.c`'s `check_call` became `check_ready`). `test_fw_tn.c`'s `mock_wait` sets the flag at each interrupt wait and the mute at the `FW_WAIT_TIMER2` wait; `test_flush` counts 1,382 `isr_par_ready` calls (was 1,379), checks `boot_loop_play`'s sequence of handshake calls and waits, P3 = $79 at the end and the flag 0;
  - `tests/test_fw_par.c`: `mock_wait` records each wait with its `at` and `par_frames_left`; the mock interrupts (`g_flag_every`, `g_abort_at`) count interrupt waits; P3 is logged and the other SFRs kept. The frame-loop tests check the waits per pass (`check_wait`, `check_rest_frames`); `test_loop_play` checks the waits $B372, $B37F, $B465 (three), $B8E6 (three), $B465, the five P3 values and 19 other SFR writes;
  - `tests/test_fw_ser.c`: the stub's `wait` records `at` ($C500); `tests/test_golden_hybrid.c`: the "does not return" and "resets the stack" fault cases hook `cmd_run_next` $A893, called by the C `cmd_run_due` for one queued command whose handler stays ROM (`bank_cmd_select`), instead of `isr_par_ready`.
- **The branch probe** (§4.5, §4.7 criterion 3): the plan's scratch tool (not committed: hooks at both destinations of each conditional branch of the 11 routines, on the corpus and the boot run, ROM only) found **10 branches** in five routines (`isr_set_volume`, `isr_volume_half`, `isr_speech_start`, `isr_dsp_reset` and `isr_speech_stop` have none): 6 seen both ways on the corpus ($B2DD, $B2F8, $B30B, $B372, $B37F, $B465); 3 one-way, each completed by a `diff_serial` probe ($B300 not taken: `timer_repeat`; $C5D7 and $C5DF not taken: `par_ready`, `par_hold`); and $B35A `BNE` taken (`isr_wait_frames`, whose one caller passes X = 1), which no probe can reach (the probes' copies hold interrupts, so no frame comes) and `test_wait_frames_two` pins.
