# M4.3a — The segment generator in C (phoneme expansion and the phrase generator) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-03-m4-3a-segment-generator.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 9): §6 step 3, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the data image and the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`: the event producers whose queue this stage rewrites, the non-local exit, the `FW_TMP_*` names (§2, §3, §7 "As built").

**User decisions (2026-10-03):**
- M4.3 (phoneme expansion and prosody: 12 routines, 2,707 code bytes, 2,319 data bytes) is split into two plans. M4.3a, this spec, is the segment generator: phoneme expansion, the phrase generator, the append routines and the declination, with all of the data. M4.3b is the stress and phrase passes and `pros_make_segments`.
- Hooks: the three entries that ROM code still calls, plus the five leaves, so that a mismatch points at one small routine (as M4.0 did with the `lts_*` routines).
- Coverage gaps are closed with new corpus texts (goldens generated once by the rom engine; the existing goldens stay unchanged) and with unit tests for what no text reaches reliably.

## 1. Goal

When M4.3a is done, the segment generator runs as C in the `hybrid` engine. It:
- turns the marked phonetic string into the four segment arrays that playback reads, byte for byte as the ROM does;
- matches its ROM originals at every call site the corpus reaches, including new texts that make more than 256 segments in one clause;
- reads its tables (symbol classes and features, phoneme lists, duration tables, speed maps, lengthening steps) only from committed, readable C.

## 2. The subsystem (all main ROM)

### 2.1 Where it sits
- `boot_loop_text` → `tn_convert_clause` (C since M4.1) leaves the phonetic string in `tn_phonetic` ($1900) and `tn_phon_len` ($53), and the command events in `cmd_ev_*` ($1200–$1485), keyed by phonetic position.
- `pros_make_segments` $CA92 (ROM until M4.3b) then runs three passes over `tn_phonetic`:
  - `pros_mark_stress` $CABD, forward (ROM until M4.3b): stress flags and the shortening and lengthening counts per position;
  - `pros_mark_phrases` $CEF6, backward (ROM until M4.3b): phrases, accent, nucleus and phrase-final lengthening. It calls `pros_declination` per phrase;
  - `pros_phrase_gen` $D129, forward (**C**): the segments.
- `boot_loop_play` $A7D4 then plays. At playback, `par_play_start` $BAF2 calls `pros_reset_seg_ptrs`, and `par_read_segment` reads the segment arrays (all M4.4).

### 2.2 Ported to C (8 routines, about 1,020 bytes)
- **`pros_phrase_gen` $D129** (692 B). Forward over `tn_phonetic`. Per symbol:
  - it calls `isr_par_ready`;
  - it rewrites a due command event's position into the current 16-bit segment index;
  - it works out a pitch target: the declining baseline / 8, plus the accent, stress and tail terms, plus 105;
  - **phoneme expansion** (`ph_decode_symbol`, a label at $D31B): the symbol indexes its phoneme list of (phoneme, max, min) triples. Per triple the duration range is scaled by the shortening and lengthening counts (`ph_dur_shorten`, `ph_dur_lengthen`, `ph_lengthen_count`), mapped through the speed map, and one segment is appended;
  - word pauses (@Q) and phrase pauses (@D) are appended as pause segments (phoneme $3B), and two 4-frame pauses end the utterance.
- **`pros_append_segment` $CA3B** (54 B): one segment into the four arrays; the 16-bit index and the page pointers advance; calls `isr_par_ready`.
- **`ph_append_pause` $CA15** (38 B): a word pause.
- **`ph_append_phrase_pause` $C9B8** (93 B): a phrase pause. It exits with `JMP $CA6D` into `pros_append_segment`'s tail (`PLA / TAY / PLA / RTS`).
- **`ph_lengthen_count` $D43E** (13 B): reads `ph_lengthen_steps` at `$D44A,Y`.
- **`pros_declination` $D3DD** (48 B) and **`pros_declination_step` $D40D** (49 B): the per-phrase baseline step. `pros_declination` branches straight into the step, or (rising baseline) swaps, calls it and negates.
- **`pros_reset_seg_ptrs` $CA71** (33 B): the four page pointers back to the array starts.

