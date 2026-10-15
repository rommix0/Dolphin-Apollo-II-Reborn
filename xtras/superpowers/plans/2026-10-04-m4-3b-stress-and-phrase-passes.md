# M4.3b The Stress and Phrase Passes in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `pros_make_segments` and the stress and phrase passes (`pros_mark_stress`, `pros_mark_phrases`, `pros_store_phrase_len`) to C, so that all of prosody before playback runs as C, and prove it against the ROM, live and with the goldens.

**Architecture:**
- **Corpus.** Three new corpus texts with rom-engine goldens reach the linking R, words of five or more syllables and a stressed syllabic consonant at the nucleus, and the `BEQ $CCBA` that spec §4.4 thought unreachable (Task 1).
- **New C files.**
  - `src/firmware/pros_phrase.c` (the backward pass and `pros_store_phrase_len`);
  - `pros_stress.c` (the forward pass);
  - `pros_make.c` (`pros_make_segments`);
  - the shared `pros_sym_flags` bits in `pros_flags.h`.

  They run on the firmware's RAM image through the `fw` context, one static helper per ROM block, and track the registers that reach `isr_par_ready`, their only ROM call.
- **C calls C.** `pros_make_segments` calls the passes and M4.3a's `pros_phrase_gen` directly. The C `tn_convert_clause` calls the C `pros_make_segments` directly.
- **Hooks.** All four routines, giving 28 replacements and 29 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md` (M4.3b spec). Its foundations are:
- the M4.3a spec, `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md` (§6 "As built");
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 10).

## Global Constraints

**Build and run**
- Build and test from the repo root with the Bash tool (Git Bash): `cmd //c ".\build.bat test [SUITE_FILTER]"`.
- In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional).
  - Fix warnings in code. Never add `/wd` flags or pragmas.
  - The only pragma allowed is the existing 4611 one in `thunks.c`. `setjmp` therefore stays out of the tests.
- Every `.c` file under `src/` and `tests/` must have a **unique basename**, because `build.bat` puts all objects in `build\`.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers.
- Audio stays 10 kHz native, with no resampling.

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C: numbers with their meaning, named bits, strings, named addresses.
- Stage files by explicit path only. Never use `git add -A` or `git add .`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`, `apollo2.png`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking the C against the ROM (git-ignored, read-only) are in the main checkout:
  - `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt`
  - `rom_C5D6_isr_par_ready.txt` is truncated: `isr_par_ready` is `PHA … PLA / RTS` and preserves A, X and Y.

**Correctness rules**
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The three new goldens are generated once by the rom engine in Task 1. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.3b spec §2.7):
  - every RAM write, write-only bytes included (`$1C12`, `$1C1C`, every `$1Cxx` byte, every per-position array byte, the `tn_phonetic` rewrites, `cmd_ev_pos`/`cmd_ev_next`, `$6B`);
  - the 8-bit wraps, and the carry into the `ADC #$0C` at `$CCD5` (no `CLC`);
  - reads past ends (`tn_phonetic` past `tn_phon_len`, X wrapping in 8 bits);
  - every `isr_par_ready` call at the ROM's point with the ROM's registers.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts` excludes it.
- ROM data is read only through `fw_rom8` at its ROM address, from the converted image.
- Thunk outputs: declare exactly what the ROM callers read after the call, checked in the dumps and stated in a comment; when in doubt, declare more (the spec's default is A, X and Y).
- `diff_lts` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's standing rule and overrides any other attribution line the environment suggests.

**Counts through the plan** (all measured in the proof run; the state before Task 1 is 31 suites, 24 replacements, `39 corpus files x 25 modes`, 24 `diff_lts` lines):

| After | Corpus names | Replacements | `golden_hybrid` | `diff_lts` lines | New `diff_lts` N |
|---|---|---|---|---|---|
| Task 1 | 41 | 24 | `42 corpus files x 25 modes` | 24 | (every N grows with the texts) |
| Task 2 | 41 | 26 | `42 corpus files x 27 modes` | 26 | `pros_mark_phrases 207`, `pros_store_phrase_len 374` |
| Task 3 | 41 | 27 | `42 corpus files x 28 modes` | 27 | `pros_mark_stress 207` |
| Task 4 | 41 | 28 | `42 corpus files x 29 modes` | 28 | `pros_make_segments 207` |

Throughout: 31 suites; `38197 converted bytes compared with the ROM, 0 differ`; `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`. `DIFF_MAX` in `tests/test_diff_lts.c` goes from 24 to 28 in Task 2, once.

## File Map

| File | Task | Responsibility |
|---|---|---|
| `tests/corpus/long_words.txt`, `linking_r.txt`, `syllabic_stress.txt`, their goldens `tests/golden/<name>.frm`, `tests/corpus_list.h` | 1 | three corpus texts with rom-engine goldens that reach the passes' rare paths |
| `src/firmware/fw.h` | 2 | the 16 RAM names of the passes' `$1Cxx` state bytes; `FW_PROS_STRESS_TOGGLE`'s comment |
| `src/firmware/pros_flags.h` (new), `src/firmware/pros_gen.c` | 2 | the `pros_sym_flags` bits, shared; `pros_gen.c` switched to them |
| `src/firmware/pros_addr.h` | 2, 3, 4 | the ROM addresses: the phrase pass, its exit block and `pros_store_phrase_len` (2), `pros_mark_stress` (3), `pros_make_segments`, moved from `tn_convert.h` (4) |
| `src/firmware/pros_phrase.h`, `src/firmware/pros_phrase.c` (new) | 2 | `pros_store_phrase_len` and `pros_mark_phrases` with its exit block |
| `src/firmware/pros_stress.h`, `src/firmware/pros_stress.c` (new) | 3 | `pros_mark_stress`, and `pros_stress_counts` (its block $CC60–$CCD7, public for the test of the carry into $CCD5) |
| `src/firmware/pros_make.h`, `src/firmware/pros_make.c` (new) | 4 | `pros_make_segments`, calling the C passes and `pros_phrase_gen` |
| `src/firmware/tn_convert.h`, `src/firmware/tn_convert.c` | 4 | `ROM_PROS_MAKE_SEGMENTS` removed; the mid-clause flush calls the C `pros_make_segments` |
| `src/engine/thunks.c` | 2, 3, 4 | the four hooks (register mappings, `THUNK` lines, table rows in address order) |
| `tests/test_diff_lts.c` | 2 | `DIFF_MAX` 24 → 28 |
| `tests/test_fw_pros.c` | 2, 3, 4 | the unit tests of the three passes and `pros_make_segments` |
| `tests/test_fw_tn.c` | 4 | the flush test, its mock host and `MAX_CALLS` |
| `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md`, `README.md` | 5 | parent spec rev 11, M4.3b status and "As built", README |

---

### Task 1: Corpus: three new texts and their rom-engine goldens (spec §4.1)

**ROM facts checked / what was verified while proving the plan** (scratch worktree off main c04267e, ROM engine only; everything below was run, nothing is copied from the spec):
- **`syllabic_stress` differs from spec §4.1: it ends with ` A|A.` before its ` @X0`.** Spec §4.4 calls `BEQ $CCBA` taken (in `pros_mark_stress`, `rom_CABD_pros_mark_stress.txt`) unreachable, "because the accent state is cleared only after a vowel has counted a syllable". It is reachable: in `A|A` the first `A`'s look-ahead skips the `|` (class 0) and sees the second `A`, so `$CD61`/`$CD63` count no syllable while `$1C0C` is set; the `|` becomes `R` (a consonant before a vowel), `$CC42` toggles the stress to 0 and clears the accent state, and `$CCB7` finds `$1C0B` still 0 (taken at the `R` and again at the last `A`). The text was extended rather than given a fourth file: the probe confirms that the extended text still reaches everything the spec's text reached ($CC59 taken, $D0CB fall, so $D0CD) and now also $CCBA taken (table). Task 3's `test_stress_no_syllable` also pins it on `A|A` directly.
- **How the corpus is fed** is unchanged from the M4.3a plan: `suite_golden`, `suite_golden_hybrid`, `diff_lts` and `apollo2_coverage` read the whole file as one string, strip trailing CR/LF and speak it on a fresh machine without boot. One line plus LF is the same as no LF.
- **Every target was reached, each text run alone through the branch probe** (a scratch tool, not part of the plan: `NOCORPUS=1 brprobe.exe TEXTFILE`, run from the main checkout's root). The branches are in `rom_CABD_pros_mark_stress.txt` ($CBD6 `BBS7 A,$CBDB`, $CC59 `BNE $CC60`, $CCBA `BEQ $CCCA`) and `rom_CEF6_pros_mark_phrases.txt` ($D0B3 `BCC $D0BE`, $D0CB `BBS7 A,$D0D1`):

| Text | CBD6 (taken = linking R, $CBDB) | D0B3 (fall = $D0B5, 5+ syllables) | D0CB (fall = $D0CD, syllabic nucleus) | CC59 (taken, accent state set) | CCBA (taken, no syllable yet) |
|---|---|---|---|---|---|
| `long_words` | taken 1, fall 0 | taken 13, **fall 46** | taken 2, fall 0 | not executed | taken 0, fall 47 |
| `linking_r` | **taken 4**, fall 2 | taken 28, fall 0 | taken 4, fall 0 | taken 0, fall 1 | taken 0, fall 4 |
| `syllabic_stress` | taken 1, fall 0 | taken 22, fall 0 | taken 2, **fall 1** | **taken 2**, fall 0 | **taken 2**, fall 0 |

  So: `$D0B5` is reached only by `long_words`; `$D0CD` only by `syllabic_stress`; `$CBDB` by `linking_r` (4 times), once by `long_words` and once by `syllabic_stress` (its `A|A`); `CC59` taken and `CCBA` taken only by `syllabic_stress` (2 times each). The spec's "four times" for `linking_r` and "once" for `long_words` are confirmed. (Without ` A|A.`, `syllabic_stress` gives CC59 taken 2, D0CB taken 1 fall 1, CCBA not executed.)
- **`$CC59` taken and `$CCBA` taken cannot be seen in the coverage file.** Their targets `$CC60` and `$CCCA` are also reached by other paths. Only the probe shows them (table). `$CBDB`, `$D0B5` and `$D0CD` are targets or fall-through instructions reached no other way, so `apollo2_coverage` does show them: **before this task** (main c04267e, `build/apollo2_coverage.exe`) all three are `MISSING` and the summary is `rom 4040, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`; **after**: all three `ok`, summary `rom 4054` (14 new instruction addresses), the other three numbers unchanged.
- **The RED step is already at 25 modes.** `golden_hybrid` prints `42 corpus files x 25 modes (all, then each routine alone)` as soon as `corpus_list.h` is edited (the count includes boot); the mode count is the 24 replacements + "all" and does not change in this task (it changes by +2/+1/+1 in Tasks 2-4). The RED failure count is 3 + 3 x 25 = 78.
- **`CORPUS_NAMES` layout.** The M4.3a row ends the file's list with a bare `"seg_wrap_word"` (no trailing comma/backslash). The edit therefore gives that name a `, \` and adds the new row `"long_words", "linking_r", "syllabic_stress"` as the last line. The header comment's "The last seven (M4.3a)" becomes "The next seven (M4.3a)" (as M4.3a did to "The last five (M4.2)"), then a new "The last three (M4.3b)" sentence is appended.
- **Peak.** The three texts raise the `suite_synth` plain corpus peak from 18727 (-4.9 dBFS, the M4.3a state) to **20522 (-4.1 dBFS)**; the b11 peak stays 15784 (-6.3 dBFS). Still inside -12...-1 dBFS and no clipping.
- **Timing** (this machine): the whole `build.bat test` took about 2 min 30 s after the task (it includes the build); the RED run of the two golden suites about 2 min 20 s.
- **Counts that this task leaves unchanged** (verified in the full run): 31 suites, 24 `diff_lts` lines, `38197 converted bytes compared with the ROM, 0 differ`, `1 mid-clause flush calls skipped` and `2 unwound calls compared`. Every `diff_lts` N grows with the new texts (before -> after, for the record: cmd_apply_speaker 89 -> 95, cmd_run_due 4615 -> 4950, cmd_scan_clause 192 -> 207, cmd_match_table 2513 -> 2623, lts_affix_next 3658 -> 4062, lts_affix_prev 1168 -> 1476, lts_sym_vowel 1065 -> 1167, lts_match_symbol 13514 -> 14938, tn_say_char 19 -> 19, lts_match_list 10239 -> 11350, tn_rule_skip_text 28144 -> 30185, tn_rule_skip_field 12118 -> 13468, tn_append_phonetic 819 -> 870, lts_convert_char 810 -> 861, tn_convert_clause 191 -> 206, cmd_hex_value 125 -> 130, ph_append_phrase_pause 306 -> 330, ph_append_pause 754 -> 784, pros_append_segment 2098 -> 2249, pros_reset_seg_ptrs 339 -> 365, pros_phrase_gen 192 -> 207, pros_declination 347 -> 374, pros_declination_step 347 -> 374, ph_lengthen_count 1038 -> 1101). `tn_say_char` stays 19.
- The three texts contain no control character other than the `@` commands shown; none of them depends on a segment count, so (unlike the `seg_*` files) a trailing-whitespace change would not alter the golden, but do not edit them anyway.

**Files:**
- Create: `tests/corpus/long_words.txt`, `tests/corpus/linking_r.txt`, `tests/corpus/syllabic_stress.txt`, and their three goldens `tests/golden/long_words.frm`, `tests/golden/linking_r.frm`, `tests/golden/syllabic_stress.frm` (generated)
- Modify: `tests/corpus_list.h`

**Interfaces:**
- Consumes: nothing.
- Produces: the corpus names `"long_words"`, `"linking_r"` and `"syllabic_stress"`. Every later task's `golden_hybrid` and `diff_lts` runs over them (Tasks 2-4 prove each new C routine against the ROM on these texts); Task 4's CLI check (`--engine hybrid` equals `--engine rom`) uses the three files.

- [ ] **Step 1: Write the corpus texts**

Each file holds exactly one line, ending with a single LF. Write them with the script below, so the bytes are exact:

```bash
python - <<'EOF'
t = {
    "long_words": "Internationalization is unbelievably uncharacteristically complicated.",
    "linking_r": "Far away, for ever, the car is here, four apples.",
    "syllabic_stress": "@X1. Bx7 BxM Bx7Z. BIA DIO BEA. A|A. @X0",
}
for name, text in t.items():
    open("tests/corpus/" + name + ".txt", "w", newline="\n").write(text + "\n")
EOF
wc -c tests/corpus/long_words.txt tests/corpus/linking_r.txt tests/corpus/syllabic_stress.txt
```

Expected `wc -c` (bytes, including the newline): 71, 50, 41 (and `162 total`).

The resulting lines, for review:

| File | Line |
|---|---|
| `long_words.txt` | `Internationalization is unbelievably uncharacteristically complicated.` |
| `linking_r.txt` | `Far away, for ever, the car is here, four apples.` |
| `syllabic_stress.txt` | `@X1. Bx7 BxM Bx7Z. BIA DIO BEA. A|A. @X0` |

- [ ] **Step 2: Add them to the corpus list**

In `tests/corpus_list.h`:
- Replace the comment line

```c
   The last seven (M4.3a) reach the segment generator's rare paths: a phrase pause that computes 0, and @D0;
```

  with:

```c
   The next seven (M4.3a) reach the segment generator's rare paths: a phrase pause that computes 0, and @D0;
```

- Replace the comment line

```c
   pause, at a word pause and at a segment). The seg_* texts depend on exact segment counts: one per file. */
```

  with:

```c
   pause, at a word pause and at a segment). The seg_* texts depend on exact segment counts: one per file.
   The last three (M4.3b) reach the stress and phrase passes' rare paths: a word of five or more syllables;
   the linking R before a vowel; and a stressed syllabic consonant at the nucleus, with the accent still set
   after a stress toggle, and a linking R after a vowel that counted no syllable. */
```

- Replace the last line of `CORPUS_NAMES`, `    "seg_wrap_word"`, with:

```c
    "seg_wrap_word", \
    "long_words", "linking_r", "syllabic_stress"
```

(The result: the `phrase_pauses ... seg_wrap_phrase` row and the `seg_wrap_word` row of M4.3a stay as they are apart from the added `, \`; one new row for M4.3b.)

- [ ] **Step 3: Run the golden suites to see them fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL, with `2 suite(s), 78 failure(s), 0 skip(s)`:
- `suite_golden` prints a `missing tests/golden/<name>.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` line for each of `long_words`, `linking_r`, `syllabic_stress` (3 failures);
- `suite_golden_hybrid` prints `42 corpus files x 25 modes (all, then each routine alone)` and fails `CHECK failed: want != NULL` 3 x 25 = 75 times.

- [ ] **Step 4: Generate the goldens with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: `2 suite(s), 0 failure(s), 0 skip(s)`, with these three lines among the `updated` lines (the frame counts are what the proof run produced; they are the rom engine's, so any difference means a different text):

```
  updated tests/golden/long_words.frm (431 frames)
  updated tests/golden/linking_r.frm (350 frames)
  updated tests/golden/syllabic_stress.frm (341 frames)
```

Then run `git status --short tests/golden`. Expected: exactly these three lines:

```
?? tests/golden/linking_r.frm
?? tests/golden/long_words.frm
?? tests/golden/syllabic_stress.frm
```

If any existing `.frm` shows as modified, STOP and report: the oracle is not deterministic, and nothing may be committed. If a frame count differs from the list above, STOP and check the text of that file against Step 1 byte for byte.

- [ ] **Step 5: Confirm the coverage**

Run (the coverage file goes under `build/`, which is git-ignored):

```bash
./build/apollo2_coverage.exe -o build/m43b.cov
for a in CBDB D0B5 D0CD; do grep -q "^x rom $a$" build/m43b.cov && echo "$a ok" || echo "$a MISSING"; done
```

Expected: the summary line `rom 4054, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges` (it was `rom 4040` before this task), then three `ok` lines. If any address is MISSING, STOP and report.

What each address stands for:

| Address | Text | Meaning |
|---|---|---|
| `CBDB` | `linking_r` (also once in `long_words` and in `syllabic_stress`) | `pros_mark_stress`: `BBS7 A,$CBDB` at $CBD6 taken, a `\|` before a vowel becomes `R` |
| `D0B5` | `long_words` | `pros_mark_phrases`: `BCC $D0BE` at $D0B3 falls, the word has 5 or more syllables |
| `D0CD` | `syllabic_stress` | `pros_mark_phrases`: `BBS7 A,$D0D1` at $D0CB falls, a stressed syllabic consonant at the nucleus |

The coverage file has no branch directions, so it cannot show the fourth target of spec §4.1, `BNE $CC60` at $CC59 **taken** (the accent state still set after a stress toggle), nor `BEQ $CCCA` at $CCBA **taken** (a vowel before the linking R counted no syllable), both in `syllabic_stress`: $CC60 and $CCCA are also reached by other paths. Both were confirmed with the branch probe while this plan was proved (table at the top of the task: `BR CC59 taken 2 fall 0` and `BR CCBA taken 2 fall 0` for `syllabic_stress` alone); `diff_lts` and `golden_hybrid` compare the C on these very texts from Task 3 on.

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected:
- All 31 suites are ok (`31 suite(s), 0 failure(s), 0 skip(s)`).
- `golden_hybrid` prints `42 corpus files x 25 modes (all, then each routine alone)`.
- `firmware_data` prints `38197 converted bytes compared with the ROM, 0 differ`.
- `diff_lts` shows 0 mismatches, 24 `N calls checked` lines (every N greater than 0; the counts grow with the new texts: the proof run printed `cmd_apply_speaker 95`, `cmd_run_due 4950`, `tn_convert_clause 206`, `pros_phrase_gen 207`, `pros_append_segment 2249`, `ph_lengthen_count 1101`), `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, both unchanged.
- `suite_synth` includes the new goldens in its corpus peak; the proof run printed `corpus peak plain 20522 (-4.1 dBFS)` and `corpus peak b11 15784 (-6.3 dBFS)`. If it reports clipping, or a plain peak outside -12...-1 dBFS, STOP and report. Do not change any calibration.

- [ ] **Step 7: Commit**

