# M4.3a The Segment Generator in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the segment generator — phoneme expansion, the phrase generator, the segment appenders and the declination — to C and prove it against the ROM, live and with the goldens.

**Architecture:**
- **Corpus.** Seven new corpus texts with rom-engine goldens reach the phrase pauses, the rising and level baselines and the 16-bit segment-index page wraps (Task 1).
- **Data.** The phoneme and prosody tables (2,319 bytes) become readable C (Task 2).
- **New C files.** `src/firmware/pros_seg.c` (the appenders), `ph_expand.c` (`ph_lengthen_count`), `pros_decl.c` (the declination) and `pros_gen.c` (`pros_phrase_gen` with the phoneme decoder) run on the firmware's RAM image through the `fw` context. Inside C, `pros_phrase_gen` calls the C leaves directly; its only ROM call is `isr_par_ready`.
- **Hooks.** The three entries that ROM code still calls (`pros_phrase_gen`, `pros_declination`, `pros_reset_seg_ptrs`) and the five leaves, giving 24 replacements and 25 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`) and Python 3 (`tools/extract_tables.py`).

**Spec:** `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md` (M4.3a spec). Its foundations are:
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built");
- the M4.2 spec, `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md` (§7 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 9).

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

**Correctness rules**
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The seven new goldens are generated once by the rom engine in Task 1. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.3a spec §2.7):
  - every RAM write, scratch included (`$78`, every `$1Cxx` byte, both bytes of `pros_seg_count`, the four page pointers in the ROM's order);
  - the pitch-target carry chain, the 8-bit wraps and the 256-step loops;
  - reads past ends (the event queue);
  - every `isr_par_ready` call at the ROM's point with the ROM's registers.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts` excludes it.
- ROM data is read only through `fw_rom8` at its ROM address, from the converted image.
- Thunk outputs: declare exactly what the ROM callers read after the call, checked in the dumps and stated in a comment; when in doubt, declare more.
- `diff_lts` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's standing rule and overrides any other attribution line the environment suggests.

## File Map

| File | Task | Responsibility |
|---|---|---|
| `tests/corpus/phrase_pauses.txt`, `baseline.txt`, `phonetic_stress.txt`, `phrase_127.txt`, `seg_256.txt`, `seg_wrap_phrase.txt`, `seg_wrap_word.txt`, their goldens `tests/golden/<name>.frm`, `tests/corpus_list.h` | 1 | seven corpus texts with rom-engine goldens that reach the segment generator's rare paths |
| `src/firmware/data/pros_data.c` (new), `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tools/extract_tables.py`, `tests/test_firmware_data.c` | 2 | the prosody tables (2,319 bytes) as readable C: names and bits, the generator, encoder step 8, the byte count 38,197 |
| `src/firmware/pros_addr.h` (new) | 3 | the ROM code addresses of the segment generator |
| `src/firmware/pros_seg.h`, `src/firmware/pros_seg.c` (new) | 3 | the segment appenders: `pros_reset_seg_ptrs`, `pros_append_segment`, `ph_append_pause`, `ph_append_phrase_pause` |
| `src/engine/thunks.c` | 3, 4, 5 | the eight hooks (register mappings, `THUNK` lines, table rows in address order) |
| `tests/test_fw_pros.c` (new), `tests/suites.h` | 3 (4, 5 extend the file) | the unit tests of the segment generator and their one mock-host harness; `SUITE(suite_fw_pros)` |
| `src/firmware/ph_expand.h`, `src/firmware/ph_expand.c` (new) | 4 | `ph_lengthen_count` |
| `src/firmware/pros_decl.h`, `src/firmware/pros_decl.c` (new) | 4 | `pros_declination`, `pros_declination_step`, and `pros_declination_y` (the Y the ROM leaves) |
| `src/firmware/fw.h` | 5 | the RAM names `FW_UTT_CONTENT`, `FW_PROS_STRESS_TOGGLE`, `FW_PROS_WORD_U` |
| `src/firmware/pros_gen.h`, `src/firmware/pros_gen.c` (new) | 5 | `pros_phrase_gen` with the phoneme decoder, calling the C leaves directly |
| `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md`, `README.md` | 6 | parent spec rev 10, M4.3a status and "As built", README |

---

### Task 1: Corpus: seven new texts and their rom-engine goldens (spec §4.1)

**ROM facts checked / what I verified while drafting** (scratch worktree at df9f720, ROM engine only):
- **How the corpus is fed.** `suite_golden` (`tests/test_golden.c`), `suite_golden_hybrid`, `diff_lts` and `apollo2_coverage` all read the *whole file* as one string, strip trailing CR/LF, and speak it on a fresh machine without boot. So one line with a trailing newline is the same as no newline; a file with several sentences is one input stream, whose clauses share state (`@Q9`, `@X1`, `@D`).
- **Every target was reached in the real feed** (each file run alone through the branch probe `brprobe.c` from the survey, which feeds files exactly as above; then all seven confirmed together with `apollo2_coverage`):

| File | Target | Probe result (this file alone) |
|---|---|---|
| `phrase_pauses` | `$C9EC` (`BNE $C9EA` falls) | `$C9EA` taken 6, fall 16 |
| | `BEQ $D1D8` taken (@D0 at punctuation) | taken 2, fall 11 |
| `baseline` | `$D3E5` reached (`BCC $D3E3` falls) and both ways from there | `$D3E3` taken 2 / fall 9; `$D3E5` taken 3 (level) / fall 6 (rising, `$D3E7`–`$D40C`) |
| `phonetic_stress` | `BEQ $D300` both ways; `$D28F` | `$D300` taken 2, fall 8; `$D28F` executed (coverage) |
| `phrase_127` | `$D41A` (`BNE $D418` falls) | fall 1 |
| `seg_256` | `$D19B` (`BNE $D199` falls: exactly 256 segments) | fall 1 (index 255 at the RTS) |
| `seg_wrap_phrase` | `$CA07` (`BNE $CA05` falls) | fall 1 (258 segments) |
| `seg_wrap_word` | `$CA2F` and `$CA62` | `$CA2D` fall 1, `$CA60` fall 1 (257 and 260 segments) |

- **Two candidates from the survey missed and were retuned:**
  - `phrase_127`: the probe file in the scratchpad (`wrap/p.txt`, 16 groups of `BABABABA` = 128 letters) never reaches `$D41A`. A brute force over 110–149 letters, with and without spaces, hits `$D41A` only at **exactly 127 letters** (spaces between the groups do not count). The final text is the survey's description: 15 groups of `BABABABA` and one `BABABAB`, separated by spaces.
  - `seg_wrap_word`: the survey's single clause (`@Q9 ` + 55 words + `.`) wraps at a word pause (`$CA2F`) but **never at a segment** (`$CA60` never falls). A sweep of 52–57 words × 5 endings (`.`, ` oh.`, ` go.`, ` to.`, ` it.`) shows that `@Q9 ` + 55 words + ` oh.` wraps at a segment (so do 55 + ` go.`/` to.`/` it.` and every 56- and 57-word variant). One clause can wrap only once (fewer than 512 segments), so the final file holds **two clauses**: the survey's clause, then `@Q9 ` + the same 55 words + ` oh.`. Repeating `@Q9 ` in the second clause matters: without it the second clause wraps at a phrase pause (`$CA07`) instead of at a segment.
- `apollo2_coverage` records only executed instruction addresses, not branch directions. Three targets are directions whose branch target is also reached another way, so the coverage file cannot show them: `BEQ $D1D8` taken (`$D1E0` is also the target of `BEQ $D1CC`, a space); `BEQ $D300` taken (`$D312` is also reached by `JMP $D312` at `$D30C`); `BEQ $D3E5` taken, the level baseline (`$D40D` is also reached by `BCC $D3E3` and by `JSR $D40D`). Those three were confirmed with the probe (table above). Step 5 checks every target that is an address.
- Before this task, the ten addresses of Step 5 are absent from the coverage of boot plus the 31 texts (checked: all `MISSING`; `rom 3968` addresses). After it, all are present (`rom 4040`).
- Segment counts are count-sensitive: the three `seg_*` texts are kept as separate files, as the spec says. Don't edit them, not even whitespace.
- Timing (this machine, `build/apollo2_tests.exe golden_hybrid`): about 44 s before this task, about 78 s after (the seven texts add 9,251 rom frames to the corpus; `seg_wrap_word` alone is 3,554). At 25 modes (Task 5) expect roughly 115 s. The whole `build.bat test` took about 1 min 45 s after the task; `diff_lts` about 7 s.

**Files:**
- Create: `tests/corpus/phrase_pauses.txt`, `tests/corpus/baseline.txt`, `tests/corpus/phonetic_stress.txt`, `tests/corpus/phrase_127.txt`, `tests/corpus/seg_256.txt`, `tests/corpus/seg_wrap_phrase.txt`, `tests/corpus/seg_wrap_word.txt`, and their seven goldens `tests/golden/<name>.frm` (generated)
- Modify: `tests/corpus_list.h`

**Interfaces:**
- Consumes: nothing.
- Produces: the corpus names `"phrase_pauses"`, `"baseline"`, `"phonetic_stress"`, `"phrase_127"`, `"seg_256"`, `"seg_wrap_phrase"` and `"seg_wrap_word"`. Every later task's `golden_hybrid` and `diff_lts` runs over them. Task 5's CLI check (`--engine hybrid` equals `--engine rom`) uses the seven files.

- [ ] **Step 1: Write the corpus texts**

Each file holds exactly one line, ending with a single LF newline. Write them with the script below, so the long texts are exact (the cycle is the seven words `a go i to it o at`, repeated; "the first n words" means words 0 … n−1 of that repetition):

```bash
python - <<'EOF'
cyc = "a go i to it o at".split()
w = lambda n: " ".join(cyc[i % 7] for i in range(n))
t = {
    "phrase_pauses": '@D1 Yes, no (maybe) - fine. @D3 "Quote" [one]. @D0 Stop. Go!',
    "baseline": "@RA40 @RBC0 Rising tone. @RF20 @RG90 Up, again. @RA80 @RB80 Level tone.",
    "phonetic_stress": "@X1 K}AE{T 8 X M}AE`T?@X0",
    "phrase_127": "@X1. " + "BABABABA " * 15 + "BABABAB.",
    "seg_256": "@Q9 " + w(54) + " oh.",
    "seg_wrap_phrase": "@Q9 " + w(54) + " oh oh.",
    "seg_wrap_word": "@Q9 " + w(55) + ". @Q9 " + w(55) + " oh.",
}
for name, text in t.items():
    open("tests/corpus/" + name + ".txt", "w", newline="\n").write(text + "\n")
EOF
wc -c tests/corpus/phrase_pauses.txt tests/corpus/baseline.txt tests/corpus/phonetic_stress.txt tests/corpus/phrase_127.txt tests/corpus/seg_256.txt tests/corpus/seg_wrap_phrase.txt tests/corpus/seg_wrap_word.txt
```

Expected `wc -c` (bytes, including the newline): 61, 72, 26, 149, 147, 150, 295.

The resulting lines, for review:

| File | Line |
|---|---|
| `phrase_pauses.txt` | `@D1 Yes, no (maybe) - fine. @D3 "Quote" [one]. @D0 Stop. Go!` |
| `baseline.txt` | `@RA40 @RBC0 Rising tone. @RF20 @RG90 Up, again. @RA80 @RB80 Level tone.` |
| `phonetic_stress.txt` | ``@X1 K}AE{T 8 X M}AE`T?@X0`` |
| `phrase_127.txt` | `@X1. BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABABA BABABAB.` |
| `seg_256.txt` | `@Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it oh.` |
| `seg_wrap_phrase.txt` | `@Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it oh oh.` |
| `seg_wrap_word.txt` | `@Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o. @Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o oh.` |

(`phrase_127` has 127 letters `A`/`B`: `tr -cd 'AB' < tests/corpus/phrase_127.txt | wc -c` prints `127`.)

- [ ] **Step 2: Add them to the corpus list**

In `tests/corpus_list.h`:
- Replace the comment lines

```c
   The last five (M4.2) reach the commands the rest never does: replies and ignored commands, the immediate
   @I+ / @I?, unusual arguments, an @ that matches no command, and @= (an empty socket, then a bank entered). */
```

  with:

```c
   The next five (M4.2) reach the commands the rest never does: replies and ignored commands, the immediate
   @I+ / @I?, unusual arguments, an @ that matches no command, and @= (an empty socket, then a bank entered).
   The last seven (M4.3a) reach the segment generator's rare paths: a phrase pause that computes 0, and @D0;
   a level and a rising baseline; phonetic symbols without features, and a second stress in a word; a phrase of
   exactly 127 phonetic letters; and the 16-bit segment index (exactly 256 segments, then a page wrap at a phrase
   pause, at a word pause and at a segment). The seg_* texts depend on exact segment counts: one per file. */
```

- Replace the last line of `CORPUS_NAMES`, `    "cmd_misc", "index_marks", "cmd_args", "cmd_unmatched", "bank_select"`, with:

```c
    "cmd_misc", "index_marks", "cmd_args", "cmd_unmatched", "bank_select", \
    "phrase_pauses", "baseline", "phonetic_stress", "phrase_127", "seg_256", "seg_wrap_phrase", \
    "seg_wrap_word"
```

- [ ] **Step 3: Run the golden suites to see them fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL, with `2 suite(s), 126 failure(s), 0 skip(s)`:
- `suite_golden` prints a `missing tests/golden/<name>.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` line for each of the seven names (7 failures);
- `suite_golden_hybrid` prints `39 corpus files x 17 modes (all, then each routine alone)` and fails `CHECK failed: want != NULL` 7 × 17 = 119 times.

- [ ] **Step 4: Generate the goldens with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: `2 suite(s), 0 failure(s), 0 skip(s)`, with these seven lines among the `updated` lines (the frame counts are what the drafting run produced; they are the rom engine's, so any difference means a different text):

```
  updated tests/golden/phrase_pauses.frm (326 frames)
  updated tests/golden/baseline.frm (322 frames)
  updated tests/golden/phonetic_stress.frm (133 frames)
  updated tests/golden/phrase_127.frm (1361 frames)
  updated tests/golden/seg_256.frm (1767 frames)
  updated tests/golden/seg_wrap_phrase.frm (1788 frames)
  updated tests/golden/seg_wrap_word.frm (3554 frames)
```

Then run `git status --short tests/golden`. Expected: exactly these seven lines:

```
?? tests/golden/baseline.frm
?? tests/golden/phonetic_stress.frm
?? tests/golden/phrase_127.frm
?? tests/golden/phrase_pauses.frm
?? tests/golden/seg_256.frm
?? tests/golden/seg_wrap_phrase.frm
?? tests/golden/seg_wrap_word.frm
```

If any existing `.frm` shows as modified, STOP and report: the oracle is not deterministic, and nothing may be committed. If a frame count differs from the list above, STOP and check the text of that file against Step 1 byte for byte.

- [ ] **Step 5: Confirm the coverage**

Run (the coverage file goes under `build/`, which is git-ignored):

```bash
./build/apollo2_coverage.exe -o build/m43a.cov
for a in C9EC D3E5 D3E7 D40C D28F D41A D19B CA07 CA2F CA62; do grep -q "^x rom $a$" build/m43a.cov && echo "$a ok" || echo "$a MISSING"; done
```

Expected: the summary line `rom 4040, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`, then ten `ok` lines. If any address is MISSING, STOP and report.

What each address stands for:

| Address | Text | Meaning |
|---|---|---|
| `C9EC` | `phrase_pauses` | `INC A`: a phrase pause that computes 0 becomes 1 |
| `D3E5` | `baseline` | `pros_declination`'s `BCC` falls (end ≥ start); the `BEQ` here, taken, is the level path |
| `D3E7`, `D40C` | `baseline` | the rising path, from its first instruction to its `RTS` |
| `D28F` | `phonetic_stress` | a second stress in a word (`pros_params[24]`) |
| `D41A` | `phrase_127` | `pros_declination_step`: the phrase-count divisor 0 → 1 |
| `D19B` | `seg_256` | the final 16-bit `DEC` with low byte 0 (exactly 256 segments) |
| `CA07` | `seg_wrap_phrase` | page wrap in `ph_append_phrase_pause` |
| `CA2F` | `seg_wrap_word` (first clause) | page wrap in `ph_append_pause` |
| `CA62` | `seg_wrap_word` (second clause) | page wrap in `pros_append_segment` |

The coverage file has no branch directions. Three targets of spec §4.1 are directions whose target address is also reached another way, so this step cannot show them: `BEQ $D1D8` taken (@D0, `phrase_pauses`), `BEQ $D300` taken (`8`/`X`, `phonetic_stress`) and `BEQ $D3E5` taken (level, `baseline`). They were confirmed with a branch probe while this plan was written (see the list at the top of the task); `diff_lts` and `golden_hybrid` compare the C on these very texts from Task 3 on.

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected:
- All 30 suites are ok (`30 suite(s), 0 failure(s), 0 skip(s)`).
- `golden_hybrid` prints `39 corpus files x 17 modes (all, then each routine alone)`.
- `diff_lts` shows 0 mismatches, 16 `N calls checked` lines (every N greater than 0; the counts grow with the new texts), `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, both unchanged.
- `suite_synth` includes the new goldens in its corpus peak; the drafting run printed `corpus peak plain 18727 (-4.9 dBFS)` and `corpus peak b11 15784 (-6.3 dBFS)`. If it reports clipping, or a plain peak outside −12…−1 dBFS, STOP and report. Do not change any calibration.

- [ ] **Step 7: Commit**

```bash
git add tests/corpus/phrase_pauses.txt tests/corpus/baseline.txt tests/corpus/phonetic_stress.txt tests/corpus/phrase_127.txt tests/corpus/seg_256.txt tests/corpus/seg_wrap_phrase.txt tests/corpus/seg_wrap_word.txt tests/golden/phrase_pauses.frm tests/golden/baseline.frm tests/golden/phonetic_stress.frm tests/golden/phrase_127.frm tests/golden/seg_256.frm tests/golden/seg_wrap_phrase.frm tests/golden/seg_wrap_word.frm tests/corpus_list.h
git commit -m "Corpus: seven texts for the segment generator's rare paths (zero phrase pause and @D0, level and rising baseline, featureless symbols and a second stress, a 127-letter phrase, exactly 256 segments and the page wraps), goldens from the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The prosody data as readable C (spec §3)

**ROM facts checked** (byte by byte in `UK_V2.01_4.04_ROM.BIN`, file offset = CPU address, against `ghidra740/annotations/phoneme.txt`, `command.txt`, `unused.txt` and the dumps `rom_D129_pros_phrase_gen.txt`, `rom_D43E_ph_lengthen_count.txt`, `rom_C9B8_ph_append_phrase_pause.txt`):
- **Every address in the conventions is right**: `PH_SYMBOL_CLASS` $9BB4, `PH_SYMBOL_FEATURES` $9CB4, `PH_LIST_PTRS` $9D74, `PH_LISTS` $A187, `PH_DUR_LENGTHEN` $C67B, `PH_DUR_SHORTEN` $C77B, `CMD_SPEED_MAP_DATA` $C898 (already in `fw_data.h`), `PH_LENGTHEN_STEPS` $D44A. This task adds three names the conventions do not list, all in `fw_data.h`: `UNUSED_A397` (the end of the lists, after the `UNUSED_9824` precedent), `PH_WORD_PAUSE` ($5F, the word-pause item), and the class and feature bit names `PHC_*` / `PHF_*`.
- **The decoder** (`ph_decode_symbol`, $D31B): `ASL A`; carry clear (symbol < $80): `LDA $9D74,Y` / `$9D75,Y`; carry set: `LDA $9E74,Y` / `$9E75,Y` with Y = 2·(s − $80). $9E74 = $9D74 + $100, so `PH_LIST_PTRS + 2*s` gives the pointer of every symbol $20–$FF ($9DB4–$9F73, 448 bytes). Items: `$FF` ends the list; `$5F` is the word-pause item (it also ends the symbol); anything else is a (phoneme, max, min) triple.
- **The 224 pointers** (symbols $20–$FF) each point at the next list: the lists lie end to end in code order from $A187 and end exactly at $A397, where `unused_A397` (617 bytes, all $FF) begins. 101 triples in 67 lists (at most 3 per list), max ≥ min in all, plus the space's `5F FF`; 224 ends + 303 + 1 = 528 bytes.
- **Aliasing:** $9D74–$9DB3 (the pointer slots of symbols $00–$1F) are `ph_symbol_features[$C0..$FF]`; the non-zero ones are $C0 ($47), $E0–$E2 ($81). Symbols $00–$1F all have class 0, so no code reads those slots as pointers.
- **`ph_symbol_class`:** only bits 7, 6, 3, 2, 1, 0 occur. Punctuation kinds: `,` = $05, `.` `:` `;` = $0B, `?` = $0D, all other punctuation $01; $E0–$E2 = $C4 (phonetic, vowel and kind bit 2 without the punctuation bit). The annotations name bits 1–3 only as "the kind of punctuation"; they are `PHC_KIND_1..3` here. **No phoneme has a name in `ghidra740/annotations`** (they are numbered: "phoneme 41", the pause $3B), so the list comments are the symbol and the annotation's notes for $20, $83, $84, $87, $8C, $98, $C0.
- **`ph_dur_lengthen`, `ph_dur_shorten`:** not a formula (round(1.1·x) fails from x = 155; x − round(x/10) fails at 216), so they stay numeric arrays. Checked bounds: |lengthen[x] − 1.1·x| < 1.5 below 233, 255 from x = 233; |shorten[x] − 0.9·x| < 1. Both monotonic.
- **`cmd_speed_map_data`** $C898–$C9B7: nine maps of 32, no zero entry; map 1 is 1, 2, 4, … 62, map 9 is all 1. It ends exactly at `ph_append_phrase_pause` ($C9B8). M4.2's `fw_cmd_speed_maps` are `CMD_SPEED_MAP_DATA + 32 * k` (k = 0–8, entry 10 repeats 9): the encoder now asserts that each points at the start of a map.
- **`ph_lengthen_steps`:** `ph_lengthen_count` ends `B9 4A D4` (`LDA $D44A,Y`), `A8` (`TAY`), `60` (`RTS` at $D44A); entries 1–30 are 1, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9, 9, 10, 10, 10, 11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15. The table ends at $D468; $D469 is M4.2's `par_voice_a_count`.
- **Byte count:** $9BB4–$9F73 (960) + $A187–$A396 (528) + $C67B–$C77A and $C77B–$C87A (512) + $C898–$C9B7 (288) + $D44A–$D468 (31) = **2,319**; `compared` goes from 35,878 to **38,197**. The `k_rom` ranges are half-open `[from, to)` (the `range` comment in the test): $9804–$9BB4 becomes $9804–$9F74, $C87B–$C898 becomes $C67B–$C9B8, $D469–$D46A becomes $D44A–$D46A, and $A187–$A397 is added.

**Files:**
- Create: `src/firmware/data/pros_data.c` (generated by the tool, then reviewed)
- Modify: `tools/extract_tables.py`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c`

**Interfaces:**
- Consumes: the M4.0/M4.2 encoder helpers in `fw_data.c` (`put`, `put_word`, `fail`, `check_end`); M4.2's `CMD_SPEED_MAPS`, `CMD_SPEED_MAP_DATA`, `PAR_VOICE_A_COUNT` and `fw_cmd_speed_maps`; `char_name` and `HEADER` in the extractor.
- Produces (in `fw_data.h`):
  - the data addresses `PH_SYMBOL_CLASS 0x9BB4`, `PH_SYMBOL_FEATURES 0x9CB4`, `PH_LIST_PTRS 0x9D74` (the pointer of symbol s, $20–$FF, at `PH_LIST_PTRS + 2 * s`), `PH_LISTS 0xA187`, `UNUSED_A397 0xA397`, `PH_DUR_LENGTHEN 0xC67B`, `PH_DUR_SHORTEN 0xC77B`, `PH_LENGTHEN_STEPS 0xD44A`, and the item value `PH_WORD_PAUSE 0x5F`; `CMD_SPEED_MAP_DATA` (existing) is now converted. The C of Tasks 4–5 reads them only through `fw_rom8` at these addresses: Task 4 `PH_LENGTHEN_STEPS`; Task 5 `PH_SYMBOL_CLASS`, `PH_SYMBOL_FEATURES`, `PH_LIST_PTRS` (and the lists through the pointers), `PH_DUR_SHORTEN`, `PH_DUR_LENGTHEN`, and the speed map through `cmd_speed_map` ($3A) as the ROM does.
  - the bit names `PHC_PHONETIC`, `PHC_VOWEL`, `PHC_KIND_3`, `PHC_KIND_2`, `PHC_KIND_1`, `PHC_PUNCT` and `PHF_VOWEL`, `PHF_CONSONANT`, `PHF_STOP`, `PHF_FRICATIVE`, `PHF_NASAL`, `PHF_LIQUID`, `PHF_LONG`, `PHF_VOICED`;
  - the types `fw_ph_triple`, `fw_ph_list` and the arrays `fw_ph_symbol_class[256]`, `fw_ph_symbol_features[256]`, `fw_ph_lists[256]`, `fw_ph_dur_lengthen[256]`, `fw_ph_dur_shorten[256]`, `fw_cmd_speed_map_data[9][32]`, `fw_ph_lengthen_steps[31]`. Only the encoder and `test_firmware_data` use them.
  - Encoder step 8 in `fw_image_build`.

No name collides: `fw_ram.h` has `PH_LIST_PTR` (zero page $36), `PH_PHONEME`, `PH_MIN_DUR`, `PH_MAX_DUR`, `PH_DURATION`, `PH_DUR_RANGE`, `PROS_CLASS`, `PROS_FEATURES`, all different; nothing in `src/` or `tests/` uses `PHC_`, `PHF_` or `fw_ph_`. `pros_data.c` is a new basename.

- [ ] **Step 1: Write the failing test**

In `tests/test_firmware_data.c`:
- Replace the `k_rom` definition:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0x9BB4 }, { 0xACC7, 0xACE8 }, { 0xC87B, 0xC898 },
                               { 0xD469, 0xD46A }, { 0xE5E2, 0xE5E3 }, { 0xF75B, 0xF77B } };
```

with:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0x9F74 }, { 0xA187, 0xA397 }, { 0xACC7, 0xACE8 },
                               { 0xC67B, 0xC9B8 }, { 0xD44A, 0xD46A }, { 0xE5E2, 0xE5E3 }, { 0xF75B, 0xF77B } };