Inside C, `pros_phrase_gen` calls the C leaves directly ("C calls C").

### 2.3 Hooks (8 new replacements; 24 in all, 25 `golden_hybrid` modes)

| Hook | ROM callers | Declared outputs |
|---|---|---|
| `pros_phrase_gen` $D129 | `pros_make_segments` $CAB9 | A, X, Y: the ROM leaves A = the old `pros_seg_count` low byte, X = `pros_pos`, Y = the last list index. No caller reads them; they are defined, so they are declared. |
| `pros_declination` $D3DD | `pros_mark_phrases` $D04D (and `pros_phrase_gen`, C to C once ported) | A (the step, or its negation on the rising path), plus anything else the caller reads after the call |
| `pros_reset_seg_ptrs` $CA71 | `pros_make_segments`, `par_play_start` $BAF5 | A = $0E |
| `pros_append_segment` $CA3B | `pros_phrase_gen` | what the callers read after the call |
| `ph_append_pause` $CA15 | `pros_phrase_gen` | as above |
| `ph_append_phrase_pause` $C9B8 | `pros_phrase_gen` | as above |
| `ph_lengthen_count` $D43E | `pros_phrase_gen` | as above (the result is in A) |
| `pros_declination_step` $D40D | `pros_declination`, by `JSR` and by tail-branch | A |

- Outputs are declared exactly as the ROM callers read them after the call, checked in the dumps and stated in a comment; when in doubt, more.
- The five leaves are reached from ROM only while `pros_phrase_gen` is ROM, that is, in the "each alone" modes. That is their purpose: a mismatch points at the leaf.
- The hook is on the PC, so the tail-branch from the ROM `pros_declination` into $D40D also enters the C step. Its return pops `pros_declination`'s return address, as the ROM's `RTS` does, and `diff_lts`'s reference copy finishes when SP rises above entry.
- `ph_append_phrase_pause`'s `JMP $CA6D` lands past the hook at $CA3B, so no hook fires mid-routine; the C does the same restores itself.
- Hook budget: 24 of `MACHINE_MAX_HOOKS` (32). M4.3b adds three (27). Raising the limit is M4.4's.

### 2.4 ROM calls from C (strict budget, `FW_ROM_CALL_BUDGET`)

| ROM routine | Called by | Why it stays ROM |
|---|---|---|
| `isr_par_ready` $C5D6 | `pros_phrase_gen` (per symbol), `pros_append_segment` (per segment) | parallel-port handshake: an SFR write (P0.3) in parallel mode (M4.6). It preserves A. |

It is called at exactly the ROM's points, with the ROM's registers. Nothing in scope plays audio, waits on an interrupt, touches the DSP or resets the stack: no flush, no unwind.

### 2.5 The output contract (the M4.4 boundary), kept bit for bit
- The four segment arrays `pros_seg_phoneme` $0200, `pros_seg_duration` $0600, `pros_seg_pitch` $0A00 and `pros_seg_flags` $0E00 (1 KiB each, 16-bit index). The phoneme array overwrites the RAMCODE copy at $0200–$03FE, harmlessly, as in ROM (`bank_load_ramcode` recopies before every use).
- `pros_seg_count` $17F6 (the index of the last segment) and the four page pointers $22–$29.
- `cmd_ev_positions` $1206, rewritten in place to segment indexes; `cmd_ev_pos` and `cmd_ev_next`.
- The "utterance has content" flag $6B, set with the ROM's `LDM #$FF`; `boot_loop_play` tests it.
- Every scratch write, including $78.

### 2.6 RAM names
`fw_ram.h` is generated from the read-only annotations, so the bytes they leave unnamed get names in `fw.h`, as M4.2's `FW_TMP_76`:
- `FW_UTT_CONTENT` $6B: "utterance has content", set at each word pause and each triple;
- `FW_PROS_STRESS_TOGGLE` $1C0D;
- `FW_PROS_WORD_U` $1C1F: the U term of a word's first symbol;
- `FW_TMP_78` exists: the list index.

