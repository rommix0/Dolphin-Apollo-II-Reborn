# M4.4a — The playback set-up in C (segments, targets, boundaries) — Design

**Status:** design, awaiting user review
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 11): §6 step 4, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`: the C `cmd_apply_pitch`, which calls `par_setup_voice`.
- `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md`: the segment arrays this milestone reads (§2.5, the output contract to playback; §6 "As built").
- `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md`: the conventions this milestone keeps (§2.3–§2.7).

**User decisions (2026-10-07):**
- M4.4 is split in two plans. **M4.4a** (this spec) is the playback set-up: everything that runs once per utterance or once per segment. **M4.4b** is the frame loop (`par_play`, the track steppers, `util_interp_step`, the command events and `boot_loop_play`).
- Hooks: all 20 routines, so that a mismatch points at one routine (as in M4.0–M4.3b). `MACHINE_MAX_HOOKS` rises to 256, enough for every routine left through M4.6.
- The C is one static helper per ROM block, each carrying its ROM address range, as in `pros_gen.c` and `pros_stress.c`. No `goto` transliteration and no semantic rewrite.
- First, one static-inline header `src/firmware/fw_ops.h` replaces the helpers duplicated across the `pros_*.c` files (the deferred M4.3b item).
- Coverage gaps are closed with three new corpus texts (`pitch_extremes`, `voicing_levels`, `fast_effect`; rom-engine goldens; the existing goldens stay unchanged), and with unit tests for what no text reaches.
- The voice tables are converted as one row per phoneme, both voices in full.
- Playback timing needs no new check: the produced frames are captured only at a buffer swap, which the timer interrupt makes only after `par_play` has finished a frame (§2.7). This replaces the frame-build check first proposed, which could never fail.

## 1. Goal

When M4.4a is done, the playback set-up runs as C in the `hybrid` engine:
- at the start of an utterance, `par_play_start` resets the tracks and loads the first segment's phoneme and pitch;
- at each segment, `par_next_segment` reads the next segment, scales its pitch target, takes its ten parameter targets from the voice table, works out the boundary values and transition durations by the dominance rules, and plans the tracks;
- `par_setup_voice` sets up the voice table pointers and the rest frames, and the C `cmd_apply_pitch` calls it directly;
- every one of the 20 routines matches its ROM original at every call site the corpus reaches.

The frame loop that calls `par_next_segment` (`par_play`), the track steppers and the frame builder stay ROM (M4.4b, M4.5).

## 2. The subsystem (all main ROM)

### 2.1 Where it sits
- `boot_loop_play` $A7D4 (ROM until M4.4b) plays an utterance when $6B is set: `isr_par_hold`, then **`par_play_start`** $BAF2, `isr_speech_start`, the volume ramp, then the frame loop `par_play` $B877.
- `par_play` counts down `par_frames_left` once per frame. At zero it calls **`par_next_segment`** $BCF3 ($B900), then steps the tracks (`par_step_tracks`, M4.4b) and finishes the frame (`frm_finish_frame`, M4.5).
- The input is M4.3a's output contract: the segment arrays (phoneme $0200, duration, pitch $0A00, flags) through the playback pointers `pros_phoneme_ptr` $22, `pros_duration_ptr` $24, `pros_pitch_ptr` $26 and `pros_flags_ptr` $28, and `pros_seg_count`. During a @T sound effect, `cmd_ev_sound_start` (ROM, M4.4b) points them into `cmd_sound_data` in ROM.
- The output is the state the frame loop reads: `par_seg_target` $17BF, `par_frames_left`, the per-parameter track arrays $16EC–$178C, `pros_play_target`/`pros_play_pitch`, `par_seg_index` and `par_next_phoneme`/`par_cur_phoneme`.
- `par_setup_voice` $B3A4 is called by `cmd_apply_pitch` (ROM $A845; C since M4.2, through `fw_rom_call` until now) whenever the voice, speaker or pitch centre changes, and at boot.

### 2.2 Ported to C (20 routines, 1,361 bytes, 642 instructions)

Every routine is contiguous. None has a second entry, code before its entry, a fall-through, a branch or jump into another routine, a stack trick, self-modifying code or decimal mode. No caller reads a flag after any of them, and V is never tested in the firmware.

**Utterance start**
- **`par_play_start` $BAF2** (67 B, 25 instructions).
  1. It calls `par_reset_tracks` and `pros_reset_seg_ptrs` (C since M4.3a).
  2. It loads `pros_play_target` from `pros_seg_pitch[0]` ($0A00, absolute) and calls `pros_scale_target`. It applies no microprosody to this first target.
  3. It sets `par_next_phoneme` and `par_cur_phoneme` from `pros_seg_phoneme[0]` ($0200), and both ranks from the voice table through `par_rank_ptr` ($BB0B).
  4. It sets `cmd_ev_pos` to $FFFF, or to `cmd_ev_positions[0]` when `cmd_ev_count` ≠ 0, then clears `cmd_ev_next` and `pros_pitch_err`.

  It leaves A = 0 and Y = `pros_seg_phoneme[0]`, and never writes X.
- **`par_reset_tracks` $BB35** (63 B, 34 instructions). It sets `par_seg_index` = 0 and `par_frames_left` = 1. For X = 10…1 it clears `par_next_start_dur[X]` and loads `frm_snap[X−1]` from `frm_rest_frame[frm_snapshot_map[X−1]]`. It sets `par_boundary[1..4]` = $20 and `par_boundary[5..10]` = 1. It preserves A, X and Y (PHA/PLA).
- **`par_setup_voice` $B3A4** (121 B, 47 instructions).
  1. It sets `par_row_len` = `par_count`, and `par_table_copy`, `par_rank_ptr` and `par_row_ptr` to `par_table_ptr`.
  2. Two `par_next_row` calls give `par_row2_ptr` (base + N) and `par_params_ptr` (base + 2N).
  3. It computes `par_param_stride` = 5N in 16 bits.
  4. It sets `isr_play_ptr`, `isr_fill_ptr` and `isr_swap_phase` by `LDM`, then calls `frm_build_rest_frame`, `frm_copy_rest_frame` and `frm_clamp_mark_space`.
  5. It swaps the two buffer pointers by `LDM`, then calls `frm_copy_rest_frame` and `frm_clamp_mark_space` again.

  It leaves A = $0B, X = 9 and Y = $0B, from its ROM calls.

**Per segment**
- **`par_next_segment` $BCF3** (20 B, 8 instructions). It sets `par_row_ptr` = `par_params_ptr`, then calls `par_read_segment`, `par_boundary_pass` and `par_plan_tracks`.
- **`par_read_segment` $BD33** (160 B, 68 instructions).
  1. The playing segment becomes the current one (rank and phoneme).
  2. It increments `par_seg_index`. When the low byte wraps, it increments its high byte and the high bytes of the four playback pointers, in the order phoneme, pitch, duration, flags.
  3. It reads the next phoneme and pitch target at the index. It keeps the old target in `pros_play_pitch`, then calls `pros_scale_target` and `pros_microprosody`.
  4. It reads the duration at index − 1. It does this with a temporary `DEC`/`INC` of `pros_duration_ptr+1` at $BD6E/$BD7E when the index is 0. The duration goes to `par_frames_left` and to `par_seg_target[0]`.
  5. For P1…P10, it takes the current phoneme's target from the voice table. It adds the @u offset `cmd_formant_ofs[X−1]` unless the target is ≥ $80 or < 3, clamps the result (`par_clamp`), scales P10 by @B (`par_scale_voicing`), and stores it in `par_seg_target[X]`.
  6. It copies `par_next_start_dur` into `par_start_dur`, and `par_boundary` into `par_start_val` and `par_line_val`.
  7. It decrements `pros_seg_count` in 16 bits.

  It leaves A = `par_boundary[1]`, **X = 0** and Y = 0.
- **`par_next_row` $BD07** (13 B, 7 instructions): `par_row_ptr` += `par_row_len` in 16 bits. It returns A = the new low byte.
- **`par_next_param` $BD14** (16 B, 8 instructions): `par_row_ptr` += `par_param_stride` in 16 bits. It returns A = the new high byte.
- **`par_clamp` $BD24** (15 B, 9 instructions, three `RTS`). It returns 1 when the N flag of A − 1 is set (A = 0 or $81–$FF), $3F when the N flag of A − $40 is clear (A = $40–$80), and A otherwise.
- **`par_scale_voicing` $BFC3** (118 B, 82 instructions, nine `RTS`). It scales a voicing value v by `cmd_voicing` (@B 0–8): 1 and 8 pass unchanged; otherwise @B0 → 2, @B1 → v/4, @B2 → v/16 + v/4 + 1, @B3 → v/8 + v/4, @B4 → v/16 + v/8 + v/4, @B5 → v/2, @B6 → v/8 + v/2, @B7 → v/4 + v/2, @B8 and above → v. At @B1–3, v = 0 gives 2. Paths 2, 3, 4, 6 and 7 use `FW_TMP_76` as scratch.

**Pitch at playback**
- **`pros_scale_target` $BDD3** (151 B, 73 instructions). It scales the deviation of `pros_play_target` from 128 by R/4 around the @F centre `pros_pitch_centre`, where R is `cmd_source_prosody` bits 0–2, limited by `cmd_speed_limit`.
  - The deviation is split into a quarter ($1814) and two fraction bits ($1813).
  - k = R & 3 additions are chained with carry into $1812/$1811, plus the whole deviation when R ≥ 4.
  - The sum is rounded by `ASL $1812` / `ADC #0`.
  - The result saturates to $FF above the centre or to 4 below it, and a final signed test sets bit 2.

  It preserves X and Y, and returns **A = the entry Y**.