```

- In `test_counts`, replace:

```c
    CHECK_EQ(fw_cmd_case_fold_count, 21);
}
```

with:

```c
    CHECK_EQ(fw_cmd_case_fold_count, 21);
    unsigned ph_lists = 0, ph_triples = 0, ph_pauses = 0;
    for (int s = 0; s < 256; s++) {
        ph_lists += fw_ph_lists[s].count > 0;
        ph_triples += fw_ph_lists[s].count;
        ph_pauses += fw_ph_lists[s].word_pause;
    }
    CHECK_EQ(ph_lists, 67);
    CHECK_EQ(ph_triples, 101);
    CHECK_EQ(ph_pauses, 1);
    CHECK(fw_ph_lists[' '].word_pause);           /* the space's list is only the word-pause item */
    CHECK_EQ(fw_ph_lengthen_steps[0], 0x60);      /* $D44A: the RTS of ph_lengthen_count */
    CHECK_EQ(fw_cmd_speed_map_data[8][31], 1);    /* speed 9: every duration is 1 frame */
}
```

- In `test_against_rom`, replace:

```c
    CHECK_EQ(compared, 35878);   /* M4.0's 35,214, M4.1's phonetic texts (97) and M4.2's command data (567) */
```

with:

```c
    CHECK_EQ(compared, 38197);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567) and
                                    M4.3a's prosody data (2,319) */
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: a compile error, `tests\test_firmware_data.c(63): error C2065: 'fw_ph_lists': undeclared identifier` (and the other new names).

- [ ] **Step 3: Declare the addresses, bits, types and arrays in `src/firmware/data/fw_data.h`**

- After the line `typedef struct { uint8_t first, last, to; bool shift; } fw_case_fold;`, add:

```c

/* A phoneme-list triple (M4.3a spec 3): the phoneme (a row of the voice tables) and its duration range, max and min,
   before the speed map. */
typedef struct { uint8_t phoneme, max, min; } fw_ph_triple;
/* The phoneme list of one symbol: count triples (at most 3), then the $FF end; word_pause: the list starts with the
   word-pause item PH_WORD_PAUSE (the space's, with no triples). */
typedef struct { uint8_t count; fw_ph_triple item[3]; bool word_pause; } fw_ph_list;
```

- Replace:

```c
#define CMD_SPEED_MAP_DATA  0xC898   /* not converted (prosody's, M4.3): cmd_speed_maps point into it */
```

with:

```c
#define CMD_SPEED_MAP_DATA  0xC898   /* byte[9][32], converted with the prosody data: cmd_speed_maps point into it */
```

- After the line `#define PAR_VOICE_B         0xE5E3   /* the M4.4 voice B table, not converted; the C writes only its address */`, add:

```c

/* Pinned addresses of the prosody data (ghidra740/annotations: phoneme.txt, command.txt, unused.txt; M4.3a spec 3) */
#define PH_SYMBOL_CLASS     0x9BB4   /* byte[256]: PHC_* bits per symbol code */
#define PH_SYMBOL_FEATURES  0x9CB4   /* byte[256]: PHF_* bits per symbol code */
#define PH_LIST_PTRS        0x9D74   /* base: the list pointer of symbol s ($20-$FF) at PH_LIST_PTRS + 2 * s; the
                                        slots of $00-$1F are the last 64 bytes of ph_symbol_features. The ROM reads
                                        the pointers of $80-$FF from $9E74 + 2 * (s - $80), the same addresses */
#define PH_LISTS            0xA187   /* the phoneme lists of the symbols $20-$FF, end to end in code order */
#define UNUSED_A397         0xA397   /* the $FF padding after the lists (617 bytes), not converted */
#define PH_DUR_LENGTHEN     0xC67B   /* byte[256]: one lengthening step */
#define PH_DUR_SHORTEN      0xC77B   /* byte[256]: one shortening step */
#define PH_LENGTHEN_STEPS   0xD44A   /* byte[31], the base ph_lengthen_count indexes; entry 0 is its own RTS opcode */
#define PH_WORD_PAUSE       0x5F     /* the word-pause item of a phoneme list (the space's) */
```

- After the line `#define CC_PUNCT        0x01   /* punctuation or symbol */`, add:

```c

/* ph_symbol_class bits (phoneme.txt) */
#define PHC_PHONETIC    0x80   /* a phonetic symbol: decoded through its phoneme list */
#define PHC_VOWEL       0x40   /* a syllable nucleus */
#define PHC_KIND_3      0x08   /* kind of punctuation, bit 3: . : ; ? */
#define PHC_KIND_2      0x04   /* kind of punctuation, bit 2: , ? (and the symbols $E0-$E2) */
#define PHC_KIND_1      0x02   /* kind of punctuation, bit 1: . : ; */
#define PHC_PUNCT       0x01   /* punctuation: a phrase pause */

/* ph_symbol_features bits (phoneme.txt) */
#define PHF_VOWEL       0x80
#define PHF_CONSONANT   0x40
#define PHF_STOP        0x20
#define PHF_FRICATIVE   0x10
#define PHF_NASAL       0x08
#define PHF_LIQUID      0x04   /* liquid or glide */
#define PHF_LONG        0x02   /* long vowel, diphthong or syllabic consonant */
#define PHF_VOICED      0x01
```

- After the line `extern const uint8_t fw_par_voice_a_count, fw_par_voice_b_count;`, add:

```c
extern const uint8_t fw_ph_symbol_class[256], fw_ph_symbol_features[256];
extern const fw_ph_list fw_ph_lists[256];   /* by symbol; $00-$1F have no list */
extern const uint8_t fw_ph_dur_lengthen[256], fw_ph_dur_shorten[256];
extern const uint8_t fw_cmd_speed_map_data[9][32];
extern const uint8_t fw_ph_lengthen_steps[31];
```

- [ ] **Step 4: Extend the converter and generate the data file**

`tools/extract_tables.py`:
- Replace the start of the module docstring:

```python
character classes, the phonetic texts (M4.1) and the command tables (M4.2) -> readable C in src/firmware/data/.
The output is a first draft: review it, annotate it by hand if useful, and commit it. Nothing runs this at build
or test time; tests/test_firmware_data.c checks the committed C against the ROM.
usage: python tools/extract_tables.py [--phonetic-only | --command-only] [ROM]
```

with:

```python
character classes, the phonetic texts (M4.1), the command tables (M4.2) and the prosody tables (M4.3a) -> readable
C in src/firmware/data/. The output is a first draft: review it, annotate it by hand if useful, and commit it.
Nothing runs this at build or test time; tests/test_firmware_data.c checks the committed C against the ROM.
usage: python tools/extract_tables.py [--phonetic-only | --command-only | --prosody-only] [ROM]
```

- After the `CMD_HANDLERS = {...}` block (its last line is `}`, after `0xABBE: "ROM_CMD_LIST_LANGUAGES", 0xAA61: "ROM_CMD_SET_SPEAKER", 0xABDC: "ROM_CMD_IGNORE",`) and before `CLASS_BITS = [...]`, add:

```python
# The prosody data (M4.3a spec 3), as annotated in ghidra740/annotations/phoneme.txt and command.txt
PH_SYMBOL_CLASS, PH_SYMBOL_FEATURES, PH_LIST_PTRS, PH_LISTS = 0x9BB4, 0x9CB4, 0x9D74, 0xA187
UNUSED_A397, PH_DUR_LENGTHEN, PH_DUR_SHORTEN, PH_LENGTHEN_STEPS = 0xA397, 0xC67B, 0xC77B, 0xD44A
PH_WORD_PAUSE, PH_APPEND_PHRASE_PAUSE = 0x5F, 0xC9B8
PH_CLASS_BITS = [(0x80, "PHC_PHONETIC"), (0x40, "PHC_VOWEL"), (0x08, "PHC_KIND_3"), (0x04, "PHC_KIND_2"),
                 (0x02, "PHC_KIND_1"), (0x01, "PHC_PUNCT")]
PH_FEATURE_BITS = [(0x80, "PHF_VOWEL"), (0x40, "PHF_CONSONANT"), (0x20, "PHF_STOP"), (0x10, "PHF_FRICATIVE"),
                   (0x08, "PHF_NASAL"), (0x04, "PHF_LIQUID"), (0x02, "PHF_LONG"), (0x01, "PHF_VOICED")]
# What phoneme.txt says about single lists (it numbers the phonemes, it names none)
PH_LIST_NOTES = {
    0x20: "the space: only the word-pause item, a pause of @Q length when @Q is not 0",
    0x83: "the only list with phoneme 7; only @X phonetic text reaches it",
    0x84: "the only list with phoneme 6; only @X phonetic text reaches it",
    0x87: "the only list with phoneme 22; only @X phonetic text reaches it",
    0x8C: "the only list with phoneme 40; only @X phonetic text reaches it",
    0x98: "the only list with phoneme 23; from letter-to-sound rules (MEGATON)",
    0xC0: "the only list with phoneme 41, a long syllabic consonant; from letter-to-sound rules (LEVEL)",
}
```

- Above `def main():` (after `write_command_data`, keeping two blank lines between functions), add:

```python
def write_prosody_data(rom):
    """src/firmware/data/pros_data.c: the tables of the phoneme expansion and the phrase generator (M4.3a spec 3).
    The list pointers and the lists' $FF ends are left to the encoder."""
    def word(a):
        return rom[a] | rom[a + 1] << 8

    def bits(v, names):
        assert v & ~sum(b for b, _ in names) == 0, "value $%02X has an unnamed bit" % v
        return " | ".join(n for b, n in names if v & b)

    def numbers(vals, per, what, first=0):
        """Rows of per decimal numbers, each with the index range it holds (the first value's index is first)."""
        out = []
        for i in range(0, len(vals), per):
            row = vals[i:i + per]
            out.append("    " + ", ".join("%3d" % v for v in row) + ",   /* %s %d-%d */"
                       % (what, first + i, first + i + len(row) - 1))
        return out

    lines = [HEADER.replace("M4.0 spec section 4", "M4.3a spec section 3").rstrip("\n") + "\n",
             "/* ph_symbol_class ($%04X): the class bits of each symbol code of tn_phonetic. Codes not listed have"
             % PH_SYMBOL_CLASS,
             "   none, among them the control codes, the space and the prosody markers ` { | } ~. */",
             "const uint8_t fw_ph_symbol_class[256] = {"]
    for s in range(256):
        v = rom[PH_SYMBOL_CLASS + s]
        assert s >= 0x20 or v == 0, "symbol $%02X below $20 has a class: the decoder would read its pointer" % s
        if v:
            lines.append("    [0x%02X] = %s,   /* %s */" % (s, bits(v, PH_CLASS_BITS), char_name(s)))
    lines += ["};", ""]

    assert PH_SYMBOL_FEATURES + 0xC0 == PH_LIST_PTRS, "ph_list_ptrs no longer starts inside ph_symbol_features"
    lines += ["/* ph_symbol_features ($%04X): the feature bits of each symbol code (codes not listed: none). Its last"
              % PH_SYMBOL_FEATURES,
              "   64 bytes, the features of $C0-$FF ($%04X-$%04X), are also the list-pointer slots of the symbols"
              % (PH_LIST_PTRS, PH_LIST_PTRS + 0x3F),
              "   $00-$1F, which have no class and are never decoded: the encoder writes them once, as features. */",
              "const uint8_t fw_ph_symbol_features[256] = {"]
    for s in range(256):
        v = rom[PH_SYMBOL_FEATURES + s]
        if v:
            alias = "; also in the pointer slot of symbol $%02X" % ((s - 0xC0) // 2) if s >= 0xC0 else ""
            lines.append("    [0x%02X] = %s,   /* %s%s */" % (s, bits(v, PH_FEATURE_BITS), char_name(s), alias))
    lines += ["};", ""]

    lists, at = {}, PH_LISTS
    for s in range(0x20, 0x100):
        assert word(PH_LIST_PTRS + 2 * s) == at, "the list pointer of symbol $%02X is not the next list" % s
        pause = rom[at] == PH_WORD_PAUSE
        at += pause
        items = []
        while rom[at] != 0xFF:
            assert not pause and rom[at] != PH_WORD_PAUSE, "a word-pause item with more at $%04X" % at
            phoneme, top, bottom = rom[at:at + 3]
            assert top >= bottom, "a triple at $%04X with max below min" % at
            items.append((phoneme, top, bottom))
            at += 3
        assert len(items) <= 3, "symbol $%02X has more than 3 triples" % s
        lists[s] = (pause, items)
        at += 1
    assert at == UNUSED_A397 and set(rom[UNUSED_A397:0xA600]) == {0xFF}, "the lists end at $%04X" % at
    lines += ["/* The phoneme lists ($%04X-$%04X), by symbol: the (phoneme, max, min) triples ph_decode_symbol turns"
              % (PH_LISTS, UNUSED_A397 - 1),
              "   into segments (the phoneme numbers a row of the voice tables; the duration is min plus the scaled",
              "   max - min). The encoder lays out the lists of $20-$FF end to end in code order, each with its $FF",
              "   end, and writes the list pointers (PH_LIST_PTRS + 2 * symbol, $%04X-$%04X) from where they land."
              % (PH_LIST_PTRS + 0x40, PH_LIST_PTRS + 0x1FF),
              "   Symbols not listed have an empty list (only the end) and make no segment. */",
              "const fw_ph_list fw_ph_lists[256] = {"]
    for s in range(0x20, 0x100):
        pause, items = lists[s]
        if not pause and not items:
            continue
        if s in PH_LIST_NOTES:
            lines.append("    /* %s */" % PH_LIST_NOTES[s])
        if pause:
            lines.append("    [0x%02X] = { .word_pause = true },   /* %s */" % (s, char_name(s)))
        else:
            body = ", ".join("{ %d, %d, %d }" % t for t in items)
            lines.append("    [0x%02X] = { %d, { %s } },   /* %s */" % (s, len(items), body, char_name(s)))
    lines += ["};", ""]

    lengthen = list(rom[PH_DUR_LENGTHEN:PH_DUR_LENGTHEN + 256])
    shorten = list(rom[PH_DUR_SHORTEN:PH_DUR_SHORTEN + 256])
    cap = lengthen.index(255)
    assert all(abs(lengthen[x] - 1.1 * x) < 1.5 for x in range(cap)) and set(lengthen[cap:]) == {255}
    assert all(abs(shorten[x] - 0.9 * x) < 1 for x in range(256))
    lines += ["/* ph_dur_lengthen ($%04X): one lengthening step, x to about 1.1 * x (less than 1.5 off), 255 from"
              % PH_DUR_LENGTHEN,
              "   x = %d on. ph_decode_symbol applies it ph_lengthen_count times to a duration range. */" % cap,
              "const uint8_t fw_ph_dur_lengthen[256] = {"]
    lines += numbers(lengthen, 16, "x =")
    lines += ["};",
              "/* ph_dur_shorten ($%04X): one shortening step, x to about 0.9 * x (less than 1 off)."
              % PH_DUR_SHORTEN,
              "   ph_decode_symbol applies it (shortening count + 1) / 2 times to a duration range. */",
              "const uint8_t fw_ph_dur_shorten[256] = {"]
    lines += numbers(shorten, 16, "x =")
    lines += ["};", ""]

    assert PH_DUR_LENGTHEN + 256 == PH_DUR_SHORTEN and PH_DUR_SHORTEN + 256 == CMD_SPEED_MAPS
    assert CMD_SPEED_MAP_DATA + 9 * 32 == PH_APPEND_PHRASE_PAUSE, "cmd_speed_map_data does not end at code"
    lines += ["/* cmd_speed_map_data ($%04X): per speed 1-9 (cmd_speed_maps) a map of 32 durations in frames:"
              % CMD_SPEED_MAP_DATA,
              "   ph_decode_symbol reads entry duration / 8 into ph_duration. It ends where ph_append_phrase_pause",
              "   begins. */",
              "const uint8_t fw_cmd_speed_map_data[9][32] = {"]
    for k in range(9):
        m = list(rom[CMD_SPEED_MAP_DATA + 32 * k:CMD_SPEED_MAP_DATA + 32 * k + 32])
        assert min(m) >= 1, "a speed map with a duration of 0"
        lines.append("    {   /* speed %d ($%04X) */" % (k + 1, CMD_SPEED_MAP_DATA + 32 * k))
        lines += ["    " + r for r in numbers(m, 16, "duration / 8 =")]
        lines.append("    },")
    lines += ["};", ""]

    assert rom[PH_LENGTHEN_STEPS - 4:PH_LENGTHEN_STEPS + 1] == b"\xb9\x4a\xd4\xa8\x60", \
        "ph_lengthen_count no longer ends with LDA $D44A,Y / TAY / RTS"
    assert PH_LENGTHEN_STEPS + 31 == PAR_VOICE_A_COUNT
    lines += ["/* ph_lengthen_steps ($%04X, the base ph_lengthen_count indexes with A = 0-30; annotated from $%04X):"
              % (PH_LENGTHEN_STEPS, PH_LENGTHEN_STEPS + 1),
              "   the number of ph_dur_lengthen steps for a half count A. Entry 0 is the RTS that ends",
              "   ph_lengthen_count (LDA $%04X,Y / TAY / RTS): A = 0, from a lengthening count of $FF, reads the"
              % PH_LENGTHEN_STEPS,
              "   opcode as %d steps. */" % rom[PH_LENGTHEN_STEPS],
              "const uint8_t fw_ph_lengthen_steps[31] = {",
              "    0x%02X,   /* A = 0: the RTS opcode at $%04X, read as data */" % (rom[PH_LENGTHEN_STEPS],
                                                                              PH_LENGTHEN_STEPS)]
    lines += numbers(list(rom[PH_LENGTHEN_STEPS + 1:PH_LENGTHEN_STEPS + 31]), 15, "A =", 1)
    lines.append("};")
    with open(os.path.join(OUT, "pros_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote pros_data.c in", OUT)
```

- In `main()`, replace:

```python
    args = [a for a in sys.argv[1:] if a not in ("--phonetic-only", "--command-only")]
```

with:

```python
    args = [a for a in sys.argv[1:] if a not in ("--phonetic-only", "--command-only", "--prosody-only")]
```

and replace:

```python
    if "--command-only" in sys.argv:   # M4.2: leave the reviewed M4.0 and M4.1 files alone
        write_command_data(rom)
        return
    write_phonetic_texts(rom)
    if "--phonetic-only" in sys.argv:   # M4.1: leave the reviewed M4.0 tables alone
        return
    write_command_data(rom)
```

with:

```python
    if "--command-only" in sys.argv:   # M4.2: leave the reviewed M4.0 and M4.1 files alone
        write_command_data(rom)
        return
    if "--prosody-only" in sys.argv:   # M4.3a: leave the reviewed M4.0, M4.1 and M4.2 files alone
        write_prosody_data(rom)
        return
    write_phonetic_texts(rom)
    if "--phonetic-only" in sys.argv:   # M4.1: leave the reviewed M4.0 tables alone
        return
    write_command_data(rom)
    write_prosody_data(rom)
```

The tool's asserts check the ROM facts above: the symbols below $20 have no class; `ph_list_ptrs` starts inside `ph_symbol_features` exactly at the features of $C0; every pointer of $20–$FF is the next list; a word-pause item stands alone; at most 3 triples, max ≥ min; the lists end at `unused_A397` and the padding is all $FF; the duration tables are within the stated bounds of 1.1·x and 0.9·x; the four tables from $C67B are contiguous and `cmd_speed_map_data` ends at `ph_append_phrase_pause`; `ph_lengthen_count` ends `LDA $D44A,Y / TAY / RTS`; the steps end at `par_voice_a_count`.

Run (from the repo root): `python tools/extract_tables.py --prosody-only "$APOLLO2_ROM"`
Expected: `wrote pros_data.c in src\firmware\data`. `git status --short src tools tests` must show exactly ` M src/firmware/data/fw_data.h`, ` M tests/test_firmware_data.c`, ` M tools/extract_tables.py` and `?? src/firmware/data/pros_data.c`: the M4.0, M4.1 and M4.2 data files are untouched. (A run without a flag would overwrite the hand-reviewed M4.0 files; do not run it.)

The generated file must read exactly (review it against this; 390 lines):

```c
/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.3a spec section 3), then reviewed.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"

/* ph_symbol_class ($9BB4): the class bits of each symbol code of tn_phonetic. Codes not listed have
   none, among them the control codes, the space and the prosody markers ` { | } ~. */
const uint8_t fw_ph_symbol_class[256] = {
    [0x21] = PHC_PUNCT,   /* '!' ($21) */
    [0x22] = PHC_PUNCT,   /* '"' ($22) */
    [0x23] = PHC_PUNCT,   /* '#' ($23) */
    [0x24] = PHC_PUNCT,   /* '$' ($24) */
    [0x25] = PHC_PUNCT,   /* '%' ($25) */
    [0x26] = PHC_PUNCT,   /* '&' ($26) */
    [0x27] = PHC_PUNCT,   /* $27 */
    [0x28] = PHC_PUNCT,   /* '(' ($28) */
    [0x29] = PHC_PUNCT,   /* ')' ($29) */
    [0x2A] = PHC_PUNCT,   /* '*' ($2A) */
    [0x2B] = PHC_PUNCT,   /* '+' ($2B) */
    [0x2C] = PHC_KIND_2 | PHC_PUNCT,   /* ',' ($2C) */
    [0x2D] = PHC_PUNCT,   /* '-' ($2D) */
    [0x2E] = PHC_KIND_3 | PHC_KIND_1 | PHC_PUNCT,   /* '.' ($2E) */
    [0x2F] = PHC_PUNCT,   /* '/' ($2F) */
    [0x30] = PHC_PHONETIC,   /* '0' ($30) */
    [0x31] = PHC_PHONETIC,   /* '1' ($31) */
    [0x32] = PHC_PHONETIC,   /* '2' ($32) */
    [0x33] = PHC_PHONETIC,   /* '3' ($33) */
    [0x34] = PHC_PHONETIC,   /* '4' ($34) */
    [0x35] = PHC_PHONETIC,   /* '5' ($35) */
    [0x36] = PHC_PHONETIC,   /* '6' ($36) */
    [0x37] = PHC_PHONETIC,   /* '7' ($37) */
    [0x38] = PHC_PHONETIC,   /* '8' ($38) */
    [0x39] = PHC_PHONETIC,   /* '9' ($39) */
    [0x3A] = PHC_KIND_3 | PHC_KIND_1 | PHC_PUNCT,   /* ':' ($3A) */
    [0x3B] = PHC_KIND_3 | PHC_KIND_1 | PHC_PUNCT,   /* ';' ($3B) */
    [0x3C] = PHC_PUNCT,   /* '<' ($3C) */
    [0x3D] = PHC_PUNCT,   /* '=' ($3D) */
    [0x3E] = PHC_PUNCT,   /* '>' ($3E) */
    [0x3F] = PHC_KIND_3 | PHC_KIND_2 | PHC_PUNCT,   /* '?' ($3F) */
    [0x40] = PHC_PUNCT,   /* '@' ($40) */
    [0x41] = PHC_PHONETIC | PHC_VOWEL,   /* 'A' ($41) */
    [0x42] = PHC_PHONETIC,   /* 'B' ($42) */
    [0x43] = PHC_PHONETIC,   /* 'C' ($43) */
    [0x44] = PHC_PHONETIC,   /* 'D' ($44) */
    [0x45] = PHC_PHONETIC | PHC_VOWEL,   /* 'E' ($45) */
    [0x46] = PHC_PHONETIC,   /* 'F' ($46) */
    [0x47] = PHC_PHONETIC,   /* 'G' ($47) */
    [0x48] = PHC_PHONETIC,   /* 'H' ($48) */
    [0x49] = PHC_PHONETIC | PHC_VOWEL,   /* 'I' ($49) */
    [0x4A] = PHC_PHONETIC | PHC_VOWEL,   /* 'J' ($4A) */
    [0x4B] = PHC_PHONETIC,   /* 'K' ($4B) */
    [0x4C] = PHC_PHONETIC,   /* 'L' ($4C) */
    [0x4D] = PHC_PHONETIC,   /* 'M' ($4D) */
    [0x4E] = PHC_PHONETIC,   /* 'N' ($4E) */
    [0x4F] = PHC_PHONETIC | PHC_VOWEL,   /* 'O' ($4F) */
    [0x50] = PHC_PHONETIC,   /* 'P' ($50) */
    [0x51] = PHC_PHONETIC | PHC_VOWEL,   /* 'Q' ($51) */
    [0x52] = PHC_PHONETIC,   /* 'R' ($52) */
    [0x53] = PHC_PHONETIC,   /* 'S' ($53) */
    [0x54] = PHC_PHONETIC,   /* 'T' ($54) */
    [0x55] = PHC_PHONETIC | PHC_VOWEL,   /* 'U' ($55) */
    [0x56] = PHC_PHONETIC,   /* 'V' ($56) */
    [0x57] = PHC_PHONETIC,   /* 'W' ($57) */
    [0x58] = PHC_PHONETIC,   /* 'X' ($58) */
    [0x59] = PHC_PHONETIC,   /* 'Y' ($59) */
    [0x5A] = PHC_PHONETIC,   /* 'Z' ($5A) */
    [0x5B] = PHC_PUNCT,   /* '[' ($5B) */
    [0x5C] = PHC_PUNCT,   /* $5C */
    [0x5D] = PHC_PUNCT,   /* ']' ($5D) */
    [0x5E] = PHC_PUNCT,   /* '^' ($5E) */
    [0x5F] = PHC_PUNCT,   /* '_' ($5F) */
    [0x61] = PHC_PHONETIC | PHC_VOWEL,   /* 'a' ($61) */
    [0x62] = PHC_PHONETIC,   /* 'b' ($62) */
    [0x63] = PHC_PHONETIC,   /* 'c' ($63) */
    [0x64] = PHC_PHONETIC,   /* 'd' ($64) */
    [0x65] = PHC_PHONETIC | PHC_VOWEL,   /* 'e' ($65) */
    [0x66] = PHC_PHONETIC,   /* 'f' ($66) */
    [0x67] = PHC_PHONETIC,   /* 'g' ($67) */
    [0x68] = PHC_PHONETIC | PHC_VOWEL,   /* 'h' ($68) */
    [0x69] = PHC_PHONETIC | PHC_VOWEL,   /* 'i' ($69) */
    [0x6A] = PHC_PHONETIC,   /* 'j' ($6A) */
    [0x6B] = PHC_PHONETIC,   /* 'k' ($6B) */
    [0x6C] = PHC_PHONETIC | PHC_VOWEL,   /* 'l' ($6C) */
    [0x6D] = PHC_PHONETIC,   /* 'm' ($6D) */
    [0x6E] = PHC_PHONETIC,   /* 'n' ($6E) */
    [0x6F] = PHC_PHONETIC | PHC_VOWEL,   /* 'o' ($6F) */
    [0x70] = PHC_PHONETIC,   /* 'p' ($70) */
    [0x71] = PHC_PHONETIC | PHC_VOWEL,   /* 'q' ($71) */
    [0x72] = PHC_PHONETIC | PHC_VOWEL,   /* 'r' ($72) */
    [0x73] = PHC_PHONETIC,   /* 's' ($73) */
    [0x74] = PHC_PHONETIC,   /* 't' ($74) */
    [0x75] = PHC_PHONETIC | PHC_VOWEL,   /* 'u' ($75) */
    [0x76] = PHC_PHONETIC | PHC_VOWEL,   /* 'v' ($76) */
    [0x77] = PHC_PHONETIC | PHC_VOWEL,   /* 'w' ($77) */
    [0x78] = PHC_PHONETIC | PHC_VOWEL,   /* 'x' ($78) */
    [0x79] = PHC_PHONETIC | PHC_VOWEL,   /* 'y' ($79) */
    [0x7A] = PHC_PHONETIC,   /* 'z' ($7A) */
    [0x80] = PHC_PHONETIC,   /* $80 */
    [0x81] = PHC_PHONETIC,   /* $81 */
    [0x82] = PHC_PHONETIC,   /* $82 */
    [0x83] = PHC_PHONETIC,   /* $83 */
    [0x84] = PHC_PHONETIC,   /* $84 */
    [0x85] = PHC_PHONETIC,   /* $85 */
    [0x86] = PHC_PHONETIC,   /* $86 */
    [0x87] = PHC_PHONETIC,   /* $87 */
    [0x88] = PHC_PHONETIC,   /* $88 */
    [0x89] = PHC_PHONETIC,   /* $89 */
    [0x8A] = PHC_PHONETIC,   /* $8A */
    [0x8B] = PHC_PHONETIC,   /* $8B */
    [0x8C] = PHC_PHONETIC,   /* $8C */
    [0x8D] = PHC_PHONETIC,   /* $8D */
    [0x8E] = PHC_PHONETIC,   /* $8E */
    [0x8F] = PHC_PHONETIC,   /* $8F */
    [0x90] = PHC_PHONETIC,   /* $90 */
    [0x91] = PHC_PHONETIC,   /* $91 */
    [0x92] = PHC_PHONETIC,   /* $92 */
    [0x93] = PHC_PHONETIC,   /* $93 */
    [0x94] = PHC_PHONETIC,   /* $94 */
    [0x95] = PHC_PHONETIC,   /* $95 */
    [0x96] = PHC_PHONETIC,   /* $96 */
    [0x97] = PHC_PHONETIC,   /* $97 */
    [0x98] = PHC_PHONETIC,   /* $98 */
    [0x99] = PHC_PHONETIC,   /* $99 */
    [0x9A] = PHC_PHONETIC,   /* $9A */
    [0x9B] = PHC_PHONETIC,   /* $9B */
    [0x9C] = PHC_PUNCT,   /* $9C */
    [0xA0] = PHC_PHONETIC,   /* $A0 */
    [0xA1] = PHC_PHONETIC,   /* $A1 */
    [0xA2] = PHC_PHONETIC,   /* $A2 */
    [0xA3] = PHC_PHONETIC,   /* $A3 */
    [0xA4] = PHC_PHONETIC,   /* $A4 */
    [0xA5] = PHC_PHONETIC,   /* $A5 */
    [0xA6] = PHC_PHONETIC,   /* $A6 */
    [0xA7] = PHC_PHONETIC,   /* $A7 */
    [0xC0] = PHC_PHONETIC,   /* $C0 */
    [0xE0] = PHC_PHONETIC | PHC_VOWEL | PHC_KIND_2,   /* $E0 */
    [0xE1] = PHC_PHONETIC | PHC_VOWEL | PHC_KIND_2,   /* $E1 */
    [0xE2] = PHC_PHONETIC | PHC_VOWEL | PHC_KIND_2,   /* $E2 */
};

/* ph_symbol_features ($9CB4): the feature bits of each symbol code (codes not listed: none). Its last
   64 bytes, the features of $C0-$FF ($9D74-$9DB3), are also the list-pointer slots of the symbols
   $00-$1F, which have no class and are never decoded: the encoder writes them once, as features. */
const uint8_t fw_ph_symbol_features[256] = {
    [0x30] = PHF_VOWEL | PHF_VOICED,   /* '0' ($30) */
    [0x31] = PHF_CONSONANT | PHF_LIQUID | PHF_VOICED,   /* '1' ($31) */
    [0x32] = PHF_VOWEL | PHF_VOICED,   /* '2' ($32) */
    [0x33] = PHF_CONSONANT | PHF_LIQUID,   /* '3' ($33) */
    [0x34] = PHF_VOWEL | PHF_VOICED,   /* '4' ($34) */
    [0x35] = PHF_VOWEL | PHF_VOICED,   /* '5' ($35) */
    [0x36] = PHF_CONSONANT | PHF_STOP,   /* '6' ($36) */
    [0x37] = PHF_CONSONANT | PHF_NASAL | PHF_LONG | PHF_VOICED,   /* '7' ($37) */
    [0x39] = PHF_VOWEL | PHF_VOICED,   /* '9' ($39) */
    [0x41] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'A' ($41) */
    [0x42] = PHF_CONSONANT | PHF_STOP | PHF_VOICED,   /* 'B' ($42) */
    [0x43] = PHF_CONSONANT | PHF_STOP,   /* 'C' ($43) */
    [0x44] = PHF_CONSONANT | PHF_STOP | PHF_VOICED,   /* 'D' ($44) */
    [0x45] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'E' ($45) */
    [0x46] = PHF_CONSONANT | PHF_FRICATIVE,   /* 'F' ($46) */
    [0x47] = PHF_CONSONANT | PHF_STOP | PHF_VOICED,   /* 'G' ($47) */
    [0x48] = PHF_CONSONANT | PHF_FRICATIVE,   /* 'H' ($48) */
    [0x49] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'I' ($49) */
    [0x4A] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'J' ($4A) */
    [0x4B] = PHF_CONSONANT | PHF_STOP,   /* 'K' ($4B) */
    [0x4C] = PHF_CONSONANT | PHF_LIQUID | PHF_VOICED,   /* 'L' ($4C) */
    [0x4D] = PHF_CONSONANT | PHF_NASAL | PHF_VOICED,   /* 'M' ($4D) */
    [0x4E] = PHF_CONSONANT | PHF_NASAL | PHF_VOICED,   /* 'N' ($4E) */
    [0x4F] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'O' ($4F) */
    [0x50] = PHF_CONSONANT | PHF_STOP,   /* 'P' ($50) */
    [0x51] = PHF_VOWEL | PHF_VOICED,   /* 'Q' ($51) */
    [0x52] = PHF_CONSONANT | PHF_LIQUID | PHF_VOICED,   /* 'R' ($52) */
    [0x53] = PHF_CONSONANT | PHF_FRICATIVE,   /* 'S' ($53) */
    [0x54] = PHF_CONSONANT | PHF_STOP,   /* 'T' ($54) */
    [0x55] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'U' ($55) */
    [0x56] = PHF_CONSONANT | PHF_FRICATIVE | PHF_VOICED,   /* 'V' ($56) */
    [0x57] = PHF_CONSONANT | PHF_LIQUID | PHF_VOICED,   /* 'W' ($57) */
    [0x59] = PHF_CONSONANT | PHF_LIQUID | PHF_VOICED,   /* 'Y' ($59) */
    [0x5A] = PHF_CONSONANT | PHF_FRICATIVE | PHF_VOICED,   /* 'Z' ($5A) */
    [0x61] = PHF_VOWEL | PHF_VOICED,   /* 'a' ($61) */
    [0x62] = PHF_VOWEL | PHF_VOICED,   /* 'b' ($62) */
    [0x63] = PHF_CONSONANT | PHF_FRICATIVE,   /* 'c' ($63) */
    [0x64] = PHF_CONSONANT | PHF_FRICATIVE | PHF_VOICED,   /* 'd' ($64) */
    [0x65] = PHF_VOWEL | PHF_VOICED,   /* 'e' ($65) */
    [0x66] = PHF_CONSONANT | PHF_STOP,   /* 'f' ($66) */
    [0x67] = PHF_CONSONANT | PHF_NASAL | PHF_VOICED,   /* 'g' ($67) */
    [0x68] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'h' ($68) */
    [0x69] = PHF_VOWEL | PHF_VOICED,   /* 'i' ($69) */
    [0x6A] = PHF_CONSONANT | PHF_FRICATIVE | PHF_VOICED,   /* 'j' ($6A) */
    [0x6B] = PHF_CONSONANT | PHF_STOP,   /* 'k' ($6B) */
    [0x6C] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'l' ($6C) */
    [0x6D] = PHF_CONSONANT | PHF_NASAL | PHF_LONG | PHF_VOICED,   /* 'm' ($6D) */
    [0x6E] = PHF_CONSONANT | PHF_NASAL | PHF_VOICED,   /* 'n' ($6E) */
    [0x6F] = PHF_VOWEL | PHF_VOICED,   /* 'o' ($6F) */
    [0x70] = PHF_CONSONANT | PHF_STOP,   /* 'p' ($70) */
    [0x71] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'q' ($71) */
    [0x72] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'r' ($72) */
    [0x73] = PHF_CONSONANT | PHF_FRICATIVE,   /* 's' ($73) */
    [0x74] = PHF_CONSONANT | PHF_FRICATIVE,   /* 't' ($74) */
    [0x75] = PHF_VOWEL | PHF_VOICED,   /* 'u' ($75) */
    [0x76] = PHF_VOWEL | PHF_VOICED,   /* 'v' ($76) */
    [0x77] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'w' ($77) */
    [0x78] = PHF_VOWEL | PHF_VOICED,   /* 'x' ($78) */
    [0x79] = PHF_VOWEL | PHF_LONG | PHF_VOICED,   /* 'y' ($79) */
    [0x7A] = PHF_CONSONANT | PHF_FRICATIVE | PHF_VOICED,   /* 'z' ($7A) */
    [0x83] = PHF_VOWEL | PHF_VOICED,   /* $83 */
    [0x84] = PHF_VOWEL | PHF_VOICED,   /* $84 */
    [0x87] = PHF_CONSONANT | PHF_STOP | PHF_VOICED,   /* $87 */
    [0x8C] = PHF_CONSONANT | PHF_LIQUID,   /* $8C */
    [0x98] = PHF_CONSONANT | PHF_STOP | PHF_VOICED,   /* $98 */
    [0xC0] = PHF_CONSONANT | PHF_LIQUID | PHF_LONG | PHF_VOICED,   /* $C0; also in the pointer slot of symbol $00 */
    [0xE0] = PHF_VOWEL | PHF_VOICED,   /* $E0; also in the pointer slot of symbol $10 */
    [0xE1] = PHF_VOWEL | PHF_VOICED,   /* $E1; also in the pointer slot of symbol $10 */
    [0xE2] = PHF_VOWEL | PHF_VOICED,   /* $E2; also in the pointer slot of symbol $11 */
};

/* The phoneme lists ($A187-$A396), by symbol: the (phoneme, max, min) triples ph_decode_symbol turns
   into segments (the phoneme numbers a row of the voice tables; the duration is min plus the scaled
   max - min). The encoder lays out the lists of $20-$FF end to end in code order, each with its $FF
   end, and writes the list pointers (PH_LIST_PTRS + 2 * symbol, $9DB4-$9F73) from where they land.
   Symbols not listed have an empty list (only the end) and make no segment. */
const fw_ph_list fw_ph_lists[256] = {
    /* the space: only the word-pause item, a pause of @Q length when @Q is not 0 */
    [0x20] = { .word_pause = true },   /* $20 */
    [0x30] = { 2, { { 50, 140, 70 }, { 54, 70, 30 } } },   /* '0' ($30) */
    [0x31] = { 1, { { 39, 90, 70 } } },   /* '1' ($31) */
    [0x32] = { 1, { { 70, 160, 60 } } },   /* '2' ($32) */
    [0x33] = { 1, { { 61, 80, 40 } } },   /* '3' ($33) */
    [0x34] = { 2, { { 53, 130, 70 }, { 49, 70, 30 } } },   /* '4' ($34) */
    [0x35] = { 2, { { 29, 130, 70 }, { 30, 70, 30 } } },   /* '5' ($35) */
    [0x36] = { 3, { { 31, 80, 60 }, { 34, 20, 20 }, { 35, 40, 30 } } },   /* '6' ($36) */
    [0x37] = { 1, { { 44, 170, 100 } } },   /* '7' ($37) */
    [0x39] = { 2, { { 51, 140, 80 }, { 52, 60, 30 } } },   /* '9' ($39) */
    [0x41] = { 2, { { 4, 130, 40 }, { 5, 110, 35 } } },   /* 'A' ($41) */
    [0x42] = { 2, { { 8, 90, 55 }, { 9, 25, 10 } } },   /* 'B' ($42) */
    [0x43] = { 3, { { 31, 80, 60 }, { 36, 20, 20 }, { 37, 40, 30 } } },   /* 'C' ($43) */
    [0x44] = { 2, { { 13, 50, 35 }, { 15, 25, 10 } } },   /* 'D' ($44) */
    [0x45] = { 1, { { 16, 150, 50 } } },   /* 'E' ($45) */
    [0x46] = { 1, { { 20, 130, 50 } } },   /* 'F' ($46) */
    [0x47] = { 2, { { 21, 65, 50 }, { 24, 30, 20 } } },   /* 'G' ($47) */
    [0x48] = { 1, { { 76, 160, 40 } } },   /* 'H' ($48) */
    [0x49] = { 2, { { 29, 140, 80 }, { 5, 110, 40 } } },   /* 'I' ($49) */
    [0x4A] = { 2, { { 27, 100, 55 }, { 28, 110, 45 } } },   /* 'J' ($4A) */
    [0x4B] = { 3, { { 31, 80, 60 }, { 32, 20, 20 }, { 33, 40, 30 } } },   /* 'K' ($4B) */
    [0x4C] = { 1, { { 38, 80, 40 } } },   /* 'L' ($4C) */
    [0x4D] = { 1, { { 42, 80, 60 } } },   /* 'M' ($4D) */
    [0x4E] = { 1, { { 44, 70, 60 } } },   /* 'N' ($4E) */
    [0x4F] = { 2, { { 48, 130, 40 }, { 46, 100, 35 } } },   /* 'O' ($4F) */
    [0x50] = { 3, { { 55, 80, 60 }, { 56, 20, 20 }, { 57, 30, 20 } } },   /* 'P' ($50) */
    [0x51] = { 1, { { 47, 140, 40 } } },   /* 'Q' ($51) */
    [0x52] = { 1, { { 60, 90, 50 } } },   /* 'R' ($52) */
    [0x53] = { 1, { { 62, 125, 80 } } },   /* 'S' ($53) */
    [0x54] = { 3, { { 64, 80, 40 }, { 65, 20, 20 }, { 66, 30, 20 } } },   /* 'T' ($54) */
    [0x55] = { 1, { { 69, 210, 70 } } },   /* 'U' ($55) */
    [0x56] = { 1, { { 71, 90, 50 } } },   /* 'V' ($56) */
    [0x57] = { 1, { { 72, 80, 60 } } },   /* 'W' ($57) */
    [0x58] = { 1, { { 59, 10, 10 } } },   /* 'X' ($58) */
    [0x59] = { 1, { { 73, 110, 60 } } },   /* 'Y' ($59) */
    [0x5A] = { 1, { { 74, 140, 40 } } },   /* 'Z' ($5A) */
    [0x61] = { 1, { { 0, 140, 40 } } },   /* 'a' ($61) */
    [0x62] = { 1, { { 16, 110, 60 } } },   /* 'b' ($62) */
    [0x63] = { 3, { { 10, 70, 50 }, { 11, 20, 20 }, { 12, 120, 40 } } },   /* 'c' ($63) */
    [0x64] = { 1, { { 14, 100, 35 } } },   /* 'd' ($64) */
    [0x65] = { 1, { { 17, 150, 70 } } },   /* 'e' ($65) */
    [0x66] = { 2, { { 64, 80, 40 }, { 65, 20, 20 } } },   /* 'f' ($66) */
    [0x67] = { 2, { { 44, 70, 30 }, { 21, 60, 20 } } },   /* 'g' ($67) */
    [0x68] = { 1, { { 2, 220, 70 } } },   /* 'h' ($68) */
    [0x69] = { 1, { { 26, 110, 40 } } },   /* 'i' ($69) */
    [0x6A] = { 2, { { 13, 70, 50 }, { 75, 100, 30 } } },   /* 'j' ($6A) */
    [0x6B] = { 2, { { 31, 80, 60 }, { 32, 20, 20 } } },   /* 'k' ($6B) */
    [0x6C] = { 2, { { 17, 145, 55 }, { 19, 125, 45 } } },   /* 'l' ($6C) */
    [0x6D] = { 1, { { 43, 170, 110 } } },   /* 'm' ($6D) */
    [0x6E] = { 1, { { 45, 130, 50 } } },   /* 'n' ($6E) */
    [0x6F] = { 1, { { 3, 210, 100 } } },   /* 'o' ($6F) */
    [0x70] = { 2, { { 55, 80, 60 }, { 56, 20, 20 } } },   /* 'p' ($70) */
    [0x71] = { 2, { { 46, 125, 55 }, { 1, 110, 45 } } },   /* 'q' ($71) */
    [0x72] = { 1, { { 18, 240, 50 } } },   /* 'r' ($72) */
    [0x73] = { 1, { { 63, 180, 80 } } },   /* 's' ($73) */
    [0x74] = { 1, { { 67, 160, 60 } } },   /* 't' ($74) */
    [0x75] = { 1, { { 68, 150, 40 } } },   /* 'u' ($75) */
    [0x76] = { 1, { { 46, 110, 50 } } },   /* 'v' ($76) */
    [0x77] = { 2, { { 50, 120, 50 }, { 49, 120, 40 } } },   /* 'w' ($77) */
    [0x78] = { 1, { { 1, 90, 40 } } },   /* 'x' ($78) */
    [0x79] = { 2, { { 51, 120, 50 }, { 5, 120, 40 } } },   /* 'y' ($79) */
    [0x7A] = { 1, { { 75, 70, 40 } } },   /* 'z' ($7A) */
    /* the only list with phoneme 7; only @X phonetic text reaches it */
    [0x83] = { 1, { { 7, 90, 50 } } },   /* $83 */
    /* the only list with phoneme 6; only @X phonetic text reaches it */
    [0x84] = { 1, { { 6, 90, 50 } } },   /* $84 */
    /* the only list with phoneme 22; only @X phonetic text reaches it */
    [0x87] = { 2, { { 21, 65, 50 }, { 22, 30, 20 } } },   /* $87 */
    /* the only list with phoneme 40; only @X phonetic text reaches it */
    [0x8C] = { 1, { { 40, 80, 40 } } },   /* $8C */
    /* the only list with phoneme 23; from letter-to-sound rules (MEGATON) */
    [0x98] = { 2, { { 21, 65, 50 }, { 23, 30, 20 } } },   /* $98 */
    /* the only list with phoneme 41, a long syllabic consonant; from letter-to-sound rules (LEVEL) */
    [0xC0] = { 1, { { 41, 220, 100 } } },   /* $C0 */
};

/* ph_dur_lengthen ($C67B): one lengthening step, x to about 1.1 * x (less than 1.5 off), 255 from
   x = 233 on. ph_decode_symbol applies it ph_lengthen_count times to a duration range. */
const uint8_t fw_ph_dur_lengthen[256] = {
      0,   1,   2,   3,   4,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  17,   /* x = 0-15 */
     18,  19,  20,  21,  22,  23,  24,  25,  26,  28,  29,  30,  31,  32,  33,  34,   /* x = 16-31 */
     35,  36,  37,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  50,  51,  52,   /* x = 32-47 */
     53,  54,  55,  56,  57,  58,  59,  61,  62,  63,  64,  65,  66,  67,  68,  69,   /* x = 48-63 */
     70,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  83,  84,  85,  86,  87,   /* x = 64-79 */
     88,  89,  90,  91,  92,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 105,   /* x = 80-95 */
    106, 107, 108, 109, 110, 111, 112, 113, 114, 116, 117, 118, 119, 120, 121, 122,   /* x = 96-111 */
    123, 124, 125, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 138, 139, 140,   /* x = 112-127 */
    141, 142, 143, 144, 145, 146, 147, 149, 150, 151, 152, 153, 154, 155, 156, 157,   /* x = 128-143 */
    158, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174,   /* x = 144-159 */
    175, 176, 177, 178, 179, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 192,   /* x = 160-175 */
    193, 194, 195, 196, 197, 198, 199, 200, 201, 203, 204, 205, 206, 207, 208, 209,   /* x = 176-191 */
    210, 211, 212, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 225, 226, 227,   /* x = 192-207 */
    228, 229, 230, 231, 232, 233, 234, 236, 237, 238, 239, 240, 241, 242, 243, 244,   /* x = 208-223 */
    245, 247, 248, 249, 250, 251, 252, 253, 254, 255, 255, 255, 255, 255, 255, 255,   /* x = 224-239 */
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   /* x = 240-255 */
};
/* ph_dur_shorten ($C77B): one shortening step, x to about 0.9 * x (less than 1 off).
   ph_decode_symbol applies it (shortening count + 1) / 2 times to a duration range. */
const uint8_t fw_ph_dur_shorten[256] = {
      0,   1,   2,   3,   4,   5,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,   /* x = 0-15 */
     14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  23,  24,  25,  26,  27,  28,   /* x = 16-31 */
     29,  30,  31,  32,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  41,  42,   /* x = 32-47 */
     43,  44,  45,  46,  47,  48,  49,  50,  50,  51,  52,  53,  54,  55,  56,  57,   /* x = 48-63 */
     58,  59,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  68,  69,  70,  71,   /* x = 64-79 */
     72,  73,  74,  75,  76,  77,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,   /* x = 80-95 */
     86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  95,  96,  97,  98,  99, 100,   /* x = 96-111 */
    101, 102, 103, 104, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 113, 114,   /* x = 112-127 */
    115, 116, 117, 118, 119, 120, 121, 122, 122, 123, 124, 125, 126, 127, 128, 129,   /* x = 128-143 */
    130, 131, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 140, 141, 142, 143,   /* x = 144-159 */
    144, 145, 146, 147, 148, 149, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158,   /* x = 160-175 */
    158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 167, 168, 169, 170, 171, 172,   /* x = 176-191 */
    173, 174, 175, 176, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 185, 186,   /* x = 192-207 */
    187, 188, 189, 190, 191, 192, 193, 194, 195, 195, 196, 197, 198, 199, 200, 201,   /* x = 208-223 */
    202, 203, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 212, 213, 214, 215,   /* x = 224-239 */
    216, 217, 218, 219, 220, 221, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230,   /* x = 240-255 */
};

/* cmd_speed_map_data ($C898): per speed 1-9 (cmd_speed_maps) a map of 32 durations in frames:
   ph_decode_symbol reads entry duration / 8 into ph_duration. It ends where ph_append_phrase_pause
   begins. */
const uint8_t fw_cmd_speed_map_data[9][32] = {
    {   /* speed 1 ($C898) */
          1,   2,   4,   6,   8,  10,  12,  14,  16,  18,  20,  22,  24,  26,  28,  30,   /* duration / 8 = 0-15 */
         32,  34,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,  58,  60,  62,   /* duration / 8 = 16-31 */
    },
    {   /* speed 2 ($C8B8) */
          1,   1,   2,   2,   3,   4,   5,   6,   6,   7,   8,   9,  10,  10,  11,  12,   /* duration / 8 = 0-15 */
         13,  14,  14,  15,  16,  17,  18,  18,  19,  20,  21,  22,  22,  23,  24,  25,   /* duration / 8 = 16-31 */
    },
    {   /* speed 3 ($C8D8) */
          1,   1,   2,   2,   3,   3,   4,   5,   5,   6,   7,   7,   8,   8,   9,   9,   /* duration / 8 = 0-15 */
         10,  10,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  15,  16,  16,   /* duration / 8 = 16-31 */
    },
    {   /* speed 4 ($C8F8) */
          1,   1,   2,   2,   3,   3,   4,   4,   5,   5,   5,   6,   6,   7,   7,   7,   /* duration / 8 = 0-15 */
          8,   8,   8,   9,   9,   9,  10,  10,  10,  11,  11,  11,  12,  12,  12,  13,   /* duration / 8 = 16-31 */
    },
    {   /* speed 5 ($C918) */
          1,   1,   2,   2,   3,   3,   3,   3,   4,   4,   4,   4,   4,   5,   5,   5,   /* duration / 8 = 0-15 */
          5,   5,   5,   6,   6,   6,   6,   6,   6,   6,   6,   7,   7,   7,   7,   7,   /* duration / 8 = 16-31 */
    },
    {   /* speed 6 ($C938) */
          1,   1,   1,   2,   2,   2,   2,   3,   3,   3,   3,   3,   4,   4,   4,   4,   /* duration / 8 = 0-15 */
          4,   4,   4,   4,   4,   4,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   /* duration / 8 = 16-31 */
    },
    {   /* speed 7 ($C958) */
          1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   /* duration / 8 = 0-15 */
          3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,   /* duration / 8 = 16-31 */
    },
    {   /* speed 8 ($C978) */
          1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   /* duration / 8 = 0-15 */
          2,   2,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   /* duration / 8 = 16-31 */
    },
    {   /* speed 9 ($C998) */
          1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   /* duration / 8 = 0-15 */
          1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   /* duration / 8 = 16-31 */
    },
};

/* ph_lengthen_steps ($D44A, the base ph_lengthen_count indexes with A = 0-30; annotated from $D44B):
   the number of ph_dur_lengthen steps for a half count A. Entry 0 is the RTS that ends
   ph_lengthen_count (LDA $D44A,Y / TAY / RTS): A = 0, from a lengthening count of $FF, reads the
   opcode as 96 steps. */
const uint8_t fw_ph_lengthen_steps[31] = {
    0x60,   /* A = 0: the RTS opcode at $D44A, read as data */
      1,   2,   3,   4,   4,   5,   6,   6,   7,   7,   8,   9,   9,   9,  10,   /* A = 1-15 */
     10,  10,  11,  11,  12,  12,  12,  13,  13,  13,  13,  14,  14,  14,  15,   /* A = 16-30 */
};
```

If the tool's output differs from this, the ROM wins. Commit the tool's output and report the difference.

- [ ] **Step 5: Encoder step 8 in `src/firmware/data/fw_data.c`**

In `fw_image_build`, replace the end of step 7 and the return:

```c
        put(&e, false, PAR_VOICE_A_COUNT, fw_par_voice_a_count);
        put(&e, false, PAR_VOICE_B_COUNT, fw_par_voice_b_count);
    }
    return e.ok;
}
```

with:

```c
        put(&e, false, PAR_VOICE_A_COUNT, fw_par_voice_a_count);
        put(&e, false, PAR_VOICE_B_COUNT, fw_par_voice_b_count);
    }
    /* 8. The prosody data (M4.3a spec 3): the symbol classes and features, the phoneme lists end to end with the
       list pointers computed from where they land, the duration steps, the speed maps and ph_lengthen_steps. */
    {
        for (int s = 0; s < 256; s++) {
            put(&e, false, PH_SYMBOL_CLASS + (uint32_t)s, fw_ph_symbol_class[s]);
            put(&e, false, PH_SYMBOL_FEATURES + (uint32_t)s, fw_ph_symbol_features[s]);
        }
        /* The pointer slots of the symbols $00-$1F are the features of $C0-$FF, written just above: those symbols
           must have no class and no list, so that the decoder never reads them as pointers. */
        check_end(&e, "ph_symbol_features", PH_SYMBOL_FEATURES + 256, PH_LIST_PTRS + 2 * 0x20);
        for (int s = 0; s < 0x20; s++)
            if (fw_ph_symbol_class[s] || fw_ph_lists[s].count || fw_ph_lists[s].word_pause)
                fail(&e, "symbol $%02X has a class or a list, but its pointer slot is a feature byte", (unsigned)s);
        uint32_t at = PH_LISTS;
        for (int s = 0x20; s < 256; s++) {
            const fw_ph_list *l = &fw_ph_lists[s];
            put_word(&e, PH_LIST_PTRS + 2u * (uint32_t)s, (uint16_t)at);
            if (l->count > 3 || (l->word_pause && l->count))
                fail(&e, "phoneme list of $%02X: %u triples%s", (unsigned)s, (unsigned)l->count,
                     l->word_pause ? " after the word-pause item" : "");
            if (l->word_pause)
                put(&e, false, at++, PH_WORD_PAUSE);
            for (unsigned i = 0; i < l->count && i < 3; i++) {
                const fw_ph_triple *t = &l->item[i];
                if (t->phoneme == 0xFF || t->phoneme == PH_WORD_PAUSE || t->max < t->min)
                    fail(&e, "phoneme list of $%02X, triple %u: phoneme $%02X, max %u, min %u", (unsigned)s, i,
                         (unsigned)t->phoneme, (unsigned)t->max, (unsigned)t->min);
                put(&e, false, at++, t->phoneme);
                put(&e, false, at++, t->max);
                put(&e, false, at++, t->min);
            }
            put(&e, false, at++, 0xFF);
        }
        check_end(&e, "the phoneme lists", at, UNUSED_A397);
        for (int x = 0; x < 256; x++) {
            put(&e, false, PH_DUR_LENGTHEN + (uint32_t)x, fw_ph_dur_lengthen[x]);
            put(&e, false, PH_DUR_SHORTEN + (uint32_t)x, fw_ph_dur_shorten[x]);
        }
        for (int k = 0; k < 9; k++)
            for (int d = 0; d < 32; d++)
                put(&e, false, CMD_SPEED_MAP_DATA + 32u * (uint32_t)k + (uint32_t)d, fw_cmd_speed_map_data[k][d]);
        for (int i = 0; i < 10; i++) {   /* M4.2's cmd_speed_maps must point at the start of a map */
            uint32_t p = fw_cmd_speed_maps[i];
            if (p < CMD_SPEED_MAP_DATA || (p - CMD_SPEED_MAP_DATA) % 32 || p - CMD_SPEED_MAP_DATA >= 9 * 32)
                fail(&e, "cmd_speed_maps entry %d ($%04X) is not the start of a speed map", i, (unsigned)p);
        }
        for (int i = 0; i < 31; i++)
            put(&e, false, PH_LENGTHEN_STEPS + (uint32_t)i, fw_ph_lengthen_steps[i]);
        check_end(&e, "ph_lengthen_steps", PH_LENGTHEN_STEPS + 31, PAR_VOICE_A_COUNT);
    }
    return e.ok;
}
```

Notes for the reviewer: the pointer slots of $00–$1F are never written by the pointer loop (it starts at $20), and `put` fails on a second write of any byte, so the features of $C0–$FF are written once; the loop over $00–$1F asserts the reason they may be (no class, no list). `put_word` writes each pointer from where the list lands, so no list address is stated in the data. The triple loop is bounded by `i < 3` even when `count` is wrong (that is reported by `fail`), so it never reads past `item[3]`. The `cmd_speed_maps` check ties M4.2's pointers to the maps now converted; `p - CMD_SPEED_MAP_DATA` is computed only after `p >= CMD_SPEED_MAP_DATA`.

- [ ] **Step 6: Run the tests**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: `38197 converted bytes compared with the ROM, 0 differ` and `suite_firmware_data          ok`. (The `build.bat` syntax check of `src\firmware` without `/Isrc` passes: `pros_data.c` includes only `"fw_data.h"`.)

Then run: `cmd //c ".\build.bat test"`
Expected: `30 suite(s), 0 failure(s), 0 skip(s)`, with no compiler warnings; `golden_hybrid` prints `39 corpus files x 17 modes (all, then each routine alone)` (Task 1's count; the C does not read the new data yet, and the image's other bytes are unchanged, so `fw_lts`, `fw_tn`, `fw_cmd`, `diff_lts` and `golden_hybrid` are unaffected). The two `warning: zeros ...` / `warning: flipped ...` lines of `suite_rom_id` are expected, as before.