The plan confirms the exact set against the dumps. The other twelve unnamed $1C06–$1C1F bytes belong to the stress and phrase passes (M4.3b).

### 2.7 Quirks kept
- **Pitch-target carry chain** ($D2DF–$D2EE): `LSR` ×3, the last bit shifted out being the carry; then `ADC` accent, stress, tail and `#$69` with no `CLC` between, so each sum's carry feeds the next. It occurs in the corpus.
- **Duration loops**: shortening and lengthening run Y = (n+1)/2 steps with `DEY / BNE`, so Y = 0 means 256 steps; each step goes through the ×0.9 or ×1.1 table; the result is mapped through the speed map at duration / 8 (`LDA ($3A),Y`).
- **Declination**: the rising baseline swaps `pros_baseline` and `pros_baseline_end`, calls the step, negates it (`0 − step`) and swaps back, so A differs by path. The step divides by repeated subtraction and caps at 25. It stores its divisor back into `pros_phrase_len` and bumps 0 to 1, so each phrase's count starts from the following phrase's count.
- **16-bit segment index**: on a page wrap the four pointers advance in the ROM's order (phoneme, pitch, duration, flags); the final decrement is 16-bit.
- **Phrase pauses**: a pause that computes 0 becomes 1 (`INC A`); @D0 at punctuation adds no pause.
- **Event rewrite**: positions are compared on the low byte only; `cmd_ev_pos+1` is zeroed; the next event's position is read past `cmd_ev_count`; `TXA / ASL` doubles the index, so an index of 128 or more aliases (the queue limit M4.2 kept).
- **`ph_lengthen_count`** reads `$D44A,Y`: Y = 0 reads the routine's own `RTS` opcode ($60) as a 96-step entry.
- **Timing**: as in M4.1 and M4.2, the C takes no CPU cycles (in ROM this pass takes up to about 127 ms of a clause), so serial arrival moves relative to the main level. The frames have stayed invariant; the goldens are the check.

## 3. Converted data (R5)

All 2,319 bytes, read by the decoder. `firmware_data` goes from 35,878 to **38,197** converted bytes.

| ROM range | Table | Readable form |
|---|---|---|
| $9BB4–$9CB3 | `ph_symbol_class` (256) | one value per symbol with named class bits (phonetic, vowel, punctuation and its kind), the symbol as a comment. It joins M4.2's $9804–$9BB3. |
| $9CB4–$9DB3 | `ph_symbol_features` (256) | one value per symbol with named feature bits (vowel, consonant, stop, fricative, nasal, liquid or glide, long or syllabic, voiced). Its last 64 bytes are also the unused pointer slots of symbols $00–$1F; the encoder writes them once, as features, and asserts that. |
| $9DB4–$9F73 | phoneme-list pointers (192 + 256) | not in C: the encoder generates them from the lists' layout. |
| $A187–$A396 | the phoneme lists (528) | per symbol $20–$FF in ROM order, its (phoneme, max, min) triples, phoneme names as comments where the annotations give them, and a word-pause flag for the space's `5F` item. The encoder lays them out end to end and asserts they fill the range exactly. |
| $C67B–$C87A | `ph_dur_lengthen`, `ph_dur_shorten` (256 each) | numeric arrays (about ×1.1 and ×0.9, capped at 255), the ratio in a comment. |
| $C898–$C9B7 | `cmd_speed_map_data` (9 × 32) | nine named maps of 32 durations; M4.2's `cmd_speed_maps` points into it. With M4.2's $C87B–$C897, $C67B–$C9B7 becomes one range. |
| $D44A–$D468 | `ph_lengthen_steps` (31) | entries 1–30 are the annotated table; entry 0, $D44A, is the `RTS` opcode of `ph_lengthen_count`, converted as data with a comment. It joins M4.2's $D469. |

- `tools/extract_tables.py --prosody-only` writes the draft of `src/firmware/data/pros_data.c`; addresses and the `fw_*` arrays go in `fw_data.h`.
- Not converted: the 617 bytes of $FF padding after the lists; the @T sound tables and the voice tables (M4.4); `boot_prosody_defaults` (M4.6).