```bash
git add tests/corpus/long_words.txt tests/corpus/linking_r.txt tests/corpus/syllabic_stress.txt tests/golden/long_words.frm tests/golden/linking_r.frm tests/golden/syllabic_stress.frm tests/corpus_list.h
git commit -m "Corpus: three texts for the stress and phrase passes' rare paths (a word of five syllables, the linking R, a stressed syllabic consonant with the accent state still set, a vowel before a linking R that counts no syllable), goldens from the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The passes' RAM names, `pros_flags.h`, and `pros_store_phrase_len` $D0FD and `pros_mark_phrases` $CEF6 in C, with their hooks (spec §2.2, §2.3, §2.6, §2.7, §4.4)

**Files:**
- Create: `src/firmware/pros_flags.h`, `src/firmware/pros_phrase.h`, `src/firmware/pros_phrase.c`
- Modify: `src/firmware/fw.h` (16 RAM names, `FW_PROS_STRESS_TOGGLE`'s comment), `src/firmware/pros_gen.c` (the flag bits from `pros_flags.h`), `src/firmware/pros_addr.h` (three addresses), `src/engine/thunks.c` (two thunks and rows), `tests/test_diff_lts.c` (`DIFF_MAX`), `tests/test_fw_pros.c` (the phrase-pass tests)

**Where the code goes.** The exit block $CEEC–$CEF5 sits before the entry and is reached only by the `BEQ` at $CF26, so it is a helper of `pros_mark_phrases`, not a routine. `pros_store_phrase_len` is a routine (two `JSR`s) and is public, because the C `pros_mark_phrases` calls it directly and it has its own hook. The rest of `src/firmware/pros_phrase.c` is `static` helpers, one per ROM block:

| Helper | ROM |
|---|---|
| `init_state` | $CEF6–$CF17, the pass's state |
| `par_ready` | the `isr_par_ready` call at $CF28, with the registers the ROM has at the `JSR` |
| `load_symbol` | $CF2B–$CF41, the position's flags, symbol, class and features |
| `word_end` | $CF6B–$CF8A, a space or punctuation |
| `phrase_params` | $CF9A–$D04A, the phrase type's five parameters |
| `phrase_end` | $CF97–$D05A, a punctuation: store, parameters, declination, final lengthening |
| `symbol_counts` (with `count_add`, `inc8`) | $D062–$D0BB, a phonetic symbol's counts |
| `accent_and_nucleus` | $D0BE–$D0F7, the accent and the nucleus mask |
| `exit_block` | $CEEC–$CEF5 |

The position loop itself ($CF1A–$CF68, the `{` and `~` tests, and the `BBC7` at $D060) is `pros_mark_phrases`.

**Interfaces:**
- Consumes:
  - Task 1's corpus (41 names). Nothing in this task depends on it, but the expected counts below assume it is applied.
  - M4.3a: `pros_declination` and `pros_declination_y` (`pros_decl.h`); `pros_addr.h`; the `pros_phrase_gen` thunk, `THUNK` line and row in `thunks.c`; the one harness of `tests/test_fw_pros.c` (`g_ram`, `g_f`, `g_ncalls`, `setup()`, `check_call()`, `check_row()`; `test_gen_closure_and_hyper();` is the last line of `suite_fw_pros`).
  - M4.0–M4.2: `fw_rom_call`, `fw_rom8`, `fw_regs`, `FW_ROM_CALL_BUDGET` (`fw.h`); `ROM_ISR_PAR_READY` (`tn_convert.h`); `PH_SYMBOL_CLASS`, `PH_SYMBOL_FEATURES`, `PHC_*`, `PHF_*` (`data/fw_data.h`); the `fw_ram.h` names `PROS_POS`, `PROS_SYMBOL`, `PROS_CLASS`, `PROS_FEATURES`, `PROS_PREV_FEATURES`, `PROS_SYM_FLAG`, `PROS_WORD_POS`, `PROS_PHRASE_LEN`, `PROS_ACCENT_STATE`, `PROS_PARAMS`, `PROS_BASELINE`, `PROS_BASELINE_END`, `PROS_STRESS_PARAM`, `PROS_NUCLEUS_PARAM`, `PROS_FINAL_PARAM`, `PROS_SYM_FLAGS`, `PROS_SYM_SHORTEN`, `PROS_SYM_LENGTHEN`, `TN_PHONETIC`, `TN_PHON_LEN`.
- Produces (used by Tasks 3 and 4):

```c
/* fw.h, after FW_PROS_WORD_U, in address order */
#define FW_PROS_NEXT_SYM           0x1C06
#define FW_PROS_NEXT_FEATURES      0x1C07
#define FW_PROS_NEXT_PHON          0x1C08
#define FW_PROS_NEXT_PHON_FEATURES 0x1C09
#define FW_PROS_SYLLABLES          0x1C0B
#define FW_PROS_VOWEL_SEEN         0x1C0C
#define FW_PROS_TILDE_SHORTEN      0x1C11
#define FW_PROS_WRITE_ONLY_1C12    0x1C12
#define FW_PROS_FINAL              0x1C13
#define FW_PROS_BACKQUOTE          0x1C14
#define FW_PROS_PUNCT_NOT_KIND3    0x1C15   /* spec 2.6: "the previous punctuation's class bit 3 is clear" */
#define FW_PROS_FINAL_LENGTHEN     0x1C16
#define FW_PROS_ACCENT_PENDING     0x1C19
#define FW_PROS_PREV_FEATURES_COPY 0x1C1A
#define FW_PROS_WRITE_ONLY_1C1C    0x1C1C
#define FW_PROS_NUCLEUS_MASK       0x1C1E
/* pros_flags.h */
#define PSF_NUCLEUS     0x07
#define PSF_FUNC_WORD   0x08
#define PSF_UNRELEASED  0x10
#define PSF_ACCENT      0x20
#define PSF_STRESS      0x40
/* pros_addr.h */
#define ROM_PROS_MARK_PHRASES_EXIT  0xCEEC
#define ROM_PROS_MARK_PHRASES       0xCEF6
#define ROM_PROS_STORE_PHRASE_LEN   0xD0FD
/* pros_phrase.h */
#define PROS_PHRASE_LEN_MARK  0x80
uint8_t pros_store_phrase_len(fw *f);       /* returns A; the ROM leaves X = pros_pos, Y untouched */
void pros_mark_phrases(fw *f, fw_regs *r);  /* r in: entry registers (Y reaches $CF28); r out: A, X, Y at $CEF5 */
```

  - The replacements `"pros_mark_phrases"` `0xCEF6` and `"pros_store_phrase_len"` `0xD0FD`, both `FW_OUT_A | FW_OUT_X | FW_OUT_Y`: 26 replacements, 27 `golden_hybrid` modes.
  - `DIFF_MAX` 28 in `tests/test_diff_lts.c`, enough for Tasks 3 and 4 too.
  - In `tests/test_fw_pros.c`: `phrase_setup(text)` and `phrase_run(y, &r)` (Task 4 may reuse them), and `test_phrases_empty();` as the last line of `suite_fw_pros`.

**ROM facts checked** (`rom_CEF6_pros_mark_phrases.txt` with its exit block at the top, `rom_D0FD_pros_store_phrase_len.txt`, `rom_CABD_pros_mark_stress.txt` for the shared bytes, the callers `rom_CA92_pros_make_segments.txt`, `rom_D129_pros_phrase_gen.txt` ($D129–$D155), `rom_D3DD_pros_declination.txt`, `rom_D40D_pros_declination_step.txt`, and the ROM bytes of `ph_symbol_class` / `ph_symbol_features`):
- **`DIFF_MAX` must rise (the spec does not say so).** `tests/test_diff_lts.c` has `#define DIFF_MAX 24` and `CHECK(fw_replacement_count <= DIFF_MAX)`; with 26 rows it fails and returns before checking anything. This task raises it to 28, which also covers Tasks 3 and 4 (28 observers plus the `boot_loop_play` stop = 29 of `MACHINE_MAX_HOOKS` 32). Tasks 3 and 4 need not touch it.
- **The name of $1C15 is `FW_PROS_PUNCT_NOT_KIND3`** (not a name like "punctuation open"). The byte is 1 when the punctuation `pros_store_phrase_len` stored last (the next one in the text) had no class bit 3. Class bit 3 (`PHC_KIND_3`) is set for `.` `:` `;` `?` only: `,` is $05, `!` is $01, `-` `(` `"` etc. are $01. So "open" (a comma-like, continuing phrase) would be wrong for `!`. Task 3 uses the name for its clear at $CAE0. Several meanings in spec §2.6 are loose or wrong and are corrected in the `fw.h` comments (next items).
- **Every $1Cxx byte was traced in both pass dumps.** Exactly these 16 unnamed bytes are used, and only by `pros_mark_stress`, `pros_mark_phrases` and `pros_store_phrase_len` (a search of all dumps for each address finds no other routine; $1C0D is also `pros_phrase_gen`'s). $1C0A is unused.
  - `FW_PROS_SYLLABLES` ($1C0B) in `pros_mark_phrases` is the syllable count of the word **before** the space or punctuation in the text, the one the backward walk enters next ($CF6B reads what `pros_mark_stress` stored at that position). Spec §2.6 says "the following word's count".
  - `FW_PROS_FINAL` ($1C13): in `pros_mark_stress` it is the class bit 0 of the first non-consonant from `pros_pos + 2` on ($CDBA–$CDEA). A space has class 0, so it means "the vowel's syllable ends a **phrase**". In `pros_mark_phrases` it is set at **every** space and punctuation and cleared at a consonant followed (in the text) by a vowel or long symbol ($D06C–$D079), so it means "in the word's **last syllable**"; spec §2.6 says "next is punctuation / phrase-final": the first half is too loose for `pros_mark_stress` (a space does not count), the second wrong for `pros_mark_phrases`. A vowel outside it is shortened by 3 ($D097).
  - `FW_PROS_ACCENT_PENDING` ($1C19) means two things. In `pros_mark_stress` it lengthens each symbol by 4 ($CC7A–$CC8A): 1 at the start and at non-space word ends, 0 at `~`, and at a space it takes `$1C11` (so the word after a function word). In `pros_mark_phrases` it is the pending accent: 1 at the start and at each `~`, cleared when a stressed vowel gets $60.
  - `FW_PROS_NEXT_SYM` ($1C06) becomes $0D when the look-ahead reaches `tn_phon_len`, **even after it stored a space** ($CB9D writes both $1C06 and $1C08).
  - `FW_PROS_STRESS_TOGGLE` ($1C0D) is written by `pros_mark_phrases` (0 at the start and at each space or punctuation, 1 at `{`) but never read there.
  - `FW_PROS_PREV_FEATURES_COPY` ($1C1A) is written with `pros_prev_features` at $CD58 but, unlike it, cleared at each word end ($CEDE). The name is kept; the comment says so.
- **For Task 3 (seen while tracing the shared bytes): `pros_mark_stress`'s look-ahead overwrites `pros_class`** with the class of the next symbol it finds ($CB5D, $CB8B). So the `BBC1` on `pros_class` at $CEBB tests the **next** symbol's class (the current one's only when the scan reached the end).
- **`pros_store_phrase_len`** ($D0FD): `ORA #$80` is applied to A only, then stored at `pros_pos` in both count arrays. The decompiler's `pros_phrase_len = pros_phrase_len | 0x80` is wrong: RAM `pros_phrase_len` becomes 0. `$1C15` is written twice (0, then 1 when `BBS3` falls through). It returns A = `pros_class` when bit 3 is set (the `BBS3` branch skips `LDA #1`), else 1, with X = `pros_pos` (`LDX`) and Y untouched.
- **The count of a phrase starts from the next phrase's.** At a punctuation `pros_store_phrase_len` clears `pros_phrase_len`, but `pros_declination_step` (through `pros_declination` at $D04D) stores `pros_sym_lengthen[pros_pos] & $7F` (at least 1) back into it. So every phrase's count goes on from the count of the phrase after it. `test_phrases_clause` pins it (the first phrase's 2 symbols are stored as 5). Bit 7 of a stored count is never read: the only readers of $1E00/$1F00 are the passes, `pros_phrase_gen` (at phonetic symbols) and `pros_declination_step` (`AND #$7F`).
- **`pros_mark_phrases` never sets `pros_pos` or `$1C15`.** It starts from the `pros_pos` that `pros_mark_stress` leaves (`tn_phon_len`); with 0 the `DEC` wraps to 255 and the pass walks 255 positions of stale RAM. `$1C15` is cleared only by `pros_mark_stress` ($CAE0).
- **The registers at `isr_par_ready` ($CF28):** A = `pros_features` as loaded at $CF1A, which is the previously walked position's features (0 at the first, from $CF04); X = `pros_pos`. Y depends on the path into $CF1A:
  - the first position: the entry Y (what `pros_mark_stress` leaves);
  - after `{` ($CF50), a space ($CF94), a marker or other symbol with class bit 7 clear ($D060 → $D05A): Y = the symbol (`TAY` at $CF37);
  - after a punctuation ($D05A): Y = `pros_declination`'s Y (the step, or the step before its negation on a rising baseline), via `pros_declination_y`;
  - after a phonetic symbol ($D0FA): Y = its features (`LDY` at $D068).
- **`{` skips everything else** ($CF4B: `$1C0D = 1`, `JMP $CF1A`); `~` sets `$1C19` and falls through with A = 1 into `CMP #$20` (no space, class 0, so nothing more). A punctuation other than the six (`-` `(` `"` …) gets `word_end`, its count stored and the declination step with the current parameters, but no new parameter set and no nucleus-mask reset (`JMP $D04D` at $D001).
- **The accent** ($D0BE–$D0E1): with `$1C19` set, `pros_sym_flag` (the flags as loaded at $CF2E, before this position's changes) bit 6, and features bit 7 (`BBS7` $D0CB) or bit 1 (`BBS1` $D0CD); otherwise `BRA $D0CF`. Then $D0E4 ORs the nucleus mask into the **current** `pros_sym_flags[X]` and tests bit 6 of the result.
- **Every add is `CLC; ADC`** (8 bits): +8 at $D088 and $D0D5, +3, +7, +4 at $D09B, $D0A9, $D0B9. `INC pros_phrase_len` and `INC pros_word_pos` are 8-bit RAM increments.
- **What the callers read after each call** (and so the declared outputs; no caller reads a flag):
  - `pros_mark_phrases` from `pros_make_segments` ($CAB6): `JSR pros_phrase_gen` follows, whose first instruction is `JSR pros_declination` ($D129). That writes A (`LDA pros_baseline_end`), X (`LDX pros_pos` in `pros_declination_step`) and Y (`LDY #0`) before any read. So nothing reads them; the spec §2.3 says they "flow into `pros_phrase_gen`, whose first `isr_par_ready` ($D155) gets X and Y", but $D155 gets `pros_declination`'s X and Y. They are defined, so A, X, Y are declared.
  - `pros_store_phrase_len` from $CF97: `LDA pros_symbol`, the parameter loads (absolute, no X or Y), then `JSR pros_declination` at $D04D, which writes X and Y first. From $CEEC: `LDA pros_word_pos`, `STA $1D00` (absolute), `RTS`, and X and Y go on as `pros_mark_phrases`' outputs (no reader, above). The spec's "X = `pros_pos`, which `pros_declination` … see[s]" is loose: `pros_declination_step` loads X itself. Declared: A, X, Y.
- **Callers by scan.** `JSR $CEF6` only at $CAB6; `JSR $D0FD` only at $CEEC and $CF97; no `JMP` to any of $CEEC, $CEF6, $D0FD; the only relative branch to $CEEC is the `BEQ` at $CF26, and none reaches $CEF6 or $D0FD (Step 4's scan).
- **Proof run** (scratch worktree off main c04267e with Task 1 applied):
  - `diff_lts`: `pros_mark_phrases    207 calls checked` and `pros_store_phrase_len 374 calls checked`, 0 mismatches; 26 lines; `pros_declination` and `pros_declination_step` 374 each (every `pros_store_phrase_len` call is followed by a `pros_declination` call: at a punctuation directly, at the exit block through `pros_phrase_gen`'s first `JSR`).
  - `golden_hybrid`: `42 corpus files x 27 modes (all, then each routine alone)`, all identical.
  - The full run: `31 suite(s), 0 failure(s), 0 skip(s)`.
  - **A ROM oracle for the unit tests** (scratch only, not part of the plan): every scenario of the tests below was also run through the ROM routine $CEF6 in the emulator (interrupts held, `isr_par_ready` hooked to record its registers and return). The RAM $0200–$3FFF, every `isr_par_ready` call's A, X, Y and the exit A, X, Y were identical to the C, including the unreachable `BRA $D0CF` and the empty-text walk of 255 positions.
  - Mutation run: the unit tests fail for each of: no `BRA $D0CF` (accent without features bit 7 or 1), no bit 7 (`ORA #$80`), Y = the symbol instead of the features after a phonetic symbol, Y = `pros_decl_step` after the negation, `!` loading the `?` set, another punctuation resetting the nucleus mask, no `$1C1C` store at $CF0F, no +3 at $D09B.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_pros.c`:
- Replace

```c
#include "firmware/pros_gen.h"
```

with

```c
#include "firmware/pros_flags.h"
#include "firmware/pros_gen.h"
#include "firmware/pros_phrase.h"
```

- Replace

```c
void suite_fw_pros(void)
{
```

with

```c
/* ---- the phrase pass (M4.3b) ---- */

/* tn_phonetic = text, and pros_pos = tn_phon_len = its length, as pros_mark_stress leaves them; every flag, count,
   parameter and state byte 0 unless the test sets it. */
static void phrase_setup(const char *text)
{
    setup();
    size_t n = strlen(text);
    memcpy(g_ram + TN_PHONETIC, text, n);
    g_ram[TN_PHON_LEN] = (uint8_t)n;
    g_ram[PROS_POS] = (uint8_t)n;
}

/* Runs the phrase pass with the entry Y = y (A and X are not read); *r = the registers it leaves. */
static void phrase_run(uint8_t y, fw_regs *r)
{
    r->a = 0xA5;
    r->x = 0xB6;
    r->y = y;
    r->p = 0;
    pros_mark_phrases(&g_f, r);
}

static void test_phrase_rows(void)
{
    check_row("pros_mark_phrases", ROM_PROS_MARK_PHRASES, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_store_phrase_len", ROM_PROS_STORE_PHRASE_LEN, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* $D0FD: bit 7 unless the punctuation stored before had no class bit 3; the ORA is in A only */
static void test_store_phrase_len(void)
{
    setup();
    g_ram[PROS_POS] = 7;
    g_ram[PROS_PHRASE_LEN] = 5;
    g_ram[PROS_CLASS] = 0x0B;                           /* '.': class bit 3 */
    CHECK_EQ(pros_store_phrase_len(&g_f), 0x0B);        /* BBS3 taken: A = pros_class */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 7], 0x85);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 7], 0x85);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 0);

    g_ram[PROS_POS] = 3;
    g_ram[PROS_PHRASE_LEN] = 4;
    g_ram[PROS_CLASS] = 0x05;                           /* ',': no class bit 3 */
    CHECK_EQ(pros_store_phrase_len(&g_f), 1);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 3], 0x84);       /* this store still gets bit 7 ... */
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 1);

    g_ram[PROS_POS] = 1;
    g_ram[PROS_PHRASE_LEN] = 6;
    g_ram[PROS_CLASS] = 0x0D;                           /* '?' */
    CHECK_EQ(pros_store_phrase_len(&g_f), 0x0D);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 0x06);       /* ... the next one does not */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 0x06);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 2], 0);          /* only pros_pos is written */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], 0);
    CHECK_EQ(g_ram[PROS_POS], 1);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* " ME, ~TO{." walked from position 9 down to 1, then the exit block at 0: the registers at isr_par_ready ($CF28) on
   each path, the counts, the accent, the nucleus marks and the state left */
static void test_phrases_clause(void)
{
    fw_regs r;
    phrase_setup(" ME, ~TO{.");
    g_ram[PROS_SYM_FLAGS + 2] = PSF_STRESS;            /* E and O stressed (by pros_mark_stress) */
    g_ram[PROS_SYM_FLAGS + 7] = PSF_STRESS;
    g_ram[PROS_SYM_LENGTHEN + 3] = 5;                   /* the syllable counts pros_mark_stress stored: 5 for "ME" */
    g_ram[PROS_SYM_LENGTHEN + 9] = 4;                   /* (a long word from 5 on), 4 for "TO" */
    for (unsigned i = 0; i < 10; i++)
        g_ram[PROS_PARAMS + i] = (uint8_t)(0x10 + i);
    g_ram[PROS_PARAMS + 0] = 0x90;                      /* A, B: '.' falls by 16 over its count of 1 */
    g_ram[PROS_PARAMS + 1] = 0x80;
    g_ram[PROS_PARAMS + 5] = 0x70;                      /* F, G: ',' rises by 12 over its count of 3 */
    g_ram[PROS_PARAMS + 6] = 0x7C;
    g_ram[FW_PROS_WRITE_ONLY_1C1C] = 0x5A;              /* stale state, set at entry */
    g_ram[FW_PROS_NUCLEUS_MASK] = 0x5A;
    g_ram[FW_PROS_ACCENT_PENDING] = 0x5A;
    g_ram[FW_PROS_FINAL] = 0x5A;
    g_ram[PROS_ACCENT_STATE] = 0x5A;
    g_ram[PROS_FEATURES] = 0x5A;
    phrase_run(0x33, &r);
    CHECK_EQ(g_ncalls, 9);
    check_call(0, ROM_ISR_PAR_READY, 0, 9, 0x33);       /* the first: A = pros_features (0 at entry), the entry Y */
    check_call(1, ROM_ISR_PAR_READY, 0, 8, 16);         /* after '.': Y = pros_declination's (falling: the step) */
    check_call(2, ROM_ISR_PAR_READY, 0, 7, '{');        /* after '{': Y = the symbol */
    check_call(3, ROM_ISR_PAR_READY, 0x83, 6, 0x83);    /* after O: Y = its features (LDY at $D068) */
    check_call(4, ROM_ISR_PAR_READY, 0x60, 5, 0x60);    /* after T */
    check_call(5, ROM_ISR_PAR_READY, 0, 4, '~');        /* after '~', a marker: Y = the symbol */
    check_call(6, ROM_ISR_PAR_READY, 0, 3, ' ');        /* after the space */
    check_call(7, ROM_ISR_PAR_READY, 0, 2, 4);          /* after ',': rising, Y = the step before its negation */
    check_call(8, ROM_ISR_PAR_READY, 0x83, 1, 0x83);    /* after E */
    CHECK_EQ(r.a, 2);                                   /* the exit block: A = pros_word_pos, X = pros_pos */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x49);                                /* M's features */
    /* pros_sym_flags: each word's phonetic-symbol count at the space or punctuation before it, the accents with
       the nucleus marks */
    static const uint8_t flags[10] = { 2, 0, 0x67, 0, 2, 0, 0, 0x67, 0, 0 };
    /* the counts: each phrase's at the punctuation before it and at 0 (bit 7: the punctuation after the phrase has
       class bit 3; the first phrase's count goes on from the second's 3, which pros_declination_step stores back),
       the accents (+8), the phrase-final lengthening before '.' (+8), a vowel outside it (+7) and a long word (+4) */
    static const uint8_t lengthen[10] = { 5, 0, 8, 0x83, 0, 0, 0, 16, 0, 0x81 };
    static const uint8_t shorten[10] = { 5, 4, 11, 0x83, 0, 0, 0, 0, 0, 0x81 };
    for (unsigned i = 0; i < 10; i++) {
        CHECK_EQ(g_ram[PROS_SYM_FLAGS + i], flags[i]);
        CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + i], lengthen[i]);
        CHECK_EQ(g_ram[PROS_SYM_SHORTEN + i], shorten[i]);
    }
    CHECK_EQ(g_ram[PROS_STRESS_PARAM], 0x17);           /* the ',' set (F-J), the last loaded */
    CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 0x18);
    CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0x19);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x70);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x7C);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xFC);              /* 0 - 4 */
    CHECK_EQ(g_ram[PROS_POS], 0);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0);
    CHECK_EQ(g_ram[PROS_WORD_POS], 2);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 1);
    CHECK_EQ(g_ram[PROS_SYMBOL], 'M');
    CHECK_EQ(g_ram[PROS_CLASS], 0x80);
    CHECK_EQ(g_ram[PROS_FEATURES], 0x49);
    CHECK_EQ(g_ram[PROS_PREV_FEATURES], 0x49);
    CHECK_EQ(g_ram[PROS_SYM_FLAG], 0);
    CHECK_EQ(g_ram[FW_PROS_SYLLABLES], 5);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);          /* 1 at '{', 0 at the space */
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);                  /* 0 at M, a consonant before a vowel */
    CHECK_EQ(g_ram[FW_PROS_FINAL_LENGTHEN], 0);
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 0);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C1C], 1);
    CHECK_EQ(g_ram[FW_PROS_NUCLEUS_MASK], 0);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 1);        /* the exit block's store: pros_class $80 */
    CHECK(!g_f.fault);
}

/* The five pros_params of each punctuation's phrase type ($CF9A-$D04A); other punctuation loads none */
static void test_phrases_params(void)
{
    static const struct { char p; uint8_t first; } k[] = {
        { '?', 10 }, { '!', 15 }, { ',', 5 }, { ':', 5 }, { '.', 0 }, { ';', 0 },
    };
    fw_regs r;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        char text[3] = { ' ', k[i].p, '\0' };
        phrase_setup(text);
        for (unsigned j = 0; j < 20; j++)
            g_ram[PROS_PARAMS + j] = (uint8_t)(0xC0 - j);   /* every set falls by 1 */
        phrase_run(0, &r);
        uint8_t s = k[i].first;
        CHECK_EQ(g_ram[PROS_BASELINE], 0xC0 - s);
        CHECK_EQ(g_ram[PROS_BASELINE_END], 0xC0 - s - 1);
        CHECK_EQ(g_ram[PROS_STRESS_PARAM], 0xC0 - s - 2);
        CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 0xC0 - s - 3);
        CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0xC0 - s - 4);
        CHECK_EQ(g_ram[PROS_DECL_STEP], 1);             /* 1 over the count 1 */
        CHECK_EQ(g_ncalls, 1);
        check_call(0, ROM_ISR_PAR_READY, 0, 1, 0);
        CHECK(!g_f.fault);
    }
    phrase_setup(" -");                                 /* '-' (class 1) keeps the current set */
    g_ram[PROS_BASELINE] = 0x66;
    g_ram[PROS_BASELINE_END] = 0x44;
    g_ram[PROS_STRESS_PARAM] = 0xE1;
    g_ram[PROS_NUCLEUS_PARAM] = 0xE2;
    g_ram[PROS_FINAL_PARAM] = 0xE3;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x66);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x44);
    CHECK_EQ(g_ram[PROS_STRESS_PARAM], 0xE1);
    CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 0xE2);
    CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0xE3);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 25);                /* still computed: $22 over 1, capped */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 0x81);       /* still stored; '-' has no class bit 3 ... */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 0x01);       /* ... so the count before it gets no bit 7 */

    /* ... and keeps the nucleus mask: the E before '-' gets no nucleus marks, the one before '.' does */
    phrase_setup(" E-E");
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    g_ram[PROS_SYM_FLAGS + 3] = PSF_STRESS;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 3], 0x67);          /* the accent and the nucleus marks */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], PSF_STRESS);    /* the mask is still 0 */
    phrase_setup(" E.E");
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    g_ram[PROS_SYM_FLAGS + 3] = PSF_STRESS;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], PSF_STRESS | PSF_NUCLEUS);
    CHECK(!g_f.fault);
}

