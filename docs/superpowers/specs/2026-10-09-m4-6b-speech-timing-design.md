# M4.6b — Speech timing and the DSP in C (the timer ISR, the frame waits, volume, the parallel handshake) — Design

**Status:** design, awaiting user review
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