- **`pros_microprosody` $B983** (50 B, 26 instructions). For the next phonemes $15, $18, $20 and $41 it adds 5 to `pros_play_target`; for $1F and $0D it subtracts 5. Either way, it stores the result only when the add or subtract does not wrap.

**Boundaries and tracks**
- **`par_boundary_pass` $BE6A** (42 B, 20 instructions). It reads the next phoneme's rank. When both ranks are ≥ 29, it calls `par_boundary_jump`. Otherwise it calls `par_boundary_next_dom` when the next rank is ≥ the current one, and `par_boundary_cur_dom` when it is not.
- **`par_boundary_jump` $BE94** (33 B, 14 instructions). It clears `par_next_start_dur[1..10]` and `par_trans_dur`, then stores the next phoneme's ten raw targets (no @u offset, no clamp) through `par_store_boundary`, moving on one parameter block each time.
- **`par_store_boundary` $BEB5** (45 B, 19 instructions). With X = p:
  1. It stores `par_boundary[p+1]` = A.
  2. It shortens `par_trans_dur` to `par_frames_left` when `par_start_dur[p+1]` is 0 and `par_frames_left` is smaller.
  3. When `par_trans_dur` is still 0 after that, it shortens `par_start_dur[p+1]` to `par_frames_left` when the latter is smaller.
  4. It stores `par_end_dur[p+1]` = `par_trans_dur`, then increments X.