/* $D0C8-$D0CF: a pending accent goes to a stressed vowel ($D0CB BBS7) or long symbol ($D0CD BBS1). BRA $D0CF (a
   stressed symbol with neither: no accent) is unreachable from pros_mark_stress, which sets the stress flag only
   with features bit 7 or 1. */
static void test_phrases_accent(void)
{
    fw_regs r;
    phrase_setup(" AME");
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    g_ram[PROS_SYM_FLAGS + 2] = PSF_STRESS;             /* M (features $49) stressed: BRA $D0CF */
    g_ram[PROS_SYM_LENGTHEN + 1] = 0xFA;                /* the accent's +8 wraps mod 256 */
    g_ram[PROS_SYM_LENGTHEN + 2] = 0x30;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 3], PSF_NUCLEUS);   /* E, unstressed after the last stress: nucleus marks */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 2], PSF_STRESS | PSF_NUCLEUS);   /* no accent, but it ends the nucleus marks */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 2], 0x30);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], 0x60);          /* the accent still pending reaches A; the mask is 0 */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 0x02);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 3], 7);           /* E in the word's last syllable */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 7 + 3);       /* A outside it (M, a consonant before a vowel, ends it) */
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 3);              /* A's word position */
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);
    CHECK_EQ(g_ncalls, 3);
    check_call(1, ROM_ISR_PAR_READY, 0x83, 2, 0x83);
    check_call(2, ROM_ISR_PAR_READY, 0x49, 1, 0x49);

    phrase_setup(" 7");                                 /* a syllabic consonant (features $4B: bit 1, not bit 7) */
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
    phrase_run(0, &r);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], 0x67);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 8);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 7);           /* long: shortened as a vowel */
    CHECK(!g_f.fault);
}

/* pros_pos 1 on entry: DEC to 0 and straight to the exit block: no isr_par_ready, the entry Y kept, and the state
   as the entry ($CEF6-$CF17) sets it */
static void test_phrases_exit_only(void)
{
    static const uint16_t state[] = {
        FW_PROS_FINAL_LENGTHEN, PROS_ACCENT_STATE, FW_PROS_STRESS_TOGGLE, PROS_WORD_POS, PROS_FEATURES,
        FW_PROS_ACCENT_PENDING, PROS_PHRASE_LEN, FW_PROS_WRITE_ONLY_1C1C, FW_PROS_FINAL, FW_PROS_NUCLEUS_MASK,
    };
    fw_regs r;
    phrase_setup(" ");
    for (size_t i = 0; i < sizeof state / sizeof state[0]; i++)
        g_ram[state[i]] = 0x5A;                         /* stale */
    g_ram[PROS_SYM_FLAGS + 0] = 0x5A;
    phrase_run(0x77, &r);
    CHECK_EQ(g_ram[FW_PROS_FINAL_LENGTHEN], 0);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);
    CHECK_EQ(g_ram[PROS_FEATURES], 0);
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 1);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C1C], 1);        /* the dead store at $CF0F */
    CHECK_EQ(g_ram[FW_PROS_FINAL], 1);
    CHECK_EQ(g_ram[FW_PROS_NUCLEUS_MASK], PSF_NUCLEUS);
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x77);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], 0);             /* pros_word_pos */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 0x81);       /* pros_phrase_len 1 from the entry, with bit 7 */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 0], 0x81);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0);
    CHECK_EQ(g_ram[FW_PROS_PUNCT_NOT_KIND3], 1);        /* pros_class 0: no bit 3 */
    CHECK_EQ(g_ram[PROS_PREV_FEATURES], 0);
    CHECK(!g_f.fault);
}

/* Empty tn_phonetic (latent): pros_mark_stress leaves pros_pos 0, so the pass walks positions 255 down to 1 of stale
   RAM. Here '.' at 255, a stressed E at 254, M at 1, and symbol 0 (class 0, a marker) between. */
static void test_phrases_empty(void)
{
    fw_regs r;
    setup();
    g_ram[TN_PHONETIC + 255] = '.';
    g_ram[TN_PHONETIC + 254] = 'E';
    g_ram[TN_PHONETIC + 1] = 'M';
    g_ram[PROS_SYM_FLAGS + 254] = PSF_STRESS;
    phrase_run(0x5A, &r);
    CHECK_EQ(g_ncalls, 255);
    check_call(0, ROM_ISR_PAR_READY, 0, 255, 0x5A);
    check_call(1, ROM_ISR_PAR_READY, 0, 254, 0);        /* after '.': a level baseline (0 to 0), step 0 */
    check_call(2, ROM_ISR_PAR_READY, 0x83, 253, 0x83);
    check_call(3, ROM_ISR_PAR_READY, 0, 252, 0);        /* after symbol 0 */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 254], 0x67);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 254], 16);       /* the accent, and the lengthening before '.' */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 8);          /* still before '.': symbol 0 has no features */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 255], 0x81);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 0x83);       /* 1 (stored back by the step) + E + M, bit 7 ('.') */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], 2);
    CHECK_EQ(g_ram[PROS_POS], 0);
    CHECK_EQ(r.a, 2);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x49);
    CHECK(!g_f.fault);
}

void suite_fw_pros(void)
{
```

- Replace

```c
    test_gen_closure_and_hyper();
}
```

with

```c
    test_gen_closure_and_hyper();
    test_phrase_rows();
    test_store_phrase_len();
    test_phrases_clause();
    test_phrases_params();
    test_phrases_accent();
    test_phrases_exit_only();
    test_phrases_empty();
}
```

What the tests pin (spec §4.4, the part for this task):
- `test_store_phrase_len`: bit 7 by the previous punctuation's class bit 3, the returned A, the `ORA` in A only.
- `test_phrases_clause`: `isr_par_ready`'s registers on every path (first, after `.` falling, `{`, a vowel, a consonant, `~`, a space, `,` rising, and the exit), the counts (+3 is in `test_phrases_accent`, +4 from 5 syllables, +7, +8 twice), the accent, the nucleus marks, the count carried over from the next phrase, the dead store `$1C1C` and every state byte left.
- `test_phrases_params`: the five parameters per punctuation (`?` K–O, `!` P–T, `,` `:` F–J, `.` `;` A–E), others none and no nucleus-mask reset.
- `test_phrases_accent`: `BRA $D0CF` (unreachable), `BBS1` $D0CD (a syllabic consonant), the wrap of a count mod 256, the +3 outside a word's last syllable.
- `test_phrases_exit_only`: the exit block at position 0 straight from the entry, and the entry state ($CEF6–$CF17, the dead store at $CF0F).
- `test_phrases_empty`: empty `tn_phonetic` (255 stale positions).

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_pros"`
Expected: a compile error, because `firmware/pros_flags.h` does not exist.

- [ ] **Step 3: Implement**

`src/firmware/fw.h`:
- Replace

```c
#define FW_PROS_STRESS_TOGGLE  0x1C0D   /* unnamed in the annotations: pros_phrase_gen's "a stress or an accent seen
                                           in this word" (0 at each word start) */
#define FW_PROS_WORD_U         0x1C1F   /* unnamed in the annotations: the U term (pros_params[20]) that the first
                                           symbol of a word adds to pros_term_stress; 0 after it */
```

with

```c
#define FW_PROS_STRESS_TOGGLE  0x1C0D   /* unnamed in the annotations: pros_phrase_gen's "a stress or an accent seen
                                           in this word" (0 at each word start). pros_mark_stress: the syllable's
                                           stress, $FF at its start, at each word end and at '}', 0 at '~', '{' and
                                           'x', inverted ($CC45) at the onset of each later syllable of a word.
                                           pros_mark_phrases writes it (0 at its start and at each space or
                                           punctuation, 1 at '{') and never reads it */
#define FW_PROS_WORD_U         0x1C1F   /* unnamed in the annotations: the U term (pros_params[20]) that the first
                                           symbol of a word adds to pros_term_stress; 0 after it */
/* The state bytes of the stress and phrase passes (pros_mark_stress $CABD, pros_mark_phrases $CEF6 and
   pros_store_phrase_len $D0FD), unnamed in the annotations; no other routine uses them (M4.3b spec 2.6). */
#define FW_PROS_NEXT_SYM           0x1C06   /* pros_mark_stress: the next symbol after pros_pos that has a class
                                               (class 0, the markers, skipped), or a space if one comes first; $0D
                                               when the scan reaches tn_phon_len (after a space too) */
#define FW_PROS_NEXT_FEATURES      0x1C07   /* pros_mark_stress: the features of FW_PROS_NEXT_SYM (0 for a space and at
                                               the end) */
#define FW_PROS_NEXT_PHON          0x1C08   /* pros_mark_stress: the next symbol that has a class, past a space; $0D at
                                               the end. The vowel path replaces it with tn_phonetic[pros_pos + 2]
                                               when it is '1' or 'L' ($CDBF) */
#define FW_PROS_NEXT_PHON_FEATURES 0x1C09   /* pros_mark_stress: the features of FW_PROS_NEXT_PHON (0 at the end) */
#define FW_PROS_SYLLABLES          0x1C0B   /* pros_mark_stress: the syllables of the word so far (a vowel counts when
                                               a consonant or no features follow, $CD65), stored at the word's end
                                               in pros_sym_lengthen and pros_sym_shorten, then 0. pros_mark_phrases:
                                               that count, read back at each space and punctuation ($CF6B), of the
                                               word it walks next (the one before it in the text); 5 or more
                                               shortens each of its phonetic symbols by 4 ($D0B5) */
#define FW_PROS_VOWEL_SEEN         0x1C0C   /* pros_mark_stress: a vowel seen in this word (1 on the vowel path, 0 at
                                               each word end) */
#define FW_PROS_TILDE_SHORTEN      0x1C11   /* pros_mark_stress: 1 at its start and at '~', 0 at each word end and
                                               while FW_PROS_ACCENT_PENDING is set ($CC81); while set, each symbol's
                                               shortening count gets 3 ($CC92). A space copies it into
                                               FW_PROS_ACCENT_PENDING ($CEA2) */
#define FW_PROS_WRITE_ONLY_1C12    0x1C12   /* pros_mark_stress, on the vowel path: 1 when FW_PROS_NEXT_PHON is '1' or
                                               'L' ($CDCB), else 0 ($CDD2); never read */
#define FW_PROS_FINAL              0x1C13   /* pros_mark_stress: the vowel's syllable ends a phrase (the class bit 0,
                                               punctuation, of the first non-consonant from pros_pos + 2 on, $CDEA).
                                               pros_mark_phrases: walking back through a word's last syllable (1 at
                                               its start and at each space and punctuation, 0 at the consonant
                                               before a vowel, $D079); a vowel outside it is shortened by 3 */
#define FW_PROS_BACKQUOTE          0x1C14   /* pros_mark_stress: a backquote seen ($CC12), so the next '?' becomes '.'
                                               ($CEB3); 0 at its start, at that '?' and at $CEC2 */
#define FW_PROS_PUNCT_NOT_KIND3    0x1C15   /* pros_store_phrase_len: the punctuation it stored at last (the next one
                                               in the text) had no class bit 3 (PHC_KIND_3 is set for . : ; ?), so
                                               this store gets no bit 7. Cleared by pros_mark_stress ($CAE0) only */
#define FW_PROS_FINAL_LENGTHEN     0x1C16   /* pros_mark_phrases: the phrase-final syllable's lengthening: 8
                                               (PHC_KIND_3) after a punctuation with class bit 3 ($D057); 0 at its
                                               start, at each space and punctuation, and at the consonant before a
                                               vowel ($D07C). While set, each phonetic symbol's lengthening count
                                               gets 8 ($D084); while clear, a vowel's shortening gets 7 ($D0A5) */
#define FW_PROS_ACCENT_PENDING     0x1C19   /* pros_mark_stress: lengthen the word's symbols by 4 ($CC84): 1 at its
                                               start and at each word end but a space, 0 at '~', and at a space
                                               FW_PROS_TILDE_SHORTEN (1 after a function word). pros_mark_phrases: an
                                               accent still to place: 1 at its start and at each '~'; the next
                                               stressed vowel walked gets the accent $60 and clears it ($D0E1) */
#define FW_PROS_PREV_FEATURES_COPY 0x1C1A   /* pros_mark_stress: pros_prev_features as stored at $CD58, but 0 at its
                                               start and at each word end ($CEDE), where pros_prev_features is kept:
                                               the previous symbol's features within the word ($CD81) */
#define FW_PROS_WRITE_ONLY_1C1C    0x1C1C   /* pros_mark_phrases: 1 at its start ($CF0F) and at each space and
                                               punctuation ($CF79); never read */
#define FW_PROS_NUCLEUS_MASK       0x1C1E   /* pros_mark_phrases: ORed into each phonetic symbol's pros_sym_flags
                                               ($D0E7): 7 at its start and at each '?' '!' ',' ':' '.' ';', 0 after
                                               the first symbol whose flags then have the stress bit $40 ($D0F7).
                                               pros_mark_stress clears it ($CAE3) */
```

Create `src/firmware/pros_flags.h`:

```c
#ifndef FW_PROS_FLAGS_H
#define FW_PROS_FLAGS_H
/* The bits of pros_sym_flags ($1D00, one byte per tn_phonetic position) that pros_mark_stress and pros_mark_phrases
   set and pros_phrase_gen reads (M4.3b spec 2.6). At a space or punctuation the byte is not bits but the number of
   phonetic symbols of the word after it (pros_mark_phrases $CF74; at position 0 its exit block, $CEF2). */
#define PSF_NUCLEUS     0x07   /* bits 0-2: 7 on each phonetic symbol from its phrase's last stressed symbol to the
                                  phrase's end (mark_phrases $D0E7 ORs in $1C1E) */
#define PSF_FUNC_WORD   0x08   /* the whole byte at a '~', the function-word marker (mark_stress $CBC7); no pass reads
                                  it (pros_phrase_gen tests the symbol) */
#define PSF_UNRELEASED  0x10   /* a stop whose next symbol is a stop (mark_stress $CCEB): only its first triple, the
                                  closure */
#define PSF_ACCENT      0x20   /* the accent, set with PSF_STRESS as $60 (mark_phrases $D0DA) */
#define PSF_STRESS      0x40   /* a vowel or long symbol in a stressed syllable (mark_stress $CC75); $60 = the accent
                                  (mark_phrases $D0DA) */

#endif
```

`src/firmware/pros_gen.c` (the flags move to `pros_flags.h`; a pure rename, `diff_lts` and `golden_hybrid` prove it):
- Replace

```c
#include "pros_decl.h"
#include "ph_expand.h"
#include "tn_convert.h"
#include "data/fw_data.h"

/* pros_sym_flags bits, set by pros_mark_stress and pros_mark_phrases */
#define FLAG_NUCLEUS    0x07   /* nucleus and tail marks */
#define FLAG_UNRELEASED 0x10   /* a stop before another stop: only its first triple, the closure */
#define FLAG_ACCENT     0x20
#define FLAG_STRESS     0x40
/* pros_params read here, by @R letter (index = letter - 'A') */
```

with

```c
#include "pros_decl.h"
#include "pros_flags.h"
#include "ph_expand.h"
#include "tn_convert.h"
#include "data/fw_data.h"

/* pros_params read here, by @R letter (index = letter - 'A') */
```

- Replace each of these lines (the comment column is kept):

```c
        if (flag & FLAG_ACCENT) {                       /* $D243 */
        } else if (flag & FLAG_STRESS) {                /* $D269 */
    if (flag & FLAG_NUCLEUS) {                          /* $D2A8 */
        if (flag & FLAG_STRESS) {
        if (r->a & FLAG_UNRELEASED)
```

  with, in order,

```c
        if (flag & PSF_ACCENT) {                        /* $D243 */
        } else if (flag & PSF_STRESS) {                 /* $D269 */
    if (flag & PSF_NUCLEUS) {                           /* $D2A8 */
        if (flag & PSF_STRESS) {
        if (r->a & PSF_UNRELEASED)
```

  Afterwards `grep -n "FLAG_" src/firmware/pros_gen.c` prints nothing.

`src/firmware/pros_addr.h`:
- Replace

```c
/* ROM code addresses of the segment generator (M4.3a spec 2.2), checked against build/ghidra740/dump and the
   JSR/JMP operands in the ROM image. isr_par_ready, the one ROM routine they call, is ROM_ISR_PAR_READY in
   tn_convert.h. */
```

with

```c
/* ROM code addresses of the segment generator (M4.3a spec 2.2) and of the stress and phrase passes before it
   (M4.3b spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image.
   isr_par_ready, the one ROM routine they call, is ROM_ISR_PAR_READY in tn_convert.h. */
```

- Replace

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
```

with

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
#define ROM_PROS_MARK_PHRASES_EXIT  0xCEEC   /* pros_mark_phrases' exit block, before its entry (BEQ from $CF26) */
#define ROM_PROS_MARK_PHRASES       0xCEF6   /* called by pros_make_segments $CAB6 */
#define ROM_PROS_STORE_PHRASE_LEN   0xD0FD   /* called by pros_mark_phrases $CF97 and $CEEC */
```

(The list stays in address order; Task 3 inserts `ROM_PROS_MARK_STRESS` before `ROM_PROS_MARK_PHRASES_EXIT`.)

Create `src/firmware/pros_phrase.h`:

```c
#ifndef FW_PROS_PHRASE_H
#define FW_PROS_PHRASE_H
/* The phrase pass of the prosody (ROM $CEEC-$D128: pros_mark_phrases with its exit block, and pros_store_phrase_len),
   decompiled (M4.3b spec 2.2). */
#include "fw.h"

#define PROS_PHRASE_LEN_MARK  0x80   /* ORA #$80: bit 7 of a stored phrase count (pros_declination_step masks it) */

/* $D0FD: pros_phrase_len into pros_sym_lengthen and pros_sym_shorten at pros_pos, with bit 7 unless
   FW_PROS_PUNCT_NOT_KIND3 is set (the punctuation stored before, later in the text, had no class bit 3); then
   pros_phrase_len = 0, and FW_PROS_PUNCT_NOT_KIND3 = 1 when pros_class has no bit 3 (PHC_KIND_3), else 0.
   Returns the A it leaves: 1, or pros_class when its bit 3 is set. The ROM leaves X = pros_pos, Y untouched. */
uint8_t pros_store_phrase_len(fw *f);
/* $CEF6: the backward pass. pros_pos counts down from its value on entry (tn_phon_len, as pros_mark_stress leaves it;
   0 wraps to 255) to 1; per position: isr_par_ready, then a phrase's phonetic symbols counted, each punctuation's
   count stored (pros_store_phrase_len) with its phrase type's five pros_params and the declination step
   (pros_declination), the accent ($60), the nucleus marks (PSF_NUCLEUS) and the context counts. At 0 the exit block
   $CEEC stores the first phrase's count and pros_sym_flags[0] = pros_word_pos.
   r in: the registers at entry; only Y is read, by the first isr_par_ready ($CF28). r out: the A, X, Y the ROM
   leaves at its RTS ($CEF5): A = pros_word_pos, X = pros_pos (0), Y as the last position left it. */
void pros_mark_phrases(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/pros_phrase.c`:

```c
#include "pros_phrase.h"
#include "pros_decl.h"
#include "pros_flags.h"
#include "tn_convert.h"
#include "data/fw_data.h"

/* The five pros_params of each phrase type start at its @R letter (index = letter - 'A'): baseline, baseline end,
   stress, nucleus, final */
#define PROS_PARAM_SET_A  (PROS_PARAMS + 0)    /* '.' ';' */
#define PROS_PARAM_SET_F  (PROS_PARAMS + 5)    /* ',' ':' */
#define PROS_PARAM_SET_K  (PROS_PARAMS + 10)   /* '?' */
#define PROS_PARAM_SET_P  (PROS_PARAMS + 15)   /* '!' */
#define PROS_LONG_WORD    5                    /* CMP #5: a word of 5 syllables or more is shortened */

/* isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static void par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }

/* LDA arr,X; CLC; ADC #n; STA arr,X: an add to a position's count, mod 256. */
static void count_add(fw *f, uint16_t arr, uint8_t x, uint8_t n)
{
    fw_wr8(f, (uint16_t)(arr + x), (uint8_t)(fw_rd8(f, (uint16_t)(arr + x)) + n));
}

static void inc8(fw *f, uint16_t addr) { fw_wr8(f, addr, (uint8_t)(fw_rd8(f, addr) + 1)); }

/* $CEF6-$CF17: the pass's state, in the ROM's order. */
static void init_state(fw *f)
{
    fw_wr8(f, FW_PROS_FINAL_LENGTHEN, 0);
    fw_wr8(f, PROS_ACCENT_STATE, 0);
    fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0);
    fw_wr8(f, PROS_WORD_POS, 0);
    fw_wr8(f, PROS_FEATURES, 0);
    fw_wr8(f, FW_PROS_ACCENT_PENDING, 1);
    fw_wr8(f, PROS_PHRASE_LEN, 1);
    fw_wr8(f, FW_PROS_WRITE_ONLY_1C1C, 1);                   /* never read */
    fw_wr8(f, FW_PROS_FINAL, 1);
    fw_wr8(f, FW_PROS_NUCLEUS_MASK, PSF_NUCLEUS);
}

/* $CF2B-$CF41: the position's flags, symbol, class and features. Returns the symbol (TAY). */
static uint8_t load_symbol(fw *f, uint8_t x)
{
    fw_wr8(f, PROS_SYM_FLAG, fw_rd8(f, (uint16_t)(PROS_SYM_FLAGS + x)));
    uint8_t sym = fw_rd8(f, (uint16_t)(TN_PHONETIC + x));
    fw_wr8(f, PROS_SYMBOL, sym);
    fw_wr8(f, PROS_CLASS, fw_rom8(f, (uint16_t)(PH_SYMBOL_CLASS + sym)));
    fw_wr8(f, PROS_FEATURES, fw_rom8(f, (uint16_t)(PH_SYMBOL_FEATURES + sym)));
    return sym;
}

/* $CF6B-$CF8A: a space or punctuation ends the word walked (the one after it in the text): its flags get that
   word's phonetic-symbol count, and the syllable count pros_mark_stress stored here is the next word's. */
static void word_end(fw *f, uint8_t x)
{
    fw_wr8(f, FW_PROS_SYLLABLES, fw_rd8(f, (uint16_t)(PROS_SYM_LENGTHEN + x)));
    fw_wr8(f, (uint16_t)(PROS_SYM_FLAGS + x), fw_rd8(f, PROS_WORD_POS));
    fw_wr8(f, FW_PROS_WRITE_ONLY_1C1C, 1);                   /* never read */
    fw_wr8(f, FW_PROS_FINAL, 1);
    fw_wr8(f, FW_PROS_FINAL_LENGTHEN, 0);
    fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0);
    fw_wr8(f, PROS_PREV_FEATURES, 0);
    fw_wr8(f, PROS_WORD_POS, 0);
}

/* $CF9A-$D04A: the five parameters of the punctuation's phrase type, and the nucleus mask back to 7; any other
   punctuation keeps the current set and mask ($D001 JMP $D04D). */
static void phrase_params(fw *f, uint8_t sym)
{
    uint16_t set;
    switch (sym) {
    case '?':
        set = PROS_PARAM_SET_K;
        break;
    case '!':
        set = PROS_PARAM_SET_P;
        break;
    case '.': case ';':
        set = PROS_PARAM_SET_A;
        break;
    case ',': case ':':
        set = PROS_PARAM_SET_F;
        break;
    default:
        return;
    }
    fw_wr8(f, PROS_STRESS_PARAM, fw_rd8(f, (uint16_t)(set + 2)));
    fw_wr8(f, PROS_NUCLEUS_PARAM, fw_rd8(f, (uint16_t)(set + 3)));
    fw_wr8(f, PROS_FINAL_PARAM, fw_rd8(f, (uint16_t)(set + 4)));
    fw_wr8(f, PROS_BASELINE, fw_rd8(f, set));
    fw_wr8(f, PROS_BASELINE_END, fw_rd8(f, (uint16_t)(set + 1)));
    fw_wr8(f, FW_PROS_NUCLEUS_MASK, PSF_NUCLEUS);
}

/* $CF97-$D05A: a punctuation: the count of the phrase after it stored here, its phrase type's parameters, the
   declination step (from that count), and the phrase-final lengthening after one with class bit 3. r: X and Y as
   pros_declination leaves them. */
static void phrase_end(fw *f, fw_regs *r)
{
    pros_store_phrase_len(f);
    phrase_params(f, fw_rd8(f, PROS_SYMBOL));
    pros_declination(f);
    r->x = fw_rd8(f, PROS_POS);                              /* pros_declination_step's LDX pros_pos */
    r->y = pros_declination_y(f);
    uint8_t k = fw_rd8(f, PROS_CLASS) & PHC_KIND_3;          /* AND #8; BEQ */
    if (k)
        fw_wr8(f, FW_PROS_FINAL_LENGTHEN, k);
}

/* $D062-$D0BB: a phonetic symbol counts in its phrase and word; its last syllable, the phrase-final lengthening
   and the shortening of vowels outside them and of long words. */
static void symbol_counts(fw *f, uint8_t x)
{
    inc8(f, PROS_PHRASE_LEN);
    inc8(f, PROS_WORD_POS);
    uint8_t features = fw_rd8(f, PROS_FEATURES);             /* LDY pros_features; TYA */
    if ((features & PHF_CONSONANT) && (fw_rd8(f, PROS_PREV_FEATURES) & (PHF_VOWEL | PHF_LONG))) {
        fw_wr8(f, FW_PROS_FINAL, 0);                         /* a consonant before a vowel or long symbol */
        fw_wr8(f, FW_PROS_FINAL_LENGTHEN, 0);
    }
    if (fw_rd8(f, FW_PROS_FINAL_LENGTHEN))
        count_add(f, PROS_SYM_LENGTHEN, x, 8);
    if (features & (PHF_VOWEL | PHF_LONG)) {                 /* TYA; AND #$82 */
        if (fw_rd8(f, FW_PROS_FINAL) == 0)
            count_add(f, PROS_SYM_SHORTEN, x, 3);
        if (fw_rd8(f, FW_PROS_FINAL_LENGTHEN) == 0)
            count_add(f, PROS_SYM_SHORTEN, x, 7);
    }
    if (fw_rd8(f, FW_PROS_SYLLABLES) >= PROS_LONG_WORD)     /* CMP #5; BCC */
        count_add(f, PROS_SYM_SHORTEN, x, 4);
}

/* $D0BE-$D0F7: the accent on the first stressed vowel or long symbol walked while one is pending (BRA $D0CF: a
   stressed symbol without features bit 7 or 1 gets none), then the nucleus mask ORed in; a stressed symbol ends the
   nucleus marks of its phrase. */
static void accent_and_nucleus(fw *f, uint8_t x)
{
    if (fw_rd8(f, FW_PROS_ACCENT_PENDING) && (fw_rd8(f, PROS_SYM_FLAG) & PSF_STRESS)
        && (fw_rd8(f, PROS_FEATURES) & (PHF_VOWEL | PHF_LONG))) {   /* BBS7; BBS1; BRA */
        count_add(f, PROS_SYM_LENGTHEN, x, 8);
        fw_wr8(f, (uint16_t)(PROS_SYM_FLAGS + x), PSF_ACCENT | PSF_STRESS);
        fw_wr8(f, FW_PROS_ACCENT_PENDING, 0);
    }
    uint8_t flags = (uint8_t)(fw_rd8(f, (uint16_t)(PROS_SYM_FLAGS + x)) | fw_rd8(f, FW_PROS_NUCLEUS_MASK));
    fw_wr8(f, (uint16_t)(PROS_SYM_FLAGS + x), flags);
    if (flags & PSF_STRESS) {                                /* BBC6 A: the ORed value */
        fw_wr8(f, PROS_ACCENT_STATE, fw_rd8(f, PROS_WORD_POS));
        fw_wr8(f, FW_PROS_NUCLEUS_MASK, 0);
    }
}

/* $CEEC-$CEF5, reached by BEQ from $CF26 when pros_pos reaches 0: the first phrase's count, and pros_sym_flags[0] =
   the first word's phonetic-symbol count. r: A and X as the ROM leaves them at its RTS (Y untouched). */
static void exit_block(fw *f, fw_regs *r)
{
    pros_store_phrase_len(f);
    r->x = fw_rd8(f, PROS_POS);                              /* its LDX pros_pos */
    r->a = fw_rd8(f, PROS_WORD_POS);
    fw_wr8(f, PROS_SYM_FLAGS, r->a);
}

uint8_t pros_store_phrase_len(fw *f)
{
    uint8_t a = fw_rd8(f, PROS_PHRASE_LEN);
    if (fw_rd8(f, FW_PROS_PUNCT_NOT_KIND3) == 0)
        a |= PROS_PHRASE_LEN_MARK;                           /* ORA #$80, in A only */
    uint8_t x = fw_rd8(f, PROS_POS);
    fw_wr8(f, (uint16_t)(PROS_SYM_LENGTHEN + x), a);
    fw_wr8(f, (uint16_t)(PROS_SYM_SHORTEN + x), a);
    fw_wr8(f, PROS_PHRASE_LEN, 0);
    fw_wr8(f, FW_PROS_PUNCT_NOT_KIND3, 0);
    a = fw_rd8(f, PROS_CLASS);
    if (!(a & PHC_KIND_3)) {                                 /* BBS3 A */
        a = 1;
        fw_wr8(f, FW_PROS_PUNCT_NOT_KIND3, a);
    }
    return a;
}

void pros_mark_phrases(fw *f, fw_regs *r)
{
    init_state(f);
    for (;;) {
        r->a = fw_rd8(f, PROS_FEATURES);                     /* $CF1A */
        fw_wr8(f, PROS_PREV_FEATURES, r->a);
        r->x = (uint8_t)(fw_rd8(f, PROS_POS) - 1);           /* DEC pros_pos; LDX pros_pos */
        fw_wr8(f, PROS_POS, r->x);
        if (r->x == 0)
            break;                                           /* BEQ $CEEC */
        par_ready(f, *r);
        r->y = load_symbol(f, r->x);
        uint8_t sym = r->y, cls = fw_rd8(f, PROS_CLASS);
        if (sym == '{') {                                    /* $CF4B: nothing else at a '{' */
            fw_wr8(f, FW_PROS_STRESS_TOGGLE, 1);
            continue;
        }
        if (sym == '~')                                      /* $CF5A: past a function word, an accent again */
            fw_wr8(f, FW_PROS_ACCENT_PENDING, 1);
        if (sym == ' ' || (cls & PHC_PUNCT)) {               /* $CF5F; BBS0 */
            word_end(f, r->x);
            if (sym != ' ')
                phrase_end(f, r);
        } else if (cls & PHC_PHONETIC) {                     /* $D060 BBC7: a marker goes on */
            r->y = fw_rd8(f, PROS_FEATURES);                 /* $D068 LDY pros_features */
            symbol_counts(f, r->x);
            accent_and_nucleus(f, r->x);
        }
    }
    exit_block(f, r);
}
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/pros_gen.h"
```

with

```c
#include "firmware/pros_gen.h"
#include "firmware/pros_phrase.h"
```

- Replace

```c
/* pros_phrase_gen $D129: no register input. A, X and Y out as the ROM leaves them: A = pros_seg_count's low byte
```

with

```c
/* pros_mark_phrases $CEF6: A, X, Y in; only Y is read, by its first isr_par_ready ($CF28; A and X are loaded first,
   $CF1A-$CF23). A = pros_word_pos, X = pros_pos (0, pros_store_phrase_len's LDX) and Y as the last position left it
   (the entry Y if none), out: its RTS at $CEF5. The C's ROM calls overwrite the machine's registers, so all three are
   set from the C. The only caller, pros_make_segments ($CAB6), calls pros_phrase_gen next, whose first instruction
   JSR pros_declination ($D129) writes A, X and Y before reading them (LDA pros_baseline_end; LDX pros_pos and LDY #0
   in pros_declination_step): no reader was found. No caller reads a flag. They are defined, so all three are
   declared. */
static void pros_mark_phrases_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_mark_phrases(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_store_phrase_len $D0FD: no register input. A (1, or pros_class when its bit 3 is set) and X = pros_pos (its
   LDX) out, Y untouched (no ROM call in the C). Both callers are in pros_mark_phrases: after $CF97 it loads A (LDA
   pros_symbol) and reads no X or Y before JSR pros_declination ($D04D), which writes X and Y first (LDX pros_pos,
   LDY #0); after $CEEC it loads A (LDA pros_word_pos) and returns, and X and Y go on to pros_make_segments, where
   pros_phrase_gen's JSR pros_declination writes them first. No reader was found; no caller reads a flag. They are
   defined, so A, X and Y are declared. */
static void pros_store_phrase_len_regs(fw *f, cpu740 *c)
{
    c->a = pros_store_phrase_len(f);
    c->x = fw_rd8(f, PROS_POS);
}

/* pros_phrase_gen $D129: no register input. A, X and Y out as the ROM leaves them: A = pros_seg_count's low byte
```

- Replace

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_phrase_gen)
```

with

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_mark_phrases)
THUNK(pros_store_phrase_len)
THUNK(pros_phrase_gen)
```

