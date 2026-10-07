# M4.3b — The stress and phrase passes in C (pros_make_segments) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-04-m4-3b-stress-and-phrase-passes.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 10): §6 step 3, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md`: `tn_convert_clause` and its mid-clause flush, which this milestone turns into a C-to-C call.
- `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md`: the segment generator that the passes feed, the converted tables they read, `tests/test_fw_pros.c` (§2, §3, §6 "As built").

**User decisions (2026-10-04):**
- M4.3b is the second half of M4.3 (split agreed on 2026-10-03). It covers `pros_make_segments` and the two passes before `pros_phrase_gen`: `pros_mark_stress`, `pros_mark_phrases` and `pros_store_phrase_len`.
- Hooks: all four routines, so that a mismatch points at one routine (as in M4.0 and M4.3a).
- The C is one static helper per ROM block, each carrying its ROM address range, as in `pros_gen.c`. No `goto` transliteration and no semantic rewrite.
- Coverage gaps are closed with three new corpus texts (rom-engine goldens; the existing goldens stay unchanged), and with unit tests for what no text reaches.

## 1. Goal

When M4.3b is done, all of prosody before playback runs as C in the `hybrid` engine:
- `pros_make_segments` turns `tn_phonetic` into the segment arrays through the three passes;
- the C `tn_convert_clause` calls it directly;
- it matches its ROM originals at every call site the corpus reaches;
- its only ROM call is `isr_par_ready`.

## 2. The subsystem (all main ROM)

### 2.1 Where it sits
- `tn_convert_clause` (C since M4.1) leaves the phonetic string in `tn_phonetic` ($1900) and `tn_phon_len` ($53), and the command events in `cmd_ev_*`.
- `pros_make_segments` $CA92 is called:
  - from ROM by `boot_reset` $A684, `boot_selftest_sound` $A7B7 (at boot, interrupts off) and `cmd_say_phonetic` $A879 (unreachable);
  - from C by `tn_convert_clause`'s mid-clause flush, through `fw_rom_call` until now.
- `pros_make_segments` does this:
  1. It copies the statement phrase parameters (`pros_params[0..4]`) into `pros_baseline`, `pros_baseline_end`, `pros_stress_param`, `pros_nucleus_param` and `pros_final_param`.
  2. It calls `pros_reset_seg_ptrs`.
  3. It calls `pros_mark_stress` (forward), `pros_mark_phrases` (backward) and `pros_phrase_gen` (forward, C since M4.3a).
- `boot_loop_play` $A7D4 then plays (ROM, M4.4).