- [ ] **Step 7: Commit**

```bash
git add src/firmware/data/pros_data.c src/firmware/data/fw_data.h src/firmware/data/fw_data.c tools/extract_tables.py tests/test_firmware_data.c
git commit -m "Converted data: the prosody tables (symbol classes and features, phoneme lists with generated pointers, duration steps, speed maps, lengthen steps) as readable C (M4.3a 3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The segment appenders in C, their hooks, and `tests/test_fw_pros.c` (spec §2.2, §2.3, §2.5, §2.7, §4.4)

**Files:**
- Create: `src/firmware/pros_addr.h`, `src/firmware/pros_seg.h`, `src/firmware/pros_seg.c`, `tests/test_fw_pros.c`
- Modify: `src/engine/thunks.c`, `tests/suites.h` (append `SUITE(suite_fw_pros)`)

**Interfaces:**
- Consumes:
  - M4.0–M4.2: `fw_rom_call`, `fw_regs`, `FW_ROM_CALL_BUDGET`, `fw_rd8`/`fw_wr8`/`fw_rd16`/`fw_wr16` (`fw.h`); `ROM_ISR_PAR_READY` (`tn_convert.h`); the RAM names `PROS_PHONEME_PTR`..`PROS_FLAGS_PTR`, `PROS_SEG_PHONEME`..`PROS_SEG_FLAGS`, `PROS_SEG_COUNT`, `PH_PHONEME`, `PH_DURATION`, `PROS_TARGET`, `PROS_LAST_TARGET`, `PROS_SYMBOL`, `CMD_PITCH_STEP`, `CMD_PHRASE_PAUSE` (`fw_ram.h`); `fw_replacement_find`, `FW_OUT_*` (`engine/thunks.h`);
  - Task 2: the converted image (`fw_image_get()`), which `setup()` loads. Nothing in this task reads ROM data.
- Produces (used by Tasks 4 and 5):

```c
/* pros_addr.h: ROM_PH_APPEND_PHRASE_PAUSE 0xC9B8, ROM_PH_APPEND_PAUSE 0xCA15, ROM_PROS_APPEND_SEGMENT 0xCA3B,
   ROM_PROS_SEG_TAIL 0xCA6D, ROM_PROS_RESET_SEG_PTRS 0xCA71, ROM_PROS_PHRASE_GEN 0xD129, ROM_PH_DECODE_SYMBOL 0xD31B,
   ROM_PROS_DECLINATION 0xD3DD, ROM_PROS_DECLINATION_STEP 0xD40D, ROM_PH_LENGTHEN_COUNT 0xD43E */
/* pros_seg.h */
#define PH_PAUSE_PHONEME    0x3B
#define PROS_SEG_FLAGS_GEN  0x80
void pros_reset_seg_ptrs(fw *f);
void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y);
uint8_t ph_append_pause(fw *f, uint8_t a);
void ph_append_phrase_pause(fw *f);
```

  - The replacements `"ph_append_phrase_pause"` `0xC9B8`, `"ph_append_pause"` `0xCA15`, `"pros_append_segment"` `0xCA3B` and `"pros_reset_seg_ptrs"` `0xCA71`, all with outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y` (20 routines in all).
  - `tests/test_fw_pros.c` and its one harness, which Tasks 4 and 5 reuse and extend (they define no mock or setup of their own): `MAX_CALLS`, `rom_call_record`, `g_ram` (64 KiB), `g_f`, `g_host`, `g_calls`, `g_ncalls`, `mock_rom_call` (records every call; `ROM_ISR_PAR_READY` returns the registers unchanged; any other address fails), `setup()`, `count(addr)`, `check_call(i, addr, a, x, y)` (strict budget), `check_row(name, addr, outputs)`, `seg_state(index, pages)`, `check_segment(ofs, phoneme, duration, pitch)`, `check_pages(pages)`.

**ROM facts checked** (`rom_CA71_pros_reset_seg_ptrs.txt`, `rom_CA3B_pros_append_segment.txt`, `rom_CA15_ph_append_pause.txt`, `rom_C9B8_ph_append_phrase_pause.txt`, `rom_C5D6_isr_par_ready.txt` with its tail `$C5EA` in `rom_C5DE_isr_par_hold.txt`, and the callers `rom_D129_pros_phrase_gen.txt`, `rom_CA92_pros_make_segments.txt`, `rom_CABD_pros_mark_stress.txt`, `rom_BAF2_par_play_start.txt`, `rom_BDD3_pros_scale_target.txt`):
- **Callers.** A scan of the ROM image for `JSR`/`JMP` operands finds exactly those in `functions.txt`: `$CA71` from `$BAF5` and `$CAB0`; `$CA3B` from `$D3CC`; `$CA15` from `$D190`, `$D193`, `$D352`; `$C9B8` from `$D1DA`, `$D1DD`; `$CA6D` only from `ph_append_phrase_pause`'s `JMP` at `$CA12`. No branch from elsewhere enters these routines.
- **`ph_append_pause` leaves A = $80, and `pros_phrase_gen` reads it.** At the end of the text, `LDA #4 / JSR $CA15 / JSR $CA15`: the second call takes the `$80` that the first leaves (its last `LDA #$80`) as its duration. So the utterance ends with a 4-frame pause and then a **128-frame** pause; the dump comment of `pros_phrase_gen` ("two pauses of 4 frames") and the survey are wrong about the second. After a word pause (`$D352`) the `$80` goes on as A to `isr_par_ready` at `$D155` (`LDM` does not change A). So A is a declared output, and Task 5's C must pass `$80` to its second end pause.
- **`pros_phrase_gen`'s exit Y comes from `ph_append_pause`.** `$D196`–`$D1A1` (`LDA`, `BNE`, `DEC`, `DEC`, `RTS`) change neither X nor Y, so `pros_phrase_gen` returns with Y = the old `pros_seg_count` low byte that the second end pause leaves, and X = `pros_pos`. (The conventions call that Y "the last list index"; Task 5 should take it from `ph_append_pause`'s return.)
- **`ph_append_phrase_pause` durations:** `.` `?` `!` take `cmd_phrase_pause` **in full, with no `AND #$0F`**; `,` `;` `:` take `LSR / AND #$0F`; every other symbol `LSR / LSR / AND #$0F`. The `CMP #0 / BNE / INC A` at `$C9E8`–`$C9EC` follows all three paths, so the mask can make 0 and 0 becomes 1 (`@D` $20 after `,` gives 1). `INC A` is the 740's `$3A`, accumulator only.
- **The pause body** (`$C9ED`–`$CA11` is the same code as `$CA15`–`$CA39`): `LDY pros_seg_count` (low byte only), duration through `($24),Y`, phoneme `$3B` through `($22),Y` **and into `ph_phoneme` ($1C01)**, `pros_last_target` through `($26),Y`, `$80` through `($28),Y`. It reads `pros_last_target`; it never writes it, and it never calls `isr_par_ready`.
- **`pros_append_segment`** calls `isr_par_ready` **first**, with the entry A, X and Y; only then `PHA / TYA / PHA`. `isr_par_ready` (`PHA`, `BBC0 boot_dip`, `SEB3 P0`, `PLA`, `RTS`) preserves A, X and Y, so the A and Y pulled at `$CA6D` are the entry ones. It stores `ph_phoneme`, `ph_duration`, `pros_last_target` = `pros_target + cmd_pitch_step` (`CLC / ADC`, 8 bits) as the pitch, and `$80`.
- **The 16-bit index.** `INC pros_seg_count / BNE`; on the wrap, `INC pros_seg_count+1`, then the pointer high bytes in the order `$23` (phoneme), `$27` (pitch), `$25` (duration), `$29` (flags), at `$CA07`, `$CA2F` and `$CA62` alike. The order is not visible in the final RAM; the C keeps it anyway. `(zp),Y` is a full 16-bit sum (`pros_phoneme_ptr` `$02F0` with Y = `$20` stores at `$0310`).
- **`pros_reset_seg_ptrs`** writes the four pointers low byte first and leaves A = `$0E`; it does not touch X or Y.
- **What the callers read after each call** (and so the declared outputs; no caller reads a flag, since an `LDA`, `LDY` or `PHA`-then-`LDA` follows every call):
  - `ph_append_phrase_pause`: two calls in a row, then `LDA #0`; X and Y then reach `isr_par_ready` at `$D155` through `JMP $D152` (`$D209`, `$D30F`). A, X, Y are as they came in. Declared: A, X, Y.
  - `ph_append_pause`: A (above), and Y and X leave `pros_phrase_gen` as its own outputs. Declared: A, X, Y.
  - `pros_append_segment`: `LDA pros_sym_flag` follows; on the `BBC4` path to `$D152`, X and Y reach `isr_par_ready` at `$D155`. Declared: A, X, Y.
  - `pros_reset_seg_ptrs`: `pros_make_segments` calls `pros_mark_stress`, whose first `isr_par_ready` (`$CB1F`) gets this Y (X is written first, `TAX` at `$CAEC`); `par_play_start` loads A next, and `pros_scale_target` pushes X and Y. Declared: A, X, Y (the spec's table names A = `$0E`; X and Y are declared too, the stricter choice).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_fw_pros.c`:

```c
/* Unit checks of the segment generator on the converted data (M4.3a spec 4.4), with a mock host that records every
   ROM call. They reach what no corpus text reaches reliably (a page wrap at every append point, the phrase pause
   that computes 0). The full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid);
   the thunks' register outputs are compared there, against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/pros_addr.h"
#include "firmware/pros_seg.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 64

typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call; isr_par_ready returns the registers unchanged (it preserves A, X and Y), any other call
   fails. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r->a;
        c->x = r->x;
        c->y = r->y;
        c->budget = budget;
    }
    g_ncalls++;
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the segment generator drives P2 */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Record i is a call of addr with A, X, Y and the strict budget. */
static void check_call(unsigned i, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].addr, addr);
    CHECK_EQ(g_calls[i].a, a);
    CHECK_EQ(g_calls[i].x, x);
    CHECK_EQ(g_calls[i].y, y);
    CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}

/* The hook of name sits at addr and declares outputs (pinned here; diff_lts compares them against the ROM). */
static void check_row(const char *name, uint16_t addr, uint8_t outputs)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, outputs);
}

/* pros_seg_count = index; the four pointers at the arrays plus `pages` pages, as `pages` wraps leave them. */
static void seg_state(uint16_t index, uint8_t pages)
{
    fw_wr16(&g_f, PROS_SEG_COUNT, index);
    fw_wr16(&g_f, PROS_PHONEME_PTR, (uint16_t)(PROS_SEG_PHONEME + 0x100 * pages));
    fw_wr16(&g_f, PROS_DURATION_PTR, (uint16_t)(PROS_SEG_DURATION + 0x100 * pages));
    fw_wr16(&g_f, PROS_PITCH_PTR, (uint16_t)(PROS_SEG_PITCH + 0x100 * pages));
    fw_wr16(&g_f, PROS_FLAGS_PTR, (uint16_t)(PROS_SEG_FLAGS + 0x100 * pages));
}

/* The segment at offset ofs of each array is (phoneme, duration, pitch, $80). */
static void check_segment(uint16_t ofs, uint8_t phoneme, uint8_t duration, uint8_t pitch)
{
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + ofs], phoneme);
    CHECK_EQ(g_ram[PROS_SEG_DURATION + ofs], duration);
    CHECK_EQ(g_ram[PROS_SEG_PITCH + ofs], pitch);
    CHECK_EQ(g_ram[PROS_SEG_FLAGS + ofs], PROS_SEG_FLAGS_GEN);
}

/* The four pointers are at the arrays plus `pages` pages, low bytes 0, and the bytes either side untouched. */
static void check_pages(uint8_t pages)
{
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME + 0x100 * pages);
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION + 0x100 * pages);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH + 0x100 * pages);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS + 0x100 * pages);
    CHECK_EQ(g_ram[PROS_PHONEME_PTR - 1], 0);
    CHECK_EQ(g_ram[PROS_FLAGS_PTR + 2], 0);
}

static void test_seg_rows(void)
{
    check_row("ph_append_phrase_pause", ROM_PH_APPEND_PHRASE_PAUSE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("ph_append_pause", ROM_PH_APPEND_PAUSE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_append_segment", ROM_PROS_APPEND_SEGMENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_reset_seg_ptrs", ROM_PROS_RESET_SEG_PTRS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

static void test_reset_seg_ptrs(void)
{
    setup();
    memset(g_ram + PROS_PHONEME_PTR, 0x5A, 8);
    pros_reset_seg_ptrs(&g_f);
    check_pages(0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_append_segment(void)
{
    setup();
    seg_state(0x0105, 1);   /* segment $105: Y = 5 through pointers one page on */
    g_ram[PH_PHONEME] = 0x12;
    g_ram[PH_DURATION] = 0x09;
    g_ram[PROS_TARGET] = 0x70;
    g_ram[CMD_PITCH_STEP] = 0x08;
    pros_append_segment(&g_f, 0xA1, 0xB2, 0xC3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_PAR_READY, 0xA1, 0xB2, 0xC3);   /* first, with the entry registers */
    check_segment(0x105, 0x12, 0x09, 0x78);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x78);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0106);
    check_pages(1);
    CHECK_EQ(g_ram[PH_PHONEME], 0x12);   /* read, not written */
    CHECK(!g_f.fault);

    /* the pitch sum is 8 bits: $FC + 8 = $04, the carry dropped */
    setup();
    seg_state(0x0000, 0);
    g_ram[PROS_TARGET] = 0xFC;
    g_ram[CMD_PITCH_STEP] = 0x08;
    pros_append_segment(&g_f, 0, 0, 0);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x04);
    CHECK_EQ(g_ram[PROS_SEG_PITCH], 0x04);

    /* (ptr),Y is a 16-bit sum: pros_phoneme_ptr = $02F0 with Y = $20 stores at $0310 */
    setup();
    seg_state(0x0020, 0);
    fw_wr16(&g_f, PROS_PHONEME_PTR, 0x02F0);
    g_ram[PH_PHONEME] = 0x33;
    pros_append_segment(&g_f, 0, 0, 0);
    CHECK_EQ(g_ram[0x0310], 0x33);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + 0x20], 0x00);
}

/* $CA62: the index wraps at a segment: both bytes of pros_seg_count and all four pointers move on */
static void test_append_segment_wrap(void)
{
    setup();
    seg_state(0x01FF, 1);
    g_ram[PH_PHONEME] = 0x21;
    g_ram[PH_DURATION] = 0x05;
    g_ram[PROS_TARGET] = 0x60;
    pros_append_segment(&g_f, 0x11, 0x22, 0x33);
    check_call(0, ROM_ISR_PAR_READY, 0x11, 0x22, 0x33);
    check_segment(0x1FF, 0x21, 0x05, 0x60);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0200);
    check_pages(2);
    CHECK(!g_f.fault);
}

static void test_append_pause(void)
{
    setup();
    seg_state(0x0010, 0);
    g_ram[PROS_LAST_TARGET] = 0x66;
    g_ram[PROS_TARGET] = 0x99;                  /* a pause repeats pros_last_target, not pros_target */
    CHECK_EQ(ph_append_pause(&g_f, 7), 0x10);   /* Y = the old index low byte */
    check_segment(0x10, PH_PAUSE_PHONEME, 7, 0x66);
    CHECK_EQ(g_ram[PH_PHONEME], PH_PAUSE_PHONEME);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x66);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0011);
    check_pages(0);
    CHECK_EQ(g_ncalls, 0);                      /* no isr_par_ready in a pause */
    CHECK(!g_f.fault);
}

/* $CA2F: the index wraps at a word pause */
static void test_append_pause_wrap(void)
{
    setup();
    seg_state(0x00FF, 0);
    g_ram[PROS_LAST_TARGET] = 0x70;
    CHECK_EQ(ph_append_pause(&g_f, 4), 0xFF);
    check_segment(0xFF, PH_PAUSE_PHONEME, 4, 0x70);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0100);
    check_pages(1);
    CHECK(!g_f.fault);
}

/* One phrase pause after symbol with cmd_phrase_pause = d, at index 0: returns its duration. */
static uint8_t phrase_pause(char symbol, uint8_t d)
{
    setup();
    seg_state(0x0000, 0);
    g_ram[PROS_SYMBOL] = (uint8_t)symbol;
    g_ram[CMD_PHRASE_PAUSE] = d;
    g_ram[PROS_LAST_TARGET] = 0x55;
    ph_append_phrase_pause(&g_f);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME], PH_PAUSE_PHONEME);
    CHECK_EQ(g_ram[PH_PHONEME], PH_PAUSE_PHONEME);
    CHECK_EQ(g_ram[PROS_SEG_PITCH], 0x55);
    CHECK_EQ(g_ram[PROS_SEG_FLAGS], PROS_SEG_FLAGS_GEN);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0001);
    CHECK_EQ(g_ram[CMD_PHRASE_PAUSE], d);       /* read, not written */
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
    return g_ram[PROS_SEG_DURATION];
}

/* . ? ! in full (no mask: $F6 stays $F6); , ; : (d >> 1) & $0F; any other symbol (d >> 2) & $0F */
static void test_phrase_pause_durations(void)
{
    static const char full[] = ".?!", half[] = ",;:", other[] = "()-\"[] X";
    for (const char *s = full; *s; s++)
        CHECK_EQ(phrase_pause(*s, 0xF6), 0xF6);
    for (const char *s = half; *s; s++)
        CHECK_EQ(phrase_pause(*s, 0xF6), 0x0B);   /* $7B & $0F */
    for (const char *s = other; *s; s++)
        CHECK_EQ(phrase_pause(*s, 0xF6), 0x0D);   /* $3D & $0F */
    CHECK_EQ(phrase_pause(',', 0x09), 0x04);
    CHECK_EQ(phrase_pause('-', 0x09), 0x02);
}

/* $C9EC: a duration that computes 0 becomes 1, on every path (@D0 itself never gets here: pros_phrase_gen appends
   no phrase pause when cmd_phrase_pause is 0, $D1D8) */
static void test_phrase_pause_zero(void)
{
    CHECK_EQ(phrase_pause('.', 0x00), 1);
    CHECK_EQ(phrase_pause(',', 0x01), 1);   /* 1 >> 1 */
    CHECK_EQ(phrase_pause('(', 0x03), 1);   /* 3 >> 2 */
    CHECK_EQ(phrase_pause(';', 0x20), 1);   /* $10 & $0F: the mask makes 0 */
    CHECK_EQ(phrase_pause('"', 0x40), 1);   /* $10 & $0F */
    CHECK_EQ(phrase_pause(',', 0x02), 1);   /* 1, not bumped */
    CHECK_EQ(phrase_pause('(', 0x08), 2);
}

/* $CA07: the index wraps at a phrase pause */
static void test_phrase_pause_wrap(void)
{
    setup();
    seg_state(0x02FF, 2);
    g_ram[PROS_SYMBOL] = '.';
    g_ram[CMD_PHRASE_PAUSE] = 0x0C;
    g_ram[PROS_LAST_TARGET] = 0x81;
    ph_append_phrase_pause(&g_f);
    check_segment(0x2FF, PH_PAUSE_PHONEME, 0x0C, 0x81);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0300);
    check_pages(3);
    CHECK(!g_f.fault);
}

/* A word pause wraps the index, then a segment lands on the next page through the moved pointers, at Y = 0 */
static void test_append_across_wrap(void)
{
    setup();
    seg_state(0x00FF, 0);
    g_ram[PROS_LAST_TARGET] = 0x40;
    g_ram[PH_DURATION] = 0x03;
    g_ram[PROS_TARGET] = 0x50;
    CHECK_EQ(ph_append_pause(&g_f, 9), 0xFF);
    g_ram[PH_PHONEME] = 0x07;                   /* the pause left $3B there; the decoder stores each phoneme */
    pros_append_segment(&g_f, 0x80, 1, 2);
    check_segment(0xFF, PH_PAUSE_PHONEME, 9, 0x40);
    check_segment(0x100, 0x07, 0x03, 0x50);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0101);
    check_pages(1);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1);
    check_call(0, ROM_ISR_PAR_READY, 0x80, 1, 2);
    CHECK(!g_f.fault);
}

void suite_fw_pros(void)
{
    CHECK(fw_image_get() != NULL);
    test_seg_rows();
    test_reset_seg_ptrs();
    test_append_segment();
    test_append_segment_wrap();
    test_append_pause();
    test_append_pause_wrap();
    test_phrase_pause_durations();
    test_phrase_pause_zero();
    test_phrase_pause_wrap();
    test_append_across_wrap();
}
```

In `tests/suites.h`, append after `SUITE(suite_fw_cmd)`:

```c
SUITE(suite_fw_pros)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_pros"`
Expected: a compile error, because `firmware/pros_addr.h` (and `firmware/pros_seg.h`) does not exist.

- [ ] **Step 3: Implement**

Create `src/firmware/pros_addr.h`:

```c
#ifndef FW_PROS_ADDR_H
#define FW_PROS_ADDR_H
/* ROM code addresses of the segment generator (M4.3a spec 2.2), checked against build/ghidra740/dump and the
   JSR/JMP operands in the ROM image. isr_par_ready, the one ROM routine they call, is ROM_ISR_PAR_READY in
   tn_convert.h. */
#define ROM_PH_APPEND_PHRASE_PAUSE  0xC9B8   /* exits with JMP ROM_PROS_SEG_TAIL */
#define ROM_PH_APPEND_PAUSE         0xCA15
#define ROM_PROS_APPEND_SEGMENT     0xCA3B
#define ROM_PROS_SEG_TAIL           0xCA6D   /* pros_append_segment's PLA / TAY / PLA / RTS (restores Y and A) */
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
#define ROM_PROS_PHRASE_GEN         0xD129   /* called by pros_make_segments $CAB9 */
#define ROM_PH_DECODE_SYMBOL        0xD31B   /* a label inside pros_phrase_gen, not a routine */
#define ROM_PROS_DECLINATION        0xD3DD   /* called by pros_mark_phrases $D04D and pros_phrase_gen $D129 */
#define ROM_PROS_DECLINATION_STEP   0xD40D   /* JSR from $D3F2, and BCC/BEQ from $D3E3/$D3E5 (a tail-branch) */
#define ROM_PH_LENGTHEN_COUNT       0xD43E

#endif
```

Create `src/firmware/pros_seg.h`:

```c
#ifndef FW_PROS_SEG_H
#define FW_PROS_SEG_H
/* The segment appenders of the segment generator (ROM $C9B8-$CA91), decompiled (M4.3a spec 2.2, 2.5). A segment
   is one byte in each of the four arrays pros_seg_phoneme, pros_seg_duration, pros_seg_pitch and pros_seg_flags,
   stored through the page pointers pros_phoneme_ptr..pros_flags_ptr at Y = the low byte of the 16-bit index
   pros_seg_count. When that byte wraps to 0, the high byte and all four pointers move on one page. */
#include "fw.h"

#define PH_PAUSE_PHONEME    0x3B   /* the phoneme of every pause segment */
#define PROS_SEG_FLAGS_GEN  0x80   /* the flags byte of every generated segment (playback never reads it) */

/* $CA71: pros_phoneme_ptr = pros_seg_phoneme ($0200), pros_duration_ptr = pros_seg_duration ($0600),
   pros_pitch_ptr = pros_seg_pitch ($0A00), pros_flags_ptr = pros_seg_flags ($0E00). The ROM leaves A = $0E. */
void pros_reset_seg_ptrs(fw *f);
/* $CA3B: the ROM isr_par_ready with A = a, X = x, Y = y first, then one segment: ph_phoneme, ph_duration, and
   pros_last_target = pros_target + cmd_pitch_step (8 bits) as its pitch; flags $80. A, X and Y are preserved
   (isr_par_ready preserves them; the ROM pushes A and Y after it and pulls them at $CA6D). */
void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y);
/* $CA15: a pause segment of duration a: phoneme $3B (also into ph_phoneme), pitch pros_last_target, flags $80.
   Returns the Y it leaves, the old low byte of pros_seg_count; it leaves A = $80 (its last LDA) and X as it was. */
uint8_t ph_append_pause(fw *f, uint8_t a);
/* $C9B8: a pause segment after the punctuation pros_symbol, as ph_append_pause, of duration cmd_phrase_pause in
   full after . ? !, (cmd_phrase_pause >> 1) & $0F after , ; : and (cmd_phrase_pause >> 2) & $0F after any other
   symbol; a duration of 0 becomes 1 ($C9EC INC A). A, X and Y are preserved (PHA / TYA / PHA, and the exit
   JMP $CA6D into pros_append_segment's PLA / TAY / PLA / RTS). */
void ph_append_phrase_pause(fw *f);

#endif
```

Create `src/firmware/pros_seg.c`:

```c
#include "pros_seg.h"
#include "tn_convert.h"

/* STA (ptr),Y: the 16-bit pointer plus y. The pointers start at the arrays ($0200, $0600, $0A00, $0E00) and only
   move up one page per 256 segments; a clause makes far fewer than 1,280 segments (at most 3 per phonetic symbol
   plus the pauses), so the address stays in RAM below $1300. */
static void seg_store(fw *f, uint16_t ptr, uint8_t y, uint8_t v) { fw_wr8(f, (uint16_t)(fw_rd16(f, ptr) + y), v); }

static void inc8(fw *f, uint16_t addr) { fw_wr8(f, addr, (uint8_t)(fw_rd8(f, addr) + 1)); }

/* INC pros_seg_count / BNE; on the wrap to 0: INC pros_seg_count+1, then the pointers' high bytes in the ROM's
   order, phoneme, pitch, duration, flags ($CA02-$CA11, $CA2A-$CA39, $CA5D-$CA6C are the same code). */
static void seg_advance(fw *f)
{
    inc8(f, PROS_SEG_COUNT);
    if (fw_rd8(f, PROS_SEG_COUNT) != 0)
        return;
    inc8(f, PROS_SEG_COUNT + 1);
    inc8(f, PROS_PHONEME_PTR + 1);
    inc8(f, PROS_PITCH_PTR + 1);
    inc8(f, PROS_DURATION_PTR + 1);
    inc8(f, PROS_FLAGS_PTR + 1);
}

/* The pause segment of both pause routines ($C9ED-$CA11 is the same code as $CA15-$CA39): duration d, phoneme
   $3B (stored to ph_phoneme too), pitch pros_last_target, flags $80. Returns Y, the old index low byte. */
static uint8_t append_pause(fw *f, uint8_t d)
{
    uint8_t y = fw_rd8(f, PROS_SEG_COUNT);
    seg_store(f, PROS_DURATION_PTR, y, d);
    seg_store(f, PROS_PHONEME_PTR, y, PH_PAUSE_PHONEME);
    fw_wr8(f, PH_PHONEME, PH_PAUSE_PHONEME);
    seg_store(f, PROS_PITCH_PTR, y, fw_rd8(f, PROS_LAST_TARGET));
    seg_store(f, PROS_FLAGS_PTR, y, PROS_SEG_FLAGS_GEN);
    seg_advance(f);
    return y;
}

void pros_reset_seg_ptrs(fw *f)
{
    fw_wr16(f, PROS_PHONEME_PTR, PROS_SEG_PHONEME);
    fw_wr16(f, PROS_DURATION_PTR, PROS_SEG_DURATION);
    fw_wr16(f, PROS_PITCH_PTR, PROS_SEG_PITCH);
    fw_wr16(f, PROS_FLAGS_PTR, PROS_SEG_FLAGS);
}

void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y)
{
    fw_regs r = { a, x, y, 0 };
    fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET);
    uint8_t i = fw_rd8(f, PROS_SEG_COUNT);
    seg_store(f, PROS_PHONEME_PTR, i, fw_rd8(f, PH_PHONEME));
    seg_store(f, PROS_DURATION_PTR, i, fw_rd8(f, PH_DURATION));
    uint8_t target = (uint8_t)(fw_rd8(f, PROS_TARGET) + fw_rd8(f, CMD_PITCH_STEP));   /* CLC; ADC: 8 bits */
    fw_wr8(f, PROS_LAST_TARGET, target);
    seg_store(f, PROS_PITCH_PTR, i, target);
    seg_store(f, PROS_FLAGS_PTR, i, PROS_SEG_FLAGS_GEN);
    seg_advance(f);
}

uint8_t ph_append_pause(fw *f, uint8_t a) { return append_pause(f, a); }

void ph_append_phrase_pause(fw *f)
{
    uint8_t d = fw_rd8(f, CMD_PHRASE_PAUSE);
    switch (fw_rd8(f, PROS_SYMBOL)) {
    case '.': case '?': case '!':
        break;                                  /* $C9E7: PLA, in full (no AND) */
    case ',': case ';': case ':':
        d = (uint8_t)((d >> 1) & 0x0F);         /* $C9E1: LSR; AND #$0F */
        break;
    default:
        d = (uint8_t)((d >> 2) & 0x0F);         /* $C9D9: LSR; LSR; AND #$0F */
        break;
    }
    if (d == 0)
        d++;                                    /* $C9EC INC A */
    append_pause(f, d);
}
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/cmd_apply.h"
```

with

```c
#include "firmware/cmd_apply.h"
#include "firmware/pros_seg.h"
```

- Replace

```c
/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
```

with

```c
/* ph_append_phrase_pause $C9B8: no register input; A, X and Y out as they came in (PHA / TYA / PHA, then the exit
   JMP $CA6D into pros_append_segment's PLA / TAY / PLA / RTS; X untouched). The C makes no ROM call, so the thunk
   leaves all three as they are. The only caller, pros_phrase_gen ($D1DA, $D1DD), calls it twice in a row (the
   second call pushes the first's A and Y again), then LDA #0 at $D1E0; X and Y then reach isr_par_ready at $D155
   through JMP $D152 ($D209, $D30F). No caller reads a flag. Declared: A, X, Y. */
static void ph_append_phrase_pause_regs(fw *f, cpu740 *c)
{
    (void)c;
    ph_append_phrase_pause(f);
}

/* ph_append_pause $CA15: A in (the duration). A = $80 (its last LDA #$80), Y = the old pros_seg_count low byte
   (its LDY), X untouched, out. pros_phrase_gen reads A: at the end of the text ($D190) the second JSR at $D193
   takes the $80 the first leaves as its duration, and after a word pause ($D352) A goes on to isr_par_ready at
   $D155 (LDM does not change A). Y and X leave pros_phrase_gen after the second end pause ($D196-$D1A1 change
   neither) as its own Y and X outputs. No caller reads a flag (LDY $78, the next LDY, or LDA follows).
   Declared: A, X, Y. */
static void ph_append_pause_regs(fw *f, cpu740 *c)
{
    c->y = ph_append_pause(f, c->a);
    c->a = PROS_SEG_FLAGS_GEN;
}

/* pros_append_segment $CA3B: A, X, Y in (to isr_par_ready); A, X, Y out as they came in (isr_par_ready preserves
   them; the ROM pushes A and Y after it and pulls them at $CA6D). They are set back from the locals because the
   ROM call from C overwrites the machine's registers. The only caller, pros_phrase_gen ($D3CC), loads A next
   (LDA pros_sym_flag); on its BBC4 path to $D152 X and Y reach isr_par_ready at $D155, else LDY $78 reloads Y.
   No caller reads a flag. Declared: A, X, Y. */
static void pros_append_segment_regs(fw *f, cpu740 *c)
{
    uint8_t a = c->a, x = c->x, y = c->y;
    pros_append_segment(f, a, x, y);
    c->a = a;
    c->x = x;
    c->y = y;
}

/* pros_reset_seg_ptrs $CA71: A = $0E (its last LDA, the high byte of pros_seg_flags) out; X and Y untouched (no
   ROM call in the C). pros_make_segments ($CAB0) calls pros_mark_stress next, whose first isr_par_ready ($CB1F)
   gets this Y (X is written first, $CAEC TAX); par_play_start ($BAF5) loads A next, and pros_scale_target pushes
   and restores X and Y. No caller reads a flag (LDA follows both calls). Declared: A, X, Y. */
static void pros_reset_seg_ptrs_regs(fw *f, cpu740 *c)
{
    pros_reset_seg_ptrs(f);
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
```

- Replace

```c
THUNK(cmd_hex_value)
#undef THUNK
```

with

```c
THUNK(cmd_hex_value)
THUNK(ph_append_phrase_pause)
THUNK(ph_append_pause)
THUNK(pros_append_segment)
THUNK(pros_reset_seg_ptrs)
#undef THUNK
```

- Replace

```c
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
```

with

```c
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
    { "ph_append_phrase_pause", 0xC9B8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ph_append_phrase_pause },
    { "ph_append_pause",    0xCA15, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ph_append_pause },
    { "pros_append_segment", 0xCA3B, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_append_segment },
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
```

The table stays in address order. `pros_seg.c` and `pros_addr.h` clash with no existing basename (`ls src/firmware tests | grep -E "^(pros|ph)_"` shows only this task's files).

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_CA71_pros_reset_seg_ptrs.txt`: eight `LDA #`/`STA` pairs → the four `fw_wr16` (low byte `$00`, high `$02`/`$06`/`$0A`/`$0E`); the last `LDA #$0E` → the thunk's A.
- `rom_CA3B_pros_append_segment.txt`:
  - `JSR isr_par_ready` before `PHA` → `fw_rom_call(ROM_ISR_PAR_READY)` with the entry A, X, Y and `FW_ROM_CALL_BUDGET`, first.
  - `LDY pros_seg_count` → `i` (low byte only).
  - `STA ($22),Y`, `STA ($24),Y` → `seg_store` of `ph_phoneme`, `ph_duration`.
  - `CLC / ADC cmd_pitch_step / STA pros_last_target / STA ($26),Y` → the 8-bit `target`, stored to both.
  - `LDA #$80 / STA ($28),Y` → `PROS_SEG_FLAGS_GEN`.
  - `INC`/`BNE`/`INC` ×5 → `seg_advance`, in the order `$17F7`, `$23`, `$27`, `$25`, `$29`.
  - `PLA / TAY / PLA` → the thunk restores A and Y (and X, untouched in ROM) from its locals.
- `rom_CA15_ph_append_pause.txt` → `append_pause`: `STA ($24),Y` with the entry A; `LDA #$3B` stored through `($22),Y` and to `ph_phoneme`; `LDA pros_last_target` through `($26),Y`; `LDA #$80` through `($28),Y`; the same wrap; the thunk's A = `$80`, Y = the old low byte.
- `rom_C9B8_ph_append_phrase_pause.txt`: the six `CMP`/`BEQ` (`$2E`, `$3F`, `$21` full; `$2C`, `$3B`, `$3A` half) → the `switch`; `LSR / AND #$0F` and `LSR / LSR / AND #$0F`; the full path has no `AND`; `CMP #0 / BNE / INC A` → `if (d == 0) d++`; `$C9ED`–`$CA11` → `append_pause`; `JMP $CA6D` → no register output (A and Y as pushed at `$C9B8`).
- `rom_C5D6_isr_par_ready.txt` and `$C5EA` in `rom_C5DE_isr_par_hold.txt`: `PHA … PLA / RTS`, no X or Y use: A, X and Y preserved.

For the thunk outputs, re-read `rom_D129_pros_phrase_gen.txt` after `$D190`, `$D193`, `$D1DD`, `$D352`, `$D3CC` (to `$D1A1`, `$D1E0`, `$D152`/`$D155`, `$D3D7`), `rom_CA92_pros_make_segments.txt` and `rom_CABD_pros_mark_stress.txt` (`$CABD`–`$CB1F`), `rom_BAF2_par_play_start.txt` and `rom_BDD3_pros_scale_target.txt`. Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xC9B8,0xCA15,0xCA3B,0xCA6D,0xCA71) for op in (0x20,0x4C)]"
```

Expected: `JSR` (`0x20`) operands only at `0xd1da 0xd1dd` (`$C9B8`), `0xd190 0xd193 0xd352` (`$CA15`), `0xd3cc` (`$CA3B`), `0xbaf5 0xcab0` (`$CA71`); `JMP` (`0x4c`) only `0xca12` (`$CA6D`).

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_pros"`, `cmd //c ".\build.bat test diff_lts"`, `cmd //c ".\build.bat test golden_hybrid"`, `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_pros                ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 20 lines `<name> N calls checked`, each with N > 0: the 16 of M4.2 and, last,

```
  ph_append_phrase_pause N calls checked
  ph_append_pause      N calls checked
  pros_append_segment  N calls checked
  pros_reset_seg_ptrs  N calls checked
