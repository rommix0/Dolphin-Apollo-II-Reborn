# M4.4b — The frame loop in C (par_play, the tracks, the command events) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-07-m4-4b-frame-loop.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 12): §6 step 4, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md`: the C `tn_convert_clause` and its mid-clause flush into `boot_loop_play` (§5.1).
- `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`: the event producers (`cmd_event.c`) and the handler dispatch by address (`cmd_run_next`).
- `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md`: the playback set-up this loop drives, its output contract (§2.5), its conventions (§2.3–§2.7) and "As built" (§6).

**User decisions (2026-10-07):**
- One plan for the whole frame loop (M4.4a was the set-up).
- Hooks: all 16 routines, `util_shl3`/`util_shl4` included, as in M4.4a (every routine, so that a mismatch points at one routine). That makes 64 replacements and 65 `golden_hybrid` modes.
- The two routines that wait on the timer interrupt (`par_play`, `boot_loop_play`) are checked against the ROM by a new live differential test, `diff_play`, with interrupts running (§4.1). `diff_lts` cannot check them, because it holds interrupts. The same test injects the receive interrupt's two effects (abort, reply due) so that the branches no corpus text can reach are compared with the ROM too.
- The C keeps the conventions of M4.4a: one static helper per ROM block with its address range, the registers flowing through `fw_regs`, C calling C, and the quirks kept (§2.6).
- One new corpus text and its rom-engine golden, for the one reachable branch no text reaches: an event at segment 256 or later ($B88D, §4.6; user decision after the plan's survey). The existing goldens stay unchanged.

## 1. Goal

When M4.4b is done, the whole of playback above the frame builder runs as C in the `hybrid` engine:
- `boot_loop_play` holds the host, sets up playback (the C `par_play_start`), starts the DSP, ramps the volume, runs the frame loop and stops the DSP;
- `par_play` runs the command events due at each segment, serves the reply output, and once per frame starts the next segment, steps the eleven tracks and has the frame finished; at the end, or at a mute, it sends three rest frames;
- the track steppers and `util_interp_step` write each frame's parameter bytes into the fill buffer;
- the command events (index mark, @T sound start and end, @B voicing, @A volume) run from `cmd_run_event`;
- the C `tn_convert_clause` calls the C `boot_loop_play` directly;
- every one of the 16 routines matches its ROM original: 14 at every call `diff_lts` samples, and the two loops at every call `diff_play` makes.

The frame builder (`frm_*`, M4.5) and the interrupt, serial and DSP routines (`isr_*`, `ser_*`, M4.6) stay ROM and are called from C.

## 2. The subsystem (all main ROM)

### 2.1 Where it sits
- `boot_reset`'s main loop ($A687), `boot_selftest_sound` ($A7BA), `cmd_say_phonetic` ($A87C, unreachable) and `tn_convert_clause` ($B204, the mid-clause flush; C since M4.1) call **`boot_loop_play`** $A7D4. When `FW_UTT_CONTENT` ($6B) is set and `ser_abort` bit 0 is clear, it continues at its body $B2B3 (`JMP`, the same routine): `isr_par_hold`, **`par_play_start`** (C since M4.4a), `isr_speech_start`, `isr_volume_half`, `isr_wait_frame`, `isr_set_volume(cmd_volume)`, **`par_play`** $B877, `isr_volume_half`, `isr_wait_frame`, `isr_set_volume(0)`, `isr_speech_stop`.
- **`par_play`** drives everything per frame. Its input is M4.4a's output contract (that spec's §2.5) and the event queue `cmd_ev_*` $1200–$1485 written by the command parser (C since M4.2). Its output is the frame parameter bytes in the fill buffer at `isr_fill_ptr` ($2C), which `frm_finish_frame` completes and the timer interrupt `isr_timer_dsp` sends to the DSP.
- The timer interrupt swaps the frame buffers at `isr_swap_buffers` $B306 only when `isr_frame_flag` is clear (`BBC0` at $B300), and then sets it. `par_play` waits for the flag, builds the next frame, and clears the flag. The machine captures each produced frame at $B306.

### 2.2 Ported to C (16 routines, 983 bytes, 401 instructions)

| C file (new) | Routine | ROM | Bytes |
|---|---|---|---|
| `src/firmware/par_track.c` | `util_interp_step` | $C1A1–$C1F1 | 81 |
| | `par_track_start` | $C24A–$C26B | 34 |
| | `par_track_end` | $C26C–$C29B | 48 |
| | `par_track_sequential` | $C14C–$C1A0 | 85 |
| | `par_track_overlap` | $C1F2–$C249 | 88 |
| | `par_step_tracks` | $C0FF–$C130 | 50 |
| `src/firmware/cmd_play_event.c` | `cmd_run_event` | $BB74–$BB9F | 44 |
| | `cmd_ev_index` | $BBA0–$BBA6 | 7 |
| | `cmd_ev_sound_start` | $BBA7–$BC7F | 217 |
| | `cmd_ev_sound_end` | $BC80–$BCE9 | 106 |
| | `cmd_ev_set_voicing` | $BCEA–$BCEC | 3 |
| | `cmd_ev_set_volume` | $BCED–$BCF2 | 6 |
| `src/firmware/par_play.c` | `par_play` | $B877–$B916 | 160 |
| | `boot_loop_play` | $A7D4–$A7DE and its body $B2B3–$B2D8 | 49 |
| `src/firmware/fw_ops.h` | `util_shl4`, `util_shl3` | $C656, $C657–$C65A | 5 |

- Each `.c` has a `.h` declaring its routines and any constant a caller or test needs, as in M4.4a. Every routine is `void name(fw *f, fw_regs *r)`: r in = the entry registers, r out = the ROM's exit registers.
- `util_shl3` and `util_shl4` are `static inline uint8_t util_shlN(uint8_t a)` in `fw_ops.h`, where the existing inline `util_shl4` moves from `cmd_match.h`. Their hooked forms are two thin thunk functions in `thunks.c` (A in, A out; X and Y untouched).
- **`par_slot_map`** $B797–$B7A0 is converted (§3).
- New ROM addresses go in `src/firmware/par_addr.h`. `ROM_BOOT_LOOP_PLAY` moves there from `tn_convert.h`.