- Replace

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
```

with

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_mark_phrases",  0xCEF6, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_mark_phrases },
    { "pros_store_phrase_len", 0xD0FD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_store_phrase_len },
```

The table stays in address order (26 rows).

`tests/test_diff_lts.c`:
- Replace

```c
#define DIFF_MAX        24
```

with

```c
#define DIFF_MAX        28
```

`pros_phrase.c` and `pros_flags.h` clash with no existing basename (`ls src/firmware tests | grep -E "^pros_(phrase|flags)"` shows only this task's files).

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_D0FD_pros_store_phrase_len.txt`: `LDA $1C15 / BNE` → `== 0`; `LDA pros_phrase_len / ORA #$80` → `a |= PROS_PHRASE_LEN_MARK` (A only); `LDX pros_pos / STA $1F00,X / STA $1E00,X` → the two stores, lengthen first; `LDA #0 / STA pros_phrase_len / STA $1C15`; `LDA pros_class / BBS3 A / LDA #1 / STA $1C15` → the returned A.
- `rom_CEF6_pros_mark_phrases.txt`:
  - $CEF6–$CF17 → `init_state`: ten stores in the ROM's order ($1C16, $1C1B, $1C0D, $1C0E, $1C04 = 0; $1C19, $1C0F, $1C1C, $1C13 = 1; $1C1E = 7).
  - $CF1A–$CF28 → the loop head: `LDA pros_features / STA pros_prev_features` (r.a), `DEC pros_pos / LDX` (8 bits, r.x), `BEQ $CEEC` → `break`, `JSR isr_par_ready` → `par_ready(f, *r)`.
  - $CF2B–$CF41 → `load_symbol` (`TAY` → r.y), the class and features through `fw_rom8` at `PH_SYMBOL_CLASS`/`PH_SYMBOL_FEATURES`.
  - $CF44–$CF50 `{` → `continue` after `$1C0D = 1`; $CF53–$CF5C `~` → `$1C19 = 1`; $CF5F–$CF68 `CMP #$20 / BEQ`, `BBS0` → the `if`, `JMP $D05D` → the `else if`.
  - $CF6B–$CF8A → `word_end` (eight stores in order); $CF8D–$CF94 → `if (sym != ' ')`.
  - $CF97 → `pros_store_phrase_len`; $CF9A–$D04A → `phrase_params` (the order `?`, `!`, `.`/`;`, `,`/`:` is a `switch` of disjoint cases; stores stress, nucleus, final, baseline, baseline end, then $1C1E = 7; `JMP $D04D` at $D001 → `default: return`); $D04D `JSR pros_declination` → the C call, X and Y as in its thunk; $D050–$D057 → `k`.
  - $D05D–$D060 `BBC7` → `else if (cls & PHC_PHONETIC)`; $D062–$D0BB → `symbol_counts` (`BBC6`, `BEQ`, `BBS1`/`BBC7` = consonant and prev & $82; the three `CLC/ADC` blocks; `CMP #5 / BCC`); $D068 `LDY` → `r->y`.
  - $D0BE–$D0F7 → `accent_and_nucleus` (the `BBS7`/`BBS1`/`BRA` → `& (PHF_VOWEL | PHF_LONG)`; `LDA #$60` → `PSF_ACCENT | PSF_STRESS`; `ORA $1C1E / STA / BBC6 A` on the ORed value).
  - $CEEC–$CEF5 → `exit_block` (r.a = `pros_word_pos`, r.x = `pros_pos`, `STA $1D00` absolute).
- For `fw.h` and `pros_flags.h`: check each comment's addresses in `rom_CABD_pros_mark_stress.txt` and `rom_CEF6_pros_mark_phrases.txt` (every store and read of each $1Cxx byte), and the class bytes `PH_SYMBOL_CLASS` + `.` `,` `?` `!` `:` `;` `-` ($0B $05 $0D $01 $0B $0B $01).

For the thunk outputs, re-read `rom_CA92_pros_make_segments.txt` ($CAB6–$CAB9), `rom_D129_pros_phrase_gen.txt` ($D129), `rom_D3DD_pros_declination.txt` and `rom_D40D_pros_declination_step.txt` (A, X and Y written before read), and `rom_CEF6_pros_mark_phrases.txt` ($CF9A–$D04D, $CEEF–$CEF5). Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xCEEC,0xCEF6,0xD0FD) for op in (0x20,0x4C)]"
```

Expected:

```
0xceec 0x20 []
0xceec 0x4c []
0xcef6 0x20 ['0xcab6']
0xcef6 0x4c []
0xd0fd 0x20 ['0xceec', '0xcf97']
0xd0fd 0x4c []
```

and for relative branches into the three entries (every byte position, 2-byte branches, `BBS`/`BBC` on A, and 3-byte `BBS`/`BBC` on zero page):

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xCEEC,0xCEF6,0xD0FD):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected: `0xceec ['0xcf26']`, `0xcef6 []`, `0xd0fd []`.

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_pros"`, `cmd //c ".\build.bat test diff_lts"`, `cmd //c ".\build.bat test golden_hybrid"`, `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_pros                ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 26 lines `<name> N calls checked`, each with N > 0; the new two between `pros_reset_seg_ptrs` and `pros_phrase_gen`:

```
  pros_reset_seg_ptrs  365 calls checked
  pros_mark_phrases    207 calls checked
  pros_store_phrase_len 374 calls checked
  pros_phrase_gen      207 calls checked