## 4. Verification

### 4.1 New corpus texts (goldens from the rom engine, once)
| File | Text | Reaches |
|---|---|---|
| `phrase_pauses` | `@D1 Yes, no (maybe) - fine. @D3 "Quote" [one]. @D0 Stop. Go!` | a phrase pause that computes 0 ($C9EC); @D0 at punctuation ($D1D8) |
| `baseline` | `@RA40 @RBC0 Rising tone. @RF20 @RG90 Up, again. @RA80 @RB80 Level tone.` | the rising and level declination paths ($D3E5–$D40C) |
| `phonetic_stress` | `@X1 K}AE{T 8 X M}AE`T?@X0` | symbols with class b7 and no features ($D300); a second stress in a word ($D28F) |
| `phrase_127` | `@X1.` and a phrase of exactly 127 phonetic letters | the divisor 0 → 1 ($D41A) |
| `seg_256` | `@Q9 `, then the first 54 words of the repeating cycle `a go i to it o at`, then ` oh.` | a final count with low byte 0 ($D19B) |
| `seg_wrap_phrase` | as `seg_256`, plus ` oh oh.` | the page wrap at a phrase pause ($CA07) |
| `seg_wrap_word` | `@Q9 `, then the first 55 words of the same cycle, then `.` | the page wrap at a word pause and at a segment ($CA2F, $CA62) |

- The corpus goes from 31 to 38 texts.
- The three segment-count texts depend on exact counts, so each is its own file; the plan confirms every target with the coverage tool before the goldens are written.
- The gaps in the stress and phrase passes (the linking R, words of 5 or more syllables, stressed syllabic consonants) are M4.3b's texts.

### 4.2 `diff_lts`
- Checks all 24 routines, each more than 0 times, with 0 mismatches.
- The flush skip and the unwound-call count are unchanged.

### 4.3 `golden_hybrid`
- Every file × 25 modes (all routines, then each alone), byte for byte.

### 4.4 Unit tests (`tests/test_fw_pros.c`, a RAM image and a mock host that records every ROM call)
- One or more cases per routine, with the `isr_par_ready` calls and their registers.
- What no text reaches reliably:
  - the 256-step duration loops (count $FF);
  - `ph_lengthen_count`'s entry 0 ($D44A) and a half-count of 31 or more ($D442);
  - a page wrap at each append point, and the final 16-bit decrement;
  - the carry chain with each carry set;
  - the declination's falling, level and rising paths, the cap at 25 and the divisor bump;
  - the event rewrite: low-byte matching, the read past the count, an index of 128 or more.

### 4.5 Done criteria
1. `diff_lts` checks all 24 routines more than 0 times each with 0 mismatches.
2. `golden_hybrid` reproduces every file × 25 modes byte for byte; every existing golden is unchanged.
3. `firmware_data` passes with 38,197 converted bytes.
4. The new goldens exist and reach what §4.1 lists.
5. `apollo2 --engine hybrid` equals `--engine rom` for every new text.
6. `src/firmware/` still builds without `machine/` or `cpu740/` headers. No unexpected `fw_rom8` fault occurs.
7. `build.bat test` is green (31 suites).
8. The docs are updated: the parent spec gets rev 10, this spec's status changes (with "As built" if anything differs), and the README's hybrid section lists the segment generator.

## 5. Out of scope
- `pros_make_segments`, `pros_mark_stress`, `pros_mark_phrases` and `pros_store_phrase_len` (M4.3b), and with them the C `tn_convert_clause` calling the C `pros_make_segments` directly.
- Everything at playback (M4.4): `par_*`, `pros_scale_target`, `pros_microprosody`, `cmd_run_event` and the `cmd_ev_*` handlers; `pros_pitch_code_dither` is dead code.
- `isr_par_ready` (M4.6).
- Raising `MACHINE_MAX_HOOKS`.

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **The end of the utterance** (§2.2 says two 4-frame pauses): one pause of 4 frames, then one of 128 ($80) frames. `ph_append_pause` leaves A = $80 (its last `LDA #$80`, the segment flags), and the second `JSR` at $D193 takes that A as its duration (`pros_gen.c`, the end of `pros_phrase_gen`).
- **The duration loops** (§2.7 and §4.4 say both run 256 steps for a count of $FF): only the shortening loop can, because its Y = (n + 1) / 2 = 0 goes straight into `DEY / BNE`. The lengthening count goes through `ph_lengthen_count`: a count of $FF gives a half-count of 0 and so entry 0 of `ph_lengthen_steps`, the `RTS` opcode $60, which is 96 steps. Entries 1–30 are 1–15 and a half-count of 31 or more gives half of it, so the lengthening loop never runs more than 96 steps (`append_triple` in `pros_gen.c`, `ph_expand.c`, the unit test `test_gen_duration_loops`: 256 steps shortening, 96 lengthening).
- **`pros_phrase_gen`'s exit registers** (§2.3 says Y = the last list index): A = `pros_seg_count`'s low byte before the final `DEC`, X = `pros_pos` (= `tn_phon_len`), Y = the old `pros_seg_count` low byte that the second end pause's `LDY` leaves ($D196–$D1A1 change neither X nor Y). The C signature is `uint8_t pros_phrase_gen(fw *f, uint8_t *x, uint8_t *y)`, returning A.
- **Thunk outputs as finally declared** (`src/engine/thunks.c`): A, X and Y for `ph_append_phrase_pause`, `ph_append_pause`, `pros_append_segment`, `pros_reset_seg_ptrs`, `pros_phrase_gen`, `pros_declination` and `pros_declination_step`; A and Y for `ph_lengthen_count`. §2.3 named A = $0E for `pros_reset_seg_ptrs`, A for `pros_declination_step` and the result in A for `ph_lengthen_count`. Why (no caller reads a flag):
  - X and Y reach `isr_par_ready` at $D155 through `JMP $D152` after the phrase pauses, a segment, the declination and its step; Y also reaches it at $CF28 in `pros_mark_phrases` (after `pros_declination`) and at $CB1F in `pros_mark_stress` (after `pros_reset_seg_ptrs`, from `pros_make_segments`);
  - `ph_append_pause`'s A is the second end pause's duration, and after a word pause it goes on to `isr_par_ready` (`LDM` does not change A);
  - `ph_lengthen_count`'s Y is the loop count its caller reads (`DEY`); X is untouched, and the caller writes it (`TAX`) before reading it;
  - `pros_phrase_gen`'s registers have no reader (its callers go on to `boot_loop_play`, which loads A from $6B), but they are defined, so they are declared.