### 2.3 C calls C, and the structure of the loop
- `boot_loop_play` calls the C `par_play_start` and the C `par_play`.
- `par_play` calls the C `cmd_run_event`, `par_next_segment`, `par_step_tracks` and `cmd_ev_sound_end`.
- `par_step_tracks` calls the C track steppers and `util_interp_step`; the steppers call `util_interp_step`; `cmd_ev_sound_start` calls `util_shl3`.
- The C `tn_convert_clause` calls the C `boot_loop_play` instead of `fw_rom_call(ROM_BOOT_LOOP_PLAY, …, FW_ROM_CALL_SPEECH_BUDGET)`. No firmware call then uses `FW_ROM_CALL_SPEECH_BUDGET`; it stays defined, because `tests/test_fw.c` checks the budget pass-through with it.
- **`cmd_run_event` dispatches on the handler address**, as `cmd_run_next` does in M4.2. It reads the 16-bit address from `cmd_ev_handlers`, writes it to `cmd_ev_vector` (RAM, kept exact), and calls the C handler for the five `cmd_ev_*` addresses. Any other address goes to `fw_rom_call` with the A, X and Y the ROM's `JMP (cmd_ev_vector)` would hand over (§2.6); `fw_rom_call` starts the routine with the machine's own flags, not those of the `LDA`, which no handler reads. The producers queue only those five, so the fallback is never taken on the corpus; a unit test pins it.
- **Waiting.** Each ROM wait loop `JSR isr_par_ready` / `BBC0 isr_frame_flag` becomes a C loop: one ROM call to `isr_par_ready` (`fw_par_ready`), then a test of `isr_frame_flag` bit 0 in RAM. The C takes no emulated cycles, so interrupts fire only inside the ROM calls. That is enough: every pass makes at least one ROM call, so the loop always advances. The main pass of `par_play` keeps the ROM's order of calls exactly (§2.6).

### 2.4 ROM calls from C

| ROM routine | Called by | Why it stays ROM |
|---|---|---|
| `isr_par_ready` $C5D6 | `par_play` (every pass and every wait) | M4.6. Parallel mode only: sets P0.3. Preserves A. |
| `ser_tx_service` $C581 | `par_play` ($B89C) | M4.6. Sends the next reply byte when the host allows it; `ser_status_due` = `ser_tx_pending`. |
| `isr_wait_frame` $B465 | `par_play` ($B8D2), `boot_loop_play` | M4.6. Waits for the flag and clears it. |
| `frm_finish_frame` $B9B5 | `par_play` ($B8C3, $B90E) | M4.5. Saves and restores A, X and Y. |
| `frm_copy_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space`, `frm_transition_flags` | `par_play` (the rest frames, $B8DA–$B8E3) | M4.5. |
| `isr_set_volume` $ACE8 | `cmd_ev_set_volume`, `boot_loop_play` | M4.6. Calls the hooked `util_shl3` (a nested thunk). |
| `isr_par_hold`, `isr_speech_start`, `isr_volume_half`, `isr_speech_stop` | `boot_loop_play` | M4.6. `isr_speech_start` waits one frame (`isr_wait_frames`). |

- Every call has `FW_ROM_CALL_BUDGET` (10 M cycles); the longest, `isr_speech_start` and `isr_wait_frame`, wait at most a frame or two (20,000 cycles each).
- Each call gets the registers the ROM has at its `JSR`, and the C carries the registers each returns, as M4.4a's calls do. No call resets the stack: there is no unwind.
- **The UART status.** `par_play` reads `UART_STATUS` $E7 (an SFR) at $B895. The firmware cannot read an SFR today (`fw_mem8` faults on one). New in `fw.h`:
  - `fw_host` gains `uint8_t (*read_sfr)(void *ctx, uint8_t addr)`; `src/engine/thunks.c` implements it with the machine's SFR read, which for $E7 has no side effect (`m50734.c`: bit 1, transmitter ready, is always set; bit 0 is receive full).
  - `uint8_t fw_sfr8(fw *f, uint8_t addr)` calls it; without a host, or for an address outside $DA–$FF, it is a fault.
  - The native host of M5 will answer $E7 with "transmitter ready".

### 2.5 Hooks (16 new replacements; 64 in all, 65 `golden_hybrid` modes)

Declared outputs: **A, X and Y for all 16**. Each thunk's comment cites what its ROM callers read after the call; the plan checks every row in the dumps.