```

  `pros_mark_phrases` equals `pros_phrase_gen`, and `pros_store_phrase_len` equals `pros_declination` and `pros_declination_step` (374 each); every other count is unchanged from Task 1. Then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged, and 0 mismatches. Task 1's `long_words` brings $D0B5 and `syllabic_stress` $D0CD into the checked calls.
- `golden_hybrid`: `42 corpus files x 27 modes (all, then each routine alone)`, all identical.
- The full run: `31 suite(s), 0 failure(s), 0 skip(s)`, with no warnings, the firmware syntax check passing, and `firmware_data` still at 38197.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/fw.h src/firmware/pros_flags.h src/firmware/pros_gen.c src/firmware/pros_addr.h src/firmware/pros_phrase.h src/firmware/pros_phrase.c src/engine/thunks.c tests/test_diff_lts.c tests/test_fw_pros.c
git commit -m "pros_store_phrase_len and pros_mark_phrases (with its exit block) in C, with hooks; the passes' RAM names in fw.h, the pros_sym_flags bits in pros_flags.h (M4.3b 2.2, 2.6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `pros_mark_stress` $CABD in C, and its hook (spec §2.2, §2.3, §2.4, §2.7, §4.4)

**Files:**
- Create: `src/firmware/pros_stress.h`, `src/firmware/pros_stress.c`
- Modify:
  - `src/firmware/pros_addr.h` (`ROM_PROS_MARK_STRESS`);
  - `src/engine/thunks.c` (the `pros_mark_stress` thunk and its row);
  - `tests/test_fw_pros.c` (the stress-pass tests).

(`DIFF_MAX` in `tests/test_diff_lts.c` is 28 since Task 2, enough for this task's 27 rows.)

**Where the code goes.** `pros_mark_stress` is one ROM routine whose blocks are joined by `JMP`s: about a dozen of them lead back to `$CB1F`, and two lead into the shared tail `$CD52`. In `src/firmware/pros_stress.c` each block is one helper. The registers that reach `isr_par_ready` at `$CB1F` travel in an explicit `fw_regs`, as `pros_gen.c` does for `$D155`:

| Helper | ROM |
|---|---|
| `start` | $CABD–$CB1C: the state, position 0 of the arrays, the event queue |
| `pros_mark_stress` (the loop) | $CB1F–$CB2C: `isr_par_ready`, `INC pros_pos`, the end test, the `RTS` |
| `position` | $CB2D–$CB48 and the dispatch between the blocks ($CBAD–$CBB3, $CC21–$CC24, $CCDA–$CCDD) |
| `look_ahead` | $CB4B–$CBAA: the next symbol and the next phonetic symbol |
| `marker` | $CBB6–$CC1E: `~ \| } { `` ` `` x`, with the linking R ($CBDB–$CBEE) |
| `stress_toggle` | $CC27–$CC5D |
| `pros_stress_counts` (public, see below) | $CC60–$CCD7: the stress flag, then the counts from the stress state |
| `consonant` | $CCE0–$CD4F, with the double-consonant exit $CD0B–$CD1B |
| `vowel` | $CD5E–$CE95 |
| `save_features` | $CD52–$CD5B, the shared tail |
| `word_end` | $CE98–$CEE9 |
| `par_ready`, `add`, `adc`, `sym_class`, `sym_features`, `phonetic` | the `JSR isr_par_ready`; `LDA arr,X / CLC / ADC #n / STA arr,X`; `ADC` with its carry; the two table reads through `fw_rom8`; `LDA $1900,X` |

**Interfaces:**
- Consumes:
  - Task 2's RAM names in `fw.h`: `FW_PROS_NEXT_SYM`, `FW_PROS_NEXT_FEATURES`, `FW_PROS_NEXT_PHON`, `FW_PROS_NEXT_PHON_FEATURES`, `FW_PROS_SYLLABLES`, `FW_PROS_VOWEL_SEEN`, `FW_PROS_TILDE_SHORTEN`, `FW_PROS_WRITE_ONLY_1C12`, `FW_PROS_FINAL`, `FW_PROS_BACKQUOTE`, `FW_PROS_PUNCT_NOT_KIND3`, `FW_PROS_ACCENT_PENDING`, `FW_PROS_PREV_FEATURES_COPY` and `FW_PROS_NUCLEUS_MASK`.
  - Task 2's `pros_flags.h`: `PSF_FUNC_WORD`, `PSF_UNRELEASED`, `PSF_STRESS`.
  - M4.3a's `FW_PROS_STRESS_TOGGLE` and `FW_UTT_CONTENT` (`fw.h`).
  - M4.3a's `PH_SYMBOL_CLASS`, `PH_SYMBOL_FEATURES`, `PHC_*` and `PHF_*` (`data/fw_data.h`).
  - The harness of `tests/test_fw_pros.c`: `g_ram`, `g_f`, `g_calls`, `g_ncalls`, `setup()`, `count()`, `check_call()`, `check_row()`; Task 2's `firmware/pros_flags.h` include there, and `test_phrases_empty();` as the last line of `suite_fw_pros`.
  - `DIFF_MAX` 28 (Task 2).
  - M4.0–M4.2's `fw_rom8`, `fw_rom_call`, `FW_ROM_CALL_BUDGET` and `ROM_ISR_PAR_READY` (`tn_convert.h`).
- Produces:

```c
/* pros_addr.h */
#define ROM_PROS_MARK_STRESS        0xCABD
/* pros_stress.h */
void pros_mark_stress(fw *f, fw_regs *r);   /* r in: entry registers; r out: exit A, X, Y (RTS $CB2C) */
void pros_stress_counts(fw *f);             /* $CC60-$CCD7 alone, for the unit test of the carry into $CCD5 */
```

  The replacement `"pros_mark_stress"` at `0xCABD` (outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y`): 27 replacements in all, 28 `golden_hybrid` modes. Task 4's C `pros_make_segments` calls `pros_mark_stress` directly, with the `fw_regs` that `pros_reset_seg_ptrs` leaves (A = $0E; X and Y are the caller's).

**ROM facts checked (in `build/ghidra740/dump/rom_CABD_pros_mark_stress.txt` unless named):**
- **`BEQ $CCBA` taken is reachable from a text.** Spec §4.4 says it is unreachable because "the accent state is cleared only after a vowel has counted a syllable". That is not so when the vowel's next phonetic symbol is another vowel behind a class-0 marker:
  - In `A|A`, the first `A`'s look-ahead skips `|` (class 0) and sees the second `A`. So `$CD61`/`$CD63` count no syllable, but `$1C0C` is set.
  - At `|` (→ `R`, a consonant before a vowel) `$CC42` toggles the stress to 0 and clears the accent state.
  - At `$CCB7`, `$1C0B` is still 0, so the `BEQ` is taken. It is taken again at the last `A`.

  The branch probe (a scratch tool: `NOCORPUS=1 brprobe.exe` on a file holding `@X1 A|A. @X0`) prints `BR CCBA taken 2 fall 0`; over boot and the corpus before Task 1 it is `taken 0`. Task 1 therefore ends `syllabic_stress` with ` A|A.` (`BR CCBA taken 2 fall 0` for that text alone), so `diff_lts` and `golden_hybrid` compare the C on it; `test_stress_no_syllable` also runs `A|A` through `pros_mark_stress` itself. Task 5 corrects the spec in "As built".
- **The carry into `$CCD5` cannot be set by `pros_mark_stress`.**
  - `$CB35` clears the position's shortening count. Before `$CCB2` at most +3 ($CC96) and +5 ($CCA4) are added, so `ADC #3` sees at most 8 and never carries.
  - The only way to test the missing `CLC` is to run the block `$CC60–$CCD7` on RAM set up directly (spec §4.4: "The test sets up RAM directly"). So `pros_stress.h` declares one function beyond the routines' own signatures: `void pros_stress_counts(fw *f)`. It is the block's helper, made public for the test; `pros_mark_stress` is its only caller in `src/`.
  - Its carry path: `CLC; ADC #3` at $CCB2. Then `LDA $1C0B; BEQ`, or `TYA; BBC7`; then `TYA; BBC2; LDA $1C07; BBC7; LDA $1E00,X`. None of these touches C, so `ADC #$0C` at $CCD5 adds the $CCB2 carry.
- **`$CCFB` reads the class table at a position, and `$CD02` lengthens the *next* position.**
  - `$CCFA INX` makes X = `pros_pos` + 1. So `LDA $9BB4,X` is `ph_symbol_class[pos + 1]`, and the `ADC #8` at $CD02–$CD08 goes to `pros_sym_lengthen[pos + 1]`.
  - The next position clears that byte at its start ($CB32), so the add survives only when pos + 1 = `tn_phon_len`. There it adds to a stale byte past the text, which can wrap mod 256.
  - `test_stress_count_wrap` pins both: class `'!'` = 1 at index 33, and $FC + 8 = $04.
  - The spec mentions only the class read. A port that read `ph_symbol_class[tn_phonetic[pos+1]]` passes `diff_lts` on the corpus (the next position clears the difference), but fails this test.
- **`pros_class` after the look-ahead is the *next* symbol's.**
  - The scans store `pros_class` at `$CB5D` and `$CB8B` for the next symbol with class ≠ 0. Only when the scan reaches `tn_phon_len` ($CB9D) does it stay the current symbol's.
  - So `word_end`'s `BBC1` at $CEBB tests the class of the symbol *after* the punctuation (e.g. `'?'` at the text's end keeps its own $0D, so bit 1 is clear).
  - The C reads RAM, as the ROM does.
- **The look-ahead tests for the space only in its first loop** ($CB53). After a space, the second loop ($CB7D) skips further spaces as class 0. On a find it writes only `pros_class`, `$1C09` and `$1C08`, so `$1C06` stays `' '` and `$1C07` stays 0.
- **The "first of two equal symbols" rewrite ($CD18) happens only to consonants.** `$CD0B` is reached only from the consonant block `$CCE0`. It skips the shared tail (`JMP $CB1F` at $CD1B), so `pros_prev_features` and `$1C1A` keep the previous symbol's features (`test_stress_paths`: `MM` → `|M`).
- **The vowel path** ($CD5E) is taken for features ≠ 0 with bit 6 clear. Besides the vowels, that includes `x` (an unstressed vowel, $81), the digits with $81 and `b`. Its rewrites of `$1C08`/`$1C09` at `$CDBF`/`$CDC6` are RAM writes, which the next position's look-ahead overwrites. The `'1'`/`'L'` path starts the consonant scan at pos + 3, the other path at pos + 2. The scan (`$CDD9`) has no bound, and X wraps inside page $19.
- **`$CC37` reads `tn_phonetic[pos + 1]` with no bound.** At the last position this is a stale byte past the text, and it can toggle the stress (`test_stress_past_end`). X never wraps here, because `pros_pos` < `tn_phon_len` ≤ 255.
- **The registers at `$CB1F`** (`rom_C5D6_isr_par_ready.txt`: `isr_par_ready` preserves A, X and Y). X = `pros_pos` on every path except the first. Y = `pros_features` except where stated:
  - first call (after `$CB0E`/`$CB1C`): A = 0 with no events (`LDA cmd_ev_count` = 0), else `cmd_ev_positions[0]`'s high byte (`LDA $1207`; `LDM` leaves A); X = 0 (`TAX` at $CAEC); Y = the caller's;
  - `~` ($CBCC): A = 8;
  - `|` before a non-vowel ($CBD8): A = `$1C09`;
  - `}` ($CBFD): A = $FF; `{` ($CC09): A = 0; `` ` `` ($CC15): A = 1;
  - the first of two equal consonants ($CD1B): A = `'|'`;
  - the shared tail ($CD5B): A = `pros_features`. Y = `pros_features` from a consonant; from a vowel Y = `$1C07` (`LDY` at $CDF0);
  - the end of a word ($CEE9): A = $FF, Y = `pros_features` = 0.

  The block `$CBAD–$CBB3` reloads X, A and Y after the look-ahead, and `$CC60/$CC63` reload X and Y after `stress_toggle`'s `TAY`/`INX`. So the look-ahead's registers never reach `$CB1F`.
- **The exit registers** (`$CB2C RTS`): A and Y as at the last `isr_par_ready`, and X = `pros_pos` (= `tn_phon_len`; with `tn_phon_len` = 0, X = 0).
- **Who reads them** (`rom_CA92_pros_make_segments.txt`, `rom_CEF6_pros_mark_phrases.txt`, `rom_D0FD_pros_store_phrase_len.txt`, `rom_D40D_pros_declination_step.txt`):
  - `pros_make_segments` goes on with `JSR pros_mark_phrases` at $CAB6.
  - `pros_mark_phrases` writes A ($CEF6 `LDA #0`) and X ($CF23 `LDX pros_pos`) before reading them. Its first `isr_par_ready` ($CF28) gets this Y.
  - With `tn_phon_len` = 1 the `BEQ $CEEC` comes first. `pros_store_phrase_len` reads no Y, so the Y leaves `pros_mark_phrases` and `pros_declination_step` overwrites it (`LDY #0` at $D41D).
  - No reader takes a flag. A, X and Y are declared.
- **Details the spec leaves out** (none contradicts it):
  - The start writes the low byte of `cmd_ev_pos` only. Its high byte is untouched without events.
  - It does not write `$1C01`, `$1C06`–`$1C0A`, `$1C12`, `$1C13`, `$1C16`–`$1C18`, `$1C1C`, `$1C1D` or `$1C1F` (`test_stress_start` checks all 32 bytes).
  - The stress flag is a `STA #$40` ($CC77), not an `ORA`.
  - The function-word flag is a `STA #8` ($CBC9).
  - The unreleased-stop flag is an `ORA #$10` ($CCEB).
- **Callers:** `JSR $CABD` only at $CAB3 (in `pros_make_segments`); no `JMP` to it. No ROM code outside $CABD–$CEEB jumps into the routine. The two byte matches the scan finds are operand bytes, not instructions:
  - $BB53 is inside `LDA $2000,Y` / `STA $16CC,X` in `par_reset_tracks`;
  - $C10D is inside `JSR $C14C` / `DEX` in `par_step_tracks`.
- **Proof run** (scratch worktree off main c04267e with Tasks 1 and 2 applied):
  - `diff_lts`: `pros_mark_stress     207 calls checked`, 0 mismatches. It is reached once per `pros_make_segments` call, the same count as `pros_phrase_gen`.
  - `golden_hybrid`: `42 corpus files x 28 modes (all, then each routine alone)`, all identical.
  - The full run: `31 suite(s), 0 failure(s), 0 skip(s)`, no warnings, and `38197 converted bytes compared with the ROM, 0 differ`.
  - While drafting, every unit-test scenario below was also run through the ROM itself (scratch only, not part of the plan): `machine_rom_call` of $CABD on the same RAM image with `isr_par_ready` hooked, and for `pros_stress_counts`, $CC60 with a return hook at $CCDA. RAM ($0000–$3FFF except the SFRs and the stack page), the exit A/X/Y and every `isr_par_ready` A/X/Y matched in all 14 runs.
  - A mutation run failed both a unit test and that ROM comparison for each of:
    - a `CLC` before $CCD5;
    - a consonant scan stopped at X = $FF;
    - the class read at `$CCFB` by symbol instead of by position.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_pros.c` (Task 2 already includes `firmware/pros_flags.h`):
- Replace

```c
#include "firmware/pros_seg.h"
```

with

```c
#include "firmware/pros_seg.h"
#include "firmware/pros_stress.h"
```

- Replace

```c
void suite_fw_pros(void)
{
```

with

```c
/* ---- the stress pass (M4.3b Task 3) ---- */

/* setup(); tn_phonetic = text; every count, flag and state byte 0 (the caller adds stale bytes past the text). */
static void stress_setup(const char *text)
{
    setup();
    size_t n = strlen(text);
    memcpy(g_ram + TN_PHONETIC, text, n);
    g_ram[TN_PHON_LEN] = (uint8_t)n;
}

/* Runs the stress pass with the entry registers a, x, y; returns the registers it leaves. */
static fw_regs stress_run(uint8_t a, uint8_t x, uint8_t y)
{
    fw_regs r = { a, x, y, 0 };
    pros_mark_stress(&g_f, &r);
    return r;
}

static void check_bytes(uint16_t addr, const uint8_t *want, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_ram[addr + i], want[i]);
}

static void test_stress_row(void)
{
    check_row("pros_mark_stress", ROM_PROS_MARK_STRESS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* $CABD-$CB1C with tn_phon_len 0 (empty tn_phonetic: one isr_par_ready, then the RTS): the state bytes it clears and
   sets, the ones it leaves; the event setup without and with events */
static void test_stress_start(void)
{
    static const uint8_t want[32] = {   /* $1C00-$1C1F after the pass; $55 = not written */
        0x00, 0x55, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x00, 0x00, 0xFF, 0x00, 0x00,
        0x00, 0x01, 0x55, 0x55, 0x00, 0x00, 0x55, 0x55, 0x55, 0x01, 0x00, 0xFF, 0x55, 0x55, 0x00, 0x55,
    };
    stress_setup("");
    memset(g_ram + PROS_POS, 0x55, 32);
    g_ram[PROS_SYM_FLAGS] = g_ram[PROS_SYM_SHORTEN] = g_ram[PROS_SYM_LENGTHEN] = 0xAA;
    g_ram[CMD_EV_NEXT] = 9;
    fw_wr16(&g_f, CMD_EV_POS, 0x7766);
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);            /* read only when cmd_ev_count is not 0 */
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_PAR_READY, 0, 0, 0x33);       /* A = cmd_ev_count (BEQ), X = 0 (TAX), Y = the caller's */
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);                                   /* pros_pos: $FF + 1 */
    CHECK_EQ(r.y, 0x33);
    check_bytes(PROS_POS, want, 32);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS], 0);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN], 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN], 0);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x77FF);        /* the low byte only */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    CHECK(!g_f.fault);

    stress_setup("");
    g_ram[CMD_EV_COUNT] = 2;
    fw_wr16(&g_f, CMD_EV_POS, 0x7766);
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);
    r = stress_run(0x11, 0x22, 0x33);
    check_call(0, ROM_ISR_PAR_READY, 0x03, 0, 0x33);    /* A = the high byte, the last one read */
    CHECK_EQ(r.a, 0x03);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0304);        /* both bytes of the first position */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);              /* LDM #$FF */
    CHECK_EQ(g_ram[CMD_EV_NEXT], 0);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 2);
    CHECK(!g_f.fault);
}

/* Every way back to isr_par_ready at $CB1F, with its registers, and the three tn_phonetic rewrites */
static void test_stress_paths(void)
{
    static const uint8_t flags[12] = { 0, PSF_FUNC_WORD, 0, 0, 0, 0, 0, 0, 0, 0, PSF_STRESS, 0 };
    static const uint8_t shorten[12] = { 0, 0, 0, 0, 0, 0, 8, 8, 8, 6, 3, 1 };
    static const uint8_t lengthen[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 1, 1 };
    stress_setup(" ~}{`|MMA|E?");
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 13);
    check_call(0, ROM_ISR_PAR_READY, 0x00, 0, 0x33);    /* the start */
    check_call(1, ROM_ISR_PAR_READY, 0xFF, 0, 0x00);    /* ' ': the word end, A = $FF, Y = features 0 */
    check_call(2, ROM_ISR_PAR_READY, PSF_FUNC_WORD, 1, 0x00);   /* '~' */
    check_call(3, ROM_ISR_PAR_READY, 0xFF, 2, 0x00);    /* '}' */
    check_call(4, ROM_ISR_PAR_READY, 0x00, 3, 0x00);    /* '{' */
    check_call(5, ROM_ISR_PAR_READY, 0x01, 4, 0x00);    /* '`' */
    check_call(6, ROM_ISR_PAR_READY, 0x49, 5, 0x00);    /* '|' before M: A = M's features ($1C09) */
    check_call(7, ROM_ISR_PAR_READY, '|', 6, 0x49);     /* M before M: becomes | */
    check_call(8, ROM_ISR_PAR_READY, 0x49, 7, 0x49);    /* the consonant tail: A = Y = its features */
    check_call(9, ROM_ISR_PAR_READY, 0x83, 8, 0x83);    /* the vowel tail: Y = the next symbol's features */
    check_call(10, ROM_ISR_PAR_READY, 0x45, 9, 0x45);   /* '|' before E: R, a consonant */
    check_call(11, ROM_ISR_PAR_READY, 0x83, 10, 0x00);  /* E before the end: Y = 0 */
    check_call(12, ROM_ISR_PAR_READY, 0xFF, 11, 0x00);  /* '?': the word end */
    CHECK_EQ(r.a, 0xFF);
    CHECK_EQ(r.x, 12);
    CHECK_EQ(r.y, 0x00);
    CHECK(memcmp(g_ram + TN_PHONETIC, " ~}{`||MARE.", 12) == 0);   /* MM -> |M, |E -> RE, `...? -> . */
    check_bytes(PROS_SYM_FLAGS, flags, 12);
    check_bytes(PROS_SYM_SHORTEN, shorten, 12);
    check_bytes(PROS_SYM_LENGTHEN, lengthen, 12);
    CHECK_EQ(g_ram[PROS_POS], 12);
    CHECK_EQ(g_ram[PROS_SYMBOL], '?');                  /* read before the rewrite */
    CHECK_EQ(g_ram[PROS_CLASS], 0x0D);
    CHECK_EQ(g_ram[PROS_PREV_FEATURES], 0x83);
    CHECK_EQ(g_ram[FW_PROS_BACKQUOTE], 0);
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 1);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0xFF);
    CHECK_EQ(g_ram[FW_PROS_NEXT_SYM], 0x0D);            /* no next symbol */
    CHECK_EQ(g_ram[FW_PROS_NEXT_PHON], 0x0D);
    CHECK(!g_f.fault);
}

/* A vowel before | and a vowel counts no syllable, so the R gets the toggle's accent state 0 with the syllable count
   still 0: BEQ $CCBA taken (twice: at R and at the last A). The spec calls it unreachable; syllabic_stress has it. */
static void test_stress_no_syllable(void)
{
    static const uint8_t shorten[3] = { 0, 5 + 3 + 12 + 3, 5 + 3 };   /* R: the $0C at $CCD5 (carry 0) */
    static const uint8_t lengthen[3] = { 8, 4, 5 };
    stress_setup("A|A");
    fw_regs r = stress_run(0, 0, 0);
    CHECK_EQ(g_ncalls, 4);
    check_call(1, ROM_ISR_PAR_READY, 0x83, 0, 0x83);
    check_call(2, ROM_ISR_PAR_READY, 0x45, 1, 0x45);
    check_call(3, ROM_ISR_PAR_READY, 0x83, 2, 0x00);
    CHECK_EQ(r.x, 3);
    CHECK(memcmp(g_ram + TN_PHONETIC, "ARA", 3) == 0);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], PSF_STRESS);
    check_bytes(PROS_SYM_SHORTEN, shorten, 3);
    check_bytes(PROS_SYM_LENGTHEN, lengthen, 3);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[FW_PROS_SYLLABLES], 1);
    CHECK(!g_f.fault);
}

/* The block $CC60-$CCD7 alone at pros_pos 5: a liquid before a vowel with the accent state 0 */
static uint8_t stress_counts(uint8_t shorten)
{
    setup();
    g_ram[PROS_POS] = 5;
    g_ram[PROS_FEATURES] = 0x45;                        /* R: a consonant, liquid, voiced */
    g_ram[FW_PROS_STRESS_TOGGLE] = 0xFF;
    g_ram[FW_PROS_NEXT_FEATURES] = 0x83;                /* a vowel next */
    g_ram[PROS_SYM_SHORTEN + 5] = shorten;
    pros_stress_counts(&g_f);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 5], 0);             /* accent state 0: no stress flag */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 5], 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
    return g_ram[PROS_SYM_SHORTEN + 5];
}

static void test_stress_carry(void)   /* $CCD5: ADC #$0C with no CLC, after the CLC; ADC #3 at $CCB2 */
{
    CHECK_EQ(stress_counts(0x10), 0x10 + 3 + 12);
    CHECK_EQ(stress_counts(0xFC), 0x0B);                /* $FC + 3 = $FF, no carry; + 12 wraps to $0B */
    CHECK_EQ(stress_counts(0xFE), 0x0E);                /* $FE + 3 = $01, carry; + 12 + 1 = $0E (not $0D) */
}

/* $CDD9: the consonant scan after a vowel has no bound; X wraps from $FF to 0 inside page $19 */
static void test_stress_scan_wrap(void)
{
    stress_setup(".E");
    memset(g_ram + TN_PHONETIC + 2, 'M', 254);          /* consonants from position 2 to 255 */
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 1);                  /* the scan stopped at position 0, '.' (class bit 0) */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4 + 4);      /* the first word: + 4; before punctuation: + 4 */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 1], PSF_STRESS);
    CHECK(!g_f.fault);

    stress_setup(" E");
    memset(g_ram + TN_PHONETIC + 2, 'M', 254);
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);                  /* position 0 is a space this time */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4 + 1);
    CHECK(!g_f.fault);
}

/* Reads past tn_phon_len: after A, L makes the scan start at position 3 and the next phonetic symbol position 2's
   ($CDBA, $1C12 = 1); at L, $CC37 reads position 2: a vowel there toggles the stress */
static void test_stress_past_end(void)
{
    stress_setup("AL");
    g_ram[TN_PHONETIC + 2] = 'E';
    g_ram[TN_PHONETIC + 3] = '.';
    g_ram[FW_PROS_WRITE_ONLY_1C12] = 0x55;
    fw_regs r = stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C12], 1);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 1);                  /* position 3, not 2 */
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);          /* toggled at L by the E past the end */
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 5 + 3 + 3);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 4);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4);
    CHECK_EQ(g_ram[FW_PROS_NEXT_PHON], 0x0D);           /* the look-ahead at L overwrote $CDBF's 'E' */
    check_call(1, ROM_ISR_PAR_READY, 0x83, 0, 0x45);    /* Y = L's features ($1C07) */
    CHECK_EQ(r.y, 0x45);
    CHECK(!g_f.fault);

    stress_setup("AL");
    g_ram[TN_PHONETIC + 2] = 'M';
    g_ram[TN_PHONETIC + 3] = '.';
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0xFF);       /* no vowel after L */
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0xFF);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 1], 3);

    stress_setup("AE");                                 /* no L: $1C12 = 0 */
    g_ram[FW_PROS_WRITE_ONLY_1C12] = 0x55;
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[FW_PROS_WRITE_ONLY_1C12], 0);
    CHECK(!g_f.fault);
}

/* $CCFB: the class table read at a position, not at a symbol; the count lengthened is position pos + 1, here
   tn_phon_len, which no position clears: it wraps mod 256 */
static void test_stress_count_wrap(void)
{
    char text[34];
    memset(text, ' ', 31);
    strcpy(text + 31, "EM");                            /* M at position 32: ph_symbol_class[33] = '!' = 1 */
    stress_setup(text);
    g_ram[PROS_SYM_LENGTHEN + 33] = 0xFC;
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 33], 0x04);      /* $FC + 8 */
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 32], 3);
    CHECK_EQ(g_ram[PROS_SYM_SHORTEN + 31], 1);          /* E before a nasal */
    CHECK_EQ(g_ncalls, 34);
    CHECK(!g_f.fault);

    memset(text, ' ', 31);
    text[31] = 'E';
    text[32] = 'M';
    text[33] = '\0';
    stress_setup(text + 1);                             /* M at position 31: ph_symbol_class[32] = ' ' = 0 */
    g_ram[PROS_SYM_LENGTHEN + 32] = 0xFC;
    stress_run(0, 0, 0);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 32], 0xFC);
    CHECK(!g_f.fault);
}

void suite_fw_pros(void)
{
```

- Replace

```c
    test_phrases_empty();
}
```

with

```c
    test_phrases_empty();
    test_stress_row();
    test_stress_start();
    test_stress_paths();
    test_stress_no_syllable();
    test_stress_carry();
    test_stress_scan_wrap();
    test_stress_past_end();
    test_stress_count_wrap();
}
```

What the tests cover (spec §4.4, Task 3's share):
- `BEQ $CCBA` taken: `test_stress_no_syllable` (and the corpus text `syllabic_stress`, Task 1).
- The carry into `$CCD5`: `test_stress_carry`.
- The consonant scan wrapping X from $FF to $00: `test_stress_scan_wrap`.
- The three `tn_phonetic` rewrites: `test_stress_paths`, and `test_stress_no_syllable` for the R.
- The dead store `$1C12`, both values: `test_stress_past_end`.
- The event setup with and without events, and empty `tn_phonetic`: `test_stress_start`.
- A count wrapping mod 256: `test_stress_count_wrap` (at $CD02), and `test_stress_carry` (at $CCD5).
- `isr_par_ready`'s registers on every path to $CB1F: `test_stress_paths`, plus the start in `test_stress_start`.
- The hook's row: `test_stress_row`.

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_pros"`
Expected: a compile error, because `firmware/pros_stress.h` does not exist.

- [ ] **Step 3: Implement**

Create `src/firmware/pros_stress.h`:

```c
#ifndef FW_PROS_STRESS_H
#define FW_PROS_STRESS_H
/* The stress pass of the prosody (ROM $CABD-$CEEB), decompiled (M4.3b spec 2.2): the first of the two passes over
   tn_phonetic that pros_make_segments runs before the phrase generator. */
#include "fw.h"

/* $CABD: clears the pass state; cmd_ev_next = 0 and cmd_ev_pos = $FF in its low byte, or, when cmd_ev_count is not
   0, both bytes of the first event's position and $6B = $FF. Then per position from 0 to tn_phon_len - 1:
   isr_par_ready; the position's flags and counts cleared; the next symbol and the next phonetic symbol looked up;
   the markers ~ | } { ` x; the stress toggle and the stress flag; the shortening and lengthening counts from the
   context (8 bits each); a word's syllable count stored at the space or punctuation that ends it. tn_phonetic is
   rewritten in place: | before a vowel becomes R, the first of two equal consonants becomes |, a ? after a
   backquote becomes a full stop.
   r in: the registers at entry (Y reaches isr_par_ready at the first position; A and X are written first).
   r out: the A, X and Y the ROM leaves at its RTS ($CB2C): A and Y as at the last isr_par_ready, X = pros_pos
   (tn_phon_len). r->p is not used. */