```

  (`ph_append_phrase_pause` is longer than the 20-column name field). Then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged, and 0 mismatches. The new corpus texts of Task 1 bring the page wraps (`seg_wrap_phrase`, `seg_wrap_word`) and the `INC A` (`phrase_pauses`) into the checked calls.
- `golden_hybrid`: `39 corpus files x 21 modes (all, then each routine alone)`, all identical.
- The full run: `31 suite(s), 0 failure(s), 0 skip(s)` (`suite_fw_pros` is new), with no warnings, the firmware syntax check passing, and `firmware_data` still at 38197.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/pros_addr.h src/firmware/pros_seg.h src/firmware/pros_seg.c src/engine/thunks.c tests/test_fw_pros.c tests/suites.h
git commit -m "The segment appenders in C: pros_reset_seg_ptrs, pros_append_segment, ph_append_pause, ph_append_phrase_pause, with hooks; tests/test_fw_pros.c (M4.3a 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `ph_lengthen_count`, `pros_declination` and `pros_declination_step` in C, and their hooks (spec §2.2, §2.3, §2.7, §4.4)

**Files:**
- Create: `src/firmware/ph_expand.h`, `src/firmware/ph_expand.c`, `src/firmware/pros_decl.h`, `src/firmware/pros_decl.c`
- Modify: `src/engine/thunks.c`, `tests/test_fw_pros.c`

**Interfaces:**
- Consumes:
  - Task 2: `PH_LENGTHEN_STEPS` (`0xD44A`, `data/fw_data.h`) and the converted bytes `$D44A`–`$D468` (entry 0 = `$60`);
  - Task 3: `pros_addr.h` (`ROM_PROS_DECLINATION`, `ROM_PROS_DECLINATION_STEP`, `ROM_PH_LENGTHEN_COUNT`), the `#include "firmware/pros_seg.h"` line, the `pros_reset_seg_ptrs` thunk, `THUNK` line and table row in `thunks.c`, and the harness of `tests/test_fw_pros.c` (`setup()`, `g_ram`, `g_f`, `g_ncalls`, `check_row`, the `#include "firmware/pros_addr.h"` line, and `test_append_across_wrap();` as the last line of `suite_fw_pros`);
  - the RAM names `PROS_POS`, `PROS_SYM_LENGTHEN`, `PROS_PHRASE_LEN`, `PROS_BASELINE`, `PROS_BASELINE_END`, `PROS_DECL_STEP` (`fw_ram.h`).
- Produces (used by Task 5):

```c
/* ph_expand.h (only this; the decoder's helpers are static in pros_gen.c, Task 5) */
uint8_t ph_lengthen_count(fw *f, uint8_t a);
/* pros_decl.h */
#define PROS_DECL_STEP_MAX  25
uint8_t pros_declination_step(fw *f);
uint8_t pros_declination(fw *f);
static inline uint8_t pros_declination_y(const fw *f);   /* the Y the ROM pros_declination leaves */
```

  - The replacements `"pros_declination"` `0xD3DD` and `"pros_declination_step"` `0xD40D` (outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y`) and `"ph_lengthen_count"` `0xD43E` (outputs `FW_OUT_A | FW_OUT_Y`): 23 routines in all.

**ROM facts checked** (`rom_D43E_ph_lengthen_count.txt`, `rom_D3DD_pros_declination.txt`, `rom_D40D_pros_declination_step.txt`, the callers `rom_D129_pros_phrase_gen.txt`, `rom_CEF6_pros_mark_phrases.txt`, `rom_D0FD_pros_store_phrase_len.txt`, and the ROM bytes `$D44A`–`$D468`):
- **Signature addition (not a change):** the conventions give `pros_declination` only A as its return, but its callers' X and Y reach `isr_par_ready` (below), so the thunk must set them. X is `pros_pos` (the step's `LDX`, untouched afterwards). Y is the step **before** the negation on the rising path. `pros_decl.h` adds `static inline uint8_t pros_declination_y(const fw *f)`, which recovers that Y from the RAM the routine leaves: `pros_decl_step`, or `0 - pros_decl_step` when `pros_baseline_end > pros_baseline` (the rising path restores both baselines, so the test still holds after the call). Task 5 can use it for its first `isr_par_ready` call.
- **`ph_lengthen_count`:** `CMP #$1F / BCC`: 31 and above take `LSR A / TAY` (a >> 1), below 31 `TAY / LDA $D44A,Y / TAY`. The table read is at most `$D468`. The ROM bytes `$D44A`–`$D468` are `60 01 02 03 04 04 05 06 06 07 07 08 09 09 09 0A 0A 0A 0B 0B 0C 0C 0C 0D 0D 0D 0D 0E 0E 0E 0F`; entry 0 is the routine's own `RTS` (96 steps). The routine leaves Y = A = the count and X untouched.
- **`pros_declination`:** `LDA pros_baseline_end / CMP pros_baseline / BCC / BEQ` into `pros_declination_step`: a falling (`end < baseline`) or level baseline is a tail-branch, so A, X and Y are the step's. Rising: `PHA` the end value, `pros_baseline_end = pros_baseline`, `pros_baseline = end` (both stored), `JSR` the step, `LDA #0 / SEC / SBC pros_decl_step / STA` (the negation, in RAM only; Y keeps the positive step), then the swap back, which leaves **A = the restored `pros_baseline`**. No register is changed after the `JSR` except A.
- **`pros_declination_step`:** `LDX pros_pos`; `pros_phrase_len = pros_sym_lengthen[X] & $7F` is **stored first**, then `INC`ed when 0 (`$D41A`), so a stored count of `$00` or `$80` ends as 1. The quotient is a repeated subtraction: `CMP pros_phrase_len / BCC` exits only when the remainder is **below** the divisor, so an exact multiple counts its last subtraction (120 / 10 = 12). Y cannot wrap (the difference is at most 255 and the divisor at least 1). `CPY #$19 / BCC / LDY #$19` caps at 25. `STY pros_decl_step / TYA / RTS`: A = Y = the step, X = `pros_pos`. The difference `pros_baseline - pros_baseline_end` is 8 bits (`SEC / SBC`).
- **The hook on `$D40D` fires on the tail-branch too** (the hook is on the PC). Then the stack holds `pros_declination`'s return address; the thunk's emulated `RTS` pops it as the ROM's `RTS` at `$D43D` does, and `diff_lts`'s reference copy finishes when SP rises above entry, at the same PC and SP. In the scratch run every `pros_declination` call was matched by one `pros_declination_step` call (`287 calls checked` each).
- **What the callers read after each call** (no caller reads a flag: an `LDA` follows every call):
  - `pros_declination` from `pros_mark_phrases` (`$D04D`): `LDA pros_class`, then `JMP $CF1A`, which reloads X (`$CF23`) but passes Y to `isr_par_ready` at `$CF28`, or leaves through `$CEEC` (`pros_store_phrase_len` writes X only). From `pros_phrase_gen` (`$D129`): X and Y reach `isr_par_ready` at `$D155` (A is reloaded). Declared: A, X, Y.
  - `pros_declination_step`: from the `JSR` at `$D3F2`, X and Y become `pros_declination`'s outputs; from the tail-branch, all three are. Declared: A, X, Y.
  - `ph_lengthen_count` from `pros_phrase_gen` (`$D3A6`): `LDA ph_dur_range`, then the loop `TAX / LDA $C67B,X / DEY / BNE`: Y is read, X is written before it is read. Declared: A and Y (both are the count; Y is the one read).

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_pros.c`:
- Replace

```c
#include "firmware/pros_addr.h"
```

with

```c
#include "firmware/ph_expand.h"
#include "firmware/pros_addr.h"
#include "firmware/pros_decl.h"
```

- Replace

```c
void suite_fw_pros(void)
{
```

with

```c
static void test_decl_rows(void)
{
    check_row("pros_declination", ROM_PROS_DECLINATION, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_declination_step", ROM_PROS_DECLINATION_STEP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("ph_lengthen_count", ROM_PH_LENGTHEN_COUNT, FW_OUT_A | FW_OUT_Y);
}

/* Below 31 the table at $D44A, whose entry 0 is the RTS opcode ($60) of ph_lengthen_count itself; from 31 on,
   a >> 1 */
static void test_lengthen_count(void)
{
    static const uint8_t steps[31] = { 0x60, 1, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9, 9, 10,
                                       10, 10, 11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15 };
    setup();
    for (unsigned a = 0; a < 31; a++)
        CHECK_EQ(ph_lengthen_count(&g_f, (uint8_t)a), steps[a]);
    CHECK_EQ(ph_lengthen_count(&g_f, 0), 96);    /* $D44A: a 96-step entry */
    CHECK_EQ(ph_lengthen_count(&g_f, 31), 15);   /* $D442: LSR */
    CHECK_EQ(ph_lengthen_count(&g_f, 32), 16);
    CHECK_EQ(ph_lengthen_count(&g_f, 61), 30);
    CHECK_EQ(ph_lengthen_count(&g_f, 0x80), 0x40);
    CHECK_EQ(ph_lengthen_count(&g_f, 0xFF), 0x7F);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* setup(); pros_pos = pos with pros_sym_lengthen[pos] = len (and other counts either side), the baseline from
   base to end, and stale pros_phrase_len and pros_decl_step. */
static void decl_setup(uint8_t pos, uint8_t len, uint8_t base, uint8_t end)
{
    setup();
    g_ram[PROS_POS] = pos;
    g_ram[PROS_SYM_LENGTHEN + (uint8_t)(pos - 1)] = 0x7F;
    g_ram[PROS_SYM_LENGTHEN + (uint8_t)(pos + 1)] = 0x7F;
    g_ram[PROS_SYM_LENGTHEN + pos] = len;
    g_ram[PROS_BASELINE] = base;
    g_ram[PROS_BASELINE_END] = end;
    g_ram[PROS_PHRASE_LEN] = 0x33;
    g_ram[PROS_DECL_STEP] = 0x44;
}

/* Falling: (base - end) / len, bit 7 of the stored count masked off; the count is stored in pros_phrase_len */
static void test_declination_falling(void)
{
    decl_setup(5, 0x8A, 0xC0, 0x40);          /* $80 / 10 = 12, remainder 8 */
    CHECK_EQ(pros_declination(&g_f), 12);     /* the tail-branch: A = the step */
    CHECK_EQ(g_ram[PROS_DECL_STEP], 12);
    CHECK_EQ(pros_declination_y(&g_f), 12);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 10);
    CHECK_EQ(g_ram[PROS_BASELINE], 0xC0);     /* the baselines are only read */
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x40);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 5], 0x8A);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);

    decl_setup(200, 10, 120, 0);              /* an exact multiple counts its last subtraction (BCC, not BEQ) */
    CHECK_EQ(pros_declination(&g_f), 12);
    decl_setup(200, 10, 119, 0);
    CHECK_EQ(pros_declination(&g_f), 11);
    decl_setup(255, 9, 9, 0);                 /* pros_pos 255 reads $1FFF */
    CHECK_EQ(pros_declination(&g_f), 1);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 9);
}