### 2.2 Ported to C (4 routines, 1,687 bytes, 677 instructions)
- **`pros_make_segments` $CA92** (43 B, 15 instructions). The parameter copies, then the four calls. In C every callee is C.
- **`pros_mark_stress` $CABD** (1,071 B, 442 instructions). This is the forward pass. It starts by clearing its state and setting up the event queue, then runs once per position, from 0 up to `tn_phon_len`:
  - it calls `isr_par_ready`;
  - it clears the position's flags and counts;
  - it reads the symbol's class and features, and scans ahead for the next symbol and the next phonetic symbol;
  - it handles the marker symbols (`~` `|` `}` `{` `` ` `` `x`);
  - it toggles the stress and sets the stress flag ($40), the unreleased-stop flag ($10) and the function-word flag ($08);
  - it adds to the shortening and lengthening counts (`pros_sym_shorten` $1E00, `pros_sym_lengthen` $1F00) from the context;
  - it handles the vowel, with its consonant scan;
  - at a space or punctuation, it stores the word's syllable count;
  - it ends with the common tail that saves the previous features.
- **`pros_mark_phrases` $CEF6** (529 B, 203 instructions, with its exit block $CEEC–$CEF5 *before* the entry). This is the backward pass, from `tn_phon_len` − 1 down to 1. Per position:
  - it calls `isr_par_ready`;
  - it counts each phrase's phonetic symbols;
  - at each punctuation, it loads that phrase type's five parameters and calls `pros_declination` (C since M4.3a);
  - it marks the accent ($60, lengthened), the nucleus (flags bits 0–2 = 7 from the last stress to the end) and the phrase-final lengthening;
  - it adds the context counts.

  When `pros_pos` reaches 0, it branches back to the exit block, which calls `pros_store_phrase_len` and sets `pros_sym_flags[0]`.
- **`pros_store_phrase_len` $D0FD** (44 B, 17 instructions). It stores `pros_phrase_len` into both count arrays at `pros_pos`, with bit 7 set unless the previous punctuation had class bit 3 clear. It then clears the count and remembers this punctuation's class bit 3.

In C, `pros_make_segments` calls `pros_reset_seg_ptrs`, `pros_mark_stress`, `pros_mark_phrases` and `pros_phrase_gen` directly, and `pros_mark_phrases` calls `pros_store_phrase_len` and `pros_declination` directly ("C calls C").

### 2.3 Hooks (4 new replacements; 28 in all, 29 `golden_hybrid` modes)

| Hook | ROM callers | Declared outputs (the plan confirms each against the caller dumps) |
|---|---|---|
| `pros_make_segments` $CA92 | `boot_reset` $A684, `boot_selftest_sound` $A7B7, `cmd_say_phonetic` $A879 | A, X, Y. Every caller goes on to `boot_loop_play`, which loads A from $6B first, so nothing reads them. They are defined, so they are declared (as `pros_phrase_gen`'s are). |
| `pros_mark_stress` $CABD | `pros_make_segments` $CAB3 | A, X, Y. `pros_mark_phrases` reloads A and X before reading them, but its first `isr_par_ready` ($CF28) gets the Y left here. |
| `pros_mark_phrases` $CEF6 | `pros_make_segments` $CAB6 | A, X, Y. They flow into `pros_phrase_gen`, whose first `isr_par_ready` ($D155) gets X and Y. |
| `pros_store_phrase_len` $D0FD | `pros_mark_phrases` $CF97 and $CEEC | A, X, Y. It leaves X = `pros_pos`, which `pros_declination` and the following code see. |

- The default is A, X and Y. The plan narrows a row only where the dumps prove a register dead on every path, and says so in the row's comment.
- `pros_store_phrase_len` is reached from ROM only while `pros_mark_phrases` is ROM, that is, in its "alone" mode; `pros_mark_stress` and `pros_mark_phrases` likewise only while `pros_make_segments` is ROM.
- Hook budget:
  - `diff_lts` installs one observer per replacement plus its own `boot_loop_play` stop, so it uses 29 of `MACHINE_MAX_HOOKS` (32).
  - `golden_hybrid`'s extra `isr_par_ready` hook also brings it to 29.
  - Raising the limit is M4.4's.

### 2.4 ROM calls from C

| ROM routine | Called by | Budget | Why it stays ROM |
|---|---|---|---|
| `isr_par_ready` $C5D6 | `pros_mark_stress` ($CB1F, once per position), `pros_mark_phrases` ($CF28, once per position) | `FW_ROM_CALL_BUDGET` | Parallel-port handshake, an SFR write (M4.6). It preserves A, X and Y. |
| `boot_loop_play` $A7D4 | `tn_convert_clause`'s flush (unchanged from M4.1) | `FW_ROM_CALL_SPEECH_BUDGET` | Playback (M4.4). |

- `isr_par_ready` is called at exactly the ROM's points, with the ROM's registers.
- In `pros_mark_stress` about a dozen paths jump back to $CB1F, each leaving different A and Y. The C tracks them in an explicit `fw_regs`, as `pros_gen.c` does for $D155.
- At $CF28, A = `pros_features` (loaded at $CF1A) and X = `pros_pos`. Y is what the previous position left, or, at the first position, what `pros_mark_stress` left.
- Nothing in scope plays audio, waits on an interrupt or resets the stack: no unwind.

**The flush becomes C to C.** `tn_convert_clause` calls the C `pros_make_segments` instead of `fw_rom_call(ROM_PROS_MAKE_SEGMENTS)`. The registers it then hands to `boot_loop_play` stay what the ROM would leave, which is `pros_phrase_gen`'s exit A/X/Y (as M4.2 did with `cmd_run_due`). `diff_lts`'s mid-clause flush skip and its unwound-call count are unchanged.

### 2.5 The output contract
Unchanged from M4.3a §2.5, plus what the two passes leave for `pros_phrase_gen` and for playback:
- the per-position arrays `pros_sym_flags` $1D00, `pros_sym_shorten` $1E00 and `pros_sym_lengthen` $1F00;
- `tn_phonetic`, rewritten in place (§2.7);
- `cmd_ev_pos` and `cmd_ev_next`, set up here;
- $6B (`FW_UTT_CONTENT`), set when events exist;
- every `$1Cxx` state byte, the write-only ones included.

### 2.6 RAM names and shared bits
- **Names in `fw.h`** for the state bytes `fw_ram.h` leaves unnamed, next to the M4.3a names. Each name comes from what the byte does; where a byte means different things in the two passes, the name and comment say both. The bytes are:
  - $1C06, $1C07, $1C08, $1C09: the next symbol, the next phonetic symbol and their features;
  - $1C0B: the syllable count / the following word's count;
  - $1C0C: a vowel seen in this word;
  - $1C11: shortening after `~`;
  - $1C12: write-only;
  - $1C13: next is punctuation / phrase-final;
  - $1C14: backquote pending;
  - $1C15: the previous punctuation's class bit 3 is clear;
  - $1C16: phrase-final lengthening active;
  - $1C19: the function word / the last accent;
  - $1C1A: the previous features, copied;
  - $1C1C: write-only;
  - $1C1E: the nucleus mask.

  `FW_PROS_STRESS_TOGGLE` ($1C0D) exists, and its comment gains the passes' meanings. The plan confirms the exact set against the dumps.
- **A shared header for the `pros_sym_flags` bits.** It holds the bits the passes set and `pros_phrase_gen` reads: the nucleus mask 0–2, the function word $08, the unreleased stop $10, the accent $20 and the stress $40. They move out of `pros_gen.c`, which closes the deferred M4.3a item.

### 2.7 Quirks kept
- **Reads past the text.**
  - `pros_mark_stress` reads `tn_phonetic[pos+1]` with no bound at $CC37.
  - On the vowel path ($CDBA–$CDE3) it steps X by two, then scans while the symbol is a consonant, with no bound; X wraps inside page $19.
  - The look-ahead scans at $CB4B and $CB7D are bounded by `tn_phon_len`.
  - The C reads the same stale bytes.
- **The class table indexed by position** at $CCFB: `ph_symbol_class[pos+1]`, read through `fw_rom8` at the ROM address.
- **The missing `CLC` at $CCD5.** `ADC #$0C` takes the carry out of the `ADC #3` at $CCB2 (`BBC` and `LDA` leave the carry alone).
- **8-bit counts.** Every add into `pros_sym_shorten` and `pros_sym_lengthen` wraps mod 256.
- **Dead stores** to $1C12 ($CDCB, $CDD2) and $1C1C ($CF0F, $CF79) are written.
- **`tn_phonetic` is rewritten in place:**
  - `|` before a vowel becomes `R` ($CBDB, the linking R);
  - the first of two equal symbols becomes `|` ($CD18);
  - a backquote turns the next `?` into `.` ($CEB3).
- **The event setup.**
  - `cmd_ev_next` = 0.
  - `cmd_ev_pos` = $FF in its low byte only.
  - With events, both bytes come from the first entry and $6B gets `LDM #$FF` ($CB1C).
- **The exit block before the entry.** `pros_mark_phrases` reaches $CEEC by `BEQ` when `pros_pos` decrements to 0.
- **Empty `tn_phonetic` (latent).** With `tn_phon_len` = 0, `pros_mark_stress` returns at once and `pros_mark_phrases` decrements `pros_pos` from 0 to $FF, then walks positions 255…1 of stale RAM. Every clause starts with a space, so no text reaches this; a unit test pins it.
- **The phrase parameters.** The punctuation decides which five `pros_params` load:
  - `?`: K–O;
  - `!`: P–T;
  - `,` and `:`: F–J;
  - `.` and `;`: A–E;
  - others keep the current set.

  The step computed at each punctuation divides by the count stored there, which is the following phrase's (M4.3a §6).
- **Timing.** As in M4.1–M4.3a, the C takes no CPU cycles. In ROM `pros_make_segments` takes up to about 127 ms of a clause (mean about 8 ms), so serial arrival moves relative to the main level. The frames have stayed invariant, and the goldens are the check.

## 3. Converted data
None. The passes read only `ph_symbol_class` and `ph_symbol_features` (M4.3a), and `pros_params` in RAM. `firmware_data` stays at 38,197 bytes.

## 4. Verification

### 4.1 New corpus texts (goldens from the rom engine, once)
| File | Text | Reaches |
|---|---|---|
| `long_words` | `Internationalization is unbelievably uncharacteristically complicated.` | a word of 5 or more syllables ($D0B5); the linking R once ($CBDB) |
| `linking_r` | `Far away, for ever, the car is here, four apples.` | `\|` before a vowel → `R` ($CBD6 taken, $CBDB), four times |
| `syllabic_stress` | `@X1. Bx7 BxM Bx7Z. BIA DIO BEA. @X0` | a stressed syllabic consonant at the nucleus ($D0CD); the accent state still set after a stress toggle ($CC59 taken) |

- The corpus goes from 38 to 41 texts, so `golden_hybrid` prints 42 files, boot included.
- Each target was confirmed with the branch probe while designing (each text on its own, without the corpus). The plan confirms the instruction targets with `apollo2_coverage` before the goldens are written.

### 4.2 `diff_lts`
- Checks all 28 routines, each more than 0 times, with 0 mismatches.
- The flush skip and the unwound-call count are unchanged.

### 4.3 `golden_hybrid`
- Every file × 29 modes (all routines, then each alone), byte for byte.

### 4.4 Unit tests (`tests/test_fw_pros.c`, the M4.3a harness)
- One or more cases per routine, with the `isr_par_ready` calls and their registers on several paths through each pass.
- `pros_make_segments`: the parameter copies, then the four steps in order.
- What no text reaches:
  - **`BEQ $CCBA` taken.** Unreachable, because the accent state is cleared only after a vowel has counted a syllable.
  - **`BRA $D0CF`.** Unreachable, because the stress flag is set only when the features have bit 7 or bit 1.
  - **The carry into $CCD5.** Unreachable: a position's shortening count starts at 0 and is at most 11 at $CCB2. The test sets up RAM directly.
  - **The vowel-path consonant scan wrapping X** from $FF to $00.
  - **Empty `tn_phonetic`.**
- Also pinned:
  - the three `tn_phonetic` rewrites;
  - the dead stores;
  - the event setup with and without events;
  - the 8-bit wrap of a count;
  - the exit block at position 0.
- `tests/test_fw_tn.c`'s flush test checks the C call in place of the recorded `fw_rom_call(ROM_PROS_MAKE_SEGMENTS)`.

### 4.5 Done criteria
1. `diff_lts` checks all 28 routines more than 0 times each with 0 mismatches.
2. `golden_hybrid` reproduces every file × 29 modes byte for byte; every existing golden is unchanged.
3. `firmware_data` passes, unchanged at 38,197 converted bytes.
4. The three new goldens exist and reach what §4.1 lists.
5. `apollo2 --engine hybrid` equals `--engine rom` for every new text.
6. `src/firmware/` still builds without `machine/` or `cpu740/` headers. No unexpected `fw_rom8` fault occurs, and `tn_convert_clause` no longer calls `ROM_PROS_MAKE_SEGMENTS`.
7. `build.bat test` is green (31 suites).
8. The docs are updated:
   - the parent spec gets rev 11;
   - this spec's status changes, with "As built" if anything differs;
   - the README's hybrid section lists the stress and phrase passes.

## 5. Out of scope
- Everything at playback (M4.4): `par_*`, `boot_loop_play`, `pros_scale_target`, `pros_microprosody`, `cmd_run_event` and the `cmd_ev_*` handlers.
- `isr_par_ready` (M4.6).
- Raising `MACHINE_MAX_HOOKS`.
- `cmd_say_phonetic`, which stays ROM and unreachable.

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **`BEQ $CCBA` taken is reachable** (§4.4 says unreachable). In `A|A` the first `A`'s look-ahead skips the class-0 `|` and sees a vowel, so $CD61/$CD63 count no syllable while `$1C0C` is set. The `|` becomes `R` (a consonant before a vowel), $CC42 toggles the stress to 0 and clears the accent state, and $CCB7 finds `$1C0B` still 0: the branch is taken at the `R` and again at the last `A`. `test_stress_no_syllable` runs `A|A` through `pros_mark_stress` and checks the counts it leaves.
- **`syllabic_stress` ends with ` A|A.`** before its ` @X0` (§4.1): the committed text is `@X1. Bx7 BxM Bx7Z. BIA DIO BEA. A|A. @X0`, so diff_lts and golden_hybrid also compare the C on $CCBA taken. A branch probe (a scratch tool, not committed) gave $CCBA taken twice for this text alone, and the text still reaches $CC59 taken and $D0CD. `long_words` and `linking_r` are as in §4.1 (the probe confirmed the linking R four times in `linking_r` and once in `long_words`). `apollo2_coverage` cannot show $CC59 taken or $CCBA taken, whose targets ($CC60, $CCCA) other paths reach too; it shows $CBDB, $D0B5 and $D0CD, which were `MISSING` before the three texts.
- **The carry into $CCD5 cannot come from `pros_mark_stress`** (§4.4 says the count is at most 11 at $CCB2). $CB35 clears the position's shortening count, and only +3 ($CC96) and +5 ($CCA4) come before the `ADC #3` at $CCB2, which so sees at most 8 and never carries. To test the missing `CLC`, `pros_stress.h` declares `pros_stress_counts` (the block $CC60–$CCD7), a public function beyond the routines' own; `pros_mark_stress` is its only caller in `src/`. `test_stress_carry` runs it on RAM set up directly: after $FE + 3, $CCD5 adds 12 and the carry.
- **$CD02 lengthens position pos + 1**: after the `INX` at $CCFA both the class read at $CCFB (`ph_symbol_class[pos + 1]`, §2.7) and the `ADC #8` go to pos + 1. The next position clears that byte at its start ($CB32), so the add survives only when pos + 1 = `tn_phon_len`, on a stale byte past the text, mod 256. `test_stress_count_wrap` pins both (class 1 of `!` at index 33; $FC + 8 = $04).
- **`pros_store_phrase_len`'s `ORA #$80` is in A only** ($D105): both stores get bit 7, but RAM `pros_phrase_len` is then set to 0; the decompiler's `pros_phrase_len | 0x80` is wrong (`test_store_phrase_len`). A phrase's count then goes on from the next phrase's, because `pros_declination_step` stores `pros_sym_lengthen[pros_pos] & $7F` (at least 1) back into `pros_phrase_len` ($D40D–$D41A); `test_phrases_clause` has the first phrase's two symbols stored as 5.
- **Registers that §2.3 says flow on have no reader.** `pros_mark_phrases`' exit registers do not reach `pros_phrase_gen`'s first `isr_par_ready` ($D155): its first instruction, `JSR pros_declination` at $D129, writes A (`LDA pros_baseline_end`), X and Y (`LDX pros_pos` at $D40D and `LDY #0` at $D41D in `pros_declination_step`) before any read, so $D155 gets `pros_declination`'s X and Y. Likewise `pros_store_phrase_len`'s X = `pros_pos` is not what `pros_declination` sees: `pros_declination_step` loads X itself.
- **Thunk outputs as finally declared** (`src/engine/thunks.c`): A, X and Y for all four, as in §2.3, because the ROM leaves them defined; no caller reads a flag. The thunks of `pros_make_segments`, `pros_mark_stress` and `pros_mark_phrases` set all three from the C's `fw_regs`, because the C's `isr_par_ready` calls overwrite the machine's registers. `pros_store_phrase_len` makes no ROM call and never touches Y: its thunk sets A (1, or `pros_class` when its bit 3 is set) and X = `pros_pos`, and leaves Y. Of the entry registers, `pros_make_segments`, `pros_mark_stress` and `pros_mark_phrases` read only Y, at their first `isr_par_ready` (`pros_make_segments` through `pros_mark_stress`'s).
- **The C flush's entry Y** (§2.4): the C `tn_convert_clause` passes `pros_make_segments` Y = 0, the M4.1 convention it already used for the ROM call, where the ROM's Y at $B201 is what the last character's conversion left. That Y reaches only `pros_mark_stress`'s first `isr_par_ready` ($CB1F, before position 0; each position reloads Y at $CBAD), which preserves it, so no RAM differs. It is a known register difference; diff_lts skips the mid-clause flush and golden_hybrid covers it.
- **The registers at `isr_par_ready` $CB1F** (§2.4 says about a dozen paths). X = `pros_pos` on every path but the first, and Y = `pros_features` unless stated:
  - the start: A = `cmd_ev_count` (0), or with events the first position's high byte (`LDM` at $CB1C leaves A); X = 0 (`TAX` at $CAEC); Y = the caller's;
  - `~`: A = 8; `|` before a non-vowel: A = `$1C09`; `}`: A = $FF; `{`: A = 0; `` ` ``: A = 1;
  - the first of two equal consonants ($CD1B): A = `'|'`;
  - the common tail ($CD5B): A = `pros_features`; after a vowel Y = `$1C07`, the next symbol's features (`LDY` at $CDF0);
  - a word end ($CEE9): A = $FF, Y = `pros_features` = 0.

  The look-ahead's registers never reach $CB1F: $CBAD–$CBB3 and $CC60/$CC63 reload them. `test_stress_paths` checks one of each path, `test_stress_start` the start with and without events. The pass leaves ($CB2C) A and Y as at the last call and X = `pros_pos` (= `tn_phon_len`).
- **The registers at `isr_par_ready` $CF28**: A = `pros_features` as loaded at $CF1A, the previously walked position's (0 at the first, from $CF04), and X = `pros_pos`. Y:
  - at the first position: the entry Y (what `pros_mark_stress` left);
  - after `{`, a space, or a marker or other symbol with class bit 7 clear: the symbol (`TAY` at $CF37);
  - after a punctuation: `pros_declination`'s Y (the step, or on a rising baseline the step before its negation), through `pros_declination_y`;
  - after a phonetic symbol: its features (`LDY` at $D068).

  `test_phrases_clause` checks each path. The pass leaves ($CEF5) A = `pros_word_pos`, X = 0 (`pros_store_phrase_len`'s `LDX`) and that Y.
- **How often `isr_par_ready` is called**: `tn_phon_len` + 1 times by `pros_mark_stress`, because $CB1F comes before the end test (an empty text gets one call, so the pass does not return "at once" as §2.7 says; `test_stress_start`), and `tn_phon_len` − 1 times by `pros_mark_phrases` (for `tn_phon_len` ≥ 1), none at position 0, where $CF26 branches to the exit block first (`test_phrases_exit_only`: none for `pros_pos` 1).
- **RAM names** (§2.6): the unnamed state bytes the passes use are the 16 that §2.6 lists plus $1C0D (`FW_PROS_STRESS_TOGGLE`, which `pros_phrase_gen` also uses); no other routine's dump uses the 16, and $1C0A is unused. `fw.h` names the 16 `FW_PROS_NEXT_SYM` $1C06, `FW_PROS_NEXT_FEATURES` $1C07, `FW_PROS_NEXT_PHON` $1C08, `FW_PROS_NEXT_PHON_FEATURES` $1C09, `FW_PROS_SYLLABLES` $1C0B, `FW_PROS_VOWEL_SEEN` $1C0C, `FW_PROS_TILDE_SHORTEN` $1C11, `FW_PROS_WRITE_ONLY_1C12`, `FW_PROS_FINAL` $1C13, `FW_PROS_BACKQUOTE` $1C14, `FW_PROS_PUNCT_NOT_KIND3` $1C15, `FW_PROS_FINAL_LENGTHEN` $1C16, `FW_PROS_ACCENT_PENDING` $1C19, `FW_PROS_PREV_FEATURES_COPY` $1C1A, `FW_PROS_WRITE_ONLY_1C1C` and `FW_PROS_NUCLEUS_MASK` $1C1E. Meanings sharper than or different from §2.6:
  - $1C15: the punctuation `pros_store_phrase_len` stored last, which is the next one in the text, had no class bit 3; bit 3 (`PHC_KIND_3`) is set for `.` `:` `;` `?` only;
  - $1C0B in `pros_mark_phrases`: the syllable count of the word before the space or punctuation in the text, the one the backward walk enters next ($CF6B reads what `pros_mark_stress` stored there), not the following word's;
  - $1C13: in `pros_mark_stress` "the vowel's syllable ends a phrase" (class bit 0 of the first non-consonant from pos + 2 on, or from pos + 3 when the next phonetic symbol is `1` or `L` ($CDBA–$CDCE, `test_stress_past_end`), $CDEA; a space has class 0); in `pros_mark_phrases` "in the word's last syllable" (1 at its start and at each space and punctuation, 0 at a consonant before a vowel or long symbol); not "next is punctuation / phrase-final";
  - $1C19: in `pros_mark_stress` it lengthens each symbol by 4 (1 at the start and at word ends but a space, 0 at `~`, and a space copies $1C11 into it); in `pros_mark_phrases` it is the pending accent (1 at the start and at each `~`);
  - $1C06 becomes $0D when the look-ahead reaches `tn_phon_len`, even after it stored a space ($CB9D);
  - `pros_mark_phrases` writes $1C0D but never reads it; $1C1A is cleared at each word end ($CEDE), unlike `pros_prev_features`.
- **The `pros_sym_flags` bits** are `PSF_NUCLEUS` 7, `PSF_FUNC_WORD` $08, `PSF_UNRELEASED` $10, `PSF_ACCENT` $20 and `PSF_STRESS` $40 in `pros_flags.h`, which replace `pros_gen.c`'s own names. §2.6 says `pros_phrase_gen` reads the function word $08: it does not (it tests the symbol `~`). The function word $08 ($CBC9), the stress flag $40 ($CC77) and the accent $60 ($D0DC) are stored as the whole byte; only the unreleased stop (`ORA #$10`, $CCEB) and the nucleus mask ($D0E7) are ORed in. At a space or punctuation, and at position 0, the byte is not bits but the phonetic-symbol count of the word after it ($CF74, $CEF2).
- **`DIFF_MAX`** in `tests/test_diff_lts.c` was raised from 24 to 28: with more rows than it, diff_lts fails `fw_replacement_count <= DIFF_MAX` before checking anything. §2.3's hook budget names only `MACHINE_MAX_HOOKS` (29 of 32 used).
- **The C layout**:
  - `pros_addr.h` gained `ROM_PROS_MAKE_SEGMENTS` (moved from `tn_convert.h`), `ROM_PROS_MARK_STRESS`, `ROM_PROS_MARK_PHRASES_EXIT` ($CEEC), `ROM_PROS_MARK_PHRASES` and `ROM_PROS_STORE_PHRASE_LEN`;
  - `pros_make.c`: `void pros_make_segments(fw *f, fw_regs *r)`, with one static helper, `copy_params` (the copies in the ROM's order C, D, E, A, B);
  - `pros_stress.c`: `void pros_mark_stress(fw *f, fw_regs *r)` and the public `pros_stress_counts`; static helpers `start`, `look_ahead`, `marker`, `stress_toggle`, `consonant`, `vowel`, `save_features`, `word_end` and `position`;
  - `pros_phrase.c`: `void pros_mark_phrases(fw *f, fw_regs *r)` and `uint8_t pros_store_phrase_len(fw *f)` (returning A); static helpers `init_state`, `load_symbol`, `word_end`, `phrase_params`, `phrase_end`, `symbol_counts`, `accent_and_nucleus` and `exit_block`;
  - each pass file has its own one-line `par_ready` and 8-bit add helpers.
- **Smaller sharpenings**:
  - the "first of two equal symbols" rewrite ($CD18) happens only to consonants, and it skips the common tail (`JMP $CB1F` at $CD1B), so `pros_prev_features` and $1C1A keep the previous symbol's features (`test_stress_paths` has `MM` become `|M`);
  - `pros_mark_stress`'s look-ahead overwrites `pros_class` with the class of the next symbol whose class is not 0 ($CB5D, $CB8B; it skips class 0), so the `BBC1` at $CEBB tests that symbol's class (the current one's only when the scan reaches the text's end);
  - `pros_mark_phrases`' entry ($CEF6–$CF17) initialises neither `pros_pos` nor $1C15: it starts from the `pros_pos` that `pros_mark_stress` leaves (and decrements it at $CF20 per position) and from `pros_mark_stress`'s clear of $1C15 at $CAE0 (each `pros_store_phrase_len` call then clears it at $D11B and sets it to 1 at $D125 when `pros_class` has no bit 3);
  - the dead stores to $1C12 are at $CDCB and $CDD4 (§2.7's $CDD2 is the `LDA`);
  - `tests/test_fw_tn.c`'s flush test now checks that `boot_loop_play` gets `pros_phrase_gen`'s exit registers (A = $71, X = $FA, Y = $70 for 250 symbols) and 1374 `isr_par_ready` calls; its mock no longer answers $CA92, and its `MAX_CALLS` went from 1024 to 2048 because the C passes' `isr_par_ready` calls are recorded;
  - the CLI check of criterion 5 also covered `long_clause`, whose mid-clause flush now runs the C.