void pros_mark_stress(fw *f, fw_regs *r);

/* $CC60-$CCD7, one block of pros_mark_stress, public only for its unit test (M4.3b spec 4.4: the carry into $CCD5,
   which pros_mark_stress itself never sets, because it clears the position's counts first). At pros_pos, from
   pros_features: the stress flag, then the counts from the stress state. The ADC #$0C at $CCD5 has no CLC: it
   adds the carry out of the ADC #3 at $CCB2. */
void pros_stress_counts(fw *f);

#endif
```

Create `src/firmware/pros_stress.c`:

```c
#include "pros_stress.h"
#include "pros_flags.h"
#include "tn_convert.h"
#include "data/fw_data.h"

#define SYM_NONE  0x0D   /* $CB9D: the look-ahead ran into tn_phon_len: no next symbol (a carriage return) */

/* isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static void par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }

/* LDA array,X; CLC; ADC #n; STA array,X: an 8-bit count. */
static void add(fw *f, uint16_t array, uint8_t x, uint8_t n)
{
    fw_wr8(f, (uint16_t)(array + x), (uint8_t)(fw_rd8(f, (uint16_t)(array + x)) + n));
}

/* ADC #m: a + m + the carry in *c; the carry out back in *c. */
static uint8_t adc(uint8_t a, uint8_t m, uint8_t *c)
{
    unsigned s = (unsigned)a + m + *c;
    *c = (uint8_t)(s >> 8);
    return (uint8_t)s;
}

static uint8_t sym_class(fw *f, uint8_t sym) { return fw_rom8(f, (uint16_t)(PH_SYMBOL_CLASS + sym)); }
static uint8_t sym_features(fw *f, uint8_t sym) { return fw_rom8(f, (uint16_t)(PH_SYMBOL_FEATURES + sym)); }
static uint8_t phonetic(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(TN_PHONETIC + x)); }   /* $1900,X */

/* $CABD-$CB1C: the pass state, the arrays' position 0, the event queue. r: A and X as at the first isr_par_ready
   (Y is the caller's). */
static void start(fw *f, fw_regs *r)
{
    static const uint16_t zeroed[] = {   /* in the ROM's order */
        PROS_SYMBOL, PROS_FEATURES, PROS_CLASS, PROS_PREV_FEATURES, FW_PROS_PREV_FEATURES_COPY, PROS_WORD_POS,
        PROS_PHRASE_LEN, FW_PROS_SYLLABLES, FW_PROS_VOWEL_SEEN, PROS_FUNC_WORD, FW_PROS_BACKQUOTE,
        FW_PROS_PUNCT_NOT_KIND3, FW_PROS_NUCLEUS_MASK, PROS_SYM_FLAGS, CMD_EV_NEXT,
    };
    for (unsigned i = 0; i < sizeof zeroed / sizeof zeroed[0]; i++)
        fw_wr8(f, zeroed[i], 0);
    r->x = 0;                                           /* TAX */
    fw_wr8(f, PROS_SYM_LENGTHEN, 0);
    fw_wr8(f, PROS_SYM_SHORTEN, 0);
    fw_wr8(f, PROS_POS, 0xFF);
    fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0xFF);
    fw_wr8(f, PROS_ACCENT_STATE, 0xFF);
    fw_wr8(f, CMD_EV_POS, 0xFF);                        /* the low byte only */
    fw_wr8(f, FW_PROS_ACCENT_PENDING, 1);
    fw_wr8(f, FW_PROS_TILDE_SHORTEN, 1);
    r->a = fw_rd8(f, CMD_EV_COUNT);
    if (r->a != 0) {                                    /* the first event's position, both bytes */
        fw_wr8(f, CMD_EV_POS, fw_rd8(f, CMD_EV_POSITIONS));
        r->a = fw_rd8(f, CMD_EV_POSITIONS + 1);
        fw_wr8(f, CMD_EV_POS + 1, r->a);
        fw_wr8(f, FW_UTT_CONTENT, 0xFF);                /* LDM #$FF */
    }
}

/* $CB4B-$CBAA: from position x + 1 up to tn_phon_len, the next symbol with class != 0 (pros_class, the next
   phonetic symbol and its features); a space before it is the next symbol, with features 0. None: $0D and 0. */
static void look_ahead(fw *f, uint8_t x)
{
    uint8_t len = fw_rd8(f, TN_PHON_LEN);
    bool space = false;
    for (;;) {
        if (++x >= len) {                               /* $CB9D */
            fw_wr8(f, FW_PROS_NEXT_PHON, SYM_NONE);
            fw_wr8(f, FW_PROS_NEXT_SYM, SYM_NONE);
            fw_wr8(f, FW_PROS_NEXT_PHON_FEATURES, 0);
            fw_wr8(f, FW_PROS_NEXT_FEATURES, 0);
            return;
        }
        uint8_t sym = phonetic(f, x);
        if (!space && sym == ' ') {                     /* $CB75: then on to the next phonetic symbol ($CB7D) */
            fw_wr8(f, FW_PROS_NEXT_SYM, sym);
            fw_wr8(f, FW_PROS_NEXT_FEATURES, 0);
            space = true;
            continue;
        }
        uint8_t cls = sym_class(f, sym);
        if (cls == 0)
            continue;
        fw_wr8(f, PROS_CLASS, cls);
        uint8_t feat = sym_features(f, sym);
        fw_wr8(f, FW_PROS_NEXT_PHON_FEATURES, feat);
        if (!space)
            fw_wr8(f, FW_PROS_NEXT_FEATURES, feat);
        fw_wr8(f, FW_PROS_NEXT_PHON, phonetic(f, x));
        if (!space)
            fw_wr8(f, FW_PROS_NEXT_SYM, phonetic(f, x));
        return;
    }
}

/* where a block goes on: $CB1F (the next position), $CC21 (TYA: the features decide) or $CC27 (the stress) */
typedef enum { NEXT_POSITION, NEXT_FEATURES, NEXT_STRESS } next_block;

/* $CBAD-$CC1E: the markers. r in: X = pros_pos, A = pros_symbol, Y = pros_features ($CBAD-$CBB3). */
static next_block marker(fw *f, fw_regs *r)
{
    if (r->a == '~') {                                  /* a function word follows: shortened, no accent */
        fw_wr8(f, FW_PROS_TILDE_SHORTEN, 1);
        fw_wr8(f, FW_PROS_ACCENT_PENDING, 0);
        fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0);
        r->a = PSF_FUNC_WORD;
        fw_wr8(f, (uint16_t)(PROS_SYM_FLAGS + r->x), r->a);
        return NEXT_POSITION;
    }
    if (r->a == '|') {
        r->a = fw_rd8(f, FW_PROS_NEXT_PHON_FEATURES);
        if (!(r->a & PHF_VOWEL))
            return NEXT_POSITION;
        r->a = 'R';                                     /* $CBDB: the linking R, before a vowel */
        fw_wr8(f, (uint16_t)(TN_PHONETIC + r->x), r->a);
        fw_wr8(f, PROS_SYMBOL, r->a);
        r->y = r->a;
        r->a = sym_features(f, r->y);
        fw_wr8(f, PROS_FEATURES, r->a);
        r->y = r->a;
        r->a = fw_rd8(f, PROS_SYMBOL);
        return NEXT_STRESS;
    }
    if (r->a == '}') {                                  /* stress on */
        r->a = 0xFF;
        fw_wr8(f, FW_PROS_STRESS_TOGGLE, r->a);
        fw_wr8(f, PROS_ACCENT_STATE, r->a);
        return NEXT_POSITION;
    }
    if (r->a == '{') {                                  /* stress off */
        r->a = 0;
        fw_wr8(f, FW_PROS_STRESS_TOGGLE, r->a);
        return NEXT_POSITION;
    }
    if (r->a == '`') {                                  /* the next ? is a statement */
        r->a = 1;
        fw_wr8(f, FW_PROS_BACKQUOTE, r->a);
        return NEXT_POSITION;
    }
    if (r->a == 'x') {                                  /* an unstressed vowel */
        r->a = 0;
        fw_wr8(f, FW_PROS_STRESS_TOGGLE, r->a);
    }
    return NEXT_FEATURES;
}

/* $CC27-$CC5D: after a vowel in the word, a consonant that is syllabic (features bit 1) or comes before a vowel
   toggles the stress; the toggle to 0 clears the accent state, and a syllabic consonant with no accent state
   clears the stress. The symbol after pros_pos is read with no bound ($CC37). */
static void stress_toggle(fw *f)
{
    if (fw_rd8(f, FW_PROS_VOWEL_SEEN) == 0)
        return;
    uint8_t feat = fw_rd8(f, PROS_FEATURES);
    if (!(feat & PHF_CONSONANT))
        return;
    if (!(feat & PHF_LONG)) {
        uint8_t next = phonetic(f, (uint8_t)(fw_rd8(f, PROS_POS) + 1));
        if (!(sym_features(f, next) & PHF_VOWEL))
            return;
    }
    uint8_t t = (uint8_t)(fw_rd8(f, FW_PROS_STRESS_TOGGLE) ^ 0xFF);
    fw_wr8(f, FW_PROS_STRESS_TOGGLE, t);
    if (t == 0)
        fw_wr8(f, PROS_ACCENT_STATE, 0);
    if ((fw_rd8(f, PROS_FEATURES) & PHF_LONG) && fw_rd8(f, PROS_ACCENT_STATE) == 0)
        fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0);
}

void pros_stress_counts(fw *f)
{
    uint8_t x = fw_rd8(f, PROS_POS), y = fw_rd8(f, PROS_FEATURES);   /* $CC60 LDX, LDY */
    if (fw_rd8(f, FW_PROS_STRESS_TOGGLE) && fw_rd8(f, PROS_ACCENT_STATE) && (y & (PHF_VOWEL | PHF_LONG)))
        fw_wr8(f, (uint16_t)(PROS_SYM_FLAGS + x), PSF_STRESS);   /* STA, not ORA */
    if (fw_rd8(f, FW_PROS_ACCENT_PENDING)) {            /* $CC7A */
        fw_wr8(f, FW_PROS_TILDE_SHORTEN, 0);
        add(f, PROS_SYM_LENGTHEN, x, 4);
    }
    if (fw_rd8(f, FW_PROS_TILDE_SHORTEN))               /* $CC8D */
        add(f, PROS_SYM_SHORTEN, x, 3);
    if (fw_rd8(f, FW_PROS_STRESS_TOGGLE) == 0)          /* $CC9B */
        add(f, PROS_SYM_SHORTEN, x, 5);
    if (fw_rd8(f, PROS_ACCENT_STATE) != 0)              /* $CCA9 */
        return;
    uint16_t at = (uint16_t)(PROS_SYM_SHORTEN + x);
    uint8_t c = 0;                                      /* CLC */
    fw_wr8(f, at, adc(fw_rd8(f, at), 3, &c));           /* $CCB2: its carry reaches $CCD5 (LDA, BBC keep it) */
    if (fw_rd8(f, FW_PROS_SYLLABLES) != 0 && (y & PHF_VOWEL)) {
        add(f, PROS_SYM_SHORTEN, x, 3);                 /* $CCBF, then BRA $CCDA */
    } else if ((y & PHF_LIQUID) && (fw_rd8(f, FW_PROS_NEXT_FEATURES) & PHF_VOWEL)) {   /* $CCCA */
        fw_wr8(f, at, adc(fw_rd8(f, at), 0x0C, &c));    /* $CCD5: no CLC */
    }
}

/* $CCE0-$CD4F: a consonant (features bit 6). r: X = pros_pos, Y = pros_features. Returns true when the next symbol
   is the same: the first of the two becomes | and the pass goes on to the next position (A = '|'). */
static bool consonant(fw *f, fw_regs *r)
{
    uint8_t x = r->x, feat = r->y;
    uint16_t flags = (uint16_t)(PROS_SYM_FLAGS + x);
    if ((feat & PHF_STOP) && (fw_rd8(f, FW_PROS_NEXT_FEATURES) & PHF_STOP))   /* a stop before a stop */
        fw_wr8(f, flags, (uint8_t)(fw_rd8(f, flags) | PSF_UNRELEASED));
    if ((feat & (PHF_NASAL | PHF_LIQUID)) && (fw_rd8(f, PROS_PREV_FEATURES) & PHF_VOWEL)) {   /* $CCF0 */
        /* INX; LDA $9BB4,X: the class table read at the position pos + 1, not at a symbol; the count it lengthens
           is the next position's, which that position clears again (it stays only at pos + 1 = tn_phon_len) */
        uint8_t x1 = (uint8_t)(x + 1);
        if (sym_class(f, x1) & (PHC_KIND_2 | PHC_KIND_1 | PHC_PUNCT))
            add(f, PROS_SYM_LENGTHEN, x1, 8);
    }
    x = fw_rd8(f, PROS_POS);                            /* $CD0B */
    if (fw_rd8(f, PROS_SYMBOL) == fw_rd8(f, FW_PROS_NEXT_SYM)) {
        r->a = '|';
        fw_wr8(f, (uint16_t)(TN_PHONETIC + x), r->a);
        return true;
    }
    if ((fw_rd8(f, PROS_PREV_FEATURES) & PHF_CONSONANT) && !(fw_rd8(f, PROS_FEATURES) & PHF_LONG))   /* $CD1E */
        add(f, PROS_SYM_SHORTEN, x, 6);
    if ((fw_rd8(f, FW_PROS_NEXT_PHON_FEATURES) & PHF_CONSONANT) && !(fw_rd8(f, PROS_FEATURES) & PHF_LONG))
        add(f, PROS_SYM_SHORTEN, x, 6);
    if (fw_rd8(f, FW_PROS_VOWEL_SEEN))                  /* $CD44 */
        add(f, PROS_SYM_SHORTEN, x, 3);
    return false;
}

/* $CD5E-$CE95: a vowel (features != 0, bit 6 clear). A syllable unless a vowel follows; then the counts from the
   stress, the neighbours and the consonants after it. r out: X = pros_pos, Y = the next symbol's features. */
static void vowel(fw *f, fw_regs *r)
{
    uint8_t x = r->x;
    uint8_t next = fw_rd8(f, FW_PROS_NEXT_FEATURES);
    if (next == 0 || (next & PHF_CONSONANT))            /* INC: 8 bits */
        fw_wr8(f, FW_PROS_SYLLABLES, (uint8_t)(fw_rd8(f, FW_PROS_SYLLABLES) + 1));
    fw_wr8(f, FW_PROS_VOWEL_SEEN, 1);
    if (fw_rd8(f, PROS_SYMBOL) == 'x')
        fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0);
    if (fw_rd8(f, FW_PROS_STRESS_TOGGLE) != 0) {        /* $CD81: stressed after an unvoiced stop */
        uint8_t p = fw_rd8(f, FW_PROS_PREV_FEATURES_COPY) & (PHF_STOP | PHF_VOICED);
        if (!(p & PHF_VOICED) && (p & PHF_STOP))
            add(f, PROS_SYM_LENGTHEN, x, 5);
    }
    if (fw_rd8(f, PROS_PREV_FEATURES) & PHF_VOWEL)      /* $CD93 */
        add(f, PROS_SYM_SHORTEN, x, 6);
    if (fw_rd8(f, FW_PROS_NEXT_PHON_FEATURES) & PHF_VOWEL)
        add(f, PROS_SYM_LENGTHEN, x, 4);
    uint8_t s = fw_rd8(f, FW_PROS_NEXT_PHON);           /* $CDAF */
    if (s == '1' || s == 'L') {                         /* the symbol after it becomes the next phonetic one */
        x = (uint8_t)(x + 2);
        s = phonetic(f, x);
        fw_wr8(f, FW_PROS_NEXT_PHON, s);
        fw_wr8(f, FW_PROS_NEXT_PHON_FEATURES, sym_features(f, s));
        fw_wr8(f, FW_PROS_WRITE_ONLY_1C12, 1);
        x++;
    } else {
        fw_wr8(f, FW_PROS_WRITE_ONLY_1C12, 0);
        x = (uint8_t)(x + 2);
    }
    uint8_t y;
    for (;;) {                                          /* $CDD9: no bound, X wraps within page $19 */
        y = phonetic(f, x);
        if (!(sym_features(f, y) & PHF_CONSONANT))
            break;
        x++;
    }
    uint8_t final = sym_class(f, y) & PHC_PUNCT;
    fw_wr8(f, FW_PROS_FINAL, final);
    x = fw_rd8(f, PROS_POS);                            /* $CDED */
    r->x = x;
    r->y = fw_rd8(f, FW_PROS_NEXT_FEATURES);
    y = r->y;
    if (y & PHF_STOP) {
        if (y & PHF_VOICED)
            add(f, PROS_SYM_LENGTHEN, x, final ? 4 : 1);
        else
            add(f, PROS_SYM_SHORTEN, x, final ? 6 : 2);   /* $CE15 */
    } else if ((y & (PHF_FRICATIVE | PHF_VOICED)) == (PHF_FRICATIVE | PHF_VOICED)) {   /* $CE32 */
        add(f, PROS_SYM_LENGTHEN, x, final ? 10 : 4);
    } else if (y & PHF_NASAL) {                         /* $CE56 */
        add(f, PROS_SYM_SHORTEN, x, final ? 6 : 1);
    } else if (fw_rd8(f, FW_PROS_NEXT_FEATURES) == 0) { /* $CE76: the end of the word or text */
        add(f, PROS_SYM_LENGTHEN, x, final ? 4 : 1);
    }
}

/* $CD52-$CD5B: the common tail. A = pros_features at the JMP $CB1F. */
static void save_features(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PROS_FEATURES);
    fw_wr8(f, PROS_PREV_FEATURES, r->a);
    fw_wr8(f, FW_PROS_PREV_FEATURES_COPY, r->a);
}

/* $CE98-$CEE9: a symbol with features 0 that is no marker (a space, punctuation): the word ends. Its syllable count
   goes into both count arrays. r: X = pros_pos; A = $FF at the JMP $CB1F. */
static void word_end(fw *f, fw_regs *r)
{
    uint8_t x = r->x;
    uint8_t sym = fw_rd8(f, PROS_SYMBOL);
    if (sym == ' ') {
        fw_wr8(f, FW_PROS_ACCENT_PENDING, fw_rd8(f, FW_PROS_TILDE_SHORTEN));
    } else {
        if (sym == '?' && fw_rd8(f, FW_PROS_BACKQUOTE)) {   /* after a backquote: a statement */
            fw_wr8(f, (uint16_t)(TN_PHONETIC + x), '.');
            fw_wr8(f, FW_PROS_BACKQUOTE, 0);
        }
        /* pros_class is the look-ahead's: the next symbol with class != 0, or this one's at the text's end */
        if (fw_rd8(f, PROS_CLASS) & PHC_KIND_1)
            fw_wr8(f, FW_PROS_BACKQUOTE, 0);
        fw_wr8(f, FW_PROS_ACCENT_PENDING, 1);
    }
    uint8_t n = fw_rd8(f, FW_PROS_SYLLABLES);            /* $CECA */
    fw_wr8(f, (uint16_t)(PROS_SYM_LENGTHEN + x), n);
    fw_wr8(f, (uint16_t)(PROS_SYM_SHORTEN + x), n);
    fw_wr8(f, FW_PROS_SYLLABLES, 0);
    fw_wr8(f, FW_PROS_TILDE_SHORTEN, 0);
    fw_wr8(f, FW_PROS_VOWEL_SEEN, 0);
    fw_wr8(f, FW_PROS_PREV_FEATURES_COPY, 0);
    r->a = 0xFF;
    fw_wr8(f, FW_PROS_STRESS_TOGGLE, r->a);
    fw_wr8(f, PROS_ACCENT_STATE, r->a);
}

/* $CB2D-$CEE9: one position. r in: X = pros_pos; r out: A, X, Y as at the JMP $CB1F. */
static void position(fw *f, fw_regs *r)
{
    uint8_t x = r->x;
    fw_wr8(f, (uint16_t)(PROS_SYM_FLAGS + x), 0);
    fw_wr8(f, (uint16_t)(PROS_SYM_LENGTHEN + x), 0);
    fw_wr8(f, (uint16_t)(PROS_SYM_SHORTEN + x), 0);
    uint8_t sym = phonetic(f, x);
    fw_wr8(f, PROS_SYMBOL, sym);
    fw_wr8(f, PROS_CLASS, sym_class(f, sym));
    fw_wr8(f, PROS_FEATURES, sym_features(f, sym));
    look_ahead(f, x);
    r->x = fw_rd8(f, PROS_POS);                         /* $CBAD */
    r->a = fw_rd8(f, PROS_SYMBOL);
    r->y = fw_rd8(f, PROS_FEATURES);
    next_block next = marker(f, r);
    if (next == NEXT_POSITION)
        return;
    if (next == NEXT_FEATURES) {
        r->a = r->y;                                    /* $CC21 TYA */
        if (r->a == 0) {
            word_end(f, r);
            return;
        }
    }
    stress_toggle(f);
    pros_stress_counts(f);
    r->x = fw_rd8(f, PROS_POS);                         /* as $CC60 left them */
    r->y = fw_rd8(f, PROS_FEATURES);
    if (r->y & PHF_CONSONANT) {                         /* $CCDA */
        if (consonant(f, r))
            return;
    } else {
        vowel(f, r);
    }
    save_features(f, r);
}

void pros_mark_stress(fw *f, fw_regs *r)
{
    start(f, r);
    for (;;) {
        par_ready(f, *r);                               /* $CB1F */
        fw_wr8(f, PROS_POS, (uint8_t)(fw_rd8(f, PROS_POS) + 1));
        r->x = fw_rd8(f, PROS_POS);
        if (r->x >= fw_rd8(f, TN_PHON_LEN))
            return;                                     /* $CB2C RTS */
        position(f, r);
    }
}
```

`src/firmware/pros_addr.h`: replace

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
```

with

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
#define ROM_PROS_MARK_STRESS        0xCABD   /* called by pros_make_segments $CAB3 */
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/pros_gen.h"
```

with

```c
#include "firmware/pros_gen.h"
#include "firmware/pros_stress.h"
```

- Replace

```c
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}
```

with

```c
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}

/* pros_mark_stress $CABD: Y in (it reaches isr_par_ready at $CB1F at position 0; A and X are written first, $CABD
   LDA #0 and $CAEC TAX). A and Y out as at the last isr_par_ready ($CB1F preserves them), X = pros_pos
   (= tn_phon_len, $CB25 LDX), as the RTS at $CB2C leaves them. The only ROM caller, pros_make_segments ($CAB3),
   calls pros_mark_phrases next ($CAB6), which writes A ($CEF6 LDA #0) and X ($CF23 LDX pros_pos) before reading
   them; its first isr_par_ready ($CF28) gets this Y (when tn_phon_len is 1 its BEQ $CEEC leaves first, and the Y
   passes on to pros_phrase_gen, whose pros_declination_step writes Y at $D41D). No caller reads a flag (JSR and
   LDA follow). Declared: A, X, Y. */
static void pros_mark_stress_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_mark_stress(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace

```c
THUNK(pros_reset_seg_ptrs)
```

with

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_mark_stress)
```

- Replace

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
```