/* Level (BEQ): the step is 0, and pros_phrase_len is still stored */
static void test_declination_level(void)
{
    decl_setup(3, 7, 0x80, 0x80);
    CHECK_EQ(pros_declination(&g_f), 0);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0);
    CHECK_EQ(pros_declination_y(&g_f), 0);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 7);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x80);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x80);
}

/* Rising: swap, the step of the swapped values, negate, swap back; A = the restored pros_baseline, Y = the step */
static void test_declination_rising(void)
{
    decl_setup(5, 10, 0x40, 0xC0);
    CHECK_EQ(pros_declination(&g_f), 0x40);   /* not the step */
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xF4);    /* 0 - 12 */
    CHECK_EQ(pros_declination_y(&g_f), 12);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 10);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x40);
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0xC0);

    decl_setup(5, 1, 0x00, 0xFF);             /* capped at 25 before the negation: 0 - 25 */
    CHECK_EQ(pros_declination(&g_f), 0x00);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xE7);
    CHECK_EQ(pros_declination_y(&g_f), 25);

    decl_setup(5, 0x81, 0x7F, 0x80);          /* rising by 1 */
    CHECK_EQ(pros_declination(&g_f), 0x7F);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 0xFF);
    CHECK_EQ(pros_declination_y(&g_f), 1);
}

/* The cap at 25 (CPY #$19) and the counts either side of it */
static void test_declination_cap(void)
{
    decl_setup(1, 1, 24, 0);
    CHECK_EQ(pros_declination_step(&g_f), 24);
    decl_setup(1, 1, 25, 0);
    CHECK_EQ(pros_declination_step(&g_f), 25);
    decl_setup(1, 1, 26, 0);
    CHECK_EQ(pros_declination_step(&g_f), 25);
    decl_setup(1, 1, 0xFF, 0);                /* 255 subtractions, Y = 255, then the cap */
    CHECK_EQ(pros_declination_step(&g_f), 25);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 25);
}

/* $D41A: a stored count of 0 (or $80) becomes 1, and the 1 is stored */
static void test_declination_divisor_bump(void)
{
    decl_setup(9, 0x00, 20, 0);
    CHECK_EQ(pros_declination_step(&g_f), 20);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 1);
    decl_setup(9, 0x80, 20, 0);
    CHECK_EQ(pros_declination(&g_f), 20);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 1);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 9], 0x80);   /* read, not written */
}

/* The step alone (the entry the tail-branch reaches): A = Y = pros_decl_step; the difference is 8 bits even when
   pros_baseline_end is above pros_baseline (never so from pros_declination, which swaps first) */
static void test_declination_step(void)
{
    decl_setup(2, 4, 0x10, 0x20);             /* $10 - $20 = $F0: 60 steps, capped */
    CHECK_EQ(pros_declination_step(&g_f), 25);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x10);     /* the step never swaps */
    CHECK_EQ(g_ram[PROS_BASELINE_END], 0x20);
    decl_setup(2, 0x7F, 0xFE, 0x00);          /* $FE / $7F = 2 */
    CHECK_EQ(pros_declination_step(&g_f), 2);
    CHECK_EQ(g_ram[PROS_PHRASE_LEN], 0x7F);
}

void suite_fw_pros(void)
{
```

- Replace

```c
    test_append_across_wrap();
}
```

with

```c
    test_append_across_wrap();
    test_decl_rows();
    test_lengthen_count();
    test_declination_falling();
    test_declination_level();
    test_declination_rising();
    test_declination_cap();
    test_declination_divisor_bump();
    test_declination_step();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_pros"`
Expected: a compile error, because `firmware/ph_expand.h` does not exist.

- [ ] **Step 3: Implement**

Create `src/firmware/ph_expand.h`:

```c
#ifndef FW_PH_EXPAND_H
#define FW_PH_EXPAND_H
/* Phoneme expansion helpers of the segment generator (M4.3a spec 2.2), decompiled. */
#include "fw.h"

/* $D43E: the number of ph_dur_lengthen steps for a half lengthening count a = (count + 1) / 2:
   ph_lengthen_steps[a] ($D44A,a) below 31, else a >> 1. Entry 0 is $D44A, the routine's own RTS opcode ($60),
   so a = 0 gives 96 steps. The ROM returns it in Y (and A). */
uint8_t ph_lengthen_count(fw *f, uint8_t a);

#endif
```

Create `src/firmware/ph_expand.c`:

```c
#include "ph_expand.h"
#include "data/fw_data.h"

#define PH_LENGTHEN_DIRECT  31   /* CMP #$1F: half counts from here on take a >> 1 instead of the table */

uint8_t ph_lengthen_count(fw *f, uint8_t a)
{
    if (a >= PH_LENGTHEN_DIRECT)
        return (uint8_t)(a >> 1);                              /* $D442: LSR A; TAY */
    return fw_rom8(f, (uint16_t)(PH_LENGTHEN_STEPS + a));      /* $D445: TAY; LDA $D44A,Y; TAY */
}
```

Create `src/firmware/pros_decl.h`:

```c
#ifndef FW_PROS_DECL_H
#define FW_PROS_DECL_H
/* The declination of a phrase (ROM $D3DD-$D43D), decompiled (M4.3a spec 2.2, 2.7): the step by which the pitch
   baseline falls per phonetic symbol, from pros_baseline to pros_baseline_end over the phrase. */
#include "fw.h"

#define PROS_DECL_STEP_MAX  25   /* CPY #$19: the step is capped at 25 */

/* $D40D: pros_phrase_len = pros_sym_lengthen[pros_pos] & $7F, then 1 if that is 0 (stored back: the next phrase's
   count starts from it); pros_decl_step = (pros_baseline - pros_baseline_end, 8 bits) / pros_phrase_len by
   repeated subtraction, at most 25. Returns A = Y = pros_decl_step; the ROM leaves X = pros_pos. */
uint8_t pros_declination_step(fw *f);
/* $D3DD: pros_declination_step when pros_baseline_end <= pros_baseline (falling or level: a tail-branch, so A, X
   and Y are the step's). A rising baseline swaps pros_baseline and pros_baseline_end, takes the step, negates
   pros_decl_step (0 - step) and swaps back; A is then the restored pros_baseline, Y the step before the negation,
   X = pros_pos. Returns A. */
uint8_t pros_declination(fw *f);

/* The Y the ROM pros_declination leaves, from the RAM it leaves: pros_decl_step, or 0 - pros_decl_step (the step
   before its negation) on the rising path, which leaves pros_baseline_end > pros_baseline as it found them. */
static inline uint8_t pros_declination_y(const fw *f)
{
    uint8_t step = fw_rd8(f, PROS_DECL_STEP);
    return fw_rd8(f, PROS_BASELINE_END) > fw_rd8(f, PROS_BASELINE) ? (uint8_t)(0 - step) : step;
}

#endif
```

Create `src/firmware/pros_decl.c`:

```c
#include "pros_decl.h"

uint8_t pros_declination_step(fw *f)
{
    uint8_t x = fw_rd8(f, PROS_POS);
    uint8_t len = fw_rd8(f, (uint16_t)(PROS_SYM_LENGTHEN + x)) & 0x7F;
    fw_wr8(f, PROS_PHRASE_LEN, len);
    if (len == 0)
        fw_wr8(f, PROS_PHRASE_LEN, ++len);                     /* $D41A INC pros_phrase_len */
    uint8_t y = 0;
    uint8_t a = (uint8_t)(fw_rd8(f, PROS_BASELINE) - fw_rd8(f, PROS_BASELINE_END));   /* SEC; SBC */
    while (a >= len) {                                         /* CMP pros_phrase_len; BCC */
        y++;
        a = (uint8_t)(a - len);                                /* INY; SEC; SBC pros_phrase_len */
    }
    if (y >= PROS_DECL_STEP_MAX)
        y = PROS_DECL_STEP_MAX;                                /* CPY #$19; BCC; LDY #$19 */
    fw_wr8(f, PROS_DECL_STEP, y);
    return y;                                                  /* TYA */
}

uint8_t pros_declination(fw *f)
{
    uint8_t end = fw_rd8(f, PROS_BASELINE_END), base = fw_rd8(f, PROS_BASELINE);
    if (end <= base)                                           /* CMP; BCC / BEQ into the step */
        return pros_declination_step(f);
    fw_wr8(f, PROS_BASELINE_END, base);                        /* PHA; LDA; STA pros_baseline_end; PLA; STA */
    fw_wr8(f, PROS_BASELINE, end);
    pros_declination_step(f);
    fw_wr8(f, PROS_DECL_STEP, (uint8_t)(0 - fw_rd8(f, PROS_DECL_STEP)));   /* LDA #0; SEC; SBC pros_decl_step */
    uint8_t a = fw_rd8(f, PROS_BASELINE_END);                  /* the swap back leaves A = pros_baseline */
    fw_wr8(f, PROS_BASELINE_END, fw_rd8(f, PROS_BASELINE));
    fw_wr8(f, PROS_BASELINE, a);
    return a;
}
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/pros_seg.h"
```

with

```c
#include "firmware/pros_seg.h"
#include "firmware/pros_decl.h"
#include "firmware/ph_expand.h"
```

- Replace

```c
/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
```

with

```c
/* pros_declination $D3DD: no register input; A (the step, or the restored pros_baseline on the rising path),
   X = pros_pos and Y = the step before its negation out. pros_mark_phrases ($D04D) loads A next and reloads X at
   $CF23, but Y reaches isr_par_ready at $CF28 (or leaves pros_mark_phrases through $CEEC, which writes no Y); in
   pros_phrase_gen ($D129) X and Y reach isr_par_ready at $D155. No caller reads a flag (LDA follows both calls).
   Declared: A, X, Y. */
static void pros_declination_regs(fw *f, cpu740 *c)
{
    c->a = pros_declination(f);
    c->x = fw_rd8(f, PROS_POS);
    c->y = pros_declination_y(f);
}

/* pros_declination_step $D40D: no register input; A = Y = pros_decl_step and X = pros_pos (its LDX) out. Reached
   by the JSR at $D3F2 (pros_declination's rising path: its X and Y then leave pros_declination unchanged) and by
   pros_declination's BCC / BEQ at $D3E3 / $D3E5. The hook is on the PC, so it also fires on that tail-branch: the
   stack then holds pros_declination's return address, and the thunk's emulated RTS pops it as the ROM's RTS at
   $D43D does, handing A, X and Y to pros_declination's caller. Declared: A, X, Y (pros_declination's outputs). */
static void pros_declination_step_regs(fw *f, cpu740 *c)
{
    uint8_t step = pros_declination_step(f);
    c->a = step;
    c->x = fw_rd8(f, PROS_POS);
    c->y = step;
}

/* ph_lengthen_count $D43E: A in; Y = A = the step count out, X untouched. The only caller, pros_phrase_gen ($D3A6),
   loads A (LDA ph_dur_range) and counts Y down (DEY at $D3B0), writing X (TAX) before reading it; no flag is read.
   Declared: A and Y (both defined as the count; Y is the one read). */
static void ph_lengthen_count_regs(fw *f, cpu740 *c)
{
    uint8_t n = ph_lengthen_count(f, c->a);
    c->a = n;
    c->y = n;
}

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
```

- Replace

```c
THUNK(pros_reset_seg_ptrs)
#undef THUNK
```

with

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_declination)
THUNK(pros_declination_step)
THUNK(ph_lengthen_count)
#undef THUNK
```

- Replace

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
```

with

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_declination",   0xD3DD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination },
    { "pros_declination_step", 0xD40D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination_step },
    { "ph_lengthen_count",  0xD43E, FW_OUT_A | FW_OUT_Y, t_ph_lengthen_count },
```

The table stays in address order (23 rows; `diff_lts`'s `DIFF_MAX` is 24).

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump side by side with its C, instruction by instruction:
- `rom_D43E_ph_lengthen_count.txt`: `CMP #$1F / BCC` → `a >= PH_LENGTHEN_DIRECT`; `LSR A / TAY / RTS` → `a >> 1`; `TAY / LDA $D44A,Y / TAY` → `fw_rom8(PH_LENGTHEN_STEPS + a)`; the `RTS` at `$D44A` is entry 0 (Task 2 converts it as data).
- `rom_D3DD_pros_declination.txt`: `LDA pros_baseline_end / CMP pros_baseline / BCC / BEQ` → `end <= base`; `PHA / LDA pros_baseline / STA pros_baseline_end / PLA / STA pros_baseline` → the two writes; `JSR $D40D`; `LDA #0 / SEC / SBC / STA pros_decl_step` → the negation; `LDA pros_baseline_end / PHA / LDA pros_baseline / STA pros_baseline_end / PLA / STA pros_baseline` → the swap back and the returned A.
- `rom_D40D_pros_declination_step.txt`: `LDX pros_pos / LDA $1F00,X / AND #$7F / STA pros_phrase_len / BNE / INC pros_phrase_len` → the store, then the bump; `LDY #0 / LDA pros_baseline / SEC / SBC pros_baseline_end`; the loop `CMP pros_phrase_len / BCC / INY / SEC / SBC pros_phrase_len / JMP`; `CPY #$19 / BCC / LDY #$19`; `STY pros_decl_step / TYA`.

For the thunk outputs, re-read `rom_CEF6_pros_mark_phrases.txt` (`$D04D`–`$D05A`, `$CF1A`–`$CF37`, `$CEEC`–`$CEF5`), `rom_D0FD_pros_store_phrase_len.txt`, and `rom_D129_pros_phrase_gen.txt` (`$D129`–`$D155`, `$D3A6`–`$D3B3`). Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xD3DD,0xD40D,0xD43E) for op in (0x20,0x4C)]"
```

Expected: `JSR` operands only at `0xd04d 0xd129` (`$D3DD`), `0xd3f2` (`$D40D`), `0xd3a6` (`$D43E`), no `JMP`; the branches into `$D40D` are the `BCC`/`BEQ` at `$D3E3`/`$D3E5` in the dump.

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_pros"`, `cmd //c ".\build.bat test diff_lts"`, `cmd //c ".\build.bat test golden_hybrid"`, `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_pros                ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 23 lines `<name> N calls checked`, each with N > 0; the last three:

```
  pros_declination     N calls checked
  pros_declination_step N calls checked
  ph_lengthen_count    N calls checked
```

  The two declination counts are equal (every `pros_declination` reaches the step, by tail-branch or by `JSR`). The flush and unwound lines are unchanged, and there are 0 mismatches. Task 1's `baseline` text brings the rising and level paths into the checked calls, and `phrase_127` the divisor bump.
- `golden_hybrid`: `39 corpus files x 24 modes (all, then each routine alone)`, all identical.
- The full run: `31 suite(s), 0 failure(s), 0 skip(s)`, with no warnings and the firmware syntax check passing.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/ph_expand.h src/firmware/ph_expand.c src/firmware/pros_decl.h src/firmware/pros_decl.c src/engine/thunks.c tests/test_fw_pros.c
git commit -m "ph_lengthen_count, pros_declination and pros_declination_step in C, with hooks (the step's also fires on the tail-branch) (M4.3a 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `pros_phrase_gen` $D129 (with the phoneme decoder `ph_decode_symbol` $D31B) in C, and its hook (spec §2.2–§2.7, §4)

**Files:**
- Create: `src/firmware/pros_gen.h`, `src/firmware/pros_gen.c`
- Modify: `src/firmware/fw.h` (three RAM names), `src/engine/thunks.c` (the `pros_phrase_gen` thunk and its row), `tests/test_fw_pros.c` (the phrase-generator tests)

**Where the code goes.** `ph_decode_symbol` is a label inside `pros_phrase_gen`, not a routine: it shares the registers and `$78` with it, every exit is a `JMP $D152` back into the position loop, and nothing else reaches it. So it and the other pieces of the phrase generator are `static` helpers in `src/firmware/pros_gen.c`, one per ROM block:

| Helper | ROM |
|---|---|
| `rewrite_events` | $D158–$D185, the event rewrite |
| `word_start` | $D1E0–$D1F7, a space or punctuation ends the word |
| `symbol_terms` | $D20F–$D2DE, the accent, stress and tail terms of a phonetic symbol |
| `pitch_target` (with `adc`) | $D2DF–$D30C, the target with its carry chain, the consonant's stress reset, the declination |
| `decode_symbol` | $D31B–$D3DA, the list walk: end byte, word pause, triples |
| `append_triple` (with `half_count`, `scale_range`) | $D360–$D3CC, one triple's duration and segment |
| `par_ready` | the `isr_par_ready` call, with the registers the ROM has at the `JSR` |

The Y that `pros_declination` leaves comes from Task 4's `pros_declination_y` (`pros_decl.h`); `pros_gen.c` has no copy of it. `src/firmware/ph_expand.c` keeps only `ph_lengthen_count` (Task 4).

**Interfaces:**
- Consumes:
  - Task 2's `PH_SYMBOL_CLASS`, `PH_SYMBOL_FEATURES`, `PH_LIST_PTRS`, `PH_DUR_SHORTEN`, `PH_DUR_LENGTHEN`, `PH_WORD_PAUSE` and the bit names `PHC_PUNCT`, `PHC_PHONETIC`, `PHF_CONSONANT` (`data/fw_data.h`), and the converted bytes at $9BB4–$9F73, $A187–$A396, $C67B–$C87A and $C898–$C9B7.
  - Task 3's `pros_seg.h` (`pros_append_segment`, `ph_append_pause`, `ph_append_phrase_pause`, `PROS_SEG_FLAGS_GEN`, and in the tests `pros_reset_seg_ptrs`), and the one harness of `tests/test_fw_pros.c` (Task 3), with no mock or setup of its own: `g_ram`, `g_f`, `g_calls`, `g_ncalls`, `setup()`, `check_call()`, `check_segment()` and `mock_rom_call` (which records the first `MAX_CALLS` calls, answers `ROM_ISR_PAR_READY` and counts every call in `g_ncalls`).
  - Task 4's `ph_lengthen_count` (`ph_expand.h`), `pros_declination` and `pros_declination_y` (`pros_decl.h`).
  - M4.0–M4.2's `fw_ind_y`, `fw_rom8`, `fw_rom_call`, `FW_ROM_CALL_BUDGET`, `FW_TMP_78` and `ROM_ISR_PAR_READY` (`tn_convert.h`).
- Produces:

```c
/* fw.h */
#define FW_UTT_CONTENT         0x006B
#define FW_PROS_STRESS_TOGGLE  0x1C0D
#define FW_PROS_WORD_U         0x1C1F
/* pros_gen.h */
uint8_t pros_phrase_gen(fw *f, uint8_t *x, uint8_t *y);   /* returns A; *x, *y = X, Y as the ROM leaves them */
```

  The replacement `"pros_phrase_gen"` at `0xD129` (outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y`): 24 replacements in all, 25 `golden_hybrid` modes. M4.3b's C `pros_make_segments` will call `pros_phrase_gen` directly.

**ROM facts checked (in the dumps named):**
- **The second end pause lasts $80 frames, not 4** (`rom_D129`, `rom_CA15`). $D18E is `LDA #4; JSR ph_append_pause; JSR ph_append_pause`, and `ph_append_pause` returns with A = $80 (its last `LDA #$80` stores the segment flags). So the second call writes duration $80. The spec (§1, §2.2), the survey and the dump's own comment say "two pauses of 4 frames"; the ROM wins. Proved in the drafting run: `diff_lts` compares the whole RAM after each call and reported 0 mismatches with $80. Task 6 records it in "As built".
- **The lengthening loop never runs 256 steps** (`rom_D43E`, $D44A–$D468). Its count comes from `ph_lengthen_count`, not straight from `(n+1)/2`. Entries 1–30 of `ph_lengthen_steps` are 1–15, entry 0 is the `RTS` opcode $60 (96), and a half-count of 31 or more gives `A >> 1` ≥ 15. So a lengthening count of $FF gives half-count 0 and **96** steps. Only the shortening loop (Y = (n+1)/2 straight into `DEY/BNE`) runs 256 steps, for a count of $FF. Spec §2.7 states both loops as 256-step; the tests pin 256 for shortening and 96 for lengthening.
- **The registers at `isr_par_ready` ($D155) differ by the path into $D152.** The C tracks A, X and Y as the ROM has them:
  - first entry: A = $FF, X = `pros_pos` before it is reset (from `pros_declination_step`'s `LDX`), Y = `pros_declination`'s Y;
  - after punctuation ($D209): A = 0, X = `pros_pos`, Y = the symbol (`TAY` at $D1A8; the phrase pauses preserve A and Y);
  - after a marker with class bit 7 clear ($D213 `BBC7` → $D209): A = Y = `pros_class`;
  - after a hypermode skip ($D30F): A = `pros_func_word`, Y = `pros_class`;
  - at a list's end byte: A = $FF; at the space's item with @Q0: A = 0. Y = the list index in both. X = `pros_pos`, or, after a triple, the X the triple left;
  - after a word pause ($D35D): A = $80 (left by `ph_append_pause`), X = `pros_pos`, Y = `$78`;
  - after the closure of an unreleased stop ($D3D4): A = `pros_sym_flag`, Y = the speed-map index.
- **X after a triple** is the index of the last `ph_dur_lengthen` lookup (the `TAX` at $D3AC) when the position has a lengthening count, else `pros_pos` (the `LDX` at $D39A). The shortening loop's X is overwritten. That X goes into `pros_append_segment` and so into its `isr_par_ready` call.
- **`pros_declination` leaves Y = the quotient** (`rom_D3DD`, `rom_D40D`). On a falling or level baseline A = Y = `pros_decl_step`. On a rising one the step is negated after the call (`LDA #0; SEC; SBC`), so Y = 0 − `pros_decl_step`, and A = the restored `pros_baseline`. The conventions' `uint8_t pros_declination(fw *f)` returns only A, so the C recovers Y from RAM with Task 4's `pros_declination_y`: the swaps restore both bytes, so `pros_baseline_end > pros_baseline` still identifies the rising path. The Task 4 signature is kept unchanged.
- **The registers `pros_phrase_gen` leaves** (`rom_D129` $D18E–$D1A1): A = `pros_seg_count`'s low byte *before* the final `DEC`; X = `pros_pos` (= `tn_phon_len`, from `LDX` at $D187); Y = the second pause's `LDY pros_seg_count`, the low byte before that pause. The spec's §2.3 table says "Y = the last list index", which is wrong; Task 6 corrects it in "As built".
- **Who reads them** (`rom_CA92`, `rom_A61C`, `rom_A79F`, `rom_A7D4`). `pros_make_segments` returns at once (`$CABC RTS`). Its callers, `boot_reset` ($A687), `boot_selftest_sound` ($A7BA) and the C `tn_convert_clause`, all call `boot_loop_play` next. That routine loads A from $6B and, when it is 0, returns with X and Y untouched. No reader was found, but the values are defined, so A, X and Y are declared (the stricter rule).
- **One pointer table for all symbols.** The `CLC` at $D31E is dead, because `ASL` sets the carry. A symbol from $80 carries into the $9E74 branch, and $9E74 = $9D74 + $100. So the list pointer is at `PH_LIST_PTRS + 2 * symbol` (16-bit) for every symbol. The absolute-indexed reads do not wrap within a page.
- **Only the space and class-bit-7 symbols reach the decoder.** Punctuation and markers (class bit 7 clear) `JMP $D152` before $D312. A phonetic symbol with no features ('8', 'X', $80–$A7) still gets its terms and target; only the baseline decrement is skipped (`BEQ` $D300). Only features bit 6 (a consonant) clears `pros_term_stress`.
- **Word state:**
  - `pros_word_len` starts as `pros_sym_flags[0]`. A space's or punctuation's flags byte holds the next word's symbol count.
  - `pros_term_accent` is reset at each phonetic symbol ($D236), not at a word end.
  - `$1C1F` (U) is set at each word start and zeroed from a word's second symbol on.
  - At $D297 the tail is `0 − W`, through `LDA tail` (0), `SEC`, `SBC`.
- **Event rewrite** ($D158–$D185):
  - `CPX cmd_ev_count` is an unsigned `>=` exit.
  - Positions are compared on `cmd_ev_pos`'s low byte only.
  - `TXA / ASL / TAX` makes the index (next × 2) & $FF. The accesses `$1206,X`, `$1207,X` and `$1208,X` are absolute-indexed and never wrap.
  - `cmd_ev_pos+1` is zeroed, and `INC cmd_ev_next` re-reads RAM.
- **`$6B`** is written by `LDM #$FF` at $D35A (a word pause, only when @Q ≠ 0) and at $D360 (before each triple is read). Nothing in the routine clears it.
- **Unnamed RAM** written or read by the routine: exactly $6B, $1C0D, $1C1F and $78 (the last is `FW_TMP_78`, which exists). Every other address it uses has a name in `fw_ram.h`.
- **The duration** is `ph_dur_range + ph_min_dur` (`CLC; ADC`), capped at $FF by `BCC; LDY #$FF`, shifted `>> 3`, and read through `(cmd_speed_map),Y`. The half-counts are `CLC; ADC #1; LSR A`, which is 8-bit, so $FF gives 0.
- **Drafting run** (before the plan was consolidated; the whole plan was then proved in order, see Step 5). In a scratch worktree with private stand-ins for Tasks 2–4, and only `pros_phrase_gen` hooked (17 replacements):
  - `diff_lts` printed `pros_phrase_gen      162 calls checked` with 0 mismatches. With the seven new texts added as `diff_lts` extras it printed 191, still 0 mismatches.
  - `golden_hybrid` printed `32 corpus files x 18 modes`, all identical.
  - The CLI rom-vs-hybrid `cmp` gave `same` for all seven new texts.
  - The unit tests below passed. A deliberate mutation run failed them for each of: no carry in the chain, a 0-step loop, a 4-frame second pause, `cmd_ev_pos+1` not zeroed, and an unwrapped event index.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_pros.c`:
- Replace

```c
#include "firmware/pros_decl.h"
```

with

```c
#include "firmware/pros_decl.h"
#include "firmware/pros_gen.h"
```

- Replace

```c
void suite_fw_pros(void)
{
```

with

```c
/* ---- the phrase generator (Task 5) ---- */

#define GEN_SPEED_MAP 0x3000   /* a speed map in free RAM: map[i] = i, so ph_duration = (min + range) / 8 */

/* tn_phonetic = text; the page pointers at the arrays; the identity speed map; a level baseline ($80 to $80, so
   pros_decl_step 0); every count, flag, parameter and setting 0. */
static void gen_setup(const char *text)
{
    setup();
    size_t n = strlen(text);
    memcpy(g_ram + TN_PHONETIC, text, n);
    g_ram[TN_PHON_LEN] = (uint8_t)n;
    pros_reset_seg_ptrs(&g_f);
    for (unsigned i = 0; i < 32; i++)
        g_ram[GEN_SPEED_MAP + i] = (uint8_t)i;
    fw_wr16(&g_f, CMD_SPEED_MAP, GEN_SPEED_MAP);
    g_ram[PROS_BASELINE] = 0x80;
    g_ram[PROS_BASELINE_END] = 0x80;
}

/* Runs the phrase generator; *a, *x, *y = the registers it leaves. */
static void gen_run(uint8_t *a, uint8_t *x, uint8_t *y) { *a = pros_phrase_gen(&g_f, x, y); }

static uint16_t gen_list(uint8_t symbol)   /* the address of a symbol's phoneme list in the converted data */
{
    uint16_t p = (uint16_t)(PH_LIST_PTRS + 2 * symbol);
    return (uint16_t)(fw_rom8(&g_f, p) | fw_rom8(&g_f, (uint16_t)(p + 1)) << 8);
}

static void test_gen_lists(void)   /* the symbols, lists and table entries the tests below rely on */
{
    static const struct { char sym; uint8_t cls, features, items[10]; } k[] = {
        { ' ', 0x00, 0x00, { 0x5F, 0xFF } },                    /* the word-pause item */
        { '.', 0x0B, 0x00, { 0xFF } },                          /* punctuation (class bit 0) */
        { '~', 0x00, 0x00, { 0xFF } },                          /* a marker (class 0) */
        { '8', 0x80, 0x00, { 0xFF } },                          /* phonetic, no features, an empty list */
        { 'X', 0x80, 0x00, { 0x3B, 10, 10, 0xFF } },            /* phonetic, no features, one triple */
        { 'M', 0x80, 0x49, { 0x2A, 80, 60, 0xFF } },            /* a consonant */
        { 'E', 0xC0, 0x83, { 0x10, 150, 50, 0xFF } },           /* a vowel */
        { 'S', 0x80, 0x50, { 0x3E, 125, 80, 0xFF } },
        { 'T', 0x80, 0x60, { 0x40, 80, 40, 0x41, 20, 20, 0x42, 30, 20, 0xFF } },   /* a stop: three triples */
    };
    setup();
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint8_t s = (uint8_t)k[i].sym;
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_SYMBOL_CLASS + s)), k[i].cls);
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_SYMBOL_FEATURES + s)), k[i].features);
        uint16_t p = gen_list(s);
        for (unsigned j = 0;; j++) {
            CHECK_EQ(fw_rom8(&g_f, (uint16_t)(p + j)), k[i].items[j]);
            if (k[i].items[j] == 0xFF)
                break;
        }
    }
    CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_DUR_SHORTEN + 20)), 18);
    CHECK_EQ(fw_rom8(&g_f, (uint16_t)(PH_DUR_LENGTHEN + 100)), 110);
    CHECK(!g_f.fault);
}

