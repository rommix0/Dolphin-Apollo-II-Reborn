# M4.5 — The frame builder in C (frm_*) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-08-m4-5-frame-builder.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 13): §1.4 (the frame bytes), §6 step 4, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md`: `par_setup_voice` (the voice set-up that builds the rest frame), its conventions (§2.3–§2.7) and "As built" (§6).
- `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`: `par_play`, which finishes every frame; `diff_play` (§4.1) and "As built" (§6).

**User decisions (2026-10-08):**
- One plan for the whole frame builder (seven routines, 470 bytes).
- Hooks: all seven routines, as in M4.4a and M4.4b (every routine, so that a mismatch points at one routine). That makes 71 replacements and 72 `golden_hybrid` modes.
- The three formula tables (`frm_product_limit`, `frm_b14_15_lo`, `frm_b14_15_hi`) are converted as formulas that the encoder expands, not written out (§3).
- Two new corpus texts and their rom-engine goldens, for the two pieces of the frame builder no text reaches (§4.5): `formant_extremes` and `mark_space_cap`. The existing goldens stay unchanged. *(Corrected 2026-10-08, user decision after the plan's proof: `formant_extremes` is `@u110+ @u510- Hello world.`; the first text clipped the synthesizer, §4.5.)*

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
- The `ROM_FRM_*` addresses exist in `src/firmware/par_addr.h` except `ROM_FRM_RAISE_FORMANTS` ($B95D), which is added; their comments are updated (the routines are now C).
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
  - `frm_transition_flags`: each window is two tests on the 8-bit difference, `CMP #k`/`BPL` and `CMP #(1 − k)`/`BMI` (frequencies $10/$F1, amplitudes $0C/$F5 and the amplitude falls $1C and $13 with `BPL` only, b10 $07/$FA), which order by bit 7 of the comparison's result, not by the true signed value; the C writes them as the ROM's flags compute them, so a difference that wraps past ±127 behaves as on the ROM.
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
- **$B9F8–$BA26**, the b14–b15 product: it needs F1 > 33 and ALF − A1 > 4 in the same frame, which no phoneme's targets give (the rows with F1 above 33 all have ALF − A1 − 4 negative). The @u formant offsets reach it. New text **`formant_extremes`**: `@u110+ @u510- Hello world.` (F1 + 16, A1 − 16; probed: reaches the multiply, the saturated index $FF and F1 = 33 exactly, $B9F4). The first choice, `@u120+ @u410+ @u510-`, reached the same branches but its ALF + 16 clipped the synthesizer (`suite_synth`: every golden must synthesize unclipped), so it was replaced.

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
- `tests/test_fw_tn.c`: the flush case runs the C `par_play`, whose rest frames are now C writes through `isr_fill_ptr`; the case points `isr_fill_ptr` at a frame buffer (`isr_frame_b`).
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

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **`frm_level_diff` and `frm_f1_excess` are not stored "only when both are positive"** (§2.6). `STA frm_level_diff` ($B9E9) follows d's own test, before e is computed, and `STA frm_f1_excess` ($B9F8) follows e's; the multiply's shifts leave both at 0 only when it runs. So d > 0 with e ≤ 0 leaves `frm_level_diff` = d and `frm_f1_excess` unwritten, and the saturated index (`BMI` at $BA02 taken) leaves them at d and e. `test_finish_frame` checks each case: d = 6, e = 0 (6, not written); e = 1 (6, 1); d = 60, e = 5 (60, 5); the two multiplied rows (0, 0); d ≤ 0 (neither written).
- **The product never needs the carries it loses** (§2.6). When the multiply runs, d and e are 1..127 and `frm_product_limit[e]` − d has bit 7 clear, which for these ranges means d ≤ 255 / e, so d × e ≤ 255; e = 1 (limit 255) always saturates, 255 − d being $80..$FE. The C still adds in 8 bits, as `ADC` does. e is F1 from `frm_snap`, taken before `frm_raise_formants` raises F1: `test_finish_frame`'s b10 = 40 row raises F1 from 34 to 36 and still gets e = 1. b15 is written before b14 ($BA2E, then `DEY`).
- **`frm_raise_formants` returns early whenever b10 − 32 has bit 7 set** (§2.5 says "negative", §4.6 "b10 < 32"): b10 = $A0..$FF returns too, with A = b10 − 32 and Y = 10. `test_raise_formants` checks b10 = 31 and b10 = $A0.
- **`frm_clamp_mark_space`'s 2c is 8 bits** (`ASL A` loses bit 7 of b10), and 160 − 2c and 47 − 2c wrap in 8 bits. `test_clamp_mark_space` checks c = 90 (2c = $B4: 160 − 2c = $EC, no cap; 47 − 2c = $7B, so b11 = 123) and c = $81 (2c = 2).
- **`frm_transition_flags`' scans** (sharper than §2.6): bit 7 tests the frequencies (i = 9 down to 6) and then, only when no frequency hit, the amplitudes (i = 5 down to 1), as one scan that stops at its first hit; bit 6 stops at its first hit too, with the limit $13 when the new value `frm_snap[i]` is 1 (`CMP #1` at $BA7C), else $1C; the rise count runs over all five amplitudes on the reverse difference, `frm_snap` − `frm_snap_prev`. Each test reads bit 7 of an 8-bit result, so a large rise reads as a fall: ALF from 0 to $77 (d = $89) sets bits 7 and 6 and counts one rise (`test_transition_flags`' last case; `test_play_tail` checks that its first rest frame hands `frm_flags` $C0 to `isr_par_ready` in A).
- **The thunks** declare A, X and Y for all seven (§2.4) and copy the C's exit registers back; each comment gives §2.5's exit registers and what the ROM callers read next (no caller reads a flag). `frm_finish_frame`'s C works on a copy of the registers and returns them unchanged; its three pushes (`PHA` at $B9B5, $B9B7, $B9B9) and the return addresses of its four `JSR`s are not written (dead stack).
- **In the "all" mode none of the seven hooks fires**: their callers (`par_setup_voice`, `par_play`, `frm_finish_frame`) are all C and call the C directly, so each hook fires only while a ROM caller runs (the single-routine modes). The same now holds for `par_next_param` and `par_clamp`, whose last ROM callers (`frm_build_rest_frame`, `frm_raise_formants`) are C; their thunk comments say so.
- **The C layout**:
  - `frm_build.c`: `frm_build_rest_frame` (`copy_template` $B41D–$B426, `pause_formants` $B428–$B438; the reset of `par_row_ptr`, $B43A–$B444, in the public function), `frm_copy_rest_frame` (no helper), `frm_snapshot` (`save_snapshot`, `take_snapshot`), `frm_clamp_mark_space` (`mark_space`, `upper_cap`, `lower_bound`, `floor_11`), `frm_raise_formants` (`raise`, once for F1 and once for F2; the test at $B95D–$B966 in the public function), `frm_transition_flags` (`minus`, `moved`, `set_flag`, `jump_flag`, `fall_flag`, `rise_flag`, `pitch_flag`) and `frm_finish_frame` (`source_filter`; `b14_15` with `level_diff`, `f1_excess`, `product_word` and `multiply`). `jump_flag` and `pitch_flag` call `moved` once per test, so up to twice per byte (the second test is skipped when the first hits), where the ROM subtracts once; it only reads RAM;
  - `frm_build.h`: the seven routines, `FRM_PAUSE_PHONEME`, `FRM_MARK_SPACE_CAP`, `FRM_MARK_SPACE_BASE`, `FRM_MARK_SPACE_FLOOR`, `FRM_RAISE_PITCH`, `FRM_LEVEL_MARGIN`, `FRM_F1_BASE`, `FRM_PRODUCT_SATURATED` and the flag bits `FRM_FLAG_JUMP` $80, `FRM_FLAG_FALL` $40, `FRM_FLAG_RISES` $10, `FRM_FLAG_PITCH` $08;
  - `frm_data.c`: `fw_frm_template` is a `uint8_t[FRAME_SIZE]` with designated initializers by the `FRAME_*` names (§3's "`frame20`" is the Ghidra type), then `fw_frm_source_codes[4]` and `fw_frm_filter_codes[2]`. `fw_data.h` has, besides §3's b11–b17 names, `FRAME_END_MARKER` 18, `FRAME_SIZE` 20, the six table addresses (`FRM_PRODUCT_LIMIT`, `FRM_B14_15_LO`, `FRM_B14_15_HI`, `FRM_TEMPLATE`, `FRM_SOURCE_CODES`, `FRM_FILTER_CODES`), `FRM_PRODUCT_LIMIT_0` (72) and `FRM_B14_15_STEP` (82). `FRAME_FLAGS` (frame byte 13) and `fw_ram.h`'s `FRM_FLAGS` ($181F, RAM) name different things; the C writes b13 from `FRM_FLAGS`;
  - `fw_data.c`'s step 10 expands both formulas in one loop over i = 0..255, then puts the template and the two code tables; its `check_end`s fail the image build unless `frm_b14_15_hi` ends where `frm_snapshot_map` starts, the template where `par_slot_map` starts and the source codes where the filter codes start. `test_firmware_data` also checks the template's b16 and b17 against `frm_source_codes[0]` and `frm_filter_codes[0]`, and its end marker $FF $FF;
  - `par_start.c` no longer includes `par_addr.h`; `par_addr.h` gains `ROM_FRM_RAISE_FORMANTS`, and its `frm_*` callers lose their parentheses; `grep -rn ROM_FRM_ src/firmware` finds only `par_addr.h`.
- **`diff_play`'s abort injection** (§4.2) is a machine hook at `isr_par_ready` ($C5D6) in both copies (in copy B a ROM call from the C `fw_par_ready`), armed at the second swap, that sets `ser_abort` bit 0 at the first entry with `isr_frame_flag` bit 0 clear and disarms. By the dump that entry is $B89F in the segment path, so the abort test at $B8A2 sees it as with the old trigger; in the tail it is $B8C9 and in the rest frames $B8E6, where no abort test follows; when the second swap comes inside `boot_play_body`'s `isr_wait_frame`, which clears the flag, it is `par_play`'s first $B89F. On the 45 texts, with the ROM frame builder, it printed the lines it printed before (175 and 46, 124 and 46, 92 and 92). On the 47: `boot_loop_play 179 calls checked, 48 with both injections`, `par_play 126 calls checked, 48 with both injections`, `$B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96`, 0 mismatches; the self-check line is unchanged.
- **`diff_lts`** prints 69 routines; the seven new ones: `frm_build_rest_frame` 121, `frm_copy_rest_frame` 782, `frm_snapshot` 19,113, `frm_clamp_mark_space` 19,331, `frm_raise_formants` 18,626, `frm_finish_frame` 18,626 and `frm_transition_flags` 19,113 calls checked, 0 mismatches.
- **The corpus texts and the branches** (§4.5, §4.7 criterion 4). `formant_extremes.frm` has 105 frames and `mark_space_cap.frm` 125, from the rom engine; no other golden changed. `apollo2_coverage` on boot and the 47 texts finds 4,106 ROM instruction addresses (4,084 on the 45), all 230 of the seven routines among them. The plan's branch probe (a scratch tool, not committed: a machine hook on each of the 32 conditional branches of the seven routines evaluates it from the flags before it runs; boot and the 47 texts on the rom engine, so the result does not depend on the C) found `32 branches, 0 one-sided`. On the 45 texts six were one-sided: $B943 (never fell through), $B9F4 (never taken), $B9F6 (never fell through), $BA02, $BA0B and $BA15 (never reached); `mark_space_cap` gives $B943's fall-through (115 times, the cap written at $B945) and `formant_extremes` the other five (e.g. $B9F4 taken 3 times, $BA02 taken 14 times and not taken 16). The unit tests pin the rarest edges as well: $B9F4 taken, $BA02 and $BA0B both ways (`test_finish_frame`), $B943 both ways (`test_clamp_mark_space`), and $BA89, $BA97, $BAD3 and $BAD7 taken (`test_transition_flags`).
- **`tests/test_fw_frm.c`** (§4.6): its mock fails and counts every ROM call, and each routine case checks 0 calls, no fault, the registers out and every RAM byte below $4000 unchanged but the ones it lists (`check_written`). `test_data` reads the tables through `fw_rom8` (the template byte by byte, both code tables' first and last entries, `frm_product_limit` at 0, 1, 2, 30 and 255, `frm_b14_15_lo/hi` at 1, 4 and 255); `test_leaf_rows` and `test_finish_rows` check each hook's address, its A, X and Y outputs and that it does not wait; then `test_build_rest_frame` (voice A's pause row FN 32, F1 32, F2 23, F3 31; a RAM table whose `par_row_ptr` is not `par_params_ptr`), `test_copy_rest_frame`, `test_snapshot` (the bytes either side of `frm_snap_prev` untouched), `test_clamp_mark_space` (8 cases), `test_raise_formants` (6), `test_transition_flags` (21) and `test_finish_frame` (9).
- **The callers' unit tests** (§4.6): the mocks of `tests/test_fw_par.c` and `tests/test_fw_cmd.c` lost their `ROM_FRM_*` cases, so any such call fails.
  - `par_setup_voice`: 0 ROM calls; `frm_rest_frame` = `frm_template` with b1, b3, b5, b7 from row $3B of the four target rows; both frame buffers = the rest frame with b11 = @M clamped at the template's pitch code 32. `test_fw_par.c`'s `setup_voice_case` (@M 0, raised to 11) also checks the buffer pointers, `isr_swap_phase` 1 and `FW_TMP_75` = 64; `test_fw_cmd.c`'s `check_setup_voice`, now without arguments, expects b11 = @M held between 11 and 96, and `test_set_prosody`'s digit case now calls it too. `check_buffers` is gone;
  - the frame loop: `check_rest_frames` expects `isr_wait_frame`, then only the `isr_par_ready` waits of each rest frame, then the last `isr_par_ready`; `test_play_tail` (the fill frame ends as the rest frame with b11 = 47 and b13 = 0, `frm_flags` 0, `par_play`'s exit A = 0), `test_play_abort` (the fill frame becomes the rest frame, zeros with b11 = 47: no frame was built), `test_play_segment` (the last rest frame's b16 = 0, exit A = 0), the three zero-frame tests (8, 8 and 261 calls; were 21, 21 and 528) and `test_loop_play` (`isr_fill_ptr` = `isr_frame_b`; 15 calls, were 27; `par_play`'s exit A = 0 reaches `isr_volume_half`).
- **`tests/test_fw_tn.c`'s flush** (§4.6): its mock lost the four rest-frame `ROM_FRM_*` cases, and `isr_fill_ptr` is set to `isr_frame_b` before the conversion (with the zeroed RAM's $0000 the C rest frames would write the zero page, as the ROM would). The checked tail is 20 calls (the C player's 15, then `isr_par_ready` for text[250..254]; were 32 and 27), with `isr_set_volume(0)` at call 13 of them (was 25); the 1,379 `isr_par_ready` calls and `tn_resume_pos` 250 are unchanged.