with

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_mark_stress",   0xCABD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_mark_stress },
```

The table stays in address order ($CA71, $CABD, then Task 2's $CEEC/$CEF6/$D0FD rows, then $D129), with 27 rows.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each block of `build/ghidra740/dump/rom_CABD_pros_mark_stress.txt` side by side with its C, instruction by instruction. Confirm every branch, every RAM write and its order, and every 8-bit wrap:

- **$CABD–$CB1C** → `start`:
  - the 15 zero stores in the order of `zeroed[]` ($1C03, $1C04, $1C05, $1C02, $1C1A, $1C0E, $1C0F, $1C0B, $1C0C, $1C10, $1C14, $1C15, $1C1E, $1D00, $1201);
  - `TAX` (X = 0), then `$1F00` and `$1E00`;
  - $FF into `pros_pos`, `$1C0D`, `pros_accent_state` and the low byte of `cmd_ev_pos`;
  - 1 into `$1C19` and `$1C11`;
  - `LDA cmd_ev_count; BEQ`, then `$1206 → $1202` and `$1207 → $1203` (A = the high byte), and `LDM #$FF,$6B`.
- **$CB1F–$CB2C** → the loop in `pros_mark_stress`: `JSR isr_par_ready` with `r`, `INC pros_pos`, `LDX`, `CPX tn_phon_len; BCC`, else `RTS` with A and Y unchanged since the `JSR`.
- **$CB2D–$CB48** → `position`'s start: three zero stores at X, then `pros_symbol`, `pros_class` and `pros_features` from Y = the symbol.
- **$CB4B–$CBAA** → `look_ahead`:
  - `INX; CPX; BCS` comes first in both loops.
  - `CMP #$20` is tested only in the first loop.
  - On a find, the stores are `pros_class`, `$1C09`, `$1C07`, `$1C08`, `$1C06` (first loop), or `pros_class`, `$1C09`, `$1C08` (second loop).
  - `$CB9D` stores $0D into `$1C08` and `$1C06`, and 0 into `$1C09` and `$1C07`.
  - X never passes `tn_phon_len`, so it never wraps.