- **`par_boundary_cur_dom` $BEE2 / `par_boundary_next_dom` $BF27** (69 B and 28 instructions each). They set the target (dominated) and dominant phonemes: the current phoneme dominates in `cur_dom`, the next one in `next_dom`. Then, for each of the ten parameters:
  1. they set `par_ofs` to the parameter's @u offset;
  2. they call `par_read_dominance`;
  3. they read the dominant phoneme's two durations, each scaled by `par_scale_duration`, into `par_next_start_dur[p+1]` and `par_trans_dur`. `next_dom` takes the two the other way round.
  4. they call `par_boundary_value`.
- **`par_read_dominance` $BF6C** (87 B, 43 instructions).
  1. The dominated target is the target row plus `par_ofs`, clamped.
  2. The dominant phoneme's fixed contribution is read, and scaled by @B for P10.
  3. The dominant phoneme's proportion is read.
  4. The fixed contribution gets the @u offset weighted by 8 − proportion. That is an 8-bit sum, divided by 8, whose sign comes from the carry of the last add.
- **`par_boundary_value` $C039** (153 B, 70 instructions).
  1. The boundary value is the fixed contribution plus a proportion (in eighths) of the dominated target plus `par_target_bias[p]`, computed with halves and quarters, chained carries and a 9-bit `ROR`.
  2. The result is limited to 1…$3F (0 or ≥ $40 gives 1).
  3. It is stored through `par_store_boundary`.
- **`par_scale_duration` $C29C** (24 B, 13 instructions). A duration of 0 passes. Otherwise, at speed ≥ 5 (`cmd_speed`), it maps the duration through `cmd_speed_map` (index capped at 31). It saves Y in `FW_TMP_78` whenever the duration is not 0.
- **`par_plan_tracks` $C0D2** (45 B, 20 instructions). For each parameter:
  - It sets `par_sequential` = 0 and `par_end_val` = the target.
  - It sets `par_end_wait` = `par_frames_left` − `par_end_dur` (0 when negative, signed).
  - When `par_end_wait` − `par_start_dur` is not negative (signed), it sets `par_steady` to that difference and `par_sequential` = 1. Otherwise `par_steady` keeps its stale value.