- **`pros_declination`'s registers** (§2.3 says A is the step or its negation): on the rising path A is the restored `pros_baseline` and Y the step before the negation (the negation is in RAM only); on a falling or level baseline A = Y = the step. X = `pros_pos` on every path. The C returns A only; the inline helper `pros_declination_y` in `pros_decl.h` recovers the Y from the RAM the routine leaves (`pros_baseline_end > pros_baseline` still marks the rising path, because the swaps restore both bytes). The thunk and `pros_phrase_gen`'s first `isr_par_ready` use it.
- **`isr_par_ready` preserves A, X and Y** (§2.4 says A), which is why `pros_append_segment`'s thunk hands its entry registers back.
- **The registers at `isr_par_ready` $D155** depend on how the loop reached $D152; the C tracks them as the ROM has them:
  - the first time: A = $FF, X = `pros_pos` before its reset, Y = `pros_declination`'s Y;
  - after punctuation: A = 0, Y = the symbol (the phrase pauses preserve A, X and Y);
  - after a marker (class bit 7 clear): A = Y = `pros_class`;
  - after a hypermode skip: A = `pros_func_word`, Y = `pros_class`;
  - at a list's end byte: A = $FF (or 0 at the space's word-pause item with @Q0), Y = the list index;
  - after a word pause: A = $80, X = `pros_pos`, Y = `$78`;
  - after the closure of an unreleased stop: A = `pros_sym_flag`, Y = the speed-map index;
  - after a triple, X is the index of the last `ph_dur_lengthen` lookup when the position has a lengthening count, else `pros_pos`. That X also goes to `pros_append_segment`'s `isr_par_ready`.
