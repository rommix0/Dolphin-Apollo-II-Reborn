# M4.5 — The frame builder in C (frm_*) — Design

**Status:** design, awaiting user review
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 13): §1.4 (the frame bytes), §6 step 4, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md`: `par_setup_voice` (the voice set-up that builds the rest frame), its conventions (§2.3–§2.7) and "As built" (§6).
- `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`: `par_play`, which finishes every frame; `diff_play` (§4.1) and "As built" (§6).

**User decisions (2026-10-08):**
- One plan for the whole frame builder (seven routines, 470 bytes).
- Hooks: all seven routines, as in M4.4a and M4.4b (every routine, so that a mismatch points at one routine). That makes 71 replacements and 72 `golden_hybrid` modes.
- The three formula tables (`frm_product_limit`, `frm_b14_15_lo`, `frm_b14_15_hi`) are converted as formulas that the encoder expands, not written out (§3).
- Two new corpus texts and their rom-engine goldens, for the two pieces of the frame builder no text reaches (§4.5): `formant_extremes` and `mark_space_cap`. The existing goldens stay unchanged.

## 1. Goal

When M4.5 is done, the frame builder runs as C in the `hybrid` engine:
- `frm_build_rest_frame` builds the rest frame from `frm_template` and the pause phoneme's FN, F1, F2, F3;
- `frm_copy_rest_frame` copies it into the producer frame;
- `frm_finish_frame` completes every frame `par_play` steps: the snapshot, the mark-space clamp, the formant raise at high pitch, the source and filter codes (b16, b17), the b14–b15 word and the transition flags (b13);
- `par_setup_voice` (C since M4.4a) and `par_play` (C since M4.4b) call the C frame builder directly, so playback calls the ROM only for `isr_*` and `ser_*` (M4.6);
- every one of the seven routines matches its ROM original at every call `diff_lts` samples, and inside `par_play` at every swap `diff_play` compares.

## 2. The subsystem (all main ROM)

### 2.1 Where it sits
- **`par_setup_voice`** $B3A4 (C, `src/firmware/par_start.c`) calls `frm_build_rest_frame`, then `frm_copy_rest_frame` and `frm_clamp_mark_space` into each of the two frame buffers.
- **`par_play`** $B877 (C, `src/firmware/par_play.c`) calls `frm_finish_frame` once per frame (from `segment_step` and `last_frames`), and per rest frame at the end `frm_copy_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space` and `frm_transition_flags` (`rest_frames`).
- **`frm_finish_frame`** calls `frm_snapshot`, `frm_clamp_mark_space`, `frm_raise_formants` and `frm_transition_flags`. `frm_raise_formants` calls `par_clamp` (C since M4.4a); `frm_build_rest_frame` calls `par_next_param` (C since M4.4a).
- The producer frame is the 20-byte buffer at `isr_fill_ptr` ($2C) in RAM; the timer interrupt swaps it with the playing one at `isr_swap_buffers` $B306 only while `isr_frame_flag` is clear, so the frame builder never races the interrupt.

### 2.2 Ported to C (7 routines, 470 bytes, 230 instructions)

| C file (new) | Routine | ROM | Bytes |
|---|---|---|---|
| `src/firmware/frm_build.c` | `frm_build_rest_frame` | $B41D–$B444 | 40 |
| | `frm_copy_rest_frame` | $B445–$B44F | 11 |
| | `frm_snapshot` | $B917–$B92F | 25 |
| | `frm_clamp_mark_space` | $B930–$B95C | 45 |
| | `frm_raise_formants` | $B95D–$B982 | 38 |
| | `frm_finish_frame` | $B9B5–$BA3E | 138 |
| | `frm_transition_flags` | $BA3F–$BAEB | 173 |

- `frm_build.h` declares the seven routines and any constant a caller or test needs. Every routine is `void name(fw *f, fw_regs *r)`: r in = the entry registers, r out = the ROM's exit registers. The larger routines are split into one static helper per ROM block, each commented with its address range, as in M4.4a and M4.4b.
- The `ROM_FRM_*` addresses already exist in `src/firmware/par_addr.h`; their comments are updated (the routines are now C).
- **Memory access.** The producer frame is read with `fw_ind_y` and written with `fw_wr_ind_y`, both on `isr_fill_ptr`. ROM tables are read with `fw_rom8` at their ROM addresses (§3). `frm_snap`/`frm_snap_prev` ($16CD/$16DD, read 1-based from $16CC/$16DC) and the scratch bytes are RAM (`fw_rd8`/`fw_wr8`).

### 2.3 C calls C
- `par_setup_voice`: its five `fw_rom_call`s (`frm_build_rest_frame`, `frm_copy_rest_frame` twice, `frm_clamp_mark_space` twice) become direct calls.
- `par_play`: its six (`frm_finish_frame` in `segment_step` and `last_frames`; `frm_copy_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space`, `frm_transition_flags` in `rest_frames`) become direct calls.
- `frm_finish_frame` → `frm_snapshot`, `frm_clamp_mark_space`, `frm_raise_formants`, `frm_transition_flags`; `frm_raise_formants` → `par_clamp`; `frm_build_rest_frame` → `par_next_param`.
- No `ROM_FRM_*` `fw_rom_call` remains in `src/firmware/`. The frame builder itself makes no ROM call.

### 2.4 Hooks (7 new replacements; 71 in all, 72 `golden_hybrid` modes)
- All seven get a row in the replacement table in `src/engine/thunks.c`. None is marked `waits`; `diff_lts` checks all seven.
- Every thunk declares A, X and Y as outputs, each with a comment saying what the ROM callers read after the call, checked in the dumps. The ROM callers that remain are each other (`frm_finish_frame`'s calls) and, in the rom-engine runs and the single-routine modes, `par_setup_voice` and `par_play`.

### 2.5 Exit registers (as the ROM leaves them)
- `frm_build_rest_frame`: Y = $3B, X = 9 (its `INX`/`INX` loop ends at `CPX #8`), A = `par_params_ptr` high byte.
- `frm_copy_rest_frame`: Y = $FF, A = `frm_rest_frame[0]`, X untouched.
- `frm_snapshot`: X = 0, Y = the frame offset of the last map entry read (`frm_snapshot_map[0]`, b10), A = that frame byte.
- `frm_clamp_mark_space`: A = $0B, Y = $0B (b11's offset), X untouched.
- `frm_raise_formants`: Y = 10 and A = b10 − 32 when that is negative (the early `RTS`); else Y = 5 and A = the clamped F2. X untouched (`par_clamp` keeps X).
- `frm_finish_frame`: A, X and Y as they came in (`PHA`/`PLA`). Its pushes land in the dead stack, which `diff_lts` and `diff_play` exclude, so the C need not reproduce them.
- `frm_transition_flags`: A = `frm_flags`, Y = $0D, X untouched.

The plan checks every one of these against the dumps (the "ROM facts checked" lists) before writing the code.

### 2.6 Quirks kept
- **Every RAM write**, the write-only and scratch ones included:
  - `$75`: `frm_clamp_mark_space` stores 2c (c = b10); `frm_raise_formants` stores d = (b10 − 32) / 4; `frm_transition_flags` counts the rises in it (`LDM #0` then `INC`).
  - `frm_level_diff` ($181C) and `frm_f1_excess` ($181D): stored only when both are positive, and the multiply's eight `ASL`/`LSR` leave both at 0 (the multiplicand shifted out, the multiplier shifted out).
  - `frm_b14` ($181E): written on every path (0, or `frm_b14_15_lo[index]`).
  - `frm_flags` ($181F): cleared, then each flag OR-ed in by its own read-modify-write.
  - `par_row_ptr`: advanced by `par_next_param` three times in `frm_build_rest_frame`, then reset to `par_params_ptr`.
  - b11 of the producer frame: written with @M first, then possibly overwritten by each clamp in turn (up to four writes).
- **The signed and unsigned tests exactly as the ROM's flags make them:**
  - `frm_clamp_mark_space`: the upper cap 160 − 2c is an unsigned `CMP`/`BCS`; the lower bound 47 − 2c is skipped when the 8-bit result has bit 7 set (`BMI`), then compared unsigned (`CMP`/`BCC`); the floor of 11 is unsigned.
  - `frm_raise_formants`: b10 − 32 tested with `BPL` (bit 7 of the 8-bit difference).
  - `frm_finish_frame`: d = ALF − A1 − 4 and e = F1 − 33 (from `frm_snap`) each go on only when not zero and bit 7 clear (`BEQ`, `BMI`); then `frm_product_limit[e]` − d is tested with `BMI` (bit 7 of the 8-bit difference), so e = 1 (limit 255) always saturates to the index $FF; otherwise the 8-bit shift-and-add multiply, whose carries out of bit 7 are lost.
  - `frm_transition_flags`: each window is two tests on the 8-bit difference, `CMP #k`/`BPL` and `CMP #−k`/`BMI`, which order by bit 7 of the comparison's result, not by the true signed value; the C writes them as the ROM's flags compute them, so a difference that wraps past ±127 behaves as on the ROM.
- **Loop orders and bounds:** `frm_template` copied 1-based for Y = 20 down to 1; `frm_copy_rest_frame` for Y = 19 down to 0 (`BPL`); `frm_snapshot` copies `frm_snap` to `frm_snap_prev` for Y = 10 down to 1, then reads the frame through `frm_snapshot_map` for X = 10 down to 1; `frm_transition_flags` walks Y = 9 down to 6 (frequencies), then 5 down to 1 (amplitudes) for bit 7, 5 down to 1 for bits 6 and 4.
- The rest frames keep the template's b16/b17 whatever @$ selects (parent spec §1.4, rev 6): `par_play`'s rest frames do not call `frm_finish_frame`.

## 3. Converted data (`firmware_data` 47,702 → 48,496 bytes)

| ROM | Table | Form |
|---|---|---|
| $B783–$B796 | `frm_template` (20) | A `frame20` with designated initializers by byte name: ALF, A1, A2, A3, AHF = 1 (the silent value); FN, F1, F2, F3 = 32; V = 63; pitch code 32; b11 = 32; b12 = $17; b13 = $20 (bit 5, which the firmware never computes; parent spec §1.4); b14–b15 = $19, $24; b16 = $82 (the default source); b17 = $4A (the male filter); the end marker $FF $FF. Read as $B782 + Y for Y = 20..1. |
| $BAEC–$BAEF | `frm_source_codes` (4) | b16 per @$ source index: default $82, spike $58, cut-down $9B, reduced HF $BA. |
| $BAF0–$BAF1 | `frm_filter_codes` (2) | b17 per @$ filter index: male $4A, female $54. |
| $B473–$B572 | `frm_product_limit` (256) | A formula the encoder expands: entry e = 255 / e (truncated) for e = 1..255. Entry 0, which nothing reads (e ≥ 1 on the only path that indexes it), is the literal 72 (`FRM_PRODUCT_LIMIT_0`), commented as such. |
| $B573–$B672, $B673–$B772 | `frm_b14_15_lo`, `frm_b14_15_hi` (256 each) | A formula the encoder expands: entry i = the low and the high byte of 82 × i (`FRM_B14_15_STEP` = 82, the word step per product index; parent spec §1.4 rev 6). |

- `src/firmware/data/fw_data.h` gains the frame-byte names b11–b17 next to the existing ones: `FRAME_MARK_SPACE` 11, `FRAME_B12` 12, `FRAME_FLAGS` 13, `FRAME_B14` 14, `FRAME_B15` 15, `FRAME_SOURCE` 16, `FRAME_FILTER` 17; the template and the C use them.
- The three small tables go in a new `src/firmware/data/frm_data.c`, the frame builder's data file. `frm_snapshot_map` stays in `par_data.c` (M4.4a converted it; `par_reset_tracks` reads it too).
- The two formulas are expanded in `fw_data.c`'s encoder (the M4.0 precedent of encoder-generated pointer tables), each next to a comment giving the formula and its meaning.
- `tests/test_firmware_data.c` checks every converted byte against the ROM. Its `k_rom` list becomes `{ 0x4000, 0x8FF4 }, { 0x9804, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xB473, 0xB77D }, { 0xB783, 0xB7A1 }, { 0xB7A7, 0xB7B1 }, { 0xBAEC, 0xBAF2 }, { 0xC67B, 0xC9B8 }, { 0xD44A, 0xF77B }`: the formula tables join `frm_snapshot_map`, and `frm_template` joins `par_slot_map`.
- Not converted: `unused_B77D` ($B77D–$B782, six unreferenced bytes).

## 4. Verification

### 4.1 `diff_lts`
- `DIFF_MAX` 72. It checks the 69 replacements not marked `waits` (62 + 7), each more than 0 times, with 0 mismatches. `frm_build_rest_frame` and `frm_copy_rest_frame` are sampled at `par_setup_voice`'s ROM calls; the others at `par_play`'s and `frm_finish_frame`'s.

### 4.2 `diff_play`
- Unchanged in what it compares. `par_play`'s C copy now runs the C frame builder, so every frame and every snapshot at every swap also checks the seven routines inside the live loop.
- **The abort injection moves.** It is made today at the first `frm_finish_frame` entry ($B9B5, a machine hook in both copies) after the second swap. Once `par_play` calls the C `frm_finish_frame` directly, the C copy never executes $B9B5. The injection moves to the first entry of `isr_par_ready` ($C5D6, ROM in both copies) after the second swap at which `isr_frame_flag` bit 0 is clear: the frame built after the swap has been finished and the flag cleared, so `par_play`'s next step is the abort test at $B8A2 ($B89F `JSR isr_par_ready`, then `BBC0 ser_abort`), as with the old trigger. Like the old one, it is a point in the program both copies reach in the same state, not an interrupt's landing point.
- This change is made in the first task, while the frame builder is still ROM, and must keep `diff_play` at 0 mismatches with $B8A5 reached in as many calls as before (92), its ROM-against-ROM self-check included.

### 4.3 `golden_hybrid`
Every file (48: boot and 47 texts) × 72 modes (all routines, then each alone), byte for byte; every existing golden is unchanged.

### 4.4 `test_timing_invariance` (`tests/test_golden.c`)
Unchanged; it passes on both engines.

### 4.5 Coverage and the new corpus texts
Probed while designing with `apollo2_coverage` on the 45 corpus texts: every instruction of the seven routines executes except
- **$B945**, the upper @M cap: b11 > 160 − 2c needs @M above 160 − 2c, i.e. b10 ≥ 49 with @M $3F. New text **`mark_space_cap`**: `@M3F @R7 @FF Take a cake.` (probed: reaches $B945).
- **$B9F8–$BA26**, the b14–b15 product: it needs F1 > 33 and ALF − A1 > 4 in the same frame, which no phoneme's targets give (the rows with F1 above 33 all have ALF − A1 − 4 negative). The @u formant offsets reach it. New text **`formant_extremes`**: `@u120+ @u410+ @u510- Hello world.` (F1 + 32, ALF + 16, A1 − 16; probed: reaches the multiply and the saturated index $FF).

The corpus goes from 45 to 47 texts; both goldens are generated once by the rom engine. The plan confirms with a precise branch probe (each conditional branch's taken and fall-through edges, not only the instructions executed) that every conditional branch of the seven routines is seen both ways on the corpus, $BA02 (the limit test) and $BA0B (the multiplier bits) included. Any branch still one-sided is pinned by a unit test, and the plan names it.

### 4.6 Unit tests (new suite `tests/test_fw_frm.c`, `suite_fw_frm`; 34 suites)
One or more cases per routine, the registers in and out per §2.5, RAM checked byte for byte against values worked out from the dumps:
- `frm_clamp_mark_space`: the upper cap; 47 − 2c negative (skipped) and positive (raising b11); the floor of 11; $75 = 2c.
- `frm_raise_formants`: b10 < 32 (the early return, Y = 10); b10 ≥ 32, F1 += d and F2 += d / 2, each through `par_clamp` (1..63), at a clamp and away from it.
- `frm_finish_frame`: d ≤ 0 and e ≤ 0 (b14 = b15 = 0); e = 1 (always $FF); limit − d negative (saturated); an exact product (b14–b15 = 82 × d × e); `frm_level_diff` and `frm_f1_excess` left at 0; b16 and b17 per index; A, X and Y preserved.
- `frm_transition_flags`: each flag bit at its threshold and one below; a difference that wraps past ±127; the rise count in $75.
- `frm_snapshot`: `frm_snap_prev` = the old `frm_snap`; the new one through `frm_snapshot_map`.
- `frm_copy_rest_frame`: the 20 bytes, Y = $FF.
- `frm_build_rest_frame`: the template, the pause row's FN, F1, F2, F3, `par_row_ptr` reset.
- `tests/test_fw_par.c` (`par_play`, `par_setup_voice`) and `tests/test_fw_cmd.c` (`cmd_apply_pitch` → `par_setup_voice`): the stub hosts record the `ROM_FRM_*` calls today. Those cases change to check that the C ran (its effect on the frame buffers and the registers that flow on) and that the stub saw no `ROM_FRM_*` call.

### 4.7 Done criteria
1. `diff_lts` checks all 69 non-waiting routines more than 0 times each, with 0 mismatches.
2. `diff_play` checks `par_play` and `boot_loop_play` more than 0 times each, reaches $B8A5 and $B89C, with 0 mismatches.
3. `golden_hybrid` reproduces every file (48) × 72 modes byte for byte; every existing golden is unchanged; the `formant_extremes` and `mark_space_cap` goldens exist.
4. Every conditional branch of the seven routines is seen both ways on the corpus, or pinned by a unit test the plan names.
5. `test_timing_invariance` passes on both engines.
6. `firmware_data` passes at 48,496 converted bytes.
7. `apollo2 --engine hybrid` equals `--engine rom` on every corpus text (`-o FILE` only).
8. No `ROM_FRM_*` `fw_rom_call` remains in `src/firmware/`; `src/firmware/` still builds without `machine/` or `cpu740/` headers.
9. `build.bat test` is green (34 suites).
10. The docs are updated: the parent spec gets rev 14 (milestone 4.5 done; next M4.6); this spec's status, with "As built" if anything differs; the README's hybrid section lists the frame builder.

### 4.8 Task outline (for the plan)
1. Data and corpus: the frame-byte names, `frm_data.c`, the encoder formulas and the `firmware_data` ranges; the two corpus texts and their goldens; `DIFF_MAX` 72; the `diff_play` abort injection moved to `isr_par_ready` (proved while the frame builder is still ROM); the new suite `suite_fw_frm` wired in.
2. The leaves: `frm_copy_rest_frame`, `frm_build_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space`, `frm_raise_formants`, with hooks and unit tests; `par_setup_voice` calls them directly.
3. The finish: `frm_transition_flags` and `frm_finish_frame`, with hooks and unit tests; `par_play` calls the frame builder directly; the branch probe recorded.
4. Parent spec rev 14, this spec's status, "As built" and the README.

## 5. Out of scope
- `isr_*`, `ser_*`, `boot_*` (other than `boot_loop_play`) and `bank_*` (M4.6).
- `unused_B77D` (unreferenced bytes).
- Any change to the synthesizer's use of b14–b17 (parent spec §1.4: decoded, no listening-test hypothesis yet).