- **$CBAD–$CC1E** → `marker`:
  - X, A and Y are reloaded at $CBAD–$CBB3.
  - The `CMP` order is `~ | } { `` ` `` x`.
  - `~`: `$1C11` = 1, `$1C19` = 0, `$1C0D` = 0, `$1D00,X` = 8.
  - `|`: `BBS7` on `$1C09` (A = `$1C09` on the `JMP $CB1F`). For the R ($CBDB–$CBEE): `$1900,X`, `pros_symbol`, `TAY`, features, `pros_features`, `TAY`, `LDA pros_symbol`, `JMP $CC27`.
  - `}`: two $FF stores. `{`: one 0 store. `` ` ``: `$1C14` = 1. `x`: `$1C0D` = 0, then on to $CC21.
- **$CC21–$CC24** → `position`: `TYA; BNE $CC27`, else `JMP $CE98`.
- **$CC27–$CC5D** → `stress_toggle`:
  - `BEQ` on `$1C0C`, `BBC6`, `BBS1`;
  - `LDX pros_pos; INX; LDA $1900,X` (no bound), `BBS7` on its features;
  - `EOR #$FF` and `BNE`;
  - `BBC1` on the reloaded `pros_features`, `BNE` on `pros_accent_state`.
- **$CC60–$CCD7** → `pros_stress_counts`:
  - `$CC69–$CC77`: `STA #$40`.
  - `$CC7A`: `$1C11` = 0, then `CLC; ADC #4` into `$1F00,X`.
  - `$CC8D` and `$CC9B`: `CLC`, then `ADC #3` and `ADC #5` into `$1E00,X`.
  - `$CCA9`: `BNE` → `$CCDA`.
  - `$CCAE–$CCB4`: `CLC; ADC #3`; the carry is kept in `c`.
  - `$CCB7` `BEQ` and `$CCBD` `BBC7` → `$CCCA`; else `CLC; ADC #3` and `BRA $CCDA`.
  - `$CCCA` `BBC2`, `$CCD0` `BBC7` on `$1C07`, then `$CCD5` `ADC #$0C` with no `CLC`.
- **$CCDA–$CCDD** → `position`: `TYA; BBS6` decides consonant or vowel. X and Y are as `$CC60/$CC63` loaded them.
- **$CCE0–$CD4F** → `consonant`:
  - `$CCE0`: `BBC5` on Y and on `$1C07`, then `ORA #$10`.
  - `$CCF0`: `AND #$0C`, `BBC7` on `pros_prev_features`, `INX`, `LDA $9BB4,X; AND #7` (by position), and `ADC #8` into `$1F00,X` with X = pos + 1.
  - `$CD0B`: `LDX pros_pos`, `CMP $1C06`, then `$7C` into `$1900,X` and `JMP $CB1F` (no tail).
  - `$CD1E`/`$CD31`: `BBC6`, then `BBS1` on `pros_features`, `+6`.
  - `$CD44`: `+3`.
- **$CD52–$CD5B** → `save_features`: `pros_prev_features` and `$1C1A`, with A = `pros_features` at the `JMP`.
- **$CD5E–$CE95** → `vowel`:
  - `$CD5E–$CD65`: `BEQ`, or `BBC6` on `$1C07`; `INC $1C0B` (8 bits). Then `$1C0C` = 1, and the `x` test.
  - `$CD79–$CD90`: `BNE` on `$1C0D`, `AND #$21`, `BBS0`, `BBC5`, `+5` into `$1F00`.
  - `$CD93`: `+6` into `$1E00`. `$CDA1`: `+4` into `$1F00`.
  - `$CDAF`: `CMP #$31`/`#$4C`.
    - `$CDBA`: `INX ×2`, `$1C08`, `$1C09`, `$1C12` = 1, `INX`.
    - `$CDD2`: `$1C12` = 0, `INX ×2`.
  - `$CDD9`: the unbounded `BBC6` scan with `INX`.
  - `$CDE5`: `AND #1` into `$1C13`; `LDX pros_pos`; `LDY $1C07`.
  - The tree: `BBC5`, `BBC0`, `AND #$11; CMP #$11`, `BBC3`, `LDA $1C07; BNE`. Each leaf is `+1/+4`, `+2/+6`, `+4/+10` or `+1/+6` by `$1C13`, into the array the dump names.
- **$CE98–$CEE9** → `word_end`:
  - `CMP #$20`, then `$1C19` = `$1C11`.
  - `CMP #$3F`, `BEQ` on `$1C14`, `'.'` into `$1900,X`, `$1C14` = 0.
  - `BBC1` on `pros_class` (the look-ahead's), `$1C14` = 0, `$1C19` = 1.
  - `$CECA`: `$1C0B` into `$1F00,X` and `$1E00,X`; 0 into `$1C0B`, `$1C11`, `$1C0C` and `$1C1A`; $FF into `$1C0D` and `pros_accent_state` (A = $FF at the `JMP`).

For the thunk outputs, re-read:
- `rom_CA92_pros_make_segments.txt` ($CAB3 `JSR`, $CAB6 `JSR pros_mark_phrases`);
- `rom_CEF6_pros_mark_phrases.txt` ($CEF6–$CF28 and the exit block $CEEC–$CEF5);
- `rom_D0FD_pros_store_phrase_len.txt` (no Y read);
- `rom_D40D_pros_declination_step.txt` (`LDY #0` at $D41D).

Confirm that no other ROM code calls `$CABD` or jumps into the routine. The dumps' comments name routines in many files, so scan the operands:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xCABD,) for op in (0x20,0x4C)];print([(hex(a),hex(op),hex(m.start())) for a in range(0xCABE,0xCEEC) for op in (0x20,0x4C) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d) if not 0xCABD<=m.start()<0xCEEC])"
```

Expected:

```
0xcabd 0x20 ['0xcab3']
0xcabd 0x4c []
[('0xcac1', '0x4c', '0xc10d'), ('0xcc9d', '0x20', '0xbb53')]
```

The two matches into the interior are operand bytes, not instructions:
- $BB53 lies inside `$BB51 LDA $2000,Y` / `$BB54 STA $16CC,X` (`rom_BB35_par_reset_tracks.txt`);
- $C10D lies inside `$C10C JSR $C14C` / `$C10F DEX` (`rom_C0FF_par_step_tracks.txt`).

Neither target is an instruction start in the dump either: $CAC1 and $CC9D are the operands' high bytes `1C`.

- [ ] **Step 5: Run the tests**

Run, in order:
- `cmd //c ".\build.bat test fw_pros"`
- `cmd //c ".\build.bat test diff_lts"`
- `cmd //c ".\build.bat test golden_hybrid"`
- `cmd //c ".\build.bat test"`

Expected:
- `suite_fw_pros` ok, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`:
  - All 27 routines print `N calls checked` with N > 0.
  - The new line is `pros_mark_stress     N calls checked`, reached once per `pros_make_segments` call (every clause and the boot self-test), so N equals `pros_phrase_gen`'s N:

```
  pros_reset_seg_ptrs  365 calls checked
  pros_mark_stress     207 calls checked
  pros_mark_phrases    207 calls checked
  pros_store_phrase_len 374 calls checked
  pros_phrase_gen      207 calls checked
```

  - `diff_lts` observes the rom engine, where the ROM `pros_make_segments` calls the ROM `pros_mark_stress`, so every other routine's count is unchanged from Task 2.
  - 0 mismatches. `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, both unchanged.
- `golden_hybrid`: `42 corpus files x 28 modes (all, then each routine alone)`, all identical.
- The full run:
  - all 31 suites ok (`31 suite(s), 0 failure(s), 0 skip(s)`), with no warnings;
  - the firmware boundary check passes;
  - `firmware_data` stays at 38197 converted bytes (this task adds no data).

If `diff_lts` reports a mismatch, find the root cause in the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/pros_stress.h src/firmware/pros_stress.c src/firmware/pros_addr.h src/engine/thunks.c tests/test_fw_pros.c
git commit -m "The stress pass in C: pros_mark_stress, one helper per ROM block, with the registers at each isr_par_ready; hook pros_mark_stress (M4.3b 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `pros_make_segments` $CA92 in C, its hook, and `tn_convert_clause`'s flush calling it directly (spec §2.1–§2.4, §4.4, §4.5)

**Files:**
- Create: `src/firmware/pros_make.h`, `src/firmware/pros_make.c`
- Modify:
  - `src/firmware/pros_addr.h` (`ROM_PROS_MAKE_SEGMENTS` moves here);
  - `src/firmware/tn_convert.h` (`ROM_PROS_MAKE_SEGMENTS` removed);
  - `src/firmware/tn_convert.c` (the flush at $B1FF calls the C);
  - `src/engine/thunks.c` (the `pros_make_segments` thunk and its row);
  - `tests/test_fw_pros.c` (the `pros_make_segments` tests);
  - `tests/test_fw_tn.c` (the flush test, its mock and `MAX_CALLS`).

**Where the code goes.** `pros_make_segments` is 15 instructions: five `LDA`/`STA` copies and four `JSR`s. `src/firmware/pros_make.c` has one static helper, `copy_params` ($CA92–$CAAD); `pros_make_segments` itself is the four calls ($CAB0–$CABC), passing one `fw_regs` along as the ROM passes the registers.

**Interfaces:**
- Consumes:
  - M4.3a: `pros_reset_seg_ptrs` (`pros_seg.h`; the ROM leaves A = $0E, X and Y untouched) and `pros_phrase_gen` (`pros_gen.h`; returns A, `*x`, `*y` as the ROM leaves them).
  - Task 2: `pros_mark_phrases(fw *, fw_regs *)` (`pros_phrase.h`). Task 3: `pros_mark_stress(fw *, fw_regs *)` (`pros_stress.h`), `ROM_PROS_MARK_STRESS` in `pros_addr.h`, and `test_stress_count_wrap();` as the last line of `suite_fw_pros`.
  - The harness of `tests/test_fw_pros.c`: `setup()`, `check_call()`, `check_row()`, `seg_state()`, `check_pages()`, `check_segment()`, `GEN_SPEED_MAP`.
  - M4.1: `tn_convert_clause`'s flush at $B1FF (`tn_convert.c`), `ROM_BOOT_LOOP_PLAY`, `TN_PHONETIC_FLUSH`, `FW_ROM_CALL_SPEECH_BUDGET`; `tests/test_fw_tn.c`'s mock host and `test_flush`.
- Produces:

```c
/* pros_addr.h (moved from tn_convert.h) */
#define ROM_PROS_MAKE_SEGMENTS      0xCA92
/* pros_make.h */
void pros_make_segments(fw *f, fw_regs *r);   /* r in: entry registers (only Y is read); r out: exit A, X, Y
                                                 (= pros_phrase_gen's, RTS $CABC) */
```

  The replacement `"pros_make_segments"` at `0xCA92` (outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y`): 28 replacements in all, 29 `golden_hybrid` modes. The C `tn_convert_clause` no longer calls `ROM_PROS_MAKE_SEGMENTS` (spec §4.5 criterion 6).

**ROM facts checked** (`rom_CA92_pros_make_segments.txt`, `rom_CA71_pros_reset_seg_ptrs.txt`, the callers `rom_A61C_boot_reset.txt`, `rom_A79F_boot_selftest_sound.txt`, `rom_A876_cmd_say_phonetic.txt` and `rom_B1E6_tn_convert_clause.txt`, and `rom_A7D4_boot_loop_play.txt`):
- **The copies are in the order C, D, E, A, B**: `pros_params[2]` → `pros_stress_param`, `[3]` → `pros_nucleus_param`, `[4]` → `pros_final_param`, `[0]` → `pros_baseline`, `[1]` → `pros_baseline_end` ($CA92–$CAAD). The order is not visible in RAM; the C keeps it.
- **Only Y of the entry registers is read.** The copies write A; `pros_reset_seg_ptrs` leaves A = $0E and X and Y untouched; `pros_mark_stress` writes A (`LDA #0`, $CABD) and X (`TAX`, $CAEC) before reading them, and its first `isr_par_ready` ($CB1F) gets the entry Y. The Y it leaves reaches `pros_mark_phrases`' first `isr_par_ready` ($CF28). `pros_phrase_gen` reads no register (its first instruction, `JSR pros_declination` at $D129, writes A, X and Y). So the exit registers are `pros_phrase_gen`'s: A = `pros_seg_count`'s low byte before its final `DEC`, X = `pros_pos` (= `tn_phon_len`), Y = the second end pause's.
- **Who reads the exit registers.** `boot_reset` ($A684), `boot_selftest_sound` ($A7B7) and `cmd_say_phonetic` ($A879, unreachable) call `boot_loop_play` next ($A687, $A7BA, $A87C). It loads A from $6B first ($A7D4) and, when that is 0 or `ser_abort` bit 0 is set, returns with X and Y untouched (as M4.3a found for `pros_phrase_gen`, no reader of them was found). No caller reads a flag. A, X, Y are declared.
- **The ROM flush** (`rom_B1E6_tn_convert_clause.txt` $B1FF–$B207): `STX tn_resume_pos`, `JSR pros_make_segments`, `JSR boot_loop_play`, `JMP $B1E9`. So `boot_loop_play` gets `pros_make_segments`' exit registers. The C keeps its `fw_regs r` (A = `tn_phon_len`, X = `tn_pos`, Y = 0, as since M4.1), passes it to the C `pros_make_segments` and hands the registers it leaves to the ROM `boot_loop_play` (spec §2.4). The entry Y (0 in the C) reaches only `isr_par_ready`, which preserves it.
- **Callers by scan:** `JSR $CA92` at $A684, $A7B7, $A879 and $B201 (the ROM `tn_convert_clause`, which the C replaces); no `JMP`; no relative branch (Step 4).
- **`tests/test_fw_tn.c` must change in three places**, not just in `test_flush`:
  - Its mock answered `ROM_PROS_MAKE_SEGMENTS`; the flush now runs the real C passes, so the mock answers only `isr_par_ready`, `boot_loop_play` and `cmd_run_next`. A ROM call of $CA92 would now fail and set `fault`.
  - `MAX_CALLS` 1024 → 2048: the flush test now makes 1375 ROM calls (1374 `isr_par_ready` and `boot_loop_play`, which comes at index 1369). With 1024 the `boot_loop_play` record would be lost, and the loop that checks it would check nothing.
  - It includes `firmware/pros_addr.h` for `ROM_PROS_MAKE_SEGMENTS`.

  Its RAM image needs nothing new: `setup_n` already gives `fw_init` the converted image (`fw_image_get()`), so the passes read `ph_symbol_class`, `ph_symbol_features`, the phoneme lists and the duration tables through `fw_rom8`. The 16 KiB `g_ram` holds every address the passes use (`tn_phonetic` + X ≤ $19FF, the segment arrays, the $1Cxx state). `cmd_speed_map` is 0, so the speed map is read from zero page RAM. The test asserts `!g_f.fault`.
- **The flush's numbers** (`test_flush`): the 250 symbols `A`…`Z` repeated give 369 segments (`pros_seg_count` = 368 = $170), so `boot_loop_play` gets A = $71, X = $FA, Y = $70. `isr_par_ready` is called 1374 times: the conversion 251 + 5, `pros_mark_stress` 251, `pros_mark_phrases` 249, `pros_phrase_gen` 251 + 367 (one per segment but the two end pauses).
- **Proof run** (scratch worktree with Tasks 1–3 applied):
  - `diff_lts`: `pros_make_segments   207 calls checked`, 0 mismatches. That is once per `pros_make_segments` call in the rom engine (the boot self-test, every clause and the mid-clause flush), as for `pros_phrase_gen`.
  - A ROM oracle (scratch only, not part of the plan): `machine_rom_call` of $CA92 with `isr_par_ready` hooked, on the `test_make_segments` RAM (entry registers $11/$22/$33 and $EE/$DD/$CC) and on the flush's 250 symbols. RAM $0000–$00D9 and $0200–$3FFF, every `isr_par_ready` A/X/Y (12, and 1118 for the flush) and the exit A/X/Y (4/3/3, and $71/$FA/$70) matched the C.
  - Mutation run: `test_make_segments` fails when the entry Y does not reach `pros_mark_stress` (`r->y = 0` before it) and when `pros_mark_stress`'s Y does not reach `pros_mark_phrases`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_pros.c`:
- Replace

```c
#include "firmware/pros_gen.h"
```

with

```c
#include "firmware/pros_gen.h"
#include "firmware/pros_make.h"
```

- Replace

```c
void suite_fw_pros(void)
{
```

with

```c
/* ---- pros_make_segments (M4.3b Task 4) ---- */

static void test_make_row(void)
{
    check_row("pros_make_segments", ROM_PROS_MAKE_SEGMENTS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* " EM" (no punctuation, so no pass loads another parameter set): stale state that the copies and
   pros_reset_seg_ptrs replace; the statement's parameters @R A-E falling from $90 to $80 */
static void make_setup(void)
{
    setup();
    memcpy(g_ram + TN_PHONETIC, " EM", 3);
    g_ram[TN_PHON_LEN] = 3;
    for (unsigned i = 0; i < 32; i++)
        g_ram[GEN_SPEED_MAP + i] = (uint8_t)i;
    fw_wr16(&g_f, CMD_SPEED_MAP, GEN_SPEED_MAP);
    static const uint8_t params[5] = { 0x90, 0x80, 7, 9, 6 };   /* A, B: the baseline and its end; C, D, E */
    memcpy(g_ram + PROS_PARAMS, params, sizeof params);
    g_ram[PROS_BASELINE] = g_ram[PROS_BASELINE_END] = 0x5A;
    g_ram[PROS_STRESS_PARAM] = g_ram[PROS_NUCLEUS_PARAM] = g_ram[PROS_FINAL_PARAM] = 0x5A;
    seg_state(0x5A5A, 3);
}

/* The parameter copies, then pros_reset_seg_ptrs, pros_mark_stress, pros_mark_phrases and pros_phrase_gen in that
   order, each pass's first isr_par_ready with the Y the step before left; the registers out are pros_phrase_gen's */
static void test_make_segments(void)
{
    make_setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    pros_make_segments(&g_f, &r);
    CHECK_EQ(g_ncalls, 12);
    check_call(0, ROM_ISR_PAR_READY, 0, 0, 0x33);       /* pros_mark_stress, the start: Y = the entry Y */
    check_call(1, ROM_ISR_PAR_READY, 0xFF, 0, 0);       /* after the space */
    check_call(2, ROM_ISR_PAR_READY, 0x83, 1, 0x49);    /* after E: Y = the next symbol's features */
    check_call(3, ROM_ISR_PAR_READY, 0x49, 2, 0x49);    /* after M; it leaves A = Y = $49, X = 3 */
    check_call(4, ROM_ISR_PAR_READY, 0, 2, 0x49);       /* pros_mark_phrases, the first: Y = pros_mark_stress's */
    check_call(5, ROM_ISR_PAR_READY, 0x49, 1, 0x49);    /* after M; it leaves A = 2, X = 0, Y = E's features */
    check_call(6, ROM_ISR_PAR_READY, 0xFF, 0, 5);       /* pros_phrase_gen, the first: X and Y as its
                                                           pros_declination leaves them ($10 over the count 3) */
    check_call(7, ROM_ISR_PAR_READY, 0, 0, 0);
    check_call(8, ROM_ISR_PAR_READY, 0x13, 0x61, 0x13); /* E's segment */
    check_call(9, ROM_ISR_PAR_READY, 0xFF, 0x61, 3);
    check_call(10, ROM_ISR_PAR_READY, 0x0A, 0x12, 0x0A);   /* M's segment */
    check_call(11, ROM_ISR_PAR_READY, 0xFF, 0x12, 3);   /* the end */
    CHECK_EQ(r.a, 4);                                   /* pros_phrase_gen's A, X and Y */
    CHECK_EQ(r.x, 3);
    CHECK_EQ(r.y, 3);
    CHECK_EQ(g_ram[PROS_STRESS_PARAM], 7);              /* the copies of C, D and E (no punctuation reloads them) */
    CHECK_EQ(g_ram[PROS_NUCLEUS_PARAM], 9);
    CHECK_EQ(g_ram[PROS_FINAL_PARAM], 6);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x80);           /* of B (falling: no swap) */
    CHECK_EQ(g_ram[PROS_BASELINE], 0x90 - 2 * 5);       /* of A, less the step at E and at M */
    CHECK_EQ(g_ram[PROS_DECL_STEP], 5);
    /* pros_mark_stress: E stressed ($40), E and M lengthened by 4, E shortened by 1 (before a nasal), M by 3 (after a
       vowel in the word); pros_mark_phrases: the word's count 2 at the space, E's accent ($60, + 8) and the nucleus
       marks (7), E shortened by 7, the phrase's count 3 at 0 with bit 7 */
    static const uint8_t flags[3] = { 2, 0x67, 0x07 };
    static const uint8_t lengthen[3] = { 0x83, 4 + 8, 4 };
    static const uint8_t shorten[3] = { 0x83, 1 + 7, 3 };
    for (unsigned i = 0; i < 3; i++) {
        CHECK_EQ(g_ram[PROS_SYM_FLAGS + i], flags[i]);
        CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + i], lengthen[i]);
        CHECK_EQ(g_ram[PROS_SYM_SHORTEN + i], shorten[i]);
    }
    check_pages(0);                                     /* pros_reset_seg_ptrs replaced the stale pointers */
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 3);
    check_segment(0, 0x10, 0x13, 0x84);                 /* E */
    check_segment(1, 0x2A, 0x0A, 0x80);                 /* M */
    check_segment(2, 0x3B, 4, 0x80);                    /* the end pauses */
    check_segment(3, 0x3B, 0x80, 0x80);
    CHECK(!g_f.fault);
}

void suite_fw_pros(void)
{
```

- Replace

```c
    test_stress_count_wrap();
}
```

with

```c
    test_stress_count_wrap();
    test_make_row();
    test_make_segments();
}
```

In `tests/test_fw_tn.c`:
- Replace

```c
#include "firmware/cmd_addr.h"
#include "firmware/tn_convert.h"
```

with

```c
#include "firmware/cmd_addr.h"
#include "firmware/pros_addr.h"
#include "firmware/tn_convert.h"
```

- Replace

```c
#define MAX_CALLS 1024
```

with

```c
#define MAX_CALLS 2048
```

- Replace

```c
    if (addr == ROM_ISR_PAR_READY || addr == ROM_PROS_MAKE_SEGMENTS || addr == ROM_BOOT_LOOP_PLAY)
```

with

```c
    if (addr == ROM_ISR_PAR_READY || addr == ROM_BOOT_LOOP_PLAY)
```

- Replace

```c
static void test_flush(void)
{
    char text[255];
    for (int i = 0; i < 255; i++)
        text[i] = (char)('A' + i % 26);
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_f.tn_flushes, 1);
    CHECK_EQ(g_ram[TN_RESUME_POS], 250);
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);            /* the second pass converted text[250..254] */
    CHECK(memcmp(g_ram + TN_PHONETIC, text + 250, 5) == 0);
    CHECK_EQ(g_ram[TN_POS], 255);
    CHECK_EQ(count(ROM_PROS_MAKE_SEGMENTS), 1);
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 1);
    for (unsigned i = 0; i + 1 < g_ncalls && i + 1 < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_PROS_MAKE_SEGMENTS)
            continue;
        CHECK_EQ(g_calls[i].a, TN_PHONETIC_FLUSH);   /* A = tn_phon_len, X = tn_pos, as at $B201 */
        CHECK_EQ(g_calls[i].x, 250);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_SPEECH_BUDGET);
        CHECK_EQ(g_calls[i + 1].addr, ROM_BOOT_LOOP_PLAY);
        CHECK_EQ(g_calls[i + 1].budget, FW_ROM_CALL_SPEECH_BUDGET);
    }
```

with

```c
/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls (the strict budget), and the ROM boot_loop_play gets the registers it leaves, as the ROM's
   would; then the conversion goes on from tn_resume_pos with an empty buffer. */
static void test_flush(void)
{
    char text[255];
    for (int i = 0; i < 255; i++)
        text[i] = (char)('A' + i % 26);
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_f.tn_flushes, 1);
    CHECK_EQ(g_ram[TN_RESUME_POS], 250);
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);            /* the second pass converted text[250..254] */
    CHECK(memcmp(g_ram + TN_PHONETIC, text + 250, 5) == 0);
    CHECK_EQ(g_ram[TN_POS], 255);
    CHECK_EQ(count(ROM_PROS_MAKE_SEGMENTS), 0);           /* the C pros_make_segments, not the ROM's */
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 1);
    CHECK_EQ(g_ram[PROS_POS], TN_PHONETIC_FLUSH);         /* pros_phrase_gen walked the 250 symbols ... */
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 368);         /* ... into 369 segments (the index of the last) */
    /* the conversion 251 + 5 (one per character, the flush's included), pros_mark_stress 251 (the start and one per
       position), pros_mark_phrases 249 (positions 249 to 1), pros_phrase_gen 251 (positions 0 to 250) + 367 (one per
       segment but the two end pauses) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1374);
    CHECK(g_ncalls <= MAX_CALLS);
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_BOOT_LOOP_PLAY)
            continue;
        CHECK_EQ(g_calls[i].a, 0x71);                     /* pros_phrase_gen's A: pros_seg_count's low byte before
                                                             its DEC, */
        CHECK_EQ(g_calls[i].x, TN_PHONETIC_FLUSH);        /* X = pros_pos (tn_phon_len), */
        CHECK_EQ(g_calls[i].y, 0x70);                     /* Y = the second end pause's: as the ROM leaves them */
        CHECK_EQ(g_calls[i].phon_len, TN_PHONETIC_FLUSH);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_SPEECH_BUDGET);
        CHECK_EQ(g_ncalls - i, 6);                        /* then isr_par_ready at text[250..254] */
    }
```

What the tests pin (spec §4.4, the part for this task):
- `test_make_row`: the hook's row.
- `test_make_segments`: the parameter copies (over stale values), the pointers reset, then the four steps in order, through every `isr_par_ready` call with its registers: the entry Y at `pros_mark_stress`' first, `pros_mark_stress`'s Y at `pros_mark_phrases`' first, `pros_declination`'s X and Y at `pros_phrase_gen`'s first; the exit registers are `pros_phrase_gen`'s.
- `test_flush` (`tests/test_fw_tn.c`): the flush runs the C `pros_make_segments` (no ROM call of $CA92, the 250 symbols' segments, every `isr_par_ready` of the three passes with the strict budget), then exactly one `boot_loop_play`, with the speech budget and `pros_phrase_gen`'s exit registers; then the conversion goes on.

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_pros"`
Expected: a compile error, because `firmware/pros_make.h` does not exist.

- [ ] **Step 3: Implement**

Create `src/firmware/pros_make.h`:

```c
#ifndef FW_PROS_MAKE_H
#define FW_PROS_MAKE_H
/* The prosody of tn_phonetic (ROM $CA92-$CABC, pros_make_segments), decompiled (M4.3b spec 2.1, 2.2). */
#include "fw.h"

/* $CA92: the statement's phrase parameters pros_params[0..4] (@R A-E) into pros_stress_param, pros_nucleus_param,
   pros_final_param, pros_baseline and pros_baseline_end (C, D, E, A, B: the ROM's order); then pros_reset_seg_ptrs,
   pros_mark_stress, pros_mark_phrases and pros_phrase_gen, all four C ("C calls C"), with the registers passed along
   as the ROM has them.
   r in: the registers at entry; only Y is read, by pros_mark_stress's first isr_par_ready ($CB1F; A is written by
   the copies and X by its TAX at $CAEC). r out: the A, X and Y the ROM leaves at its RTS ($CABC), which are
   pros_phrase_gen's. r->p is not used. */
void pros_make_segments(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/pros_make.c`:

```c
#include "pros_make.h"
#include "pros_gen.h"
#include "pros_phrase.h"
#include "pros_seg.h"
#include "pros_stress.h"

/* $CA92-$CAAD: LDA pros_params[i]; STA, the phrase parameters of a statement (@R A-E), in the ROM's order. */
static void copy_params(fw *f)
{
    static const struct { uint16_t to; uint8_t index; } copies[] = {
        { PROS_STRESS_PARAM, 2 }, { PROS_NUCLEUS_PARAM, 3 }, { PROS_FINAL_PARAM, 4 },
        { PROS_BASELINE, 0 }, { PROS_BASELINE_END, 1 },
    };
    for (unsigned i = 0; i < sizeof copies / sizeof copies[0]; i++)
        fw_wr8(f, copies[i].to, fw_rd8(f, (uint16_t)(PROS_PARAMS + copies[i].index)));
}

void pros_make_segments(fw *f, fw_regs *r)
{
    copy_params(f);
    pros_reset_seg_ptrs(f);                             /* $CAB0 */
    r->a = (uint8_t)(PROS_SEG_FLAGS >> 8);              /* its last LDA #$0E; X and Y untouched */
    pros_mark_stress(f, r);                             /* $CAB3: the entry Y reaches its first isr_par_ready */
    pros_mark_phrases(f, r);                            /* $CAB6: pros_mark_stress's Y reaches its first one */
    r->a = pros_phrase_gen(f, &r->x, &r->y);            /* $CAB9 (it reads no register), then RTS $CABC */
}
```

`src/firmware/pros_addr.h`: replace

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
```

with

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
#define ROM_PROS_MAKE_SEGMENTS      0xCA92   /* called by boot_reset $A684, boot_selftest_sound $A7B7 and
                                                cmd_say_phonetic $A879; the C tn_convert_clause calls the C */
```

(The list stays in address order: $CA71, $CA92, then Task 3's $CABD.)

`src/firmware/tn_convert.h`: replace

```c
#define ROM_ISR_PAR_READY       0xC5D6   /* parallel mode: ready for the next host byte; A, X, Y preserved */
#define ROM_PROS_MAKE_SEGMENTS  0xCA92   /* prosody of tn_phonetic */
#define ROM_BOOT_LOOP_PLAY      0xA7D4   /* play the utterance */
```

with

```c
#define ROM_ISR_PAR_READY       0xC5D6   /* parallel mode: ready for the next host byte; A, X, Y preserved */
#define ROM_BOOT_LOOP_PLAY      0xA7D4   /* play the utterance */
```

`src/firmware/tn_convert.c`:
- Replace

```c
#include "lts.h"
#include "tn_rules.h"
```

with

```c
#include "lts.h"
#include "pros_make.h"
#include "tn_rules.h"
```

- Replace

```c
        /* $B1FF: speak tn_phonetic, then convert on from here with an empty buffer */
        fw_wr8(f, TN_RESUME_POS, x);
        fw_regs r = { fw_rd8(f, TN_PHON_LEN), x, 0, 0 };
        fw_rom_call(f, ROM_PROS_MAKE_SEGMENTS, &r, FW_ROM_CALL_SPEECH_BUDGET);
        fw_rom_call(f, ROM_BOOT_LOOP_PLAY, &r, FW_ROM_CALL_SPEECH_BUDGET);
```

with

```c
        /* $B1FF: speak tn_phonetic, then convert on from here with an empty buffer. The C pros_make_segments
           (M4.3b) leaves in r what the ROM's leaves (pros_phrase_gen's A, X and Y) for boot_loop_play. */
        fw_wr8(f, TN_RESUME_POS, x);
        fw_regs r = { fw_rd8(f, TN_PHON_LEN), x, 0, 0 };
        pros_make_segments(f, &r);
        fw_rom_call(f, ROM_BOOT_LOOP_PLAY, &r, FW_ROM_CALL_SPEECH_BUDGET);
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/pros_gen.h"
```

with

```c
#include "firmware/pros_gen.h"
#include "firmware/pros_make.h"
```

- Replace

```c
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}
```

with

```c
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}

/* pros_make_segments $CA92: A, X, Y in; only Y is read, by pros_mark_stress's first isr_par_ready ($CB1F; A is
   written by the parameter copies, X by pros_mark_stress's TAX at $CAEC). A, X and Y out as pros_phrase_gen leaves
   them (RTS at $CABC). The C's ROM calls overwrite the machine's registers, so all three are set from the C. Its
   ROM callers, boot_reset ($A684), boot_selftest_sound ($A7B7) and cmd_say_phonetic ($A879, unreachable), call
   boot_loop_play next ($A687, $A7BA, $A87C), which loads A from $6B first ($A7D4) and, when that is 0 or the host
   aborted, returns with X and Y untouched; the C tn_convert_clause hands them to boot_loop_play the same way. No
   reader of X or Y was found; no caller reads a flag. They are defined, so all three are declared. */
static void pros_make_segments_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_make_segments(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace

```c
THUNK(pros_reset_seg_ptrs)
```

with

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_make_segments)
```

- Replace

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
```

with

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_make_segments", 0xCA92, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_make_segments },
```

The table stays in address order ($CA71, $CA92, $CABD, …), with 28 rows (`DIFF_MAX` is 28 since Task 2). `pros_make.c` clashes with no existing basename.

- [ ] **Step 4: Faithfulness check against the dumps**

Read `build/ghidra740/dump/rom_CA92_pros_make_segments.txt` side by side with `pros_make.c`:
- $CA92–$CAAD → `copy_params`: five `LDA pros_params[i]` / `STA` pairs in the order of `copies[]` (`[2]` → `pros_stress_param`, `[3]` → `pros_nucleus_param`, `[4]` → `pros_final_param`, `[0]` → `pros_baseline`, `[1]` → `pros_baseline_end`).
- $CAB0 `JSR pros_reset_seg_ptrs` → the C call; `rom_CA71_pros_reset_seg_ptrs.txt` ends with `LDA #$0E; STA pros_flags_ptr+1; RTS` → `r->a = $0E`, X and Y untouched.
- $CAB3, $CAB6, $CAB9 → `pros_mark_stress`, `pros_mark_phrases`, `pros_phrase_gen`, in that order, each with the registers the step before left; $CABC `RTS` → the return.

For the flush, read `rom_B1E6_tn_convert_clause.txt` $B1FF–$B207 (`STX`, `JSR $CA92`, `JSR $A7D4`). For the thunk outputs, read `rom_A61C_boot_reset.txt` ($A684–$A68A), `rom_A79F_boot_selftest_sound.txt` ($A7B7–$A7BD), `rom_A876_cmd_say_phonetic.txt` ($A879–$A87F) and `rom_A7D4_boot_loop_play.txt` ($A7D4–$A7DE).

Confirm the callers by scanning the operands (the dumps' comments name `pros_make_segments` in many files):

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xCA92,) for op in (0x20,0x4C)]"
```

Expected:

```
0xca92 0x20 ['0xa684', '0xa7b7', '0xa879', '0xb201']
0xca92 0x4c []
```

and no relative branch reaches it:

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xCA92,):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected: `0xca92 []`.

Then confirm that only the address list names `ROM_PROS_MAKE_SEGMENTS` in `src/` (spec §4.5 criterion 6):

```bash
grep -rn "ROM_PROS_MAKE_SEGMENTS" src
```

Expected: one match, `src/firmware/pros_addr.h` (the `#define`).

- [ ] **Step 5: Run the tests**

Run, in order:
- `cmd //c ".\build.bat test fw_pros"`
- `cmd //c ".\build.bat test fw_tn"`
- `cmd //c ".\build.bat test diff_lts"`
- `cmd //c ".\build.bat test golden_hybrid"`
- `cmd //c ".\build.bat test"`

Expected:
- `suite_fw_pros` ok, then `1 suite(s), 0 failure(s), 0 skip(s)`; the same for `suite_fw_tn`.
- `diff_lts`:
  - All 28 routines print `N calls checked` with N > 0. The new line comes between `pros_reset_seg_ptrs` and `pros_mark_stress`:

```
  pros_reset_seg_ptrs  365 calls checked
  pros_make_segments   207 calls checked
  pros_mark_stress     207 calls checked
  pros_mark_phrases    207 calls checked
  pros_store_phrase_len 374 calls checked
  pros_phrase_gen      207 calls checked
```

  - `pros_make_segments` equals `pros_phrase_gen` (once per call in the rom engine: the boot self-test, every clause and the flush). Every other count is unchanged from Task 3.
  - 0 mismatches. `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, both unchanged.
- `golden_hybrid`: `42 corpus files x 29 modes (all, then each routine alone)`, all identical.
- The full run: all 31 suites ok (`31 suite(s), 0 failure(s), 0 skip(s)`), with no warnings, the firmware boundary check passing, and `firmware_data` still at `38197 converted bytes compared with the ROM, 0 differ`.

If `diff_lts` reports a mismatch, find the root cause in the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

Then confirm spec criterion 5: the CLI's hybrid output equals the rom engine's for every new text. `long_clause` is added because this task changes its mid-clause flush. Each text is read from its corpus file; the outputs go to a scratch directory outside the repo, always with `-o`:

```bash
SP="$(mktemp -d)"
for t in long_words linking_r syllabic_stress long_clause; do
  ./build/apollo2.exe --engine rom --text-file tests/corpus/$t.txt -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file tests/corpus/$t.txt -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done
rm -rf "$SP"
```

Expected: four lines, `long_words same`, `linking_r same`, `syllabic_stress same`, `long_clause same`, with no `failed` or `DIFFERS` line.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/pros_make.h src/firmware/pros_make.c src/firmware/pros_addr.h src/firmware/tn_convert.h src/firmware/tn_convert.c src/engine/thunks.c tests/test_fw_pros.c tests/test_fw_tn.c
git commit -m "pros_make_segments in C, calling the C passes and the C phrase generator; hook pros_make_segments; tn_convert_clause's flush calls the C directly (M4.3b 2.2, 2.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files (none of this task's).

---

### Task 5: Parent spec rev 11, M4.3b status and README (criterion 8)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 11**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 11)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 11)* Revision 11 records milestone 4.3b: the stress and phrase passes and pros_make_segments in C, so all of prosody before playback runs as C (design: docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.3a done**`). Replace its last sentence, `Next: M4.3b, the stress and phrase passes and pros_make_segments.`, with:

   `*(rev 11)* **M4.3b done** <DATE>: the stress and phrase passes run as C (pros_make_segments, pros_mark_stress, pros_mark_phrases and pros_store_phrase_len; 28 routines hooked in all), and the C tn_convert_clause calls the C pros_make_segments directly; three new corpus goldens (rom engine) reach the linking R, words of five or more syllables, a stressed syllabic consonant at the nucleus and a branch the design thought unreachable ($CCBA); diff_lts and golden_hybrid (42 files x 29 modes) pass. Next: M4.4, the parameter tracks (playback).`

   Replace `<DATE>` with the date of your commit (`date +%F`).

- [ ] **Step 2: M4.3b spec status and As built**

In `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-04-m4-3b-stress-and-phrase-passes.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the ROM is more precise than, or differs from, the sections above. The ROM wins; ``ghidra740/`` is not edited.` (one pair of backquotes around `ghidra740/`). Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–4 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the thunk outputs as finally declared for the 4 new hooks (read the rows and comments in `src/engine/thunks.c`) and why;
  - the C file layout (`pros_phrase.c`, `pros_stress.c`, `pros_make.c`, `pros_flags.h`, the `pros_addr.h` additions) and the static helpers of `pros_mark_stress` and `pros_mark_phrases`;
  - the registers at `isr_par_ready` $CB1F and $CF28 per path;
  - the RAM names actually added to `fw.h`, if they differ from §2.6;
- the corpus texts as finally committed, if any differs from §4.1.

Every bullet must be true of the committed code and the dumps. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md`.

**Known spec differences to record in As built** (found while drafting and proving this plan; each is stated in a task's "ROM facts checked" and holds for the committed code; check each against the code and the dumps again, word it as one or two bullets, and add any other that a task records):
1. **`BEQ $CCBA` taken is reachable** (§4.4 says unreachable). In `A|A` the first `A`'s look-ahead skips the class-0 `|` and sees a vowel, so no syllable is counted while `$1C0C` is set; the `|` becomes `R`, the stress toggles to 0 and clears the accent state, and `$CCB7` finds `$1C0B` = 0 (Task 3). It is covered in the corpus: `syllabic_stress` is `@X1. Bx7 BxM Bx7Z. BIA DIO BEA. A|A. @X0`, with ` A|A.` added to §4.1's text (Task 1; the probe gives $CCBA taken 2, and the text still reaches $CC59 taken and $D0CD); and by `test_stress_no_syllable`.
2. **The carry into `$CCD5`**: the count that the `ADC #3` at $CCB2 adds to is at most 8 (+3 at $CC96, +5 at $CCA4; at most 11 after the add, which §4.4 loosely calls "at most 11 at $CCB2"), so it never carries and the missing `CLC` cannot show in `pros_mark_stress`. The test runs the block $CC60–$CCD7 alone through `pros_stress_counts`, a public function of `pros_stress.h` beyond the signatures of §2.2 (Task 3).
3. **`pros_mark_phrases`' exit registers are not read by `pros_phrase_gen`** (§2.3's row says they flow into its first `isr_par_ready` at $D155): its first instruction, `JSR pros_declination` at $D129, writes A, X and Y before any read, so $D155 gets `pros_declination`'s X and Y. Likewise `pros_store_phrase_len`'s X = `pros_pos` is not what `pros_declination` sees (`pros_declination_step` loads X itself, $D40D). All rows still declare A, X, Y, as the ROM leaves them defined (Task 2).
4. **RAM names and meanings** (§2.6): $1C15 is `FW_PROS_PUNCT_NOT_KIND3` (the punctuation `pros_store_phrase_len` stored last, the next one in the text, had no class bit 3; bit 3 is set for `.` `:` `;` `?` only). Corrected meanings: $1C0B in `pros_mark_phrases` is the syllable count of the word before the space or punctuation in the text, the one the backward walk enters next (not "the following word's"); $1C13 is "the vowel's syllable ends a phrase" in `pros_mark_stress` and "in the word's last syllable" in `pros_mark_phrases` (not "next is punctuation / phrase-final"); $1C19 lengthens each symbol by 4 in `pros_mark_stress` (1 at the start and at word ends but a space, 0 at `~`, a space copies $1C11) and is the pending accent in `pros_mark_phrases`; $1C06 becomes $0D when the look-ahead reaches `tn_phon_len`, even after it stored a space. Also: `pros_mark_phrases` writes $1C0D but never reads it; $1C1A is cleared at each word end, unlike `pros_prev_features` (Task 2, `fw.h`).
5. **`DIFF_MAX`** in `tests/test_diff_lts.c` was raised from 24 to 28 (Task 2); with 26 rows `diff_lts` would have failed `fw_replacement_count <= DIFF_MAX`. §2.3's hook budget names only `MACHINE_MAX_HOOKS` (29 of 32 used).
6. **`$CD02` lengthens position pos + 1**: after the `INX` at $CCFA both the class read ($CCFB, `ph_symbol_class[pos + 1]`, which §2.7 mentions) and the `ADC #8` into `pros_sym_lengthen` go to pos + 1. The next position clears that byte, so the add survives only at pos + 1 = `tn_phon_len`, on a stale byte past the text (Task 3, `test_stress_count_wrap`).
7. **`pros_store_phrase_len`'s `ORA #$80` is in A only**: the stores get bit 7, but RAM `pros_phrase_len` becomes 0 (the decompiler's `pros_phrase_len | 0x80` is wrong). Its count then goes on from the next phrase's, because `pros_declination_step` stores `pros_sym_lengthen[pos] & $7F` back into `pros_phrase_len` (Task 2, `test_phrases_clause`).
8. **Smaller sharpenings** (Tasks 2–4): the "first of two equal symbols" rewrite ($CD18) happens only to consonants; `pros_mark_stress`'s look-ahead overwrites `pros_class` with the next symbol's class, so the `BBC1` at $CEBB tests the next symbol; `pros_mark_phrases` never sets `pros_pos` or $1C15 (it starts from `pros_mark_stress`'s `pros_pos`); of `pros_make_segments`' entry registers only Y is read; `tests/test_fw_tn.c`'s flush test now checks that `boot_loop_play` gets `pros_phrase_gen`'s exit registers, and its `MAX_CALLS` went from 1024 to 2048 because the C passes' `isr_par_ready` calls are recorded; the CLI check of criterion 5 also covered `long_clause`, whose mid-clause flush Task 4 changed.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
and every deferred command handler (M4.2), and the segment generator: phoneme expansion, the phrase generator,
the segment appenders and the declination (M4.3a); the rest still runs on the emulated CPU.
```

with:

```
and every deferred command handler (M4.2), the segment generator: phoneme expansion, the phrase generator,
the segment appenders and the declination (M4.3a), and the stress and phrase passes with pros_make_segments
(M4.3b), so all of prosody before playback; the rest still runs on the emulated CPU.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all 31 suites ok.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md README.md
git commit -m "Spec rev 11: milestone 4.3b done (the stress and phrase passes in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