| Hook | ROM callers | Read after the call / reached in "all" mode |
|---|---|---|
| `boot_loop_play` $A7D4 | `boot_reset` $A687, `boot_selftest_sound` $A7BA, `cmd_say_phonetic` $A87C, `tn_convert_clause` $B204 | Nothing found (`JMP`/`RTS` follow); X and Y are untouched when it does not play, which the C `tn_convert_clause` relies on (M4.3b's `pros_make_segments` thunk comment). Fires in "all" mode from the ROM main loop. |
| `par_play` $B877 | `boot_play_body` $B2C7 | Nothing (`isr_volume_half` loads A). Alone only. |
| `cmd_run_event` $BB74 | `par_play` $B88F | Nothing (`JMP par_play`, `LDX`). Alone only. |
| `cmd_ev_index`, `cmd_ev_sound_start`, `cmd_ev_set_voicing`, `cmd_ev_set_volume` | `JMP (cmd_ev_vector)` at $BB9D; they return to `par_play` | Nothing (`JMP par_play`). Alone only. |
| `cmd_ev_sound_end` $BC80 | `JMP (cmd_ev_vector)`; `par_play` $B8F4 | Nothing (`JSR isr_par_ready`, which preserves A; `RTS`). Alone only. |
| `par_step_tracks` $C0FF | `par_play` $B8C0, $B90B | Nothing (`frm_finish_frame` pushes and restores them). Alone only. |
| `par_track_sequential` $C14C, `par_track_overlap` $C1F2 | `par_step_tracks` $C10C, $C106 | **X** (`DEX` at $C10F). Alone only. |
| `par_track_start` $C24A, `par_track_end` $C26C | `par_track_overlap` $C1F2, $C1F8 | A, X. Alone only. |
| `util_interp_step` $C1A1 | `par_step_tracks` $C124, `par_track_sequential` $C163/$C195, `par_track_overlap` $C210, `par_track_start` $C265, `par_track_end` $C28E | A, and X at every site but $C124 (the pitch track reads A only); the callers load Y themselves. Alone only. |
| `util_shl3` $C657 | `cmd_ev_sound_start` $BBA7, `isr_set_volume` $ACEC, and the fall-through from the ROM `util_shl4` | A. Fires in "all" mode from the ROM `isr_set_volume`. In `util_shl4`'s alone mode the ROM `util_shl4` is never reached (its callers are C), so the fall-through only happens on the rom engine. |
| `util_shl4` $C656 | `cmd_apply_pitch`, `cmd_hex_byte`, `cmd_set_formant`, `cmd_set_mark_space` (ROM) | A. Alone only, as far as the dumps show: in "all" mode these callers are C and inline it (the plan checks each one). |

- Where nothing is read, the thunk still sets A, X and Y to the ROM's exit values.
- **The replacement table marks the two loop routines.** `fw_replacement` gains a field (`waits`, true for `par_play` and `boot_loop_play`). `diff_lts` skips the marked ones and `diff_play` observes exactly those (§4).
- **Hook budget.** `MACHINE_MAX_HOOKS` stays 256: `golden_hybrid` installs 64 replacements plus its `isr_par_ready` hook. `DIFF_MAX` (tests/test_diff_lts.c) rises from 48 to 64.

### 2.6 Quirks kept

All of M4.4a's rules hold: every RAM write, the write-only ones included; 8-bit wraps and carry chains as the ROM has them; signed and unsigned tests as the ROM's flags make them; registers through `fw_regs`; ROM data only through `fw_rom8`/`fw_ind_y`/`fw_mem8`. Specific to M4.4b:

- **`par_play`'s two loops.**
  - *While segments remain* (`pros_seg_count` ≠ 0, both bytes), each pass: run every due event (`cmd_ev_next` < `cmd_ev_count`, unsigned, and `par_seg_index` = `cmd_ev_pos`, both bytes; one event per `JMP par_play`, so the test repeats after each); then, when `UART_STATUS` bit 1 is set and `ser_status_due` ≠ 0, `ser_tx_service`; then `isr_par_ready`; then, when `ser_abort` bit 0 is set, the end; then, when `isr_frame_flag` bit 0 is clear, a second `isr_par_ready` and the next pass. With the flag set: `DEC par_frames_left`; at 0, `par_next_segment`, and when `par_frames_left` is still 0, back to the `pros_seg_count` test ($B906 → $B8B1); otherwise `isr_par_ready`, `par_step_tracks`, `frm_finish_frame`, `LDM #0,isr_frame_flag`, next pass.
  - *Once `pros_seg_count` is 0* ($B8BB), a tighter loop with no events, no reply service and no abort test: `DEC par_frames_left`; at 0, the end; otherwise `par_step_tracks`, `frm_finish_frame`, clear the flag, then wait (`isr_par_ready` / flag) and loop.
  - **A zero-frame segment wraps.** When `par_next_segment` leaves `par_frames_left` = 0 and segments remain, the next `DEC` at $B8FB makes it $FF; when none remain, the `DEC` at $B8BB does. The C does the same. Unreachable: every duration source is at least 1, and on the corpus the `BEQ` at $B906 is never taken; a unit test pins the wrap.
  - *The end* ($B8D2): `isr_wait_frame`, `par_frames_left` = 3, then three times: `frm_copy_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space`, `frm_transition_flags`, wait for the flag, clear it, `DEC`. Then `cmd_ev_sound_end` and a last `isr_par_ready`.
  - `LDM #0,isr_frame_flag` writes the whole byte. The flag tests read bit 0 only.
- **`cmd_run_event`.** A = `cmd_ev_next`; `cmd_ev_next` += 1; X = A·2 (`ASL`, 8-bit). `cmd_ev_vector` = `cmd_ev_handlers[X]`. `INX`/`INX` wraps at X = $FE, so `cmd_ev_pos` = `cmd_ev_positions` at (X + 2) & $FF; the @T producer writes its end event at $1208,X without that wrap (M4.2's `cmd_sound`). X = `cmd_ev_next` − 1, A = `cmd_ev_args[X]`. The handler is entered with that A and X, the caller's Y, and the flags of the `LDA`.
- **`cmd_ev_sound_start`.**
  - X = `util_shl3(A)`: the effect's four pointers come from `cmd_sound_table` + X with `fw_rom8`.
  - The saves: the four playback pointers to `cmd_sound_save`; `par_seg_index` to `cmd_sound_seg_index`; `pros_seg_count`, `cmd_voicing`, `pros_pitch_centre`, `cmd_source_prosody`, `pros_play_target`, `par_next_rank` and `par_next_phoneme` to `cmd_sound_state[0..7]`, in the ROM's order (`par_next_phoneme` goes to [7] before `par_next_rank` to [6]).
  - The effect's first phoneme through `fw_ind_y` ($22, the pointer now into ROM), its rank through `fw_ind_y` ($2E, `par_rank_ptr`).
  - Fixed values: `pros_pitch_centre` = $80, `cmd_source_prosody` = 3, `cmd_voicing` = 8; `par_seg_index` = 0 and `pros_seg_count` = $00FF (`LDY #$FF`/`STY` before the scan; the scan does not change it).
  - **The terminator scan** counts Y from 0 to the $FF marker (`INY` first, so the length is the marker's index). Y wraps as the ROM's does; the C faults after 256 bytes instead of hanging. Unreachable: @T queues only effects 0–16 (M4.2's `cmd_sound`), and each has its marker.
  - The end event's position, at X = `cmd_ev_next`·2 (8-bit), and `cmd_ev_pos` = the length, high bytes 0. `cmd_sound_active` bit 0 set (`SEB0`).
- **`cmd_ev_sound_end`.** Returns at once when `cmd_sound_active` bit 0 is clear; otherwise clears it and restores everything `cmd_ev_sound_start` saved, in the ROM's order.
- **`cmd_ev_index`:** `cmd_index_count` −= 1 unless 0. **`cmd_ev_set_voicing`:** `cmd_voicing` = A. **`cmd_ev_set_volume`:** `cmd_volume` = A, then `isr_set_volume` with that A.
- **`util_interp_step`** (A = target, Y = steps, `util_interp_cur`, `util_interp_steps` = Y, `util_interp_target` = A by every caller).
  - Y = 1: returns A unchanged. Y = 2: `CLC`/`ADC util_interp_cur`/`ROR`, the 9-bit average.
  - A − cur = 0 (`SEC`/`SBC`): returns `util_interp_target`.
  - Otherwise the quotient by repeated `SBC util_interp_steps` (carry set) or `ADC` (carry clear), then `LSR util_interp_steps` — **the halved count stays in RAM** (except on the down path's exact quotient, where the `BEQ` at $C1E1 skips the `LSR`) — and the rounding: the remainder plus half the steps, tested with `BPL`/`BMI`/`BEQ` as at $C1C6–$C1EF; `util_interp_quot` is written, and A = cur + quotient. Y out as each path leaves it.
  - **Zero steps.** Y = 0 with A ≠ cur makes the ROM loop forever (`SBC #0` with the carry set, or `ADC #0` with it clear, never ends). The C faults instead. Unreachable: a track's count reaches 0 only after the step with Y = 1, which leaves the value equal to its target, and the target does not move within a segment; with equal values Y = 0 returns at $C1B2, and the count goes on to $FF. On the corpus Y is never 0 (it ranges 1–80 for the pitch track, 1–40 and 1–13 for the parameter tracks), so neither Y = 0 case is reached; unit tests pin both.
- **The tracks.**
  - `par_step_tracks`: X = 10 down to 1, `par_sequential[X]` ≠ 0 → `par_track_sequential`, else `par_track_overlap`; then the pitch track: `util_interp_steps` = `par_frames_left`, `pros_play_pitch` steps to `pros_play_target`, and frame byte b10 = `pros_play_pitch` >> 2.
  - `par_track_sequential`: start transition while `par_start_dur[X]` ≠ 0 (decremented), then hold `par_seg_target[X]` while `par_steady[X]` ≠ 0 (decremented), then the end transition from `par_seg_target[X]` itself to `par_boundary[X]` over `par_end_dur[X]` (decremented, wrapping past 0).
  - `par_track_overlap`: the median of the line value (`par_line_val[X]` to `par_boundary[X]` over `par_frames_left`), the start track and the end track. **The comparisons are `CMP` then `BPL`/`BMI`: the sign of the 8-bit difference, not a true signed comparison** (overflow ignored). The C tests bit 7 of the 8-bit difference exactly as the ROM's branches do. `par_start_track`/`par_end_track` are written (write-only outside this routine).
  - `par_track_start`: `par_seg_target[X]` once `par_start_dur[X]` is 0, else one step of `par_start_val[X]`.
  - `par_track_end`: **a negative `par_end_wait[X]` is zeroed** ($C271, `LDY #0`/`TYA`/`STA`) — unreachable, because `par_plan_tracks` stores 0–127 and the count is decremented only while non-zero; the C keeps it. While it is non-zero: decrement and return `par_seg_target[X]`; at 0: one step of `par_end_val[X]` to `par_boundary[X]` over `par_end_dur[X]`.
  - **Every frame byte** goes through `(isr_fill_ptr),Y` with Y = `par_slot_map[X]` (or 10 for the pitch). New in `fw.h`: `void fw_wr_ind_y(fw *f, uint8_t zp, uint8_t y, uint8_t v)`, which faults when the address is not RAM (≥ $4000, or an SFR).
- **`boot_loop_play`.** When it does not play, A = `FW_UTT_CONTENT` and X, Y are untouched. When it plays, the registers flow through the ROM calls and the C `par_play_start`/`par_play`; the exit registers are those `isr_speech_stop` leaves. `isr_set_volume` gets A = `cmd_volume`, then A = 0.
- **`util_shl4`** = `ASL` then `util_shl3` (three `ASL`s): A << 4, 8-bit. The carry out is not read by any caller.
- **Timing.** The C takes no CPU cycles. The produced frames cannot change: the timer interrupt swaps the buffers only when `isr_frame_flag` is clear, and `par_play` clears it only after it has built the whole frame (M4.4a §2.7). `test_timing_invariance` gains the hybrid engine (§4.4). Interrupts are serviced inside the ROM calls; the moment within a frame at which a pass sees the flag moves, which nothing but the reply-service timing can observe, and no reply is due during playback on the corpus.

## 3. Converted data (`firmware_data` 47,692 → 47,702 bytes)

| ROM | Table | Form |
|---|---|---|
| $B797–$B7A0 | `par_slot_map` (10) | Per parameter P1…P10, the frame byte offset, by name: FN, F1, F2, F3 (b1, b3, b5, b7), ALF, A1, A2, A3, AHF (b0, b2, b4, b6, b8), voicing (b9). In `src/firmware/data/par_data.c`, next to `frm_snapshot_map`. Read as $B796 + X with X = 1–10; $B796 itself is not read and not converted. |

- `tests/test_firmware_data.c` checks every converted byte against the ROM; its `k_rom` list gains {$B797, $B7A1}.
- Not converted: `unused_B7A1` (unreferenced), and the `frm_*` tables (M4.5).

## 4. Verification

### 4.1 `diff_play` (new suite, `tests/test_diff_play.c`)
- Runs every corpus file and the boot run on the ROM engine, with an observer on each replacement marked `waits` (`par_play` $B877, `boot_loop_play` $A7D4). **Every call** is checked (a few per file; no sampling). `par_play` re-enters $B877 by its own `JMP par_play` ($B892, $B8AE, $B914) with the same stack, about 27,000 times per call; only a real call counts, recognised by the `JSR par_play` at $B2C7 having just run (a hook there arms the next $B877 entry). `boot_loop_play` has no such re-entry.
- At each call the machine is cloned twice, **with interrupts live in both**:
  - copy A runs the ROM routine to its return (`machine_finish_routine`, budget 1 G cycles);
  - copy B runs the C thunk.
- Both copies record, through the machine's frame callback at $B306, every produced frame and at that moment a snapshot of: all RAM except the SFRs ($DA–$FF) and the stack page ($0100–$01FF); the P2 latch; the UART transmit count and last byte. At a swap both main programs are waiting for the flag, so their state must agree.
- **Pass:** the same number of swaps; every frame and every snapshot equal; and at exit: PC and SP, A, X and Y, the P2 latch, the UART transmit state, and all RAM except the SFRs, the dead stack below the exit SP, and the timer interrupt's per-word counters `isr_tick_div` $59, `isr_word_ofs` $5A and `isr_words_left` $5B. Those advance between DSP words, so they depend on how far into a frame each copy returns; every swap has compared them already. (The survey checked `isr_timer_dsp`'s writes: `isr_swap_phase` $5D, `isr_play_ptr`, `isr_fill_ptr` and `isr_frame_flag` change only at a swap.) Neither copy is run on past its return: after a `ser_status_due` injection the ROM never reaches idle again (`boot_get_clause` waits for the host at $A6D4).
- **Injected receive-interrupt effects.** On the first call of each of the two routines in each corpus file (and in the boot run) that plays (in five texts the first `boot_loop_play` call returns at once), two more pairs of copies run, each with one change made to both copies as the receive interrupt would make it. The reply-due flag is set inside the frame callback of the second swap. The abort bit is set at the first `frm_finish_frame` entry ($B9B5) after the second swap, a point both copies reach in the same state: at a swap the ROM can be interrupted between its abort test ($B8A2) and its flag test ($B8A8), and then it builds one more frame before it sees the abort, where the C (interrupted only inside its ROM calls) sees it at once. That is a difference of when the interrupt lands, not of the logic; the drafts measured it in 4 of 46 calls when the bit was set at the swap. The corpus runs' own boot `boot_loop_play` call (return address $A7BD) and the `par_play` inside it are checked in the boot run only, so that "the first call" of a corpus run is its text's first utterance.
  - `ser_abort` bit 0 set (a mute): reaches the end path from $B8A5;
  - `ser_status_due` = 1 (a reply due): reaches `ser_tx_service` from $B89C.
  
  They are compared as above. The suite counts the calls whose ROM copy executed $B8A5 and $B89C (an observer hook on the ROM copy) and requires both counts > 0.
- Prints the calls checked per routine; each must be > 0, with 0 mismatches. A mismatch prints the corpus file, the routine, the call, the swap number and the first differing byte or register.

### 4.2 `diff_lts`
- `DIFF_MAX` 64. It skips the replacements marked `waits`; it checks the other 62 (48 + 14), each more than 0 times, with 0 mismatches. "Every routine exercised" holds across `diff_lts` and `diff_play` together.
- The flush skip (`stop_at_play`, tn_convert_clause only) is unchanged: the ROM copy still reaches $A7D4.
- `cmd_ev_set_volume` (which calls the ROM `isr_set_volume`) and `util_shl3` (called from the ROM `isr_set_volume`) run with interrupts held like any other.

### 4.3 `golden_hybrid`
Every file × 65 modes (all routines, then each alone), byte for byte; every existing golden is unchanged.

### 4.4 `test_timing_invariance` (`tests/test_golden.c`)
Also runs `hybrid_engine_speak` with all hooks on `hello` at the timer scales 1/4 and 3/1, against the same golden. With 3/1 the frame period is shorter than the ROM's longest build (the ROM builds 30 of hello's frames late there); the C loop builds in no emulated time, so it is never late at either scale, and the check is that the frames stay the same whatever the timer's speed.

### 4.5 Unit tests (`tests/test_fw_par.c`)
- One or more cases per routine, the registers in and out per §2.5, RAM checked byte for byte against values worked out from the dumps. The ROM calls are recorded with a stub host, as in the existing cases.
- `util_interp_step`: Y = 1; Y = 2 with a carry out of the add; equal values (Y = 0 included); quotients up and down with each rounding branch ($C1CA `BPL`, $C1E1 `BEQ`, $C1EA `BMI`, $C1EC `BEQ`, $C1EE `INY`); `util_interp_steps` halved in RAM; Y = 0 with different values faults.
- `par_track_overlap`: each of the six median outcomes, and one where the 8-bit difference overflows (e.g. $10 against $90), which a true signed comparison would order the other way.
- `par_track_end`: a negative `par_end_wait` zeroed ($C271), and a wait ending.
- `par_track_sequential`: each phase, and `par_end_dur` wrapping past 0.
- `par_step_tracks`: the slot routing through `par_slot_map` and b10.
- `cmd_run_event`: `cmd_ev_next` = 127 (X = $FE; `cmd_ev_pos` from `cmd_ev_positions[0]`), the handler registers, and an unknown handler address going to `fw_rom_call` with A, X, Y as the ROM's `JMP` hands them.
- `cmd_ev_index` with `cmd_index_count` = 0 ($BBA2 taken: nothing written) and with 1.
- `cmd_ev_sound_start` / `cmd_ev_sound_end`: save, set and restore, with effect 0 and effect 16; `cmd_ev_sound_end` with the flag clear writes nothing.
- `par_play` with a stub host whose `isr_par_ready` sets the flag every n-th call: the order of ROM calls in a pass, the two loops, the zero-frame wrap, the abort exit and the reply service, the three rest frames.
- `boot_loop_play`: not playing ($6B = 0; abort set) with X and Y untouched; playing, with its ROM calls in order.
- `fw_wr_ind_y` faults outside RAM; `fw_sfr8` faults without a host and outside $DA–$FF.
- `tests/test_fw_tn.c`: the case that checks the flush's `fw_rom_call(ROM_BOOT_LOOP_PLAY, …, FW_ROM_CALL_SPEECH_BUDGET)` instead checks that the C `boot_loop_play` ran (its first ROM calls, `isr_par_hold` then the C `par_play_start`'s work).

### 4.6 Coverage and the new corpus text
Probed while designing with `apollo2_coverage` on the 44 corpus texts: every instruction of the 16 routines executes except $B89C (reply service), $B8A5 (abort) and $C271–$C274 (negative `par_end_wait`). The plan's branch probe found these branches one-sided:
- **$B88D** (`cmd_ev_pos` high byte against `par_seg_index` high byte): never taken, because no event sits at segment 256 or later. **New corpus text** `late_event`: the `seg_256` text with a @B command placed after segment 256, so the event's position has a high byte of 1 and the low bytes meet first on page 0. Its golden is generated once by the rom engine; the plan confirms with the probe that $B88D is taken. The corpus goes from 44 to 45 texts (`golden_hybrid` prints 46 files, boot included).
- **$A7D8** (abort set when `boot_loop_play` is entered): needs serial input during playback; a unit test pins it.
- **$B906** (a zero-frame segment): unreachable, since every duration source is at least 1 (the survey found the `BEQ` never taken and neither `DEC` starting from 0); a unit test pins it.
- **$BBA2** (`cmd_index_count` already 0 when an index mark plays): reachable only through an 8-bit wrap of the count; a unit test pins it.
- $B89C and $B8A5 need serial input during playback. The machine delivers a host byte only at the idle `WIT` (parent spec §4), never during playback, so no text can reach them; `diff_play`'s injected cases compare them with the ROM (§4.1).
- $C271 is unreachable (§2.6); the unit test pins it.
- With `late_event`, every other conditional branch of the 16 routines is seen both ways on the corpus.

### 4.7 Done criteria
1. `diff_lts` checks all 62 non-waiting routines more than 0 times each, with 0 mismatches.
2. `diff_play` checks `par_play` and `boot_loop_play` more than 0 times each, reaches $B8A5 and $B89C, with 0 mismatches.
3. `golden_hybrid` reproduces every file (46, boot included) × 65 modes byte for byte; every existing golden is unchanged; the `late_event` golden exists and takes $B88D.
4. `test_timing_invariance` passes on both engines.
5. `firmware_data` passes at 47,702 converted bytes.
6. `apollo2 --engine hybrid` equals `--engine rom` on every corpus text (`-o FILE` only).
7. `src/firmware/` still builds without `machine/` or `cpu740/` headers; `tn_convert_clause` no longer calls `ROM_BOOT_LOOP_PLAY`.
8. `build.bat test` is green (33 suites).
9. The docs are updated: the parent spec gets rev 13 (milestone 4.4b done; M4.4 complete; the new `read_sfr` host capability recorded in §6/§8); this spec's status, with "As built" if anything differs; the README's hybrid section lists the frame loop.

### 4.8 Task outline (for the plan)
1. The host capability: `read_sfr`/`fw_sfr8`, `fw_wr_ind_y`, `util_shl3` into `fw_ops.h` (and `util_shl4` moved), the `waits` field, `DIFF_MAX` 64; `par_slot_map` converted; the `late_event` corpus text and its golden.
2. The tracks: `util_interp_step`, `par_track_start`, `par_track_end`, `par_track_sequential`, `par_track_overlap`, `par_step_tracks`, with hooks.
3. The command events: `cmd_run_event` and the five handlers, `util_shl3`/`util_shl4` hooks.
4. `diff_play` (first against the ROM routines alone: both copies ROM, to prove the comparison itself before any C loop exists).
5. `par_play` and `boot_loop_play` in C with hooks; the C `tn_convert_clause` calls the C `boot_loop_play`; `test_timing_invariance` on the hybrid engine.
6. Spec rev 13, the status, "As built" and the README.

## 5. Out of scope
- The frame builder (M4.5): every `frm_*` routine and its tables (`frm_template`, `frm_b14_15_*`, …), including the five that `par_play` calls.
- `isr_*`, `ser_*`, `boot_*` (other than `boot_loop_play`) and `bank_*` (M4.6).
- `pros_pitch_code_dither` $C131 (unreferenced).
- Serial input during playback in the machine model (a host mute or query mid-utterance as a corpus golden). The delivery rule of the parent spec §4 is unchanged; `diff_play` injects the effects instead.

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **`util_interp_step` faults on `util_interp_steps` = 0, not on Y** (§2.6). Y only picks the path (1, 2, other); the quotient loops subtract or add `util_interp_steps` from RAM, which every caller stores from Y first. So the C faults when `util_interp_steps` is 0 on the general path with a non-zero difference, which on every real call is §2.6's "Y = 0 with A ≠ cur". Its exit Y is 0 for Y = 1 and Y = 2, Y − 2 for equal values ($FE for Y = 0), else the quotient (`util_interp_quot`).
- **The two endless loops fault through a new public helper, `fw_fault(fw *f, uint16_t addr)`** (`fw.h`), a one-line wrapper of `fw.c`'s static `fault`: with `f->trap` set it longjmps, else it sets `f->fault` and keeps the first `fault_addr`. Its callers are `util_interp_step` (`quotient_up` and `quotient_down`, at `ROM_UTIL_INTERP_STEP`) and `cmd_ev_sound_start` (no $FF within 256 reads, at `ROM_CMD_EV_SOUND_START`, before the end event: `cmd_ev_pos`, the end event's position and `cmd_sound_active` are not written). `fw_sfr8` and `fw_wr_ind_y` use the static `fault`. `test_fault` (`tests/test_fw.c`) checks, without a trap, that it sets the fault and keeps the first address.
- **Without a trap, `util_interp_step`'s fault does not stop its C callers.** `par_track.h` says it "returns with nothing written": `util_interp_step` itself writes nothing, but `track_step` then stores the A it leaves (the difference) in the track value, and the caller goes on to the frame byte. Every thunk sets the trap (`run_thunk`), so in the hybrid engine the fault longjmps out first. Only the unit tests run without one, and `test_interp_zero_steps` calls `util_interp_step` alone.
- **`host_read_sfr` reads only `UART_STATUS` $E7** (§2.4 has it use the machine's SFR read). It reads $E7 through `m->bus.read`, as the CPU's `LDA` does, and returns $FF for any other SFR without reaching the bus, so that no read side effect (`UART_RX` $E5 clears receive full) can happen behind the ROM's back; an SFR the firmware comes to poll must be added there. `test_host_read_sfr` (`tests/test_machine_hooks.c`) checks through a real bridge: $E7 reads $02, then $03 with a byte received, and again $03 (receive full stays set); $E5 reads $FF and the byte stays received; `unmodelled_count` stays 0. `fw_sfr8` faults and reads $FF, without calling the host, with no host, no `read_sfr` or an address below $DA (`test_sfr8`).
- **The thunks** declare A, X and Y for all 16 (§2.5). The 14 C routines' thunks build `fw_regs r = { c->a, c->x, c->y, 0 }`, call the C and copy A, X and Y back; `util_shl3` and `util_shl4` set A only and leave the machine's X and Y as they are. The exit registers, sharper than §2.5 (each in its thunk's comment and its header):
  - `boot_loop_play`: A = $6B with X and Y untouched when it does not play, else what `isr_speech_stop` leaves; `par_play`: what `cmd_ev_sound_end` leaves, kept by the last `isr_par_ready`;
  - `cmd_run_event`: what the handler leaves (the C one, or the ROM one through `fw_rom_call`); `cmd_ev_index` and `cmd_ev_set_voicing`: all three untouched; `cmd_ev_set_volume`: what `isr_set_volume` leaves (A = the new P3, X and Y untouched); `cmd_ev_sound_start`: A = 0, X = `cmd_ev_next` << 1 (8 bits; already incremented, so the @T end event's slot), Y = the index of the $FF; `cmd_ev_sound_end`: A = `cmd_sound_state[6]` when it restores, else as it came in, X and Y untouched;
  - `par_step_tracks`: A = `pros_play_pitch` >> 2, X = 0, Y = 10; `par_track_sequential` and `par_track_overlap`: A = the value written, X untouched, Y = its frame byte; `par_track_start`: Y = 0 when there is no step, else `util_interp_step`'s; `par_track_end`: Y = the wait count before its `DEC` while waiting, else `util_interp_step`'s (a zeroed negative count goes on to the step).
- **`util_shl4` never fires in "all" mode** (§2.5: "as far as the dumps show"): its five `JSR`s are in `cmd_apply_pitch` ($A835, $A83F), `cmd_set_formant` ($A952), `cmd_set_mark_space` ($A9AD) and `cmd_hex_byte` ($B455), all C, which inline it. Its inline moved from `cmd_match.h` to `fw_ops.h` (as `util_shl3(a << 1)`); `cmd_apply.c`, `cmd_match.c` and `cmd_set.c` include `fw_ops.h`, and the inline is gone from `cmd_match.h`.
- **`par_play`'s waits end on a failed ROM call**, the one place its C departs from the dump: `wait_frame_flag` and the top of each pass test `f->fault`. Under a thunk the failed call has longjmped out already; in a unit test whose mock fails a call the loop would otherwise never end (the ROM routine would never return either).
- **The C layout**:
  - `par_track.c`: `util_interp_step` (static helpers `average`, `quotient_up`, `round_up`, `quotient_down`, `round_down`, `add_quotient`), `par_track_start`, `par_track_end` (`zero_wait`, `end_wait`), `par_track_sequential` (`hold`), `par_track_overlap` (`minus`, `median`) and `par_step_tracks` (`pitch_track`). One helper, `track_step`, makes the six copies of the step pattern ($C151, $C183, $C201, $C253, $C27C, $C115), with the count's address 0 for the two that have no `DEC` (the overlap line and the pitch); `slot_store` is `LDY $B796,X; STA ($2C),Y`;
  - `cmd_play_event.c`: `cmd_run_event` (`event_vector`, `next_position`, `dispatch`, with the table `k_events` of the five handler addresses), `cmd_ev_sound_start` (`save_pointers`, `effect_pointers`, `save_state`, `first_phoneme`, `fixed_state`, `find_end`, `end_event`) and `cmd_ev_sound_end` (`restore_pointers`, `restore_state`); `cmd_ev_index`, `cmd_ev_set_voicing` and `cmd_ev_set_volume` have no helper. `cmd_play_event.h` holds `CMD_SOUND_ACTIVE_ON`, `CMD_SOUND_PITCH_CENTRE`, `CMD_SOUND_SOURCE_PROSODY` and `CMD_SOUND_VOICING`;
  - `par_play.c`: `par_play` (`count_frame`, `wait_frame_flag`, `run_due_event`, `serve_reply`, `segment_frame`, `segment_step`, `last_frames`, `rest_frames`) and `boot_loop_play` (`play_body`, $B2B3–$B2D8). `PAR_REST_FRAMES` (3) is in `par_play.h`; `SFR_UART_STATUS` ($E7) and `UART_TX_READY` (bit 1) stay in the `.c`. `isr_par_ready` is called through M4.4a's `fw_par_ready`, which takes the registers by value, so A, X and Y stay as its `PHA`/`PLA` keeps them;
  - `fw.h`/`fw.c`: `read_sfr` (the last member of `fw_host`), `fw_sfr8`, `fw_wr_ind_y` (the address formed as `fw_ind_y` forms it; every caller passes `ISR_FILL_PTR`) and `fw_fault`; `fw_ops.h`: `util_shl3`, `util_shl4`;
  - `par_addr.h`: `ROM_BOOT_LOOP_PLAY` (moved from `tn_convert.h`), `ROM_PAR_PLAY`, `ROM_CMD_RUN_EVENT`, the six track routines, `ROM_UTIL_SHL4`, `ROM_UTIL_SHL3`, and the ROM routines the loop calls (`isr_set_volume`, `isr_speech_start`, `isr_speech_stop`, `isr_wait_frame`, `isr_volume_half`, `isr_par_hold`, `ser_tx_service`, `frm_snapshot`, `frm_finish_frame`, `frm_transition_flags`), each with its callers; a caller in parentheses stays ROM, so `ROM_FRM_COPY_REST_FRAME` and `ROM_FRM_CLAMP_MARK_SPACE` now name `par_play` outside them. The five `ROM_CMD_EV_*` stay in `cmd_addr.h` and `ROM_ISR_PAR_READY` in `tn_convert.h`.
- **`par_slot_map`** (§3) is written with the `FRAME_*` names, P10 as `FRAME_V`. The encoder fails an entry above `FRAME_V` (sharper than `frm_snapshot_map`'s `FRAME_PITCH`): b10 is the pitch track's, which `par_step_tracks` writes itself (`LDY #$0A`). The C reads the map only through `fw_rom8` at `PAR_SLOT_MAP` − 1 + X.
- **`late_event`** (§4.6) is `@Q9`, `seg_256`'s 54 words and the next two of their cycle (`o at`), then `@B5 oh.` (`seg_256` is `@Q9`, the 54 words, `oh.`). `seg_256`'s words end at segment 252, so the @B event lands at segment 259 ($0103): at segment 3 on page 0 the low bytes match and the high-byte `BNE` at $B88D is taken; on page 1 `cmd_ev_set_voicing` runs. Its rom-engine golden has 1,845 frames. `apollo2_coverage` cannot show $B88D taken (its target $B895 is also reached from $B87D and $B885); the branch probe (a scratch tool, not committed) showed it taken 1,134 times. `golden_hybrid` and `diff_play` run it like any other text.
- **`diff_play`** (§4.1):
  - The hook at $B2C7 (`boot_play_body`'s `JSR par_play`) arms the next $B877 entry only when the `boot_loop_play` call around it is checked; the hook at $B877 acts on an armed entry and disarms. The boot's own `boot_loop_play` call is recognised by its return address $A7BD.
  - The injected pairs go to each run's first call of each routine whose ROM copy, uninjected, swapped the buffers at least twice. The reply: `ser_status_due` = 1 in each copy's frame callback at its second swap. The mute: `ser_abort` bit 0 set by a hook at `frm_finish_frame` $B9B5 on each copy, at its first entry after the second swap (in copy B, a ROM call from the C).
  - Copy A's swap states are kept and copy B's compared as they come, so a difference names the first swap. Copy A has a budget of 1 G cycles. Copy B, the C thunk on a bridge bound to that copy, has none as a whole (each of its ROM calls has `FW_ROM_CALL_BUDGET`), so a C wait that never saw the flag would hang the suite instead of failing it.
  - Hooks on copy A at $B8A5 and $B89C count the copy-A runs that reached them. The first 10 mismatches are printed (file, routine, call, injection, then what differs: the swap and the first byte, or the exit state); any mismatch fails the suite.
  - The self-check (ROM against ROM, hello) requires both routines called, one injected call each and both branches reached; it prints `par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2`.
  - On the 45 texts and the boot run it prints `boot_loop_play 175 calls checked, 46 with both injections`, `par_play 124 calls checked, 46 with both injections` and `$B8A5 (abort) reached in 92 calls, $B89C (reply due) in 92`, with 0 mismatches: one injected call per routine per run, and 92 is the number of injected runs of each kind (a `boot_loop_play` copy runs the ROM `par_play` inside it). The suite itself requires exactly two `waits` replacements, each with more than 0 calls, both reach counts above 0, and 0 mismatches.
- **The unit-test harness** (`tests/test_fw_par.c`, §4.5): `MAX_CALLS` 64 → 1024, and the call record gains `frames` (`par_frames_left` at the call); the longest test, `test_play_zero_frames_tail`, makes 528 calls. The mock answers the loop's ROM callees with the registers the ROM ones leave: `isr_par_ready` (counted: it sets `isr_frame_flag` at every n-th call, `g_flag_every`, and `ser_abort` bit 0 at one call, `g_abort_at`), `isr_par_hold`, `ser_tx_service`, `frm_finish_frame` and `isr_speech_stop` (all three kept), `isr_wait_frame` (the flag cleared), `isr_speech_start` (A = X = 0, the flag cleared), `isr_volume_half` (on into `isr_set_volume` with `cmd_volume` >> 1), `isr_set_volume` (A = the new value of a modelled P3, `g_p3`), `frm_snapshot` (A = the fill frame's b10, X = 0, Y = 10) and `frm_transition_flags` (A = `frm_flags`, Y = $0D), besides M4.4a's three `frm_*` cases; any other address fails. Its `read_sfr` gives `g_uart_status` for $E7 ($02, transmitter ready, after `setup`) and 0 for any other SFR. The three zero-frame wraps have a test each: `test_play_zero_frames_main` ($B8FB from 0), `_segment` (`par_next_segment` leaving 0, back to $B8B1 by the `BEQ` at $B906) and `_tail` ($B8BB from 0: 255 frames).
- **`tests/test_fw_tn.c`'s flush** (§4.5) checks the whole C player, not only its first calls: the mock sets `isr_frame_flag` at every `isr_par_ready` and `ser_abort` bit 0 at `isr_speech_start`, so the C `par_play` goes from its first pass to the three rest frames. The test checks that the ROM `boot_loop_play` is not called; the last 32 calls in order (the C player's 27, then `isr_par_ready` for text[250..254]); `isr_par_hold`'s A = $FF, X = `TN_PHONETIC_FLUSH` and Y = $70; A at both `isr_set_volume` calls; every budget `FW_ROM_CALL_BUDGET`; `cmd_ev_pos` = $FFFF (`par_play_start` ran); and 1,379 `isr_par_ready` calls in all.