static void test_gen_clause(void)   /* " ME." end to end */
{
    uint8_t a, x, y;
    gen_setup(" ME.");
    g_ram[PROS_SYM_FLAGS + 0] = 2;            /* the space: the next word has 2 symbols */
    g_ram[PROS_SYM_FLAGS + 2] = 0x21;         /* E: accent, nucleus */
    g_ram[PROS_SYM_SHORTEN + 1] = 1;          /* M: one shortening step */
    g_ram[PROS_SYM_LENGTHEN + 2] = 2;         /* E: ph_lengthen_count(1) = 1 step */
    g_ram[PROS_SYM_LENGTHEN + 0] = 4;         /* the phrase's count, read by pros_declination at pros_pos 0 */
    g_ram[PROS_BASELINE_END] = 0x70;          /* falling: (0x80 - 0x70) / 4 = 4 */
    g_ram[PROS_PARAMS + 20] = 3;              /* U */
    g_ram[PROS_PARAMS + 21] = 2;              /* V */
    g_ram[PROS_PARAMS + 22] = 10;             /* W */
    g_ram[PROS_PARAMS + 24] = 5;              /* Y */
    g_ram[PROS_PARAMS + 25] = 1;              /* Z */
    g_ram[PROS_STRESS_PARAM] = 7;
    g_ram[PROS_NUCLEUS_PARAM] = 9;
    g_ram[PROS_FINAL_PARAM] = 6;
    g_ram[CMD_PHRASE_PAUSE] = 8;              /* @D8: two pauses of 8 after '.' */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ncalls, 7);
    check_call(0, ROM_ISR_PAR_READY, 0xFF, 0, 4);       /* position 0; X and Y as pros_declination leaves them */
    check_call(1, ROM_ISR_PAR_READY, 0, 0, 0);          /* position 1: the space's list ended at its item (@Q0) */
    check_call(2, ROM_ISR_PAR_READY, 9, 1, 9);          /* M's segment: (60 + 18) / 8 */
    check_call(3, ROM_ISR_PAR_READY, 0xFF, 1, 3);       /* position 2: M's list end */
    check_call(4, ROM_ISR_PAR_READY, 20, 100, 20);      /* E's segment: X = the last lengthening index */
    check_call(5, ROM_ISR_PAR_READY, 0xFF, 100, 3);
    check_call(6, ROM_ISR_PAR_READY, 0, 3, '.');        /* position 4 (the end), after the punctuation */
    /* M: $80 / 8 + U 3 + 105 = $7C. E: ($7C >> 3 = 15, carry 1) + W 10 + Z 1 + final 6 + 105 = $8A */
    check_segment(0, 0x2A, 9, 0x7C);
    check_segment(1, 0x10, 20, 0x8A);
    check_segment(2, 0x3B, 8, 0x8A);                    /* the phrase pauses, at the last target */
    check_segment(3, 0x3B, 8, 0x8A);
    check_segment(4, 0x3B, 4, 0x8A);                    /* the end: 4 frames ... */
    check_segment(5, 0x3B, 0x80, 0x8A);                 /* ... then $80, the A the first pause leaves */
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 5);
    CHECK_EQ(a, 6);
    CHECK_EQ(x, 4);
    CHECK_EQ(y, 5);
    CHECK_EQ(g_ram[PROS_POS], 4);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 4);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x78);               /* two symbols with features: $80 - 2 * 4 */
    CHECK_EQ(g_ram[PROS_TARGET], 0x8A);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x8A);
    CHECK_EQ(g_ram[PROS_TERM_ACCENT], 10);              /* not reset by the punctuation */
    CHECK_EQ(g_ram[PROS_TERM_STRESS], 0);
    CHECK_EQ(g_ram[PROS_TERM_TAIL], 0);
    CHECK_EQ(g_ram[PROS_ACCENT_STATE], 0);
    CHECK_EQ(g_ram[FW_PROS_STRESS_TOGGLE], 0);
    CHECK_EQ(g_ram[FW_PROS_WORD_U], 3);
    CHECK_EQ(g_ram[PROS_WORD_POS], 0);
    CHECK_EQ(g_ram[PROS_WORD_LEN], 0);                  /* the '.' flags */
    CHECK_EQ(g_ram[PROS_SYMBOL], '.');
    CHECK_EQ(g_ram[PROS_CLASS], 0x0B);
    CHECK_EQ(g_ram[PH_PHONEME], 0x3B);
    CHECK_EQ(g_ram[PH_MAX_DUR], 150);
    CHECK_EQ(g_ram[PH_MIN_DUR], 50);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 110);                 /* 100, lengthened once */
    CHECK_EQ(g_ram[PH_DURATION], 20);
    CHECK_EQ(fw_rd16(&g_f, PH_LIST_PTR), gen_list('E'));
    CHECK_EQ(g_ram[FW_TMP_78], 2);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    CHECK(!g_f.fault);
}

/* " E", E accented at the word's end: pros_target = baseline / 8 (+ the carry) + W + U + final + 105 */
static uint8_t gen_target(uint8_t baseline, uint8_t w, uint8_t u, uint8_t final)
{
    uint8_t a, x, y;
    gen_setup(" E");
    g_ram[PROS_SYM_FLAGS + 0] = 1;
    g_ram[PROS_SYM_FLAGS + 1] = 0x21;
    g_ram[PROS_BASELINE] = baseline;
    g_ram[PROS_BASELINE_END] = baseline;
    g_ram[PROS_PARAMS + 22] = w;
    g_ram[PROS_PARAMS + 20] = u;
    g_ram[PROS_FINAL_PARAM] = final;
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PROS_TERM_ACCENT], w);
    CHECK_EQ(g_ram[PROS_TERM_STRESS], u);
    CHECK_EQ(g_ram[PROS_TERM_TAIL], final);
    CHECK_EQ(g_ram[PROS_SEG_PITCH + 0], g_ram[PROS_TARGET]);
    CHECK(!g_f.fault);
    return g_ram[PROS_TARGET];
}

static void test_gen_carry_chain(void)   /* $D2DF-$D2EE: no CLC between the ADCs */
{
    CHECK_EQ(gen_target(0x00, 0, 0, 0), 0x69);
    CHECK_EQ(gen_target(0x04, 0, 0, 0), 0x6A);          /* the LSR's carry (bit 2) into $D2E5 */
    CHECK_EQ(gen_target(0x08, 0xFF, 2, 0), 0x6C);       /* 1 + $FF: a carry into $D2E8 */
    CHECK_EQ(gen_target(0x00, 1, 0xFF, 2), 0x6C);       /* 1 + $FF: a carry into $D2EB */
    CHECK_EQ(gen_target(0x00, 0, 1, 0xFF), 0x6A);       /* 1 + $FF: a carry into $D2EE */
    CHECK_EQ(gen_target(0xFC, 0xE1, 0xFE, 0xFF), 0x6A); /* every carry set; the last one is lost */
}

static void test_gen_duration_loops(void)   /* "S": max 125, min 80, range 45 */
{
    uint8_t a, x, y;
    gen_setup("S");
    g_ram[PROS_SYM_SHORTEN + 0] = 0xFF;                 /* (255 + 1) / 2 = 0 in 8 bits: DEY/BNE runs 256 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 5);                   /* x0.9 down to its fixed point (0 steps would leave 45) */
    check_call(1, ROM_ISR_PAR_READY, 10, 0, 10);        /* (80 + 5) / 8; X = pros_pos (no lengthening) */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 10);

    gen_setup("S");
    g_ram[PROS_SYM_LENGTHEN + 0] = 0xFF;                /* half-count 0: ph_lengthen_count(0) = $60, 96 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 255);                 /* x1.1 up to 255 */
    check_call(1, ROM_ISR_PAR_READY, 31, 255, 31);      /* 80 + 255 capped at 255, / 8; X = the last index */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 31);
    CHECK(!g_f.fault);
}

static void test_gen_events(void)
{
    uint8_t a, x, y;
    gen_setup("ME");
    g_ram[CMD_EV_COUNT] = 3;
    fw_wr16(&g_f, CMD_EV_POS, 0x0500);                  /* only the low byte is compared: due at position 0 */
    fw_wr16(&g_f, CMD_EV_POSITIONS + 0, 0x0000);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 2, 0x0200);        /* due at 0 as well (low byte 0) */
    fw_wr16(&g_f, CMD_EV_POSITIONS + 4, 0x0001);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 6, 0x0077);        /* past cmd_ev_count: still read as the next position */
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 0), 0);   /* now segment indexes */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 2), 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 4), 1);   /* after M's one segment */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 6), 0x0077);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0077);        /* the high byte zeroed */
    CHECK_EQ(g_ram[CMD_EV_NEXT], 3);

    gen_setup("M");                                     /* index 130: TXA/ASL wraps it to entry 2 */
    g_ram[CMD_EV_COUNT] = 131;
    g_ram[CMD_EV_NEXT] = 130;
    fw_wr16(&g_f, CMD_EV_POSITIONS + 4, 0x1234);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 6, 0x0009);
    fw_wr16(&g_f, CMD_EV_POSITIONS + 260, 0x5678);      /* where entry 130 would be */
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 4), 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 9);             /* entry 3's low byte */
    CHECK_EQ(g_ram[CMD_EV_NEXT], 131);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 260), 0x5678);
    CHECK(!g_f.fault);
}

static void test_gen_punctuation(void)   /* @D0: '.' adds no phrase pause */
{
    uint8_t a, x, y;
    gen_setup("M.");
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);         /* M and the two end pauses */
    check_segment(1, 0x3B, 4, g_ram[PROS_SEG_PITCH + 0]);
    check_segment(2, 0x3B, 0x80, g_ram[PROS_SEG_PITCH + 0]);
    check_call(3, ROM_ISR_PAR_READY, 0, 1, '.');
    CHECK(!g_f.fault);
}

static void test_gen_no_features(void)   /* class bit 7, features 0 ('8', 'X'): no declination, no stress reset */
{
    uint8_t a, x, y;
    gen_setup("8X");
    g_ram[PROS_BASELINE_END] = 0x40;                    /* falling: the step is 25 (the cap) */
    g_ram[PROS_PARAMS + 25] = 4;                        /* Z: pros_term_stress of a word's later symbols */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PROS_DECL_STEP], 25);
    CHECK_EQ(g_ram[PROS_BASELINE], 0x80);
    CHECK_EQ(g_ram[PROS_TERM_STRESS], 4);
    CHECK_EQ(g_ram[PROS_TARGET], 0x80 / 8 + 4 + 105);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);         /* '8' has an empty list; X makes one segment */
    check_segment(0, 0x3B, 1, 0x80 / 8 + 4 + 105);      /* (10 + 0) / 8 */
    CHECK(!g_f.fault);
}

static void test_gen_content_flag(void)   /* $6B: $FF at a word pause and at a triple, else unchanged */
{
    uint8_t a, x, y;
    gen_setup(" ");                                     /* @Q0 */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    gen_setup("8");
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    gen_setup(" ");
    g_ram[CMD_WORD_PAUSE] = 5;                          /* @Q5 */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    check_segment(0, 0x3B, 5, 0x80);                    /* pitch: pros_last_target as initialised */
    CHECK_EQ(g_ram[FW_TMP_78], 0);
    check_call(1, ROM_ISR_PAR_READY, 0x80, 0, 0);       /* A as ph_append_pause leaves it, X = pros_pos, Y = $78 */
    gen_setup("X");
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    CHECK(!g_f.fault);
}

static void test_gen_final_count(void)   /* 254 segments and the two pauses: 256, then a 16-bit decrement */
{
    char text[255];
    uint8_t a, x, y;
    memset(text, 'E', 254);
    text[254] = '\0';
    gen_setup(text);
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x00FF);    /* $0100 - 1 */
    CHECK_EQ(a, 0);                                     /* the low byte before the decrement */
    CHECK_EQ(x, 254);
    CHECK_EQ(y, 0xFF);                                  /* the second pause's index */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0xFE], 4);
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0xFF], 0x80);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME + 0x100);   /* the second pause wrapped the page */
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION + 0x100);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH + 0x100);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS + 0x100);
    CHECK_EQ(g_ncalls, 255 + 254);                      /* one per position and at the end, one per segment */
    CHECK(!g_f.fault);
}

static void test_gen_closure_and_hyper(void)
{
    uint8_t a, x, y;
    gen_setup("T");
    g_ram[PROS_SYM_FLAGS + 0] = 0x10;                   /* an unreleased stop: the closure only */
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + 0], 0x40);
    check_call(1, ROM_ISR_PAR_READY, 10, 0, 10);        /* (40 + 40) / 8 */
    check_call(2, ROM_ISR_PAR_READY, 0x10, 0, 10);      /* A = pros_sym_flag at the JMP */

    gen_setup("~M");                                    /* @H1: a function word makes no segment */
    g_ram[CMD_HYPER] = 1;
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);         /* the two end pauses only */
    CHECK_EQ(g_ram[PROS_FUNC_WORD], 1);
    CHECK_EQ(g_ncalls, 3);
    check_call(1, ROM_ISR_PAR_READY, 0, 0, 0);          /* after '~' (class 0): A = Y = pros_class */
    check_call(2, ROM_ISR_PAR_READY, 1, 1, 0x80);       /* after M: A = pros_func_word, Y = pros_class */
    CHECK(!g_f.fault);
}

void suite_fw_pros(void)
{
```

- Replace

```c
    test_declination_step();
}
```

with

```c
    test_declination_step();
    test_gen_lists();
    test_gen_clause();
    test_gen_carry_chain();
    test_gen_duration_loops();
    test_gen_events();
    test_gen_punctuation();
    test_gen_no_features();
    test_gen_content_flag();
    test_gen_final_count();
    test_gen_closure_and_hyper();
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_pros"`
Expected: a compile error, because `firmware/pros_gen.h` does not exist.

- [ ] **Step 3: Implement**

`src/firmware/fw.h`: replace

```c
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */
```

with

```c
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */
#define FW_UTT_CONTENT         0x006B   /* unnamed in the annotations: "the utterance has content", $FF (LDM) at each
                                           word pause and each phoneme triple; boot_loop_play tests it */
#define FW_PROS_STRESS_TOGGLE  0x1C0D   /* unnamed in the annotations: pros_phrase_gen's "a stress or an accent seen
                                           in this word" (0 at each word start) */
#define FW_PROS_WORD_U         0x1C1F   /* unnamed in the annotations: the U term (pros_params[20]) that the first
                                           symbol of a word adds to pros_term_stress; 0 after it */
```

Create `src/firmware/pros_gen.h`:

```c
#ifndef FW_PROS_GEN_H
#define FW_PROS_GEN_H
/* The phrase generator of the segment generator (ROM $D129, with the phoneme decoder ph_decode_symbol at $D31B),
   decompiled (M4.3a spec 2.2). */
#include "fw.h"

/* $D129: walks tn_phonetic from position 0 to tn_phon_len. Per position: isr_par_ready; the command events due
   there get the current segment index; a space or punctuation ends the word (punctuation with @D adds two phrase
   pauses); a phonetic symbol gets its pitch target pros_target. Then the symbol's phoneme list appends one segment
   per (phoneme, max, min) triple, or a word pause (@Q) for the space. At the end two pauses (4 frames, then $80:
   the first pause leaves A = $80) and pros_seg_count back to the index of the last segment.
   Returns the A the ROM leaves (pros_seg_count's low byte before that decrement); *x = pros_pos (tn_phon_len),
   *y = the Y the second pause leaves (pros_seg_count's low byte before it). */
uint8_t pros_phrase_gen(fw *f, uint8_t *x, uint8_t *y);

#endif
```

Create `src/firmware/pros_gen.c`:

```c
#include "pros_gen.h"
#include "pros_seg.h"
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
#define PROS_PARAM_U  (PROS_PARAMS + 20)   /* pros_term_stress of a word's first symbol */
#define PROS_PARAM_V  (PROS_PARAMS + 21)   /* added to pros_term_tail at a word's last symbol */
#define PROS_PARAM_W  (PROS_PARAMS + 22)   /* the accent */
#define PROS_PARAM_Y  (PROS_PARAMS + 24)   /* a later stress of a word */
#define PROS_PARAM_Z  (PROS_PARAMS + 25)   /* pros_term_stress of a word's later symbols */
/* the end of a phoneme list (its other items: the triples, and PH_WORD_PAUSE, the space's word pause of @Q frames) */
#define PH_LIST_END    0xFF

/* isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static void par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }

/* $D158: the command events due at pros_pos (low bytes compared) get the segment index pros_seg_count in their
   cmd_ev_positions entry; cmd_ev_pos becomes the next entry's low byte (read even past cmd_ev_count). */
static void rewrite_events(fw *f)
{
    for (;;) {
        uint8_t n = fw_rd8(f, CMD_EV_NEXT);
        if (n >= fw_rd8(f, CMD_EV_COUNT) || fw_rd8(f, PROS_POS) != fw_rd8(f, CMD_EV_POS))
            return;
        uint8_t i = (uint8_t)(n << 1);                  /* TXA; ASL; TAX: an index of 128 or more aliases */
        fw_wr8(f, (uint16_t)(CMD_EV_POSITIONS + i), fw_rd8(f, PROS_SEG_COUNT));
        fw_wr8(f, (uint16_t)(CMD_EV_POSITIONS + 1 + i), fw_rd8(f, PROS_SEG_COUNT + 1));
        fw_wr8(f, CMD_EV_POS, fw_rd8(f, (uint16_t)(CMD_EV_POSITIONS + 2 + i)));
        fw_wr8(f, CMD_EV_POS + 1, 0);
        fw_wr8(f, CMD_EV_NEXT, (uint8_t)(fw_rd8(f, CMD_EV_NEXT) + 1));
    }
}

/* $D1E0: a space or punctuation ends the word. Its flags hold the next word's symbol count. */
static void word_start(fw *f)
{
    fw_wr8(f, PROS_FUNC_WORD, 0);
    fw_wr8(f, PROS_TERM_STRESS, 0);
    fw_wr8(f, FW_PROS_STRESS_TOGGLE, 0);
    fw_wr8(f, PROS_WORD_POS, 0);
    fw_wr8(f, FW_PROS_WORD_U, fw_rd8(f, PROS_PARAM_U));
    fw_wr8(f, PROS_WORD_LEN, fw_rd8(f, PROS_SYM_FLAG));
}

/* $D215-$D2DE: the accent, stress and tail terms of a phonetic symbol. */
static void symbol_terms(fw *f)
{
    uint8_t wp = (uint8_t)(fw_rd8(f, PROS_WORD_POS) + 1);
    fw_wr8(f, PROS_WORD_POS, wp);
    if (wp != 1) {                                      /* after a word's first symbol: Z instead of U */
        fw_wr8(f, FW_PROS_WORD_U, 0);
        fw_wr8(f, PROS_TERM_STRESS, fw_rd8(f, PROS_PARAM_Z));
    }
    fw_wr8(f, PROS_TERM_STRESS, (uint8_t)(fw_rd8(f, PROS_TERM_STRESS) + fw_rd8(f, FW_PROS_WORD_U)));
    fw_wr8(f, PROS_TERM_ACCENT, 0);
    uint8_t flag = fw_rd8(f, PROS_SYM_FLAG);
    if (fw_rd8(f, PROS_FUNC_WORD) == 0) {
        if (flag & FLAG_ACCENT) {                       /* $D243 */
            fw_wr8(f, PROS_TERM_ACCENT, fw_rd8(f, PROS_PARAM_W));
            fw_wr8(f, PROS_TERM_TAIL, 0);
            if (fw_rd8(f, PROS_ACCENT_STATE)) {
                fw_wr8(f, PROS_ACCENT_STATE, 0);
            } else {
                fw_wr8(f, FW_PROS_STRESS_TOGGLE, 1);
                fw_wr8(f, PROS_ACCENT_STATE, 1);
            }
        } else if (flag & FLAG_STRESS) {                /* $D269 */
            if (fw_rd8(f, FW_PROS_STRESS_TOGGLE)) {
                fw_wr8(f, PROS_TERM_STRESS, fw_rd8(f, PROS_PARAM_Y));
            } else {
                fw_wr8(f, FW_PROS_STRESS_TOGGLE, 1);
                fw_wr8(f, PROS_TERM_TAIL, 0);
                if (fw_rd8(f, PROS_ACCENT_STATE)) {     /* right after an accent: 0 */
                    fw_wr8(f, PROS_ACCENT_STATE, 0);
                    fw_wr8(f, PROS_TERM_STRESS, 0);
                } else {
                    fw_wr8(f, PROS_TERM_STRESS, fw_rd8(f, PROS_STRESS_PARAM));
                }
            }
        } else if (fw_rd8(f, PROS_ACCENT_STATE) && fw_rd8(f, PROS_TERM_TAIL) == 0) {   /* $D297 */
            fw_wr8(f, PROS_TERM_TAIL, (uint8_t)(0 - fw_rd8(f, PROS_PARAM_W)));         /* LDA tail (0); SEC; SBC W */
        }
    }
    if (flag & FLAG_NUCLEUS) {                          /* $D2A8 */
        if (flag & FLAG_STRESS) {
            fw_wr8(f, PROS_TERM_STRESS, fw_rd8(f, PROS_NUCLEUS_PARAM));
        } else if (fw_rd8(f, PROS_WORD_LEN) == fw_rd8(f, PROS_WORD_POS)) {
            fw_wr8(f, PROS_TERM_TAIL, fw_rd8(f, PROS_FINAL_PARAM));
            return;                                     /* BRA $D2DF: no V */
        }
    }
    if (fw_rd8(f, PROS_WORD_LEN) == fw_rd8(f, PROS_WORD_POS))   /* $D2CD: the word's last symbol */
        fw_wr8(f, PROS_TERM_TAIL, (uint8_t)(fw_rd8(f, PROS_TERM_TAIL) + fw_rd8(f, PROS_PARAM_V)));
}

/* ADC #m: a + m + the carry in *c; the carry out back in *c. */
static uint8_t adc(uint8_t a, uint8_t m, uint8_t *c)
{
    unsigned s = (unsigned)a + m + *c;
    *c = (uint8_t)(s >> 8);
    return (uint8_t)s;
}

/* $D2DF: pros_target, then the consonant's stress reset and the declination of pros_baseline. */
static void pitch_target(fw *f)
{
    uint8_t b = fw_rd8(f, PROS_BASELINE);
    uint8_t c = (b >> 2) & 1;                           /* LSR x3: the carry is the last bit shifted out */
    uint8_t a = (uint8_t)(b >> 3);
    a = adc(a, fw_rd8(f, PROS_TERM_ACCENT), &c);        /* no CLC between: each carry feeds the next ADC */
    a = adc(a, fw_rd8(f, PROS_TERM_STRESS), &c);
    a = adc(a, fw_rd8(f, PROS_TERM_TAIL), &c);
    a = adc(a, 105, &c);
    fw_wr8(f, PROS_TARGET, a);
    if (fw_rd8(f, PROS_FEATURES) & PHF_CONSONANT)      /* a consonant clears pros_term_stress after its target */
        fw_wr8(f, PROS_TERM_STRESS, 0);
    if (fw_rd8(f, PROS_FEATURES) != 0)                  /* a symbol with no features keeps the baseline */
        fw_wr8(f, PROS_BASELINE, (uint8_t)(fw_rd8(f, PROS_BASELINE) - fw_rd8(f, PROS_DECL_STEP)));
}

/* CLC; ADC #1; LSR A: (n + 1) / 2 in 8 bits, so n = $FF gives 0. */
static uint8_t half_count(uint8_t n) { return (uint8_t)((uint8_t)(n + 1) >> 1); }

/* steps lookups of ph_dur_range in the table at table (TAX; LDA table,X; DEY; BNE: 0 steps runs 256).
   *x is left as the last TAX leaves it, the index of the last lookup. */
static void scale_range(fw *f, uint16_t table, uint8_t steps, uint8_t *x)
{
    uint8_t a = fw_rd8(f, PH_DUR_RANGE);
    do {
        *x = a;
        a = fw_rom8(f, (uint16_t)(table + *x));
    } while (--steps != 0);
    fw_wr8(f, PH_DUR_RANGE, a);
}

/* $D363-$D3CC: the triple at list index r->y: ph_phoneme, ph_max_dur, ph_min_dur ($78 = the index of min); the
   range max - min shortened (ph_dur_shorten) and lengthened (ph_dur_lengthen) by the position's counts; min + range,
   at most 255, / 8 through the speed map (cmd_speed_map) is ph_duration; then the segment. */
static void append_triple(fw *f, fw_regs *r)
{
    fw_wr8(f, PH_PHONEME, fw_ind_y(f, PH_LIST_PTR, r->y));
    r->y++;
    fw_wr8(f, PH_MAX_DUR, fw_ind_y(f, PH_LIST_PTR, r->y));
    r->y++;
    fw_wr8(f, PH_MIN_DUR, fw_ind_y(f, PH_LIST_PTR, r->y));
    fw_wr8(f, FW_TMP_78, r->y);
    r->x = fw_rd8(f, PROS_POS);
    fw_wr8(f, PH_DUR_RANGE, (uint8_t)(fw_rd8(f, PH_MAX_DUR) - fw_rd8(f, PH_MIN_DUR)));
    uint8_t n = fw_rd8(f, (uint16_t)(PROS_SYM_SHORTEN + r->x));
    if (n != 0)
        scale_range(f, PH_DUR_SHORTEN, half_count(n), &r->x);
    r->x = fw_rd8(f, PROS_POS);
    n = fw_rd8(f, (uint16_t)(PROS_SYM_LENGTHEN + r->x));
    if (n != 0)
        scale_range(f, PH_DUR_LENGTHEN, ph_lengthen_count(f, half_count(n)), &r->x);
    unsigned d = (unsigned)fw_rd8(f, PH_DUR_RANGE) + fw_rd8(f, PH_MIN_DUR);
    r->y = (uint8_t)((d > 0xFF ? 0xFF : d) >> 3);       /* BCC; LDY #$FF; then LSR x3 */
    r->a = fw_ind_y(f, CMD_SPEED_MAP, r->y);
    fw_wr8(f, PH_DURATION, r->a);
    pros_append_segment(f, r->a, r->x, r->y);
}

/* $D31B ph_decode_symbol: ph_list_ptr = the phoneme list of pros_symbol; a segment per triple, or the space's word
   pause; the list's end goes on to the next position. r: the registers (X = pros_pos on entry), left as the ROM
   has them at its JMP $D152. */
static void decode_symbol(fw *f, fw_regs *r)
{
    /* ASL: a symbol from $80 carries into the next page ($9E74), so the pointer is at ph_list_ptrs + 2 * symbol */
    uint16_t p = (uint16_t)(PH_LIST_PTRS + 2 * fw_rd8(f, PROS_SYMBOL));
    fw_wr8(f, PH_LIST_PTR, fw_rom8(f, p));
    fw_wr8(f, PH_LIST_PTR + 1, fw_rom8(f, (uint16_t)(p + 1)));
    r->y = 0;
    for (;;) {                                          /* $D33F */
        r->a = fw_ind_y(f, PH_LIST_PTR, r->y);
        if (r->a == PH_LIST_END)
            return;
        if (r->a == PH_WORD_PAUSE) {                    /* $D348 */
            r->a = fw_rd8(f, CMD_WORD_PAUSE);
            if (r->a == 0)
                return;                                 /* @Q0: no pause */
            fw_wr8(f, FW_TMP_78, r->y);
            ph_append_pause(f, r->a);
            r->a = PROS_SEG_FLAGS_GEN;                  /* ph_append_pause's last LDA #$80 (the segment flags) */
            r->y = fw_rd8(f, FW_TMP_78);
            r->x = fw_rd8(f, PROS_POS);
            fw_wr8(f, FW_UTT_CONTENT, 0xFF);            /* LDM #$FF */
            return;
        }
        fw_wr8(f, FW_UTT_CONTENT, 0xFF);                /* $D360 LDM #$FF */
        append_triple(f, r);
        r->a = fw_rd8(f, PROS_SYM_FLAG);
        if (r->a & FLAG_UNRELEASED)
            return;                                     /* the closure only */
        r->y = (uint8_t)(fw_rd8(f, FW_TMP_78) + 1);
    }
}

uint8_t pros_phrase_gen(fw *f, uint8_t *x, uint8_t *y)
{
    fw_regs r = { 0, 0, 0, 0 };
    r.a = pros_declination(f);
    r.x = fw_rd8(f, PROS_POS);                          /* pros_declination_step's LDX pros_pos */
    r.y = pros_declination_y(f);                        /* pros_declination_step's STY, before any negation */
    fw_wr8(f, PROS_LAST_TARGET, 0x80);
    fw_wr8(f, PROS_TERM_TAIL, 0);
    fw_wr8(f, PROS_ACCENT_STATE, 0);
    fw_wr8(f, PROS_TERM_STRESS, 0);
    fw_wr8(f, FW_PROS_WORD_U, 0);
    fw_wr8(f, PROS_WORD_LEN, fw_rd8(f, PROS_SYM_FLAGS));   /* pros_sym_flags[0]: the first word's symbol count */
    fw_wr8(f, PROS_SEG_COUNT, 0);
    fw_wr8(f, PROS_SEG_COUNT + 1, 0);
    r.a = 0xFF;
    fw_wr8(f, PROS_POS, r.a);
    for (;;) {
        fw_wr8(f, PROS_POS, (uint8_t)(fw_rd8(f, PROS_POS) + 1));   /* $D152 */
        par_ready(f, r);
        rewrite_events(f);
        r.x = fw_rd8(f, PROS_POS);
        if (r.x >= fw_rd8(f, TN_PHON_LEN))
            break;
        uint8_t sym = fw_rd8(f, (uint16_t)(TN_PHONETIC + r.x));   /* $D1A2 */
        fw_wr8(f, PROS_SYMBOL, sym);
        r.y = sym;
        fw_wr8(f, PROS_CLASS, fw_rom8(f, (uint16_t)(PH_SYMBOL_CLASS + r.y)));
        fw_wr8(f, PROS_FEATURES, fw_rom8(f, (uint16_t)(PH_SYMBOL_FEATURES + r.y)));
        fw_wr8(f, PROS_SYM_FLAG, fw_rd8(f, (uint16_t)(PROS_SYM_FLAGS + r.x)));
        if (sym == '~')
            fw_wr8(f, PROS_FUNC_WORD, 1);               /* a function word (for hypermode) */
        uint8_t cls = fw_rd8(f, PROS_CLASS);
        if (sym == ' ' || (cls & PHC_PUNCT)) {
            if (sym != ' ' && fw_rd8(f, CMD_PHRASE_PAUSE)) {   /* $D1D6: @D0 adds no pause */
                ph_append_phrase_pause(f);              /* A and Y preserved */
                ph_append_phrase_pause(f);
            }
            word_start(f);
            if (sym != ' ') {                           /* punctuation also ends the accent */
                fw_wr8(f, PROS_TERM_TAIL, 0);
                fw_wr8(f, PROS_ACCENT_STATE, 0);
                r.a = 0;
                continue;                               /* no segment */
            }
        } else {
            r.y = cls;                                  /* $D20F LDY pros_class; TYA */
            r.a = cls;
            if (!(cls & PHC_PHONETIC))
                continue;                               /* a marker: no segment */
            symbol_terms(f);
            pitch_target(f);
        }
        if (fw_rd8(f, CMD_HYPER) && fw_rd8(f, PROS_FUNC_WORD)) {   /* $D312: @H1 skips a function word */
            r.a = fw_rd8(f, PROS_FUNC_WORD);
            continue;
        }
        decode_symbol(f, &r);
    }
    ph_append_pause(f, 4);                              /* $D18E */
    r.y = ph_append_pause(f, PROS_SEG_FLAGS_GEN);       /* the first pause left A = $80: this one lasts $80 frames */
    r.a = fw_rd8(f, PROS_SEG_COUNT);                    /* 16-bit decrement: the index of the last segment */
    if (r.a == 0)
        fw_wr8(f, PROS_SEG_COUNT + 1, (uint8_t)(fw_rd8(f, PROS_SEG_COUNT + 1) - 1));
    fw_wr8(f, PROS_SEG_COUNT, (uint8_t)(fw_rd8(f, PROS_SEG_COUNT) - 1));
    *x = r.x;
    *y = r.y;
    return r.a;
}
```

`src/engine/thunks.c` (Tasks 3 and 4 added their includes, register mappings, `THUNK` lines and rows, in address order):
- Replace

```c
#include "firmware/pros_decl.h"
#include "firmware/ph_expand.h"
```

with

```c
#include "firmware/pros_decl.h"
#include "firmware/ph_expand.h"
#include "firmware/pros_gen.h"
```

- Replace

```c
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}

/* pros_declination $D3DD: no register input;
```

with

```c
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}

/* pros_phrase_gen $D129: no register input. A, X and Y out as the ROM leaves them: A = pros_seg_count's low byte
   before the final DEC, X = pros_pos (= tn_phon_len), Y = the second end pause's LDY pros_seg_count. Its one ROM
   caller, pros_make_segments, returns at once ($CABC RTS); its callers (boot_reset $A687, boot_selftest_sound $A7BA
   and the C tn_convert_clause) call boot_loop_play next, which loads A from $6B and, when that is 0, returns with X
   and Y untouched. No reader of them was found; they are defined, so all three are declared. */
static void pros_phrase_gen_regs(fw *f, cpu740 *c)
{
    uint8_t x, y;
    c->a = pros_phrase_gen(f, &x, &y);
    c->x = x;
    c->y = y;
}

/* pros_declination $D3DD: no register input;
```

- Replace

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_declination)
```

with

```c
THUNK(pros_reset_seg_ptrs)
THUNK(pros_phrase_gen)
THUNK(pros_declination)
```

- Replace

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_declination",   0xD3DD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination },
```

with

```c
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_phrase_gen",    0xD129, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_phrase_gen },
    { "pros_declination",   0xD3DD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination },
```

The table stays in address order, with 24 rows (`diff_lts`'s `DIFF_MAX` is 24).

- [ ] **Step 4: Faithfulness check against the dumps**

Read each block of `build/ghidra740/dump/rom_D129_pros_phrase_gen.txt` side by side with its C, instruction by instruction. Confirm every branch, every RAM write and every 8-bit wrap:

- **$D129–$D14F.** `JSR pros_declination` → `r.a`. X and Y come from `rom_D3DD_pros_declination.txt` and `rom_D40D_pros_declination_step.txt`: `LDX pros_pos` at $D40D, then `STY`/`TYA`, then the rising path's `SBC`. Then the eight stores, in order: `pros_last_target`, `pros_term_tail`, `pros_accent_state`, `pros_term_stress`, `$1C1F`, `pros_word_len` (from `$1D00`), both bytes of `pros_seg_count`, and `pros_pos = $FF` with A = $FF.
- **$D152–$D155.** `INC pros_pos`, then `JSR isr_par_ready` with A, X and Y as each path into $D152 leaves them (see "ROM facts checked"; `rom_C5D6_isr_par_ready.txt` preserves all three).
- **$D158–$D185** → `rewrite_events`:
  - `BCS` on `CPX cmd_ev_count`;
  - `CMP cmd_ev_pos` (low byte);
  - `TXA/ASL/TAX`;
  - the stores to `$1206,X` and `$1207,X`, the `LDA $1208,X`, `cmd_ev_pos+1 = 0`, and `INC cmd_ev_next`.
- **$D187–$D1A1** → the code after the loop:
  - `CPX tn_phon_len` / `BCC`, so the loop ends at X ≥ `tn_phon_len`;
  - `LDA #4`, then `JSR ph_append_pause` twice. The second call gets A = $80 (`rom_CA15_ph_append_pause.txt` ends with `LDA #$80; STA (flags),Y`).
  - the `DEC` of the high byte only when the low byte is 0, then the low byte. The return registers are A = the low byte read at $D196, X from $D187, and Y from the second pause.
- **$D1A2–$D1C4.** The four stores (`pros_symbol`, `pros_class`, `pros_features`, `pros_sym_flag`). The class and features reads use Y = the symbol. `~` → `pros_func_word = 1`.
- **$D1C7–$D209.** The space (`CMP #$20`) is tested before class bit 0 (`BBS0 A`). The phrase pauses come only when `cmd_phrase_pause` ≠ 0. They are `rom_C9B8_ph_append_phrase_pause.txt`, which pushes and pulls A and Y through `$CA6D`. Then `word_start`'s six stores in order. Then, for punctuation only, `pros_term_tail` and `pros_accent_state` = 0, and A = 0 at the `JMP`.
- **$D20F–$D213.** `LDY pros_class; TYA; BBC7 A`: A = Y = class at the `JMP $D152`.
- **$D215–$D2DE** → `symbol_terms`. Check each `BBC5`, `BBC6` and `BNE` against the C's `if`s. The `CMP #1` is on the incremented `pros_word_pos`. `CLC` comes before the U add. `SEC; SBC` gives `0 − W`. `pros_final_param` takes the `BRA $D2DF` that skips V.
- **$D2DF–$D30C** → `pitch_target`. Three `LSR`s, then four `ADC`s with no `CLC`. Then `BBC6` on the features (consonant), then `BEQ` on features 0, then `SEC; SBC pros_decl_step`.
- **$D312–$D319.** `cmd_hyper` first, then `pros_func_word`; A = `pros_func_word` at the `JMP`.
- **$D31B–$D33D** → `decode_symbol`'s pointer. The `CLC` is dead (`ASL` sets C). `$9D74,Y` and `$9E74,Y` both equal `PH_LIST_PTRS + 2 * symbol`. Both branches set Y = 0.
- **$D33F–$D35D.** The `$FF` test, then `$5F`. With `cmd_word_pause` = 0 the routine leaves A = 0. Otherwise: `STY $78`, `JSR ph_append_pause` (A = @Q), `LDY $78`, `LDX pros_pos`, `LDM #$FF,$6B`.
- **$D360–$D3CC** → `append_triple`:
  - `LDM` comes before the triple's reads; `$78` = the min index;
  - `SEC; SBC` for the range;
  - the shortening count at `$1E00,X`, with `CLC; ADC #1; LSR A`, then `TAX`/`LDA $C77B,X`/`DEY`/`BNE`;
  - `LDX pros_pos`, then the lengthening count at `$1F00,X` through `JSR ph_lengthen_count` (`rom_D43E_ph_lengthen_count.txt`: Y = its result), then `$C67B,X`;
  - `CLC; ADC ph_min_dur`, `BCC`, `LDY #$FF`, `LSR` ×3, `LDA ($3A),Y`, `STA ph_duration`;
  - `JSR pros_append_segment` with A = the duration, X as left, Y = the index (`rom_CA3B_pros_append_segment.txt`: `isr_par_ready` first, A and Y pushed and pulled, X untouched).
- **$D3CF–$D3DA.** `BBC4` on `pros_sym_flag` (A = the flag at the `JMP`), else `LDY $78; INY`.

For the thunk outputs, re-read `rom_CA92_pros_make_segments.txt` ($CAB9 `JSR`, $CABC `RTS`), `rom_A61C_boot_reset.txt` ($A684–$A68A), `rom_A79F_boot_selftest_sound.txt` ($A7B7–$A7BD) and `rom_A7D4_boot_loop_play.txt` ($A7D4–$A7DE). Confirm that no other ROM code calls `$D129` or enters `$D31B` (the dumps' comments name `pros_phrase_gen` in many files, so scan the operands):

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xD129,0xD31B) for op in (0x20,0x4C)]"
```

Expected: `JSR` (`0x20`) operands only at `0xcab9` (`$D129`, in `pros_make_segments`); none for `$D31B`; no `JMP` (`0x4c`) to either.

- [ ] **Step 5: Run the tests**

Run, in order:
- `cmd //c ".\build.bat test fw_pros"`
- `cmd //c ".\build.bat test diff_lts"`
- `cmd //c ".\build.bat test golden_hybrid"`
- `cmd //c ".\build.bat test"`

Expected:
- `suite_fw_pros` ok, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`:
  - All 24 routines print `N calls checked` with N > 0. The new line is `pros_phrase_gen      N calls checked`, reached once per `pros_make_segments` call (every clause and the boot self-test). The proof run of this plan printed `pros_phrase_gen      192 calls checked`.
  - `diff_lts` observes the rom engine, where the ROM `pros_phrase_gen` still calls the ROM leaves, so the leaves' counts from Tasks 3–4 are unchanged.
  - 0 mismatches. `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, both unchanged.
- `golden_hybrid`: `39 corpus files x 25 modes (all, then each routine alone)`, all identical.
- The full run: all 31 suites ok (`31 suite(s), 0 failure(s), 0 skip(s)`), with no warnings, the firmware boundary check passing, and `firmware_data` still at 38197 converted bytes (this task adds no data).

If `diff_lts` reports a mismatch, find the root cause in the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

Then confirm spec criterion 5: the CLI's hybrid output equals the rom engine's for every new text. Each text is read from its corpus file, as M4.2's Task 9 did. The outputs go to a scratch directory outside the repo, always with `-o`:

```bash
SP="$(mktemp -d)"
for t in phrase_pauses baseline phonetic_stress phrase_127 seg_256 seg_wrap_phrase seg_wrap_word; do
  ./build/apollo2.exe --engine rom --text-file tests/corpus/$t.txt -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file tests/corpus/$t.txt -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done
```

Expected: seven lines, `phrase_pauses same`, `baseline same`, `phonetic_stress same`, `phrase_127 same`, `seg_256 same`, `seg_wrap_phrase same`, `seg_wrap_word same`, with no `failed` or `DIFFERS` line.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/pros_gen.h src/firmware/pros_gen.c src/firmware/fw.h src/engine/thunks.c tests/test_fw_pros.c
git commit -m "The phrase generator in C: pros_phrase_gen with the phoneme decoder, calling the C appenders, lengthening and declination; hook pros_phrase_gen (M4.3a 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Parent spec rev 10, M4.3a status and README (criterion 8)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 10**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 10)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 10)* Revision 10 records milestone 4.3a: the segment generator in C (phoneme expansion, the phrase generator, the segment appenders and the declination) and the phoneme and prosody tables converted (design: docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.2 done**`). Replace its last sentence, `Next: M4.3, phoneme expansion and prosody.`, with:

   `Next: M4.3, phoneme expansion and prosody, in two plans (M4.3a, M4.3b). *(rev 10)* **M4.3a done** <DATE>: the segment generator runs as C (pros_phrase_gen with the phoneme decoder, the segment appenders, ph_lengthen_count and the declination; 24 routines hooked in all); the phoneme and prosody tables are converted (38,197 bytes in all); seven new corpus goldens (rom engine) reach the phrase pauses, the rising and level baselines and the 16-bit segment-index page wraps; diff_lts and golden_hybrid (39 files x 25 modes) pass. Next: M4.3b, the stress and phrase passes and pros_make_segments.`

   Replace `<DATE>` with the date of your commit (`date +%F`).

- [ ] **Step 2: M4.3a spec status and As built**

In `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-03-m4-3a-segment-generator.md)`

Then add a final section headed `6. As built` (a level-2 heading, after §5), with one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–5 of the plan, keeping only those that contradict or sharpen the spec;
- the code: the thunk outputs as finally declared for the 8 new hooks (read the rows in `src/engine/thunks.c`) and why they are what they are; the C file layout (`pros_seg.c`, `ph_expand.c`, `pros_decl.c`, `pros_gen.c`, `pros_addr.h`) and the helper structure of `pros_phrase_gen`; the RAM names actually added to `fw.h`;
- the corpus texts as finally committed, if any differs from §4.1.

Follow the style of the "As built" sections in `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md` (§7) and the M4.1 spec (§7). Start it with the M4.2 spec's sentence: "Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited."

These differences are known and must appear (re-read the named code before writing each bullet, and state what was built, not this list's wording):
- **The end of the utterance** (§2.2 says "two 4-frame pauses"): one pause of 4 frames, then one of 128 ($80) frames. `ph_append_pause` leaves A = $80 (its last `LDA #$80`, the segment flags), and the second `JSR` at $D193 takes that as its duration (`pros_gen.c`, the end of `pros_phrase_gen`).
- **The duration loops** (§2.7, §4.4 say both run 256 steps for a count of $FF): only the shortening loop can run 256 steps (Y = (n + 1) / 2 = 0 straight into `DEY / BNE`). The lengthening count goes through `ph_lengthen_count`, so a count of $FF gives a half-count of 0 and entry 0 of `ph_lengthen_steps`, the `RTS` opcode $60: 96 steps (`pros_gen.c` `append_triple`; `ph_expand.c`; the unit test `test_gen_duration_loops`).
- **`pros_phrase_gen`'s exit registers** (§2.3 says Y = the last list index): A = `pros_seg_count`'s low byte before the final `DEC`, X = `pros_pos` (= `tn_phon_len`), Y = the old `pros_seg_count` low byte that the second end pause's `LDY` leaves. The C signature is `uint8_t pros_phrase_gen(fw *f, uint8_t *x, uint8_t *y)`, returning A.
- **Thunk outputs as finally declared** (read the rows in `src/engine/thunks.c`): A, X and Y for `ph_append_phrase_pause`, `ph_append_pause`, `pros_append_segment`, `pros_reset_seg_ptrs`, `pros_phrase_gen`, `pros_declination` and `pros_declination_step`; A and Y for `ph_lengthen_count`. §2.3 named A = $0E for `pros_reset_seg_ptrs`, A for `pros_declination_step` and the result in A for `ph_lengthen_count`. Why, in one line each from the thunk comments: X and Y reach `isr_par_ready` at $D155 through `JMP $D152` (and Y at $CF28 in `pros_mark_phrases`, and at $CB1F in `pros_mark_stress` after `pros_reset_seg_ptrs`); `ph_append_pause`'s A is the second end pause's duration and goes on to `isr_par_ready` after a word pause; `ph_lengthen_count`'s Y is the loop count its caller reads (`DEY`); `pros_phrase_gen`'s registers have no reader but are defined. Also: `pros_declination`'s A on the rising path is the restored `pros_baseline`, not the negated step (§2.3), and its Y is the step before the negation.
- **The corpus texts retuned from §4.1**: `phrase_127` is `@X1. ` followed by 15 × `BABABABA ` and `BABABAB.`, exactly 127 letters (the survey's 128-letter text never reaches $D41A); `seg_wrap_word` is two clauses, each led by `@Q9 `: the first 55 words of the cycle then `.`, then the same 55 words then ` oh.` (one clause wraps only once, at the word pause; the second wraps at a segment). Quote the committed files.
- **The registers at `isr_par_ready` $D155** depend on how the loop was entered: A = $FF, X = `pros_pos` and Y = `pros_declination`'s Y the first time; after punctuation A = 0 and Y = the symbol; after a marker A = Y = `pros_class`; after a hypermode skip A = `pros_func_word`, Y = `pros_class`; at a list's end A = $FF (or 0 at the space's item with @Q0) and Y = the list index; after a word pause A = $80, Y = `$78`; after an unreleased stop's closure A = `pros_sym_flag`, Y = the speed-map index. After a triple, X is the index of the last `ph_dur_lengthen` lookup when the position has a lengthening count, else `pros_pos`; that X also goes to `pros_append_segment`'s `isr_par_ready`.
- **Phrase pauses** (§2.7 says only that 0 becomes 1): after `.` `?` `!` the pause is `cmd_phrase_pause` in full, unmasked; after `,` `;` `:` it is `(cmd_phrase_pause >> 1) & $0F`, after any other symbol `(cmd_phrase_pause >> 2) & $0F`; the 0 → 1 bump (`INC A` at $C9EC) applies on all three paths. Every pause also stores $3B into `ph_phoneme`.
- **`isr_par_ready` preserves A, X and Y** (§2.4 says A), which is why `pros_append_segment`'s thunk can hand its entry registers back.
- **The C layout**: `pros_addr.h` (the ROM addresses), `pros_seg.c` (the four appenders, sharing one static pause body and one page-wrap helper), `ph_expand.c` (only `ph_lengthen_count`), `pros_decl.c` (the declination) with `pros_declination_y` in `pros_decl.h` (the Y the ROM `pros_declination` leaves, recovered from RAM because the C returns only A; `pros_phrase_gen` uses it for its first `isr_par_ready`), and `pros_gen.c` (`pros_phrase_gen`; `ph_decode_symbol` is a label inside it, so it and the other blocks are static helpers: `rewrite_events`, `word_start`, `symbol_terms`, `pitch_target`, `decode_symbol`, `append_triple`). The RAM names added to `fw.h` are exactly `FW_UTT_CONTENT` $6B, `FW_PROS_STRESS_TOGGLE` $1C0D and `FW_PROS_WORD_U` $1C1F (with the existing `FW_TMP_78`), the set §2.6 expected.
- **The converted data** (§3): no phoneme has a name in `ghidra740/annotations`, so the list comments are the symbols and the annotation's notes; `fw_data.h` also gained `UNUSED_A397`, `PH_WORD_PAUSE`, the class and feature bit names `PHC_*` / `PHF_*` and the types `fw_ph_triple` / `fw_ph_list`; the encoder now also checks that M4.2's `cmd_speed_maps` point at the start of a map.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
and tn_append_phonetic (M4.1), and the command parser: the @ command matcher, the clause scan, the dispatcher
and every deferred command handler (M4.2); the rest still runs on the emulated CPU.
```

with:

```
and tn_append_phonetic (M4.1), the command parser: the @ command matcher, the clause scan, the dispatcher
and every deferred command handler (M4.2), and the segment generator: phoneme expansion, the phrase generator,
the segment appenders and the declination (M4.3a); the rest still runs on the emulated CPU.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all 31 suites ok.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md README.md
git commit -m "Spec rev 10: milestone 4.3a done (the segment generator in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