- **Phrase pauses** (§2.7 says only that 0 becomes 1): punctuation appends two in a row ($D1DA, $D1DD) unless `cmd_phrase_pause` is 0. After `.` `?` `!` the pause is `cmd_phrase_pause` in full, unmasked; after `,` `;` `:` it is `(cmd_phrase_pause >> 1) & $0F`, after any other symbol `(cmd_phrase_pause >> 2) & $0F`. The 0 → 1 bump (`INC A` at $C9EC) applies on all three paths, so the mask can make 0 and 0 becomes 1 (`@D` $20 after `,` gives 1). Every pause, the word and end pauses included, also stores $3B into `ph_phoneme`; no pause writes `pros_last_target` or calls `isr_par_ready`.
- **The declination step** (§2.7): `pros_phrase_len = pros_sym_lengthen[pros_pos] & $7F` is stored first and then bumped from 0 to 1 ($D41A), so a stored count of $00 or $80 ends as 1. The difference `pros_baseline − pros_baseline_end` is 8 bits, and the repeated subtraction stops only when the remainder is below the divisor, so an exact multiple counts its last subtraction.
- **`$6B`** (`FW_UTT_CONTENT`, §2.5) is set by `LDM #$FF` at a word pause only when @Q ≠ 0 ($D35A), and before each triple ($D360). Nothing in the routine clears it.
- **One pointer table for every symbol**: the decoder's list pointer is at `PH_LIST_PTRS + 2 * symbol`. The `CLC` at $D31E is dead (`ASL` sets the carry), and the $80–$FF branch reads $9E74 = $9D74 + $100.
- **The C layout**:
  - `pros_addr.h`: the ROM addresses (with `ROM_PH_DECODE_SYMBOL` and `ROM_PROS_SEG_TAIL` $CA6D);
  - `pros_seg.c`: the four appenders, sharing one static pause body (`append_pause`: $C9ED–$CA11 is the same code as $CA15–$CA39) and one page-wrap helper (`seg_advance`);
  - `ph_expand.c`: only `ph_lengthen_count`;
  - `pros_decl.c`: the declination and its step, with `pros_declination_y` in `pros_decl.h`;
  - `pros_gen.c`: `pros_phrase_gen`. `ph_decode_symbol` is a label inside it, so it and the other blocks are static helpers: `rewrite_events`, `word_start`, `symbol_terms`, `pitch_target`, `decode_symbol` and `append_triple`.
- **The RAM names added to `fw.h`** are exactly the set §2.6 expected: `FW_UTT_CONTENT` $6B, `FW_PROS_STRESS_TOGGLE` $1C0D and `FW_PROS_WORD_U` $1C1F, with the existing `FW_TMP_78`.
- **The converted data** (§3): no phoneme has a name in `ghidra740/annotations` (they are numbered), so the list comments are the symbols and the annotation's notes. `fw_data.h` also gained `UNUSED_A397` (the 617 bytes of $FF after the lists), `PH_WORD_PAUSE` ($5F), the class and feature bit names `PHC_*` and `PHF_*`, and the types `fw_ph_triple` and `fw_ph_list`. The encoder now also checks that M4.2's `cmd_speed_maps` point at the start of a map.
- **Two corpus texts were retuned from §4.1** (the other five are as listed there):
  - `phrase_127` is `@X1. ` followed by 15 × `BABABABA ` and `BABABAB.`: exactly 127 letters (the spaces do not count). A 128-letter phrase never reaches $D41A; a sweep of 110–149 letters reached it only at 127.
  - `seg_wrap_word` is two clauses, each led by `@Q9 `: the first 55 words of the cycle and `.`, then the same 55 words and ` oh.`. One clause makes fewer than 512 segments, so it wraps only once: the first clause wraps at a word pause ($CA2F), the second at a segment ($CA62). Without the second `@Q9 ` the second clause wraps at a phrase pause instead. The committed file is one line:

    ```
    @Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o. @Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o oh.
    ```