**C calls C.** In C:
- `par_next_segment` calls `par_read_segment`, `par_boundary_pass` and `par_plan_tracks` directly, and so on down the tree.
- `par_play_start` calls the C `pros_reset_seg_ptrs`.
- The C `cmd_apply_pitch` calls the C `par_setup_voice` instead of `fw_rom_call(ROM_PAR_SETUP_VOICE)`. The registers it hands back stay what the ROM would leave (A = $0B, X = 9, Y = $0B; `cmd_apply.h` already states them).

**Registers in C.**
- A routine that reads an entry register takes it as a parameter: A for `par_clamp`, `par_scale_voicing`, `par_store_boundary` and `par_scale_duration`; X for the boundary routines.
- A routine whose exit registers a caller reads returns them.
- The **running X** of the boundary pass is explicit. `par_read_segment` leaves X = 0. `par_boundary_jump`, `cur_dom` and `next_dom` never set X, and pass it to `par_store_boundary` and `par_boundary_value`, which index with it and increment it. The dominance loop's own counter `par_param_index` agrees with X only because X enters as 0. In C the pass takes X from its caller (the C `par_read_segment`'s 0, or the machine's X in a thunk) and does not assume 0.

### 2.3 Hooks (20 new replacements; 48 in all, 49 `golden_hybrid` modes)

Declared outputs: **A, X and Y for all 20**. Each thunk's comment cites what its ROM callers read after the call:

| Hook | ROM callers | Read after the call / reached in "all" mode |
|---|---|---|
| `par_play_start` $BAF2 | `boot_play_body` $B2B6 | Nothing (`isr_speech_start` writes A and X first; Y is unread until `par_play`'s `LDY` at $B8B1). Fires in "all" mode while `boot_loop_play` is ROM. |
| `par_next_segment` $BCF3 | `par_play` $B900 | Nothing (`LDA` at $B903; `par_step_tracks` loads X and Y). Fires in "all" mode while `par_play` is ROM. |
| `par_next_param` $BD14 | `frm_build_rest_frame` $B431, `par_read_segment` $BD9D, `par_boundary_jump` $BEAC | X ($B434 `INX`, $BDA0 `CPX`), Y. Fires in "all" mode from the ROM `frm_build_rest_frame`, also inside the C `par_setup_voice`'s ROM call (a nested thunk; `run_thunk` saves the outer trap). |
| `par_clamp` $BD24 | `frm_raise_formants` $B970, $B97D; `par_read_segment` $BD8F; `par_read_dominance` $BF75; `par_boundary_value` $C0BF | A, and **Y** (`STA (2c),Y` right after at $B973/$B980). Fires in "all" mode from the ROM `frm_raise_formants`. |
| `par_setup_voice` $B3A4 | `cmd_apply_pitch` $A845 | A (restored by `PLA`), and X and Y, which reach RAM through `boot_reset` → `bank_load_ramcode` (`STY $78` / `STX $76`). Alone only, once the C `cmd_apply_pitch` calls the C directly. |
| `par_read_segment` $BD33 | `par_next_segment` $BCFD | **X = 0**, read by the boundary pass. |
| `pros_scale_target` $BDD3 | `par_play_start` $BAFE, `par_read_segment` $BD64 | X ($BD76), Y ($BD6A, $BD70). |
| `pros_microprosody` $B983 | `par_read_segment` $BD67 | X, Y. |
| `par_next_row` $BD07 | `par_setup_voice` ×2, `cur_dom` ×2, `next_dom` ×2, `par_read_dominance` ×3 | A ($B3C1, $B3CA), X, Y. |
| `par_scale_voicing` $BFC3 | `par_read_segment` $BD96, `par_read_dominance` $BF87 | A, X, Y. |
| `par_boundary_pass` $BE6A | `par_next_segment` $BD00 | Nothing. |
| `par_boundary_jump` $BE94 | `par_boundary_pass` $BE7F | Nothing. |
| `par_store_boundary` $BEB5 | `par_boundary_jump` $BEA9, `par_boundary_value` $C0AE | **X + 1**, Y. |
| `par_boundary_cur_dom` $BEE2 | `par_boundary_pass` $BE90 | Nothing. |
| `par_boundary_next_dom` $BF27 | `par_boundary_pass` $BE8C | Nothing. |
| `par_read_dominance` $BF6C | `cur_dom` $BEFE, `next_dom` $BF43 | X = p, **Y = the dominant phoneme**. |
| `par_boundary_value` $C039 | `cur_dom` $BF19, `next_dom` $BF5E | X + 1 (the next iteration's `TXA`/`PHA`). |
| `par_scale_duration` $C29C | `cur_dom` $BF03, $BF0E; `next_dom` $BF48, $BF53 | A, X, Y. |
| `par_plan_tracks` $C0D2 | `par_next_segment` $BD03 | Nothing. |
| `par_reset_tracks` $BB35 | `par_play_start` $BAF2 | X, Y (preserved). |

- Where nothing is read, the thunk still sets A, X and Y to the ROM's exit values, as `pros_phrase_gen`'s does.
- Apart from the four hooks that fire in "all" mode, each routine is reached from ROM only while its C caller is ROM, that is, in its "alone" mode.
- **Hook budget.** `MACHINE_MAX_HOOKS` (src/machine/machine.h) rises from 32 to 256: `diff_lts` installs 48 observers, and `golden_hybrid` 48 replacements plus its `isr_par_ready` hook. `DIFF_MAX` (tests/test_diff_lts.c) rises from 28 to 48. `hook_map` already filters the lookup, so the larger table costs memory only. The test for the full table moves to the new limit.

### 2.4 ROM calls from C

| ROM routine | Called by | Budget | Why it stays ROM |
|---|---|---|---|
| `frm_build_rest_frame` $B41D | `par_setup_voice` ($B404) | `FW_ROM_CALL_BUDGET` | Frame builder (M4.5). It reads `frm_template`, the pause phoneme's P1–P4 targets through `par_row_ptr`, and calls the hooked `par_next_param` four times. It returns X = 9. |
| `frm_copy_rest_frame` $B445 | `par_setup_voice` ($B407, $B416) | `FW_ROM_CALL_BUDGET` | M4.5. It copies `frm_rest_frame` to the buffer at `isr_fill_ptr`. |
| `frm_clamp_mark_space` $B930 | `par_setup_voice` ($B40A, $B419) | `FW_ROM_CALL_BUDGET` | M4.5. It returns A = Y = $0B. |

- None of the three reads its entry registers. The C passes the registers the ROM would have, and carries X = 9 from `frm_build_rest_frame` to the exit.
- The C writes `isr_play_ptr`, `isr_fill_ptr` and `isr_swap_phase` in the ROM's order before each call, because the callees write through `isr_fill_ptr`.
- None of the 20 calls `isr_par_ready`, plays audio, waits on an interrupt or resets the stack: there is no unwind.
- New addresses go in `src/firmware/par_addr.h`. `ROM_PAR_SETUP_VOICE` moves there from `cmd_addr.h`, as `ROM_PROS_MAKE_SEGMENTS` moved in M4.3b.

### 2.5 The output contract (the M4.4b boundary), kept bit for bit
- `par_seg_target[0..10]`, `par_frames_left`, `par_seg_index` (16 bits), `par_next_phoneme`/`par_cur_phoneme` and their ranks.
- The track arrays: `par_boundary`, `par_start_val`, `par_line_val`, `par_next_start_dur`, `par_start_dur`, `par_end_dur`, `par_sequential`, `par_end_val`, `par_end_wait` and `par_steady`. Each is 1-based, with entry 0 not written.
- `pros_play_target`, `pros_play_pitch` and `pros_pitch_err`.
- `par_row_ptr` and the playback pointers, including the high-byte moves.
- `frm_snap`, the event start `cmd_ev_pos`/`cmd_ev_next`, the rest frames in both buffers, and the voice set-up pointers.
- Every scratch byte, the write-only ones included: `par_row2_ptr`, `par_table_copy`, `par_seg_target[0]`, $1811–$1814, `FW_TMP_76`, `FW_TMP_78` and `FW_TMP_75` (through the ROM calls).

### 2.6 RAM names and shared helpers
- **`fw.h` names** for the four bytes `fw_ram.h` leaves unnamed, all used only by `pros_scale_target`:
  - `FW_PROS_SCALE_DEV` $1811: the scaled deviation added to or subtracted from the centre;
  - `FW_PROS_SCALE_FRAC` $1812: the fraction accumulator, whose bit 7 rounds;
  - `FW_PROS_DEV_FRAC` $1813: the deviation's two low bits, in bits 7–6;
  - `FW_PROS_DEV_QUARTER` $1814: the deviation >> 2.

  The comments of `FW_TMP_75`, `FW_TMP_76` and `FW_TMP_78` gain their playback meanings. The plan confirms the set against the dumps.
- **`src/firmware/fw_ops.h`** holds, as `static inline`:
  - `fw_par_ready` (the `isr_par_ready` ROM call);
  - `fw_adc` (8-bit add with carry in and out);
  - `fw_inc8`;
  - `fw_add8` (an 8-bit add into a RAM array byte).

  `pros_gen.c`, `pros_phrase.c`, `pros_seg.c` and `pros_stress.c` drop their copies. The new files use it for any further shared steps (e.g. 16-bit pointer adds).

### 2.7 Quirks kept
- **The running X** of the boundary pass (§2.2).
- **`par_clamp`'s "signed" tests** are the N flag of an 8-bit difference: $80 gives $3F, and $81–$FF and 0 give 1. A target plus a @u offset that passes $80 is clamped by that rule.
- **`par_read_segment`.**
  - A target ≥ $80 or < 3 gets no @u offset, but is still clamped.
  - P10 is clamped, then scaled.
  - The index − 1 duration read uses the temporary `DEC`/`INC` of the pointer's high byte.
  - On an index page wrap, the high bytes of the index and of the four pointers move, the flags pointer included, though nothing here reads it.
- **The weighted @u offset** ($BFA6–$BFBA): an 8-bit sum. Its sign comes from the carry of the last add only.
- **`par_ofs_weight` = 8 − proportion wraps** for a proportion > 8, making 255 loop steps. The tables hold 0–8 for every reachable phoneme, so this is latent.
- **`par_boundary_value`.**
  - The 8-bit target + bias for proportion 8 and for proportion & 3 = 0.
  - `_half` = (x << 1 & $FF) >> 2, which drops bit 7.
  - The carry chains through the halves loops.
  - The `ROR` at $C09B takes the 9th bit of the sum.
  - The rounding `ADC #0`.
  - Proportion 0 is clamped without the 0/≥ $40 test.
  - The ≥ $40 test is unsigned.
- **`par_plan_tracks`:** the signed `BPL`/`BMI` on 8-bit differences; `par_steady` stays stale on the overlap path.
- **`par_store_boundary`:** unsigned compares; the second cut only when `par_trans_dur` is still 0.
- **`par_scale_duration`:** 0 leaves `FW_TMP_78` unwritten; below speed 5 it writes `FW_TMP_78` but leaves A; the index is capped at 31.
- **`par_scale_voicing`:** 1 and 8 pass at every @B; v = 0 gives 2 at @B1–3 but 0 at @B4–7 (@B0 gives 2 for every v except 1 and 8); @B2's + 1 comes from `SEC`/`ADC`; each term truncates.
- **`pros_scale_target`.**
  - **The Z flag at $BE06.** The `BEQ` tests the Z left by `CMP cmd_speed_limit` when R < limit, not R itself. So with R = 0 below the limit, it runs on through $BE0C/$BE58 and writes $1811 = 0, with the same result as the centre path.
  - The signed `CMP #4`/`BPL` on R.
  - The chained adds and the `ASL` rounding.
  - The saturation to $FF or 4.
  - The final `ORA #4` for results 0–3 and ≥ $84.
  - The exit A = entry Y.
- **`pros_microprosody`:** the ± 5 is not stored on a wrap, but A still holds the wrapped value.
- **The first segment.** `par_play_start` scales pitch[0] without microprosody. `par_frames_left` = 1, so the first frame calls `par_next_segment` at once, and `pros_play_pitch` is stale until then.
- **Reads past the 86 phonemes.** At the end of a @T effect, `par_read_segment` reads the $FF end marker as the next phoneme, and the pitch byte one past the effect's pitch array.
  - The boundary pass then reads the rank and the ten targets at index $FF of the voice table. Those bytes fall in later rows, inside the table.
  - Rank[$FF] is 32 in both voices, so the jump path stores those raw bytes as boundaries.
  - The C reads the same bytes through `fw_rom8`/`fw_ind_y`.
  - The playback pointers point at RAM normally and into ROM during an effect, so the C reads them with `fw_ind_y`, never `fw_rd8`.
- **Dead stores** kept: `par_row2_ptr`, `par_table_copy`, the stride low byte at $B3D4, `par_seg_target[0]`, `pros_pitch_err` and the $1811 write above. Dead `CLC`s and `AND #0` need no C.
- **Timing.**
  - The C takes no CPU cycles, and in playback that cannot change the produced frames.
    - The timer interrupt `isr_timer_dsp` swaps the frame buffers, and the machine captures a produced frame, only at `isr_swap_buffers` $B306. It gets there only when `isr_frame_flag` is clear (`BBC0` at $B300).
    - `par_play` clears the flag only after it has built the whole frame ($B8C6, $B911).
    - So a build that runs late makes the DSP replay the playing frame (the raw stream, which is not compared), and no frame is captured. A faster build changes nothing.
    - `test_timing_invariance` (`tests/test_golden.c`) already checks this, with the timers 4× slower and 3× faster than the CPU.
  - For the record: in ROM the longest frame build, a segment change included, is 16,007 cycles on the corpus (`long_words`), against a frame period of 20,000.
  - Serial arrival may move relative to the main level, as in M4.1–M4.3b. The goldens are the check.

## 3. Converted data (`firmware_data` 38,197 → 47,692 bytes)

| ROM | Table | Form |
|---|---|---|
| $D46A–$E5E1, $E5E3–$F75A | `par_voice_a`, `par_voice_b` (4,472 bytes each) | **One row per phoneme 0–85, both voices in full** (`src/firmware/data/par_data.c`). Each row holds the rank, the spare byte, and for P1…P10 the five values {target, fixed contribution, proportion 0–8, internal duration, external duration}. A comment gives the phoneme's name where the annotations give one. The encoder writes them back in the ROM's row order (rank row, spare row, then five rows per parameter, each N = 86 bytes) and asserts the size. This joins `$D44A`–`$F77A` into one converted range. |
| $B7A7–$B7B0 | `par_target_bias` (10) | Per parameter P1…P10, with the parameter names (non-zero only for FN, F1, F2, F3). |
| $B773–$B77C | `frm_snapshot_map` (10) | Per snapshot entry, the frame byte offset, by name (b10, AHF, A3, A2, A1, ALF, FN, F1, F2, F3). |
| $9F74–$9FFB | `cmd_sound_table` (136) | Per @T effect 0–16, its four arrays, as offsets into `cmd_sound_data` by name. |
| $9FFC–$A186 | `cmd_sound_data` (395) | The shared flag array, then per effect its phoneme array (ending in $FF), duration array and pitch-target array, as numbers. The encoder lays them out end to end and asserts they fill the range exactly. This joins $9804–$A396 into one range. |

- No ROM bytes appear as hex blobs. `tests/test_firmware_data.c` checks every converted byte against the ROM, and its range list (`k_rom`) is updated.
- The @T tables are in `src/firmware/data/cmd_data.c`, next to the other command tables. `cmd_sound_table` is read only by `cmd_ev_sound_start` (M4.4b). It is converted now so that all the @T data comes in one step.
- Not converted here: `par_slot_map`, `frm_template` and `frm_b14_15_*` (read only by ROM routines, M4.4b/M4.5).

## 4. Verification

### 4.1 New corpus texts (goldens from the rom engine, once)
| File | Text | Reaches |
|---|---|---|
| `pitch_extremes` | `@R7 @FF Take a cake. @F0 Do a deck.` | `pros_scale_target`: R ≥ 4 ($BE0A taken), the cap at $FF ($BE3D taken), the floor at 4 ($BE63 not taken). `pros_microprosody`: the + 5 that wraps after a capped target ($B9A5 not taken) and the − 5 that borrows ($B9AE not taken). |
| `voicing_levels` | `@B0 Hi, there. @B2 Hi, there. @B3 Hi, there. @B5 Hi, there. @u937- Hello.` | `par_scale_voicing`: @B0, @B2, @B3 and @B5 ($BFCE, $BFE0, $BFF1, $C017 not taken); v = 0 under @B2 and @B3, where the pause dominates a vowel ($BFE3, $BFF4 taken); and v = 8 from a P10 target of 63 with the @u offset $C9 ($BFC5 taken). |
| `fast_effect` | `@W9 @TC Done.` | `par_scale_duration` $C2AA not taken: in @T effect 12, phoneme 83 dominates 82 and its internal duration 40 passes the cap at 31. |

- The corpus goes from 41 to 44 texts, so `golden_hybrid` prints 45 files, boot included.
- With these three texts, every conditional branch in the 20 routines is seen both ways, except $BD85 (§4.4). This was confirmed with the branch probe while designing, on the combined text alone and with the corpus. The plan confirms the instruction targets with `apollo2_coverage` before the goldens are written.

### 4.2 `diff_lts`
- Checks all 48 routines, each more than 0 times, with 0 mismatches.
- The routines under `boot_loop_play` are observed on the ROM machine. None of the 20 waits on an interrupt.

### 4.3 `golden_hybrid`
Every file × 49 modes (all routines, then each alone), byte for byte. This is about 4 minutes, against 2.5 today.

### 4.4 Unit tests (`tests/test_fw_par.c`)
- One or more cases per routine. The registers in and out follow §2.3, and the RAM is checked byte for byte against expected values worked out from the dumps.
- What no text reaches:
  - **`BMI` at $BD85 taken** (a target ≥ $80 gets no @u offset). Unreachable: the current phoneme is 0–84 (the lists give 0–76, pauses 59, the @T effects 59, 77 and 82–84), and every target byte of both voices at indices 0–88 is 1–63. The first ≥ $80 is at index 89. An effect's $FF end marker never becomes the current phoneme, because `cmd_ev_sound_end` runs first; even so, the ten target bytes at index 255 are all below $80. The test sets a phoneme row with a target ≥ $80.
  - The proportion > 8 weight wrap.
  - `par_clamp` at the edges 0, 1, $3F, $40, $80, $81 and $FF.
  - The `ROR` at $C09B with a 9-bit sum.
  - `par_plan_tracks` with a difference ≥ $80.
  - The 16-bit index wrap in `par_read_segment` (pointers moving) with the duration read at index 0.
- Also pinned:
  - the running X entering the boundary pass non-zero (the thunk case);
  - `pros_scale_target`'s exit A = entry Y, and the R = 0 < limit path writing $1811;
  - `par_setup_voice`'s ROM calls in order, with the buffer pointers set before each;
  - the C `cmd_apply_pitch` calling the C `par_setup_voice`. The cases in `tests/test_fw_cmd.c` that check a recorded `fw_rom_call(ROM_PAR_SETUP_VOICE)` (with its A, X and Y) instead check that the C ran: its five `frm_*` ROM calls in order, the voice pointers and the stride it sets, and the registers handed back.

### 4.5 Done criteria
1. `diff_lts` checks all 48 routines more than 0 times each, with 0 mismatches.
2. `golden_hybrid` reproduces every file × 49 modes byte for byte, and every existing golden is unchanged.
3. `firmware_data` passes at 47,692 converted bytes.
4. The three new goldens exist and reach what §4.1 lists.
5. `apollo2 --engine hybrid` equals `--engine rom` for every new text.
6. `src/firmware/` still builds without `machine/` or `cpu740/` headers. No unexpected `fw_rom8` fault occurs, and `cmd_apply_pitch` no longer calls `ROM_PAR_SETUP_VOICE`.
7. `build.bat test` is green.
8. The docs are updated:
   - the parent spec gets rev 12;
   - this spec's status changes, with "As built" if anything differs;
   - the README's hybrid section lists the playback set-up.

### 4.6 Task outline (for the plan)
1. `fw_ops.h` and the four `pros_*.c` files moved onto it. `MACHINE_MAX_HOOKS` 256 and `DIFF_MAX` 48.
2. The three corpus texts and their goldens.
3. The data (voice tables, `par_target_bias`, `frm_snapshot_map`, the @T tables).
4. The leaves: `par_next_row`, `par_next_param`, `par_clamp`, `par_scale_voicing`, `par_scale_duration`, `pros_scale_target`, `pros_microprosody`.
5. The boundary pass: `par_store_boundary`, `par_read_dominance`, `par_boundary_value`, `cur_dom`, `next_dom`, `par_boundary_jump`, `par_boundary_pass`.
6. The top: `par_read_segment`, `par_plan_tracks`, `par_next_segment`, `par_reset_tracks`, `par_play_start`, `par_setup_voice` (with the C `cmd_apply_pitch` calling it).
7. Spec rev 12, the status, "As built" and the README.

## 5. Out of scope
- The frame loop and everything per frame (M4.4b): `par_play`, `par_step_tracks`, `par_track_*`, `util_interp_step`, `cmd_run_event`, the `cmd_ev_*` handlers, `boot_loop_play`, `util_shl3`/`shl4`, and `par_slot_map`.
- The frame builder (M4.5): every `frm_*` routine and its tables.
- `isr_*`, `ser_*`, `boot_*` and `bank_*` (M4.6).
- `pros_pitch_code_dither` $C131, which is unreferenced and the only reader of `pros_pitch_err`.
