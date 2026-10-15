# M4.4a The Playback Set-up in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the 20 playback set-up routines to C and prove them against the ROM, live and with the goldens. They run once per utterance or once per segment: the start of playback, segment reading, pitch scaling, the boundary and dominance pass, track planning and the voice set-up.

**Architecture:**
- **Shared helpers first.** `src/firmware/fw_ops.h` replaces the helpers duplicated across the `pros_*.c` files. `MACHINE_MAX_HOOKS` goes to 256 (Task 1).
- **Corpus.** Three new corpus texts with rom-engine goldens reach the pitch, voicing and duration paths no text reached (Task 2).
- **Data.** The voice tables, `par_target_bias`, `frm_snapshot_map` and the @T tables are converted (Task 3).
- **New C files.** The routines run on the firmware's RAM image through the `fw` context, one static helper per ROM block:
  - `src/firmware/par_segment.c`, `pros_play.c`, `par_boundary.c` and `par_start.c`;
  - every routine takes `fw_regs *r` (the entry registers in, the ROM's exit registers out);
  - Tasks 4–6 build them from the leaves up.
- **C calls C.** The C callers call the C routines directly, with the registers flowing as in the ROM. The C `cmd_apply_pitch` calls the C `par_setup_voice`.
- **Hooks.** All 20 routines, giving 48 replacements and 49 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md` (M4.4a spec). Its foundations are:
- the M4.3b spec, `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md` (§6 "As built");
- the M4.3a spec, `docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md` (§2.5, the output contract to playback);
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 11).

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
  - `data.txt` and `functions.txt` in the same folder.

**Correctness rules**
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The three new goldens are generated once by the rom engine in Task 2. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.4a spec §2.7):
  - every RAM write, the write-only and dead stores included: `par_row2_ptr`, `par_table_copy`, the stride low byte at $B3D4, `par_seg_target[0]`, `pros_pitch_err`, $1811 on the R = 0 below-limit path, `FW_TMP_76` and `FW_TMP_78`;
  - the 8-bit wraps and the carry chains exactly as the ROM has them (no `CLC` where the ROM has none);
  - signed and unsigned tests exactly as the ROM's flags make them;
  - the running X of the boundary pass, never assumed 0;
  - reads past the 86 phonemes as the ROM does them.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts` excludes it.
- ROM data is read only through `fw_rom8`, `fw_ind_y` or `fw_mem8` at its ROM address, from the converted image. The playback pointers point into ROM during a @T effect, so they are never read with `fw_rd8`.
- Thunk outputs: A, X and Y for all 20, each with a comment saying what the ROM callers read after the call, checked in the dumps.
- `diff_lts` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's standing rule and overrides any other attribution line the environment suggests.

---

### Task 1: `fw_ops.h` and the four `pros_*.c` files moved onto it; `MACHINE_MAX_HOOKS` 256 and `DIFF_MAX` 48 (spec §2.3, §2.6)

**ROM facts checked / what was verified while proving the plan** (scratch worktree off main 5f28310; everything below was run, nothing is copied from the spec):
- **No ROM code changes in this task.** The four helpers of `fw_ops.h` are the copies they replace, character for character in their bodies: `par_ready` (`pros_gen.c`, `pros_phrase.c`, `pros_stress.c`, three identical one-liners), `adc` (`pros_gen.c`, `pros_stress.c`, identical), `inc8` (`pros_phrase.c`, `pros_seg.c`, identical), and `count_add` (`pros_phrase.c`) / `add` (`pros_stress.c`), identical apart from the parameter name `arr`/`array`. Only `static` becomes `static inline` and the names get the `fw_` prefix. `diff_lts` and `golden_hybrid` prove the rename: every `N calls checked` line and the `golden_hybrid` line are identical before and after this task.
- **`fw_ops.h` adds no include the four files lacked.** All four already include `tn_convert.h` (which includes `fw.h`); `fw_ops.h` includes the same two. Each file keeps its `#include "tn_convert.h"`: `pros_seg.c` still uses `ROM_ISR_PAR_READY` itself (next point), and the others are left alone (a pure rename).
- **Other repeated steps in the four files, reported and NOT hoisted** (conventions: nothing beyond the fixed list without saying so):
  - `pros_seg.c`'s `pros_append_segment` makes the same `isr_par_ready` call inline (`fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET);`, line 49 after the task). It could become `fw_par_ready(f, r)`; left inline.
  - In-line `INC`s that `fw_inc8` could express: `pros_gen.c` `CMD_EV_NEXT` (`rewrite_events`) and `PROS_POS` ($D152), `pros_stress.c` `FW_PROS_SYLLABLES` and `PROS_POS`. Left inline.
  - The symbol class and features lookups: `pros_stress.c` has the helpers `sym_class`/`sym_features`; `pros_gen.c` ($D1xx, `decode_symbol`) and `pros_phrase.c` (`load_symbol`) do the same two `fw_rom8` reads inline. Not hoisted.
  - No other helper of the four files has a copy in another of them (`seg_store`, `half_count`, `scale_range`, `phonetic` are each in one file only).
- **Spec §2.3 says "The test for the full table moves to the new limit". There was no such test.** `tests/test_machine_hooks.c` never filled the table; `machine_hook` (`src/machine/machine.c`) refuses when `hook_count == MACHINE_MAX_HOOKS` or the address is already hooked, and nothing tested the first condition. This task adds `test_hook_table_full`: `MACHINE_MAX_HOOKS - 1` observers at $8000 up plus a replacement at $C100 fill the table, the next distinct address is refused, and the last entry still fires (so the lookup reaches the end of the table). Its first line `CHECK_EQ(MACHINE_MAX_HOOKS, 256)` gives the RED step; at 32 everything else in it already passes (exactly 1 failure, proved).
- **Cost of 256 hooks.** `machine_hook_entry` is 24 bytes on x64 (`uint16_t` + padding + two pointers), so `machine.hooks` grows from 768 to 6,144 bytes per `machine`. `hook_map` is tested first (`machine.c` `find_hook`), so the lookup cost is unchanged; `machine_clone` (one `memcpy` of the struct, per `diff_lts` call) copies 5,376 bytes more. The full `build.bat test` took 172 s after this task (it includes the build).
- **`DIFF_MAX` 48 at 28 rows** is harmless: `diff_lts` checks `fw_replacement_count <= DIFF_MAX` and sizes `g_obs`/`g_checked` with it. It installs 28 observers plus its `boot_loop_play` stop now, 49 hooks after Task 6.
- **Counts this task leaves unchanged** (proved, full run): `31 suite(s), 0 failure(s), 0 skip(s)`; `42 corpus files x 29 modes (all, then each routine alone)`; 28 `diff_lts` lines, every N identical to the state before (`cmd_apply_speaker 95` … `ph_lengthen_count 1101`); `1 mid-clause flush calls skipped (golden_hybrid covers them)`; `2 unwound calls compared`; `38197 converted bytes compared with the ROM, 0 differ`; `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 15784 (-6.3 dBFS)`.

**Files:**
- Create: `src/firmware/fw_ops.h`
- Modify: `src/firmware/pros_gen.c`, `src/firmware/pros_phrase.c`, `src/firmware/pros_seg.c`, `src/firmware/pros_stress.c` (their helper copies removed, the calls renamed), `src/machine/machine.h` (`MACHINE_MAX_HOOKS`), `tests/test_machine_hooks.c` (`test_hook_table_full`), `tests/test_diff_lts.c` (`DIFF_MAX`)

**Interfaces:**
- Consumes: `fw`, `fw_regs`, `fw_rd8`, `fw_wr8`, `fw_rom_call`, `FW_ROM_CALL_BUDGET` (`fw.h`); `ROM_ISR_PAR_READY` (`tn_convert.h`); `machine_hook`, `machine_hook_entry`, `hook_count` (`machine.h`); the harness of `tests/test_machine_hooks.c` (`load_main`, `run_to`, `hook_observe`, `hook_replace`, `g_m`, `g_calls`).
- Produces (used by Tasks 4–6; they do not add to it):

```c
/* src/firmware/fw_ops.h */
static inline void fw_par_ready(fw *f, fw_regs r);                       /* JSR isr_par_ready */
static inline uint8_t fw_adc(uint8_t a, uint8_t m, uint8_t *c);         /* ADC with carry in and out */
static inline void fw_inc8(fw *f, uint16_t addr);                        /* INC addr */
static inline void fw_add8(fw *f, uint16_t arr, uint8_t x, uint8_t n);  /* LDA arr,X; CLC; ADC #n; STA arr,X */
```

  and `MACHINE_MAX_HOOKS` 256 (`src/machine/machine.h`), `DIFF_MAX` 48 (`tests/test_diff_lts.c`), enough for Tasks 4–6 (48 replacements; `diff_lts` 48 observers + its stop, `golden_hybrid` 48 + its `isr_par_ready` hook).

- [ ] **Step 1: Write the failing test**

In `tests/test_machine_hooks.c`:
- Replace

```c
    machine_clear_hooks(&g_m);
    CHECK_EQ(g_m.hook_count, 0);
}
```

  with

```c
    machine_clear_hooks(&g_m);
    CHECK_EQ(g_m.hook_count, 0);
}

/* The hook table holds exactly MACHINE_MAX_HOOKS hooks (M4.4a spec 2.3: 256, enough for every routine left through
   M4.6; diff_lts installs one observer per replacement plus its boot_loop_play stop). MACHINE_MAX_HOOKS - 1
   observers at $8000 up (never executed here) and a replacement at $C100 fill it: the next distinct address is
   refused, and the last entry still fires. */
static void test_hook_table_full(void)
{
    CHECK_EQ(MACHINE_MAX_HOOKS, 256);
    load_main(0xC100);
    for (unsigned i = 0; i < MACHINE_MAX_HOOKS - 1; i++)
        CHECK(machine_hook(&g_m, (uint16_t)(0x8000 + i), hook_observe, NULL));
    CHECK(machine_hook(&g_m, 0xC100, hook_replace, NULL));
    CHECK_EQ(g_m.hook_count, MACHINE_MAX_HOOKS);
    CHECK(!machine_hook(&g_m, 0xC200, hook_replace, NULL));   /* the table is full */
    CHECK_EQ(g_m.hook_count, MACHINE_MAX_HOOKS);
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x77);   /* the last entry replaced $C100 */
    CHECK_EQ(g_calls, 1);            /* and no observer fired */
}
```

  (The replaced block is the end of `test_observer_lets_rom_run`, the only place these three lines occur.)
- In `suite_machine_hooks`, replace

```c
    test_observer_lets_rom_run();
```

  with

```c
    test_observer_lets_rom_run();
    test_hook_table_full();
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test machine_hooks"`
Expected: FAIL, exactly one failure:

```
tests\test_machine_hooks.c:83: CHECK_EQ failed: MACHINE_MAX_HOOKS == 256 (got 0x20, want 0x100)
suite_machine_hooks          FAILED
1 suite(s), 1 failure(s), 0 skip(s)
```

(With 32 the rest of the test passes: the table logic does not depend on the size.)

- [ ] **Step 3: Raise the limits**

`src/machine/machine.h`: replace

```c
#define MACHINE_MAX_HOOKS          32
```

with

```c
#define MACHINE_MAX_HOOKS          256
```

`tests/test_diff_lts.c`: replace

```c
#define DIFF_MAX        28
```

with

```c
#define DIFF_MAX        48
```

Run: `cmd //c ".\build.bat test machine_hooks"`
Expected: `suite_machine_hooks          ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.

- [ ] **Step 4: Create `src/firmware/fw_ops.h`**

```c
#ifndef FW_OPS_H
#define FW_OPS_H

/* Small steps the ported routines share (M4.4a spec 2.6). */

#include "fw.h"
#include "tn_convert.h"   /* ROM_ISR_PAR_READY */

/* JSR isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static inline void fw_par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }

/* ADC: a + m + the carry in *c; the carry out back in *c. */
static inline uint8_t fw_adc(uint8_t a, uint8_t m, uint8_t *c)
{
    unsigned s = (unsigned)a + m + *c;
    *c = (uint8_t)(s >> 8);
    return (uint8_t)s;
}

/* INC addr: mod 256. */
static inline void fw_inc8(fw *f, uint16_t addr) { fw_wr8(f, addr, (uint8_t)(fw_rd8(f, addr) + 1)); }

/* LDA arr,X; CLC; ADC #n; STA arr,X: an 8-bit add into an array byte. */
static inline void fw_add8(fw *f, uint16_t arr, uint8_t x, uint8_t n)
{
    fw_wr8(f, (uint16_t)(arr + x), (uint8_t)(fw_rd8(f, (uint16_t)(arr + x)) + n));
}

#endif
```

`fw_ops.h` clashes with no existing file (`ls src/firmware | grep fw_ops` shows only it). It is a header, so the unique-basename rule for `.c` files is not involved.

- [ ] **Step 5: Move the four `pros_*.c` files onto it (a pure rename)**

**5a. Include it and delete the copies.** In each of the four files, replace the one line

```c
#include "tn_convert.h"
```

with

```c
#include "fw_ops.h"
#include "tn_convert.h"
```

Then delete these blocks (each block is deleted together with the one blank line that follows it):

`src/firmware/pros_gen.c`, `src/firmware/pros_phrase.c` and `src/firmware/pros_stress.c` (one copy in each):

```c
/* isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static void par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }
```

`src/firmware/pros_gen.c` and `src/firmware/pros_stress.c` (one copy in each):

```c
/* ADC #m: a + m + the carry in *c; the carry out back in *c. */
static uint8_t adc(uint8_t a, uint8_t m, uint8_t *c)
{
    unsigned s = (unsigned)a + m + *c;
    *c = (uint8_t)(s >> 8);
    return (uint8_t)s;
}
```

`src/firmware/pros_phrase.c`:

```c
/* LDA arr,X; CLC; ADC #n; STA arr,X: an add to a position's count, mod 256. */
static void count_add(fw *f, uint16_t arr, uint8_t x, uint8_t n)
{
    fw_wr8(f, (uint16_t)(arr + x), (uint8_t)(fw_rd8(f, (uint16_t)(arr + x)) + n));
}
```

`src/firmware/pros_phrase.c` and `src/firmware/pros_seg.c` (one copy in each):

```c
static void inc8(fw *f, uint16_t addr) { fw_wr8(f, addr, (uint8_t)(fw_rd8(f, addr) + 1)); }
```

`src/firmware/pros_stress.c`:

```c
/* LDA array,X; CLC; ADC #n; STA array,X: an 8-bit count. */
static void add(fw *f, uint16_t array, uint8_t x, uint8_t n)
{
    fw_wr8(f, (uint16_t)(array + x), (uint8_t)(fw_rd8(f, (uint16_t)(array + x)) + n));
}
```

**5b. Rename the calls** (after 5a, so no definition is left to rename):

```bash
sed -i -E 's/\bpar_ready\(/fw_par_ready(/g; s/\badc\(/fw_adc(/g' src/firmware/pros_gen.c
sed -i -E 's/\bpar_ready\(/fw_par_ready(/g; s/\bcount_add\(/fw_add8(/g; s/\binc8\(/fw_inc8(/g' src/firmware/pros_phrase.c
sed -i -E 's/\binc8\(/fw_inc8(/g' src/firmware/pros_seg.c
sed -i -E 's/\bpar_ready\(/fw_par_ready(/g; s/\badd\(/fw_add8(/g; s/\badc\(/fw_adc(/g' src/firmware/pros_stress.c
```

(`\b` does not match after `_`, so `fw_rom_call(`, `fw_adc(` and the like are never touched. The renames: pros_gen.c 4 `fw_adc` + 1 `fw_par_ready`; pros_phrase.c 2 `fw_inc8` + 5 `fw_add8` + 1 `fw_par_ready`; pros_seg.c 6 `fw_inc8`; pros_stress.c 16 `fw_add8` + 2 `fw_adc` + 1 `fw_par_ready`.)

**5c. Keep the comment column.** Five renamed lines carry a trailing comment that the longer name pushed right. Replace each line (as 5b leaves it) with the second form:

`src/firmware/pros_gen.c`:

```c
    a = fw_adc(a, fw_rd8(f, PROS_TERM_ACCENT), &c);        /* no CLC between: each carry feeds the next ADC */
```

```c
    a = fw_adc(a, fw_rd8(f, PROS_TERM_ACCENT), &c);     /* no CLC between: each carry feeds the next ADC */
```

`src/firmware/pros_stress.c`, in order:

```c
    fw_wr8(f, at, fw_adc(fw_rd8(f, at), 3, &c));           /* $CCB2: its carry reaches $CCD5 (LDA, BBC keep it) */
        fw_add8(f, PROS_SYM_SHORTEN, x, 3);                 /* $CCBF, then BRA $CCDA */
        fw_wr8(f, at, fw_adc(fw_rd8(f, at), 0x0C, &c));    /* $CCD5: no CLC */
        fw_par_ready(f, *r);                               /* $CB1F */
```

```c
    fw_wr8(f, at, fw_adc(fw_rd8(f, at), 3, &c));        /* $CCB2: its carry reaches $CCD5 (LDA, BBC keep it) */
        fw_add8(f, PROS_SYM_SHORTEN, x, 3);             /* $CCBF, then BRA $CCDA */
        fw_wr8(f, at, fw_adc(fw_rd8(f, at), 0x0C, &c)); /* $CCD5: no CLC */
        fw_par_ready(f, *r);                            /* $CB1F */
```

(The line `fw_add8(f, PROS_SYM_SHORTEN, x, final ? 6 : 2);   /* $CE15 */` was already past the column and keeps its three spaces.)

**5d. Check the result:**

```bash
grep -nE "\b(par_ready|adc|inc8|count_add|add)\(" src/firmware/pros_gen.c src/firmware/pros_phrase.c src/firmware/pros_seg.c src/firmware/pros_stress.c
grep -c "fw_par_ready\|fw_adc\|fw_inc8\|fw_add8" src/firmware/pros_gen.c src/firmware/pros_phrase.c src/firmware/pros_seg.c src/firmware/pros_stress.c
git diff --stat src/firmware
```

Expected: the first `grep` prints nothing; the second prints

```
src/firmware/pros_gen.c:5
src/firmware/pros_phrase.c:8
src/firmware/pros_seg.c:6
src/firmware/pros_stress.c:19
```

and `git diff --stat` shows

```
 src/firmware/pros_gen.c    | 22 +++++-------------
 src/firmware/pros_phrase.c | 28 ++++++++---------------
 src/firmware/pros_seg.c    | 15 ++++++-------
 src/firmware/pros_stress.c | 56 +++++++++++++++++-----------------------------
 4 files changed, 42 insertions(+), 79 deletions(-)
```

(`fw_ops.h` is untracked until it is staged, so it is not in this list.)

- [ ] **Step 6: Faithfulness check**

No ROM routine is ported in this task, so there is no dump to compare and no caller `JSR`/`JMP` scan. Check instead that the moved bodies are unchanged: `git diff src/firmware` must show only (a) the added `#include "fw_ops.h"` lines, (b) the deleted helper blocks of Step 5a, and (c) call lines that differ only by the new name and the comment column. Compare the four bodies in `fw_ops.h` with the deleted ones: `fw_par_ready`, `fw_adc` and `fw_inc8` are the same statements; `fw_add8` is `count_add`/`add` with the parameter named `arr`. The carry chains that use `fw_adc` keep their order and their comments (`pros_gen.c` $D2DF: no `CLC` between the four adds; `pros_stress.c` $CCB2/$CCD5: the carry of the first reaches the second). `diff_lts` and `golden_hybrid` (Step 7) prove the rename on the corpus.

- [ ] **Step 7: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected (the proof run's output; it took 172 s, build included):
- `31 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing.
- `suite_machine_hooks          ok`.
- `firmware_data`: `38197 converted bytes compared with the ROM, 0 differ`.
- `diff_lts`: 28 `N calls checked` lines, every N unchanged from before this task (`cmd_apply_speaker 95`, `cmd_run_due 4950`, `tn_convert_clause 206`, `pros_append_segment 2249`, `pros_reset_seg_ptrs 365`, `pros_make_segments 207`, `pros_mark_stress 207`, `pros_mark_phrases 207`, `pros_store_phrase_len 374`, `pros_phrase_gen 207`, `pros_declination 374`, `pros_declination_step 374`, `ph_lengthen_count 1101`, …), then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, 0 mismatches.
- `golden_hybrid`: `42 corpus files x 29 modes (all, then each routine alone)`, all identical.
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 15784 (-6.3 dBFS)`.

If `diff_lts` or `golden_hybrid` reports any difference, the rename changed behaviour: compare the diff with Step 6 and fix it. Never loosen a comparison.

- [ ] **Step 8: Commit**

```bash
git add src/firmware/fw_ops.h src/firmware/pros_gen.c src/firmware/pros_phrase.c src/firmware/pros_seg.c src/firmware/pros_stress.c src/machine/machine.h tests/test_machine_hooks.c tests/test_diff_lts.c
git commit -m "fw_ops.h: the helpers the pros_*.c files duplicated (fw_par_ready, fw_adc, fw_inc8, fw_add8), a pure rename; MACHINE_MAX_HOOKS 256 with a full-table test, DIFF_MAX 48 (M4.4a 2.3, 2.6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Corpus: three new texts and their rom-engine goldens (spec §4.1)

**ROM facts checked / what was verified while proving the plan** (scratch worktree off main 5f28310 with Task 1 applied, ROM engine only; everything below was run, nothing is copied from the spec):
- **How the goldens are made.** `suite_golden` (`tests/test_golden.c`) speaks each `CORPUS_NAMES` text (whole file, trailing CR/LF stripped, no boot) on the rom engine and compares the hex frame list with `tests/golden/<name>.frm`; a missing file prints `missing tests/golden/<name>.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` and counts one failure. With `APOLLO2_UPDATE_GOLDEN` set (non-empty), it **rewrites every golden** (all 44 texts and `boot`, 45 `updated … (N frames)` lines), refusing one whose run did not reach idle, produced no frames or logged an unmodelled access. So `git status` is the proof that every existing golden is byte-identical. `suite_golden_hybrid` (`tests/test_golden_hybrid.c`) reads the same goldens; for a missing one it fails `CHECK failed: want != NULL` once per mode. This is the procedure of M4.3b Task 1, unchanged.
- **The RED step.** With `corpus_list.h` edited and no goldens: `2 suite(s), 90 failure(s), 0 skip(s)` = 3 `missing` lines + 3 × 29 modes `want != NULL` (proved). `golden_hybrid` already prints `45 corpus files x 29 modes (all, then each routine alone)` (44 texts + boot; 28 replacements + "all").
- **Generation.** `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"` printed, among its 45 `updated` lines, `pitch_extremes.frm (212 frames)`, `voicing_levels.frm (518 frames)`, `fast_effect.frm (67 frames)`, and `2 suite(s), 0 failure(s), 0 skip(s)` (the hybrid suite then compares against the files just written). `git status --short tests/golden` showed exactly the three new files as `??`; `git diff --stat tests/golden` was empty: every existing golden byte-identical.
- **Every target was reached, each text alone, with the branch probe** (a scratch tool, not part of the plan: `NOCORPUS=1 brprobe44.exe TEXTFILE`, run from the main checkout's root; it prints `BR <addr> taken N fall N` for the 78 conditional branches of the 20 routines). The branches are in `rom_BDD3_pros_scale_target.txt` ($BE0A `BPL $BE0E`, $BE3D `BCS $BE4D`, $BE63 `BCS $BE3F`), `rom_B983_pros_microprosody.txt` ($B9A5 `BCC $B9B1`, $B9AE `BCS $B9B1`), `rom_BFC3_par_scale_voicing.txt` ($BFC5 `BEQ $BFD3`, $BFCE `BNE $BFD4`, $BFE0 `BNE $BFEF`, $BFE3 `BEQ $BFD1`, $BFF1 `BNE $BFFF`, $BFF4 `BEQ $BFD1`, $C017 `BNE $C01C`) and `rom_C29C_par_scale_duration.txt` ($C2AA `BCC $C2AE`). Targets in **bold**:

| Text | BE0A | BE3D | BE63 | B9A5 | B9AE | BFC5 | BFCE | BFE0 | BFE3 | BFF1 | BFF4 | C017 | C2AA |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `pitch_extremes` | **taken 33**, fall 7 | **taken 2**, fall 16 | taken 14, **fall 8** | taken 3, **fall 1** | taken 3, **fall 2** | taken 0, fall 57 | taken 23, fall 0 | taken 23, fall 0 | not executed | taken 23, fall 0 | not executed | taken 23, fall 0 | not executed |
| `voicing_levels` | taken 0, fall 72 | taken 0, fall 40 | taken 32, fall 0 | not executed | not executed | **taken 4**, fall 87 | taken 35, **fall 10** | taken 25, **fall 10** | **taken 2**, fall 8 | taken 15, **fall 10** | **taken 2**, fall 8 | taken 1, **fall 14** | not executed |
| `fast_effect` | taken 0, fall 7 | taken 0, fall 6 | taken 1, fall 0 | not executed | not executed | taken 0, fall 27 | taken 17, fall 0 | taken 17, fall 0 | not executed | taken 17, fall 0 | not executed | taken 17, fall 0 | taken 96, **fall 9** |

  So every target of spec §4.1 is reached by its own text: `pitch_extremes` R ≥ 4, the $FF cap, the floor 4, the + 5 that wraps and the − 5 that borrows; `voicing_levels` @B0, @B2, @B3, @B5, v = 0 under @B2 and @B3, and v = 8; `fast_effect` the duration capped at 31. `$BD85` stays taken 0 in all three (spec §4.4: unreachable). Without `@u937-` (`@B0 Hi, there. @B2 Hi, there. @B3 Hi, there. @B5 Hi, there. Hello.`) `BFC5` is `taken 0, fall 91`: the @u offset is what makes the P10 target 8.
- **With the whole corpus.** The probe over the 41 existing texts plus the three new files: of the 78 branches only `BR BD85 taken 0 fall 32840` is one-sided. Every other conditional branch of the 20 routines is seen both ways (spec §4.1 confirmed).
- **The coverage file shows nine of the targets** (`apollo2_coverage`, Step 5): the instruction reached only by the target direction. Before this task (main 5f28310 + Task 1, `build/apollo2_coverage.exe -o build/m44a_before.cov`) all nine are `MISSING` and the summary is `rom 4054, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`; after: all nine `ok` and `rom 4084` (30 new instruction addresses, all inside the routines: B9A7 B9B0 BE4D BE4F BE65 BE67 BFD0 BFE2 BFE3 BFE5 BFE6 BFE7 BFE9 BFEA BFEB BFEC BFEE BFF3 BFF4 BFF6 BFF7 BFF8 BFFA BFFB BFFC BFFE C019 C01A C01B C2AC), the other three numbers unchanged.
- **Four targets cannot be seen in the coverage file**, because their target instruction is also reached another way: `$BE0A` taken ($BE0E, also the fall-through of $BE0C), `$BFC5` taken ($BFD3, also from `$BFC9` for v = 1), `$BFE3` and `$BFF4` taken ($BFD1, also the fall-through from $BFD0 and the `BEQ` at $BFD9). Only the probe shows them (table above); `diff_lts` and `golden_hybrid` compare the C on these very texts from Task 4 on.
- **Peak.** The b11 corpus peak rises from 15784 (-6.3 dBFS) to **16038 (-6.2 dBFS)**; the plain peak stays 20522 (-4.1 dBFS). Still inside -12...-1 dBFS, no clipping.
- **Timing** (this machine): the RED run of the two golden suites 169 s, the generation 187 s, the full `build.bat test` after the task 185 s (each includes the build).
- **Counts after this task** (full run): `31 suite(s), 0 failure(s), 0 skip(s)`; `45 corpus files x 29 modes`; 28 `diff_lts` lines; `38197 converted bytes compared with the ROM, 0 differ`; `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged. Every `diff_lts` N grows with the texts except `lts_affix_prev` and `tn_say_char` (before -> after, for the record: cmd_apply_speaker 95 -> 101, cmd_run_due 4950 -> 5199, cmd_scan_clause 207 -> 225, cmd_match_table 2623 -> 2895, lts_affix_next 4062 -> 4152, lts_affix_prev 1476 -> 1476, lts_sym_vowel 1167 -> 1209, lts_match_symbol 14938 -> 15488, tn_say_char 19 -> 19, lts_match_list 11350 -> 11671, tn_rule_skip_text 30185 -> 31490, tn_rule_skip_field 13468 -> 13841, tn_append_phonetic 870 -> 893, lts_convert_char 861 -> 884, tn_convert_clause 206 -> 224, cmd_hex_value 130 -> 144, ph_append_phrase_pause 330 -> 360, ph_append_pause 784 -> 820, pros_append_segment 2249 -> 2307, pros_reset_seg_ptrs 365 -> 398, pros_make_segments 207 -> 225, pros_mark_stress 207 -> 225, pros_mark_phrases 207 -> 225, pros_store_phrase_len 374 -> 407, pros_phrase_gen 207 -> 225, pros_declination 374 -> 407, pros_declination_step 374 -> 407, ph_lengthen_count 1101 -> 1139).
- Each text runs on a fresh machine (no state carries between corpus files), so `fast_effect`'s `@W9` and `pitch_extremes`' `@R7`/`@FF`/`@F0` affect only their own file.

**Files:**
- Create: `tests/corpus/pitch_extremes.txt`, `tests/corpus/voicing_levels.txt`, `tests/corpus/fast_effect.txt`, and their three goldens `tests/golden/pitch_extremes.frm`, `tests/golden/voicing_levels.frm`, `tests/golden/fast_effect.frm` (generated)
- Modify: `tests/corpus_list.h`

**Interfaces:**
- Consumes: nothing. (Task 1 is applied first; the counts below assume it, but nothing here depends on it.)
- Produces: the corpus names `"pitch_extremes"`, `"voicing_levels"` and `"fast_effect"` (44 names). Every later task's `golden_hybrid` and `diff_lts` runs over them (Tasks 4–6 prove each new C routine against the ROM on these texts); Task 6's CLI check (`--engine hybrid` equals `--engine rom`) uses the three files.

- [ ] **Step 1: Write the corpus texts**

Each file holds exactly one line, ending with a single LF. Write them with the script below, so the bytes are exact:

```bash
python - <<'EOF'
t = {
    "pitch_extremes": "@R7 @FF Take a cake. @F0 Do a deck.",
    "voicing_levels": "@B0 Hi, there. @B2 Hi, there. @B3 Hi, there. @B5 Hi, there. @u937- Hello.",
    "fast_effect": "@W9 @TC Done.",
}
for name, text in t.items():
    open("tests/corpus/" + name + ".txt", "w", newline="\n").write(text + "\n")
EOF
wc -c tests/corpus/pitch_extremes.txt tests/corpus/voicing_levels.txt tests/corpus/fast_effect.txt
```

Expected `wc -c` (bytes, including the newline): 36, 74, 14 (and `124 total`).

The resulting lines, for review:

| File | Line | Reaches |
|---|---|---|
| `pitch_extremes.txt` | `@R7 @FF Take a cake. @F0 Do a deck.` | `pros_scale_target` R ≥ 4, the cap at $FF, the floor at 4; `pros_microprosody`'s + 5 that wraps and − 5 that borrows |
| `voicing_levels.txt` | `@B0 Hi, there. @B2 Hi, there. @B3 Hi, there. @B5 Hi, there. @u937- Hello.` | `par_scale_voicing` @B0, @B2, @B3, @B5; v = 0 under @B2 and @B3 (the pause dominates a vowel); v = 8 (P10 target 63 + the @u offset $C9) |
| `fast_effect.txt` | `@W9 @TC Done.` | `par_scale_duration`'s cap at 31 (@T effect 12: phoneme 83 dominates 82, internal duration 40) |

- [ ] **Step 2: Add them to the corpus list**

In `tests/corpus_list.h`:
- Replace the comment line

```c
   The last three (M4.3b) reach the stress and phrase passes' rare paths: a word of five or more syllables;
```

  with:

```c
   The next three (M4.3b) reach the stress and phrase passes' rare paths: a word of five or more syllables;
```

- Replace the comment line

```c
   after a stress toggle, and a linking R after a vowel that counted no syllable. */
```

  with:

```c
   after a stress toggle, and a linking R after a vowel that counted no syllable.
   The last three (M4.4a) reach the playback set-up's rare paths: @R7 with the pitch capped at $FF under @FF and
   floored at 4 under @F0, where the microprosody's + 5 and - 5 would wrap; @B0, @B2, @B3 and @B5, with a pause
   dominating a vowel (a voicing value 0), and a P10 target that @u937- makes 8; and a @T effect at @W9, whose
   duration 40 the speed map caps at 31. */
```

- Replace the last line of `CORPUS_NAMES`

```c
    "long_words", "linking_r", "syllabic_stress"
```

  with:

```c
    "long_words", "linking_r", "syllabic_stress", \
    "pitch_extremes", "voicing_levels", "fast_effect"
```

(The rows above stay as they are; one new row for M4.4a. The longest comment line is 113 characters, as before.)

- [ ] **Step 3: Run the golden suites to see them fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL, with `2 suite(s), 90 failure(s), 0 skip(s)`:
- `suite_golden` prints `missing tests/golden/<name>.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` for each of `pitch_extremes`, `voicing_levels`, `fast_effect` (3 failures);
- `suite_golden_hybrid` prints `45 corpus files x 29 modes (all, then each routine alone)` and fails `CHECK failed: want != NULL` 3 x 29 = 87 times.

- [ ] **Step 4: Generate the goldens with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: `2 suite(s), 0 failure(s), 0 skip(s)`, 45 `updated` lines (update mode rewrites every golden), among them (the frame counts are the rom engine's; any difference means a different text):

```
  updated tests/golden/pitch_extremes.frm (212 frames)
  updated tests/golden/voicing_levels.frm (518 frames)
  updated tests/golden/fast_effect.frm (67 frames)
```

Then run `git status --short tests/golden` and `git diff --stat tests/golden`. Expected: exactly these three lines from the first, nothing from the second:

```
?? tests/golden/fast_effect.frm
?? tests/golden/pitch_extremes.frm
?? tests/golden/voicing_levels.frm
```

If any existing `.frm` shows as modified, STOP and report: the oracle is not deterministic, and nothing may be committed. If a frame count differs from the list above, STOP and check the text of that file against Step 1 byte for byte.

- [ ] **Step 5: Confirm the coverage**

Run (the coverage file goes under `build/`, which is git-ignored; the tool runs boot and every `CORPUS_NAMES` text, so run it from the repo root):

```bash
./build/apollo2_coverage.exe -o build/m44a.cov
for a in BE4D BE65 B9A7 B9B0 BFD0 BFE2 BFF3 C019 C2AC; do grep -q "^x rom $a$" build/m44a.cov && echo "$a ok" || echo "$a MISSING"; done
```

Expected: the summary line `rom 4084, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges` (it was `rom 4054` before this task), then nine `ok` lines. If any address is MISSING, STOP and report.

What each address stands for (each is reached only by the direction named):

| Address | Text | Meaning |
|---|---|---|
| `BE4D` | `pitch_extremes` | `pros_scale_target`: `BCS $BE4D` at $BE3D taken, centre + deviation carries, capped at $FF |
| `BE65` | `pitch_extremes` | `pros_scale_target`: `BCS $BE3F` at $BE63 falls, centre − deviation borrows, floored at 4 |
| `B9A7` | `pitch_extremes` | `pros_microprosody`: `BCC $B9B1` at $B9A5 falls, the + 5 wraps and is not stored |
| `B9B0` | `pitch_extremes` | `pros_microprosody`: `BCS $B9B1` at $B9AE falls, the − 5 borrows and is not stored |
| `BFD0` | `voicing_levels` | `par_scale_voicing`: `BNE $BFD4` at $BFCE falls, @B0 |
| `BFE2` | `voicing_levels` | `par_scale_voicing`: `BNE $BFEF` at $BFE0 falls, @B2 |
| `BFF3` | `voicing_levels` | `par_scale_voicing`: `BNE $BFFF` at $BFF1 falls, @B3 |
| `C019` | `voicing_levels` | `par_scale_voicing`: `BNE $C01C` at $C017 falls, @B5 |
| `C2AC` | `fast_effect` | `par_scale_duration`: `BCC $C2AE` at $C2AA falls, a duration ≥ 31 capped at 31 |

The coverage file has no branch directions, so it cannot show four more targets of spec §4.1, whose target instructions are also reached by other paths: `BPL $BE0E` at $BE0A **taken** (R ≥ 4; $BE0E is also the fall-through of $BE0C) in `pitch_extremes`; `BEQ $BFD3` at $BFC5 **taken** (v = 8; $BFD3 is also reached from $BFC9 for v = 1), and `BEQ $BFD1` at $BFE3 and at $BFF4 **taken** (v = 0 under @B2 and @B3; $BFD1 also follows $BFD0 and the `BEQ` at $BFD9), in `voicing_levels`. All four were confirmed with the branch probe while this plan was proved (table at the top of the task: `BR BE0A taken 33`, `BR BFC5 taken 4`, `BR BFE3 taken 2`, `BR BFF4 taken 2`, each text alone); `diff_lts` and `golden_hybrid` compare the C on these very texts from Task 4 on.

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected:
- All 31 suites are ok (`31 suite(s), 0 failure(s), 0 skip(s)`).
- `golden_hybrid` prints `45 corpus files x 29 modes (all, then each routine alone)`.
- `firmware_data` prints `38197 converted bytes compared with the ROM, 0 differ`.
- `diff_lts` shows 0 mismatches, 28 `N calls checked` lines (every N greater than 0; the counts grow with the new texts: the proof run printed `cmd_apply_speaker 101`, `cmd_run_due 5199`, `tn_convert_clause 224`, `pros_append_segment 2307`, `pros_reset_seg_ptrs 398`, `pros_make_segments 225`, `pros_phrase_gen 225`, `pros_declination 407`, `ph_lengthen_count 1139`), `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, both unchanged.
- `suite_synth` includes the new goldens in its corpus peak; the proof run printed `corpus peak plain 20522 (-4.1 dBFS)` and `corpus peak b11 16038 (-6.2 dBFS)`. If it reports clipping, or a plain peak outside -12...-1 dBFS, STOP and report. Do not change any calibration.

- [ ] **Step 7: Commit**

```bash
git add tests/corpus/pitch_extremes.txt tests/corpus/voicing_levels.txt tests/corpus/fast_effect.txt tests/golden/pitch_extremes.frm tests/golden/voicing_levels.frm tests/golden/fast_effect.frm tests/corpus_list.h
git commit -m "Corpus: three texts for the playback set-up's rare paths (@R7 with the pitch capped at \$FF and floored at 4, where the microprosody's +5 and -5 would wrap; @B0, @B2, @B3 and @B5 with a voicing value 0, and a P10 target that @u937- makes 8; a @T effect at @W9 whose duration the speed map caps at 31), goldens from the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(The `\$FF` keeps Bash from expanding `$FF` inside the double quotes; the message reads `$FF`.)

---

### Task 3: The playback data as readable C: the voice tables, `par_target_bias`, `frm_snapshot_map` and the @T tables (spec §3)

**ROM facts checked** (byte by byte in `UK_V2.01_4.04_ROM.BIN`, file offset = CPU address, against `ghidra740/annotations/params.txt`, `frame.txt`, `command.txt`, `types.txt`, `build/ghidra740/dump/data.txt` and the dumps `rom_BEE2_par_boundary_cur_dom.txt`, `rom_BF27_par_boundary_next_dom.txt`, `rom_BF6C_par_read_dominance.txt`, `rom_C039_par_boundary_value.txt`, `rom_B917_frm_snapshot.txt`, `rom_BB35_par_reset_tracks.txt`, `rom_BBA7_cmd_ev_sound_start.txt`, `rom_A7FE_cmd_select_voice_a.txt`, `rom_A814_cmd_select_voice_b.txt`, `rom_B41D_frm_build_rest_frame.txt`; proved in a scratch worktree off main 5f28310 with this task alone applied):
- **Every address in the conventions is right**: `CMD_SOUND_TABLE` $9F74, `CMD_SOUND_DATA` $9FFC, `FRM_SNAPSHOT_MAP` $B773, `PAR_TARGET_BIAS` $B7A7, `PAR_VOICE_A` $D46A, `PAR_VOICE_B` $E5E3, `PAR_PHONEMES` 86 (= the count bytes at $D469 and $E5E2). The only absolute readers in the whole dump set are the 13 lines of the scan in Step 6 (`cmd_ev_sound_start` for the table, `frm_snapshot`/`par_reset_tracks` through the 1-based base $B772, `par_boundary_value` for the bias); the voice tables and `cmd_sound_data` are reached only through pointers (`cmd_select_voice_a/b` load `#$6A/#$D4` and `#$E3/#$E5` into `par_table_ptr`; `cmd_ev_sound_start` loads the four playback pointers from the table).
- **Row 3 is the external duration and row 4 the internal one — the conventions' reading is confirmed, so the fields are `ext_dur, int_dur` in that (ROM) order. Spec §3 lists them the other way round ("internal duration, external duration"); the As built should record it.** The proof: `par_read_dominance` moves `par_row_ptr` three times ($BF7B target→fixed, $BF8D fixed→prop, $BFBF prop→row 3; its dump header says it "leaves par_row_ptr on the external-duration row"). Then `par_boundary_cur_dom` (the current phoneme dominates) stores row 3 in `par_next_start_dur[p+1]` ($BF06 `STA $171D,X`: the next segment's start transition, inside the next, dominated phoneme) and row 4 in `par_trans_dur` ($BF11: the playing segment's end transition, inside the current, dominant phoneme); `par_boundary_next_dom` (the next phoneme dominates) stores row 3 in `par_trans_dur` ($BF4B: inside the current, dominated phoneme) and row 4 in `par_next_start_dur` ($BF56: inside the next, dominant phoneme). So in both cases row 3 lies in the dominated neighbour and row 4 in the dominant phoneme itself, exactly as `types.txt` names the fields (`p1_fn_ext_dur` at offset 430 = row 5 of the table, `p1_fn_int_dur` at 516 = row 6).
- **Deviation from the conventions: `fw_par_rule.fixed` is `int8_t`, not `uint8_t`.** The fixed contribution is signed (`types.txt`: "fixed contribution (signed)"; `par_boundary_value` adds it in 8 bits and limits the sum to 1…$3F): the table holds −26 … 50 (bytes $E6 … $32), so as `uint8_t` the data would read 230, 246, 252 where it means −26, −10, −4. The encoder writes `(uint8_t)fixed`, so the image is byte-identical. Nothing but the encoder and `test_firmware_data` uses the struct: the ported C reads the tables through `fw_ind_y` at ROM addresses. The other four fields stay `uint8_t`.
- **The voice tables**: 52 rows of 86 bytes each (4,472), the rank row, the spare row, then per parameter P1…P10 (FN, F1, F2, F3, ALF, A1, A2, A3, AHF, V; `types.txt`) the target, fixed, prop, ext_dur and int_dur rows. Voice A ends at $E5E2 (voice B's count byte) and voice B at $F75B (`cmd_version_lang_text`, converted in M4.1), so the converted range $D44A–$F77A becomes one. Over all 86 phonemes of both voices: every target is 1–63, every proportion 0–8 (so the 8 − proportion weight never wraps, spec §2.7), ext_dur 0–20, int_dur 0–63, fixed −26 … 50. **Voice B differs from voice A in 464 bytes, all in the target and fixed rows of P1–P4** (the annotation's claim; the test pins it). The pause, phoneme 59 ($3B, `PH_PAUSE_PHONEME`), has rank 31; byte 255 of the rank row (the read past the table at the end of a @T effect, spec §2.7) is the P1 target of phoneme 83 and is 32 in both voices. The spare row is mostly 32 and is never read.
- **No phoneme has a name** in the annotations or the dumps (as M4.3a found). Each row's comment therefore gives what they do say: the symbols whose phoneme lists (`fw_ph_lists`, M4.3a) give the phoneme, with its place in a list of two or three (e.g. `'A' (1/2)`); the notes of `data.txt` (`$83`, `$84`, `$87`, `$8C`: only @X phonetic text; `$98`: letter-to-sound, MEGATON; `$C0`: a long syllabic consonant, LEVEL, NOVEL); the @T effects that use it. Phonemes 77 and 82–84 occur only in @T effects, and 25, 58, 78–81 and 85 in no list and no effect (`data.txt`, `ph_list_ptrs_hi`); the generated comments agree with both statements.
- **`par_target_bias`** ($B7A7–$B7B0) = 18, 4, 10, 26, then six 0: P1–P4 only (`params.txt`). `par_boundary_value` reads it as $B7A7,X with X = 0–9 ($C04D, $C0B6, $C0C5). The zero bytes after it belong to `unused_B7B1` and stay unconverted.
- **`frm_snapshot_map`** ($B773–$B77C) = 10, 8, 6, 4, 2, 0, 1, 3, 5, 7, the frame bytes b10, AHF, A3, A2, A1, ALF, FN, F1, F2, F3 (`frame.txt`), read 1-based as $B772,X with X = 10 down to 1 ($B924 `frm_snapshot`, $BB4E `par_reset_tracks`). This task names the frame bytes `FRAME_ALF` … `FRAME_PITCH` (b0–b10, from `types.txt`'s `frame20`) in `fw_data.h`, an addition to the conventions' names; nothing in `src/` or `tests/` uses `FRAME_` yet (only `APOLLO_FRAME_BYTES`/`SYNTH_FRAME_BYTES`/`ISR_FRAME_*`, which do not clash).
- **The @T tables.** `cmd_ev_sound_start` takes X = effect × 8 (`util_shl3`) and loads the four playback pointers from $9F74–$9F7B,X: phoneme, duration, pitch, flags ($BBD3–$BBF6). For all 17 effects the flags pointer is $9FFC, and `cmd_sound_data` starts with 18 bytes of $80 (the flag array, `PROS_SEG_FLAGS_GEN`; playback never reads it). Then, for effects 0–16 in order, the phoneme array with its $FF end, the duration array and the pitch array follow end to end, each array exactly where the table points, the durations and pitches one per phoneme. The phoneme counts are 3 (effects 0–7), 5, 10, 14, 18, 6, 10, 14, 18 (8–15) and 1 (16): 120 segments, 18 + 3 × 120 + 17 ends = 395 bytes, ending at $A186; $A187 is `ph_lists` (M4.3a). The longest effects (11 and 15) have 18 phonemes, the length of the shared flag array. The phonemes are 59 (the pause, in every effect), 77 (@T0–@T3), 84 (@T4–@T7), 83 (@T8–@TF) and 82 (@TC–@TF); effect 16 (`@TG` or `@TX`, `cmd_event.c`) is one pause of 10 frames at pitch 128. The pitch byte read one past an effect's pitch array (spec §2.7) is the next effect's first phoneme, $3B, or for effect 16 the $5F at $A187: both are converted.
- **Types for the @T tables** (the drafter's choice): `fw_cmd_sound_effect` = the phoneme count and the three arrays as numbers (`phoneme`, `duration`, `pitch`, each `[CMD_SOUND_MAX]`), `fw_cmd_sound_flags[CMD_SOUND_MAX]`, and `fw_cmd_sound` = one `cmd_sound_table` entry as four references (`const uint8_t *`) to those arrays. The encoder lays the arrays out end to end in ROM order, records where each lands, and writes each table pointer by looking up its reference, as the rule-list pointers are written from where the lists landed (`list_addr`); a reference to anything else fails. New constants beside the conventions' names: `CMD_SOUND_EFFECTS` 17, `CMD_SOUND_MAX` 18, `CMD_SOUND_END` $FF. `cmd_data.c` includes `../pros_seg.h` for `PROS_SEG_FLAGS_GEN` (it only includes `fw.h`; the `build.bat` syntax check without `/Isrc` passes).
- **Byte count**: 2 × 4,472 + 10 + 10 + 136 + 395 = **9,495**; `compared` goes from 38,197 to **47,692**. The half-open `k_rom` ranges: $9804–$9F74 and $A187–$A397 join into $9804–$A397; $D44A–$D46A, $E5E2–$E5E3 and $F75B–$F77B join into $D44A–$F77B; $B773–$B77D and $B7A7–$B7B1 are added.
- **How the data was made.** The C below was generated from the ROM by a scratch script while drafting (not part of the plan, and `tools/extract_tables.py` is not changed, unlike M4.3a's Task 2) and is checked byte for byte by `firmware_data`. Because `extract_tables.py --command-only` rewrites `cmd_data.c`, this task adds one sentence to that file's header saying the @T section is not the tool's. The encoder's checks were tried while proving: an effect count of 2 instead of 1 for effect 16 fails the build of the image (`$A187 written twice`), and a table entry pointing at the wrong array of an effect fails `firmware_data` (`1 differ`, and the reference check in `test_counts`).

**Files:**
- Create: `src/firmware/data/par_data.c`
- Modify: `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `src/firmware/data/cmd_data.c`, `tests/test_firmware_data.c`

**Interfaces:**
- Consumes: the encoder helpers in `fw_data.c` (`put`, `put_word`, `fail`, `check_end`); `PAR_VOICE_A_COUNT`, `PAR_VOICE_B_COUNT`, `CMD_VERSION_LANG_TEXT`, `PH_LISTS`, `fw_par_voice_a_count`, `fw_par_voice_b_count` (`fw_data.h`, M4.2/M4.3a); `PROS_SEG_FLAGS_GEN` (`pros_seg.h`, M4.3a).
- Produces (in `fw_data.h`):
  - the data addresses `CMD_SOUND_TABLE 0x9F74`, `CMD_SOUND_DATA 0x9FFC`, `FRM_SNAPSHOT_MAP 0xB773`, `PAR_TARGET_BIAS 0xB7A7`, and `PAR_PHONEMES 86`; `PAR_VOICE_A` and `PAR_VOICE_B` (existing) are now converted. The C of Tasks 5–6 reads them only through `fw_rom8`/`fw_ind_y` at these addresses: Task 5 the voice tables (through `par_rank_ptr` and `par_row_ptr`) and `PAR_TARGET_BIAS` (`par_boundary_value`); Task 6 the voice tables, `cmd_sound_data` through the playback pointers (`par_read_segment`, `par_play_start`) and `FRM_SNAPSHOT_MAP` (`par_reset_tracks`). Task 4's leaves read none of them. `cmd_sound_table` is read by no ported routine (only by `cmd_ev_sound_start`, M4.4b).
  - `CMD_SOUND_EFFECTS`, `CMD_SOUND_MAX`, `CMD_SOUND_END`, and the frame byte names `FRAME_ALF` … `FRAME_PITCH`;
  - the types `fw_par_rule` (`target`, `int8_t fixed`, `prop`, `ext_dur`, `int_dur`), `fw_par_phoneme` (`rank`, `spare`, `p[10]`), `fw_cmd_sound_effect`, `fw_cmd_sound`, and the arrays `fw_par_voice_a[PAR_PHONEMES]`, `fw_par_voice_b[PAR_PHONEMES]`, `fw_par_target_bias[10]`, `fw_frm_snapshot_map[10]`, `fw_cmd_sound_flags[CMD_SOUND_MAX]`, `fw_cmd_sound_effects[CMD_SOUND_EFFECTS]`, `fw_cmd_sound_table[CMD_SOUND_EFFECTS]`. Only the encoder and `test_firmware_data` use them.
  - Encoder step 9 in `fw_image_build`. From this task on, the image covers every ROM byte the 20 routines read, so their C never faults in `fw_rom8` (the Task 4–6 unit tests build on `fw_image_get()`).

No name collides: `fw_ram.h` has `PAR_TABLE_PTR`, `PAR_ROW_PTR`, `CMD_SOUND_ACTIVE`, `CMD_SOUND_SEG_INDEX`, `CMD_SOUND_STATE`, `CMD_SOUND_SAVE`, `FRM_SNAP`, `FRM_SNAP_PREV`, all different; `cmd_addr.h` has `ROM_CMD_SOUND`. Nothing in `src/` or `tests/` uses `PAR_PHONEMES`, `CMD_SOUND_EFFECTS/MAX/END/TABLE/DATA`, `FRAME_A*`/`FRAME_F*`/`FRAME_V`/`FRAME_PITCH`, `fw_par_` (other than the two counts), `fw_frm_` or `fw_cmd_sound`. `par_data.c` is a new basename.

- [ ] **Step 1: Write the failing test**

In `tests/test_firmware_data.c`:
- Replace the `k_rom` definition:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0x9F74 }, { 0xA187, 0xA397 }, { 0xACC7, 0xACE8 },
                               { 0xC67B, 0xC9B8 }, { 0xD44A, 0xD46A }, { 0xE5E2, 0xE5E3 }, { 0xF75B, 0xF77B } };
```

with:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xB773, 0xB77D },
                               { 0xB7A7, 0xB7B1 }, { 0xC67B, 0xC9B8 }, { 0xD44A, 0xF77B } };
```

- In `test_counts`, replace:

```c
    CHECK_EQ(fw_cmd_speed_map_data[8][31], 1);    /* speed 9: every duration is 1 frame */
}
```

with:

```c
    CHECK_EQ(fw_cmd_speed_map_data[8][31], 1);    /* speed 9: every duration is 1 frame */

    /* The playback data (M4.4a spec 3) */
    unsigned bad_target = 0, bad_prop = 0, b_differs = 0;
    for (int ph = 0; ph < PAR_PHONEMES; ph++) {
        const fw_par_phoneme *a = &fw_par_voice_a[ph], *b = &fw_par_voice_b[ph];
        b_differs += a->rank != b->rank || a->spare != b->spare;
        for (int k = 0; k < 10; k++) {
            const fw_par_rule *ra = &a->p[k], *rb = &b->p[k];
            bad_target += ra->target < 1 || ra->target > 63 || rb->target < 1 || rb->target > 63;
            bad_prop += ra->prop > 8 || rb->prop > 8;
            b_differs += ra->prop != rb->prop || ra->ext_dur != rb->ext_dur || ra->int_dur != rb->int_dur;
            if (k >= 4)
                b_differs += ra->target != rb->target || ra->fixed != rb->fixed;
        }
    }
    CHECK_EQ(bad_target, 0);   /* every target is 1-63, below $80: par_read_segment's BMI at $BD85 is never taken */
    CHECK_EQ(bad_prop, 0);     /* every proportion is 0-8: par_ofs_weight = 8 - proportion never wraps */
    CHECK_EQ(b_differs, 0);    /* voice B differs only in the targets and fixed contributions of P1-P4 */
    CHECK_EQ(fw_par_voice_a[59].rank, 31);   /* the pause */
    CHECK_EQ(fw_par_target_bias[3], 26);     /* P4 F3 */
    CHECK_EQ(fw_frm_snapshot_map[0], FRAME_PITCH);
    unsigned sound_segments = 0, sound_longest = 0, sound_refs = 0;
    for (int x = 0; x < CMD_SOUND_EFFECTS; x++) {
        const fw_cmd_sound_effect *s = &fw_cmd_sound_effects[x];
        const fw_cmd_sound *t = &fw_cmd_sound_table[x];
        sound_segments += s->count;
        if (s->count > sound_longest)
            sound_longest = s->count;
        sound_refs += t->phoneme == s->phoneme && t->duration == s->duration && t->pitch == s->pitch &&
                      t->flags == fw_cmd_sound_flags;
    }
    CHECK_EQ(sound_segments, 120);             /* 18 flags + 120 * 3 + 17 ends = 395 bytes */
    CHECK_EQ(sound_longest, CMD_SOUND_MAX);    /* the shared flag array covers the longest effect */
    CHECK_EQ(sound_refs, CMD_SOUND_EFFECTS);   /* entry x: effect x's arrays and the shared flags */
}
```

- In `test_against_rom`, replace:

```c
    CHECK_EQ(compared, 38197);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567) and
                                    M4.3a's prosody data (2,319) */
```

with:

```c
    CHECK_EQ(compared, 47692);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319) and M4.4a's playback data (9,495) */
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: a compile error, `tests\test_firmware_data.c(76): error C2065: 'PAR_PHONEMES': undeclared identifier` (and the other new names).

- [ ] **Step 3: Declare the addresses, types and arrays in `src/firmware/data/fw_data.h`**

- After the line `typedef struct { uint8_t count; fw_ph_triple item[3]; bool word_pause; } fw_ph_list;`, add (the block starts with an empty line):

```c

/* One parameter of a phoneme in a voice table (M4.4a spec 3), its five rows in the ROM's order: the target; the
   signed fixed contribution and the proportion 0-8 (eighths of the other phoneme's target) of the boundary value;
   the transition durations in frames in the dominated neighbour (ext_dur) and in this phoneme (int_dur), both
   when this phoneme dominates. */
typedef struct { uint8_t target; int8_t fixed; uint8_t prop, ext_dur, int_dur; } fw_par_rule;
/* One phoneme of a voice table: its rank, its spare byte and P1..P10 (p[0..9]). */
typedef struct { uint8_t rank, spare; fw_par_rule p[10]; } fw_par_phoneme;

#define CMD_SOUND_EFFECTS   17     /* @T0-@TF, and @TG or @TX */
#define CMD_SOUND_MAX       18     /* the most phonemes in an effect, and the length of the shared flag array */
#define CMD_SOUND_END       0xFF   /* ends the phoneme array of an effect */
/* The segment arrays of a @T sound effect (M4.4a spec 3): count phonemes (rows of the voice tables), then per
   phoneme its duration in frames and its pitch target. The encoder writes the end CMD_SOUND_END after the
   phonemes. */
typedef struct {
    uint8_t count;
    uint8_t phoneme[CMD_SOUND_MAX], duration[CMD_SOUND_MAX], pitch[CMD_SOUND_MAX];
} fw_cmd_sound_effect;
/* A cmd_sound_table entry: the arrays cmd_ev_sound_start points pros_phoneme_ptr, pros_duration_ptr,
   pros_pitch_ptr and pros_flags_ptr at, as references into fw_cmd_sound_effects and fw_cmd_sound_flags. */
typedef struct { const uint8_t *phoneme, *duration, *pitch, *flags; } fw_cmd_sound;
```

- Replace:

```c
#define PAR_VOICE_A         0xD46A   /* the M4.4 voice A table, not converted; the C writes only its address */
#define PAR_VOICE_B         0xE5E3   /* the M4.4 voice B table, not converted; the C writes only its address */
```

with:

```c
#define PAR_VOICE_A         0xD46A   /* par_table of voice A: rank row, spare row, five rows per parameter, N = 86 */
#define PAR_VOICE_B         0xE5E3   /* par_table of voice B */
#define PAR_PHONEMES        86       /* N, the phoneme count of both voices */
```

- After the line `#define PH_WORD_PAUSE       0x5F     /* the word-pause item of a phoneme list (the space's) */`, add (the block starts with an empty line):

```c

/* Pinned addresses of the playback data (ghidra740/annotations: command.txt, frame.txt, params.txt; M4.4a spec 3) */
#define CMD_SOUND_TABLE     0x9F74   /* ptr[17][4]: per @T effect its phoneme, duration, pitch-target and flag arrays */
#define CMD_SOUND_DATA      0x9FFC   /* the @T segment arrays (395 bytes) */
#define FRM_SNAPSHOT_MAP    0xB773   /* byte[10]: frame byte offset per snapshot entry */
#define PAR_TARGET_BIAS     0xB7A7   /* byte[10]: per parameter P1..P10 */
```

- After the line `#define PHF_VOICED      0x01`, add (the block starts with an empty line):

```c

/* The bytes of a DSP frame (frame20: ghidra740/annotations/types.txt; parent spec 1.4): the values of
   frm_snapshot_map */
#define FRAME_ALF       0    /* b0 low-frequency amplitude */
#define FRAME_FN        1    /* b1 nasal (low) resonance frequency */
#define FRAME_A1        2
#define FRAME_F1        3
#define FRAME_A2        4
#define FRAME_F2        5
#define FRAME_A3        6
#define FRAME_F3        7
#define FRAME_AHF       8    /* b8 high-frequency amplitude */
#define FRAME_V         9    /* b9 degree of voicing */
#define FRAME_PITCH     10   /* b10 pitch code */
```

- After the line `extern const uint8_t fw_ph_lengthen_steps[31];`, add:

```c
extern const fw_par_phoneme fw_par_voice_a[PAR_PHONEMES], fw_par_voice_b[PAR_PHONEMES];
extern const uint8_t fw_par_target_bias[10], fw_frm_snapshot_map[10];
extern const uint8_t fw_cmd_sound_flags[CMD_SOUND_MAX];
extern const fw_cmd_sound_effect fw_cmd_sound_effects[CMD_SOUND_EFFECTS];
extern const fw_cmd_sound fw_cmd_sound_table[CMD_SOUND_EFFECTS];
```

- [ ] **Step 4: The data: `src/firmware/data/par_data.c` (new) and the @T tables in `src/firmware/data/cmd_data.c`**

Create `src/firmware/data/par_data.c` with exactly this content (743 lines; the C was generated from the ROM while drafting; `firmware_data` checks every byte):

```c
/* Converted from the Apollo 2 ROM (M4.4a spec section 3), then reviewed.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"

/* The voice tables (par_table, ghidra740/annotations/types.txt). The ROM stores each as 52 rows of N = 86
   bytes indexed by phoneme: the rank row, the spare row, then per parameter P1..P10 its five rows; the
   encoder writes them back in that order. Per phoneme here:
   - rank: of two neighbouring phonemes the higher rank dominates the boundary (a tie goes to the
     following one); both 29 or more: no transition (par_boundary_pass, par_boundary_jump);
   - spare: the second row, which par_setup_voice points par_row2_ptr at and nothing reads;
   - per parameter: target (par_read_segment adds the @u offset and clamps it to 1-63); fixed, the signed
     fixed contribution to the boundary value when this phoneme dominates; prop, the proportion 0-8 (in
     eighths) of the other phoneme's target taken into the boundary value; ext_dur, the transition
     duration in frames in the dominated neighbour, and int_dur, the one in this phoneme, both when this
     phoneme dominates (par_boundary_cur_dom, par_boundary_next_dom).
   The comment of each phoneme names the symbols whose phoneme lists (fw_ph_lists) give it, as the
   symbol and its place in the list, and the @T effects that use it: the annotations name no phoneme.
   Voice B differs from voice A only in the targets and fixed contributions of P1-P4. */

/* par_voice_a ($D46A, voice A: male, cmd_speaker 0): per phoneme 0-85 { rank, spare, {
       P1 FN, P2 F1, P3 F2, P4 F3, P5 ALF,
       P6 A1, P7 A2, P8 A3, P9 AHF, P10 V } },
   each parameter {target, fixed, prop, ext_dur, int_dur}. */
const fw_par_phoneme fw_par_voice_a[PAR_PHONEMES] = {
    [ 0] = {  2, 32, {   /* from 'a' */
        {32,   7, 4,  0,  0}, {30,  13, 4,  3,  3}, {23,   6, 4,  3,  3}, {30,   3, 4,  3,  3}, {46,  23, 4,  3,  3},
        {45,  23, 4,  3,  3}, {50,  25, 4,  3,  3}, {48,  24, 4,  3,  3}, {46,  23, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 1] = {  2, 32, {   /* from 'q' (2/2), 'x' */
        {32,   7, 4,  0,  0}, {17,   6, 4,  2,  2}, {19,   5, 4,  2,  2}, {32,   3, 4,  3,  3}, {47,  23, 4,  2,  2},
        {48,  24, 4,  2,  2}, {46,  23, 4,  2,  2}, {42,  21, 4,  2,  2}, {39,  20, 4,  2,  2}, {63,  31, 4,  1,  1},
    } },
    [ 2] = {  2, 32, {   /* from 'h' */
        {32,   7, 4,  0,  0}, {25,  11, 4,  3,  3}, { 9,   0, 4,  3,  3}, {34,   4, 4,  3,  3}, {50,  25, 4,  3,  3},
        {50,  25, 4,  3,  3}, {53,  26, 4,  3,  3}, {42,  21, 4,  3,  3}, {42,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 3] = {  2, 32, {   /* from 'o' */
        {32,   7, 4,  0,  0}, {16,   6, 4,  3,  3}, { 5,  -3, 4,  3,  3}, {29,   1, 4,  3,  3}, {48,  24, 4,  3,  3},
        {50,  25, 4,  3,  3}, {47,  24, 4,  3,  3}, {38,  19, 4,  3,  3}, {44,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 4] = {  2, 32, {   /* from 'A' (1/2) */
        {32,   7, 4,  0,  0}, {19,   7, 4,  2,  5}, {28,   9, 4,  5,  5}, {28,   1, 4,  5,  5}, {46,  23, 4,  3,  3},
        {47,  23, 4,  3,  3}, {49,  24, 4,  3,  3}, {49,  24, 4,  3,  3}, {45,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 5] = {  2, 32, {   /* from 'A' (2/2), 'I' (2/2), 'y' (2/2) */
        {32,   7, 4,  0,  0}, {14,   5, 4,  3,  3}, {32,  11, 4,  3,  3}, {30,   2, 4,  3,  3}, {48,  24, 4,  3,  3},
        {48,  24, 4,  3,  3}, {47,  23, 4,  3,  3}, {48,  24, 4,  3,  3}, {43,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 6] = {  2, 32, {   /* from $84; only @X phonetic text reaches $84 */
        {32,   7, 4,  0,  0}, {28,  12, 4,  3,  3}, {19,   5, 4,  3,  3}, {31,   3, 4,  3,  3}, {51,  26, 4,  3,  3},
        {43,  22, 4,  3,  3}, {48,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {51,  25, 4,  3,  3}, {63,  24, 1,  0,  0},
    } },
    [ 7] = {  2, 32, {   /* from $83; only @X phonetic text reaches $83 */
        {32,   7, 4,  0,  0}, {28,  12, 4,  3,  3}, {19,   5, 4,  3,  3}, {31,   3, 4,  3,  3}, {51,  26, 4,  3,  3},
        {38,  19, 4,  3,  3}, {41,  20, 4,  3,  3}, {37,  18, 4,  3,  3}, {46,  23, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 8] = { 28, 32, {   /* from 'B' (1/2) */
        {32,   7, 4,  0,  0}, { 4,   0, 4,  3,  0}, {12,  -3, 6,  3,  0}, {24,  -1, 4,  3,  0}, {30,  28, 0,  3,  0},
        {32,  30, 0,  3,  0}, { 1, -10, 8,  2,  0}, { 1, -10, 8,  2,  0}, { 1, -10, 8,  2,  0}, {62,  31, 4,  1,  0},
    } },
    [ 9] = { 29, 32, {   /* from 'B' (2/2) */
        {32,   7, 4,  0,  0}, { 4,   0, 4,  2,  0}, {12,   1, 4,  2, 10}, {18,   8, 2,  3,  0}, {35,   0, 8,  3,  0},
        {30, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, {32, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, {32,  16, 4,  1,  0},
    } },
    [10] = { 30, 32, {   /* from 'c' (1/3) */
        {32,   7, 4,  0,  0}, { 4,   1, 3,  3, 10}, {26,   6, 4,  3, 10}, {28,  -9, 6,  3, 10}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [11] = { 30, 32, {   /* from 'c' (2/3) */
        {32,   7, 4,  0,  0}, { 4,   1, 4,  2,  1}, {26,   6, 4,  2,  1}, {28,   1, 4,  2,  1}, { 1, -10, 8,  3,  1},
        { 1,   0, 4,  3,  1}, {44,  22, 4,  3,  1}, {56,  28, 4,  3,  1}, {57,  28, 4,  3,  1}, { 1,   1, 4,  2,  0},
    } },
    [12] = {  6, 32, {   /* from 'c' (3/3) */
        {32,   7, 4,  0,  0}, {12,   4, 4,  5,  2}, {26,   8, 4,  5,  2}, {24,  -3, 4,  6,  2}, { 1,   0, 0,  3,  2},
        { 1,   0, 0,  3,  2}, {48,  24, 4,  2,  2}, {51,  26, 4,  2,  2}, {58,  29, 4,  2,  2}, { 1,   1, 4,  0,  0},
    } },
    [13] = { 27,  8, {   /* from 'D' (1/2), 'j' (1/2) */
        {32,   7, 4,  0,  0}, { 4,   0, 4,  3,  1}, {26,   6, 4,  3,  1}, {28,  -9, 6,  3,  2}, {39,  37, 0,  3,  0},
        {40,  40, 0,  3,  1}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {62,  31, 4,  1,  0},
    } },
    [14] = {  6, 32, {   /* from 'd' */
        {32,   7, 4,  0,  0}, { 7,   2, 4,  5,  2}, {16,   3, 4,  5,  2}, {28,  28, 0,  5,  2}, {40,  20, 4,  2,  2},
        {33,  16, 4,  2,  2}, {32,  16, 4,  2,  2}, {34,  17, 4,  2,  2}, {41,  20, 4,  2,  2}, {36,  18, 4,  2,  2},
    } },
    [15] = { 29,  3, {   /* from 'D' (2/2) */
        {32,   7, 4,  0,  0}, { 4,  -1, 5,  3,  4}, {26,   8, 4,  3,  0}, {28, -13, 6,  3,  0}, {20,   0, 8,  2,  2},
        {20,   0, 8,  3,  0}, {42,  -5, 8,  3,  0}, {40,  -5, 8,  3,  0}, {51,  -5, 8,  3,  0}, {32,  16, 4,  1,  0},
    } },
    [16] = {  2, 32, {   /* from 'E', 'b' */
        {32,   7, 4,  0,  0}, { 8,   3, 4,  3,  3}, {40,  15, 4,  3,  3}, {40,   7, 4,  3,  3}, {48,  24, 4,  3,  3},
        {41,  18, 4,  3,  3}, {40,  20, 4,  3,  3}, {46,  23, 4,  3,  3}, {50,  25, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [17] = {  2, 32, {   /* from 'e', 'l' (1/2) */
        {32,   7, 4,  0,  0}, {19,   8, 4,  3,  3}, {31,  11, 4,  3,  3}, {30,   2, 4,  3,  3}, {46,  23, 4,  3,  3},
        {46,  23, 4,  3,  3}, {50,  25, 4,  3,  3}, {48,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [18] = {  2, 32, {   /* from 'r' */
        {32,   7, 4,  0,  0}, {18,   7, 4,  3,  3}, {18,   4, 4,  3,  3}, {30,   2, 4,  3,  3}, {47,  23, 4,  3,  3},
        {49,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {45,  22, 4,  3,  3}, {45,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [19] = {  2, 32, {   /* from 'l' (2/2) */
        {32,   7, 4,  0,  0}, {24,  10, 4,  3,  3}, {22,   6, 4,  3,  3}, {31,   3, 4,  3,  3}, {48,  24, 4,  3,  3},
        {37,  17, 4,  3,  3}, {39,  19, 4,  3,  3}, {36,  18, 4,  3,  3}, {40,  20, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [20] = {  6, 32, {   /* from 'F' */
        {32,   7, 4,  0,  0}, {12,   0, 6,  2,  2}, {17,  -2, 6,  2,  2}, {25, -13, 6,  2,  2}, { 1,   0, 0,  2,  1},
        { 1,   0, 0,  2,  1}, {28, -20, 8,  2,  1}, {34, -20, 8,  2,  1}, {31, -25, 8,  4,  1}, { 1,   1, 4,  0,  0},
    } },
    [21] = { 28, 12, {   /* from 'G' (1/2), 'g' (2/2), $87 (1/2), $98 (1/2) */
        {32,   7, 4,  0,  0}, { 1,   1, 3,  3,  1}, {40,  -6, 7,  3,  1}, {24,  -9, 5,  3,  1}, {20,  16, 1,  3,  0},
        {20,   5, 6,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {62,  31, 4,  1,  0},
    } },
    [22] = { 29, 32, {   /* from $87 (2/2); only @X phonetic text reaches $87 */
        {32,   7, 4,  3,  0}, {12,   4, 4,  3,  0}, {30,   0, 6,  3,  0}, {32, -11, 6,  3,  0}, {40,   5, 0,  0,  0},
        {35,   0, 0,  0,  0}, {45,   0, 0,  0,  0}, {40,   0, 0,  0,  0}, {42,   0, 0,  0,  0}, {32,  16, 4,  0,  0},
    } },
    [23] = { 29, 32, {   /* from $98 (2/2); from letter-to-sound rules (MEGATON) */
        {32,   7, 4,  5,  5}, {16,   6, 4,  5,  5}, {22,   6, 4,  5,  5}, {32,   3, 4,  5,  5}, { 1,   0, 0,  0,  0},
        {30,   0, 0,  0,  0}, {49,   0, 0,  0,  0}, {23,   8, 0,  0,  0}, {35,  10, 0,  0,  0}, {32,  16, 4,  0,  0},
    } },
    [24] = { 30, 32, {   /* from 'G' (2/2) */
        {32,   7, 4,  0,  0}, { 4,   1, 3,  0,  4}, {34,  -4, 7,  1,  4}, {18,   5, 2,  3,  4}, { 1,  -2, 8,  2,  0},
        { 1,  -2, 8,  2,  0}, {36,  -5, 8,  2,  2}, {40,  -5, 8,  2,  2}, {34, -10, 8,  2,  2}, {32,  16, 4,  1,  0},
    } },
    [25] = {  4, 32, {   /* no phoneme list or @T effect uses it */
        {32, -18, 8,  0,  0}, {26,  -4, 8,  0, 40}, {24, -10, 8,  0, 40}, {30, -26, 8,  0, 40}, {30,   8, 6,  1,  4},
        {36,  14, 5,  2,  3}, {47,  30, 3,  3,  2}, {45,  34, 2,  4,  1}, {45,  45, 0,  5,  0}, { 1,   0, 8,  5,  4},
    } },
    [26] = {  2, 32, {   /* from 'i' */
        {32,   7, 4,  0,  0}, {10,   3, 4,  3,  3}, {35,  13, 4,  3,  3}, {32,   3, 4,  3,  3}, {46,  23, 4,  3,  3},
        {49,  24, 4,  3,  3}, {49,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {46,  23, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [27] = {  2, 32, {   /* from 'J' (1/2) */
        {32,   7, 4,  0,  0}, {12,   4, 4,  6,  6}, {38,  14, 4,  6,  6}, {31,   3, 4,  6,  6}, {46,  23, 4,  6,  6},
        {48,  24, 4,  6,  6}, {55,  22, 4,  6,  6}, {54,  22, 4,  6,  6}, {45,  21, 4,  6,  6}, {63,  31, 4,  0,  0},
    } },
    [28] = {  2, 32, {   /* from 'J' (2/2) */
        {32,   7, 4,  0,  0}, {17,   7, 4,  3,  3}, {23,   7, 4,  3,  3}, {29,   2, 4,  3,  3}, {48,  24, 4,  3,  3},
        {40,  20, 4,  3,  3}, {36,  18, 4,  3,  3}, {34,  17, 4,  3,  3}, {39,  19, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [29] = {  2, 32, {   /* from '5' (1/2), 'I' (1/2) */
        {32,   7, 4,  0,  0}, {30,  13, 4,  6,  6}, {10,   0, 4,  6,  6}, {31,   3, 4,  6,  6}, {46,  23, 4,  6,  6},
        {47,  23, 4,  6,  6}, {50,  25, 4,  6,  6}, {49,  24, 4,  6,  6}, {44,  22, 4,  6,  6}, {63,  31, 4,  1,  1},
    } },
    [30] = {  2, 32, {   /* from '5' (2/2) */
        {32,   7, 4,  0,  0}, {22,   9, 4,  3,  3}, {22,   6, 4,  3,  3}, {29,   2, 4,  3,  3}, {51,  26, 4,  3,  3},
        {41,  21, 4,  3,  3}, {35,  18, 4,  3,  3}, {37,  19, 4,  3,  3}, {42,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [31] = { 28, 32, {   /* from '6' (1/3), 'C' (1/3), 'K' (1/3), 'k' (1/2) */
        {22,   7, 4,  0,  0}, { 4,   1, 3,  3,  2}, {24,  -5, 7,  5,  2}, {24,  -9, 5,  3,  2}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  1,  0},
    } },
    [32] = { 30, 32, {   /* from 'K' (2/3), 'k' (2/2) */
        {32,   7, 4,  0,  0}, { 4,   1, 3,  3, 10}, {23,  -4, 7,  5,  0}, {20,  -9, 5,  3,  0}, { 1, -10, 8,  3,  0},
        {22, -10, 8,  3,  0}, {42, -10, 8,  3,  0}, {38, -10, 8,  3,  0}, {27, -10, 8,  3,  0}, { 1,   1, 4,  0,  0},
    } },
    [33] = { 30, 32, {   /* from 'K' (3/3) */
        {32,   7, 4,  0,  0}, {16,  -4, 8,  3, 10}, {23,  -6, 7,  3,  0}, {20,  -9, 5,  3,  0}, { 1,   0, 0,  3,  0},
        {18,   0, 0,  3,  0}, {30, -10, 8,  3,  0}, {37, -10, 8,  3,  0}, {25, -10, 8,  3,  0}, { 1,   1, 4,  0,  0},
    } },
    [34] = { 30, 32, {   /* from '6' (2/3) */
        {32,   7, 4,  5,  5}, {16,   6, 4,  5,  5}, {24,   7, 4,  5,  5}, {18,  -4, 4,  5,  5}, { 1,  -5, 8,  3,  0},
        {30, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, {28, -10, 8,  3,  0}, {40, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [35] = { 29, 32, {   /* from '6' (3/3) */
        {32,   7, 4,  5,  5}, {16,  -1, 7,  5,  5}, {24,  -6, 7,  5,  5}, {18, -21, 7,  5,  5}, { 1,   0, 0,  3,  0},
        {30,   0, 0,  3,  0}, {43, -10, 8,  3,  0}, {28, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [36] = { 30, 32, {   /* from 'C' (2/3) */
        {32,   7, 4,  5,  5}, {12,   4, 4,  5,  5}, {38,  38, 0, 20,  0}, {32,  32, 0, 20,  0}, { 1,  -5, 8,  3,  0},
        {16, -10, 8,  3,  0}, {43, -10, 8,  3,  0}, {46, -10, 8,  3,  0}, {39, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [37] = { 29, 32, {   /* from 'C' (3/3) */
        {32,   7, 4,  5,  5}, {12,   5, 4,  5,  5}, {38,  14, 4,  5, 10}, {26,   0, 4,  5, 10}, { 1,   0, 0,  3,  0},
        {16,   0, 0,  3,  0}, {38, -10, 8,  3,  0}, {45, -10, 8,  3,  0}, {34, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [38] = {  8, 32, {   /* from 'L' */
        {32,   7, 4,  0,  0}, {10,   3, 4,  5,  0}, {18,   4, 4,  5,  0}, {30,   2, 4,  5,  0}, {48,  24, 4,  2,  2},
        {46, -14, 8,  2,  0}, {47, -10, 8,  2,  0}, {45,  -7, 8,  2,  0}, {45,  -7, 8,  2,  0}, {63,  31, 4,  0,  0},
    } },
    [39] = {  8, 32, {   /* from '1' */
        {32,   7, 4,  0,  0}, {10,   3, 4,  3,  1}, { 8,  -1, 4,  3,  1}, {30,   2, 4,  3,  1}, {45,  22, 4,  1,  2},
        {40,  20, 4,  1,  2}, {36,  18, 4,  1,  1}, {29,  -7, 8,  1,  0}, {29,  -7, 8,  1,  0}, {63,  31, 4,  0,  0},
    } },
    [40] = {  8, 32, {   /* from $8C; only @X phonetic text reaches $8C */
        {32,   7, 4,  0,  0}, {10,   3, 4,  6,  0}, {18,   4, 4,  6,  0}, {30,   2, 4,  6,  0}, {48,  24, 4,  2,  2},
        {46, -14, 8,  2,  0}, {47, -10, 8,  2,  0}, {45,  -7, 8,  2,  0}, {45,  -7, 8,  2,  0}, {36,  18, 4,  0,  0},
    } },
    [41] = {  8, 32, {   /* from $C0; a long syllabic consonant, from letter-to-sound rules (LEVEL, NOVEL) */
        {32,   7, 4,  0,  0}, {10,   3, 4,  4,  2}, { 8,  -1, 4,  4,  2}, {30,   2, 4,  4,  0}, {48,  24, 4,  1,  2},
        {46,  23, 4,  1,  2}, {47,  24, 4,  1,  2}, {45,  -7, 8,  1,  0}, {45,  -7, 8,  1,  0}, {63,  31, 4,  0,  0},
    } },
    [42] = {  9, 32, {   /* from 'M' */
        {32,   7, 4,  0,  0}, {10,   2, 5,  3,  0}, {12,   1, 4,  3,  0}, {20,  -3, 4,  2,  3}, {48,   0, 8,  3,  0},
        {26, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {63,  31, 4,  0,  0},
    } },
    [43] = {  9, 32, {   /* from 'm' */
        {32,   7, 4,  0,  0}, {10,   2, 5,  3,  0}, {12,  -3, 6,  3,  0}, {22,  -2, 4,  5,  0}, {48,   0, 8,  3,  0},
        {31, -10, 8,  3,  0}, {36, -10, 8,  3,  0}, {39, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {63,  31, 4,  2,  0},
    } },
    [44] = {  9, 32, {   /* from '7', 'N', 'g' (1/2) */
        {32,   7, 4,  0,  0}, { 7,   3, 3,  3,  0}, {22,  15, 2,  3,  0}, {30,   2, 4,  3,  1}, {48,   0, 8,  3,  0},
        {30, -10, 8,  3,  0}, {25, -10, 8,  2,  0}, {30, -10, 8,  2,  0}, {26, -10, 8,  2,  0}, {63,  31, 4,  0,  0},
    } },
    [45] = {  9,  9, {   /* from 'n' */
        {32,   7, 4,  0,  0}, {11,  -4, 8,  2,  2}, {32,  11, 4,  2,  2}, {24,  17, 1,  2,  3}, {40,   1, 8,  2,  1},
        {20, -10, 8,  2,  2}, {30, -10, 8,  2,  3}, {35, -10, 8,  2,  0}, { 1, -10, 8,  2,  0}, {62,  31, 4,  0,  0},
    } },
    [46] = {  2, 32, {   /* from 'O' (2/2), 'q' (1/2), 'v' */
        {32,   7, 4,  0,  0}, {10,   3, 4,  3,  3}, { 8,  -1, 4,  3,  3}, {22,  -2, 4,  3,  3}, {45,  22, 4,  3,  3},
        {46,  23, 4,  3,  3}, {44,  22, 4,  3,  3}, {32,  16, 4,  3,  3}, {44,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [47] = {  2, 32, {   /* from 'Q' */
        {32,   7, 4,  0,  0}, {20,   8, 4,  3,  3}, { 8,  -1, 4,  3,  3}, {36,   5, 4,  3,  3}, {46,  24, 4,  3,  3},
        {48,  24, 4,  3,  3}, {48,  25, 4,  3,  3}, {37,  20, 4,  3,  3}, {36,  19, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [48] = {  2, 32, {   /* from 'O' (1/2) */
        {32,   7, 4,  0,  0}, {20,   9, 4,  6,  6}, {17,   4, 4,  6,  6}, {30,   2, 4,  6,  6}, {46,  23, 4,  6,  6},
        {48,  24, 4,  6,  6}, {48,  24, 4,  6,  6}, {47,  23, 4,  6,  6}, {44,  22, 4,  6,  6}, {63,  31, 4,  0,  0},
    } },
    [49] = {  2, 32, {   /* from '4' (2/2), 'w' (2/2) */
        {32,   7, 4,  0,  0}, {11,   4, 4,  3,  3}, {11,   1, 4,  3,  3}, {30,   2, 4,  3,  3}, {46,  22, 4,  3,  3},
        {45,  23, 4,  3,  3}, {32,  16, 4,  3,  3}, {24,  12, 4,  3,  3}, {40,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [50] = {  2, 32, {   /* from '0' (1/2), 'w' (1/2) */
        {32,   7, 4,  0,  0}, {32,  14, 4,  6,  6}, {17,   4, 4,  6,  6}, {29,   2, 4,  6,  6}, {45,  23, 4,  6,  6},
        {47,  23, 4,  6,  6}, {49,  24, 4,  6,  6}, {47,  24, 4,  6,  6}, {44,  24, 4,  6,  6}, {63,  31, 4,  0,  0},
    } },
    [51] = {  2, 32, {   /* from '9' (1/2), 'y' (1/2) */
        {32,   7, 4,  3,  3}, {16,   6, 4,  3,  3}, { 6,  -2, 4,  3,  3}, {37,   6, 4,  3,  3}, {47,  23, 4,  3,  3},
        {47,  23, 4,  3,  3}, {49,  25, 4,  3,  3}, {40,  20, 4,  3,  3}, {39,  19, 4,  3,  3}, {63,  31, 4,  3,  3},
    } },
    [52] = {  2, 32, {   /* from '9' (2/2) */
        {32,   7, 4,  0,  0}, {15,   6, 4,  3,  3}, {18,   4, 4,  3,  3}, {24,  -1, 4,  3,  3}, {51,  27, 4,  3,  3},
        {49,  25, 4,  3,  3}, {36,  18, 4,  3,  3}, {37,  19, 4,  3,  3}, {43,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [53] = {  2, 32, {   /* from '4' (1/2) */
        {32,   7, 4,  0,  0}, {19,   8, 4,  3,  3}, { 8,  -1, 4,  3,  3}, {30,   2, 4,  3,  3}, {49,  29, 4,  3,  3},
        {45,  23, 4,  3,  3}, {32,  16, 4,  3,  3}, {24,  12, 4,  3,  3}, {48,  25, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [54] = {  2, 32, {   /* from '0' (2/2) */
        {32,   7, 4,  0,  0}, {19,   8, 4,  3,  3}, { 8,  -1, 4,  3,  3}, {30,   2, 4,  3,  3}, {54,  29, 4,  3,  3},
        {45,  23, 4,  3,  3}, {32,  16, 4,  3,  3}, {24,  12, 4,  3,  3}, {48,  25, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [55] = { 26, 32, {   /* from 'P' (1/3), 'p' (1/2) */
        {32,   7, 4,  0,  0}, {12,   7, 3,  2,  0}, {12,  -2, 6,  2,  0}, {19,  -1, 4,  5,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [56] = { 30, 32, {   /* from 'P' (2/3), 'p' (2/2) */
        {32,   7, 4,  0,  0}, {12,  -4, 8,  2,  0}, {12,   1, 4,  2,  0}, {24,  -1, 4,  2,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, {39, -10, 8,  3,  0}, {38, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [57] = { 29, 32, {   /* from 'P' (3/3) */
        {32,   7, 4,  0,  0}, { 4,  -4, 8,  2, 10}, {12,   1, 4,  2,  2}, {20,  -3, 4,  2,  2}, { 1,   0, 0,  3,  0},
        { 1,   0, 0,  3,  0}, {34, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, { 1,   1, 4,  1,  0},
    } },
    [58] = { 31, 32, {   /* no phoneme list or @T effect uses it */
        {32, -18, 8,  0, 63}, {19,  -4, 8,  0, 63}, {18, -10, 8,  0, 63}, {23, -26, 8,  0, 63}, { 1,  -5, 8,  4,  6},
        { 1, -10, 8,  4,  6}, { 1, -10, 8,  4,  6}, { 1, -10, 8,  4,  6}, { 1, -10, 8,  4,  6}, { 1,   0, 8,  4,  6},
    } },
    [59] = { 31, 10, {   /* the pause (pause segments, 'X', the rest frame's FN-F3); every @T effect */
        {32, -18, 8,  0, 10}, {32,  -4, 8,  0, 10}, {23, -10, 8,  0, 10}, {31, -26, 8,  0, 10}, { 1,  -5, 8,  1,  0},
        { 1, -10, 8,  1,  0}, { 1, -10, 8,  1,  0}, { 1, -10, 8,  1,  0}, { 1, -10, 8,  1,  0}, { 1,   0, 8,  0, 10},
    } },
    [60] = {  7, 32, {   /* from 'R' */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  5,  3}, {13,   1, 4,  5,  3}, { 6, -10, 4,  5,  6}, {48,  24, 4,  5,  4},
        {51,  25, 4,  5,  4}, {48,  25, 4,  5,  4}, {46,  23, 4,  5,  4}, { 1,   0, 4,  5,  4}, {63,  31, 4,  0,  0},
    } },
    [61] = {  7, 32, {   /* from '3' */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  4,  3}, {13,   1, 4,  4,  3}, { 6, -10, 4,  4,  5}, {48,  24, 4,  4,  3},
        {51,  25, 4,  4,  3}, {53,  26, 4,  4,  3}, {49,  24, 4,  4,  3}, { 1,   0, 4,  4,  3}, {36,  18, 4,  0,  0},
    } },
    [62] = {  6, 32, {   /* from 'S' */
        {32,   7, 4,  0,  0}, {12,   4, 4,  2,  1}, {24,   7, 4,  2,  1}, {26,   0, 4,  2,  1}, { 1,   0, 0,  3,  0},
        { 1,   0, 0,  3,  0}, {27,  13, 4,  2,  0}, {33,  16, 4,  2,  0}, {56,  28, 4,  2,  0}, { 1,   1, 2,  1,  0},
    } },
    [63] = {  6, 32, {   /* from 's' */
        {32,   7, 4,  0,  0}, {12,   4, 4,  3,  2}, {26,   8, 4,  3,  2}, {20,  -3, 4,  4,  2}, { 1,   0, 0,  3,  2},
        { 1,   0, 0,  3,  2}, {48,  24, 4,  2,  2}, {55,  27, 4,  2,  2}, {57,  28, 4,  2,  2}, { 1,   1, 4,  0,  0},
    } },
    [64] = { 26, 62, {   /* from 'T' (1/3), 'f' (1/2) */
        {32,   7, 4,  0,  0}, { 4,   1, 3,  3,  0}, {26,   6, 4,  3,  0}, {28,  -9, 6,  3,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [65] = { 30, 62, {   /* from 'T' (2/3), 'f' (2/2) */
        {32,   7, 4,  0,  0}, { 4,   1, 3,  2,  0}, {26,   6, 4,  2,  0}, {28,  -9, 6,  2,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, {31,   0, 8,  1,  0}, {45,   0, 8,  1,  0}, {56,   0, 8,  1,  0}, { 1,   1, 4,  2,  0},
    } },
    [66] = { 29,  2, {   /* from 'T' (3/3) */
        {32,   7, 4,  0,  0}, {16,  -4, 8,  3,  1}, {26,   8, 4,  3,  3}, {28,   1, 4,  3,  3}, { 1,   0, 0,  3,  0},
        { 1,   0, 0,  3,  0}, {35, -10, 8,  3,  0}, {40, -10, 8,  3,  0}, {45, -15, 8,  3,  5}, { 1,   1, 4,  0,  0},
    } },
    [67] = {  6, 32, {   /* from 't' */
        {32,   7, 4,  0,  0}, {12,   4, 4,  5,  2}, {16,  13, 3,  5,  2}, {28,   0, 4,  5,  2}, { 1,   0, 0,  0,  2},
        { 1,   0, 0,  0,  2}, {10, -20, 8,  2,  2}, {18,  -5, 8,  2,  2}, {32,   0, 8,  6,  2}, { 1,   1, 4,  0,  0},
    } },
    [68] = {  2, 32, {   /* from 'u' */
        {32,   7, 4,  0,  0}, {27,  12, 4,  3,  3}, {15,   1, 4,  3,  3}, {36,   5, 4,  3,  3}, {43,  21, 4,  3,  3},
        {43,  21, 4,  3,  3}, {43,  22, 4,  3,  3}, {39,  19, 4,  3,  3}, {29,  14, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [69] = {  2, 32, {   /* from 'U' */
        {32,   7, 4,  0,  0}, { 9,   2, 4,  3,  3}, { 7,  -2, 4,  3,  3}, {23,  -1, 4,  3,  3}, {44,  22, 4,  3,  3},
        {44,  22, 4,  3,  3}, {27,  13, 4,  3,  3}, {25,  12, 4,  3,  3}, {42,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [70] = {  2, 32, {   /* from '2' */
        {32,   7, 4,  0,  0}, {21,   9, 4,  3,  3}, {11,   1, 4,  3,  3}, {36,   5, 4,  3,  3}, {48,  24, 4,  3,  3},
        {43,  22, 4,  3,  3}, {49,  25, 4,  3,  3}, {47,  23, 4,  3,  3}, {37,  18, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [71] = {  6, 32, {   /* from 'V' */
        {32,   7, 4,  0,  0}, { 7,   2, 4,  5,  2}, {17,   4, 4,  5,  2}, {25,   0, 4,  5,  2}, {40,  20, 4,  2,  2},
        {33,  16, 4,  2,  2}, {38,  19, 4,  2,  2}, {38,  19, 4,  2,  2}, {43,  21, 4,  2,  2}, {36,  18, 4,  2,  2},
    } },
    [72] = {  7, 32, {   /* from 'W' */
        {32,   7, 4,  0,  0}, { 7,   2, 4,  3,  2}, { 5,  -2, 4,  3,  2}, {24,  -1, 4,  4,  2}, {48,  24, 4,  2,  3},
        {44,  22, 4,  2,  3}, {43,  21, 4,  2,  3}, {24,  12, 4,  2,  3}, {28,  14, 4,  2,  3}, {46,  23, 4,  1,  0},
    } },
    [73] = {  7, 32, {   /* from 'Y' */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  4,  4}, {36,  13, 4,  4,  6}, {34,   4, 4,  4,  4}, {48,  24, 4,  4,  4},
        {42,  21, 4,  4,  4}, {43,  21, 4,  4,  4}, {45,  22, 4,  4,  4}, {40,  20, 4,  4,  4}, {45,  22, 4,  0,  3},
    } },
    [74] = {  6, 32, {   /* from 'Z' */
        {32,   7, 4,  0,  0}, { 7,   2, 4,  2,  2}, {24,   7, 4,  2,  2}, {26,   0, 4,  2,  2}, {43,  21, 4,  2,  1},
        {33,  16, 4,  2,  1}, {37,  18, 4,  2,  1}, {37,  18, 4,  2,  1}, {56,  28, 4,  2,  1}, {36,  18, 4,  0,  1},
    } },
    [75] = {  6, 32, {   /* from 'j' (2/2), 'z' */
        {32,   7, 4,  0,  0}, { 7,   2, 4,  5,  2}, {26,   8, 4,  5,  2}, {20,  -3, 4,  6,  2}, {40,  20, 4,  2,  2},
        {33,  16, 4,  2,  2}, {48,  24, 4,  2,  2}, {55,  27, 4,  2,  2}, {57,  28, 4,  2,  2}, {36,  18, 4,  2,  2},
    } },
    [76] = {  8,  6, {   /* from 'H' */
        {32, -18, 8,  0,  0}, {26,  -4, 8,  0, 40}, {24, -10, 8,  0, 40}, {30, -26, 8,  0, 40}, {30,  11, 5,  1,  4},
        {30,  11, 5,  2,  3}, {32,  21, 3,  3,  2}, {30,  24, 2,  4,  1}, {28,  28, 0,  5,  0}, { 1,   1, 4,  4,  3},
    } },
    [77] = { 32, 12, {   /* only in @T0-@T3 */
        {32,  32, 0,  0,  0}, {12,  12, 0,  5,  2}, {63, -10, 0,  5, 13}, {23,  23, 0,  6,  2}, { 1, -10, 0,  2,  2},
        { 1, -10, 0,  2,  2}, {60,  50, 1,  2,  2}, { 1, -10, 0,  2,  2}, {30,  50, 1,  2,  2}, { 1,   0, 0,  2,  2},
    } },
    [78] = {  2, 14, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, {11,   3, 4,  3,  3}, {34,  11, 4,  3,  3}, {33,  -1, 4,  3,  3}, {44,  24, 4,  3,  3},
        {43,  24, 4,  3,  3}, {40,  20, 4,  3,  3}, {39,  23, 4,  3,  3}, {47,  22, 4,  3,  3}, {62,  31, 4,  0,  0},
    } },
    [79] = {  2,  8, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, {28,  11, 4,  3,  3}, {15,   3, 4,  3,  3}, {24,  -1, 4,  3,  3}, {44,  24, 4,  3,  3},
        {46,  25, 4,  3,  3}, {43,  24, 4,  3,  3}, {39,  21, 4,  3,  3}, {40,  22, 4,  3,  3}, {62,  31, 4,  0,  0},
    } },
    [80] = { 19, 25, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  0, 15}, {20,  13, 4,  0, 15}, {60,   4, 4,  0, 15}, { 1,  24, 0,  3, 15},
        { 1,  27, 0,  3, 15}, {53,  32, 0,  7, 15}, {42,  28, 4,  7, 15}, { 1,  21, 4,  7, 16}, {62,  31, 4,  0,  0},
    } },
    [81] = { 19, 25, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  0, 15}, {25,  13, 4,  0, 15}, {51,   4, 4,  0, 15}, { 1,  24, 2,  7, 15},
        {23,  27, 4,  7, 15}, {45,  22, 4,  7, 15}, {45,  22, 4,  7, 15}, { 1,  21, 4,  7, 16}, { 1,  31, 4,  0,  0},
    } },
    [82] = { 21, 31, {   /* only in @TC-@TF */
        {32,   7, 4,  0,  0}, { 1,   1, 4,  1,  1}, {20,   5, 4,  2,  2}, { 1,   4, 4,  1,  1}, { 1,  24, 0,  3,  0},
        { 1,  26, 0,  3,  0}, {45,  32, 0,  3,  0}, { 1,  28, 4,  3,  0}, { 1,  21, 4,  3,  0}, {63,  31, 4,  1,  0},
    } },
    [83] = { 32, 25, {   /* only in @T8-@TF */
        {32,   7, 4,  1, 40}, {36,  16, 4,  1, 40}, {40,   0, 0,  1,  1}, {62,  18, 4,  1, 40}, { 1,   0, 8,  1,  1},
        { 1,   0, 8,  1,  1}, {53,   0, 8,  1,  1}, { 1,   0, 8,  1,  1}, { 1,   0, 8,  1,  1}, {63,   0, 8,  0,  0},
    } },
    [84] = { 32, 25, {   /* only in @T4-@T7 */
        {32,   7, 4,  1, 20}, {36,  16, 4,  1, 10}, {30,  10, 4,  1, 20}, {62,  18, 4,  1, 20}, { 1,   0, 8,  2,  3},
        {44,   0, 8,  3,  3}, { 1,   0, 8,  2,  3}, { 1,   0, 8,  2,  3}, { 1,   0, 8,  2,  3}, {61,  31, 4,  1,  1},
    } },
    [85] = { 32, 25, {   /* no phoneme list or @T effect uses it */
        {32,  32, 0,  1, 20}, {36,  36, 0,  1, 20}, {30,  30, 0,  1, 20}, {34,  24, 0, 10, 15}, { 1, -10, 0,  1,  3},
        { 1, -10, 0,  1,  3}, { 1, -10, 0,  1,  3}, {49, -10, 0,  2,  4}, { 1, -10, 0,  1,  3}, {63,   0, 0,  1,  2},
    } },
};

/* par_voice_b ($E5E3, voice B: @K1, non-male): per phoneme 0-85 { rank, spare, {
       P1 FN, P2 F1, P3 F2, P4 F3, P5 ALF,
       P6 A1, P7 A2, P8 A3, P9 AHF, P10 V } },
   each parameter {target, fixed, prop, ext_dur, int_dur}. */
const fw_par_phoneme fw_par_voice_b[PAR_PHONEMES] = {
    [ 0] = {  2, 32, {   /* from 'a' */
        {37,   9, 4,  0,  0}, {33,  14, 4,  3,  3}, {27,   8, 4,  3,  3}, {32,   4, 4,  3,  3}, {46,  23, 4,  3,  3},
        {45,  23, 4,  3,  3}, {50,  25, 4,  3,  3}, {48,  24, 4,  3,  3}, {46,  23, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 1] = {  2, 32, {   /* from 'q' (2/2), 'x' */
        {37,   9, 4,  0,  0}, {19,   6, 4,  2,  2}, {23,   7, 4,  2,  2}, {34,   4, 4,  3,  3}, {47,  23, 4,  2,  2},
        {48,  24, 4,  2,  2}, {46,  23, 4,  2,  2}, {42,  21, 4,  2,  2}, {39,  20, 4,  2,  2}, {63,  31, 4,  1,  1},
    } },
    [ 2] = {  2, 32, {   /* from 'h' */
        {37,   9, 4,  0,  0}, {27,  12, 4,  3,  3}, {11,   0, 4,  3,  3}, {35,   4, 4,  3,  3}, {50,  25, 4,  3,  3},
        {50,  25, 4,  3,  3}, {53,  26, 4,  3,  3}, {42,  21, 4,  3,  3}, {42,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 3] = {  2, 32, {   /* from 'o' */
        {37,   9, 4,  0,  0}, {18,   6, 4,  3,  3}, { 7,  -3, 4,  3,  3}, {32,   2, 4,  3,  3}, {48,  24, 4,  3,  3},
        {50,  25, 4,  3,  3}, {47,  24, 4,  3,  3}, {38,  19, 4,  3,  3}, {44,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 4] = {  2, 32, {   /* from 'A' (1/2) */
        {37,   9, 4,  0,  0}, {21,   8, 4,  2,  5}, {33,  11, 4,  5,  5}, {31,   2, 4,  5,  5}, {46,  23, 4,  3,  3},
        {47,  23, 4,  3,  3}, {49,  24, 4,  3,  3}, {49,  24, 4,  3,  3}, {45,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 5] = {  2, 32, {   /* from 'A' (2/2), 'I' (2/2), 'y' (2/2) */
        {37,   9, 4,  0,  0}, {15,   5, 4,  3,  3}, {38,  14, 4,  3,  3}, {32,   3, 4,  3,  3}, {48,  24, 4,  3,  3},
        {48,  24, 4,  3,  3}, {47,  23, 4,  3,  3}, {48,  24, 4,  3,  3}, {43,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 6] = {  2, 32, {   /* from $84; only @X phonetic text reaches $84 */
        {37,   9, 4,  0,  0}, {31,  13, 4,  3,  3}, {23,   7, 4,  3,  3}, {33,   4, 4,  3,  3}, {51,  26, 4,  3,  3},
        {43,  22, 4,  3,  3}, {48,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {51,  25, 4,  3,  3}, {63,  24, 1,  0,  0},
    } },
    [ 7] = {  2, 32, {   /* from $83; only @X phonetic text reaches $83 */
        {37,   9, 4,  0,  0}, {31,  13, 4,  3,  3}, {23,   7, 4,  3,  3}, {33,   4, 4,  3,  3}, {51,  26, 4,  3,  3},
        {38,  19, 4,  3,  3}, {41,  20, 4,  3,  3}, {37,  18, 4,  3,  3}, {46,  23, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [ 8] = { 28, 32, {   /* from 'B' (1/2) */
        {37,   9, 4,  0,  0}, { 4,   0, 4,  3,  0}, {15,  -3, 6,  3,  0}, {28,  -1, 4,  3,  0}, {30,  28, 0,  3,  0},
        {32,  30, 0,  3,  0}, { 1, -10, 8,  2,  0}, { 1, -10, 8,  2,  0}, { 1, -10, 8,  2,  0}, {62,  31, 4,  1,  0},
    } },
    [ 9] = { 29, 32, {   /* from 'B' (2/2) */
        {37,   9, 4,  0,  0}, { 4,   0, 4,  2,  0}, {15,   2, 4,  2, 10}, {25,  12, 2,  3,  0}, {35,   0, 8,  3,  0},
        {30, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, {32, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, {32,  16, 4,  1,  0},
    } },
    [10] = { 30, 32, {   /* from 'c' (1/3) */
        {37,   9, 4,  0,  0}, { 4,   1, 3,  3, 10}, {31,   8, 4,  3, 10}, {31,  -9, 6,  3, 10}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [11] = { 30, 32, {   /* from 'c' (2/3) */
        {37,   9, 4,  0,  0}, { 4,   1, 4,  2,  1}, {31,   8, 4,  2,  1}, {31,   2, 4,  2,  1}, { 1, -10, 8,  3,  1},
        { 1,   0, 4,  3,  1}, {44,  22, 4,  3,  1}, {56,  28, 4,  3,  1}, {57,  28, 4,  3,  1}, { 1,   1, 4,  2,  0},
    } },
    [12] = {  6, 32, {   /* from 'c' (3/3) */
        {37,   9, 4,  0,  0}, {13,   4, 4,  5,  2}, {31,  10, 4,  5,  2}, {28,  -3, 4,  6,  2}, { 1,   0, 0,  3,  2},
        { 1,   0, 0,  3,  2}, {48,  24, 4,  2,  2}, {51,  26, 4,  2,  2}, {58,  29, 4,  2,  2}, { 1,   1, 4,  0,  0},
    } },
    [13] = { 27,  8, {   /* from 'D' (1/2), 'j' (1/2) */
        {37,   9, 4,  0,  0}, { 4,   0, 4,  3,  1}, {31,   8, 4,  3,  1}, {31,  -9, 6,  3,  2}, {39,  37, 0,  3,  0},
        {40,  40, 0,  3,  1}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {62,  31, 4,  1,  0},
    } },
    [14] = {  6, 32, {   /* from 'd' */
        {37,   9, 4,  0,  0}, { 8,   2, 4,  5,  2}, {19,   4, 4,  5,  2}, {31,  31, 0,  5,  2}, {40,  20, 4,  2,  2},
        {33,  16, 4,  2,  2}, {32,  16, 4,  2,  2}, {34,  17, 4,  2,  2}, {41,  20, 4,  2,  2}, {36,  18, 4,  2,  2},
    } },
    [15] = { 29,  3, {   /* from 'D' (2/2) */
        {37,   9, 4,  0,  0}, { 4,  -1, 5,  3,  4}, {31,  10, 4,  3,  0}, {31, -13, 6,  3,  0}, {20,   0, 8,  2,  2},
        {20,   0, 8,  3,  0}, {42,  -5, 8,  3,  0}, {40,  -5, 8,  3,  0}, {51,  -5, 8,  3,  0}, {32,  16, 4,  1,  0},
    } },
    [16] = {  2, 32, {   /* from 'E', 'b' */
        {37,   9, 4,  0,  0}, { 9,   3, 4,  3,  3}, {47,  18, 4,  3,  3}, {39,   6, 4,  3,  3}, {48,  24, 4,  3,  3},
        {41,  18, 4,  3,  3}, {40,  20, 4,  3,  3}, {46,  23, 4,  3,  3}, {50,  25, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [17] = {  2, 32, {   /* from 'e', 'l' (1/2) */
        {37,   9, 4,  0,  0}, {21,   9, 4,  3,  3}, {37,  14, 4,  3,  3}, {32,   3, 4,  3,  3}, {46,  23, 4,  3,  3},
        {46,  23, 4,  3,  3}, {50,  25, 4,  3,  3}, {48,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [18] = {  2, 32, {   /* from 'r' */
        {37,   9, 4,  0,  0}, {20,   8, 4,  3,  3}, {22,   6, 4,  3,  3}, {32,   3, 4,  3,  3}, {47,  23, 4,  3,  3},
        {49,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {45,  22, 4,  3,  3}, {45,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [19] = {  2, 32, {   /* from 'l' (2/2) */
        {37,   9, 4,  0,  0}, {26,  11, 4,  3,  3}, {26,   8, 4,  3,  3}, {33,   4, 4,  3,  3}, {48,  24, 4,  3,  3},
        {37,  17, 4,  3,  3}, {39,  19, 4,  3,  3}, {36,  18, 4,  3,  3}, {40,  20, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [20] = {  6, 32, {   /* from 'F' */
        {37,   9, 4,  0,  0}, {13,   0, 6,  2,  2}, {21,  -2, 6,  2,  2}, {29, -13, 6,  2,  2}, { 1,   0, 0,  2,  1},
        { 1,   0, 0,  2,  1}, {28, -20, 8,  2,  1}, {34, -20, 8,  2,  1}, {31, -25, 8,  4,  1}, { 1,   1, 4,  0,  0},
    } },
    [21] = { 28, 12, {   /* from 'G' (1/2), 'g' (2/2), $87 (1/2), $98 (1/2) */
        {37,   9, 4,  0,  0}, { 1,   1, 3,  3,  1}, {47,  -6, 7,  3,  1}, {28,  -9, 5,  3,  1}, {20,  16, 1,  3,  0},
        {20,   5, 6,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {62,  31, 4,  1,  0},
    } },
    [22] = { 29, 32, {   /* from $87 (2/2); only @X phonetic text reaches $87 */
        {37,   9, 4,  3,  0}, {13,   4, 4,  3,  0}, {36,   0, 6,  3,  0}, {34, -11, 6,  3,  0}, {40,   5, 0,  0,  0},
        {35,   0, 0,  0,  0}, {45,   0, 0,  0,  0}, {40,   0, 0,  0,  0}, {42,   0, 0,  0,  0}, {32,  16, 4,  0,  0},
    } },
    [23] = { 29, 32, {   /* from $98 (2/2); from letter-to-sound rules (MEGATON) */
        {37,   9, 4,  5,  5}, {18,   6, 4,  5,  5}, {26,   8, 4,  5,  5}, {34,   4, 4,  5,  5}, { 1,   0, 0,  0,  0},
        {30,   0, 0,  0,  0}, {49,   0, 0,  0,  0}, {23,   8, 0,  0,  0}, {35,  10, 0,  0,  0}, {32,  16, 4,  0,  0},
    } },
    [24] = { 30, 32, {   /* from 'G' (2/2) */
        {37,   9, 4,  0,  0}, { 4,   1, 3,  0,  4}, {40,  -4, 7,  1,  4}, {25,  10, 2,  3,  4}, { 1,  -2, 8,  2,  0},
        { 1,  -2, 8,  2,  0}, {36,  -5, 8,  2,  2}, {40,  -5, 8,  2,  2}, {34, -10, 8,  2,  2}, {32,  16, 4,  1,  0},
    } },
    [25] = {  4, 32, {   /* no phoneme list or @T effect uses it */
        {37, -18, 8,  0,  0}, {29,  -4, 8,  0, 40}, {29, -10, 8,  0, 40}, {32, -26, 8,  0, 40}, {30,   8, 6,  1,  4},
        {36,  14, 5,  2,  3}, {47,  30, 3,  3,  2}, {45,  34, 2,  4,  1}, {45,  45, 0,  5,  0}, { 1,   0, 8,  5,  4},
    } },
    [26] = {  2, 32, {   /* from 'i' */
        {37,   9, 4,  0,  0}, {11,   3, 4,  3,  3}, {41,  16, 4,  3,  3}, {34,   4, 4,  3,  3}, {46,  23, 4,  3,  3},
        {49,  24, 4,  3,  3}, {49,  24, 4,  3,  3}, {48,  24, 4,  3,  3}, {46,  23, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [27] = {  2, 32, {   /* from 'J' (1/2) */
        {37,   9, 4,  0,  0}, {13,   4, 4,  6,  6}, {45,  17, 4,  6,  6}, {33,   4, 4,  6,  6}, {46,  23, 4,  6,  6},
        {48,  24, 4,  6,  6}, {55,  22, 4,  6,  6}, {54,  22, 4,  6,  6}, {45,  21, 4,  6,  6}, {63,  31, 4,  0,  0},
    } },
    [28] = {  2, 32, {   /* from 'J' (2/2) */
        {37,   9, 4,  0,  0}, {19,   8, 4,  3,  3}, {27,   9, 4,  3,  3}, {32,   3, 4,  3,  3}, {48,  24, 4,  3,  3},
        {40,  20, 4,  3,  3}, {36,  18, 4,  3,  3}, {34,  17, 4,  3,  3}, {39,  19, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [29] = {  2, 32, {   /* from '5' (1/2), 'I' (1/2) */
        {37,   9, 4,  0,  0}, {33,  14, 4,  6,  6}, {12,   0, 4,  6,  6}, {33,   4, 4,  6,  6}, {46,  23, 4,  6,  6},
        {47,  23, 4,  6,  6}, {50,  25, 4,  6,  6}, {49,  24, 4,  6,  6}, {44,  22, 4,  6,  6}, {63,  31, 4,  1,  1},
    } },
    [30] = {  2, 32, {   /* from '5' (2/2) */
        {37,   9, 4,  0,  0}, {24,  10, 4,  3,  3}, {26,   8, 4,  3,  3}, {32,   3, 4,  3,  3}, {51,  26, 4,  3,  3},
        {41,  21, 4,  3,  3}, {35,  18, 4,  3,  3}, {37,  19, 4,  3,  3}, {42,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [31] = { 28, 32, {   /* from '6' (1/3), 'C' (1/3), 'K' (1/3), 'k' (1/2) */
        {26,   9, 4,  0,  0}, { 4,   1, 3,  3,  2}, {29,  -5, 7,  5,  2}, {28,  -9, 5,  3,  2}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  1,  0},
    } },
    [32] = { 30, 32, {   /* from 'K' (2/3), 'k' (2/2) */
        {37,   9, 4,  0,  0}, { 4,   1, 3,  3, 10}, {27,  -4, 7,  5,  0}, {26,  -9, 5,  3,  0}, { 1, -10, 8,  3,  0},
        {22, -10, 8,  3,  0}, {42, -10, 8,  3,  0}, {38, -10, 8,  3,  0}, {27, -10, 8,  3,  0}, { 1,   1, 4,  0,  0},
    } },
    [33] = { 30, 32, {   /* from 'K' (3/3) */
        {37,   9, 4,  0,  0}, {18,  -4, 8,  3, 10}, {27,  -6, 7,  3,  0}, {26,  -9, 5,  3,  0}, { 1,   0, 0,  3,  0},
        {18,   0, 0,  3,  0}, {30, -10, 8,  3,  0}, {37, -10, 8,  3,  0}, {25, -10, 8,  3,  0}, { 1,   1, 4,  0,  0},
    } },
    [34] = { 30, 32, {   /* from '6' (2/3) */
        {37,   9, 4,  5,  5}, {18,   6, 4,  5,  5}, {29,   9, 4,  5,  5}, {25,  -4, 4,  5,  5}, { 1,  -5, 8,  3,  0},
        {30, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, {28, -10, 8,  3,  0}, {40, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [35] = { 29, 32, {   /* from '6' (3/3) */
        {37,   9, 4,  5,  5}, {18,  -1, 7,  5,  5}, {29,  -6, 7,  5,  5}, {25, -21, 7,  5,  5}, { 1,   0, 0,  3,  0},
        {30,   0, 0,  3,  0}, {43, -10, 8,  3,  0}, {28, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [36] = { 30, 32, {   /* from 'C' (2/3) */
        {37,   9, 4,  5,  5}, {13,   4, 4,  5,  5}, {45,  45, 0, 20,  0}, {34,  34, 0, 20,  0}, { 1,  -5, 8,  3,  0},
        {16, -10, 8,  3,  0}, {43, -10, 8,  3,  0}, {46, -10, 8,  3,  0}, {39, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [37] = { 29, 32, {   /* from 'C' (3/3) */
        {37,   9, 4,  5,  5}, {13,   5, 4,  5,  5}, {45,  17, 4,  5, 10}, {30,   0, 4,  5, 10}, { 1,   0, 0,  3,  0},
        {16,   0, 0,  3,  0}, {38, -10, 8,  3,  0}, {45, -10, 8,  3,  0}, {34, -10, 8,  3,  0}, { 1,   0, 0,  0,  0},
    } },
    [38] = {  8, 32, {   /* from 'L' */
        {37,   9, 4,  0,  0}, {11,   3, 4,  5,  0}, {22,   6, 4,  5,  0}, {32,   3, 4,  5,  0}, {48,  24, 4,  2,  2},
        {46, -14, 8,  2,  0}, {47, -10, 8,  2,  0}, {45,  -7, 8,  2,  0}, {45,  -7, 8,  2,  0}, {63,  31, 4,  0,  0},
    } },
    [39] = {  8, 32, {   /* from '1' */
        {37,   9, 4,  0,  0}, {11,   3, 4,  3,  1}, {10,  -1, 4,  3,  1}, {32,   3, 4,  3,  1}, {45,  22, 4,  1,  2},
        {40,  20, 4,  1,  2}, {36,  18, 4,  1,  1}, {29,  -7, 8,  1,  0}, {29,  -7, 8,  1,  0}, {63,  31, 4,  0,  0},
    } },
    [40] = {  8, 32, {   /* from $8C; only @X phonetic text reaches $8C */
        {37,   9, 4,  0,  0}, {11,   3, 4,  6,  0}, {22,   6, 4,  6,  0}, {32,   3, 4,  6,  0}, {48,  24, 4,  2,  2},
        {46, -14, 8,  2,  0}, {47, -10, 8,  2,  0}, {45,  -7, 8,  2,  0}, {45,  -7, 8,  2,  0}, {36,  18, 4,  0,  0},
    } },
    [41] = {  8, 32, {   /* from $C0; a long syllabic consonant, from letter-to-sound rules (LEVEL, NOVEL) */
        {37,   9, 4,  0,  0}, {11,   3, 4,  4,  2}, {10,  -1, 4,  4,  2}, {32,   3, 4,  4,  0}, {48,  24, 4,  1,  2},
        {46,  23, 4,  1,  2}, {47,  24, 4,  1,  2}, {45,  -7, 8,  1,  0}, {45,  -7, 8,  1,  0}, {63,  31, 4,  0,  0},
    } },
    [42] = {  9, 32, {   /* from 'M' */
        {37,   9, 4,  0,  0}, {11,   2, 5,  3,  0}, {15,   2, 4,  3,  0}, {26,  -3, 4,  2,  3}, {48,   0, 8,  3,  0},
        {26, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, {30, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {63,  31, 4,  0,  0},
    } },
    [43] = {  9, 32, {   /* from 'm' */
        {37,   9, 4,  0,  0}, {11,   2, 5,  3,  0}, {15,  -3, 6,  3,  0}, {27,  -2, 4,  5,  0}, {48,   0, 8,  3,  0},
        {31, -10, 8,  3,  0}, {36, -10, 8,  3,  0}, {39, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, {63,  31, 4,  2,  0},
    } },
    [44] = {  9, 32, {   /* from '7', 'N', 'g' (1/2) */
        {37,   9, 4,  0,  0}, { 8,   3, 3,  3,  0}, {26,  18, 2,  3,  0}, {32,   3, 4,  3,  1}, {48,   0, 8,  3,  0},
        {30, -10, 8,  3,  0}, {25, -10, 8,  2,  0}, {30, -10, 8,  2,  0}, {26, -10, 8,  2,  0}, {63,  31, 4,  0,  0},
    } },
    [45] = {  9,  9, {   /* from 'n' */
        {37,   9, 4,  0,  0}, {12,  -4, 8,  2,  2}, {38,  14, 4,  2,  2}, {28,  21, 1,  2,  3}, {40,   1, 8,  2,  1},
        {20, -10, 8,  2,  2}, {30, -10, 8,  2,  3}, {35, -10, 8,  2,  0}, { 1, -10, 8,  2,  0}, {62,  31, 4,  0,  0},
    } },
    [46] = {  2, 32, {   /* from 'O' (2/2), 'q' (1/2), 'v' */
        {37,   9, 4,  0,  0}, {11,   3, 4,  3,  3}, {10,  -1, 4,  3,  3}, {27,  -2, 4,  3,  3}, {45,  22, 4,  3,  3},
        {46,  23, 4,  3,  3}, {44,  22, 4,  3,  3}, {32,  16, 4,  3,  3}, {44,  22, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [47] = {  2, 32, {   /* from 'Q' */
        {37,   9, 4,  0,  0}, {22,   9, 4,  3,  3}, {10,  -1, 4,  3,  3}, {36,   5, 4,  3,  3}, {46,  24, 4,  3,  3},
        {48,  24, 4,  3,  3}, {48,  25, 4,  3,  3}, {37,  20, 4,  3,  3}, {36,  19, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [48] = {  2, 32, {   /* from 'O' (1/2) */
        {37,   9, 4,  0,  0}, {22,  10, 4,  6,  6}, {21,   6, 4,  6,  6}, {32,   3, 4,  6,  6}, {46,  23, 4,  6,  6},
        {48,  24, 4,  6,  6}, {48,  24, 4,  6,  6}, {47,  23, 4,  6,  6}, {44,  22, 4,  6,  6}, {63,  31, 4,  0,  0},
    } },
    [49] = {  2, 32, {   /* from '4' (2/2), 'w' (2/2) */
        {37,   9, 4,  0,  0}, {12,   4, 4,  3,  3}, {14,   2, 4,  3,  3}, {32,   3, 4,  3,  3}, {46,  22, 4,  3,  3},
        {45,  23, 4,  3,  3}, {32,  16, 4,  3,  3}, {24,  12, 4,  3,  3}, {40,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [50] = {  2, 32, {   /* from '0' (1/2), 'w' (1/2) */
        {37,   9, 4,  0,  0}, {35,  15, 4,  6,  6}, {21,   6, 4,  6,  6}, {32,   3, 4,  6,  6}, {45,  23, 4,  6,  6},
        {47,  23, 4,  6,  6}, {49,  24, 4,  6,  6}, {47,  24, 4,  6,  6}, {44,  24, 4,  6,  6}, {63,  31, 4,  0,  0},
    } },
    [51] = {  2, 32, {   /* from '9' (1/2), 'y' (1/2) */
        {37,   9, 4,  3,  3}, {18,   6, 4,  3,  3}, { 8,  -2, 4,  3,  3}, {37,   6, 4,  3,  3}, {47,  23, 4,  3,  3},
        {47,  23, 4,  3,  3}, {49,  25, 4,  3,  3}, {40,  20, 4,  3,  3}, {39,  19, 4,  3,  3}, {63,  31, 4,  3,  3},
    } },
    [52] = {  2, 32, {   /* from '9' (2/2) */
        {37,   9, 4,  0,  0}, {16,   6, 4,  3,  3}, {22,   6, 4,  3,  3}, {28,  -1, 4,  3,  3}, {51,  27, 4,  3,  3},
        {49,  25, 4,  3,  3}, {36,  18, 4,  3,  3}, {37,  19, 4,  3,  3}, {43,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [53] = {  2, 32, {   /* from '4' (1/2) */
        {37,   9, 4,  0,  0}, {21,   9, 4,  3,  3}, {10,  -1, 4,  3,  3}, {32,   3, 4,  3,  3}, {49,  29, 4,  3,  3},
        {45,  23, 4,  3,  3}, {32,  16, 4,  3,  3}, {24,  12, 4,  3,  3}, {48,  25, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [54] = {  2, 32, {   /* from '0' (2/2) */
        {37,   9, 4,  0,  0}, {21,   9, 4,  3,  3}, {10,  -1, 4,  3,  3}, {32,   3, 4,  3,  3}, {54,  29, 4,  3,  3},
        {45,  23, 4,  3,  3}, {32,  16, 4,  3,  3}, {24,  12, 4,  3,  3}, {48,  25, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [55] = { 26, 32, {   /* from 'P' (1/3), 'p' (1/2) */
        {37,   9, 4,  0,  0}, {13,   8, 3,  2,  0}, {15,  -2, 6,  2,  0}, {25,  -1, 4,  5,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [56] = { 30, 32, {   /* from 'P' (2/3), 'p' (2/2) */
        {37,   9, 4,  0,  0}, {13,  -4, 8,  2,  0}, {15,   2, 4,  2,  0}, {28,  -1, 4,  2,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, {39, -10, 8,  3,  0}, {38, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [57] = { 29, 32, {   /* from 'P' (3/3) */
        {37,   9, 4,  0,  0}, { 4,  -4, 8,  2, 10}, {15,   2, 4,  2,  2}, {26,  -3, 4,  2,  2}, { 1,   0, 0,  3,  0},
        { 1,   0, 0,  3,  0}, {34, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, {35, -10, 8,  3,  0}, { 1,   1, 4,  1,  0},
    } },
    [58] = { 31, 32, {   /* no phoneme list or @T effect uses it */
        {37, -18, 8,  0, 63}, {21,  -4, 8,  0, 63}, {22, -10, 8,  0, 63}, {28, -26, 8,  0, 63}, { 1,  -5, 8,  4,  6},
        { 1, -10, 8,  4,  6}, { 1, -10, 8,  4,  6}, { 1, -10, 8,  4,  6}, { 1, -10, 8,  4,  6}, { 1,   0, 8,  4,  6},
    } },
    [59] = { 31, 10, {   /* the pause (pause segments, 'X', the rest frame's FN-F3); every @T effect */
        {37, -18, 8,  0, 10}, {35,  -4, 8,  0, 10}, {27, -10, 8,  0, 10}, {33, -26, 8,  0, 10}, { 1,  -5, 8,  1,  0},
        { 1, -10, 8,  1,  0}, { 1, -10, 8,  1,  0}, { 1, -10, 8,  1,  0}, { 1, -10, 8,  1,  0}, { 1,   0, 8,  0, 10},
    } },
    [60] = {  7, 32, {   /* from 'R' */
        {37,   9, 4,  0,  0}, { 6,   1, 4,  5,  3}, {16,   2, 4,  5,  3}, {17, -10, 4,  5,  6}, {48,  24, 4,  5,  4},
        {51,  25, 4,  5,  4}, {48,  25, 4,  5,  4}, {46,  23, 4,  5,  4}, { 1,   0, 4,  5,  4}, {63,  31, 4,  0,  0},
    } },
    [61] = {  7, 32, {   /* from '3' */
        {37,   9, 4,  0,  0}, { 6,   1, 4,  4,  3}, {16,   2, 4,  4,  3}, {17, -10, 4,  4,  5}, {48,  24, 4,  4,  3},
        {51,  25, 4,  4,  3}, {53,  26, 4,  4,  3}, {49,  24, 4,  4,  3}, { 1,   0, 4,  4,  3}, {36,  18, 4,  0,  0},
    } },
    [62] = {  6, 32, {   /* from 'S' */
        {37,   9, 4,  0,  0}, {13,   4, 4,  2,  1}, {29,   9, 4,  2,  1}, {30,   0, 4,  2,  1}, { 1,   0, 0,  3,  0},
        { 1,   0, 0,  3,  0}, {27,  13, 4,  2,  0}, {33,  16, 4,  2,  0}, {56,  28, 4,  2,  0}, { 1,   1, 2,  1,  0},
    } },
    [63] = {  6, 32, {   /* from 's' */
        {37,   9, 4,  0,  0}, {13,   4, 4,  3,  2}, {31,  10, 4,  3,  2}, {26,  -3, 4,  4,  2}, { 1,   0, 0,  3,  2},
        { 1,   0, 0,  3,  2}, {48,  24, 4,  2,  2}, {55,  27, 4,  2,  2}, {57,  28, 4,  2,  2}, { 1,   1, 4,  0,  0},
    } },
    [64] = { 26, 62, {   /* from 'T' (1/3), 'f' (1/2) */
        {37,   9, 4,  0,  0}, { 4,   1, 3,  3,  0}, {31,   8, 4,  3,  0}, {31,  -9, 6,  3,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1, -10, 8,  3,  0}, { 1,   1, 4,  2,  0},
    } },
    [65] = { 30, 62, {   /* from 'T' (2/3), 'f' (2/2) */
        {37,   9, 4,  0,  0}, { 4,   1, 3,  2,  0}, {31,   8, 4,  2,  0}, {31,  -9, 6,  2,  0}, { 1, -10, 8,  3,  0},
        { 1, -10, 8,  3,  0}, {31,   0, 8,  1,  0}, {45,   0, 8,  1,  0}, {56,   0, 8,  1,  0}, { 1,   1, 4,  2,  0},
    } },
    [66] = { 29,  2, {   /* from 'T' (3/3) */
        {37,   9, 4,  0,  0}, {18,  -4, 8,  3,  1}, {31,  10, 4,  3,  3}, {31,   2, 4,  3,  3}, { 1,   0, 0,  3,  0},
        { 1,   0, 0,  3,  0}, {35, -10, 8,  3,  0}, {40, -10, 8,  3,  0}, {45, -15, 8,  3,  5}, { 1,   1, 4,  0,  0},
    } },
    [67] = {  6, 32, {   /* from 't' */
        {37,   9, 4,  0,  0}, {13,   4, 4,  5,  2}, {19,  16, 3,  5,  2}, {31,   0, 4,  5,  2}, { 1,   0, 0,  0,  2},
        { 1,   0, 0,  0,  2}, {10, -20, 8,  2,  2}, {18,  -5, 8,  2,  2}, {32,   0, 8,  6,  2}, { 1,   1, 4,  0,  0},
    } },
    [68] = {  2, 32, {   /* from 'u' */
        {37,   9, 4,  0,  0}, {30,  13, 4,  3,  3}, {18,   2, 4,  3,  3}, {36,   5, 4,  3,  3}, {43,  21, 4,  3,  3},
        {43,  21, 4,  3,  3}, {43,  22, 4,  3,  3}, {39,  19, 4,  3,  3}, {29,  14, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [69] = {  2, 32, {   /* from 'U' */
        {37,   9, 4,  0,  0}, {10,   2, 4,  3,  3}, { 9,  -2, 4,  3,  3}, {28,  -1, 4,  3,  3}, {44,  22, 4,  3,  3},
        {44,  22, 4,  3,  3}, {27,  13, 4,  3,  3}, {25,  12, 4,  3,  3}, {42,  21, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [70] = {  2, 32, {   /* from '2' */
        {37,   9, 4,  0,  0}, {23,  10, 4,  3,  3}, {14,   2, 4,  3,  3}, {36,   5, 4,  3,  3}, {48,  24, 4,  3,  3},
        {43,  22, 4,  3,  3}, {49,  25, 4,  3,  3}, {47,  23, 4,  3,  3}, {37,  18, 4,  3,  3}, {63,  31, 4,  0,  0},
    } },
    [71] = {  6, 32, {   /* from 'V' */
        {37,   9, 4,  0,  0}, { 8,   2, 4,  5,  2}, {21,   6, 4,  5,  2}, {29,   0, 4,  5,  2}, {40,  20, 4,  2,  2},
        {33,  16, 4,  2,  2}, {38,  19, 4,  2,  2}, {38,  19, 4,  2,  2}, {43,  21, 4,  2,  2}, {36,  18, 4,  2,  2},
    } },
    [72] = {  7, 32, {   /* from 'W' */
        {37,   9, 4,  0,  0}, { 8,   2, 4,  3,  2}, { 7,  -2, 4,  3,  2}, {28,  -1, 4,  4,  2}, {48,  24, 4,  2,  3},
        {44,  22, 4,  2,  3}, {43,  21, 4,  2,  3}, {24,  12, 4,  2,  3}, {28,  14, 4,  2,  3}, {46,  23, 4,  1,  0},
    } },
    [73] = {  7, 32, {   /* from 'Y' */
        {37,   9, 4,  0,  0}, { 6,   1, 4,  4,  4}, {42,  16, 4,  4,  6}, {35,   4, 4,  4,  4}, {48,  24, 4,  4,  4},
        {42,  21, 4,  4,  4}, {43,  21, 4,  4,  4}, {45,  22, 4,  4,  4}, {40,  20, 4,  4,  4}, {45,  22, 4,  0,  3},
    } },
    [74] = {  6, 32, {   /* from 'Z' */
        {37,   9, 4,  0,  0}, { 8,   2, 4,  2,  2}, {29,   9, 4,  2,  2}, {30,   0, 4,  2,  2}, {43,  21, 4,  2,  1},
        {33,  16, 4,  2,  1}, {37,  18, 4,  2,  1}, {37,  18, 4,  2,  1}, {56,  28, 4,  2,  1}, {36,  18, 4,  0,  1},
    } },
    [75] = {  6, 32, {   /* from 'j' (2/2), 'z' */
        {37,   9, 4,  0,  0}, { 8,   2, 4,  5,  2}, {31,  10, 4,  5,  2}, {26,  -3, 4,  6,  2}, {40,  20, 4,  2,  2},
        {33,  16, 4,  2,  2}, {48,  24, 4,  2,  2}, {55,  27, 4,  2,  2}, {57,  28, 4,  2,  2}, {36,  18, 4,  2,  2},
    } },
    [76] = {  8,  6, {   /* from 'H' */
        {37, -18, 8,  0,  0}, {29,  -4, 8,  0, 40}, {29, -10, 8,  0, 40}, {32, -26, 8,  0, 40}, {30,  11, 5,  1,  4},
        {30,  11, 5,  2,  3}, {32,  21, 3,  3,  2}, {30,  24, 2,  4,  1}, {28,  28, 0,  5,  0}, { 1,   1, 4,  4,  3},
    } },
    [77] = { 32, 12, {   /* only in @T0-@T3 */
        {32,  32, 0,  0,  0}, {12,  12, 0,  5,  2}, {63, -10, 0,  5, 13}, {23,  23, 0,  6,  2}, { 1, -10, 0,  2,  2},
        { 1, -10, 0,  2,  2}, {60,  50, 1,  2,  2}, { 1, -10, 0,  2,  2}, {30,  50, 1,  2,  2}, { 1,   0, 0,  2,  2},
    } },
    [78] = {  2, 14, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, {11,   3, 4,  3,  3}, {34,  11, 4,  3,  3}, {33,  -1, 4,  3,  3}, {44,  24, 4,  3,  3},
        {43,  24, 4,  3,  3}, {40,  20, 4,  3,  3}, {39,  23, 4,  3,  3}, {47,  22, 4,  3,  3}, {62,  31, 4,  0,  0},
    } },
    [79] = {  2,  8, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, {28,  11, 4,  3,  3}, {15,   3, 4,  3,  3}, {24,  -1, 4,  3,  3}, {44,  24, 4,  3,  3},
        {46,  25, 4,  3,  3}, {43,  24, 4,  3,  3}, {39,  21, 4,  3,  3}, {40,  22, 4,  3,  3}, {62,  31, 4,  0,  0},
    } },
    [80] = { 19, 25, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  0, 15}, {20,  13, 4,  0, 15}, {60,   4, 4,  0, 15}, { 1,  24, 0,  3, 15},
        { 1,  27, 0,  3, 15}, {53,  32, 0,  7, 15}, {42,  28, 4,  7, 15}, { 1,  21, 4,  7, 16}, {62,  31, 4,  0,  0},
    } },
    [81] = { 19, 25, {   /* no phoneme list or @T effect uses it */
        {32,   7, 4,  0,  0}, { 6,   1, 4,  0, 15}, {25,  13, 4,  0, 15}, {51,   4, 4,  0, 15}, { 1,  24, 2,  7, 15},
        {23,  27, 4,  7, 15}, {45,  22, 4,  7, 15}, {45,  22, 4,  7, 15}, { 1,  21, 4,  7, 16}, { 1,  31, 4,  0,  0},
    } },
    [82] = { 21, 31, {   /* only in @TC-@TF */
        {32,   7, 4,  0,  0}, { 1,   1, 4,  1,  1}, {20,   5, 4,  2,  2}, { 1,   4, 4,  1,  1}, { 1,  24, 0,  3,  0},
        { 1,  26, 0,  3,  0}, {45,  32, 0,  3,  0}, { 1,  28, 4,  3,  0}, { 1,  21, 4,  3,  0}, {63,  31, 4,  1,  0},
    } },
    [83] = { 32, 25, {   /* only in @T8-@TF */
        {32,   7, 4,  1, 40}, {36,  16, 4,  1, 40}, {40,   0, 0,  1,  1}, {62,  18, 4,  1, 40}, { 1,   0, 8,  1,  1},
        { 1,   0, 8,  1,  1}, {53,   0, 8,  1,  1}, { 1,   0, 8,  1,  1}, { 1,   0, 8,  1,  1}, {63,   0, 8,  0,  0},
    } },
    [84] = { 32, 25, {   /* only in @T4-@T7 */
        {32,   7, 4,  1, 20}, {36,  16, 4,  1, 10}, {30,  10, 4,  1, 20}, {62,  18, 4,  1, 20}, { 1,   0, 8,  2,  3},
        {44,   0, 8,  3,  3}, { 1,   0, 8,  2,  3}, { 1,   0, 8,  2,  3}, { 1,   0, 8,  2,  3}, {61,  31, 4,  1,  1},
    } },
    [85] = { 32, 25, {   /* no phoneme list or @T effect uses it */
        {32,  32, 0,  1, 20}, {36,  36, 0,  1, 20}, {30,  30, 0,  1, 20}, {34,  24, 0, 10, 15}, { 1, -10, 0,  1,  3},
        { 1, -10, 0,  1,  3}, { 1, -10, 0,  1,  3}, {49, -10, 0,  2,  4}, { 1, -10, 0,  1,  3}, {63,   0, 0,  1,  2},
    } },
};

/* par_target_bias ($B7A7): per parameter P1..P10, added to the dominated phoneme's target before the
   proportion is taken (par_boundary_value); non-zero for the four frequencies only. */
const uint8_t fw_par_target_bias[10] = {
    18,   /* P1 FN */
     4,   /* P2 F1 */
    10,   /* P3 F2 */
    26,   /* P4 F3 */
     0,   /* P5 ALF */
     0,   /* P6 A1 */
     0,   /* P7 A2 */
     0,   /* P8 A3 */
     0,   /* P9 AHF */
     0,   /* P10 V */
};

/* frm_snapshot_map ($B773): per snapshot entry 1-10 (read as $B772,X) the frame byte that frm_snapshot
   copies into frm_snap, and par_reset_tracks from frm_rest_frame. */
const uint8_t fw_frm_snapshot_map[10] = {
    FRAME_PITCH, FRAME_AHF, FRAME_A3, FRAME_A2, FRAME_A1,
    FRAME_ALF, FRAME_FN, FRAME_F1, FRAME_F2, FRAME_F3,
};
```

In `src/firmware/data/cmd_data.c`:
- Replace the first lines:

```c
/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.2 spec section 4), then reviewed.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"
#include "../cmd_addr.h"
```

with:

```c
/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.2 spec section 4), then reviewed. The @T
   tables at the end (M4.4a spec section 3) were converted without the tool, which does not write them.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"
#include "../cmd_addr.h"
#include "../pros_seg.h"   /* PROS_SEG_FLAGS_GEN */
```

- After the last line of the file, `const uint8_t fw_par_voice_a_count = 86, fw_par_voice_b_count = 86;`, append exactly this (it starts with one empty line):

```c

/* cmd_sound_data ($9FFC): the segment arrays of the @T sound effects, in the format of the playback arrays.
   First the flag array that all effects share, one byte per segment of the longest effect (playback never
   reads it). Then per effect: its phonemes (rows of the voice tables; 59 is the pause), after which the
   encoder writes the end CMD_SOUND_END; per phoneme its duration in frames; per phoneme its pitch target
   (128 = the @F centre). The encoder lays them out end to end in this order and checks that they fill
   $9FFC-$A186 exactly. */
const uint8_t fw_cmd_sound_flags[CMD_SOUND_MAX] = {
    PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN,
    PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN,
    PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN,
    PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN, PROS_SEG_FLAGS_GEN,
};
const fw_cmd_sound_effect fw_cmd_sound_effects[CMD_SOUND_EFFECTS] = {
    [ 0] = {  3,   /* @T0: phonemes, durations, pitch targets */
        { 59, 77, 59 },
        { 5, 20, 5 },
        { 208, 208, 208 } },
    [ 1] = {  3,   /* @T1: phonemes, durations, pitch targets */
        { 59, 77, 59 },
        { 5, 40, 5 },
        { 208, 208, 208 } },
    [ 2] = {  3,   /* @T2: phonemes, durations, pitch targets */
        { 59, 77, 59 },
        { 5, 60, 5 },
        { 208, 208, 208 } },
    [ 3] = {  3,   /* @T3: phonemes, durations, pitch targets */
        { 59, 77, 59 },
        { 5, 80, 5 },
        { 208, 208, 208 } },
    [ 4] = {  3,   /* @T4: phonemes, durations, pitch targets */
        { 59, 84, 59 },
        { 5, 3, 5 },
        { 208, 208, 208 } },
    [ 5] = {  3,   /* @T5: phonemes, durations, pitch targets */
        { 59, 84, 59 },
        { 5, 10, 5 },
        { 208, 208, 208 } },
    [ 6] = {  3,   /* @T6: phonemes, durations, pitch targets */
        { 59, 84, 59 },
        { 5, 40, 5 },
        { 208, 208, 208 } },
    [ 7] = {  3,   /* @T7: phonemes, durations, pitch targets */
        { 59, 84, 59 },
        { 5, 80, 5 },
        { 208, 208, 208 } },
    [ 8] = {  5,   /* @T8: phonemes, durations, pitch targets */
        { 59, 83, 83, 83, 59 },
        { 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224 } },
    [ 9] = { 10,   /* @T9: phonemes, durations, pitch targets */
        { 59, 83, 83, 83, 83, 83, 83, 83, 83, 59 },
        { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208, 224, 208, 224, 208 } },
    [10] = { 14,   /* @TA: phonemes, durations, pitch targets */
        { 59, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 59 },
        { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208 } },
    [11] = { 18,   /* @TB: phonemes, durations, pitch targets */
        { 59, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 59 },
        { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208 } },
    [12] = {  6,   /* @TC: phonemes, durations, pitch targets */
        { 59, 83, 82, 83, 82, 59 },
        { 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208 } },
    [13] = { 10,   /* @TD: phonemes, durations, pitch targets */
        { 59, 83, 82, 83, 82, 83, 82, 83, 82, 59 },
        { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208, 224, 208, 224, 208 } },
    [14] = { 14,   /* @TE: phonemes, durations, pitch targets */
        { 59, 83, 82, 83, 82, 83, 82, 83, 82, 83, 82, 83, 82, 59 },
        { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208 } },
    [15] = { 18,   /* @TF: phonemes, durations, pitch targets */
        { 59, 83, 82, 83, 82, 83, 82, 83, 82, 83, 82, 83, 82, 83, 82, 83, 82, 59 },
        { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
        { 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208, 224, 208 } },
    [16] = {  1,   /* @TG/@TX: phonemes, durations, pitch targets */
        { 59 },
        { 10 },
        { 128 } },
};

/* cmd_sound_table ($9F74): per @T effect the four arrays cmd_ev_sound_start points pros_phoneme_ptr,
   pros_duration_ptr, pros_pitch_ptr and pros_flags_ptr at. The encoder writes the ROM address of each
   from where it landed in cmd_sound_data. */
const fw_cmd_sound fw_cmd_sound_table[CMD_SOUND_EFFECTS] = {
    [ 0] = { fw_cmd_sound_effects[0].phoneme, fw_cmd_sound_effects[0].duration,
            fw_cmd_sound_effects[0].pitch, fw_cmd_sound_flags },
    [ 1] = { fw_cmd_sound_effects[1].phoneme, fw_cmd_sound_effects[1].duration,
            fw_cmd_sound_effects[1].pitch, fw_cmd_sound_flags },
    [ 2] = { fw_cmd_sound_effects[2].phoneme, fw_cmd_sound_effects[2].duration,
            fw_cmd_sound_effects[2].pitch, fw_cmd_sound_flags },
    [ 3] = { fw_cmd_sound_effects[3].phoneme, fw_cmd_sound_effects[3].duration,
            fw_cmd_sound_effects[3].pitch, fw_cmd_sound_flags },
    [ 4] = { fw_cmd_sound_effects[4].phoneme, fw_cmd_sound_effects[4].duration,
            fw_cmd_sound_effects[4].pitch, fw_cmd_sound_flags },
    [ 5] = { fw_cmd_sound_effects[5].phoneme, fw_cmd_sound_effects[5].duration,
            fw_cmd_sound_effects[5].pitch, fw_cmd_sound_flags },
    [ 6] = { fw_cmd_sound_effects[6].phoneme, fw_cmd_sound_effects[6].duration,
            fw_cmd_sound_effects[6].pitch, fw_cmd_sound_flags },
    [ 7] = { fw_cmd_sound_effects[7].phoneme, fw_cmd_sound_effects[7].duration,
            fw_cmd_sound_effects[7].pitch, fw_cmd_sound_flags },
    [ 8] = { fw_cmd_sound_effects[8].phoneme, fw_cmd_sound_effects[8].duration,
            fw_cmd_sound_effects[8].pitch, fw_cmd_sound_flags },
    [ 9] = { fw_cmd_sound_effects[9].phoneme, fw_cmd_sound_effects[9].duration,
            fw_cmd_sound_effects[9].pitch, fw_cmd_sound_flags },
    [10] = { fw_cmd_sound_effects[10].phoneme, fw_cmd_sound_effects[10].duration,
            fw_cmd_sound_effects[10].pitch, fw_cmd_sound_flags },
    [11] = { fw_cmd_sound_effects[11].phoneme, fw_cmd_sound_effects[11].duration,
            fw_cmd_sound_effects[11].pitch, fw_cmd_sound_flags },
    [12] = { fw_cmd_sound_effects[12].phoneme, fw_cmd_sound_effects[12].duration,
            fw_cmd_sound_effects[12].pitch, fw_cmd_sound_flags },
    [13] = { fw_cmd_sound_effects[13].phoneme, fw_cmd_sound_effects[13].duration,
            fw_cmd_sound_effects[13].pitch, fw_cmd_sound_flags },
    [14] = { fw_cmd_sound_effects[14].phoneme, fw_cmd_sound_effects[14].duration,
            fw_cmd_sound_effects[14].pitch, fw_cmd_sound_flags },
    [15] = { fw_cmd_sound_effects[15].phoneme, fw_cmd_sound_effects[15].duration,
            fw_cmd_sound_effects[15].pitch, fw_cmd_sound_flags },
    [16] = { fw_cmd_sound_effects[16].phoneme, fw_cmd_sound_effects[16].duration,
            fw_cmd_sound_effects[16].pitch, fw_cmd_sound_flags },
};
```

- [ ] **Step 5: Encoder step 9 in `src/firmware/data/fw_data.c`**

- After the function `check_end` (its last line is `        fail(e, "%s ends at $%04X, the next table starts at $%04X", what, (unsigned)at, (unsigned)next);` followed by `}`), add (the block starts with an empty line):

```c

/* A voice table (par_table) from at, in the ROM's row order: the rank row, the spare row, then per parameter
   P1..P10 its target, fixed, prop, ext_dur and int_dur rows, each PAR_PHONEMES bytes indexed by phoneme. Returns
   the address after the table. */
static uint32_t put_par_table(enc *e, uint32_t at, const fw_par_phoneme *t)
{
    for (uint32_t ph = 0; ph < PAR_PHONEMES; ph++) {
        put(e, false, at + ph, t[ph].rank);
        put(e, false, at + PAR_PHONEMES + ph, t[ph].spare);
        for (uint32_t k = 0; k < 10; k++) {
            const fw_par_rule *r = &t[ph].p[k];
            uint32_t row = at + (2 + 5 * k) * PAR_PHONEMES + ph;
            put(e, false, row, r->target);
            put(e, false, row + PAR_PHONEMES, (uint8_t)r->fixed);
            put(e, false, row + 2 * PAR_PHONEMES, r->prop);
            put(e, false, row + 3 * PAR_PHONEMES, r->ext_dur);
            put(e, false, row + 4 * PAR_PHONEMES, r->int_dur);
        }
    }
    return at + 52 * PAR_PHONEMES;
}

/* One @T array of n bytes at *at; records where it landed (arr/addr, *na entries) for cmd_sound_table. */
static void put_sound_array(enc *e, uint32_t *at, const uint8_t *v, unsigned n, const uint8_t **arr,
                            uint16_t *addr, size_t *na)
{
    arr[*na] = v;
    addr[(*na)++] = (uint16_t)*at;
    for (unsigned i = 0; i < n; i++)
        put(e, false, (*at)++, v[i]);
}
```

- In `fw_image_build`, replace the end of step 8 and the return:

```c
        check_end(&e, "ph_lengthen_steps", PH_LENGTHEN_STEPS + 31, PAR_VOICE_A_COUNT);
    }
    return e.ok;
}
```

with:

```c
        check_end(&e, "ph_lengthen_steps", PH_LENGTHEN_STEPS + 31, PAR_VOICE_A_COUNT);
    }
    /* 9. The playback data (M4.4a spec 3): the two voice tables, each after its count byte (step 7) and filling the
       space up to the next table; par_target_bias and frm_snapshot_map; the @T sound effects end to end in
       cmd_sound_data, exactly filling it, and cmd_sound_table computed from where their arrays landed. */
    {
        if (fw_par_voice_a_count != PAR_PHONEMES || fw_par_voice_b_count != PAR_PHONEMES)
            fail(&e, "the voice counts are %u and %u, the tables have %u phonemes", (unsigned)fw_par_voice_a_count,
                 (unsigned)fw_par_voice_b_count, (unsigned)PAR_PHONEMES);
        check_end(&e, "par_voice_a_count", PAR_VOICE_A_COUNT + 1, PAR_VOICE_A);
        check_end(&e, "par_voice_a", put_par_table(&e, PAR_VOICE_A, fw_par_voice_a), PAR_VOICE_B_COUNT);
        check_end(&e, "par_voice_b_count", PAR_VOICE_B_COUNT + 1, PAR_VOICE_B);
        check_end(&e, "par_voice_b", put_par_table(&e, PAR_VOICE_B, fw_par_voice_b), CMD_VERSION_LANG_TEXT);
        for (int k = 0; k < 10; k++) {
            put(&e, false, PAR_TARGET_BIAS + (uint32_t)k, fw_par_target_bias[k]);
            if (fw_frm_snapshot_map[k] > FRAME_PITCH)
                fail(&e, "frm_snapshot_map entry %d: frame byte %u", k + 1, (unsigned)fw_frm_snapshot_map[k]);
            put(&e, false, FRM_SNAPSHOT_MAP + (uint32_t)k, fw_frm_snapshot_map[k]);
        }
        const uint8_t *arr[1 + 3 * CMD_SOUND_EFFECTS];
        uint16_t addr[1 + 3 * CMD_SOUND_EFFECTS];
        size_t na = 0;
        uint32_t at = CMD_SOUND_DATA;
        put_sound_array(&e, &at, fw_cmd_sound_flags, CMD_SOUND_MAX, arr, addr, &na);
        for (int x = 0; x < CMD_SOUND_EFFECTS; x++) {
            const fw_cmd_sound_effect *s = &fw_cmd_sound_effects[x];
            unsigned n = s->count;
            if (n > CMD_SOUND_MAX) {
                fail(&e, "@T effect %d: %u phonemes, at most %d", x, n, CMD_SOUND_MAX);
                n = CMD_SOUND_MAX;
            }
            for (unsigned i = 0; i < n; i++)
                if (s->phoneme[i] == CMD_SOUND_END)
                    fail(&e, "@T effect %d: phoneme %u is the end $%02X", x, i, CMD_SOUND_END);
            put_sound_array(&e, &at, s->phoneme, n, arr, addr, &na);
            put(&e, false, at++, CMD_SOUND_END);
            put_sound_array(&e, &at, s->duration, n, arr, addr, &na);
            put_sound_array(&e, &at, s->pitch, n, arr, addr, &na);
        }
        check_end(&e, "cmd_sound_data", at, PH_LISTS);
        for (int x = 0; x < CMD_SOUND_EFFECTS; x++) {
            const fw_cmd_sound *t = &fw_cmd_sound_table[x];
            const uint8_t *ref[4] = { t->phoneme, t->duration, t->pitch, t->flags };
            for (int k = 0; k < 4; k++) {
                size_t i = 0;
                while (i < na && arr[i] != ref[k])
                    i++;
                if (i == na)
                    fail(&e, "cmd_sound_table entry %d, pointer %d: not one of the @T arrays", x, k);
                else
                    put_word(&e, CMD_SOUND_TABLE + 8u * (uint32_t)x + 2u * (uint32_t)k, addr[i]);
            }
        }
        check_end(&e, "cmd_sound_table", CMD_SOUND_TABLE + 8 * CMD_SOUND_EFFECTS, CMD_SOUND_DATA);
    }
    return e.ok;
}
```

Notes for the reviewer: `put` fails on a second write of any byte, and `put_par_table` writes each of the 52 × 86 bytes once, so the two `check_end`s of the voice tables are the fill assertion (each table ends exactly at the next byte already converted: voice B's count, `cmd_version_lang_text`). The @T arrays are laid out in ROM order with one `CMD_SOUND_END` after each phoneme array, and `check_end` asserts that they end exactly at `PH_LISTS`; an overrun is also caught as `$A187 written twice`, since step 8 has written the lists. A count above `CMD_SOUND_MAX` is reported and clamped, so the loops never read past an array. `cmd_sound_table` holds no address: each pointer is looked up from where its array landed.

- [ ] **Step 6: Faithfulness check against the ROM and the dumps**

1. The readers of the converted data (from the repo root of the main checkout, where `build/ghidra740/dump` is):

```bash
grep -hE "^  [0-9A-F]{4}  [0-9A-F]+ +[A-Z0-9]+ +[^;]*0x(9f[7-9a-f][0-9a-f]|a0[0-9a-f]{2}|a1[0-8][0-9a-f]|b77[0-9a-f]|b7[a-b][0-9a-f]|d4[6-9a-f][0-9a-f]|d[5-9a-f][0-9a-f]{2}|e[0-9a-f]{3}|f[0-6][0-9a-f]{2}|f7[0-5][0-9a-f])\b" build/ghidra740/dump/rom_*.txt build/ghidra740/dump/ramcode_*.txt | sort
```

Expected, exactly these 13 lines (the table by `cmd_ev_sound_start`, the snapshot map through its 1-based base, the bias by `par_boundary_value`; no other code reads an absolute address in the new ranges, and the voice tables and `cmd_sound_data` are reached only through pointers):

```
  B924  BC72B7    LDY 0xb772,X
  BB4E  BC72B7    LDY 0xb772,X
  BBD3  BD749F    LDA 0x9f74,X
  BBD8  BD759F    LDA 0x9f75,X
  BBDD  BD769F    LDA 0x9f76,X
  BBE2  BD779F    LDA 0x9f77,X
  BBE7  BD789F    LDA 0x9f78,X
  BBEC  BD799F    LDA 0x9f79,X
  BBF1  BD7A9F    LDA 0x9f7a,X
  BBF6  BD7B9F    LDA 0x9f7b,X
  C04D  BDA7B7    LDA 0xb7a7,X
  C0B6  7DA7B7    ADC 0xb7a7,X
  C0C5  BDA7B7    LDA 0xb7a7,X
```

2. Check in `rom_BF6C_par_read_dominance.txt` that the routine calls `par_next_row` three times ($BF7B, $BF8D, $BFBF), so it leaves `par_row_ptr` on row 3; in `rom_BEE2_par_boundary_cur_dom.txt` that row 3 goes to `par_next_start_dur` ($BF06 `STA 0x171d,X`) and row 4 to `par_trans_dur` ($BF11); in `rom_BF27_par_boundary_next_dom.txt` the other way round ($BF4B, $BF56). Row 3 is then always the transition in the dominated phoneme (`ext_dur`) and row 4 the one in the dominant phoneme (`int_dur`), the order of `fw_par_rule`.
3. Check in `rom_BBA7_cmd_ev_sound_start.txt` that the four loads $BBD3–$BBF6 go to `pros_phoneme_ptr`, `pros_duration_ptr`, `pros_pitch_ptr` and `pros_flags_ptr` in that order: the order of `fw_cmd_sound`.

- [ ] **Step 7: Run the tests**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: `47692 converted bytes compared with the ROM, 0 differ` and `suite_firmware_data          ok`, with no compiler warnings. (The `build.bat` syntax check of `src\firmware` without `/Isrc` passes: `par_data.c` includes only `"fw_data.h"`, `cmd_data.c` adds `"../pros_seg.h"`.)

Then run: `cmd //c ".\build.bat test"`
Expected:
- All 31 suites are ok (`31 suite(s), 0 failure(s), 0 skip(s)`), with no compiler warnings. The two `warning: zeros ...` / `warning: flipped ...` lines of `suite_rom_id` are expected, as before.
- `firmware_data` prints `47692 converted bytes compared with the ROM, 0 differ`.
- Every other count is unchanged by this task (no C reads the new tables yet, and the image's other bytes are unchanged): `golden_hybrid` prints `45 corpus files x 29 modes (all, then each routine alone)` (Task 2's count), `diff_lts` shows 0 mismatches and the same 28 `N calls checked` lines as after Task 2, with `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`; `suite_synth` prints the corpus peaks of Task 2.

(Proof run, the consolidated plan executed literally with Tasks 1–2 applied first: `31 suite(s), 0 failure(s), 0 skip(s)`, `47692 converted bytes compared with the ROM, 0 differ`, `45 corpus files x 29 modes (all, then each routine alone)`, the 28 `diff_lts` lines identical to Task 2's (e.g. `cmd_run_due          5199 calls checked`, `pros_phrase_gen      225 calls checked`, `ph_lengthen_count    1139 calls checked`), `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`, and `corpus peak plain 20522 (-4.1 dBFS)` / `corpus peak b11 16038 (-6.2 dBFS)`. While drafting, this task alone on main 5f28310 printed the same with main's corpus counts: `42 corpus files x 29 modes`, `cmd_run_due          4950 calls checked`, `corpus peak b11 15784 (-6.3 dBFS)`.)

- [ ] **Step 8: Commit**

```bash
git add src/firmware/data/par_data.c src/firmware/data/fw_data.h src/firmware/data/fw_data.c src/firmware/data/cmd_data.c tests/test_firmware_data.c
git commit -m "Converted data: the voice tables (one row per phoneme, both voices), par_target_bias, frm_snapshot_map and the @T sound tables (effects end to end, the table from where they land) as readable C (M4.4a 3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The leaves in C: `par_next_row` $BD07, `par_next_param` $BD14, `par_clamp` $BD24, `par_scale_voicing` $BFC3, `pros_scale_target` $BDD3, `pros_microprosody` $B983 and `par_scale_duration` $C29C, their hooks, `par_addr.h`, `pros_scale_target`'s RAM names, and `tests/test_fw_par.c` (spec §2.2, §2.3, §2.6, §2.7, §4.4)

**ROM facts checked** (`rom_BD07_par_next_row.txt`, `rom_BD14_par_next_param.txt`, `rom_BD24_par_clamp.txt`, `rom_BFC3_par_scale_voicing.txt`, `rom_BDD3_pros_scale_target.txt`, `rom_B983_pros_microprosody.txt`, `rom_C29C_par_scale_duration.txt`, every caller: `rom_B3A4_par_setup_voice.txt`, `rom_B41D_frm_build_rest_frame.txt`, `rom_B95D_frm_raise_formants.txt`, `rom_BAF2_par_play_start.txt`, `rom_BD33_par_read_segment.txt`, `rom_BE94_par_boundary_jump.txt`, `rom_BEE2_par_boundary_cur_dom.txt`, `rom_BF27_par_boundary_next_dom.txt`, `rom_BF6C_par_read_dominance.txt`, `rom_C039_par_boundary_value.txt`, `rom_C0D2_par_plan_tracks.txt`, and for the `fw.h` comments `rom_B930_frm_clamp_mark_space.txt`):
- **This task needs nothing from Task 3.** None of the seven reads the voice tables, `par_target_bias`, `frm_snapshot_map` or the @T data; `par_scale_duration` reads `cmd_speed_map` ($C898–$C9B7 through $3A, converted in M4.3a). The drafting run had no stand-in for Task 3 at all, and `firmware_data` printed `38197 converted bytes compared with the ROM, 0 differ` (with Task 3 in place it prints 47692; this task does not change it). No `fw_rom8` fault in any mode.
- **In this task all seven hooks fire in "all" mode**, because every one of their callers is still ROM. After Tasks 5 and 6 only `par_next_param` (from the ROM `frm_build_rest_frame` $B431) and `par_clamp` (from the ROM `frm_raise_formants` $B970/$B97D) still do; the others then fire only in their "alone" modes. `diff_lts` observes on the ROM machine, so it checks all seven at every ROM call site in every task (the counts below).
- **`par_addr.h` names more callers for two `frm_*` routines than spec §2.4 does.** The scan finds `JSR frm_copy_rest_frame` also at $B8DA (`par_play`) and `JSR frm_clamp_mark_space` also at $B8E0 (`par_play`) and $B9BD (`frm_finish_frame`). Their comments name them in parentheses. Every other caller list matches spec §2.3 (the scan in Step 4 covers the 7 routines of this task; the other 15 addresses were scanned the same way while drafting and match their comments).
- **No second entries, no fall-through.** The byte before each of the seven entries is an `RTS` ($B982, $BD06, $BD13, $BD23, $BDD2, $BFC2, $C29B). The branch scan's only hit, `$BD65` for $BD24, is the operand byte $D3 of `JSR $BDD3` at $BD64 (bytes `20 D3 BD`), which matches the `BBC`/`BBS` pattern, not an instruction.
- **`par_clamp`** ($BD24): `CMP #1 / BPL` and `CMP #$40 / BPL` test only the N flag of the 8-bit differences, as spec §2.7 says: 0 and $81–$FF give 1, $40–$80 give $3F (so $80 gives $3F and $81 gives 1), 1–$3F pass. Three `RTS`; no RAM; X and Y untouched.
- **`par_scale_voicing`** ($BFC3), checked branch by branch:
  - 1 and 8 return at once (`BEQ $BFD3`), before the `PHA`, at every @B.
  - @B9 (`cmd_set_voicing` accepts 0–9) falls through every `CMP` to $C037 `PLA / RTS`: v, like @B8. Spec "8 and above → v" is right.
  - `FW_TMP_76` is written at @B2 and @B3 only when v ≠ 0 (the `BEQ $BFD1` after `PLA` comes first), at @B4, @B6 and @B7 always (0 for v = 0). @B4 writes it twice: v/4 ($C006), then v/8 + v/4 ($C00D). The `PHA`/`PLA` at $C009/$C00F is dead stack.
  - @B2's `SEC` makes the + 1; every other add is `CLC`. No sum can carry (the largest is 79, for v = $FF at @B2).
- **`pros_scale_target`** ($BDD3):
  - `$1812`, `$1813` and `$1814` are written on every path ($BDE6–$BDF6, before the R test); `$1811` on every path but the limited R = 0 one ($BE52).
  - d = $80 for a target of 0 (`EOR #$FF / ADC #1` of $80). `$1813` = (d & 3) << 6: the first `ROR` brings d bit 0 into bit 7, the second moves it to bit 6 and brings d bit 1 into bit 7.
  - **The Z quirk at $BE06 is confirmed:** on the `BCC` path (R < `cmd_speed_limit`) the `BEQ` sees the Z of the `CMP`, which is clear. R = 0 there goes $BE08 (`BPL` not taken: Y = 0), $BE0E (`AND #3` = 0, `BEQ $BE58`), `TYA`, $BE31 `STA $1811` = 0. With R = 0 at or above the limit (`AND #7` = 0), $BE52 loads the centre and $1811 keeps its value. The limited R is `cmd_speed_limit & 7` (so R = 7 with limit 5 gives 5).
  - The carry chains only inside one add step: `CLC` at $BE19 starts each step, and the `ADC $1814` at $BE23 takes the carry of `ADC $1812`. `ASL $1812 / ADC #0` adds bit 7 of the final `$1812` to the last `$1811` value, which is still in A.
  - The final test is `CMP #4 / BPL`: bit 2 is ORed into 0–3 and $84–$FF; $80–$83 pass. The result is stored in `pros_play_target`; A leaves as the entry Y (the last `PLA`), X as the entry X.
- **`pros_microprosody`** ($B983): the phoneme tests come in the order $15, $1F, $0D, $18, $20, $41 (disjoint, so the C `switch` order does not matter). A is the phoneme on the `RTS` at $B99E, else the sum or difference, stored at $B9B1 only without a carry (+ 5, `BCC`) or a borrow (− 5, `BCS`). No other RAM, X and Y untouched.
- **`par_scale_duration`** ($C29C): A = 0 returns before `STY $78`. Every other path stores Y in $78 and reloads it at $C2B1 (the speed < 5 `BCC` lands there too), so Y comes back unchanged. The index is min(A, 31): `CMP #$1F / BCC` keeps 0–30, 31 and up load 31. X untouched.
- **`par_next_row` / `par_next_param`**: `par_next_row` `INC`s the high byte only on a carry and returns the low byte; `par_next_param` has one `CLC` and carries from the low add into the high `ADC`, and returns the high byte. Both wrap in 16 bits. Neither touches X or Y.
- **`fw.h`.** $1811–$1814 appear in no dump but `rom_BDD3_pros_scale_target.txt` (a search of all dumps), and `fw_ram.h` names none of them. `fw.h`'s list is not in address order (`FW_TMP_78` comes before `FW_TMP_75`), so the four names go after `FW_TMP_1809`, the nearest address below. `$75` is `frm_clamp_mark_space`'s 2 × b10 ($B936) and `frm_raise_formants`' (b10 − 32) / 4 ($B969); `$76` and `$78` as the routines above.
- **What the callers read after each call** (the declared outputs; every caller site checked in its dump, and no caller reads a flag: an `STA`, `LDA`, `LDY`, `CPX`, `CPY`, `INX`, `DEC`, `PLA`, `JSR` or `JMP` follows each call, or an `RTS` into one):
  - `pros_microprosody`: Y ($BD6A `CPY #0`, $BD70 `DEY`) and X ($BD76 `STA $17BF,X`); A is loaded first ($BD71).
  - `par_next_row`: A at $B3C1/$B3CA (`par_setup_voice`); Y at $BF0C/$BF51 and $BF90; X at $BF83/$BF9D; through $BFBF (`par_read_dominance`'s last call, then `RTS`) Y at $BF01/$BF46 and X at $BF06/$BF56.
  - `par_next_param`: X at $B434 (`INX`), $BDA0 (`CPX`), and $BEB5 (`STA $16ED,X`, the next `par_store_boundary`); Y at $B42C, $BD83, $BEA7; A is loaded before it is read on each path.
  - `par_clamp`: A and **Y** at $B973/$B980 (`STA ($2C),Y`); A and X at $BD92/$BD99 (or through `par_scale_voicing`), Y at the next $BD83; A at $BF78 and X at $BF83; A and X through `JMP $C0AE` into `par_store_boundary`.
  - `pros_scale_target`: from $BAFE, A and Y are loaded next ($BB01, $BB0A `TAY`), X goes on to `par_play_start`'s exit; from $BD64, through `pros_microprosody` (which loads A and keeps X and Y), Y at $BD6A/$BD70 and X at $BD76.
  - `par_scale_voicing`: A and X at $BD99/$BD9C, Y at the next $BD83; A at $BF8A, Y at $BF90, X at $BF9D.
  - `par_scale_duration`: A and X at $BF06/$BF56 (`STA $171D,X`), A at $BF11/$BF4B, Y at $BF0C/$BF51 (`LDA ($32),Y`).
- **The harness.** `count()` and `check_call()` (copied with the harness of `tests/test_fw_pros.c`) are kept though no test of this task calls them (none of the leaves makes a ROM call; Task 6 uses them). The `/W4 /WX` build is clean with them unused. `snapshot()` / `check_written()` are new: they pin that a routine writes nothing but the listed bytes.
- **Proof run** (the consolidated plan executed literally in a scratch worktree from main 5f28310, Tasks 1–3 applied as written first; the ROM oracle and the mutation run below were made while drafting, on Tasks 1–2 without Task 3, which these leaves do not need):
  - `fw_par`: `suite_fw_par                 ok`. The RED step fails with `tests\test_fw_par.c(10): fatal error C1083: Cannot open include file: 'firmware/par_addr.h': No such file or directory`.
  - `diff_lts`: 35 lines, every N > 0, 0 mismatches; the seven new ones are `pros_microprosody 3391`, `par_next_row 40696`, `par_next_param 30676`, `par_clamp 39664`, `pros_scale_target 3564`, `par_scale_voicing 5689`, `par_scale_duration 25782` (calls checked: every call up to 1000 per routine and file, then every 64th).
  - `golden_hybrid`: `45 corpus files x 36 modes (all, then each routine alone)`, all identical.
  - Full run: `32 suite(s), 0 failure(s), 0 skip(s)`.
  - **A ROM oracle for the unit tests** (scratch only, not part of the plan): each of the 109 routine calls in `suite_fw_par` was also run through its ROM original in the emulator on the same RAM and registers (interrupts held). The RAM $0000–$3FFF (but the SFRs and the stack page) and the exit A, X and Y were identical to the C in all 109, including `par_clamp` at every edge, @B9, the R = 0 paths, the 16-bit wraps and the microprosody wraps.
  - Mutation run: `fw_par` fails for each of: the `BEQ` at $BE06 testing R itself (1 failure), an unsigned `< 4` for the `ORA #4` (1), no carry from the `$1812` add into the `$1811` add (2), no `ADC #0` rounding (4), the microprosody storing on a wrap (8), R ≥ 3 instead of the signed `CMP #4` (6), no `STY $78` (18), a 0 test at @B4 (2), `CLC` instead of `SEC` at @B2 (2), a signed `par_clamp` (1), no `INC` of `par_row_ptr+1` (2). (`A < 32 ? A : 31` for the cap is the same function as the ROM's, so it passes.)

**Files:**
- Create: `src/firmware/par_addr.h`, `src/firmware/par_segment.h`, `src/firmware/par_segment.c`, `src/firmware/pros_play.h`, `src/firmware/pros_play.c`, `src/firmware/par_boundary.h`, `src/firmware/par_boundary.c`, `tests/test_fw_par.c`
- Modify: `src/firmware/fw.h` (the four names $1811–$1814; the comments of `FW_TMP_78`, `FW_TMP_75`, `FW_TMP_76`), `src/engine/thunks.c` (seven thunks, `THUNK` lines and rows), `tests/suites.h` (`SUITE(suite_fw_par)`)

**Where the code goes.** Each routine goes in the file that Tasks 5 and 6 extend: `par_next_row`, `par_next_param`, `par_clamp` and `par_scale_voicing` in `par_segment.c` (Task 6 adds `par_read_segment`), `pros_scale_target` and `pros_microprosody` in `pros_play.c`, `par_scale_duration` in `par_boundary.c` (Task 5 adds the boundary pass). The static helpers, one per ROM block:

| File | Helper | ROM |
|---|---|---|
| `par_segment.c` | `voicing_b2` | $BFE5–$BFEE, @B2: v/4 into `FW_TMP_76`, then v/16 + it + 1 |
| | `voicing_b3` | $BFF6–$BFFE, @B3: v/4 into `FW_TMP_76`, then v/8 + it |
| | `voicing_b4` | $C004–$C014, @B4: v/4, then v/8 + v/4 into `FW_TMP_76`, then v/16 + it |
| | `voicing_b6` | $C021–$C029, @B6: v/2 into `FW_TMP_76`, then v/8 + it |
| | `voicing_b7` | $C02F–$C036, @B7: v/2 into `FW_TMP_76`, then v/4 + it |
| `pros_play.c` | `split_deviation` | $BDD7–$BDF8: d, and its split into $1814 and $1813 |
| | `limit_r` | $BDF9–$BE06: R, and the Z flag the `BEQ` at $BE06 tests |
| | `scale_deviation` | $BE08–$BE30 and $BE58: s |
| | `around_centre` | $BE31–$BE39, $BE4D, $BE5C–$BE67: centre ± s, saturated |
| | `store_target` | $BE3F–$BE45: the `ORA #4`, and the store |
| | `micro_raise` | $B99F–$B9A7 with $B9B1: + 5 |
| | `micro_lower` | $B9A8–$B9B0 with $B9B1: − 5 |

`par_scale_voicing`'s @B0, @B1, @B5 and @B8+ blocks are one line each, in the `switch` with their ROM ranges. `par_next_row` (7 instructions), `par_next_param` (8), `par_clamp` (9) and `par_scale_duration` (13) are one block each and have no helper. `pros_scale_target`'s `TYA/PHA/TXA/PHA` … `PLA/TAX/PLA/TAY` is the register contract (X and Y come back unchanged, A = the entry Y).

**Interfaces:**
- Consumes:
  - Task 1: `src/firmware/fw_ops.h` (`fw_adc`, `fw_inc8`), `MACHINE_MAX_HOOKS` 256 and `DIFF_MAX` 48 (35 replacements need 35 observers).
  - Task 2: the corpus (44 names). Nothing here depends on it, but the counts below assume it; its texts drive every rare branch of these seven routines through `diff_lts` and `golden_hybrid`.
  - Task 3: nothing. None of the seven reads a voice-table byte; the only ROM table read is `cmd_speed_map` (through $3A), converted since M4.3a.
  - M4.0–M4.3b: `fw_regs`, `fw_rd8`/`fw_wr8`/`fw_rd16`/`fw_wr16`, `fw_ind_y`, `FW_RAM_SIZE`, `FW_TMP_76`, `FW_TMP_78`, `FW_ROM_CALL_BUDGET` (`fw.h`); the `fw_ram.h` names `PAR_ROW_PTR`, `PAR_ROW_LEN`, `PAR_PARAM_STRIDE`, `CMD_VOICING`, `CMD_SPEED`, `CMD_SPEED_MAP`, `CMD_SPEED_LIMIT`, `CMD_SOURCE_PROSODY`, `PROS_PITCH_CENTRE`, `PROS_PLAY_TARGET`, `PAR_NEXT_PHONEME`; `PAR_VOICE_A`, `CMD_SPEED_MAP_DATA`, `fw_cmd_speed_map_data` (`data/fw_data.h`); `fw_replacement_find`, `FW_OUT_*` (`engine/thunks.h`); the thunk style of `src/engine/thunks.c`; the harness of `tests/test_fw_pros.c` (copied).
- Produces (used by Tasks 5 and 6):

```c
/* fw.h, after FW_TMP_1809 */
#define FW_PROS_SCALE_DEV    0x1811
#define FW_PROS_SCALE_FRAC   0x1812
#define FW_PROS_DEV_FRAC     0x1813
#define FW_PROS_DEV_QUARTER  0x1814
/* par_addr.h: the 22 ROM_* addresses, ROM_FRM_BUILD_REST_FRAME 0xB41D .. ROM_PAR_SCALE_DURATION 0xC29C (Step 3) */
/* par_segment.h */
#define PAR_CLAMP_MIN      1
#define PAR_CLAMP_MAX      0x3F
#define PAR_VOICING_FLOOR  2
void par_next_row(fw *f, fw_regs *r);       /* $BD07 */
void par_next_param(fw *f, fw_regs *r);     /* $BD14 */
void par_clamp(fw *f, fw_regs *r);          /* $BD24 */
void par_scale_voicing(fw *f, fw_regs *r);  /* $BFC3 */
/* pros_play.h */
#define PROS_TARGET_FLOOR  4
#define PROS_MICRO_STEP    5
void pros_scale_target(fw *f, fw_regs *r);  /* $BDD3 */
void pros_microprosody(fw *f, fw_regs *r);  /* $B983 */
/* par_boundary.h */
#define PAR_FAST_SPEED      5
#define PAR_SPEED_MAP_LAST  0x1F
void par_scale_duration(fw *f, fw_regs *r); /* $C29C */
```

  - The replacements `"pros_microprosody"` `0xB983`, `"par_next_row"` `0xBD07`, `"par_next_param"` `0xBD14`, `"par_clamp"` `0xBD24`, `"pros_scale_target"` `0xBDD3`, `"par_scale_voicing"` `0xBFC3` and `"par_scale_duration"` `0xC29C`, all `FW_OUT_A | FW_OUT_X | FW_OUT_Y`, between the `tn_convert_clause` and `cmd_hex_value` rows: 35 replacements, 36 `golden_hybrid` modes.
  - `tests/test_fw_par.c` and its harness, which Tasks 5 and 6 reuse and extend (they define no mock or setup of their own): `MAX_CALLS`, `rom_call_record`, `g_ram` (64 KiB), `g_before`, `g_f`, `g_host`, `g_calls`, `g_ncalls`, `mock_rom_call` (records every call and fails it; Task 6 makes it answer the three `frm_*` calls), `setup()`, `count(addr)`, `check_call(i, addr, a, x, y)`, `check_row(name, addr, outputs)`, `snapshot()`, `check_written(addrs, n)` (every RAM byte $0000–$3FFF but the listed ones is as `snapshot()` saw it), `check_no_calls()`. `test_microprosody();` is the last line of `suite_fw_par` (Task 5's anchor; Task 6 then anchors on Task 5's last line). `suite_fw_par` is the 32nd suite, after `suite_fw_pros`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_fw_par.c`:

```c
/* Unit checks of the playback set-up on the converted data (M4.4a spec 4.4), with a mock host that records every
   ROM call. They reach what no corpus text reaches (par_clamp at its edges, every @B with v = 0, 1, 8, the
   R = 0 path of pros_scale_target below the limit, a microprosody step that wraps). Each expected value is worked
   out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. The full proof is the
   live differential test (diff_lts) and the hybrid goldens (golden_hybrid); the thunks' register outputs are
   compared there, against the ROM. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_segment.h"
#include "firmware/pros_play.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 64

typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static uint8_t g_before[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call, and fails it: none of the leaves makes one. */
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
    return FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
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

/* The RAM as it is now, for check_written. */
static void snapshot(void) { memcpy(g_before, g_ram, sizeof g_before); }

/* Every RAM byte except the n listed is as snapshot() saw it (the listed ones are checked by the caller). */
static void check_written(const uint16_t *addrs, size_t n)
{
    for (uint16_t a = 0; a < FW_RAM_SIZE; a++) {
        bool listed = false;
        for (size_t i = 0; i < n; i++)
            listed |= addrs[i] == a;
        if (!listed && g_ram[a] != g_before[a]) {
            fprintf(stderr, "  RAM $%04X written: %02X -> %02X\n", a, g_before[a], g_ram[a]);
            CHECK(g_ram[a] == g_before[a]);
        }
    }
}

/* No ROM call was made and nothing faulted. */
static void check_no_calls(void)
{
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* ---- the leaves (M4.4a Task 4) ---- */

static void test_leaf_rows(void)
{
    check_row("pros_microprosody", ROM_PROS_MICROPROSODY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_next_row", ROM_PAR_NEXT_ROW, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_next_param", ROM_PAR_NEXT_PARAM, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_clamp", ROM_PAR_CLAMP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("pros_scale_target", ROM_PROS_SCALE_TARGET, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_scale_voicing", ROM_PAR_SCALE_VOICING, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_scale_duration", ROM_PAR_SCALE_DURATION, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* par_next_row with par_row_ptr = ptr and par_row_len = len: the new pointer and A = its low byte (CLC; ADC; STA;
   BCC; INC hi), X and Y untouched, only par_row_ptr written. */
static void next_row_case(uint16_t ptr, uint8_t len, uint16_t want)
{
    static const uint16_t w[] = { PAR_ROW_PTR, PAR_ROW_PTR + 1 };
    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, ptr);
    g_ram[PAR_ROW_LEN] = len;
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_next_row(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), want);
    CHECK_EQ(r.a, want & 0xFF);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    check_written(w, 2);
    check_no_calls();
}

static void test_next_row(void)
{
    next_row_case(PAR_VOICE_A, 86, PAR_VOICE_A + 86);             /* $D46A + N = $D4C0: no carry */
    next_row_case(0xD4C0, 86, 0xD516);                            /* $C0 + $56 = $116: the carry into the high byte */
    next_row_case(0xFFF0, 0x20, 0x0010);                          /* the high byte wraps: INC $33 is 8 bits */
}

/* par_next_param with par_row_ptr = ptr and par_param_stride = stride: the 16-bit sum with the carry between the
   bytes (CLC once), A = its high byte. */
static void next_param_case(uint16_t ptr, uint16_t stride, uint16_t want)
{
    static const uint16_t w[] = { PAR_ROW_PTR, PAR_ROW_PTR + 1 };
    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, ptr);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, stride);
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_next_param(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), want);
    CHECK_EQ(r.a, want >> 8);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    check_written(w, 2);
    check_no_calls();
}

static void test_next_param(void)
{
    next_param_case(0xD400, 5 * 86, 0xD5AE);   /* the stride of N = 86 is 430 = $01AE: no carry */
    next_param_case(0xD4C0, 5 * 86, 0xD66E);   /* $C0 + $AE = $16E: the carry into $D4 + $01 */
    next_param_case(0xFF80, 0x0080, 0x0000);   /* the sum wraps in 16 bits: A = 0 */
}

/* par_clamp: CMP #1; BPL: the N flag of A - 1 is set for 0 and $81-$FF (1); CMP #$40; BPL: the N flag of A - $40 is
   clear for $40-$80 ($3F); 1-$3F pass. */
static void test_clamp(void)
{
    static const struct { uint8_t in, out; } k[] = {
        { 0x00, 1 }, { 0x01, 0x01 }, { 0x3F, 0x3F }, { 0x40, 0x3F }, { 0x80, 0x3F }, { 0x81, 1 }, { 0xFF, 1 },
        { 0x20, 0x20 }, { 0x7F, 0x3F }, { 0xC0, 1 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        setup();
        snapshot();
        fw_regs r = { k[i].in, 0x5A, 0xA5, 0 };
        par_clamp(&g_f, &r);
        CHECK_EQ(r.a, k[i].out);
        CHECK_EQ(r.x, 0x5A);
        CHECK_EQ(r.y, 0xA5);
        check_written(NULL, 0);
        check_no_calls();
    }
}

/* par_scale_voicing at every @B 0-9 with v = 0, 1, 8 and 61 (a P10 target: 61/2 = 30, /4 = 15, /8 = 7, /16 = 3).
   FW_TMP_76 starts at $EE: the term the ROM stores there, or $EE where it stores nothing (v = 1 and 8 at every @B,
   @B0, @B1, @B5, @B8, @B9, and v = 0 at @B2 and @B3, which give 2 at the BEQ $BFD1 first). */
static void test_scale_voicing(void)
{
    static const uint8_t v[4] = { 0, 1, 8, 61 };
    static const struct { uint8_t out[4], tmp[4]; } k[10] = {
        { { 2, 1, 8, 2 }, { 0xEE, 0xEE, 0xEE, 0xEE } },     /* @B0: LDA #2 */
        { { 2, 1, 8, 15 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B1: v/4 */
        { { 2, 1, 8, 19 }, { 0xEE, 0xEE, 0xEE, 15 } },      /* @B2: 3 + 15 + 1 (SEC; ADC); $76 = v/4 */
        { { 2, 1, 8, 22 }, { 0xEE, 0xEE, 0xEE, 15 } },      /* @B3: 7 + 15; $76 = v/4 */
        { { 0, 1, 8, 25 }, { 0, 0xEE, 0xEE, 22 } },         /* @B4: 3 + (7 + 15); $76 = v/8 + v/4; 0 gives 0 */
        { { 0, 1, 8, 30 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B5: v/2 */
        { { 0, 1, 8, 37 }, { 0, 0xEE, 0xEE, 30 } },         /* @B6: 7 + 30; $76 = v/2 */
        { { 0, 1, 8, 45 }, { 0, 0xEE, 0xEE, 30 } },         /* @B7: 15 + 30; $76 = v/2 */
        { { 0, 1, 8, 61 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B8: PLA, v */
        { { 0, 1, 8, 61 }, { 0xEE, 0xEE, 0xEE, 0xEE } },    /* @B9 (the corpus never sets it): as @B8 */
    };
    static const uint16_t w[] = { FW_TMP_76 };
    for (uint8_t b = 0; b < 10; b++) {
        for (unsigned i = 0; i < 4; i++) {
            setup();
            g_ram[CMD_VOICING] = b;
            g_ram[FW_TMP_76] = 0xEE;
            snapshot();
            fw_regs r = { v[i], 0x5A, 0xA5, 0 };
            par_scale_voicing(&g_f, &r);
            CHECK_EQ(r.a, k[b].out[i]);
            CHECK_EQ(g_ram[FW_TMP_76], k[b].tmp[i]);
            CHECK_EQ(r.x, 0x5A);
            CHECK_EQ(r.y, 0xA5);
            check_written(w, 1);
            check_no_calls();
        }
    }
    setup();                                            /* each term truncates: $FF at @B2 is 15 + 63 + 1 */
    g_ram[CMD_VOICING] = 2;
    fw_regs r = { 0xFF, 0, 0, 0 };
    par_scale_voicing(&g_f, &r);
    CHECK_EQ(r.a, 79);
}

/* par_scale_duration at speed `speed` with A = a and Y = $C3 (X = $5A): A out, FW_TMP_78 (starting at $EE). */
static void scale_duration_case(uint8_t speed, uint8_t a, uint8_t want_a, uint8_t want_78)
{
    static const uint16_t w[] = { FW_TMP_78 };
    g_ram[CMD_SPEED] = speed;
    g_ram[FW_TMP_78] = 0xEE;
    snapshot();
    fw_regs r = { a, 0x5A, 0xC3, 0 };
    par_scale_duration(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(g_ram[FW_TMP_78], want_78);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xC3);                                /* LDY $78: the entry Y, on every path */
    check_written(w, 1);
    check_no_calls();
}

static void test_scale_duration(void)
{
    setup();                                            /* a map in RAM: index i holds $80 + i */
    fw_wr16(&g_f, CMD_SPEED_MAP, 0x3000);
    for (unsigned i = 0; i < 64; i++)
        g_ram[0x3000 + i] = (uint8_t)(0x80 + i);
    scale_duration_case(9, 0, 0, 0xEE);                 /* CMP #0; BEQ: 0 passes, $78 is not written */
    scale_duration_case(1, 0, 0, 0xEE);
    scale_duration_case(4, 40, 40, 0xC3);               /* below speed 5 (CPY #5; BCC): $78 = Y, A unchanged */
    scale_duration_case(1, 0xFF, 0xFF, 0xC3);
    scale_duration_case(5, 30, 0x80 + 30, 0xC3);        /* speed 5 and up: map[A] ... */
    scale_duration_case(5, 31, 0x80 + 31, 0xC3);
    scale_duration_case(9, 32, 0x80 + 31, 0xC3);        /* ... the index capped at 31 (CMP #$1F; BCC; LDA #$1F) */
    scale_duration_case(7, 0xFF, 0x80 + 31, 0xC3);
    scale_duration_case(6, 1, 0x80 + 1, 0xC3);

    setup();                                            /* the speed 5 map in ROM, through the pointer (fw_ind_y) */
    fw_wr16(&g_f, CMD_SPEED_MAP, CMD_SPEED_MAP_DATA + 32 * 4);
    scale_duration_case(5, 10, fw_cmd_speed_map_data[4][10], 0xC3);
    scale_duration_case(5, 40, fw_cmd_speed_map_data[4][31], 0xC3);
}

/* One pros_scale_target run. In: pros_play_target, cmd_source_prosody, cmd_speed_limit, pros_pitch_centre; the four
   scratch bytes start at $EE. Out: the new target, and $1811 (FW_PROS_SCALE_DEV, $EE when not written); A = the
   entry Y ($C3), X ($5A) and Y preserved. $1812-$1814 are checked by the caller. */
static void scale_target_case(uint8_t target, uint8_t prosody, uint8_t limit, uint8_t centre, uint8_t want,
                              uint8_t want_dev)
{
    static const uint16_t w[] = {
        PROS_PLAY_TARGET, FW_PROS_SCALE_DEV, FW_PROS_SCALE_FRAC, FW_PROS_DEV_FRAC, FW_PROS_DEV_QUARTER,
    };
    setup();
    g_ram[PROS_PLAY_TARGET] = target;
    g_ram[CMD_SOURCE_PROSODY] = prosody;
    g_ram[CMD_SPEED_LIMIT] = limit;
    g_ram[PROS_PITCH_CENTRE] = centre;
    memset(g_ram + FW_PROS_SCALE_DEV, 0xEE, 4);
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xC3, 0 };
    pros_scale_target(&g_f, &r);
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], want);
    CHECK_EQ(g_ram[FW_PROS_SCALE_DEV], want_dev);
    CHECK_EQ(r.a, 0xC3);                                /* PLA; TAY last: A = the entry Y */
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xC3);
    check_written(w, 5);
    check_no_calls();
}

/* The split of d = |target - $80| (written on every path): FW_PROS_DEV_QUARTER = d >> 2, FW_PROS_DEV_FRAC =
   (d & 3) << 6, and FW_PROS_SCALE_FRAC as the adds and the ASL leave it. */
static void check_split(uint8_t quarter, uint8_t frac, uint8_t scale_frac)
{
    CHECK_EQ(g_ram[FW_PROS_DEV_QUARTER], quarter);
    CHECK_EQ(g_ram[FW_PROS_DEV_FRAC], frac);
    CHECK_EQ(g_ram[FW_PROS_SCALE_FRAC], scale_frac);
}

static void test_scale_target(void)
{
    /* R = 0 below the limit: the BEQ at $BE06 sees the Z of CMP cmd_speed_limit (clear), so it goes on: Y = 0
       (R < 4), k = 0, s = TYA = 0, $1811 = 0 at $BE31; centre + 0 */
    scale_target_case(0x90, 0x08, 7, 0x60, 0x60, 0);   /* @R bits 0-2 only: $08 & 7 = 0 */
    check_split(4, 0, 0);                               /* d = $10 */
    /* R = 0 at the limit: R = limit & 7 = 0, BEQ $BE52: the centre, $1811 not written */
    scale_target_case(0x90, 0, 0, 0x60, 0x60, 0xEE);
    check_split(4, 0, 0);
    /* R above the limit becomes the limit: R = 5, limit 0 (speeds 7-9): the centre, $1811 not written */
    scale_target_case(0x90, 5, 0, 0x61, 0x61, 0xEE);
    /* R = 7, limit 7 (not below it): R = 7 & 7. d = $40: Y = d (R >= 4), $1811 = $40 + 3 x $10 = $70, frac 0; centre
       $F0 + $70 carries: $FF ($BE4D), then CMP #4 is negative: ORA #4 keeps $FF */
    scale_target_case(0xC0, 7, 7, 0xF0, 0xFF, 0x70);
    check_split(0x10, 0, 0);
    /* the same deviation below $80: centre $20 - $70 borrows: 4 ($BE65), and CMP #4 passes 4 */
    scale_target_case(0x40, 7, 7, 0x20, 4, 0x70);
    /* R = 3 (Y = 0, the signed CMP #4 / BPL): d = $10, s = 3 x 4 = $0C; centre $0E - $0C = 2: ORA #4 gives 6 */
    scale_target_case(0x70, 3, 7, 0x0E, 6, 0x0C);
    /* R = 3, d = 1: the fraction $40 three times: $1812 = $C0, ASL carries 1 into ADC #0: s = 1, $1812 = $80;
       centre $87 + 1 = $88 is negative after CMP #4: ORA #4 gives $8C */
    scale_target_case(0x81, 3, 7, 0x87, 0x8C, 1);
    check_split(0, 0x40, 0x80);
    /* R = 7, d = 3: Y = 3; the fraction $C0 chains its carry into $1811: $1812 $C0, $80 (C), $40 (C); $1811 3, 4, 5;
       ASL $40: no carry: s = 5 (3 x 7 / 4 = 5.25); centre $40 + 5 */
    scale_target_case(0x83, 7, 7, 0x40, 0x45, 5);
    check_split(0, 0xC0, 0x80);
    /* R = 4 (k = 0): s = Y = d = $10 through TYA at $BE58 */
    scale_target_case(0x90, 4, 7, 0x50, 0x60, 0x10);
    /* R = 5 limited from 7 by limit 5 (R & 7 = 5): d = $13, quarter 4, frac $C0; s = $13 + 4 = $17, $1812 $C0, ASL
       carries: $18 (19 x 5 / 4 = 23.75); centre $40 + $18 */
    scale_target_case(0x93, 7, 5, 0x40, 0x58, 0x18);
    check_split(4, 0xC0, 0x80);
    /* target 0: d = $80 (EOR #$FF; ADC #1 of $80); R = 3: s = 3 x $20 = $60; centre $70 - $60 = $10 */
    scale_target_case(0x00, 3, 7, 0x70, 0x10, 0x60);
    check_split(0x20, 0, 0);
    /* a result of $80-$83: CMP #4 positive, no ORA (centre $83, R = 0 at limit 0) */
    scale_target_case(0x10, 0, 0, 0x83, 0x83, 0xEE);
    /* a centre of 0-3 on the R = 0 path still gets bit 2 */
    scale_target_case(0x10, 0, 0, 0x01, 0x05, 0xEE);
}

/* pros_microprosody with par_next_phoneme = ph and pros_play_target = t: A and the target left; X and Y untouched,
   only the target may be written. */
static void micro_case(uint8_t ph, uint8_t t, uint8_t want_a, uint8_t want_t)
{
    static const uint16_t w[] = { PROS_PLAY_TARGET };
    setup();
    g_ram[PAR_NEXT_PHONEME] = ph;
    g_ram[PROS_PLAY_TARGET] = t;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xA5, 0 };
    pros_microprosody(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], want_t);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    check_no_calls();
}

static void test_microprosody(void)
{
    static const uint8_t up[] = { 0x15, 0x18, 0x20, 0x41 }, down[] = { 0x1F, 0x0D };
    for (size_t i = 0; i < sizeof up; i++) {
        micro_case(up[i], 0x50, 0x55, 0x55);            /* CLC; ADC #5; BCC: stored */
        micro_case(up[i], 0xFA, 0xFF, 0xFF);
        micro_case(up[i], 0xFB, 0x00, 0xFB);            /* carries: not stored, A = the wrapped sum */
        micro_case(up[i], 0xFF, 0x04, 0xFF);
    }
    for (size_t i = 0; i < sizeof down; i++) {
        micro_case(down[i], 0x50, 0x4B, 0x4B);          /* SEC; SBC #5; BCS: stored */
        micro_case(down[i], 0x05, 0x00, 0x00);
        micro_case(down[i], 0x04, 0xFF, 0x04);          /* borrows: not stored, A = the wrapped difference */
        micro_case(down[i], 0x00, 0xFB, 0x00);
    }
    micro_case(0x3B, 0x50, 0x3B, 0x50);                 /* any other phoneme: RTS with A = the phoneme */
    micro_case(0x00, 0xFB, 0x00, 0xFB);
    micro_case(0x14, 0x50, 0x14, 0x50);
    micro_case(0xFF, 0x50, 0xFF, 0x50);
}

void suite_fw_par(void)
{
    CHECK(fw_image_get() != NULL);
    test_leaf_rows();
    test_next_row();
    test_next_param();
    test_clamp();
    test_scale_voicing();
    test_scale_duration();
    test_scale_target();
    test_microprosody();
}
```

In `tests/suites.h`, replace

```c
SUITE(suite_fw_pros)
```

with

```c
SUITE(suite_fw_pros)
SUITE(suite_fw_par)
```

What the tests pin (spec §4.4, the part for this task):
- `test_leaf_rows`: the seven rows, their addresses and declared outputs.
- `test_next_row`, `test_next_param`: the 16-bit adds, the carry between the bytes, the 16-bit wrap, the returned A (the low byte for `par_next_row`, the high byte for `par_next_param`), X and Y untouched, nothing else written.
- `test_clamp`: 0, 1, $3F, $40, $80, $81, $FF (and $20, $7F, $C0).
- `test_scale_voicing`: every @B 0–9 with v = 0, 1, 8 and 61; `FW_TMP_76` written at @B2/@B3 (v ≠ 0), @B4, @B6 and @B7 only; the truncation of each term.
- `test_scale_duration`: 0 leaves `FW_TMP_78` unwritten; speed < 5 writes it and leaves A; speed ≥ 5 maps through `cmd_speed_map` (a RAM map and the ROM's speed-5 map) with the index capped at 31; Y comes back on every path.
- `test_scale_target`: exit A = the entry Y with X and Y preserved (every case); R = 0 below the limit writes $1811 = 0; R = 0 at the limit, and R above a limit of 0, leave $1811; a limited R that is not 0; the $FF cap; the floor 4; the `ORA #4` for 0–3 and ≥ $84 (and none for $80–$83); R ≥ 4 (k = 0 and k = 1, 3); the carry chain and the rounding; a target of 0 (d = $80); the split bytes $1812–$1814.
- `test_microprosody`: the six phonemes, the store only without a wrap, A = the wrapped value otherwise, other phonemes (A = the phoneme).

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_par"`
Expected: a compile error, `tests\test_fw_par.c(10): fatal error C1083: Cannot open include file: 'firmware/par_addr.h': No such file or directory`.

- [ ] **Step 3: Implement**

Create `src/firmware/par_addr.h`:

```c
#ifndef FW_PAR_ADDR_H
#define FW_PAR_ADDR_H
/* ROM code addresses of the playback set-up (M4.4a spec 2.2) and of the three frame-builder routines that
   par_setup_voice calls (2.4), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image. */
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* M4.5; called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* M4.5; called by par_setup_voice $B407, $B416 (and par_play $B8DA) */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* M4.5; called by par_setup_voice $B40A, $B419 (and par_play $B8E0,
                                               frm_finish_frame $B9BD) */
#define ROM_PROS_MICROPROSODY      0xB983   /* called by par_read_segment $BD67 */
#define ROM_PAR_PLAY_START         0xBAF2   /* called by boot_play_body $B2B6 */
#define ROM_PAR_RESET_TRACKS       0xBB35   /* called by par_play_start $BAF2 */
#define ROM_PAR_NEXT_SEGMENT       0xBCF3   /* called by par_play $B900 */
#define ROM_PAR_NEXT_ROW           0xBD07   /* called by par_setup_voice $B3BE, $B3C7; par_boundary_cur_dom $BF09,
                                               $BF14; par_boundary_next_dom $BF4E, $BF59; par_read_dominance $BF7B,
                                               $BF8D, $BFBF */
#define ROM_PAR_NEXT_PARAM         0xBD14   /* called by frm_build_rest_frame $B431, par_read_segment $BD9D,
                                               par_boundary_jump $BEAC */
#define ROM_PAR_CLAMP              0xBD24   /* called by frm_raise_formants $B970, $B97D; par_read_segment $BD8F;
                                               par_read_dominance $BF75; par_boundary_value $C0BF */
#define ROM_PAR_READ_SEGMENT       0xBD33   /* called by par_next_segment $BCFD */
#define ROM_PROS_SCALE_TARGET      0xBDD3   /* called by par_play_start $BAFE, par_read_segment $BD64 */
#define ROM_PAR_BOUNDARY_PASS      0xBE6A   /* called by par_next_segment $BD00 */
#define ROM_PAR_BOUNDARY_JUMP      0xBE94   /* called by par_boundary_pass $BE7F */
#define ROM_PAR_STORE_BOUNDARY     0xBEB5   /* called by par_boundary_jump $BEA9, par_boundary_value $C0AE */
#define ROM_PAR_BOUNDARY_CUR_DOM   0xBEE2   /* called by par_boundary_pass $BE90 */
#define ROM_PAR_BOUNDARY_NEXT_DOM  0xBF27   /* called by par_boundary_pass $BE8C */
#define ROM_PAR_READ_DOMINANCE     0xBF6C   /* called by par_boundary_cur_dom $BEFE, par_boundary_next_dom $BF43 */
#define ROM_PAR_SCALE_VOICING      0xBFC3   /* called by par_read_segment $BD96, par_read_dominance $BF87 */
#define ROM_PAR_BOUNDARY_VALUE     0xC039   /* called by par_boundary_cur_dom $BF19, par_boundary_next_dom $BF5E */
#define ROM_PAR_PLAN_TRACKS        0xC0D2   /* called by par_next_segment $BD03 */
#define ROM_PAR_SCALE_DURATION     0xC29C   /* called by par_boundary_cur_dom $BF03, $BF0E; par_boundary_next_dom
                                               $BF48, $BF53 */

#endif
```

Create `src/firmware/par_segment.h`:

```c
#ifndef FW_PAR_SEGMENT_H
#define FW_PAR_SEGMENT_H
/* The segment reader of the playback set-up and its leaves (ROM $BD07-$BD32 and $BFC3-$C038), decompiled (M4.4a
   spec 2.2). Every routine takes the registers the ROM has at its entry in r (r->p is not read: no routine here
   reads an entry flag) and returns in r the A, X and Y the ROM leaves at its RTS; a register the routine does not
   touch is left as it came in. */
#include "fw.h"

#define PAR_CLAMP_MIN      1      /* par_clamp: LDA #1 */
#define PAR_CLAMP_MAX      0x3F   /* par_clamp: LDA #$3F */
#define PAR_VOICING_FLOOR  2      /* par_scale_voicing: LDA #2, at @B0, and for 0 at @B1-@B3 */

/* $BD07: par_row_ptr += par_row_len, 16 bits (the next row of N bytes). A = the new low byte; X and Y untouched. */
void par_next_row(fw *f, fw_regs *r);
/* $BD14: par_row_ptr += par_param_stride, 16 bits (the same row of the next parameter, 5N bytes on). A = the new
   high byte; X and Y untouched. */
void par_next_param(fw *f, fw_regs *r);
/* $BD24: A clamped by the N flag of two 8-bit differences (CMP; BPL): 1 when A - 1 has bit 7 set (A = 0 or
   $81-$FF), $3F when A - $40 has bit 7 clear (A = $40-$80), else A (1-$3F). X and Y untouched. */
void par_clamp(fw *f, fw_regs *r);
/* $BFC3: a voicing value v = A scaled by @B (cmd_voicing). 1 and 8 pass at every @B. Otherwise @B0 gives 2;
   @B1 v/4; @B2 v/16 + v/4 + 1; @B3 v/8 + v/4; @B4 v/16 + v/8 + v/4; @B5 v/2; @B6 v/8 + v/2; @B7 v/4 + v/2;
   @B8 and above v. Each term truncates; v = 0 gives 2 at @B1-@B3 (0 at @B4-@B7). FW_TMP_76 is scratch at @B2,
   @B3 (not for v = 0), @B4, @B6 and @B7. A = the result; X and Y untouched. */
void par_scale_voicing(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/par_segment.c`:

```c
#include "par_segment.h"
#include "fw_ops.h"

/* par_scale_voicing, one helper per @B block that keeps a term in FW_TMP_76; v is not 0 at @B2 and @B3 (their
   BEQ $BFD1 gives 2 first), and may be 0 at @B4, @B6 and @B7. */

/* $BFE5-$BFEE (@B2): v/4 in FW_TMP_76, then v/16 + FW_TMP_76 + 1 (SEC; ADC). */
static uint8_t voicing_b2(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 2));
    uint8_t c = 1;                                           /* SEC */
    return fw_adc((uint8_t)(v >> 4), fw_rd8(f, FW_TMP_76), &c);
}

/* $BFF6-$BFFE (@B3): v/4 in FW_TMP_76, then v/8 + FW_TMP_76 (CLC; ADC). */
static uint8_t voicing_b3(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 2));
    uint8_t c = 0;                                           /* CLC */
    return fw_adc((uint8_t)(v >> 3), fw_rd8(f, FW_TMP_76), &c);
}

/* $C004-$C014 (@B4): v/4 in FW_TMP_76; v/8 (PHA) + FW_TMP_76 back into FW_TMP_76; then v/16 (PLA; LSR) +
   FW_TMP_76. */
static uint8_t voicing_b4(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 2));
    uint8_t c = 0;                                           /* CLC */
    fw_wr8(f, FW_TMP_76, fw_adc((uint8_t)(v >> 3), fw_rd8(f, FW_TMP_76), &c));
    c = 0;                                                   /* CLC */
    return fw_adc((uint8_t)(v >> 4), fw_rd8(f, FW_TMP_76), &c);
}

/* $C021-$C029 (@B6): v/2 in FW_TMP_76, then v/8 + FW_TMP_76. */
static uint8_t voicing_b6(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 1));
    uint8_t c = 0;                                           /* CLC */
    return fw_adc((uint8_t)(v >> 3), fw_rd8(f, FW_TMP_76), &c);
}

/* $C02F-$C036 (@B7): v/2 in FW_TMP_76, then v/4 + FW_TMP_76. */
static uint8_t voicing_b7(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 1));
    uint8_t c = 0;                                           /* CLC */
    return fw_adc((uint8_t)(v >> 2), fw_rd8(f, FW_TMP_76), &c);
}

void par_next_row(fw *f, fw_regs *r)
{
    uint8_t c = 0;                                           /* CLC */
    r->a = fw_adc(fw_rd8(f, PAR_ROW_PTR), fw_rd8(f, PAR_ROW_LEN), &c);
    fw_wr8(f, PAR_ROW_PTR, r->a);
    if (c)                                                   /* BCC $BD13 */
        fw_inc8(f, PAR_ROW_PTR + 1);
}

void par_next_param(fw *f, fw_regs *r)
{
    uint8_t c = 0;                                           /* CLC */
    fw_wr8(f, PAR_ROW_PTR, fw_adc(fw_rd8(f, PAR_ROW_PTR), fw_rd8(f, PAR_PARAM_STRIDE), &c));
    r->a = fw_adc(fw_rd8(f, PAR_ROW_PTR + 1), fw_rd8(f, PAR_PARAM_STRIDE + 1), &c);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
}

void par_clamp(fw *f, fw_regs *r)
{
    (void)f;
    if ((uint8_t)(r->a - 1) & 0x80)                          /* CMP #1; BPL $BD2B */
        r->a = PAR_CLAMP_MIN;
    else if (!((uint8_t)(r->a - 0x40) & 0x80))               /* CMP #$40; BPL $BD30 */
        r->a = PAR_CLAMP_MAX;
}

void par_scale_voicing(fw *f, fw_regs *r)
{
    uint8_t v = r->a;
    if (v == 8 || v == 1)                                    /* $BFC3-$BFC9: BEQ $BFD3 */
        return;
    switch (fw_rd8(f, CMD_VOICING)) {                        /* PHA; LDA cmd_voicing; CMP #n; BNE; PLA */
    case 0:                                                  /* $BFD0-$BFD3 */
        r->a = PAR_VOICING_FLOOR;
        break;
    case 1:                                                  /* $BFD8-$BFDD */
        r->a = v == 0 ? PAR_VOICING_FLOOR : (uint8_t)(v >> 2);
        break;
    case 2:                                                  /* $BFE2-$BFEE */
        r->a = v == 0 ? PAR_VOICING_FLOOR : voicing_b2(f, v);
        break;
    case 3:                                                  /* $BFF3-$BFFE */
        r->a = v == 0 ? PAR_VOICING_FLOOR : voicing_b3(f, v);
        break;
    case 4:                                                  /* $C003-$C014: no test for 0 */
        r->a = voicing_b4(f, v);
        break;
    case 5:                                                  /* $C019-$C01B */
        r->a = (uint8_t)(v >> 1);
        break;
    case 6:                                                  /* $C020-$C029 */
        r->a = voicing_b6(f, v);
        break;
    case 7:                                                  /* $C02E-$C036 */
        r->a = voicing_b7(f, v);
        break;
    default:                                                 /* $C037-$C038: PLA, v unchanged */
        break;
    }
}
```

Create `src/firmware/pros_play.h`:

```c
#ifndef FW_PROS_PLAY_H
#define FW_PROS_PLAY_H
/* The pitch target at playback (ROM $BDD3-$BE69 and $B983-$B9B4), decompiled (M4.4a spec 2.2): each segment's
   pitch target, scaled around the @F centre by @R, then moved by the microprosody of the phoneme starting to play.
   r in = the registers at entry (r->p is not read); r out = the A, X and Y the ROM leaves at its RTS. */
#include "fw.h"

#define PROS_TARGET_FLOOR  4   /* pros_scale_target: LDA #4 on a borrow; CMP #4 / ORA #4 sets bit 2 below it */
#define PROS_MICRO_STEP    5   /* pros_microprosody: ADC #5 / SBC #5 */

/* $BDD3: pros_play_target scaled by R/4 around pros_pitch_centre. d = |pros_play_target - $80| ($80 for a target
   of 0) is split into FW_PROS_DEV_QUARTER = d >> 2 and FW_PROS_DEV_FRAC = (d & 3) << 6. R = cmd_source_prosody & 7,
   or cmd_speed_limit & 7 when R is not below cmd_speed_limit. When that limited R is 0, the result is the centre
   and FW_PROS_SCALE_DEV is not written; R = 0 below the limit goes on (the BEQ at $BE06 tests the Z of the CMP) and
   writes FW_PROS_SCALE_DEV = 0. Otherwise s = (d when R >= 4, else 0) + (R & 3) chained adds of the quarter and the
   fraction bits, rounded by bit 7 of FW_PROS_SCALE_FRAC; FW_PROS_SCALE_DEV = s. The result is centre + s ($FF on a
   carry) for a target >= $80, centre - s (4 on a borrow) below; a result of 0-3 or $84-$FF gets bit 2 (CMP #4;
   BPL; ORA #4); pros_play_target = the result. X and Y are preserved (PHA / PLA); A = the entry Y. */
void pros_scale_target(fw *f, fw_regs *r);
/* $B983: for par_next_phoneme $15, $18, $20 or $41, A = pros_play_target + 5; for $1F or $0D, A =
   pros_play_target - 5; stored only when the add does not carry or the subtract does not borrow (A keeps the
   wrapped value). Any other phoneme: A = the phoneme. X and Y untouched. */
void pros_microprosody(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/pros_play.c`:

```c
#include "pros_play.h"
#include "fw_ops.h"

/* $BDD7-$BDF8: d = |pros_play_target - $80| (SEC; SBC #$80; BPL, else EOR #$FF; CLC; ADC #1); FW_PROS_DEV_FRAC =
   FW_PROS_SCALE_FRAC = 0; then twice LSR A / ROR FW_PROS_DEV_FRAC, and FW_PROS_DEV_QUARTER = A. Returns d (TAY). */
static uint8_t split_deviation(fw *f)
{
    uint8_t d = (uint8_t)(fw_rd8(f, PROS_PLAY_TARGET) - 0x80);
    if (d & 0x80)
        d = (uint8_t)((d ^ 0xFF) + 1);
    fw_wr8(f, FW_PROS_DEV_FRAC, 0);
    fw_wr8(f, FW_PROS_SCALE_FRAC, 0);
    uint8_t a = d;
    for (unsigned i = 0; i < 2; i++) {                       /* LDX #2; DEX; BNE */
        fw_wr8(f, FW_PROS_DEV_FRAC, (uint8_t)((a & 1) << 7 | fw_rd8(f, FW_PROS_DEV_FRAC) >> 1));
        a >>= 1;
    }
    fw_wr8(f, FW_PROS_DEV_QUARTER, a);
    return d;
}

/* $BDF9-$BE06: R = cmd_source_prosody & 7; when not below cmd_speed_limit (CMP; BCC, unsigned), cmd_speed_limit & 7.
   *zero = the Z flag the BEQ at $BE06 tests: that of the AND on the limited path, that of the CMP (clear, since
   R < cmd_speed_limit) otherwise. */
static uint8_t limit_r(fw *f, bool *zero)
{
    uint8_t R = fw_rd8(f, CMD_SOURCE_PROSODY) & 7;
    uint8_t limit = fw_rd8(f, CMD_SPEED_LIMIT);
    if (R < limit) {                                         /* BCC $BE06 */
        *zero = false;
        return R;
    }
    R = limit & 7;
    *zero = R == 0;
    return R;
}

/* $BE08-$BE30, $BE58: s. Y = d when R >= 4 (CMP #4; BPL: the N flag of R - 4), else 0. k = R & 3; k = 0 gives s = Y
   ($BE58 TYA). Else FW_PROS_SCALE_DEV = Y, then k times FW_PROS_SCALE_FRAC += FW_PROS_DEV_FRAC (CLC) and
   FW_PROS_SCALE_DEV += FW_PROS_DEV_QUARTER plus that add's carry; ASL FW_PROS_SCALE_FRAC; ADC #0 rounds. */
static uint8_t scale_deviation(fw *f, uint8_t R, uint8_t d)
{
    uint8_t y = ((uint8_t)(R - 4) & 0x80) ? 0 : d;           /* BPL $BE0E; LDY #0 */
    uint8_t k = R & 3;
    if (k == 0)                                              /* AND #3; BEQ $BE58 */
        return y;
    fw_wr8(f, FW_PROS_SCALE_DEV, y);                         /* TAX; STY $1811 */
    uint8_t a = y, c;
    for (; k != 0; k--) {                                    /* $BE16-$BE2A */
        c = 0;                                               /* CLC */
        fw_wr8(f, FW_PROS_SCALE_FRAC, fw_adc(fw_rd8(f, FW_PROS_DEV_FRAC), fw_rd8(f, FW_PROS_SCALE_FRAC), &c));
        a = fw_adc(fw_rd8(f, FW_PROS_SCALE_DEV), fw_rd8(f, FW_PROS_DEV_QUARTER), &c);
        fw_wr8(f, FW_PROS_SCALE_DEV, a);
    }
    uint8_t frac = fw_rd8(f, FW_PROS_SCALE_FRAC);
    c = frac >> 7;                                           /* ASL $1812 */
    fw_wr8(f, FW_PROS_SCALE_FRAC, (uint8_t)(frac << 1));
    return fw_adc(a, 0, &c);                                 /* ADC #0 */
}

/* $BE31-$BE39, $BE4D, $BE5C-$BE67: FW_PROS_SCALE_DEV = s; for a target >= $80 (LDX pros_play_target; BPL) centre + s
   (CLC; ADC), $FF on a carry (ORA #$FF); below, centre - s (SEC; SBC), 4 on a borrow. */
static uint8_t around_centre(fw *f, uint8_t s)
{
    fw_wr8(f, FW_PROS_SCALE_DEV, s);
    uint8_t centre = fw_rd8(f, PROS_PITCH_CENTRE);
    if (fw_rd8(f, PROS_PLAY_TARGET) & 0x80) {
        unsigned sum = (unsigned)centre + s;
        return sum > 0xFF ? 0xFF : (uint8_t)sum;             /* BCS $BE4D */
    }
    return centre >= s ? (uint8_t)(centre - s) : PROS_TARGET_FLOOR;   /* BCS $BE3F */
}

/* $BE3F-$BE45: CMP #4; BPL: when A - 4 has bit 7 set (A = 0-3 or $84-$FF), ORA #4; pros_play_target = A. */
static void store_target(fw *f, uint8_t a)
{
    if ((uint8_t)(a - PROS_TARGET_FLOOR) & 0x80)
        a |= PROS_TARGET_FLOOR;
    fw_wr8(f, PROS_PLAY_TARGET, a);
}

/* $B99F-$B9A7: pros_play_target + 5 (CLC; ADC), stored when it does not carry ($B9A5 BCC $B9B1). Returns A. */
static uint8_t micro_raise(fw *f)
{
    unsigned a = fw_rd8(f, PROS_PLAY_TARGET) + PROS_MICRO_STEP;
    if (a <= 0xFF)
        fw_wr8(f, PROS_PLAY_TARGET, (uint8_t)a);             /* $B9B1 */
    return (uint8_t)a;
}

/* $B9A8-$B9B0: pros_play_target - 5 (SEC; SBC), stored when it does not borrow ($B9AE BCS $B9B1). Returns A. */
static uint8_t micro_lower(fw *f)
{
    uint8_t t = fw_rd8(f, PROS_PLAY_TARGET);
    uint8_t a = (uint8_t)(t - PROS_MICRO_STEP);
    if (t >= PROS_MICRO_STEP)
        fw_wr8(f, PROS_PLAY_TARGET, a);                      /* $B9B1 */
    return a;
}

void pros_scale_target(fw *f, fw_regs *r)
{
    uint8_t d = split_deviation(f);                          /* after TYA; PHA; TXA; PHA */
    bool zero;
    uint8_t R = limit_r(f, &zero);
    uint8_t a;
    if (zero)                                                /* BEQ $BE52 */
        a = fw_rd8(f, PROS_PITCH_CENTRE);
    else
        a = around_centre(f, scale_deviation(f, R, d));
    store_target(f, a);
    r->a = r->y;                                             /* PLA; TAX; PLA; TAY: X and Y restored, A = the entry Y */
}

void pros_microprosody(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PAR_NEXT_PHONEME);
    switch (r->a) {                                          /* $B983-$B99E, the ROM's order */
    case 0x15: case 0x18: case 0x20: case 0x41:
        r->a = micro_raise(f);
        break;
    case 0x1F: case 0x0D:
        r->a = micro_lower(f);
        break;
    default:                                                 /* $B99E RTS */
        break;
    }
}
```

Create `src/firmware/par_boundary.h`:

```c
#ifndef FW_PAR_BOUNDARY_H
#define FW_PAR_BOUNDARY_H
/* The boundary pass of the playback set-up (ROM $BE6A-$C0D1 and $C29C-$C2B3), decompiled (M4.4a spec 2.2): at each
   segment, the boundary value and the transition durations of each parameter, by the dominance rules. r in = the
   registers at entry (r->p is not read); r out = the A, X and Y the ROM leaves at its RTS. */
#include "fw.h"

#define PAR_FAST_SPEED      5      /* par_scale_duration: CPY #5, the first speed (cmd_speed) that maps durations */
#define PAR_SPEED_MAP_LAST  0x1F   /* par_scale_duration: CMP #$1F; LDA #$1F, the last index of a 32-byte map */

/* $C29C: a transition duration A. 0 passes, and FW_TMP_78 is not written. Otherwise FW_TMP_78 = Y; at cmd_speed >= 5,
   A = cmd_speed_map[min(A, 31)] (through the pointer at $3A); slower speeds leave A. Y is restored from FW_TMP_78,
   X untouched. */
void par_scale_duration(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/par_boundary.c`:

```c
#include "par_boundary.h"

void par_scale_duration(fw *f, fw_regs *r)
{
    if (r->a == 0)                                           /* CMP #0; BEQ $C2B3 */
        return;
    fw_wr8(f, FW_TMP_78, r->y);                              /* STY $78 */
    if (fw_rd8(f, CMD_SPEED) >= PAR_FAST_SPEED) {            /* LDY cmd_speed; CPY #5; BCC $C2B1 */
        uint8_t i = r->a < PAR_SPEED_MAP_LAST ? r->a : PAR_SPEED_MAP_LAST;   /* CMP #$1F; BCC $C2AE; LDA #$1F */
        r->a = fw_ind_y(f, CMD_SPEED_MAP, i);                /* TAY; LDA ($3A),Y */
    }
    r->y = fw_rd8(f, FW_TMP_78);                             /* LDY $78 */
}
```

`src/firmware/fw.h`:
- Replace

```c
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
                                 cmd_set_prosody's parameter index */
```

with

```c
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters;
                                 par_scale_duration's saved Y ($C2A0; not written for a duration of 0) */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 frm_clamp_mark_space's 2 x b10 ($B936; called by par_setup_voice at $B40A and
                                 $B419) and frm_raise_formants' (b10 - 32) / 4 ($B969) */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
                                 cmd_set_prosody's parameter index; par_scale_voicing's first term at @B2, @B3, @B4,
                                 @B6 and @B7 ($BFE7, $BFF8, $C006/$C00D, $C022, $C030) */
```

- Replace

```c
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */
```

with

```c
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */
/* The scratch bytes of pros_scale_target $BDD3, unnamed in the annotations; no other routine uses them (M4.4a spec
   2.6). d = |pros_play_target - $80|. */
#define FW_PROS_SCALE_DEV    0x1811  /* the scaled deviation s added to (target >= $80) or subtracted from
                                        pros_pitch_centre ($BE13, $BE26, $BE31); not written when the limited R is 0
                                        ($BE52), written 0 for R = 0 below cmd_speed_limit */
#define FW_PROS_SCALE_FRAC   0x1812  /* the fraction accumulator of the R & 3 adds (0 at $BDE9); its bit 7 rounds s
                                        ($BE2C ASL) */
#define FW_PROS_DEV_FRAC     0x1813  /* d bits 1-0, in bits 7-6 (0 at $BDE6, rotated in at $BDF0) */
#define FW_PROS_DEV_QUARTER  0x1814  /* d >> 2 ($BDF6) */
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/pros_phrase.h"
#include "firmware/data/fw_data.h"
```

with

```c
#include "firmware/pros_phrase.h"
#include "firmware/par_segment.h"
#include "firmware/pros_play.h"
#include "firmware/par_boundary.h"
#include "firmware/data/fw_data.h"
```

- Replace

```c
/* tn_convert_clause $B1E6: no register input or output (boot_loop_text returns straight after it) */
static void tn_convert_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    tn_convert_clause(f);
}
```

with

```c
/* tn_convert_clause $B1E6: no register input or output (boot_loop_text returns straight after it) */
static void tn_convert_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    tn_convert_clause(f);
}

/* pros_microprosody $B983: no register input (it loads A first). A = par_next_phoneme, or pros_play_target plus or
   minus 5 (stored or not), out; X and Y untouched. The only caller, par_read_segment ($BD67), reads Y next ($BD6A
   CPY #0, $BD70 DEY, $BD71 LDA ($24),Y) and X ($BD76 STA $17BF,X), and loads A first ($BD71). No caller reads a
   flag (CPY follows). Declared: A, X, Y. */
static void pros_microprosody_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_microprosody(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_row $BD07: no register input. A = par_row_ptr's new low byte out; X and Y untouched. par_setup_voice
   reads A ($B3C1 STA par_row2_ptr, $B3CA STA par_params_ptr); par_boundary_cur_dom and par_boundary_next_dom read
   Y ($BF0C, $BF51 LDA ($32),Y; at $BF17/$BF5C PLA / TAX write A and X); par_read_dominance reads X ($BF83 CPX #9
   after $BF7B, $BF9D TXA after $BF8D) and Y ($BF90 LDA ($32),Y), and its last call ($BFBF) hands A, X and Y to its
   callers, which read Y ($BF01, $BF46) and X ($BF06, $BF56 STA $171D,X). No caller reads a flag (STA, LDA, LDY,
   PLA or RTS follow). Declared: A, X, Y. */
static void par_next_row_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_next_row(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_param $BD14: no register input. A = par_row_ptr's new high byte out; X and Y untouched. Callers read X
   (frm_build_rest_frame $B434 INX; par_read_segment $BDA0 CPX #$0B; par_boundary_jump's next par_store_boundary,
   $BEB5 STA $16ED,X) and Y (the (par_row_ptr),Y loads at $B42C, $BD83, $BEA7); each loads A before reading it
   ($B43A, $BD83/$BDA4, $BEA7 LDA; or RTS at $BEB4 into par_boundary_pass's RTS and par_plan_tracks, which loads
   it first). No caller reads a flag (INX, CPX, DEC follow). Reached from ROM in every mode through
   frm_build_rest_frame (M4.5). Declared: A, X, Y. */
static void par_next_param_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_next_param(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_clamp $BD24: A in; A clamped out, X and Y untouched. frm_raise_formants reads A and Y ($B973, $B980 STA
   ($2C),Y); par_read_segment reads X ($BD92 CPX #$0A, $BD99 STA $17BF,X) and A (there, or through
   par_scale_voicing), and Y at the next $BD83; par_read_dominance reads A ($BF78 STA par_dominated_target) and X
   ($BF83 CPX #9); par_boundary_value hands A and X to par_store_boundary ($C0C2 JMP $C0AE; $BEB5 STA $16ED,X). No
   caller reads a flag (STA, CPX or JMP follow). Reached from ROM in every mode through frm_raise_formants (M4.5).
   Declared: A, X, Y. */
static void par_clamp_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_clamp(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_scale_target $BDD3: Y in (A and X are written first; TYA / PHA, TXA / PHA). X and Y restored, A = the entry
   Y (the last PLA), out. par_play_start ($BAFE) loads A and Y next ($BB01 LDA, $BB0A TAY) and leaves X to its own
   exit; par_read_segment ($BD64) calls pros_microprosody, which loads A first and leaves X and Y, then reads Y
   ($BD6A CPY #0, $BD70 DEY) and X ($BD76 STA $17BF,X). No caller reads a flag (LDA or JSR follow). Declared: A, X,
   Y. */
static void pros_scale_target_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_scale_target(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_scale_voicing $BFC3: A in; A scaled out, X and Y untouched. par_read_segment ($BD96) reads A and X ($BD99
   STA $17BF,X, $BD9C INX) and Y at the next $BD83; par_read_dominance ($BF87) reads A ($BF8A STA par_fixed), Y
   ($BF90 LDA ($32),Y) and X ($BF9D TXA). No caller reads a flag (STA follows). Declared: A, X, Y. */
static void par_scale_voicing_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_scale_voicing(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_scale_duration $C29C: A and Y in (Y is saved in $78 and restored). A out; X untouched; Y as it came in.
   par_boundary_cur_dom reads A and X at $BF06 (STA $171D,X), Y at $BF0C (LDA ($32),Y) and A at $BF11 (STA
   par_trans_dur); par_boundary_next_dom reads A at $BF4B, Y at $BF51, A and X at $BF56. No caller reads a flag
   (STA follows). Declared: A, X, Y. */
static void par_scale_duration_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_scale_duration(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace

```c
THUNK(tn_convert_clause)
THUNK(cmd_hex_value)
```

with

```c
THUNK(tn_convert_clause)
THUNK(pros_microprosody)
THUNK(par_next_row)
THUNK(par_next_param)
THUNK(par_clamp)
THUNK(pros_scale_target)
THUNK(par_scale_voicing)
THUNK(par_scale_duration)
THUNK(cmd_hex_value)
```

- Replace

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
```

with

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
    { "par_next_row",       0xBD07, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_next_row },
    { "par_next_param",     0xBD14, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_next_param },
    { "par_clamp",          0xBD24, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_clamp },
    { "pros_scale_target",  0xBDD3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_scale_target },
    { "par_scale_voicing",  0xBFC3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_voicing },
    { "par_scale_duration", 0xC29C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_duration },
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
```

The table stays in address order (35 rows). The new file names clash with no existing basename: `ls src/firmware tests | grep -E "^(par_|pros_play|test_fw_par)"` shows only this task's files.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_BD07_par_next_row.txt`: `LDA par_row_ptr / CLC / ADC par_row_len / STA par_row_ptr` → `c = 0`, `r->a = fw_adc(…)`, the store; `BCC $BD13 / INC par_row_ptr+1` → `if (c) fw_inc8`; `RTS` with A = the low byte. No X or Y.
- `rom_BD14_par_next_param.txt`: `LDA / CLC / ADC par_param_stride / STA` → the first `fw_adc` with `c = 0`; `LDA par_row_ptr+1 / ADC par_param_stride+1 / STA` → the second `fw_adc` with the same `c`, into `r->a`.
- `rom_BD24_par_clamp.txt`: `CMP #1 / BPL $BD2B / LDA #1 / RTS` → `(uint8_t)(a - 1) & 0x80` → `PAR_CLAMP_MIN`; `CMP #$40 / BPL $BD30 / RTS` and `LDA #$3F / RTS` → `!((uint8_t)(a - 0x40) & 0x80)` → `PAR_CLAMP_MAX`, else A.
- `rom_BFC3_par_scale_voicing.txt`: $BFC3–$BFC9 → `v == 8 || v == 1`; `PHA / LDA cmd_voicing` and the `CMP #n / BNE` chain → the `switch` (`default` = $C037 `PLA / RTS`); in each case the `PLA`, the `BEQ $BFD1` (cases 1–3 only) → `v == 0 ? PAR_VOICING_FLOOR`; the `LSR` counts and `STA $76` → the shifts and `FW_TMP_76` stores in each helper (@B2 `SEC`: `c = 1`; @B4: `STA $76` twice, the `PHA`/`PLA` pair as `v >> 3` then `v >> 4`).
- `rom_BDD3_pros_scale_target.txt`: $BDD7–$BDF8 → `split_deviation` (`SEC / SBC #$80 / BPL / EOR #$FF / CLC / ADC #1`; `STY $1813`, `STY $1812` with Y = 0; `TAY` → the returned d; `LSR A / ROR $1813` twice; `STA $1814`); $BDF9–$BE06 → `limit_r` (`AND #7`, `CMP cmd_speed_limit / BCC`, `LDA cmd_speed_limit / AND #7`, the Z into `*zero`); `BEQ $BE52` → `if (zero)` with `LDA pros_pitch_centre`; $BE08–$BE30 → `scale_deviation` (`CMP #4 / BPL / LDY #0` → `y`; `AND #3 / BEQ $BE58` → `k == 0` → `TYA`; `TAX / STY $1811`; the loop body `LDA $1813 / CLC / ADC $1812 / STA $1812 / LDA $1811 / ADC $1814 / STA $1811`; `ASL $1812 / ADC #0`); $BE31–$BE39, $BE4D, $BE5C–$BE67 → `around_centre`; $BE3F–$BE45 → `store_target`; `PLA / TAX / PLA / TAY` → `r->a = r->y` (X and Y not changed).
- `rom_B983_pros_microprosody.txt`: `LDA par_next_phoneme` → `r->a`; the six `CMP / BEQ` → the `case` labels ($15, $18, $20, $41 → $B99F; $1F, $0D → $B9A8); $B99E `RTS` → `default`; $B99F–$B9A7 → `micro_raise` (`CLC / ADC #5 / BCC $B9B1`), $B9A8–$B9B0 → `micro_lower` (`SEC / SBC #5 / BCS $B9B1`), $B9B1 `STA pros_play_target`.
- `rom_C29C_par_scale_duration.txt`: `CMP #0 / BEQ $C2B3` → `r->a == 0`; `STY $78` → `fw_wr8(FW_TMP_78, r->y)`; `LDY cmd_speed / CPY #5 / BCC $C2B1` → `>= PAR_FAST_SPEED`; `CMP #$1F / BCC / LDA #$1F` → `i`; `TAY / LDA ($3A),Y` → `fw_ind_y(f, CMD_SPEED_MAP, i)`; `LDY $78` → `r->y`.
- For `fw.h`: the addresses in each comment against `rom_BDD3_pros_scale_target.txt` ($1811–$1814), `rom_C29C_par_scale_duration.txt` ($78), `rom_BFC3_par_scale_voicing.txt` ($76), `rom_B930_frm_clamp_mark_space.txt` and `rom_B95D_frm_raise_formants.txt` ($75). `grep -l "181[1-4]" build/ghidra740/dump/rom_*.txt` prints only `rom_BDD3_pros_scale_target.txt`.
- For the thunk comments: every caller site named in them, in the caller dumps listed at the top of this task.

Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xB983,0xBD07,0xBD14,0xBD24,0xBDD3,0xBFC3,0xC29C) for op in (0x20,0x4C)]"
```

Expected:

```
0xb983 0x20 ['0xbd67']
0xb983 0x4c []
0xbd07 0x20 ['0xb3be', '0xb3c7', '0xbf09', '0xbf14', '0xbf4e', '0xbf59', '0xbf7b', '0xbf8d', '0xbfbf']
0xbd07 0x4c []
0xbd14 0x20 ['0xb431', '0xbd9d', '0xbeac']
0xbd14 0x4c []
0xbd24 0x20 ['0xb970', '0xb97d', '0xbd8f', '0xbf75', '0xc0bf']
0xbd24 0x4c []
0xbdd3 0x20 ['0xbafe', '0xbd64']
0xbdd3 0x4c []
0xbfc3 0x20 ['0xbd96', '0xbf87']
0xbfc3 0x4c []
0xc29c 0x20 ['0xbf03', '0xbf0e', '0xbf48', '0xbf53']
0xc29c 0x4c []
```

and for relative branches into the seven entries (every byte position, 2-byte branches, `BBS`/`BBC` on A, and 3-byte `BBS`/`BBC` on zero page):

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xB983,0xBD07,0xBD14,0xBD24,0xBDD3,0xBFC3,0xC29C):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected:

```
0xb983 []
0xbd07 []
0xbd14 []
0xbd24 ['0xbd65']
0xbdd3 []
0xbfc3 []
0xc29c []
```

`0xbd65` is not a branch: it is the operand byte $D3 of `JSR $BDD3` at $BD64 (`20 D3 BD`). No other entry is reached by a branch.

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_par"`, then the full suite `cmd //c ".\build.bat test"` (it runs `diff_lts` and `golden_hybrid` and prints their lines below; no separate filtered runs)
Expected:
- `suite_fw_par                 ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 35 lines `<name> N calls checked`, each with N > 0; the seven new ones between `tn_convert_clause` and `cmd_hex_value` (the proof run's numbers, with Task 2's corpus):

```
  tn_convert_clause    224 calls checked
  pros_microprosody    3391 calls checked
  par_next_row         40696 calls checked
  par_next_param       30676 calls checked
  par_clamp            39664 calls checked
  pros_scale_target    3564 calls checked
  par_scale_voicing    5689 calls checked
  par_scale_duration   25782 calls checked
  cmd_hex_value        144 calls checked
```

  Every other count is unchanged from Task 2 (the observers are on the ROM machine, so the new hooks do not move them). Then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged, and 0 mismatches.
- `golden_hybrid`: `45 corpus files x 36 modes (all, then each routine alone)`, all identical.
- The full run: `32 suite(s), 0 failure(s), 0 skip(s)`, with no warnings, the firmware syntax check passing, and `firmware_data` at its Task 3 count (`47692 converted bytes compared with the ROM, 0 differ`; this task does not change it).

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/par_addr.h src/firmware/par_segment.h src/firmware/par_segment.c src/firmware/pros_play.h src/firmware/pros_play.c src/firmware/par_boundary.h src/firmware/par_boundary.c src/firmware/fw.h src/engine/thunks.c tests/test_fw_par.c tests/suites.h
git commit -m "The playback leaves in C (par_next_row, par_next_param, par_clamp, par_scale_voicing, pros_scale_target, pros_microprosody, par_scale_duration), with hooks; par_addr.h, pros_scale_target's RAM names in fw.h, tests/test_fw_par.c (M4.4a 2.2, 2.3, 2.6, 4.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The boundary pass in C: `par_store_boundary` $BEB5, `par_read_dominance` $BF6C, `par_boundary_value` $C039, `par_boundary_cur_dom` $BEE2, `par_boundary_next_dom` $BF27, `par_boundary_jump` $BE94 and `par_boundary_pass` $BE6A, with their hooks (spec §2.2, §2.3, §2.7, §4.4)

**ROM facts checked** (`rom_BEB5_par_store_boundary.txt`, `rom_BF6C_par_read_dominance.txt`, `rom_C039_par_boundary_value.txt`, `rom_BEE2_par_boundary_cur_dom.txt`, `rom_BF27_par_boundary_next_dom.txt`, `rom_BE94_par_boundary_jump.txt`, `rom_BE6A_par_boundary_pass.txt`, each instruction by instruction; the leaves `rom_BD07`, `rom_BD14`, `rom_BD24`, `rom_BFC3`, `rom_C29C`; the callers `rom_BCF3_par_next_segment.txt`, `rom_C0D2_par_plan_tracks.txt`, `rom_B877_par_play.txt` ($B8B1–$B914), `rom_C0FF_par_step_tracks.txt`; `data.txt` for $B7A7/$B7B1; the ROM image for the table bytes):
- **Sizes and counts agree with spec §2.2:** 42/20, 33/14, 45/19, 69/28, 69/28, 87/43, 153/70 bytes/instructions (the instruction lines of each dump counted).
- **The running X can only be 0 in the ROM, and a non-zero X runs past `par_target_bias`.** `par_boundary_value` reads `LDA $B7A7,X` at $C04D on every path (the set-up), and in a dominance pass X goes from the entry X to X + 9. The ten bytes $B7A7–$B7B0 are the table; $B7B1 starts `unused_B7B1` (198 bytes, a 0 there, not converted). So a dominance pass entered with X ≠ 0 reads past the table in the ROM and faults in the C at $B7B1 (`fw_rom8`); `test_dominance_running_x` pins that (the stores at X + 1 for p = 0…8, then the fault at `PAR_TARGET_BIAS + 10`). The jump path reads no bias, so `test_jump_running_x` runs a whole pass with X entering at 3. In the ROM the X always comes from `par_read_segment`'s `LDX #0` ($BDD0), and `diff_lts`/`golden_hybrid` never saw a fault.
- **`par_store_boundary`'s second cut after the first changes nothing.** The first cut runs only when `par_start_dur[p + 1]` is 0; the second then compares `par_frames_left < 0`, never true. So the second cut acts only when `par_trans_dur` is 0 on entry and `par_start_dur[p + 1]` is not (the jump path, and a dominant duration of 0). Both compares are `BCS` (unsigned; equal keeps the value). Exit A = `par_trans_dur` (the `LDA` at $BEDA), Y untouched.
- **Rows 3 and 4 (confirms the controller's reading for Task 3's `ext_dur, int_dur`).** `par_read_dominance` leaves `par_row_ptr` on row 3 ($BFBF; the annotation calls it the external-duration row). `cur_dom` stores row 3 in `par_next_start_dur[p + 1]` ($BF06, `STA $171D,X`) and row 4 in `par_trans_dur` ($BF11); `next_dom` stores row 3 in `par_trans_dur` ($BF4B) and row 4 in `par_next_start_dur[p + 1]` ($BF56). Spec §3 lists the five values as {…, internal duration, external duration}, the other order. The C names the rows by position in its comments ("row 3: the external duration"), so it does not depend on Task 3's field names.
- **The weighted @u offset** ($BF9D–$BFBE): `LDX par_ofs_weight` / `BEQ` skips the loop for weight 0 (not 256 steps); X counts the adds, so a proportion of 9 (weight $FF) makes 255 adds. `CLC` before every `ADC`, so the sum is 8 bits and the `PHP`/`PLP` around `LSR` ×3 carries the carry of the **last** add only (an earlier carry is lost: `test_read_dominance` has $90 × 3, where only the second add carries, giving +$16). The `PLA`/`TAX` restores X and leaves A = X; `par_next_row` then sets A.
- **`par_read_dominance`'s P10 test is `CPX #9` on its own entry X**, which the dominance loop sets to `par_param_index` (`LDX` at $BEF5/$BF3A), not the running X.
- **`par_boundary_value`** (all as spec §2.7): `_half` = `(x << 1 & $FF) >> 2` (bit 7 lost); `par_bias_sum` and `par_target_sum` are cleared on every path, `par_prop_low` is written only on the halves path, where the bias's sum stays in A (`par_bias_sum` keeps 0 or x2); the halves loops have one `CLC` each, so the carry chains from add to add; the `ROR` at $C09B takes the carry of `ADC par_target_sum` (a 9-bit sum, up to 219 + 91 with real data), the `LSR` puts bit 0 in C and `ADC #0` rounds; proportion 8 and proportion & 3 = 0 (4, 12, …) are 8-bit sums; proportion 0 is `par_clamp(par_fixed)` with no 0/$40 test; the `CMP #$40` / `BCC` is unsigned. Y: `TAY` then two `DEY` loops leave Y = 0 on the halves path, any other path leaves Y as it came. Exit A = `par_trans_dur`, X + 1 (from `par_store_boundary`).
- **Exit registers.** `par_boundary_jump`: A = `par_row_ptr`'s high byte (`par_next_param`'s; `DEC par_param_index` does not touch A), X + 10, Y = `par_next_phoneme`. `cur_dom`/`next_dom`: A = 10 (`LDA par_param_index` / `CMP #$0A`), X + 10, Y as the last `par_boundary_value` left it (the dominant phoneme, or 0 on the halves path). `par_boundary_pass`: A = `par_next_rank` going into `next_dom`/`cur_dom` and `par_cur_rank` going into the jump; none of the three reads A (`AND #0`, `LDY #$0A`/`LDA #0`), and the pass returns what its callee leaves.
- **What the callers read after each call** (the thunk comments cite these; no caller reads a flag, V is never tested):
  - `par_store_boundary`: from the jump ($BEA9), `par_next_param` (A written, X and Y kept), then Y at $BEA7 and X at the next $BEB5; from `par_boundary_value` ($C0AE), `RTS` at $C0B1, then X at the next parameter's `TXA` ($BEF3/$BF38).
  - `par_read_dominance`: Y at $BF01/$BF46 (`LDA (par_row_ptr),Y`), X at $BF06/$BF56 (`STA $171D,X`) after `par_scale_duration`, which keeps X and Y; A overwritten.
  - `par_boundary_value`: A written next (`INC`/`LDA par_param_index`), X at the next `TXA`; Y written by `par_read_dominance`'s `LDY` before being read; after the tenth, the pass's `RTS`.
  - `par_boundary_jump`, `cur_dom`, `next_dom`: the pass returns at once ($BE82, $BE93, $BE8F).
  - `par_boundary_pass`: `par_next_segment` calls `par_plan_tracks` ($BD03), which writes Y and A first (`LDY #$0A`, `LDA #0`) and never reads X, then returns to `par_play` $B903 (`LDA par_frames_left`), whose paths write Y ($B8B1 `LDY pros_seg_count`) or X (`par_step_tracks`' `LDX #$0A` at $C0FF, after `isr_par_ready`, which preserves them) first.

  No reader of A, X or Y was found for the four that only return through the pass; they are defined, so all seven declare A, X and Y (spec §2.3's default).
- **No ROM call, no flag reader, nothing reached in "all" mode.** None of the seven calls `isr_par_ready` or any ROM routine: every callee is C (Task 4's leaves and each other). Each is reached from ROM only while its caller is ROM, i.e. in its own "alone" mode of `golden_hybrid`.
- **Dead `CLC`s** at $BE72, $BE7A, $BE86, $BEB8, $BECE, $C0A7 and the `AND #0` at $BEEE/$BF33/$BF9F have no C beyond `r->a = 0`. The `PHA`s of X ($BEF4, $BF39, $BF9E) and the `PHP` ($BFAD) are dead stack.
- **Callers by scan** (Step 4): `JSR $BE6A` only at $BD00; `JSR $BE94` only at $BE7F; `JSR $BEB5` at $BEA9 and $C0AE; `JSR $BEE2` at $BE90; `JSR $BF27` at $BE8C; `JSR $BF6C` at $BEFE and $BF43; `JSR $C039` at $BF19 and $BF5E; no `JMP` to any of them and no relative branch into any of them.
- **Proof run.** The consolidated plan was executed literally in a scratch worktree from main 5f28310, Tasks 1–4 applied as written first; it printed the lines of Steps 2 and 5. The ROM oracle and the mutation run below were made while drafting, on stand-ins for Tasks 1–4 written from the dumps (Task 3 as the ROM ranges copied into the image):
  - `fw_par`: ok. **ROM oracle** (scratch only, not part of the plan): every call the tests make to `par_store_boundary`, `par_read_dominance`, `par_boundary_value`, `par_boundary_cur_dom` and `par_boundary_pass` (34 calls) was also run through the ROM routine in the emulator from the same RAM (interrupts held); RAM $0000–$00D9 and $0200–$3FFF and the exit A, X, Y were identical to the C in all 34, including the latent paths (proportion 9, the carry chained through a halves loop, the 8-bit proportion-8 sum, the jump with X entering at 3). Only the faulting `test_dominance_running_x` was left out (the ROM reads the 0 at $B7B1 where the C faults).
  - Mutation run: the tests fail for each of: the `ROR` without the 9th bit; the weighted sign from any carry instead of the last; a `CLC` before every halves add; no duration swap in `next_dom`; signed compares in the cuts; the store index = `par_param_index` (X not restored); the second cut without its `par_trans_dur` = 0 test; a clamp on the jump's raw targets; proportion 0 through the limit; a weight of 0 for a proportion above 8; `par_target_bias` indexed by `par_param_index`.
  - `diff_lts`: 42 lines, every N > 0, 0 mismatches (the lines in Step 5). `golden_hybrid`: `45 corpus files x 43 modes (all, then each routine alone)`, all identical (4 min 3 s alone). The full run: `32 suite(s), 0 failure(s), 0 skip(s)`, `47692 converted bytes compared with the ROM, 0 differ`, `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`, no warnings.

**Where the code goes.** All seven are public (each has its own hook, and each is called C to C by the one above it). They are appended to `src/firmware/par_boundary.c` after Task 4's `par_scale_duration`, as one function each plus `static` helpers, one per ROM block:

| Helper | ROM |
|---|---|
| `cut_trans_dur` | $BEB9–$BEC8, the first cut (no start transition: `par_trans_dur` to `par_frames_left`) |
| `cut_start_dur` | $BEC9–$BED9, the second cut (no end transition: `par_start_dur[p + 1]` to `par_frames_left`) |
| `dominated_target` | $BF6C–$BF7B, `par_clamp(target + par_ofs)` and the next row |
| `fixed_contribution` | $BF7E–$BF8D, the fixed contribution (P10 through `par_scale_voicing`) and the next row |
| `weighted_offset` | $BF9D–$BFBE, the weighted @u offset into `par_fixed`, X saved |
| `halves_setup` | $C039–$C058, the sums cleared, x2 and half of the target and of `par_target_bias[X]` |
| `halves` | $C066–$C09D, the halves path (proportion & 3 not 0): chained adds, the 9-bit `ROR`, `LSR`, `ADC #0` |
| `sum_whole` | $C0B2–$C0B9, proportion 8 |
| `sum_half` | $C0C5–$C0CF, proportion & 3 = 0 |
| `limit` | $C09F–$C0AD, `+ par_fixed`, 0 and ≥ $40 to 1 |
| `store_duration` | `STA $171D,X` / `STA par_trans_dur` ($BF06/$BF11, $BF4B/$BF56) |
| `dominance_loop` | $BEEE–$BF26 (cur_dom) and $BF33–$BF6B (next_dom): the two loops are byte for byte the same apart from which of the two duration stores comes first, so they share this helper with a `next_dom` flag |
| `jump_setup` | $BE94–$BEA4, the cleared durations and `par_param_index` = 10 |

The public functions hold the rest: `par_store_boundary` ($BEB5, $BEDA–$BEE1), `par_read_dominance` ($BF90–$BF9A, $BFBF), `par_boundary_value` (the dispatch $C059–$C064, $C0BC–$C0C2, $C0AE), the two entries $BEE2–$BEED and $BF27–$BF32, the jump loop $BEA7–$BEB2 and the pass $BE6A–$BE93.

**The running X.** The store index of `par_store_boundary` and `par_boundary_value` (`par_boundary`, `par_start_dur`, `par_end_dur`, `par_target_bias`, each + X) is `r->x` as it comes into the pass, incremented once per parameter by `par_store_boundary`'s `INX`. The dominance loops save it around their body (`TXA`/`PHA` … `PLA`/`TAX`) and use `par_param_index` (`LDX par_param_index`) for `cmd_formant_ofs`, `par_next_start_dur` and the P10 test. In the C, `r` simply flows: no routine sets `r->x` to 0, and the thunks take the machine's X.

**Files:**
- Modify: `src/firmware/par_boundary.h` (seven declarations), `src/firmware/par_boundary.c` (three includes, the seven routines), `src/engine/thunks.c` (seven thunks, `THUNK` lines and rows), `tests/test_fw_par.c` (the boundary-pass tests)

**Interfaces:**
- Consumes:
  - Task 1: `src/firmware/fw_ops.h` (`fw_adc`, `fw_inc8`); `MACHINE_MAX_HOOKS` 256 and `DIFF_MAX` 48.
  - Task 2: the corpus (44 names); the counts below assume it.
  - Task 3: in `src/firmware/data/fw_data.h`, `PAR_TARGET_BIAS` (0xB7A7), `PAR_VOICE_A`, `PAR_VOICE_B` and `PAR_PHONEMES` (86); the converted `par_target_bias` and both voice tables in the image (`test_jump_end_marker` reads index $FF of both; `firmware_data` at 47692).
  - Task 4:
    - `src/firmware/par_addr.h`: `ROM_PAR_BOUNDARY_PASS`, `ROM_PAR_BOUNDARY_JUMP`, `ROM_PAR_STORE_BOUNDARY`, `ROM_PAR_BOUNDARY_CUR_DOM`, `ROM_PAR_BOUNDARY_NEXT_DOM`, `ROM_PAR_READ_DOMINANCE`, `ROM_PAR_BOUNDARY_VALUE`.
    - `src/firmware/par_segment.h`: `void par_next_row(fw *, fw_regs *)`, `par_next_param`, `par_clamp`, `par_scale_voicing` (same signature).
    - `src/firmware/par_boundary.h` with `void par_scale_duration(fw *f, fw_regs *r);` and its last line `#endif`; `src/firmware/par_boundary.c`, whose first line is `#include "par_boundary.h"`, with `par_scale_duration`.
    - In `src/engine/thunks.c`: `#include "firmware/par_boundary.h"`; the lines `THUNK(pros_scale_target)` and `THUNK(par_scale_voicing)`; the rows of `pros_scale_target` (0xBDD3) and `par_scale_voicing` (0xBFC3) in the table's column style (the exact lines are quoted in Step 3).
    - `tests/test_fw_par.c`: the harness (`g_ram`, `g_f`, `g_ncalls`, `setup()`, `check_row()`), with `engine/thunks.h`, `firmware/par_addr.h`, `firmware/par_boundary.h` and `firmware/data/fw_data.h` included, and `suite_fw_par`.
  - Existing: `fw_ind_y`, `fw_rom8`, `fw_rd8`/`fw_wr8`, `fw_rd16`/`fw_wr16` (`fw.h`); `FW_TMP_76`, `FW_TMP_78`; the `fw_ram.h` names `PAR_BOUNDARY`, `PAR_NEXT_START_DUR`, `PAR_START_DUR`, `PAR_END_DUR`, `PAR_FRAMES_LEFT`, `PAR_NEXT_PHONEME`, `PAR_CUR_PHONEME`, `PAR_NEXT_RANK`, `PAR_CUR_RANK`, `PAR_TRANS_DUR`, `PAR_TARGET_PHONEME`, `PAR_DOMINANT_PHONEME`, `PAR_FIXED`, `PAR_PROP`, `PAR_DOMINATED_TARGET`, `PAR_BIAS_SUM` … `PAR_TARGET_SUM`, `PAR_ROW_LEN`, `PAR_PARAM_INDEX`, `PAR_PARAM_STRIDE`, `PAR_RANK_PTR`, `PAR_ROW_PTR`, `CMD_FORMANT_OFS`, `PAR_OFS`, `PAR_OFS_WEIGHT`, `CMD_VOICING`.
- Produces (Task 6's C `par_next_segment` calls `par_boundary_pass` with the `r` the C `par_read_segment` leaves, X = 0):

```c
/* par_boundary.h, after par_scale_duration */
void par_store_boundary(fw *f, fw_regs *r);     /* $BEB5: A, X in; A = par_trans_dur, X + 1, Y kept */
void par_read_dominance(fw *f, fw_regs *r);     /* $BF6C: X = p in; A = par_row_ptr low, X = p, Y = dominant */
void par_boundary_value(fw *f, fw_regs *r);     /* $C039: X (and Y) in; A = par_trans_dur, X + 1, Y = 0 or kept */
void par_boundary_cur_dom(fw *f, fw_regs *r);   /* $BEE2: X in; A = 10, X + 10, Y as par_boundary_value leaves it */
void par_boundary_next_dom(fw *f, fw_regs *r);  /* $BF27: the same */
void par_boundary_jump(fw *f, fw_regs *r);      /* $BE94: X in; A = par_row_ptr high, X + 10, Y = par_next_phoneme */
void par_boundary_pass(fw *f, fw_regs *r);      /* $BE6A: X in; out as the callee leaves them */
```

  - The replacements `par_boundary_pass` 0xBE6A, `par_boundary_jump` 0xBE94, `par_store_boundary` 0xBEB5, `par_boundary_cur_dom` 0xBEE2, `par_boundary_next_dom` 0xBF27, `par_read_dominance` 0xBF6C and `par_boundary_value` 0xC039, all `FW_OUT_A | FW_OUT_X | FW_OUT_Y`: **42 replacements, 43 `golden_hybrid` modes, 42 `diff_lts` lines**.
  - In `tests/test_fw_par.c`: `pt_setup()`, `pt_set(p, row, phoneme, v)` and `ROW_TARGET` … `ROW_INT_DUR` (a voice table in RAM; Task 6 may reuse them), and `test_jump_end_marker();` as the last line of `suite_fw_par`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_par.c`, replace

```c
void suite_fw_par(void)
{
```

with

```c
/* ---- the boundary pass (M4.4a Task 5) ---- */

/* A voice table in RAM (fw_ind_y reads RAM below $4000) with N = PT_N phonemes per row: parameter p's five rows
   (target, fixed contribution, proportion, external duration, internal duration) start at PT_BASE + 5 * PT_N * p, as
   they do from par_params_ptr; the ranks at PT_RANKS (par_rank_ptr). */
#define PT_BASE   0x3000
#define PT_N      8
#define PT_RANKS  0x2F00
enum { ROW_TARGET, ROW_FIXED, ROW_PROP, ROW_EXT_DUR, ROW_INT_DUR };

static void pt_set(unsigned p, unsigned row, uint8_t phoneme, uint8_t v)
{
    g_ram[PT_BASE + 5 * PT_N * p + PT_N * row + phoneme] = v;
}

/* Zeroed RAM; par_row_ptr on P1's target row, par_row_len N, par_param_stride 5N, par_rank_ptr on the ranks. */
static void pt_setup(void)
{
    setup();
    g_ram[PAR_ROW_LEN] = PT_N;
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 5 * PT_N);
    fw_wr16(&g_f, PAR_ROW_PTR, PT_BASE);
    fw_wr16(&g_f, PAR_RANK_PTR, PT_RANKS);
}

static void test_boundary_rows(void)
{
    check_row("par_boundary_pass", ROM_PAR_BOUNDARY_PASS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_jump", ROM_PAR_BOUNDARY_JUMP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_store_boundary", ROM_PAR_STORE_BOUNDARY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_cur_dom", ROM_PAR_BOUNDARY_CUR_DOM, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_next_dom", ROM_PAR_BOUNDARY_NEXT_DOM, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_read_dominance", ROM_PAR_READ_DOMINANCE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_boundary_value", ROM_PAR_BOUNDARY_VALUE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* $BEB5: par_store_boundary's two cuts ($BEB9 and $BEC9), both unsigned (BCS), the second only while par_trans_dur
   is 0; par_end_dur = par_trans_dur; A = par_trans_dur, X + 1, Y kept */
static void test_store_boundary(void)
{
    fw_regs r;
    setup();
    g_ram[PAR_FRAMES_LEFT] = 5;                     /* no start transition: par_trans_dur 9 is cut to 5 */
    g_ram[PAR_TRANS_DUR] = 9;
    r = (fw_regs){ 0x2A, 2, 0x77, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 3], 0x2A);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 5);
    CHECK_EQ(g_ram[PAR_END_DUR + 3], 5);
    CHECK_EQ(g_ram[PAR_START_DUR + 3], 0);          /* the second cut: par_trans_dur is not 0 */
    CHECK_EQ(r.a, 5);
    CHECK_EQ(r.x, 3);
    CHECK_EQ(r.y, 0x77);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 0);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 0);

    g_ram[PAR_TRANS_DUR] = 0xF0;                    /* unsigned: 5 < $F0 cuts (a signed compare would not) */
    r = (fw_regs){ 0x11, 0, 0, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 5);
    CHECK_EQ(g_ram[PAR_END_DUR + 1], 5);
    CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 0x11);

    g_ram[PAR_TRANS_DUR] = 6;                       /* par_frames_left 6, equal: no cut (BCS) */
    g_ram[PAR_FRAMES_LEFT] = 6;
    r = (fw_regs){ 0x12, 0, 0, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 6);

    g_ram[PAR_START_DUR + 5] = 12;                  /* a start transition and no end one: the start one is cut */
    g_ram[PAR_FRAMES_LEFT] = 7;
    g_ram[PAR_TRANS_DUR] = 0;
    r = (fw_regs){ 0x13, 4, 0x01, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_START_DUR + 5], 7);
    CHECK_EQ(g_ram[PAR_END_DUR + 5], 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 5);

    g_ram[PAR_START_DUR + 2] = 12;                  /* both: neither is cut (the first needs par_start_dur 0, */
    g_ram[PAR_FRAMES_LEFT] = 3;                     /* the second par_trans_dur 0) */
    g_ram[PAR_TRANS_DUR] = 6;
    r = (fw_regs){ 0x14, 1, 0, 0 };
    par_store_boundary(&g_f, &r);
    CHECK_EQ(g_ram[PAR_START_DUR + 2], 12);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 6);
    CHECK_EQ(g_ram[PAR_END_DUR + 2], 6);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* One par_read_dominance at X = p, @B voicing, with par_target_phoneme 3 and par_dominant_phoneme 5: their rows of
   parameter p hold target, fixed and prop; par_row_ptr starts on p's target row. */
static void dominance_case(uint8_t p, uint8_t voicing, uint8_t ofs, uint8_t target, uint8_t fixed, uint8_t prop,
                           fw_regs *r)
{
    pt_setup();
    fw_wr16(&g_f, PAR_ROW_PTR, (uint16_t)(PT_BASE + 5 * PT_N * p));
    g_ram[CMD_VOICING] = voicing;
    g_ram[PAR_TARGET_PHONEME] = 3;
    g_ram[PAR_DOMINANT_PHONEME] = 5;
    g_ram[PAR_OFS] = ofs;
    pt_set(p, ROW_TARGET, 3, target);
    pt_set(p, ROW_FIXED, 5, fixed);
    pt_set(p, ROW_PROP, 5, prop);
    *r = (fw_regs){ 0xA5, p, 0xB6, 0 };
    par_read_dominance(&g_f, r);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* $BF6C: the dominated target clamped; the fixed contribution plus the weighted @u offset: 8 - prop adds of par_ofs
   in 8 bits, >> 3, negative only when the last add carried ($BFA6-$BFBA); the weight wraps for a proportion above 8
   (latent: the tables hold 0-8) */
static void test_read_dominance(void)
{
    fw_regs r;
    uint16_t row3 = PT_BASE + 5 * PT_N * 2 + 3 * PT_N;
    dominance_case(2, 8, 0xFE, 40, 20, 7, &r);      /* weight 1: 0 + $FE, no carry: $FE >> 3 = $1F, so -2 adds +31 */
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 38);      /* 40 + $FE, clamped (1..$3F: kept) */
    CHECK_EQ(g_ram[PAR_PROP], 7);
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 1);             /* (7 - 9) ^ $FF */
    CHECK_EQ(g_ram[PAR_FIXED], 20 + 0x1F);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), row3);     /* three rows on: the external durations */
    CHECK_EQ(r.a, row3 & 0xFF);                     /* par_next_row's low byte */
    CHECK_EQ(r.x, 2);
    CHECK_EQ(r.y, 5);                               /* the dominant phoneme, for its duration rows */
    CHECK_EQ(g_ram[FW_TMP_76], 0);                  /* no par_scale_voicing below P10 */

    dominance_case(2, 8, 0xFE, 40, 20, 6, &r);      /* weight 2: $FE + $FE carries: $FC >> 3 | $E0 = $FF (-1) */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 2);
    CHECK_EQ(g_ram[PAR_FIXED], 19);

    dominance_case(2, 8, 0x50, 40, 20, 4, &r);      /* weight 4: $50 x 4 = $140, the last add carries: +40 */
    CHECK_EQ(g_ram[PAR_FIXED], (uint8_t)(20 + 0xE8));   /* becomes ($40 >> 3) | $E0 = $E8 (-24) */
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 0x3F);    /* 40 + $50 = $78: par_clamp gives $3F */

    dominance_case(2, 8, 0x90, 40, 20, 5, &r);      /* weight 3: $90, $120 (carries), $B0 (does not): only the last */
    CHECK_EQ(g_ram[PAR_FIXED], 20 + 0x16);          /* counts, so $B0 >> 3 = $16 stays positive */

    dominance_case(2, 8, 0x50, 40, 20, 8, &r);      /* weight 0 (LDX; BEQ): par_fixed as read */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0);
    CHECK_EQ(g_ram[PAR_FIXED], 20);
    CHECK_EQ(r.x, 2);

    dominance_case(2, 8, 0xFF, 40, 20, 9, &r);      /* prop 9: weight (9 - 9) ^ $FF = 255 adds of $FF; the last is */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0xFF);          /* 2 + $FF, which carries: 1 >> 3 | $E0 = $E0 */
    CHECK_EQ(g_ram[PAR_FIXED], (uint8_t)(20 + 0xE0));
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 39);      /* 40 + $FF */

    dominance_case(9, 6, 0, 2, 46, 8, &r);          /* P10 (CPX #9): @B6 scales 46 to 46/2 + 46/8 = 28 ... */
    CHECK_EQ(g_ram[PAR_FIXED], 28);
    CHECK_EQ(g_ram[FW_TMP_76], 23);                 /* ... with its scratch */
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 2);       /* the target is not scaled */
    CHECK_EQ(r.x, 9);
    dominance_case(8, 6, 0, 2, 46, 8, &r);          /* P9: not scaled */
    CHECK_EQ(g_ram[PAR_FIXED], 46);
}

/* One par_boundary_value at X = x, Y = $66, with par_prop, par_dominated_target and par_fixed as given;
   par_start_dur[x + 1] = 1 and par_trans_dur = 7, so par_store_boundary cuts nothing and leaves A = 7. */
static void value_case(uint8_t x, uint8_t prop, uint8_t target, uint8_t fixed, fw_regs *r)
{
    setup();
    g_ram[PAR_PROP] = prop;
    g_ram[PAR_DOMINATED_TARGET] = target;
    g_ram[PAR_FIXED] = fixed;
    g_ram[PAR_START_DUR + 1 + x] = 1;
    g_ram[PAR_TRANS_DUR] = 7;
    g_ram[PAR_FRAMES_LEFT] = 200;
    *r = (fw_regs){ 0xA5, x, 0x66, 0 };
    par_boundary_value(&g_f, r);
    CHECK_EQ(r->a, 7);
    CHECK_EQ(r->x, (uint8_t)(x + 1));
    CHECK_EQ(g_ram[PAR_END_DUR + 1 + x], 7);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* $C039: par_target_bias is 18, 4, 10, 26, then 0 for P5-P10 (ROM $B7A7). Each path: prop 0, 8, a multiple of 4,
   and the halves (with the 9-bit sum the ROR at $C09B takes, and the carry chained through a halves loop); the
   limit (0 and $40 up become 1, unsigned); the halves state; Y = 0 on the halves path only. */
static void test_boundary_value(void)
{
    fw_regs r;
    value_case(1, 0, 33, 0x45, &r);                 /* prop 0: par_clamp($45) = $3F, no 0 / $40 test */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 0x3F);
    CHECK_EQ(r.y, 0x66);
    CHECK_EQ(g_ram[PAR_TARGET_X2], 66);             /* the set-up runs on every path: t = 33 */
    CHECK_EQ(g_ram[PAR_TARGET_HALF], 16);
    CHECK_EQ(g_ram[PAR_BIAS_X2], 8);                /* b = 4 */
    CHECK_EQ(g_ram[PAR_BIAS_HALF], 2);
    value_case(1, 0, 33, 0, &r);                    /* prop 0: par_clamp(0) = 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 1);

    value_case(3, 8, 33, 0xF8, &r);                 /* prop 8: 33 + 26 - 8 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 51);
    CHECK_EQ(r.y, 0x66);
    value_case(3, 8, 0xF0, 0, &r);                  /* latent: $F0 + 26 = $10A, the carry lost: 10 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 10);

    value_case(0, 4, 33, 3, &r);                    /* prop 4: 18 + 33 = 51, >> 1 rounded = 26, + 3 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 29);
    CHECK_EQ(r.y, 0x66);
    value_case(2, 12, 21, 0, &r);                   /* prop 12, & 3 = 0: the same path, 10 + 21 = 31 -> 16 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 3], 16);

    value_case(1, 3, 33, 5, &r);                    /* prop 3: 3 halves from 0: target 16 x 3 = 48, bias 2 x 3 = 6; */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 2], 19);          /* 54 >> 2 = 13, + the bit out (1) = 14, + 5 */
    CHECK_EQ(g_ram[PAR_TARGET_SUM], 48);
    CHECK_EQ(g_ram[PAR_BIAS_SUM], 0);               /* the bias's sum stays in A */
    CHECK_EQ(g_ram[PAR_PROP_LOW], 3);
    CHECK_EQ(r.y, 0);

    value_case(3, 7, 63, 0xF0, &r);                 /* prop 7: from x2: target 126 + 31 x 3 = 219, bias 52 + 13 x 3 = */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 4], 62);          /* 91; 310 is 9 bits: ROR gives $9B, LSR $4D + 1 = $4E; + $F0 */
    CHECK_EQ(g_ram[PAR_TARGET_SUM], 219);           /* = $13E -> $3E (with 8 bits only it would be 1) */
    CHECK_EQ(g_ram[PAR_BIAS_SUM], 52);              /* x2, not the sum */
    CHECK_EQ(g_ram[PAR_PROP_LOW], 3);
    CHECK_EQ(r.y, 0);

    value_case(4, 6, 0x7F, 0, &r);                  /* latent: prop 6 from x2 $FE: $FE + $3F carries into the */
    CHECK_EQ(g_ram[PAR_TARGET_SUM], 0x7D);          /* second add: $3D + $3F + 1 = $7D */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 0x1F);        /* bias 0: $7D >> 2 = $1F, the bit out 0 */

    value_case(4, 8, 16, 0xF0, &r);                 /* the limit: 16 + 0 - 16 = 0 becomes 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 1);
    value_case(4, 8, 20, 0xC0, &r);                 /* $D4: $40 or more, unsigned: 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 1);
    value_case(4, 8, 20, 43, &r);                   /* $3F is kept */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 0x3F);
    value_case(4, 8, 20, 44, &r);                   /* $40: 1 */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 5], 1);
}

/* Phonemes 2 and 6 in the RAM table: per parameter p, targets 30 + p and 40 + p, fixed contributions 10 + p and
   20 + p, proportion 0 (the boundary is par_clamp(par_fixed)), external durations 3 + p and 50 + p, internal 7 + p
   and 70 + p; ranks 5 and 9. cmd_formant_ofs[1] = 2 (weight 8: 16 >> 3 = +2 on P2's fixed contribution), @B5,
   speed 0 (par_scale_duration passes), par_frames_left 200 (no cut). */
static void pair_setup(uint8_t cur, uint8_t next)
{
    pt_setup();
    for (unsigned p = 0; p < 10; p++) {
        pt_set(p, ROW_TARGET, 2, (uint8_t)(30 + p));
        pt_set(p, ROW_TARGET, 6, (uint8_t)(40 + p));
        pt_set(p, ROW_FIXED, 2, (uint8_t)(10 + p));
        pt_set(p, ROW_FIXED, 6, (uint8_t)(20 + p));
        pt_set(p, ROW_EXT_DUR, 2, (uint8_t)(3 + p));
        pt_set(p, ROW_EXT_DUR, 6, (uint8_t)(50 + p));
        pt_set(p, ROW_INT_DUR, 2, (uint8_t)(7 + p));
        pt_set(p, ROW_INT_DUR, 6, (uint8_t)(70 + p));
    }
    g_ram[PT_RANKS + 2] = 5;
    g_ram[PT_RANKS + 6] = 9;
    g_ram[CMD_FORMANT_OFS + 1] = 2;
    g_ram[CMD_VOICING] = 5;
    g_ram[PAR_FRAMES_LEFT] = 200;
    g_ram[PAR_CUR_PHONEME] = cur;
    g_ram[PAR_CUR_RANK] = g_ram[PT_RANKS + cur];
    g_ram[PAR_NEXT_PHONEME] = next;
}

/* The pass picks the dominant phoneme by rank, and the two dominance routines take its durations the other way
   round: next_dom row 3 (external) into par_trans_dur and row 4 into par_next_start_dur, cur_dom the reverse */
static void test_pass_dominance(void)
{
    fw_regs r;
    pair_setup(2, 6);                               /* next rank 9 >= 5: par_boundary_next_dom, phoneme 6 dominates */
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 9);
    CHECK_EQ(g_ram[PAR_TARGET_PHONEME], 2);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1 + p], 70 + p);    /* row 4 */
        CHECK_EQ(g_ram[PAR_END_DUR + 1 + p], 50 + p);           /* row 3, through par_trans_dur */
        CHECK_EQ(g_ram[PAR_BOUNDARY + 1 + p], p == 1 ? 23 : p == 9 ? 29 / 2 : 20 + p);   /* P10 at @B5: 29 / 2 */
    }
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 59);
    CHECK_EQ(g_ram[PAR_DOMINATED_TARGET], 39);      /* the last parameter's: the target phoneme 2's 30 + 9 */
    CHECK_EQ(g_ram[PAR_FIXED], 14);
    CHECK_EQ(g_ram[PAR_OFS], 0);                    /* cmd_formant_ofs[9] */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 8);
    CHECK_EQ(g_ram[PAR_PARAM_INDEX], 10);
    CHECK_EQ(g_ram[FW_TMP_78], 6);                  /* par_scale_duration's saved Y */
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), PT_BASE + 50 * PT_N);   /* ten parameters of five rows on */
    CHECK_EQ(r.a, 10);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 6);                               /* prop 0 keeps Y */
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR], 0);         /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_BOUNDARY], 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);

    pair_setup(6, 2);                               /* next rank 5 < 9: par_boundary_cur_dom, phoneme 6 dominates */
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 5);
    CHECK_EQ(g_ram[PAR_TARGET_PHONEME], 2);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1 + p], 50 + p);    /* row 3 */
        CHECK_EQ(g_ram[PAR_END_DUR + 1 + p], 70 + p);           /* row 4, through par_trans_dur */
        CHECK_EQ(g_ram[PAR_BOUNDARY + 1 + p], p == 1 ? 23 : p == 9 ? 29 / 2 : 20 + p);
    }
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 79);
    CHECK_EQ(r.a, 10);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 6);
    CHECK(!g_f.fault);

    pair_setup(2, 2);                               /* equal ranks: par_boundary_next_dom (BCC not taken) */
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_END_DUR + 1], 3);            /* row 3 into par_trans_dur: next_dom */
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1], 7);

    pair_setup(2, 6);                               /* the next rank 29 or more, the current one below: no jump */
    g_ram[PT_RANKS + 6] = 31;
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1], 70);
    pair_setup(6, 2);                               /* the current rank 29 or more, the next one below: no jump */
    g_ram[PAR_CUR_RANK] = 31;
    r = (fw_regs){ 0xA5, 0, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    CHECK_EQ(g_ram[PAR_DOMINANT_PHONEME], 6);
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1], 50);
    CHECK(!g_f.fault);
}

/* Both ranks 29 or more: par_boundary_jump stores the next phoneme's raw targets (no @u offset, no clamp) at the
   running X, entering here at 3 (it is 0 in the ROM, but the pass does not set it): par_store_boundary indexes
   par_boundary, par_start_dur and par_end_dur at X + 1 while par_param_index counts 10 down to 1 */
static void test_jump_running_x(void)
{
    static const uint8_t raw[10] = { 0x90, 0, 63, 64, 5, 6, 7, 8, 9, 10 };
    fw_regs r;
    pt_setup();
    for (unsigned p = 0; p < 10; p++) {
        pt_set(p, ROW_TARGET, 4, raw[p]);
        g_ram[CMD_FORMANT_OFS + p] = 0x11;          /* not added on this path */
    }
    for (unsigned i = 0; i < 16; i++) {
        g_ram[PAR_NEXT_START_DUR + i] = 0x55;
        g_ram[PAR_START_DUR + i] = 6;
    }
    g_ram[PT_RANKS + 4] = 31;
    g_ram[PAR_CUR_RANK] = 29;
    g_ram[PAR_NEXT_PHONEME] = 4;
    g_ram[PAR_TRANS_DUR] = 9;
    g_ram[PAR_FRAMES_LEFT] = 4;
    r = (fw_regs){ 0xA5, 3, 0xB6, 0 };
    par_boundary_pass(&g_f, &r);
    for (unsigned i = 1; i <= 10; i++)
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + i], 0); /* par_next_start_dur[1..10], by Y, not X */
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 0], 0x55);
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 11], 0x55);
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 0);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_BOUNDARY + 4 + p], raw[p]);
        CHECK_EQ(g_ram[PAR_START_DUR + 4 + p], 4);  /* par_trans_dur 0: the second cut, 6 to par_frames_left */
        CHECK_EQ(g_ram[PAR_END_DUR + 4 + p], 0);
    }
    for (unsigned i = 1; i <= 3; i++) {
        CHECK_EQ(g_ram[PAR_BOUNDARY + i], 0);
        CHECK_EQ(g_ram[PAR_START_DUR + i], 6);
    }
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 31);
    CHECK_EQ(g_ram[PAR_PARAM_INDEX], 0);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), PT_BASE + 50 * PT_N);  /* ten strides on, not reset */
    CHECK_EQ(r.a, (PT_BASE + 50 * PT_N) >> 8);      /* par_next_param's high byte */
    CHECK_EQ(r.x, 13);
    CHECK_EQ(r.y, 4);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The running X through a dominance pass: par_param_index p indexes cmd_formant_ofs and par_next_start_dur, X = p + 1
   the stores and par_target_bias. At p = 9, X = 10 reads $B7B1, past par_target_bias (unused_B7B1, not converted):
   the C faults there; no ROM path enters the pass with X other than 0. */
static void test_dominance_running_x(void)
{
    fw_regs r;
    pair_setup(6, 2);
    r = (fw_regs){ 0xA5, 1, 0xB6, 0 };
    par_boundary_cur_dom(&g_f, &r);
    for (unsigned p = 0; p < 10; p++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + 1 + p], 50 + p);
        CHECK_EQ(g_ram[PAR_END_DUR + 2 + p], 70 + p);
    }
    for (unsigned p = 0; p < 9; p++)
        CHECK_EQ(g_ram[PAR_BOUNDARY + 2 + p], p == 1 ? 23 : 20 + p);   /* P2's offset lands in par_boundary[3] */
    CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 0);
    CHECK_EQ(r.x, 11);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, PAR_TARGET_BIAS + 10);
}

/* The end of a @T effect: par_next_phoneme $FF, read past the 86 phonemes of the converted voice tables: rank[$FF]
   is 32 and the pause (rank 31) is current, so par_boundary_jump stores the raw bytes at index $FF of P1..P10's
   target rows, 4, 4, 0, 4, 8, 8, 8, 8, 8, 8 in both voices (the 0 as it is) */
static void test_jump_end_marker(void)
{
    static const uint8_t raw[10] = { 4, 4, 0, 4, 8, 8, 8, 8, 8, 8 };
    static const uint16_t voice[2] = { PAR_VOICE_A, PAR_VOICE_B };
    for (unsigned v = 0; v < 2; v++) {
        fw_regs r = { 0, 0, 0, 0 };
        setup();
        g_ram[PAR_ROW_LEN] = PAR_PHONEMES;
        fw_wr16(&g_f, PAR_PARAM_STRIDE, 5 * PAR_PHONEMES);
        fw_wr16(&g_f, PAR_RANK_PTR, voice[v]);
        fw_wr16(&g_f, PAR_ROW_PTR, (uint16_t)(voice[v] + 2 * PAR_PHONEMES));   /* par_params_ptr */
        g_ram[PAR_NEXT_PHONEME] = 0xFF;
        g_ram[PAR_CUR_RANK] = 31;
        g_ram[PAR_FRAMES_LEFT] = 3;
        par_boundary_pass(&g_f, &r);
        CHECK_EQ(g_ram[PAR_NEXT_RANK], 32);
        for (unsigned p = 0; p < 10; p++) {
            CHECK_EQ(g_ram[PAR_BOUNDARY + 1 + p], raw[p]);
            CHECK_EQ(g_ram[PAR_END_DUR + 1 + p], 0);
        }
        CHECK_EQ(r.x, 10);
        CHECK_EQ(r.y, 0xFF);
        CHECK(!g_f.fault);
    }
}

void suite_fw_par(void)
{
```

Then, in `suite_fw_par`, replace its last two lines (as Task 4 leaves them)

```c
    test_microprosody();
}
```

with

```c
    test_microprosody();
    test_boundary_rows();
    test_store_boundary();
    test_read_dominance();
    test_boundary_value();
    test_pass_dominance();
    test_jump_running_x();
    test_dominance_running_x();
    test_jump_end_marker();
}
```

What the tests pin (spec §4.4, the part for this task):
- `test_boundary_rows`: the seven rows (address, A/X/Y).
- `test_store_boundary`: both cuts, unsigned (5 < $F0 cuts) and equal (no cut); the second cut only while `par_trans_dur` is 0; A = `par_trans_dur`, X + 1, Y kept.
- `test_read_dominance`: **the weighted @u sign from the last carry** (−2 once gives +31; $50 × 4 gives −24; $90 × 3, an earlier carry only, stays positive); **the proportion > 8 weight wrap** (prop 9: 255 adds of $FF give $E0); weight 0 (prop 8); the dominated target clamped; P10 through `par_scale_voicing` (and `FW_TMP_76`), P9 not; the exit A/X/Y and `par_row_ptr` on row 3.
- `test_boundary_value`: every path (prop 0 clamped with no limit, 8, 4 and 12, 3 and 7 and 6); **the `ROR` at $C09B with a 9-bit sum** (310: 62 instead of 1); the carry chained through a halves loop ($7D); the 8-bit carry loss of proportion 8; the limit (0, $3F, $40, $D4); the halves state; Y = 0 on the halves path only.
- `test_pass_dominance`: the dispatch by rank (higher next, lower next, equal, one rank from 29 up); **`cur_dom` vs `next_dom` duration swap** (rows 3/4 into `par_next_start_dur`/`par_trans_dur` and back); the @u offset by `par_param_index` (P2's +2); P10 at @B5; `FW_TMP_78`; the exit registers and `par_row_ptr` 50 rows on; entry 0 of the arrays not written.
- `test_jump_running_x`: **the running X entering the pass non-zero** (3: stores at 4…13, `par_next_start_dur[1..10]` by Y); **the jump path's raw targets** ($90, 0 and 64 stored unclamped, the @u offset not added); the second cut on every parameter; `par_row_ptr` ten strides on; exit A/X/Y.
- `test_dominance_running_x`: the running X through a dominance pass (X = p + 1 for the stores and the bias, p for `cmd_formant_ofs` and `par_next_start_dur`), up to the fault at $B7B1.
- `test_jump_end_marker`: the end of a @T effect on the converted voice tables: rank[$FF] = 32 and the raw targets at index $FF (4, 4, 0, 4, 8, 8, 8, 8, 8, 8) in both voices.

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_par"`
Expected: the build stops in `tests\test_fw_par.c` with `error C2220: the following warning is treated as an error` and `warning C4013: 'par_store_boundary' undefined; assuming extern returning int`, then the same warning for `par_read_dominance`, `par_boundary_value`, `par_boundary_pass` and `par_boundary_cur_dom` (the five the tests call), because `par_boundary.h` does not declare them yet (checked in the proof run).

- [ ] **Step 3: Implement**

`src/firmware/par_boundary.h`: replace its last line

```c
#endif
```

with

```c
/* The boundary pass ($BE6A-$C0D1, M4.4a spec 2.2), run once per segment by par_next_segment after
   par_read_segment. The running X: the store index of par_store_boundary and par_boundary_value (par_boundary,
   par_start_dur, par_end_dur, par_target_bias, each + X) is the X the pass is entered with, incremented once per
   parameter. par_read_segment leaves it 0 ($BDD0), so in the ROM it always runs 0..9 next to par_param_index; the
   C takes it from r->x and never assumes 0. */

/* $BEB5: X = the store index p: par_boundary[p + 1] = A; when par_start_dur[p + 1] is 0, par_trans_dur is cut to
   par_frames_left if that is smaller; then, when par_trans_dur is 0, par_start_dur[p + 1] is cut to par_frames_left
   if that is smaller (both compares unsigned); par_end_dur[p + 1] = par_trans_dur.
   r in: A, X. r out: A = par_trans_dur, X + 1 (8 bits), Y untouched. */
void par_store_boundary(fw *f, fw_regs *r);
/* $BF6C: X = the parameter p (CPX #9: P10 is scaled by @B), par_ofs = its @u offset, par_row_ptr on the parameter's
   target row: par_dominated_target = par_clamp(target[par_target_phoneme] + par_ofs); par_fixed = the fixed
   contribution of par_dominant_phoneme (P10: par_scale_voicing), plus the weighted offset when
   par_ofs_weight = 8 - proportion (mod 256) is not 0: par_ofs added that many times in 8 bits, >> 3, with bits 7-5
   set when the last add carried; par_prop = the proportion. Three par_next_row calls leave par_row_ptr on the
   external-duration row (row 3). r in: X. r out: A = par_row_ptr's low byte, X = p, Y = par_dominant_phoneme. */
void par_read_dominance(fw *f, fw_regs *r);
/* $C039: X = the store index: the boundary value from par_fixed, par_prop (eighths of the way), par_dominated_target
   and par_target_bias[X]: proportion 0: par_clamp(par_fixed); 8: target + bias (8 bits); a multiple of 4: half of
   that 8-bit sum, rounded; otherwise from halves with chained carries, the 9-bit sum >> 2 rounded. Each but 0 adds
   par_fixed and turns 0 and $40 or more (unsigned) into 1. Stored by par_store_boundary. Writes the halves state
   (par_bias_sum ... par_target_sum). r in: X (Y kept unless the halves path). r out: A = par_trans_dur, X + 1,
   Y = 0 on the halves path (proportion & 3 not 0), else as on entry. */
void par_boundary_value(fw *f, fw_regs *r);
/* $BEE2: the current phoneme dominates (par_dominant_phoneme = par_cur_phoneme, par_target_phoneme =
   par_next_phoneme). For p = par_param_index 0..9: par_ofs = cmd_formant_ofs[p], par_read_dominance, the dominant's
   external duration (row 3) into par_next_start_dur[p + 1] and its internal duration (row 4) into par_trans_dur,
   each through par_scale_duration, then par_boundary_value at the running X, which the loop saves around its body.
   r in: X. r out: A = 10, X + 10, Y as par_boundary_value leaves it. */
void par_boundary_cur_dom(fw *f, fw_regs *r);
/* $BF27: as par_boundary_cur_dom with the next phoneme dominant (par_dominant_phoneme = par_next_phoneme,
   par_target_phoneme = par_cur_phoneme) and the two durations the other way round: row 3 into par_trans_dur,
   row 4 into par_next_start_dur[p + 1]. */
void par_boundary_next_dom(fw *f, fw_regs *r);
/* $BE94: no transitions: par_next_start_dur[1..10] = 0 and par_trans_dur = 0, then par_param_index counts 10 down to
   1: the next phoneme's raw target (no @u offset, no clamp) through par_store_boundary, par_next_param after each.
   par_row_ptr is left ten strides on. r in: X. r out: A = par_row_ptr's high byte, X + 10, Y = par_next_phoneme. */
void par_boundary_jump(fw *f, fw_regs *r);
/* $BE6A: par_next_rank = the next phoneme's rank (par_rank_ptr); both ranks 29 or more: par_boundary_jump; else
   par_boundary_next_dom when par_next_rank >= par_cur_rank, par_boundary_cur_dom when it is lower (unsigned).
   r in: X, the running store index. r out: as the callee leaves them (A = par_next_rank going in). */
void par_boundary_pass(fw *f, fw_regs *r);

#endif
```

`src/firmware/par_boundary.c`: replace its first line

```c
#include "par_boundary.h"
```

with

```c
#include "par_boundary.h"
#include "par_segment.h"
#include "fw_ops.h"
#include "data/fw_data.h"
```

(Task 4's file includes only `par_boundary.h`), and append to the end of the file:

```c
/* ---- The boundary pass (M4.4a Task 5) ---- */

#define PAR_PARAMS        10     /* P1..P10: CMP #$0A, LDY #$0A */
#define PAR_P10_INDEX     9      /* CPX #9: the voicing parameter P10, scaled by @B */
#define PAR_RANK_JUMP     29     /* CMP #$1D: two ranks from 29 up (the pause 31, rank[$FF] 32) have no transitions */
#define PAR_PROP_WHOLE    8      /* CMP #8: the whole of target + bias */
#define PAR_BOUNDARY_MAX  0x3F   /* CMP #$40: a boundary value from $40 up becomes PAR_BOUNDARY_MIN */
#define PAR_BOUNDARY_MIN  1      /* LDA #1 */

/* $BEB9-$BEC8: when par_start_dur[p + 1] is 0, par_trans_dur is cut to par_frames_left if that is lower (BCS:
   unsigned; equal keeps it). */
static void cut_trans_dur(fw *f, uint8_t x)
{
    if (fw_rd8(f, (uint16_t)(PAR_START_DUR + 1 + x)) != 0)
        return;
    uint8_t left = fw_rd8(f, PAR_FRAMES_LEFT);
    if (left < fw_rd8(f, PAR_TRANS_DUR))
        fw_wr8(f, PAR_TRANS_DUR, left);
}

/* $BEC9-$BED9: when par_trans_dur is (still) 0, par_start_dur[p + 1] is cut to par_frames_left if that is lower. */
static void cut_start_dur(fw *f, uint8_t x)
{
    if (fw_rd8(f, PAR_TRANS_DUR) != 0)
        return;
    uint8_t left = fw_rd8(f, PAR_FRAMES_LEFT);
    if (left < fw_rd8(f, (uint16_t)(PAR_START_DUR + 1 + x)))
        fw_wr8(f, (uint16_t)(PAR_START_DUR + 1 + x), left);
}

void par_store_boundary(fw *f, fw_regs *r)
{
    fw_wr8(f, (uint16_t)(PAR_BOUNDARY + 1 + r->x), r->a);     /* $BEB5 STA $16ED,X */
    cut_trans_dur(f, r->x);
    cut_start_dur(f, r->x);
    r->a = fw_rd8(f, PAR_TRANS_DUR);                           /* $BEDA-$BEE0 */
    fw_wr8(f, (uint16_t)(PAR_END_DUR + 1 + r->x), r->a);
    r->x++;
}

/* $BF6C-$BF7B: par_dominated_target = par_clamp(target[par_target_phoneme] + par_ofs) (CLC; 8 bits), then the next
   row (the fixed contributions). */
static void dominated_target(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_TARGET_PHONEME);
    r->a = (uint8_t)(fw_ind_y(f, PAR_ROW_PTR, r->y) + fw_rd8(f, PAR_OFS));
    par_clamp(f, r);
    fw_wr8(f, PAR_DOMINATED_TARGET, r->a);
    par_next_row(f, r);
}

/* $BF7E-$BF8D: par_fixed = the dominant phoneme's fixed contribution, scaled by @B for P10, then the next row (the
   proportions). Y is left on par_dominant_phoneme for the rows after. */
static void fixed_contribution(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_DOMINANT_PHONEME);
    r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);
    if (r->x == PAR_P10_INDEX)
        par_scale_voicing(f, r);
    fw_wr8(f, PAR_FIXED, r->a);
    par_next_row(f, r);
}

/* $BF9D-$BFBE: with X saved (TXA; PHA ... PLA; TAX): A = 0 (AND #0), then par_ofs added par_ofs_weight times (LDX;
   BEQ: none when the weight is 0; CLC before each ADC, so 8 bits), >> 3 (PHP; LSR x3; PLP), with bits 7-5 set when
   the last add carried (BCC; ORA #$E0), added to par_fixed (CLC). A is left = X (PLA). */
static void weighted_offset(fw *f, fw_regs *r)
{
    uint8_t n = fw_rd8(f, PAR_OFS_WEIGHT);
    if (n != 0) {
        uint8_t a = 0, c = 0;
        do {
            c = 0;
            a = fw_adc(a, fw_rd8(f, PAR_OFS), &c);
        } while (--n != 0);
        a = (uint8_t)(a >> 3);
        if (c)
            a |= 0xE0;
        c = 0;
        fw_wr8(f, PAR_FIXED, fw_adc(a, fw_rd8(f, PAR_FIXED), &c));
    }
    r->a = r->x;
}

void par_read_dominance(fw *f, fw_regs *r)
{
    dominated_target(f, r);
    fixed_contribution(f, r);
    r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);                     /* $BF90-$BF9A: the proportion */
    fw_wr8(f, PAR_PROP, r->a);
    r->a = (uint8_t)((uint8_t)(r->a - 9) ^ 0xFF);              /* SEC; SBC #9; EOR #$FF: 8 - prop, mod 256 */
    fw_wr8(f, PAR_OFS_WEIGHT, r->a);
    weighted_offset(f, r);
    par_next_row(f, r);                                        /* $BFBF: row 3, the external durations */
}

/* $C039-$C058: the sums 0; x2 = t << 1 and half = x2 >> 2 (ASL; LSR; LSR: bit 7 is lost) of the dominated target t
   and of the bias b = par_target_bias[X] (LDA $B7A7,X). */
static void halves_setup(fw *f, uint8_t x)
{
    fw_wr8(f, PAR_BIAS_SUM, 0);
    fw_wr8(f, PAR_TARGET_SUM, 0);
    uint8_t t2 = (uint8_t)(fw_rd8(f, PAR_DOMINATED_TARGET) << 1);
    fw_wr8(f, PAR_TARGET_X2, t2);
    fw_wr8(f, PAR_TARGET_HALF, (uint8_t)(t2 >> 2));
    uint8_t b2 = (uint8_t)(fw_rom8(f, (uint16_t)(PAR_TARGET_BIAS + x)) << 1);
    fw_wr8(f, PAR_BIAS_X2, b2);
    fw_wr8(f, PAR_BIAS_HALF, (uint8_t)(b2 >> 2));
}

/* $C066-$C09D, proportion & 3 = k not 0: from 0, or from x2 when the proportion has bit 2, k halves added with the
   carry chained from add to add (one CLC before each loop): the target's sum into par_target_sum, the bias's left in
   A (par_bias_sum keeps 0 or x2); then (bias + target) as 9 bits (ROR takes the carry), >> 2 (LSR), rounded by the
   bit shifted out (ADC #0). Y counts down to 0. */
static uint8_t halves(fw *f, fw_regs *r, uint8_t prop)
{
    uint8_t k = prop & 3, a, c;
    r->y = k;                                                  /* TAY */
    if (prop & 4) {
        fw_wr8(f, PAR_TARGET_SUM, fw_rd8(f, PAR_TARGET_X2));
        fw_wr8(f, PAR_BIAS_SUM, fw_rd8(f, PAR_BIAS_X2));
    }
    fw_wr8(f, PAR_PROP_LOW, k);
    a = fw_rd8(f, PAR_TARGET_SUM);
    c = 0;
    do
        a = fw_adc(a, fw_rd8(f, PAR_TARGET_HALF), &c);
    while (--r->y != 0);
    fw_wr8(f, PAR_TARGET_SUM, a);
    r->y = fw_rd8(f, PAR_PROP_LOW);
    a = fw_rd8(f, PAR_BIAS_SUM);
    c = 0;
    do
        a = fw_adc(a, fw_rd8(f, PAR_BIAS_HALF), &c);
    while (--r->y != 0);
    c = 0;
    a = fw_adc(a, fw_rd8(f, PAR_TARGET_SUM), &c);
    a = (uint8_t)(c << 7 | a >> 1);                            /* ROR A */
    c = a & 1;                                                 /* LSR A */
    a = (uint8_t)(a >> 1);
    return fw_adc(a, 0, &c);                                   /* ADC #0 */
}

/* $C0B2-$C0B9, proportion 8: target + bias (CLC; 8 bits, the carry lost before $C09F's CLC). */
static uint8_t sum_whole(fw *f, uint8_t x)
{
    return (uint8_t)(fw_rd8(f, PAR_DOMINATED_TARGET) + fw_rom8(f, (uint16_t)(PAR_TARGET_BIAS + x)));
}

/* $C0C5-$C0CF, proportion & 3 = 0 (4, or 12 and up): bias + target (CLC; 8 bits) >> 1 (LSR), rounded (ADC #0). */
static uint8_t sum_half(fw *f, uint8_t x)
{
    uint8_t s = (uint8_t)(fw_rom8(f, (uint16_t)(PAR_TARGET_BIAS + x)) + fw_rd8(f, PAR_DOMINATED_TARGET));
    return (uint8_t)((s >> 1) + (s & 1));
}

/* $C09F-$C0AD: + par_fixed (CLC; 8 bits); 0 becomes 1, and so does $40 or more (CMP #$40; BCC: unsigned). */
static uint8_t limit(fw *f, uint8_t a)
{
    a = (uint8_t)(a + fw_rd8(f, PAR_FIXED));
    if (a == 0)
        a = PAR_BOUNDARY_MIN;
    if (a > PAR_BOUNDARY_MAX)
        a = PAR_BOUNDARY_MIN;
    return a;
}

void par_boundary_value(fw *f, fw_regs *r)
{
    halves_setup(f, r->x);
    uint8_t prop = fw_rd8(f, PAR_PROP);
    if (prop == 0) {                                           /* $C0BC: clamped, no 0 / $40 test */
        r->a = fw_rd8(f, PAR_FIXED);
        par_clamp(f, r);
    } else {
        if (prop == PAR_PROP_WHOLE)
            r->a = sum_whole(f, r->x);
        else if ((prop & 3) == 0)
            r->a = sum_half(f, r->x);
        else
            r->a = halves(f, r, prop);
        r->a = limit(f, r->a);
    }
    par_store_boundary(f, r);                                  /* $C0AE */
}

/* STA $171D,X (X = p: par_next_start_dur[p + 1]) or STA par_trans_dur: where a scaled duration goes. */
static void store_duration(fw *f, const fw_regs *r, bool start)
{
    if (start)
        fw_wr8(f, (uint16_t)(PAR_NEXT_START_DUR + 1 + r->x), r->a);
    else
        fw_wr8(f, PAR_TRANS_DUR, r->a);
}

/* $BEEE-$BF26 (cur_dom) and $BF33-$BF6B (next_dom): par_param_index = 0 (AND #0), then per parameter p, with the
   running X saved (TXA; PHA): X = p, par_ofs = cmd_formant_ofs[p], par_read_dominance (Y = the dominant phoneme), its
   duration rows 3 and 4 through par_scale_duration, par_next_row after each (row 3 into par_next_start_dur[p + 1]
   for cur_dom, into par_trans_dur for next_dom; row 4 the other), X restored (PLA; TAX), par_boundary_value; until
   par_param_index is 10. */
static void dominance_loop(fw *f, fw_regs *r, bool next_dom)
{
    r->a = 0;
    fw_wr8(f, PAR_PARAM_INDEX, r->a);
    do {
        uint8_t running = r->x;
        r->x = fw_rd8(f, PAR_PARAM_INDEX);
        r->a = fw_rd8(f, (uint16_t)(CMD_FORMANT_OFS + r->x));
        fw_wr8(f, PAR_OFS, r->a);
        par_read_dominance(f, r);
        r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);                 /* row 3: the external duration */
        par_scale_duration(f, r);
        store_duration(f, r, !next_dom);
        par_next_row(f, r);
        r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);                 /* row 4: the internal duration */
        par_scale_duration(f, r);
        store_duration(f, r, next_dom);
        par_next_row(f, r);
        r->a = running;
        r->x = running;
        par_boundary_value(f, r);
        fw_inc8(f, PAR_PARAM_INDEX);
        r->a = fw_rd8(f, PAR_PARAM_INDEX);
    } while (r->a != PAR_PARAMS);
}

void par_boundary_cur_dom(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_NEXT_PHONEME);                        /* $BEE2-$BEED */
    fw_wr8(f, PAR_TARGET_PHONEME, r->y);
    r->y = fw_rd8(f, PAR_CUR_PHONEME);
    fw_wr8(f, PAR_DOMINANT_PHONEME, r->y);
    dominance_loop(f, r, false);
}

void par_boundary_next_dom(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_CUR_PHONEME);                         /* $BF27-$BF32 */
    fw_wr8(f, PAR_TARGET_PHONEME, r->y);
    r->y = fw_rd8(f, PAR_NEXT_PHONEME);
    fw_wr8(f, PAR_DOMINANT_PHONEME, r->y);
    dominance_loop(f, r, true);
}

/* $BE94-$BEA4: par_param_index = 10, par_next_start_dur[10..1] = 0 (STA $171C,Y), par_trans_dur = 0, Y = the next
   phoneme. */
static void jump_setup(fw *f, fw_regs *r)
{
    r->y = PAR_PARAMS;
    fw_wr8(f, PAR_PARAM_INDEX, r->y);
    r->a = 0;
    do
        fw_wr8(f, (uint16_t)(PAR_NEXT_START_DUR + r->y), r->a);
    while (--r->y != 0);
    fw_wr8(f, PAR_TRANS_DUR, r->a);
    r->y = fw_rd8(f, PAR_NEXT_PHONEME);
}

void par_boundary_jump(fw *f, fw_regs *r)
{
    jump_setup(f, r);
    do {                                                       /* $BEA7-$BEB2 */
        r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);
        par_store_boundary(f, r);
        par_next_param(f, r);
        fw_wr8(f, PAR_PARAM_INDEX, (uint8_t)(fw_rd8(f, PAR_PARAM_INDEX) - 1));
    } while (fw_rd8(f, PAR_PARAM_INDEX) != 0);
}

void par_boundary_pass(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_NEXT_PHONEME);
    r->a = fw_ind_y(f, PAR_RANK_PTR, r->y);
    fw_wr8(f, PAR_NEXT_RANK, r->a);
    if (r->a >= PAR_RANK_JUMP) {
        r->a = fw_rd8(f, PAR_CUR_RANK);
        if (r->a >= PAR_RANK_JUMP) {                           /* $BE7F */
            par_boundary_jump(f, r);
            return;
        }
    }
    r->a = fw_rd8(f, PAR_NEXT_RANK);                           /* $BE83 */
    if (r->a >= fw_rd8(f, PAR_CUR_RANK))
        par_boundary_next_dom(f, r);
    else
        par_boundary_cur_dom(f, r);
}
```

`src/engine/thunks.c` (`firmware/par_boundary.h` is already included, by Task 4). The thunk functions go in address order among Task 4's: the six from $BE6A to $BF6C after `pros_scale_target_regs` ($BDD3), and `par_boundary_value_regs` ($C039) after `par_scale_voicing_regs` ($BFC3).
- Replace the end of `pros_scale_target_regs`

```c
    pros_scale_target(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    pros_scale_target(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_pass $BE6A: X in (the running store index, handed down to par_store_boundary and par_boundary_value;
   par_read_segment leaves it 0 at $BDD0, but the thunk takes the machine's); A and Y are written first (LDY
   par_next_phoneme, LDA (par_rank_ptr),Y). A, X and Y out as par_boundary_jump, par_boundary_next_dom or
   par_boundary_cur_dom leaves them (RTS at $BE82, $BE8F, $BE93). The only caller, par_next_segment ($BD00), calls
   par_plan_tracks next ($BD03), which writes Y and A first (LDY #$0A, LDA #0) and never reads X; then it returns to
   par_play ($B903 LDA par_frames_left), which writes Y ($B8B1 LDY) or X (par_step_tracks' LDX #$0A at $C0FF, after
   isr_par_ready, which preserves them) before reading them. No caller reads a flag. Declared: A, X, Y. */
static void par_boundary_pass_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_pass(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_jump $BE94: X in (the store index; A and Y are written first, LDY #$0A, LDA #0). A = par_row_ptr's
   high byte (par_next_param's), X + 10 and Y = par_next_phoneme out (RTS at $BEB4). The only caller,
   par_boundary_pass ($BE7F), returns at once ($BE82), and nothing reads them (par_boundary_pass's thunk comment).
   Declared: A, X, Y. */
static void par_boundary_jump_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_jump(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_store_boundary $BEB5: A (the boundary value) and X (the store index) in. A = par_trans_dur, X + 1 and Y
   untouched out (RTS at $BEE1). par_boundary_jump ($BEA9) calls par_next_param next, which writes A and keeps X
   and Y: its loop reads Y at $BEA7 (LDA (par_row_ptr),Y) and hands X to the next par_store_boundary. In
   par_boundary_value ($C0AE) the RTS at $C0B1 follows, and X reaches the next parameter's TXA in the dominance loop
   ($BEF3, $BF38). No caller reads a flag (JSR, RTS). Declared: A, X, Y. */
static void par_store_boundary_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_store_boundary(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_cur_dom $BEE2: X in (the store index; A and Y are written first, AND #0 and LDY). A = 10, X + 10 and
   Y as par_boundary_value leaves it out (RTS at $BF26). The only caller, par_boundary_pass ($BE90), returns at once
   ($BE93), and nothing reads them (par_boundary_pass's thunk comment). Declared: A, X, Y. */
static void par_boundary_cur_dom_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_cur_dom(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_next_dom $BF27: as par_boundary_cur_dom (RTS at $BF6B); the only caller, par_boundary_pass ($BE8C),
   returns at once ($BE8F). Declared: A, X, Y. */
static void par_boundary_next_dom_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_next_dom(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_read_dominance $BF6C: X = the parameter p in (CPX #9 at $BF83; A and Y are written first). A = par_row_ptr's
   low byte (par_next_row's), X = p and Y = par_dominant_phoneme out (RTS at $BFC2). Its callers read Y next
   (LDA (par_row_ptr),Y at $BF01 in par_boundary_cur_dom, $BF46 in par_boundary_next_dom) and X after
   par_scale_duration, which keeps X and Y (STA $171D,X at $BF06, $BF56); A is overwritten by that LDA. No caller
   reads a flag. Declared: A, X, Y. */
static void par_read_dominance_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_read_dominance(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace the end of `par_scale_voicing_regs`

```c
    par_scale_voicing(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    par_scale_voicing(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_value $C039: X (the store index) and Y (kept unless the halves path) in; A is written first (LDA #0).
   A = par_trans_dur (par_store_boundary's), X + 1, and Y = 0 on the halves path, else as it came in, out (RTS at
   $C0B1). Its callers, the dominance loops of par_boundary_cur_dom ($BF19) and par_boundary_next_dom ($BF5E),
   write A next (INC / LDA par_param_index); the next parameter reads X (TXA at $BEF3, $BF38) and writes Y
   (par_read_dominance's LDY at $BF6C) before reading it; after the tenth they return to par_boundary_pass (its thunk
   comment). No caller reads a flag. Declared: A, X, Y. */
static void par_boundary_value_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_value(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace

```c
THUNK(pros_scale_target)
```

with

```c
THUNK(pros_scale_target)
THUNK(par_boundary_pass)
THUNK(par_boundary_jump)
THUNK(par_store_boundary)
THUNK(par_boundary_cur_dom)
THUNK(par_boundary_next_dom)
THUNK(par_read_dominance)
```

- Replace

```c
THUNK(par_scale_voicing)
```

with

```c
THUNK(par_scale_voicing)
THUNK(par_boundary_value)
```

- Replace

```c
    { "pros_scale_target",  0xBDD3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_scale_target },
```

with

```c
    { "pros_scale_target",  0xBDD3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_scale_target },
    { "par_boundary_pass",  0xBE6A, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_pass },
    { "par_boundary_jump",  0xBE94, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_jump },
    { "par_store_boundary", 0xBEB5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_store_boundary },
    { "par_boundary_cur_dom", 0xBEE2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_cur_dom },
    { "par_boundary_next_dom", 0xBF27, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_next_dom },
    { "par_read_dominance", 0xBF6C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_read_dominance },
```

- Replace

```c
    { "par_scale_voicing",  0xBFC3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_voicing },
```

with

```c
    { "par_scale_voicing",  0xBFC3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_voicing },
    { "par_boundary_value", 0xC039, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_value },
```

The table stays in address order (42 rows); the `THUNK` lines follow it.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_BEB5_par_store_boundary.txt`: `STA $16ED,X` → `PAR_BOUNDARY + 1 + r->x` (an absolute,X address: no 8-bit wrap of the sum); $BEB9–$BEC8 → `cut_trans_dur` (`LDA $172D,X / BNE`, `LDA par_frames_left / CMP par_trans_dur / BCS` = `left < trans`); $BEC9–$BED9 → `cut_start_dur` (`LDA par_trans_dur / BNE`, `CMP $172D,X / BCS`); $BEDA–$BEE0 → `r->a`, `STA $173D,X`; `INX` → `r->x++` (8 bits).
- `rom_BF6C_par_read_dominance.txt`: $BF6C–$BF7B → `dominated_target` (`LDY par_target_phoneme`, `LDA (par_row_ptr),Y` through `fw_ind_y`, `CLC; ADC par_ofs` 8 bits, `JSR par_clamp`, `STA`, `JSR par_next_row`); $BF7E–$BF8D → `fixed_contribution` (`CPX #9 / BNE` → `r->x == PAR_P10_INDEX`); $BF90–$BF9A (`SEC; SBC #9; EOR #$FF`); $BF9D–$BFBE → `weighted_offset` (`TXA/PHA`, `AND #0`, `LDX/BEQ`, the `CLC; ADC; DEX; BNE` loop with `c` reset each time, `PHP; LSR ×3; PLP; BCC; ORA #$E0`, `CLC; ADC par_fixed; STA`, `PLA; TAX` → `r->a = r->x`); $BFBF `JSR par_next_row`.
- `rom_C039_par_boundary_value.txt`: $C039–$C058 → `halves_setup` (`LDA $B7A7,X` through `fw_rom8`); $C059–$C064 → the dispatch (`BEQ $C0BC`, `CMP #8 / BEQ $C0B2`, `AND #3 / BEQ $C0C5`); $C066–$C09D → `halves` (`TAY`; `AND #4 / BEQ`; `STY par_prop_low`; the two loops, one `CLC` each; `CLC; ADC par_target_sum; ROR A; LSR A; ADC #0`); $C09F–$C0AD → `limit` (`CLC; ADC par_fixed; BNE; LDA #1; CMP #$40; BCC; LDA #1`); $C0AE → `par_store_boundary`; $C0B2–$C0B9 → `sum_whole`; $C0BC–$C0C2 → `par_clamp(par_fixed)` then the store; $C0C5–$C0CF → `sum_half`.
- `rom_BEE2_par_boundary_cur_dom.txt` and `rom_BF27_par_boundary_next_dom.txt`: the entries ($BEE2–$BEED, $BF27–$BF32: the two `LDY`/`STY` pairs, in that order); the loops → `dominance_loop` (`AND #0; STA par_param_index`; `TXA; PHA` → `running`; `LDX par_param_index`; `LDA $2014,X; STA par_ofs`; `JSR par_read_dominance`; `LDA (par_row_ptr),Y; JSR par_scale_duration; STA …; JSR par_next_row` twice, with `STA $171D,X` first in cur_dom ($BF06) and `STA par_trans_dur` first in next_dom ($BF4B) → `store_duration(f, r, !next_dom)` then `store_duration(f, r, next_dom)`; `PLA; TAX`; `JSR par_boundary_value`; `INC par_param_index; LDA; CMP #$0A; BNE`). Compare the two dumps line by line: they differ only in the entry's two `LDY`s and in the two `STA`s.
- `rom_BE94_par_boundary_jump.txt`: $BE94–$BEA4 → `jump_setup` (`LDY #$0A; STY par_param_index; LDA #0`, `STA $171C,Y` for Y = 10…1, `STA par_trans_dur`, `LDY par_next_phoneme`); $BEA7–$BEB2 → the loop (`LDA (par_row_ptr),Y` raw, `JSR par_store_boundary`, `JSR par_next_param`, `DEC par_param_index; BNE`).
- `rom_BE6A_par_boundary_pass.txt`: `LDY par_next_phoneme; LDA (par_rank_ptr),Y; STA par_next_rank`; `CMP #$1D / BCC` twice (unsigned, `>= PAR_RANK_JUMP`); `JSR par_boundary_jump; RTS`; $BE83 `LDA par_next_rank; CMP par_cur_rank; BCC $BE90` → `>=` gives `next_dom`.
- Every read of the voice table is `fw_ind_y` on `PAR_ROW_PTR`/`PAR_RANK_PTR` (they point into ROM); `par_target_bias` through `fw_rom8`; no `fw_rd8` on anything that can point into ROM.

For the thunk outputs, re-read `rom_BCF3_par_next_segment.txt` ($BD00–$BD06), `rom_C0D2_par_plan_tracks.txt` (its first instructions), `rom_B877_par_play.txt` ($B903–$B914, $B8B1) and `rom_C0FF_par_step_tracks.txt` ($C0FF), and the call sites listed in "ROM facts checked". Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xBE6A,0xBE94,0xBEB5,0xBEE2,0xBF27,0xBF6C,0xC039) for op in (0x20,0x4C)]"
```

Expected:

```
0xbe6a 0x20 ['0xbd00']
0xbe6a 0x4c []
0xbe94 0x20 ['0xbe7f']
0xbe94 0x4c []
0xbeb5 0x20 ['0xbea9', '0xc0ae']
0xbeb5 0x4c []
0xbee2 0x20 ['0xbe90']
0xbee2 0x4c []
0xbf27 0x20 ['0xbe8c']
0xbf27 0x4c []
0xbf6c 0x20 ['0xbefe', '0xbf43']
0xbf6c 0x4c []
0xc039 0x20 ['0xbf19', '0xbf5e']
0xc039 0x4c []
```

and for relative branches into the seven entries (every byte position, 2-byte branches, `BBS`/`BBC` on A, and 3-byte `BBS`/`BBC` on zero page):

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xBE6A,0xBE94,0xBEB5,0xBEE2,0xBF27,0xBF6C,0xC039):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected: seven lines, each with `[]` (`0xbe6a []` … `0xc039 []`).

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_par"`, then the full suite `cmd //c ".\build.bat test"` (it runs `diff_lts` and `golden_hybrid` and prints their lines below; no separate filtered runs)
Expected:
- `suite_fw_par                 ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 42 lines `<name> N calls checked`, each N > 0, 0 mismatches. The proof run printed, from `tn_convert_clause` to `cmd_hex_value` (the seven of this task among Task 4's seven):

```
  tn_convert_clause    224 calls checked
  pros_microprosody    3391 calls checked
  par_next_row         40696 calls checked
  par_next_param       30676 calls checked
  par_clamp            39664 calls checked
  pros_scale_target    3564 calls checked
  par_boundary_pass    3391 calls checked
  par_boundary_jump    1093 calls checked
  par_store_boundary   23447 calls checked
  par_boundary_cur_dom 833 calls checked
  par_boundary_next_dom 1465 calls checked
  par_read_dominance   15982 calls checked
  par_scale_voicing    5689 calls checked
  par_boundary_value   15982 calls checked
  par_scale_duration   25782 calls checked
  cmd_hex_value        144 calls checked
```

  `par_boundary_pass` (3391) equals `pros_microprosody` (3391, one per segment) and is `par_boundary_jump` + `par_boundary_cur_dom` + `par_boundary_next_dom` (1093 + 833 + 1465); `par_read_dominance` equals `par_boundary_value` (15982; both are sampled, `DIFF_FULL_CALLS` 1000 then every 64th per file). Every line outside these seven is as after Task 4. Then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged.
- `golden_hybrid`: `45 corpus files x 43 modes (all, then each routine alone)`, all identical (about 4 minutes).
- The full run: `32 suite(s), 0 failure(s), 0 skip(s)`, with no warnings, the firmware syntax check passing, and `firmware_data` still at `47692 converted bytes compared with the ROM, 0 differ`.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/par_boundary.h src/firmware/par_boundary.c src/engine/thunks.c tests/test_fw_par.c
git commit -m "The boundary pass in C: par_store_boundary, par_read_dominance, par_boundary_value, par_boundary_cur_dom, par_boundary_next_dom, par_boundary_jump and par_boundary_pass, with hooks; the running X flows through r (M4.4a 2.2, 2.7)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The top: `par_read_segment` $BD33, `par_plan_tracks` $C0D2, `par_next_segment` $BCF3, `par_reset_tracks` $BB35, `par_play_start` $BAF2 and `par_setup_voice` $B3A4 in C, with their hooks; the C `cmd_apply_pitch` calls the C `par_setup_voice` (spec §2.1–§2.5, §2.7, §4.4, §4.5)

**ROM facts checked** (dumps `rom_BD33_par_read_segment.txt`, `rom_C0D2_par_plan_tracks.txt`, `rom_BCF3_par_next_segment.txt`, `rom_BB35_par_reset_tracks.txt`, `rom_BAF2_par_play_start.txt`, `rom_B3A4_par_setup_voice.txt`; the ROM callees `rom_B41D_frm_build_rest_frame.txt`, `rom_B445_frm_copy_rest_frame.txt`, `rom_B930_frm_clamp_mark_space.txt`, `rom_CA71_pros_reset_seg_ptrs.txt`; the callers `rom_A832_cmd_apply_pitch.txt`, `rom_A7D4_boot_loop_play.txt` ($B2B6, boot_play_body), `rom_B877_par_play.txt` ($B900), and for what they read next `rom_B323_isr_speech_start.txt`, `rom_C0FF_par_step_tracks.txt`, `rom_A78C_cmd_apply_speaker.txt`, `rom_A84A_cmd_apply_speed.txt`, `rom_A61C_boot_reset.txt`, `rom_A600_bank_load_ramcode.txt`; every instruction of the six routines was matched to the C below):
- **No name or signature had to change.** All six take `(fw *, fw_regs *)` as fixed. Nothing was added to `fw_ops.h`; the C uses `fw_adc` and `fw_inc8`. `par_segment.c` needs `fw_ops.h` (Task 4's `par_next_row` already uses `fw_inc8`) and gets `#include "pros_play.h"` here, for the two pitch calls.
- **`par_read_segment` reads `par_row_ptr` without setting it.** Its first read is `LDA (0x32),Y` at $BD83, and it relies on `par_next_segment` ($BCF3) having set the pointer to `par_params_ptr`. It sets it back itself at $BDA4. The unit tests set it as the caller would.
- **`par_read_segment`'s exit A is `par_boundary[1]`.** That is the `LDA $16EC,Y` at $BDB6 with Y = 1; the later `STA`s, `DEY`, `LDX`/`DEX`/`CPX`/`DEC`/`STX` and `LDX #0` do not change A. Y = 0 (the copy loop's end) and X = 0 ($BDD0).
- **The duration's `DEC`/`INC` pair is exact.** `DEC pros_duration_ptr+1` ($BD6E) runs when Y = 0 before the `DEY`. `INC` ($BD7E) runs on `CPY #$FF` after the read, that is, exactly when Y was 0. So the pointer is always back, and the read at Y = $FF hits the byte before the page. X is 0 at `STA $17BF,X` ($BD76), because `pros_scale_target` and `pros_microprosody` keep X. The C passes `r` through them, so it takes that X from them, not a constant.
- **The @u offset is `cmd_formant_ofs[X − 1]`** (`ADC $2013,X`, X = 1..10). Before it, `BMI` ($BD85) on the loaded target, then `CMP #3` / `BCC` ($BD87). The `CLC` at $BD8B is the only carry set-up. `par_clamp` takes the sum in A; at X = 10, `par_scale_voicing` takes the clamped value. `par_next_param` ($BD9D) runs after the `INX`; it does not touch X or Y.
- **`par_plan_tracks`' exit A** is the last difference when P1 overlaps (the negative `par_end_wait[1] − par_start_dur[1]`, as the `BMI` at $C0F1 leaves it), else 1 ($C0F6). X is not touched; Y = 0.
- **`par_reset_tracks` reads `frm_snapshot_map` as `$B772,X`, X = 10..1**, so only $B773–$B77C (Task 3's range). $B772, the last byte of `frm_b14_15_hi`, is never read. `frm_snap` is written as `$16CC,X` = $16CD–$16D6, and `par_boundary[0]` ($16EC) and `par_next_start_dur[0]` are not touched. The C makes no ROM call there, so `r` is left as it is; the PHA/PLA pairs are on the dead stack.
- **`par_play_start`'s register flow.** `pros_reset_seg_ptrs` (C since M4.3a) leaves A = $0E ($CA71 dump: last `LDA #$0E`), which `LDA $0A00` ($BAF8) overwrites. `pros_scale_target` returns A = the entry Y, which `LDA $0200` ($BB01) overwrites. X is never written. Exit: A = 0 ($BB2C) and Y = `pros_seg_phoneme[0]` ($BB0A `TAY`). `pros_seg_pitch[0]` and `pros_seg_phoneme[0]` are read absolute ($0A00, $0200), so the C uses `fw_rd8` for them. The rank is `(par_rank_ptr),Y` ($BB0B), so the C uses `fw_ind_y`. The rank of the pause phoneme $3B is 31 in both voices (ROM bytes $D4A5 and $E61E).
- **`par_setup_voice`'s registers at each ROM call.**
  - At `JSR frm_build_rest_frame` ($B404): A = `par_param_stride+1` (the `STA` at $B3F2), X = 0 (the shift loop's `DEX`, $B3E3) and Y = the entry Y. `par_next_row` does not touch Y, and nothing else writes it.
  - None of the three ROM routines reads its entry registers. `frm_build_rest_frame` starts with `LDY #$14` and sets X with `LDX #1` ($B42A) before using it. `frm_copy_rest_frame` starts with `LDY #$13`. `frm_clamp_mark_space` starts with `LDY #$0A`.
  - What they return:
    - `frm_build_rest_frame`: A = `par_params_ptr+1` (`LDA` $B43F), X = 9 (`INX`/`INX` from 1 while < 8) and Y = $3B (its loop does not change Y).
    - `frm_copy_rest_frame`: Y = $FF (`DEY`/`BPL`), A = `frm_rest_frame[0]` (the last `LDA $2000,Y`), X untouched.
    - `frm_clamp_mark_space`: A = $0B (`LDA #$0B` at $B954; the `STA` after it does not change A), Y = $0B (`LDY #$0A` / `INY`), X untouched.
  - So the C's exit registers are A = $0B, X = 9 and Y = $0B, as spec §2.2 and facts.md §3 say. The unit-test mocks return exactly these.
- **`par_setup_voice`'s second `LDM`s write only the low bytes** ($B40D `LDM #$B9,$2A`, $B410 `LDM #$A5,$2C`). The high bytes stay $16 from $B3F5/$B3FB. The C writes the same bytes in the same order (high byte first in the first group).
- **The stride arithmetic** ($B3D2–$B3F2): `AND #0` writes 0 to both stride bytes. Then `ASL A` / `ROL stride+1`, twice (X = 2), gives 4N with its two top bits in the high byte. Then `CLC` / `ADC par_row_len` → low byte, and `LDA` hi / `ADC #0` → high byte, with that carry. This is 5N exactly for any 8-bit N. The C keeps the dead first store of the low byte ($B3D4).
- **The nested thunk works** (spec §2.3, `par_next_param` row). In the hybrid engine the C `cmd_apply_pitch` (inside the `cmd_apply_speaker` thunk, $A78C) calls the C `par_setup_voice`. Its `fw_rom_call(ROM_FRM_BUILD_REST_FRAME)` runs the ROM `frm_build_rest_frame`, whose `JSR par_next_param` ($B431) hits the $BD14 hook. That calls `run_thunk` again on the same bridge.
  - `run_thunk` saves the outer trap (`saved = b->f.trap`), sets its own, restores it, and emulates the RTS (`machine_return`) back into the ROM routine.
  - A scratch probe (a depth counter in `run_thunk`, not part of the plan) printed `NESTED thunk at BD14 inside A78C (depth 1)` for `apollo2 --engine hybrid -o … "Hello."` and for `presets`.
  - `golden_hybrid`'s "all" mode and the CLI check (Step 5) produce identical frames through that path.
- **`tests/test_fw_cmd.c` loses two things a `fw_rom_call(ROM_PAR_SETUP_VOICE)` record used to show.** The A at the call (`pros_pitch_centre`) and the X can no longer be seen: the C `par_setup_voice` overwrites both before its first ROM call (`LDA par_count` at $B3A4, `LDX #2` at $B3DA), as the ROM does.
  - The checks now pin `pros_pitch_centre` in RAM where the old record checked a non-zero A (`test_set_pitch` $B0, `test_set_voice` per preset; `test_apply_pitch` already checks $90).
  - The Y is still pinned: it is frm_build_rest_frame's Y.
  - Every other assertion of those tests is kept. `g_ncalls` goes from 1 to 5 in each case.
- **Callers by scan** (Step 4):
  - `JSR $BD33` only at $BCFD; `JSR $C0D2` only at $BD03; `JSR $BCF3` only at $B900; `JSR $BB35` only at $BAF2; `JSR $BAF2` only at $B2B6; `JSR $B3A4` only at $A845.
  - No `JMP` reaches any of the six, and no relative branch does either.
- **What the callers read** (the thunk comments, checked in the caller dumps):
  - `par_read_segment`: X = 0, read by the boundary pass ($BEB5 `STA $16ED,X` via the jump, $BEF3/$BF38 `TXA`). The pass loads A and Y first.
  - `par_plan_tracks`: nothing. `par_next_segment` returns at once ($BD06).
  - `par_next_segment`: nothing. `par_play` does `LDA par_frames_left` at $B903, then either `LDY` at $B8B1, or `isr_par_ready` (which preserves A, X and Y) and `par_step_tracks` (`LDX #$0A` first, $C0FF).
  - `par_reset_tracks`: X and Y are preserved. X becomes `par_play_start`'s exit X; Y is pushed and restored by `pros_scale_target`, then replaced at $BB0A.
  - `par_play_start`: nothing. `isr_speech_start` writes X ($B323 `LDX #0`) and A ($B32B `LDA`) first, and Y is unread until `par_play`'s `LDY` at $B8B1.
  - `par_setup_voice`: A is restored by `cmd_apply_pitch`'s `PLA` ($A848). X and Y reach RAM: `cmd_apply_speaker` → `cmd_apply_speed`, which keeps A and Y (PHA/TYA/PHA) and does not touch X → `boot_reset` $A675 → `bank_load_ramcode` `STY $78` / `STX $76` ($A601/$A603).
  - No caller reads a flag.
- **Where the hooks fire.** In "all" mode, `par_play_start` and `par_next_segment` fire from the ROM `boot_play_body` and `par_play`. `par_setup_voice`, `par_read_segment`, `par_plan_tracks` and `par_reset_tracks` fire only in their "alone" modes. The C `cmd_apply_pitch` now calls the C `par_setup_voice`, so its hook fires only when the ROM `cmd_apply_pitch` runs.
- **`cmd_apply.c` no longer includes `cmd_addr.h`.** `ROM_PAR_SETUP_VOICE` was the only name it used from it. After this task, `grep -rn ROM_PAR_SETUP_VOICE src` finds only `par_addr.h`.
- **The `diff_lts` counts are sampled**: every call up to 1000 per routine and corpus file, then every 64th. So `par_next_segment 3391` is not the number of segments. `par_read_segment`, `par_plan_tracks` and `par_boundary_pass` equal it, because each runs once per `par_next_segment`.

**Proof run.** The consolidated plan was executed literally in a scratch worktree from main 5f28310 (`APOLLO2_ROM` = the main checkout's image), Tasks 1–5 applied as written first. (The task was drafted on stand-ins for Tasks 1–5; the scratch probe of the nested thunk and the mutations below are from that drafting run.)

Results:
- `fw_par`, `fw_cmd`, `fw_pros`, `fw_tn` and `fw_lts`: all ok.
- Full `build.bat test`: `32 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: all 48 lines > 0 and 0 mismatches (lines in Step 5). `golden_hybrid`: `45 corpus files x 49 modes`, all identical. `firmware_data`: `47692 converted bytes compared with the ROM, 0 differ`.
- CLI: `pitch_extremes same`, `voicing_levels same`, `fast_effect same`, `effects same`.
- Mutations (scratch), each caught by `suite_fw_par`:
  - dropping the `BMI` test fails `test_read_segment` (P1 gets 0x3F, not 1);
  - dropping the `DEC` fails `test_read_segment_wrap` (the pointer ends at $0900 and the duration reads $EE);
  - `isr_swap_phase` 2 for the second frame fails `test_setup_voice` (`phase` 2, not 1).

**Files:**
- Create: `src/firmware/par_start.h`, `src/firmware/par_start.c`
- Modify:
  - `src/firmware/par_segment.h`, `src/firmware/par_segment.c` (`par_read_segment` appended);
  - `src/firmware/par_addr.h` (`ROM_PAR_SETUP_VOICE` moves here);
  - `src/firmware/cmd_addr.h` (`ROM_PAR_SETUP_VOICE` removed);
  - `src/firmware/cmd_apply.c`, `src/firmware/cmd_apply.h` (the C call and its comment);
  - `src/engine/thunks.c` (six thunks and rows);
  - `tests/test_fw_par.c` (the mock answers the `frm_*` calls and records the frame buffer pointers; the Task 6 tests);
  - `tests/test_fw_cmd.c` (the `par_setup_voice` checks become checks of the C call; its mock answers the `frm_*` calls).

**Interfaces:**
- Consumes:
  - Task 1: `fw_ops.h` (`fw_adc`, `fw_inc8`).
  - Task 3 (in `data/fw_data.h`):
    - `FRM_SNAPSHOT_MAP` ($B773, read by `par_reset_tracks`);
    - `PAR_VOICE_A`, `PAR_PHONEMES` (tests);
    - the voice tables converted (`par_play_start`'s rank, through `par_rank_ptr`; `par_read_segment`'s targets, through `par_row_ptr`);
    - `cmd_sound_data` converted (`par_read_segment` reads through the playback pointers during @T).
  - Task 4:
    - `par_segment.h`: `par_next_row`, `par_next_param`, `par_clamp`, `par_scale_voicing`;
    - `pros_play.h`: `pros_scale_target`, `pros_microprosody`;
    - `par_addr.h` with `ROM_FRM_BUILD_REST_FRAME`, `ROM_FRM_COPY_REST_FRAME`, `ROM_FRM_CLAMP_MARK_SPACE`, `ROM_PAR_PLAY_START`, `ROM_PAR_RESET_TRACKS`, `ROM_PAR_NEXT_SEGMENT`, `ROM_PAR_READ_SEGMENT`, `ROM_PAR_PLAN_TRACKS`;
    - the harness of `tests/test_fw_par.c`: `g_ram`, `g_f`, `g_host`, `g_calls`, `g_ncalls`, `MAX_CALLS`, `rom_call_record`, `mock_rom_call`, `setup()`, `check_call()`, `check_row()`, `suite_fw_par`.
  - Task 5: `par_boundary.h`: `par_boundary_pass`, and through it the boundary routines.
  - M4.3a: `pros_reset_seg_ptrs(fw *)` (`pros_seg.h`). M4.2: `cmd_apply_pitch` (`cmd_apply.c`) and the mock of `tests/test_fw_cmd.c`.
- Produces:

```c
/* par_segment.h */
void par_read_segment(fw *f, fw_regs *r);   /* $BD33 */
/* par_start.h */
void par_plan_tracks(fw *f, fw_regs *r);    /* $C0D2 */
void par_next_segment(fw *f, fw_regs *r);   /* $BCF3 */
void par_reset_tracks(fw *f, fw_regs *r);   /* $BB35 */
void par_play_start(fw *f, fw_regs *r);     /* $BAF2 */
void par_setup_voice(fw *f, fw_regs *r);    /* $B3A4 */
/* par_addr.h (moved from cmd_addr.h) */
#define ROM_PAR_SETUP_VOICE        0xB3A4
```

  - Six replacements, each declaring `FW_OUT_A | FW_OUT_X | FW_OUT_Y`: 48 in all, and 49 `golden_hybrid` modes.
  - The C `cmd_apply_pitch` no longer calls `ROM_PAR_SETUP_VOICE` (spec §4.5 criterion 6).

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_par.c` (Task 4's harness, as Tasks 4 and 5 leave it):

- Replace

```c
#include "firmware/par_segment.h"
```

with

```c
#include "firmware/par_segment.h"
#include "firmware/par_start.h"
```

- Replace the record type

```c
typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;
```

with

```c
/* One ROM call: its address, registers and budget, and the frame buffer pointers as the C left them at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint16_t play, fill;
    uint8_t phase;
} rom_call_record;
```

- Replace Task 4's `mock_rom_call` and its comment

```c
/* Records every ROM call, and fails it: none of the leaves makes one. */
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
    return FW_CALL_FAILED;
}
```

  with

```c
/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) return the registers the ROM ones
   leave; any other call fails (no other routine of the playback set-up makes one). */
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
        c->play = fw_rd16(&g_f, ISR_PLAY_PTR);
        c->fill = fw_rd16(&g_f, ISR_FILL_PTR);
        c->phase = g_ram[ISR_SWAP_PHASE];
    }
    g_ncalls++;
    switch (addr) {
    case ROM_FRM_BUILD_REST_FRAME:   /* $B428-$B444: LDY #$3B; LDX #1, INX / INX until X >= 8 (9); LDA par_params_ptr+1 */
        r->a = g_ram[PAR_PARAMS_PTR + 1];
        r->x = 9;
        r->y = 0x3B;
        return FW_CALL_RETURNED;
    case ROM_FRM_COPY_REST_FRAME:    /* $B445-$B44F: LDA $2000,Y for Y = $13..0, DEY / BPL: Y = $FF; X untouched */
        r->a = g_ram[FRM_REST_FRAME];
        r->y = 0xFF;
        return FW_CALL_RETURNED;
    case ROM_FRM_CLAMP_MARK_SPACE:   /* $B930-$B95C: LDY #$0A, INY; LDA #$0B at $B954; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}
```

  (`g_f` is declared before the mock in the harness, as in `test_fw_pros.c`; Task 4's and Task 5's tests make no ROM call, so the extension changes nothing for them.)

- Replace

```c
void suite_fw_par(void)
{
```

with

```c
/* ---- the top of the playback set-up (M4.4a Task 6) ---- */

#define T6_TABLE   0x3000   /* a voice table built in RAM: one parameter block every T6_STRIDE bytes */
#define T6_STRIDE  0x10
#define T6_RANKS   0x3800   /* its rank row */
#define T6_CUR     5        /* the phoneme that becomes the current one */
#define T6_NEXT    0x2A     /* the phoneme at the next index: in no microprosody list */

static void test_top_rows(void)
{
    check_row("par_setup_voice", ROM_PAR_SETUP_VOICE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_play_start", ROM_PAR_PLAY_START, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_reset_tracks", ROM_PAR_RESET_TRACKS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_next_segment", ROM_PAR_NEXT_SEGMENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_read_segment", ROM_PAR_READ_SEGMENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_plan_tracks", ROM_PAR_PLAN_TRACKS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* The pitch settings under which pros_scale_target keeps a target below $80: R = cmd_source_prosody & 7 = 4 (below
   cmd_speed_limit 7) adds the whole deviation d = |target - $80| back to the centre $80 ($BE08 BPL taken, k = 0:
   A = Y = d at $BE58), and $80 - d >= 4 gets no ORA #4. */
static void pitch_identity(void)
{
    g_ram[CMD_SOURCE_PROSODY] = 4;
    g_ram[CMD_SPEED_LIMIT] = 7;
    g_ram[PROS_PITCH_CENTRE] = 0x80;
}

/* par_cur_phoneme-to-be T6_CUR's ten targets in the RAM table, with their @u offsets, one per case of $BD83-$BD99,
   and the segment arrays at index 4 with the duration at index 3; the tracks' boundaries and next start durations
   to copy; pros_seg_count = $0100. */
static void read_setup(void)
{
    setup();
    pitch_identity();
    fw_wr16(&g_f, PAR_PARAMS_PTR, T6_TABLE);
    fw_wr16(&g_f, PAR_ROW_PTR, T6_TABLE);           /* par_next_segment's job ($BCF3) */
    fw_wr16(&g_f, PAR_PARAM_STRIDE, T6_STRIDE);
    static const uint8_t target[10] = { 0x85, 2, 3, 0x30, 0x7F, 0x70, 0x20, 4, 0x3F, 0x10 };
    static const uint8_t ofs[10] = { 0xC0, 5, 5, 0x20, 1, 0x20, 0xFE, 0xFC, 0, 0 };
    for (unsigned p = 0; p < 10; p++) {
        g_ram[T6_TABLE + T6_CUR + T6_STRIDE * p] = target[p];
        g_ram[CMD_FORMANT_OFS + p] = ofs[p];
    }
    g_ram[CMD_VOICING] = 3;
    g_ram[PAR_NEXT_PHONEME] = T6_CUR;
    g_ram[PAR_NEXT_RANK] = 0x21;
    g_ram[PROS_PLAY_TARGET] = 0x55;
    fw_wr16(&g_f, PAR_SEG_INDEX, 3);
    fw_wr16(&g_f, PROS_PHONEME_PTR, PROS_SEG_PHONEME);
    fw_wr16(&g_f, PROS_DURATION_PTR, PROS_SEG_DURATION);
    fw_wr16(&g_f, PROS_PITCH_PTR, PROS_SEG_PITCH);
    fw_wr16(&g_f, PROS_FLAGS_PTR, PROS_SEG_FLAGS);
    g_ram[PROS_SEG_PHONEME + 4] = T6_NEXT;
    g_ram[PROS_SEG_PITCH + 4] = 0x70;
    g_ram[PROS_SEG_DURATION + 3] = 9;
    g_ram[PROS_SEG_DURATION + 4] = 0xEE;              /* the duration at the index itself, not read */
    for (unsigned y = 0; y <= 10; y++) {
        g_ram[PAR_BOUNDARY + y] = (uint8_t)(0x10 + y);
        g_ram[PAR_NEXT_START_DUR + y] = (uint8_t)(0x20 + y);
        g_ram[PAR_START_DUR + y] = g_ram[PAR_START_VAL + y] = g_ram[PAR_LINE_VAL + y] = 0x5A;
        g_ram[PAR_SEG_TARGET + y] = 0x5A;
    }
    fw_wr16(&g_f, PROS_SEG_COUNT, 0x0100);
}

/* par_read_segment: the index moves on (no wrap); the next phoneme and its scaled target; the duration at index - 1;
   the ten targets: P1 $85 >= $80 gets no @u ($BD85 BMI taken: with the offset $C0 it would be $45 -> $3F) and is
   clamped to 1; P2 2 < 3 gets none (2, not 7); P3 3 + 5 = 8; P4 $30 + $20 = $50 -> $3F; P5 $7F + 1 = $80 -> $3F
   (N of $80 - $40 clear); P6 $70 + $20 = $90 -> 1 (N of $90 - 1 set); P7 $20 + $FE = $1E (8 bits); P8 4 + $FC = 0
   -> 1; P9 $3F; P10 $10 clamped, then @B3: $10 / 4 = 4 into $76, + 4 / 2 = 6. The tracks start from the
   boundaries; pros_seg_count $0100 -> $00FF (the DEC of the high byte). A = par_boundary[1], X = Y = 0. */
static void test_read_segment(void)
{
    read_setup();
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_read_segment(&g_f, &r);
    CHECK_EQ(g_ram[PAR_CUR_RANK], 0x21);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], T6_CUR);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME);   /* no wrap: the pointers stay */
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS);
    CHECK_EQ(g_ram[PAR_NEXT_PHONEME], T6_NEXT);
    CHECK_EQ(g_ram[PROS_PLAY_PITCH], 0x55);                        /* the old target */
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], 0x70);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 9);
    static const uint8_t want[11] = { 9, 1, 2, 8, 0x3F, 0x3F, 1, 0x1E, 1, 0x3F, 6 };
    for (unsigned x = 0; x <= 10; x++)
        CHECK_EQ(g_ram[PAR_SEG_TARGET + x], want[x]);
    CHECK_EQ(g_ram[FW_TMP_76], 4);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), T6_TABLE);                 /* back on P1's target row */
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_START_DUR + y], 0x20 + y);
        CHECK_EQ(g_ram[PAR_START_VAL + y], 0x10 + y);
        CHECK_EQ(g_ram[PAR_LINE_VAL + y], 0x10 + y);
    }
    CHECK_EQ(g_ram[PAR_START_DUR], 0x5A);                          /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_START_VAL], 0x5A);
    CHECK_EQ(g_ram[PAR_LINE_VAL], 0x5A);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x00FF);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The 16-bit index wrap: $01FF -> $0200 moves the index's high byte and the four pointers on a page (from one page
   on to two). At index low byte 0, the duration is read at index - 1 through the DEC / INC of pros_duration_ptr+1:
   ($0800 - $100) + $FF = $07FF, not $08FF; the pointer is back at $0800 afterwards. */
static void test_read_segment_wrap(void)
{
    read_setup();
    fw_wr16(&g_f, PAR_SEG_INDEX, 0x01FF);
    fw_wr16(&g_f, PROS_PHONEME_PTR, PROS_SEG_PHONEME + 0x100);
    fw_wr16(&g_f, PROS_DURATION_PTR, PROS_SEG_DURATION + 0x100);
    fw_wr16(&g_f, PROS_PITCH_PTR, PROS_SEG_PITCH + 0x100);
    fw_wr16(&g_f, PROS_FLAGS_PTR, PROS_SEG_FLAGS + 0x100);
    g_ram[PROS_SEG_PHONEME + 0x200] = T6_NEXT;
    g_ram[PROS_SEG_PITCH + 0x200] = 0x70;
    g_ram[PROS_SEG_DURATION + 0x1FF] = 7;
    g_ram[PROS_SEG_DURATION + 0x2FF] = 0xEE;          /* what Y = $FF without the DEC would read */
    fw_wr16(&g_f, PROS_SEG_COUNT, 5);
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_read_segment(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 0x0200);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME + 0x200);
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION + 0x200);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH + 0x200);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS + 0x200);
    CHECK_EQ(g_ram[PAR_NEXT_PHONEME], T6_NEXT);
    CHECK_EQ(g_ram[PROS_PLAY_TARGET], 0x70);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 7);
    CHECK_EQ(g_ram[PAR_SEG_TARGET], 7);
    CHECK_EQ(g_ram[PAR_SEG_TARGET + 10], 6);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 4);       /* no borrow: the high byte stays */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK(!g_f.fault);
}

/* par_next_segment on the jump path: par_row_ptr is set to par_params_ptr first; par_read_segment's X = 0 is the
   running index of par_boundary_jump's stores, so the next phoneme's ten raw targets (P1 $85: no clamp) land in
   par_boundary[1..10]; with par_trans_dur 0 each par_start_dur ($21..$2A, from par_next_start_dur) is cut to
   par_frames_left 9 and par_end_dur = 0; then par_plan_tracks: par_end_wait = 9 - 0, 9 - 9 = 0 is not negative,
   so par_steady = 0 and par_sequential = 1. Both ranks are >= 29 ($21 and $1E). Out: A = 1 (par_plan_tracks' last
   LDA #1), X = 10 (ten INX in par_store_boundary), Y = 0; par_row_ptr ten strides on. */
static void test_next_segment(void)
{
    read_setup();
    fw_wr16(&g_f, PAR_ROW_PTR, 0x1234);
    fw_wr16(&g_f, PAR_RANK_PTR, T6_RANKS);
    g_ram[T6_RANKS + T6_NEXT] = 0x1E;
    for (unsigned p = 0; p < 10; p++)
        g_ram[T6_TABLE + T6_NEXT + T6_STRIDE * p] = (uint8_t)(p == 0 ? 0x85 : 0x30 + p);
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_next_segment(&g_f, &r);
    CHECK_EQ(g_ram[PAR_NEXT_RANK], 0x1E);
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_BOUNDARY + y], y == 1 ? 0x85 : 0x30 + y - 1);
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + y], 0);
        CHECK_EQ(g_ram[PAR_START_DUR + y], 9);
        CHECK_EQ(g_ram[PAR_END_DUR + y], 0);
        CHECK_EQ(g_ram[PAR_END_WAIT + y], 9);
        CHECK_EQ(g_ram[PAR_STEADY + y], 0);
        CHECK_EQ(g_ram[PAR_SEQUENTIAL + y], 1);
        CHECK_EQ(g_ram[PAR_END_VAL + y], g_ram[PAR_SEG_TARGET + y]);
    }
    CHECK_EQ(g_ram[PAR_BOUNDARY], 0x10);             /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_TRANS_DUR], 0);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), T6_TABLE + 10 * T6_STRIDE);
    CHECK_EQ(r.a, 1);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* par_plan_tracks with par_frames_left = 200: for each parameter, par_end_dur and par_start_dur, and what follows
   from $C0DF-$C0F8 (SEC / SBC, BPL and BMI on bit 7 of the 8-bit difference). */
static void test_plan_tracks(void)
{
    static const struct { uint8_t end_dur, start_dur, end_wait, steady, sequential; } k[11] = {
        { 0 },
        { 5, 1, 0, 0x5A, 0 },          /* 195 = $C3 is negative: 0; 0 - 1 = $FF: overlap, par_steady stale */
        { 0xC8, 5, 0, 0x5A, 0 },       /* 0; 0 - 5 = $FB: overlap */
        { 0x49, 0xFF, 0x7F, 0x5A, 0 }, /* $7F; $7F - $FF = $80 (bit 7): overlap */
        { 0xC0, 3, 8, 5, 1 },
        { 0xD0, 0, 0, 0, 1 },          /* 200 - 208 = $F8: 0 */
        { 0x50, 0x78, 0x78, 0, 1 },
        { 0x48, 0, 0, 0, 1 },          /* 200 - 72 = 128 = $80, a difference >= $80: 0 */
        { 0xC7, 0, 1, 1, 1 },
        { 0x4A, 0x7E, 0x7E, 0, 1 },
        { 0x4A, 0, 0x7E, 0x7E, 1 },
    };
    setup();
    g_ram[PAR_FRAMES_LEFT] = 200;
    for (unsigned y = 0; y <= 10; y++) {
        g_ram[PAR_END_DUR + y] = k[y].end_dur;
        g_ram[PAR_START_DUR + y] = k[y].start_dur;
        g_ram[PAR_SEG_TARGET + y] = (uint8_t)(0x30 + y);
        g_ram[PAR_STEADY + y] = 0x5A;
        g_ram[PAR_SEQUENTIAL + y] = g_ram[PAR_END_VAL + y] = g_ram[PAR_END_WAIT + y] = 0xA5;
    }
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_plan_tracks(&g_f, &r);
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_END_VAL + y], 0x30 + y);
        CHECK_EQ(g_ram[PAR_END_WAIT + y], k[y].end_wait);
        CHECK_EQ(g_ram[PAR_STEADY + y], k[y].steady);
        CHECK_EQ(g_ram[PAR_SEQUENTIAL + y], k[y].sequential);
    }
    CHECK_EQ(g_ram[PAR_SEQUENTIAL], 0xA5);           /* entry 0 is not written */
    CHECK_EQ(g_ram[PAR_END_VAL], 0xA5);
    CHECK_EQ(g_ram[PAR_END_WAIT], 0xA5);
    CHECK_EQ(g_ram[PAR_STEADY], 0x5A);
    CHECK_EQ(r.a, 0xFF);                             /* P1's overlap difference, the last one computed */
    CHECK_EQ(r.x, 0x34);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(g_ncalls, 0);
}

/* par_reset_tracks: frm_snap[i] = frm_rest_frame[frm_snapshot_map[i]], the map being 10 8 6 4 2 0 1 3 5 7 (b10, AHF,
   A3, A2, A1, ALF, FN, F1, F2, F3); every entry 0 and the byte before frm_snap (isr_frame_b's last) untouched;
   A, X and Y kept. */
static void test_reset_tracks(void)
{
    static const uint8_t map[10] = { 10, 8, 6, 4, 2, 0, 1, 3, 5, 7 };
    setup();
    fw_wr16(&g_f, PAR_SEG_INDEX, 0x1234);
    g_ram[PAR_FRAMES_LEFT] = 0x77;
    memset(g_ram + FRM_SNAP - 1, 0x5A, 12);
    for (unsigned i = 0; i <= 10; i++)
        g_ram[PAR_NEXT_START_DUR + i] = g_ram[PAR_BOUNDARY + i] = 0x5A;
    for (unsigned i = 0; i < 20; i++)
        g_ram[FRM_REST_FRAME + i] = (uint8_t)(0x40 + i);
    fw_regs r = { 0x12, 0x34, 0x56, 0 };
    par_reset_tracks(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 0);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 1);
    for (unsigned i = 0; i < 10; i++)
        CHECK_EQ(g_ram[FRM_SNAP + i], 0x40 + map[i]);
    CHECK_EQ(g_ram[FRM_SNAP - 1], 0x5A);
    CHECK_EQ(g_ram[FRM_SNAP + 10], 0x5A);
    for (unsigned y = 1; y <= 10; y++) {
        CHECK_EQ(g_ram[PAR_NEXT_START_DUR + y], 0);
        CHECK_EQ(g_ram[PAR_BOUNDARY + y], y <= 4 ? 0x20 : 1);
    }
    CHECK_EQ(g_ram[PAR_NEXT_START_DUR], 0x5A);
    CHECK_EQ(g_ram[PAR_BOUNDARY], 0x5A);
    CHECK_EQ(r.a, 0x12);
    CHECK_EQ(r.x, 0x34);
    CHECK_EQ(r.y, 0x56);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* par_play_start on voice A: the pause phoneme $3B first, whose rank (par_voice_a's rank row, $D46A + $3B) is 31;
   pitch[0] $70 scaled by pros_scale_target (identity here), no microprosody; stale pointers reset; the first event's
   position, or $FFFF without events. Out: A = 0, X = the entry X, Y = the phoneme. */
static void test_play_start(void)
{
    for (int events = 0; events <= 1; events++) {
        setup();
        pitch_identity();
        fw_wr16(&g_f, PAR_RANK_PTR, PAR_VOICE_A);
        memset(g_ram + PROS_PHONEME_PTR, 0x5A, 8);
        g_ram[PROS_SEG_PHONEME] = 0x3B;
        g_ram[PROS_SEG_PITCH] = 0x70;
        g_ram[PROS_PLAY_PITCH] = 0x5A;
        g_ram[CMD_EV_COUNT] = (uint8_t)(events ? 2 : 0);
        fw_wr16(&g_f, CMD_EV_POSITIONS, 0x1234);
        g_ram[CMD_EV_NEXT] = 7;
        g_ram[PROS_PITCH_ERR] = 0x33;
        fw_regs r = { 0x12, 0x34, 0x56, 0 };
        par_play_start(&g_f, &r);
        CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), PROS_SEG_PHONEME);
        CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), PROS_SEG_DURATION);
        CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), PROS_SEG_PITCH);
        CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), PROS_SEG_FLAGS);
        CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 1);                    /* par_reset_tracks ran */
        CHECK_EQ(g_ram[PAR_BOUNDARY + 1], 0x20);
        CHECK_EQ(g_ram[PROS_PLAY_TARGET], 0x70);
        CHECK_EQ(g_ram[PROS_PLAY_PITCH], 0x5A);                 /* stale until the first par_read_segment */
        CHECK_EQ(g_ram[PAR_NEXT_PHONEME], 0x3B);
        CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);
        CHECK_EQ(g_ram[PAR_NEXT_RANK], 31);
        CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
        CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), events ? 0x1234 : 0xFFFF);
        CHECK_EQ(g_ram[CMD_EV_NEXT], 0);
        CHECK_EQ(g_ram[PROS_PITCH_ERR], 0);
        CHECK_EQ(r.a, 0);
        CHECK_EQ(r.x, 0x34);
        CHECK_EQ(r.y, 0x3B);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

/* Record i's frame buffer pointers: isr_play_ptr, isr_fill_ptr and isr_swap_phase as the C set them (LDM) before
   the call. */
static void check_buffers(unsigned i, uint16_t play, uint16_t fill, uint8_t phase)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].play, play);
    CHECK_EQ(g_calls[i].fill, fill);
    CHECK_EQ(g_calls[i].phase, phase);
}

/* par_setup_voice on a table at `table` with n phonemes: the pointers and the stride, then the five ROM calls in
   order with the registers the ROM has at each JSR and the buffers set before each; out, the last call's A and Y and
   frm_build_rest_frame's X. The entry A and X are not read; the entry Y reaches frm_build_rest_frame. */
static void setup_voice_case(uint16_t table, uint8_t n)
{
    setup();
    g_ram[PAR_COUNT] = n;
    fw_wr16(&g_f, PAR_TABLE_PTR, table);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 0x5A5A);
    g_ram[FRM_REST_FRAME] = 0x77;                     /* frm_rest_frame[0]: frm_copy_rest_frame's exit A */
    fw_regs r = { 0x90, 0x31, 0x42, 0 };
    par_setup_voice(&g_f, &r);
    uint16_t params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_COPY), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW2_PTR), table + n);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), params);     /* left on P1's target row, as frm_build_rest_frame needs */
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
    CHECK_EQ(g_ncalls, 5);
    check_call(0, ROM_FRM_BUILD_REST_FRAME, (uint8_t)(stride >> 8), 0, 0x42);   /* A = the stride's high byte
                                                                                   ($B3F2), X = 0 (the shift loop) */
    check_buffers(0, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(1, ROM_FRM_COPY_REST_FRAME, (uint8_t)(params >> 8), 9, 0x3B);    /* frm_build_rest_frame's exit */
    check_buffers(1, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(2, ROM_FRM_CLAMP_MARK_SPACE, 0x77, 9, 0xFF);                     /* frm_copy_rest_frame's exit */
    check_buffers(2, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(3, ROM_FRM_COPY_REST_FRAME, 0x0B, 9, 0x0B);                      /* frm_clamp_mark_space's exit */
    check_buffers(3, ISR_FRAME_B, ISR_FRAME_A, 1);
    check_call(4, ROM_FRM_CLAMP_MARK_SPACE, 0x77, 9, 0xFF);
    check_buffers(4, ISR_FRAME_B, ISR_FRAME_A, 1);
    CHECK_EQ(r.a, 0x0B);
    CHECK_EQ(r.x, 9);
    CHECK_EQ(r.y, 0x0B);
    CHECK(!g_f.fault);
}

/* Voice A (N = 86: stride 430 = $01AE, 4N = 344 sets the high byte through ROL) and a RAM table of 255 phonemes
   ($10F0: both par_next_row calls carry into the high byte; 4N = $3FC, + N carries: stride $04FB). */
static void test_setup_voice(void)
{
    setup_voice_case(PAR_VOICE_A, PAR_PHONEMES);
    setup_voice_case(0x10F0, 0xFF);
}

void suite_fw_par(void)
{
```

- In `suite_fw_par`, replace its last two lines (as Task 5 leaves them)

```c
    test_jump_end_marker();
}
```

  with

```c
    test_jump_end_marker();
    test_top_rows();
    test_read_segment();
    test_read_segment_wrap();
    test_next_segment();
    test_plan_tracks();
    test_reset_tracks();
    test_play_start();
    test_setup_voice();
}
```

In `tests/test_fw_cmd.c`:
- Replace

```c
#include "firmware/cmd_run.h"
```

with

```c
#include "firmware/cmd_run.h"
#include "firmware/par_addr.h"
```

- Replace

```c
    if (addr == ROM_PAR_SETUP_VOICE) {
        /* par_setup_voice reads none of A, X, Y and leaves X = 9 and Y = $0B, as the ROM's does
           (frm_build_rest_frame's loop ends at X = 9, frm_clamp_mark_space's at Y = $0B) */
        r->x = 9;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
```

with

```c
    /* the frame routines the C par_setup_voice calls (M4.5) return the registers the ROM ones leave */
    if (addr == ROM_FRM_BUILD_REST_FRAME) {     /* $B428-$B444: Y = $3B, X = 9 (its INX loop), A = par_params_ptr+1 */
        r->a = g_ram[PAR_PARAMS_PTR + 1];
        r->x = 9;
        r->y = 0x3B;
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_FRM_COPY_REST_FRAME) {      /* $B445-$B44F: Y = $FF, A = frm_rest_frame[0]; X untouched */
        r->a = g_ram[FRM_REST_FRAME];
        r->y = 0xFF;
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_FRM_CLAMP_MARK_SPACE) {     /* $B930-$B95C: A = Y = $0B; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
```

- Replace

```c
static void test_apply_pitch(void)
{
```

with

```c
/* Calls i..i+4 are the C par_setup_voice's (M4.4a): frm_build_rest_frame with A = par_param_stride's high byte, X = 0
   and Y = y (the Y handed to par_setup_voice), then frm_copy_rest_frame, frm_clamp_mark_space, frm_copy_rest_frame
   and frm_clamp_mark_space, each with the registers the one before left; and the voice pointers and the stride it
   set from par_count (N) and par_table_ptr. */
static void check_setup_voice(unsigned i, uint8_t y)
{
    uint8_t n = g_ram[PAR_COUNT];
    uint16_t table = fw_rd16(&g_f, PAR_TABLE_PTR), params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    uint8_t rest0 = g_ram[FRM_REST_FRAME];
    check_call(i, ROM_FRM_BUILD_REST_FRAME, (uint8_t)(stride >> 8), 0, y);
    check_call(i + 1, ROM_FRM_COPY_REST_FRAME, (uint8_t)(params >> 8), 9, 0x3B);
    check_call(i + 2, ROM_FRM_CLAMP_MARK_SPACE, rest0, 9, 0xFF);
    check_call(i + 3, ROM_FRM_COPY_REST_FRAME, 0x0B, 9, 0x0B);
    check_call(i + 4, ROM_FRM_CLAMP_MARK_SPACE, rest0, 9, 0xFF);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
}

static void test_apply_pitch(void)
{
```

- In `test_apply_pitch`, replace

```c
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0x90, 0x31, 0x42);   /* A = pros_pitch_centre, as at $A845 */
```

with

```c
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0x42);                 /* the C par_setup_voice: A (pros_pitch_centre) and X are not read */
```

- In `test_apply_speaker`, replace

```c
    CHECK_EQ(count(ROM_PAR_SETUP_VOICE), 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 1, 2);
```

with

```c
    CHECK_EQ(count(ROM_FRM_BUILD_REST_FRAME), 1);
    check_setup_voice(0, 2);                    /* voice A: N = 86, stride 430 */
```

- In `test_set_pitch`, replace

```c
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0xB0, 7, 2);
```

with

```c
    CHECK_EQ(g_ncalls, 5);
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0xB0);   /* the A that cmd_apply_pitch hands to par_setup_voice */
    check_setup_voice(0, 2);
```

- In `test_set_source` **and** in `test_set_speaker` (the same two lines in both), replace

```c
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 4, 0);
```

with

```c
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0);
```

- In `test_set_voice`, replace

```c
        CHECK_EQ(g_ncalls, 1);
        check_call(0, ROM_PAR_SETUP_VOICE, (uint8_t)(k_want[v - 1].pitch << 4), 2, 0xFF);
```

with

```c
        CHECK_EQ(g_ncalls, 5);
        CHECK_EQ(g_ram[PROS_PITCH_CENTRE], (uint8_t)(k_want[v - 1].pitch << 4));
        check_setup_voice(0, 0xFF);
```

- In `test_set_prosody`, replace

```c
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 3, 0);   /* X = cmd_find_back's */
```

with

```c
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0);
```

  and, a few lines further on (the `" @RR12"` case), replace

```c
    cmd_set_prosody(&g_f, 5, '2');
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 1);
```

  with

```c
    cmd_set_prosody(&g_f, 5, '2');
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 5);
```

After these edits `grep -n "ROM_PAR_SETUP_VOICE" tests/test_fw_cmd.c` prints nothing.

What the tests pin (spec §4.4, the part for this task):
- `test_top_rows`: the six rows.
- `test_read_segment`: every case of the target loop. The **`BMI` at $BD85 taken**, which no text reaches: a RAM-built table, since par_row_ptr points at RAM. Also: < 3, the offset added in 8 bits with a negative offset, clamping at 0, at $40–$80 and past $80, and P10 clamped and then scaled by @B3 with `FW_TMP_76`. Then the index step, the old target into `pros_play_pitch`, the duration at index − 1 into `par_frames_left` and `par_seg_target[0]`, the track copies with entry 0 untouched, the 16-bit `pros_seg_count` borrow, and A = `par_boundary[1]`, X = Y = 0, from an entry X of $34.
- `test_read_segment_wrap`: the 16-bit index wrap with the four pointers' high bytes, and the duration read at index 0 through the `DEC`/`INC` of `pros_duration_ptr+1`. A decoy byte proves the read address.
- `test_next_segment`: `par_row_ptr` set first, then the three calls in order. `par_read_segment`'s X = 0 flows into the boundary pass's running index (the jump path's raw targets land in `par_boundary[1..10]`). Then `par_plan_tracks` on what the pass left. The registers out are `par_plan_tracks`'.
- `test_plan_tracks`: differences ≥ $80 (both tests signed), `par_steady` stale on overlap, entry 0 untouched, and A = the last (negative) difference.
- `test_reset_tracks`: the snapshot map, the boundaries, the index and frame count, entry 0 and the byte before `frm_snap` untouched, and A, X and Y kept.
- `test_play_start`: with and without events. Also: the rank through `par_rank_ptr` on the converted voice A table, no microprosody, `pros_play_pitch` stale, the pointers reset, and A = 0, X kept, Y = the phoneme.
- `test_setup_voice`: the five ROM calls in order, with their registers and the buffer pointers and phase set before each; the stride with and without the carries; the dead stores (`par_table_copy`, `par_row2_ptr`); the exit registers.
- `tests/test_fw_cmd.c`: the C `cmd_apply_pitch` (and every handler that reaches it) now runs the C `par_setup_voice`: no `ROM_PAR_SETUP_VOICE` call, its five ROM calls instead, the pointers and the stride it sets, and the registers handed back.

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_par"`
Expected: a compile error: `tests\test_fw_par.c(13): fatal error C1083: Cannot open include file: 'firmware/par_start.h': No such file or directory`.

- [ ] **Step 3: Implement**

`src/firmware/par_addr.h`: replace

```c
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* M4.5; called by par_setup_voice $B404 */
```

with

```c
#define ROM_PAR_SETUP_VOICE        0xB3A4   /* called by cmd_apply_pitch $A845; the C cmd_apply_pitch calls the C */
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* M4.5; called by par_setup_voice $B404 */
```

`src/firmware/cmd_addr.h`: delete the line

```c
#define ROM_PAR_SETUP_VOICE       0xB3A4   /* rebuild the voice set-up and both DSP frames (M4.4/M4.5) */
```

`src/firmware/par_segment.h`: replace the file's last line

```c
#endif
```

with

```c
/* $BD33: the next segment (M4.4a spec 2.2). The playing segment becomes the current one (par_cur_rank,
   par_cur_phoneme = the next ones). par_seg_index += 1; when its low byte wraps to 0, its high byte and the high
   bytes of pros_phoneme_ptr, pros_pitch_ptr, pros_duration_ptr and pros_flags_ptr move on one page, in that order.
   At the index (Y = its low byte, through the pointers, which point into ROM during a @T effect):
   par_next_phoneme, and pros_play_target, scaled by pros_scale_target and moved by pros_microprosody after
   pros_play_pitch takes the old target. par_frames_left = par_seg_target[0] = the duration at index - 1 (at a low
   byte of 0, pros_duration_ptr+1 is decremented for the read and incremented after it, $BD6E/$BD7E).
   par_seg_target[1..10] = par_cur_phoneme's ten targets from the rows at par_row_ptr (which the caller set to
   par_params_ptr), each plus its @u offset cmd_formant_ofs unless it is >= $80 or < 3, then par_clamp, and P10
   scaled by par_scale_voicing; par_row_ptr moves on one parameter block (par_next_param) per target and is then
   set back to par_params_ptr. For P1..P10, par_start_dur = par_next_start_dur and par_start_val = par_line_val =
   par_boundary. pros_seg_count -= 1 in 16 bits.
   r in: not read (X is set to 0 first, A and Y are loaded). r out: A = par_boundary[1], X = 0 (which the boundary
   pass's stores index with), Y = 0, as the ROM leaves them at its RTS ($BDD2). */
void par_read_segment(fw *f, fw_regs *r);  /* $BD33 */

#endif
```

`src/firmware/par_segment.c`:
- Replace

```c
#include "par_segment.h"
```

with

```c
#include "par_segment.h"
#include "pros_play.h"
```

  (`fw_ops.h` is already included there for Task 4's `par_next_row`; this code uses its `fw_inc8`.)

- Append at the end of the file:

```c

/* ---- par_read_segment $BD33 (M4.4a Task 6) ---- */

/* $BD33-$BD50: X = 0; the playing segment becomes the current one (rank, then phoneme, through Y); par_seg_index
   += 1, and when its low byte wraps to 0, its high byte and the four playback pointers' high bytes, in the ROM's
   order (phoneme, pitch, duration, flags). */
static void advance(fw *f, fw_regs *r)
{
    r->x = 0;
    r->y = fw_rd8(f, PAR_NEXT_RANK);
    fw_wr8(f, PAR_CUR_RANK, r->y);
    r->y = fw_rd8(f, PAR_NEXT_PHONEME);
    fw_wr8(f, PAR_CUR_PHONEME, r->y);
    fw_inc8(f, PAR_SEG_INDEX);
    if (fw_rd8(f, PAR_SEG_INDEX) != 0)
        return;
    fw_inc8(f, PAR_SEG_INDEX + 1);
    fw_inc8(f, PROS_PHONEME_PTR + 1);
    fw_inc8(f, PROS_PITCH_PTR + 1);
    fw_inc8(f, PROS_DURATION_PTR + 1);
    fw_inc8(f, PROS_FLAGS_PTR + 1);
}

/* $BD51-$BD69: Y = the index's low byte; pros_play_pitch = the old pros_play_target; par_next_phoneme and
   pros_play_target from (pros_phoneme_ptr),Y and (pros_pitch_ptr),Y; then pros_scale_target and
   pros_microprosody, which keep X and Y. */
static void read_next(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_SEG_INDEX);
    r->a = fw_rd8(f, PROS_PLAY_TARGET);
    fw_wr8(f, PROS_PLAY_PITCH, r->a);
    r->a = fw_ind_y(f, PROS_PHONEME_PTR, r->y);
    fw_wr8(f, PAR_NEXT_PHONEME, r->a);
    r->a = fw_ind_y(f, PROS_PITCH_PTR, r->y);
    fw_wr8(f, PROS_PLAY_TARGET, r->a);
    pros_scale_target(f, r);
    pros_microprosody(f, r);
}

/* $BD6A-$BD7F: the duration at index - 1: at Y = 0 the DEC of pros_duration_ptr+1 turns the DEY's wrap to $FF into
   the byte before the page; (pros_duration_ptr),Y goes to par_frames_left and to par_seg_target[X] (X = 0: never
   read); INX; at Y = $FF the INC puts the pointer back. */
static void read_duration(fw *f, fw_regs *r)
{
    if (r->y == 0)
        fw_wr8(f, PROS_DURATION_PTR + 1, (uint8_t)(fw_rd8(f, PROS_DURATION_PTR + 1) - 1));
    r->y--;
    r->a = fw_ind_y(f, PROS_DURATION_PTR, r->y);
    fw_wr8(f, PAR_FRAMES_LEFT, r->a);
    fw_wr8(f, (uint16_t)(PAR_SEG_TARGET + r->x), r->a);
    r->x++;
    if (r->y == 0xFF)
        fw_inc8(f, PROS_DURATION_PTR + 1);
}

/* $BD80-$BDA3: Y = par_cur_phoneme; for X = 1..10: A = (par_row_ptr),Y, the target; unless it is >= $80 (BMI) or
   < 3 (CMP #3 / BCC), CLC / ADC cmd_formant_ofs[X - 1] ($2013,X; 8 bits); par_clamp; at X = 10 (P10)
   par_scale_voicing; par_seg_target[X] = A; INX; par_next_param, the same row of the next parameter. */
static void read_targets(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_CUR_PHONEME);
    do {
        r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);
        if (!(r->a & 0x80) && r->a >= 3)
            r->a = (uint8_t)(r->a + fw_rd8(f, (uint16_t)(CMD_FORMANT_OFS - 1 + r->x)));
        par_clamp(f, r);
        if (r->x == 10)
            par_scale_voicing(f, r);
        fw_wr8(f, (uint16_t)(PAR_SEG_TARGET + r->x), r->a);
        r->x++;
        par_next_param(f, r);
    } while (r->x != 11);
}

/* $BDA4-$BDC1: par_row_ptr = par_params_ptr; for Y = 10..1 the tracks start from the boundary: par_start_dur[Y] =
   par_next_start_dur[Y], par_start_val[Y] = par_line_val[Y] = par_boundary[Y]. Leaves A = par_boundary[1], Y = 0. */
static void start_tracks(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PAR_PARAMS_PTR);
    fw_wr8(f, PAR_ROW_PTR, r->a);
    r->a = fw_rd8(f, PAR_PARAMS_PTR + 1);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
    for (r->y = 10; r->y != 0; r->y--) {
        r->a = fw_rd8(f, (uint16_t)(PAR_NEXT_START_DUR + r->y));
        fw_wr8(f, (uint16_t)(PAR_START_DUR + r->y), r->a);
        r->a = fw_rd8(f, (uint16_t)(PAR_BOUNDARY + r->y));
        fw_wr8(f, (uint16_t)(PAR_START_VAL + r->y), r->a);
        fw_wr8(f, (uint16_t)(PAR_LINE_VAL + r->y), r->a);
    }
}

/* $BDC2-$BDD2: pros_seg_count -= 1 in 16 bits (LDX low / DEX / CPX #$FF / DEC high / STX low); LDX #0. */
static void count_down(fw *f, fw_regs *r)
{
    r->x = (uint8_t)(fw_rd8(f, PROS_SEG_COUNT) - 1);
    if (r->x == 0xFF)
        fw_wr8(f, PROS_SEG_COUNT + 1, (uint8_t)(fw_rd8(f, PROS_SEG_COUNT + 1) - 1));
    fw_wr8(f, PROS_SEG_COUNT, r->x);
    r->x = 0;
}

void par_read_segment(fw *f, fw_regs *r)
{
    advance(f, r);
    read_next(f, r);
    read_duration(f, r);
    read_targets(f, r);
    start_tracks(f, r);
    count_down(f, r);
}
```

Create `src/firmware/par_start.h`:

```c
#ifndef FW_PAR_START_H
#define FW_PAR_START_H
/* The playback set-up's top level (ROM $B3A4-$B41C, $BAF2-$BB73, $BCF3-$BD06 and $C0D2-$C0FE), decompiled (M4.4a
   spec 2.1, 2.2): the voice set-up, the utterance start, and the per-segment step that the frame loop par_play
   (ROM until M4.4b) calls. Each routine takes the registers at entry in r and leaves in r the A, X and Y that the
   ROM leaves at its RTS. r->p is not used: no caller reads a flag. */
#include "fw.h"

/* $C0D2: for P10..P1 (Y = 10..1): par_sequential = 0 and par_end_val = par_seg_target. par_end_wait =
   par_frames_left - par_end_dur, or 0 when that 8-bit difference is negative (BPL, signed). When par_end_wait -
   par_start_dur is not negative (BMI, signed), par_steady = it and par_sequential = 1; otherwise par_steady keeps
   its old value. r in: not read. r out: A = the last difference or 1, X unchanged, Y = 0. */
void par_plan_tracks(fw *f, fw_regs *r);

/* $BCF3: par_row_ptr = par_params_ptr, then par_read_segment, par_boundary_pass and par_plan_tracks (all C), with
   the registers flowing from one to the next: par_read_segment's X = 0 is the running index of the boundary pass's
   stores. r in: not read. r out: as par_plan_tracks leaves them. */
void par_next_segment(fw *f, fw_regs *r);

/* $BB35: par_seg_index = 0 and par_frames_left = 1. For X = 10..1, par_next_start_dur[X] = 0 and frm_snap[X - 1] =
   frm_rest_frame[frm_snapshot_map[X - 1]]. par_boundary[1..4] = $20 and par_boundary[5..10] = 1. A, X and Y are
   preserved (PHA / PLA), so r is left as it is. */
void par_reset_tracks(fw *f, fw_regs *r);

/* $BAF2: par_reset_tracks and pros_reset_seg_ptrs. pros_play_target = pros_seg_pitch[0], scaled by
   pros_scale_target (no microprosody). par_next_phoneme = par_cur_phoneme = pros_seg_phoneme[0], and par_next_rank
   = par_cur_rank = its rank, (par_rank_ptr),Y. cmd_ev_pos = $FFFF, or cmd_ev_positions[0] when cmd_ev_count is not
   0. cmd_ev_next = pros_pitch_err = 0. r in: X passes through (nothing here writes it). r out: A = 0, X unchanged,
   Y = pros_seg_phoneme[0]. */
void par_play_start(fw *f, fw_regs *r);

/* $B3A4: the voice set-up for the voice table at par_table_ptr with par_count (N) phonemes. par_row_len = N.
   par_table_copy (never read), par_rank_ptr and par_row_ptr = the table. Two par_next_row calls give par_row2_ptr
   (never read) = table + N and par_params_ptr = table + 2N. par_param_stride = 5N in 16 bits. Then the rest frames:
   with isr_play_ptr = isr_frame_a, isr_fill_ptr = isr_frame_b and isr_swap_phase = 2, the ROM frm_build_rest_frame,
   frm_copy_rest_frame and frm_clamp_mark_space; with the two buffers swapped and isr_swap_phase = 1,
   frm_copy_rest_frame and frm_clamp_mark_space again. These M4.5 routines are called through fw_rom_call with the
   registers the ROM has at each JSR.
   r in: Y passes through to frm_build_rest_frame, which does not read it (A and X are written first). r out: the
   registers the last ROM call leaves: A = Y = $0B (frm_clamp_mark_space) and X = 9 (frm_build_rest_frame's). */
void par_setup_voice(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/par_start.c`:

```c
#include "par_start.h"
#include "fw_ops.h"
#include "par_addr.h"
#include "par_boundary.h"
#include "par_segment.h"
#include "pros_play.h"
#include "pros_seg.h"
#include "data/fw_data.h"

#define PAR_BOUNDARY_START_FORMANT  0x20   /* $BB5A: the start boundary of P1-P4 (FN, F1, F2, F3) */
#define PAR_BOUNDARY_START_OTHER    0x01   /* $BB64: the start boundary of P5-P10 */
#define ISR_SWAP_PHASE_FILL_B       2      /* $B401: isr_frame_b is the fill frame */
#define ISR_SWAP_PHASE_FILL_A       1      /* $B413: isr_frame_a is the fill frame */

/* ---- par_plan_tracks $C0D2 ---- */

/* $C0D4-$C0FC, one parameter (Y = 1..10): par_sequential[Y] = 0 and par_end_val[Y] = par_seg_target[Y]. SEC / SBC:
   par_end_wait[Y] = par_frames_left - par_end_dur[Y], or 0 when bit 7 is set (BPL). SEC / SBC again: when
   par_end_wait[Y] - par_start_dur[Y] has bit 7 clear (BMI), par_steady[Y] = it and par_sequential[Y] = 1;
   otherwise par_steady[Y] is not written. */
static void plan_track(fw *f, fw_regs *r)
{
    uint8_t y = r->y;
    r->a = 0;
    fw_wr8(f, (uint16_t)(PAR_SEQUENTIAL + y), r->a);
    r->a = fw_rd8(f, (uint16_t)(PAR_SEG_TARGET + y));
    fw_wr8(f, (uint16_t)(PAR_END_VAL + y), r->a);
    r->a = (uint8_t)(fw_rd8(f, PAR_FRAMES_LEFT) - fw_rd8(f, (uint16_t)(PAR_END_DUR + y)));
    if (r->a & 0x80)
        r->a = 0;
    fw_wr8(f, (uint16_t)(PAR_END_WAIT + y), r->a);
    r->a = (uint8_t)(r->a - fw_rd8(f, (uint16_t)(PAR_START_DUR + y)));
    if (r->a & 0x80)
        return;
    fw_wr8(f, (uint16_t)(PAR_STEADY + y), r->a);
    r->a = 1;
    fw_wr8(f, (uint16_t)(PAR_SEQUENTIAL + y), r->a);
}

void par_plan_tracks(fw *f, fw_regs *r)
{
    for (r->y = 10; r->y != 0; r->y--)   /* LDY #$0A ... DEY / BNE $C0D4 */
        plan_track(f, r);
}

/* ---- par_next_segment $BCF3 ---- */

void par_next_segment(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PAR_PARAMS_PTR);    /* $BCF3-$BCFC: par_row_ptr = par_params_ptr, P1's target row */
    fw_wr8(f, PAR_ROW_PTR, r->a);
    r->a = fw_rd8(f, PAR_PARAMS_PTR + 1);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
    par_read_segment(f, r);              /* $BCFD: leaves X = 0 */
    par_boundary_pass(f, r);             /* $BD00: indexes its stores with that X */
    par_plan_tracks(f, r);               /* $BD03, then RTS $BD06 */
}

/* ---- par_reset_tracks $BB35 ---- */

/* $BB3A-$BB46: par_seg_index = 0 (both bytes) and par_frames_left = 1, so the first frame starts a segment. */
static void reset_index(fw *f)
{
    fw_wr8(f, PAR_SEG_INDEX, 0);
    fw_wr8(f, PAR_SEG_INDEX + 1, 0);
    fw_wr8(f, PAR_FRAMES_LEFT, 1);
}

/* $BB47-$BB59: for X = 10..1: par_next_start_dur[X] = 0 ($171C,X); Y = frm_snapshot_map[X - 1] ($B772,X);
   frm_snap[X - 1] ($16CC,X) = frm_rest_frame[Y]. */
static void reset_snapshot(fw *f)
{
    for (uint8_t x = 10; x != 0; x--) {
        fw_wr8(f, (uint16_t)(PAR_NEXT_START_DUR + x), 0);
        uint8_t y = fw_rom8(f, (uint16_t)(FRM_SNAPSHOT_MAP - 1 + x));
        fw_wr8(f, (uint16_t)(FRM_SNAP - 1 + x), fw_rd8(f, (uint16_t)(FRM_REST_FRAME + y)));
    }
}

/* $BB5A-$BB6D: par_boundary[4..1] = $20 ($16EC,Y), then par_boundary[10..5] = 1 ($16F0,Y for Y = 6..1). */
static void reset_boundaries(fw *f)
{
    for (uint8_t y = 4; y != 0; y--)
        fw_wr8(f, (uint16_t)(PAR_BOUNDARY + y), PAR_BOUNDARY_START_FORMANT);
    for (uint8_t y = 6; y != 0; y--)
        fw_wr8(f, (uint16_t)(PAR_BOUNDARY + 4 + y), PAR_BOUNDARY_START_OTHER);
}

void par_reset_tracks(fw *f, fw_regs *r)
{
    (void)r;                             /* PHA / TXA / PHA / TYA / PHA ... PLA / TAY / PLA / TAX / PLA */
    reset_index(f);
    reset_snapshot(f);
    reset_boundaries(f);
}

/* ---- par_play_start $BAF2 ---- */

/* $BAF8-$BB00: pros_play_target = pros_seg_pitch[0] ($0A00, absolute), then pros_scale_target (A = the entry Y;
   X and Y kept). */
static void first_pitch(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PROS_SEG_PITCH);
    fw_wr8(f, PROS_PLAY_TARGET, r->a);
    pros_scale_target(f, r);
}

/* $BB01-$BB12: par_next_phoneme = par_cur_phoneme = pros_seg_phoneme[0] ($0200, absolute); TAY; both ranks =
   (par_rank_ptr),Y, from the voice table's rank row. */
static void first_phoneme(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PROS_SEG_PHONEME);
    fw_wr8(f, PAR_NEXT_PHONEME, r->a);
    fw_wr8(f, PAR_CUR_PHONEME, r->a);
    r->y = r->a;
    r->a = fw_ind_y(f, PAR_RANK_PTR, r->y);
    fw_wr8(f, PAR_NEXT_RANK, r->a);
    fw_wr8(f, PAR_CUR_RANK, r->a);
}

/* $BB13-$BB34: cmd_ev_pos = $FFFF; when cmd_ev_count is not 0, cmd_ev_pos = cmd_ev_positions[0], low byte then
   high byte. Then A = 0: cmd_ev_next = 0 and pros_pitch_err = 0 (read only by the unreferenced $C131). */
static void first_event(fw *f, fw_regs *r)
{
    r->a = 0xFF;
    fw_wr8(f, CMD_EV_POS, r->a);
    fw_wr8(f, CMD_EV_POS + 1, r->a);
    r->a = fw_rd8(f, CMD_EV_COUNT);
    if (r->a != 0) {
        r->a = fw_rd8(f, CMD_EV_POSITIONS);
        fw_wr8(f, CMD_EV_POS, r->a);
        r->a = fw_rd8(f, CMD_EV_POSITIONS + 1);
        fw_wr8(f, CMD_EV_POS + 1, r->a);
    }
    r->a = 0;
    fw_wr8(f, CMD_EV_NEXT, r->a);
    fw_wr8(f, PROS_PITCH_ERR, r->a);
}

void par_play_start(fw *f, fw_regs *r)
{
    par_reset_tracks(f, r);              /* $BAF2: keeps A, X and Y */
    pros_reset_seg_ptrs(f);              /* $BAF5 */
    r->a = (uint8_t)(PROS_SEG_FLAGS >> 8);   /* its last LDA #$0E; X and Y untouched */
    first_pitch(f, r);
    first_phoneme(f, r);
    first_event(f, r);
}

/* ---- par_setup_voice $B3A4 ---- */

/* $B3A4-$B3BD: par_row_len = par_count. par_table_ptr's low byte, then its high byte, into par_table_copy,
   par_rank_ptr and par_row_ptr (each byte in the ROM's store order). */
static void voice_pointers(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, PAR_COUNT);
    fw_wr8(f, PAR_ROW_LEN, r->a);
    r->a = fw_rd8(f, PAR_TABLE_PTR);
    fw_wr8(f, PAR_TABLE_COPY, r->a);
    fw_wr8(f, PAR_RANK_PTR, r->a);
    fw_wr8(f, PAR_ROW_PTR, r->a);
    r->a = fw_rd8(f, PAR_TABLE_PTR + 1);
    fw_wr8(f, PAR_TABLE_COPY + 1, r->a);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
    fw_wr8(f, PAR_RANK_PTR + 1, r->a);
}

/* $B3BE-$B3D1: par_next_row (A = the new low byte): par_row2_ptr = table + N. par_next_row again: par_params_ptr =
   table + 2N, P1's target row. par_row_ptr is left there. */
static void row_pointers(fw *f, fw_regs *r)
{
    par_next_row(f, r);
    fw_wr8(f, PAR_ROW2_PTR, r->a);
    r->a = fw_rd8(f, PAR_ROW_PTR + 1);
    fw_wr8(f, PAR_ROW2_PTR + 1, r->a);
    par_next_row(f, r);
    fw_wr8(f, PAR_PARAMS_PTR, r->a);
    r->a = fw_rd8(f, PAR_ROW_PTR + 1);
    fw_wr8(f, PAR_PARAMS_PTR + 1, r->a);
}

/* $B3D2-$B3F4: par_param_stride = 5N. AND #0: both bytes 0 (the low byte's store is overwritten below). A = N, then
   X = 2 times ASL A / ROL the high byte: 4N, its two top bits shifted into the high byte. CLC / ADC N gives the low
   byte, and the high byte gets ADC #0 with that carry. Leaves A = the high byte and X = 0. */
static void param_stride(fw *f, fw_regs *r)
{
    r->a = 0;
    fw_wr8(f, PAR_PARAM_STRIDE, r->a);
    fw_wr8(f, PAR_PARAM_STRIDE + 1, r->a);
    r->a = fw_rd8(f, PAR_ROW_LEN);
    for (r->x = 2; r->x != 0; r->x--) {
        uint8_t c = r->a >> 7;
        r->a = (uint8_t)(r->a << 1);
        fw_wr8(f, PAR_PARAM_STRIDE + 1, (uint8_t)(fw_rd8(f, PAR_PARAM_STRIDE + 1) << 1 | c));
    }
    uint8_t c = 0;
    r->a = fw_adc(r->a, fw_rd8(f, PAR_ROW_LEN), &c);
    fw_wr8(f, PAR_PARAM_STRIDE, r->a);
    r->a = fw_adc(fw_rd8(f, PAR_PARAM_STRIDE + 1), 0, &c);
    fw_wr8(f, PAR_PARAM_STRIDE + 1, r->a);
}

/* $B3F5-$B40C: LDM isr_play_ptr = isr_frame_a (high byte first), isr_fill_ptr = isr_frame_b, isr_swap_phase = 2.
   Then the rest frame is built at frm_rest_frame, copied to the fill frame, and its b11 clamped. */
static void first_rest_frame(fw *f, fw_regs *r)
{
    fw_wr8(f, ISR_PLAY_PTR + 1, ISR_FRAME_A >> 8);
    fw_wr8(f, ISR_PLAY_PTR, ISR_FRAME_A & 0xFF);
    fw_wr8(f, ISR_FILL_PTR + 1, ISR_FRAME_B >> 8);
    fw_wr8(f, ISR_FILL_PTR, ISR_FRAME_B & 0xFF);
    fw_wr8(f, ISR_SWAP_PHASE, ISR_SWAP_PHASE_FILL_B);
    fw_rom_call(f, ROM_FRM_BUILD_REST_FRAME, r, FW_ROM_CALL_BUDGET);    /* returns X = 9 */
    fw_rom_call(f, ROM_FRM_COPY_REST_FRAME, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_FRM_CLAMP_MARK_SPACE, r, FW_ROM_CALL_BUDGET);
}

/* $B40D-$B41B: LDM the low bytes swapped (isr_play_ptr = isr_frame_b, isr_fill_ptr = isr_frame_a) and
   isr_swap_phase = 1. Then the rest frame is copied to the other frame, and its b11 clamped. */
static void second_rest_frame(fw *f, fw_regs *r)
{
    fw_wr8(f, ISR_PLAY_PTR, ISR_FRAME_B & 0xFF);
    fw_wr8(f, ISR_FILL_PTR, ISR_FRAME_A & 0xFF);
    fw_wr8(f, ISR_SWAP_PHASE, ISR_SWAP_PHASE_FILL_A);
    fw_rom_call(f, ROM_FRM_COPY_REST_FRAME, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_FRM_CLAMP_MARK_SPACE, r, FW_ROM_CALL_BUDGET);    /* leaves A = Y = $0B */
}

void par_setup_voice(fw *f, fw_regs *r)
{
    voice_pointers(f, r);
    row_pointers(f, r);
    param_stride(f, r);
    first_rest_frame(f, r);
    second_rest_frame(f, r);
}
```

`src/firmware/cmd_apply.c`:
- Replace

```c
#include "cmd_apply.h"
#include "cmd_addr.h"
#include "cmd_match.h"
```

with

```c
#include "cmd_apply.h"
#include "cmd_match.h"
#include "par_start.h"
```

- Replace

```c
    fw_regs r = { a, *x, *y, 0 };
    fw_rom_call(f, ROM_PAR_SETUP_VOICE, &r, FW_ROM_CALL_BUDGET);
```

with

```c
    fw_regs r = { a, *x, *y, 0 };
    par_setup_voice(f, &r);                     /* $A845: the C (M4.4a); it leaves X = 9 and Y = $0B */
```

`src/firmware/cmd_apply.h`: replace

```c
/* $A832: cmd_source_prosody = cmd_source * 16 | cmd_prosody, pros_pitch_centre = cmd_pitch * 16, then the ROM
   par_setup_voice (M4.4) rebuilds the voice set-up, with A = pros_pitch_centre, X = *x and Y = *y (it reads
```

with

```c
/* $A832: cmd_source_prosody = cmd_source * 16 | cmd_prosody, pros_pitch_centre = cmd_pitch * 16, then the C
   par_setup_voice (M4.4a) rebuilds the voice set-up, with A = pros_pitch_centre, X = *x and Y = *y (it reads
```

`src/engine/thunks.c`. The functions, `THUNK` lines and rows go in address order among Task 4's and Task 5's: each function is inserted by replacing the end of the function before it.

- Replace

```c
#include "firmware/par_boundary.h"
```

with

```c
#include "firmware/par_boundary.h"
#include "firmware/par_start.h"
```

- After the end of `tn_convert_clause_regs`, that is, replace

```c
static void tn_convert_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    tn_convert_clause(f);
}
```

with

```c
static void tn_convert_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    tn_convert_clause(f);
}

/* par_setup_voice $B3A4: A, X, Y in; none is read (Y reaches frm_build_rest_frame, whose first instruction is
   LDY #$14 at $B41D). A = $0B, X = 9 and Y = $0B out, as its ROM calls leave them: frm_build_rest_frame's loop ends
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954). The C's ROM calls
   overwrite the machine's registers, so all three are set from the C. Its one ROM caller, cmd_apply_pitch ($A845),
   restores A (PLA at $A848) and returns; X and Y reach RAM: cmd_apply_speaker ($A798) calls cmd_apply_speed, which
   keeps A and Y and does not touch X, and after boot_reset's second call ($A675) bank_load_ramcode stores them
   (STY $78, STX $76 at $A601/$A603). No caller reads a flag. The C cmd_apply_pitch calls the C directly, so the
   hook fires only while the ROM cmd_apply_pitch runs (its "alone" mode). Declared: A, X, Y. */
static void par_setup_voice_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_setup_voice(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace the end of Task 4's `pros_microprosody_regs`

```c
    pros_microprosody(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    pros_microprosody(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_play_start $BAF2: A, X, Y in; none is read (X passes through untouched, A and Y are loaded). A = 0
   ($BB2C), X = the entry X, Y = pros_seg_phoneme[0] ($BB0A TAY) out. Its one ROM caller, boot_play_body ($B2B6),
   calls isr_speech_start next, which writes X ($B323 LDX #0) and A ($B32B LDA) before reading them; Y is not read
   before par_play's LDY pros_seg_count ($B8B1). No caller reads a flag. It fires in "all" mode (boot_loop_play
   stays ROM until M4.4b). Declared: A, X, Y. */
static void par_play_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_play_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_reset_tracks $BB35: A, X and Y preserved (PHA / TXA / PHA / TYA / PHA, then PLA / TAY / PLA / TAX / PLA at
   $BB6E-$BB72); no ROM call in the C, so the thunk leaves all three. Its one ROM caller, par_play_start ($BAF2),
   calls pros_reset_seg_ptrs next (A = $0E, X and Y untouched); then pros_scale_target pushes and restores X and Y
   ($BDD3-$BDD6, $BE48-$BE4B), and X leaves par_play_start as its exit X while Y is replaced at $BB0A (TAY). No
   caller reads a flag (JSR follows). Declared: A, X, Y. */
static void par_reset_tracks_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_reset_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_segment $BCF3: A, X, Y in; none is read (A is loaded first, X is set to 0 by par_read_segment, Y is
   loaded). A, X and Y out as par_plan_tracks leaves them (A = its last result or 1, X = what the boundary pass
   left, Y = 0). Its one ROM caller, par_play ($B900), loads A next ($B903 LDA par_frames_left); then either
   $B8B1 LDY, or isr_par_ready (which preserves A, X and Y) and par_step_tracks, which loads X and Y before reading
   them. No caller reads a flag. It fires in "all" mode (par_play stays ROM until M4.4b). Declared: A, X, Y. */
static void par_next_segment_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_next_segment(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace the end of Task 4's `par_clamp_regs`

```c
    par_clamp(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    par_clamp(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_read_segment $BD33: A, X, Y in; none is read (LDX #0 first). A = par_boundary[1] (the copy loop's last LDA),
   X = 0 ($BDD0 LDX #0) and Y = 0 out. Its one ROM caller, par_next_segment ($BCFD), calls par_boundary_pass next,
   which loads A and Y first ($BE6A LDY, $BE6D LDA) and passes X on: the stores of par_boundary_jump
   (par_store_boundary, STA $16ED,X) and the TXA / PHA of the dominance loops ($BEF3, $BF38) read it. No caller
   reads a flag. Declared: A, X, Y. */
static void par_read_segment_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_read_segment(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace the end of Task 5's `par_boundary_value_regs`

```c
    par_boundary_value(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    par_boundary_value(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_plan_tracks $C0D2: A, X, Y in; none is read (LDY #$0A first, A loaded, X untouched). A = the last
   difference or 1, X = the entry X, Y = 0 out. Its one ROM caller, par_next_segment ($BD03), returns at once
   ($BD06 RTS), so what par_next_segment's caller reads applies: nothing. No caller reads a flag. Declared:
   A, X, Y. */
static void par_plan_tracks_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_plan_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- The `THUNK` lines: replace

```c
THUNK(tn_convert_clause)
THUNK(pros_microprosody)
```

  with

```c
THUNK(tn_convert_clause)
THUNK(par_setup_voice)
THUNK(pros_microprosody)
THUNK(par_play_start)
THUNK(par_reset_tracks)
THUNK(par_next_segment)
```

  replace

```c
THUNK(par_clamp)
```

  with

```c
THUNK(par_clamp)
THUNK(par_read_segment)
```

  and replace

```c
THUNK(par_boundary_value)
```

  with

```c
THUNK(par_boundary_value)
THUNK(par_plan_tracks)
```

- The rows: replace

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
```

with

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
```

  Replace

```c
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
```

  with

```c
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
    { "par_play_start",     0xBAF2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play_start },
    { "par_reset_tracks",   0xBB35, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_reset_tracks },
    { "par_next_segment",   0xBCF3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_next_segment },
```

  replace

```c
    { "par_clamp",          0xBD24, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_clamp },
```

  with

```c
    { "par_clamp",          0xBD24, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_clamp },
    { "par_read_segment",   0xBD33, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_read_segment },
```

  and replace

```c
    { "par_boundary_value", 0xC039, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_value },
```

  with

```c
    { "par_boundary_value", 0xC039, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_value },
    { "par_plan_tracks",    0xC0D2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_plan_tracks },
```

The table then holds 48 rows in address order, from `cmd_apply_speaker` $A78C to `ph_lengthen_count` $D43E: … $B1E6, $B3A4, $B983, $BAF2, $BB35, $BCF3, $BD07, $BD14, $BD24, $BD33, $BDD3, $BE6A, $BE94, $BEB5, $BEE2, $BF27, $BF6C, $BFC3, $C039, $C0D2, $C29C, $C520, … `DIFF_MAX` (48 since Task 1) is exactly the row count. No new `.c` basename clashes: `par_start.c` is new under `src/` and `tests/`.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump side by side with the C, block by block (each static helper names its range):
- `rom_BD33_par_read_segment.txt` against `par_segment.c`:
  - `advance` ($BD33–$BD50): `LDX #0`, then `LDY`/`STY` for the rank, then the phoneme. `INC par_seg_index` / `BNE`. On the wrap, the five `INC`s in the order index+1, $23, $27, $25, $29 (phoneme, pitch, duration, flags).
  - `read_next` ($BD51–$BD69): `LDY` the index's low byte; `LDA pros_play_target` / `STA pros_play_pitch`; `LDA (0x22),Y` → `par_next_phoneme`; `LDA (0x26),Y` → `pros_play_target`; `JSR pros_scale_target`; `JSR pros_microprosody`.
  - `read_duration` ($BD6A–$BD7F): `CPY #0` / `BNE` / `DEC $25`; `DEY`; `LDA (0x24),Y` → `par_frames_left` and `$17BF,X`; `INX`; `CPY #$FF` / `BNE` / `INC $25`.
  - `read_targets` ($BD80–$BDA3): `LDY par_cur_phoneme` once. The loop: `LDA (0x32),Y`; `BMI` and `CMP #3` / `BCC` skip the `CLC` / `ADC $2013,X`; `JSR par_clamp`; `CPX #$0A` / `BNE` around `JSR par_scale_voicing`; `STA $17BF,X`; `INX`; `JSR par_next_param`; `CPX #$0B` / `BNE`.
  - `start_tracks` ($BDA4–$BDC1): the two pointer bytes; `LDY #$0A`; the copies `$171C,Y` → `$172C,Y` and `$16EC,Y` → `$16FC,Y` and `$170C,Y`; `DEY` / `BNE`.
  - `count_down` ($BDC2–$BDD2): `LDX` low, `DEX`, `CPX #$FF` / `BNE` / `DEC` high, `STX` low, `LDX #0`, `RTS`.
- `rom_C0D2_par_plan_tracks.txt` against `plan_track` and `par_plan_tracks`, instruction for instruction ($C0D2 `LDY #$0A` … $C0FB `DEY` / `BNE` / `RTS`). The `BPL` at $C0E6 and the `BMI` at $C0F1 are the `& 0x80` tests. On the `BMI` path nothing is stored.
- `rom_BCF3_par_next_segment.txt` against `par_next_segment`: the two pointer bytes, then the three `JSR`s, then `RTS`.
- `rom_BB35_par_reset_tracks.txt` against `reset_index` ($BB3A–$BB46), `reset_snapshot` ($BB47–$BB59: `LDA #0` / `STA $171C,X`, `LDY $B772,X`, `LDA $2000,Y`, `STA $16CC,X`, `DEX` / `BNE`) and `reset_boundaries` ($BB5A–$BB6D). The `PHA`/`PLA` frame is the `(void)r`.
- `rom_BAF2_par_play_start.txt` against `par_play_start`, `first_pitch` ($BAF8–$BB00), `first_phoneme` ($BB01–$BB12) and `first_event` ($BB13–$BB34). Check `rom_CA71_pros_reset_seg_ptrs.txt` for the A = $0E.
- `rom_B3A4_par_setup_voice.txt` against `voice_pointers` ($B3A4–$B3BD, the store order $17EB, $2E, $32, then $17EC, $33, $2F), `row_pointers` ($B3BE–$B3D1), `param_stride` ($B3D2–$B3F4), `first_rest_frame` ($B3F5–$B40C, the five `LDM`s in order, then the three `JSR`s) and `second_rest_frame` ($B40D–$B41B, three `LDM`s, two `JSR`s, `RTS`).
- For the exit registers of the ROM calls (the mocks), read `rom_B41D_frm_build_rest_frame.txt`, `rom_B445_frm_copy_rest_frame.txt` and `rom_B930_frm_clamp_mark_space.txt`.
- For the thunk comments, read the callers:
  - `rom_A832_cmd_apply_pitch.txt` ($A845–$A849), `rom_A78C_cmd_apply_speaker.txt`, `rom_A84A_cmd_apply_speed.txt`, `rom_A61C_boot_reset.txt` ($A675), `rom_A600_bank_load_ramcode.txt` ($A601/$A603);
  - `rom_A7D4_boot_loop_play.txt` ($B2B6–$B2B9) and `rom_B323_isr_speech_start.txt` ($B323, $B32B);
  - `rom_B877_par_play.txt` ($B900–$B90B, $B8B1) and `rom_C0FF_par_step_tracks.txt` ($C0FF).

Confirm the callers by scanning the operands:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xBD33,0xC0D2,0xBCF3,0xBB35,0xBAF2,0xB3A4) for op in (0x20,0x4C)]"
```

Expected:

```
0xbd33 0x20 ['0xbcfd']
0xbd33 0x4c []
0xc0d2 0x20 ['0xbd03']
0xc0d2 0x4c []
0xbcf3 0x20 ['0xb900']
0xbcf3 0x4c []
0xbb35 0x20 ['0xbaf2']
0xbb35 0x4c []
0xbaf2 0x20 ['0xb2b6']
0xbaf2 0x4c []
0xb3a4 0x20 ['0xa845']
0xb3a4 0x4c []
```

and no relative branch reaches them:

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xBD33,0xC0D2,0xBCF3,0xBB35,0xBAF2,0xB3A4):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected: `0xbd33 []`, `0xc0d2 []`, `0xbcf3 []`, `0xbb35 []`, `0xbaf2 []`, `0xb3a4 []`.

Then confirm spec §4.5 criterion 6:

```bash
grep -rn "ROM_PAR_SETUP_VOICE" src
```

Expected: one match, `src/firmware/par_addr.h` (the `#define`).

- [ ] **Step 5: Run the tests**

Run, in order:
- `cmd //c ".\build.bat test fw_par"`
- `cmd //c ".\build.bat test fw_cmd"`
- `cmd //c ".\build.bat test"` (the full suite; it runs `diff_lts` and `golden_hybrid` and prints their lines below; no separate filtered runs)

Expected:
- `suite_fw_par` ok, then `1 suite(s), 0 failure(s), 0 skip(s)`; the same for `suite_fw_cmd`.
- `diff_lts`: 48 lines, every N > 0, 0 mismatches. The proof run printed:

```
  cmd_apply_speaker    101 calls checked
  cmd_run_due          5199 calls checked
  cmd_scan_clause      225 calls checked
  cmd_match_table      2895 calls checked
  lts_affix_next       4152 calls checked
  lts_affix_prev       1476 calls checked
  lts_sym_vowel        1209 calls checked
  lts_match_symbol     15488 calls checked
  tn_say_char          19 calls checked
  lts_match_list       11671 calls checked
  tn_rule_skip_text    31490 calls checked
  tn_rule_skip_field   13841 calls checked
  tn_append_phonetic   893 calls checked
  lts_convert_char     884 calls checked
  tn_convert_clause    224 calls checked
  par_setup_voice      113 calls checked
  pros_microprosody    3391 calls checked
  par_play_start       173 calls checked
  par_reset_tracks     173 calls checked
  par_next_segment     3391 calls checked
  par_next_row         40696 calls checked
  par_next_param       30676 calls checked
  par_clamp            39664 calls checked
  par_read_segment     3391 calls checked
  pros_scale_target    3564 calls checked
  par_boundary_pass    3391 calls checked
  par_boundary_jump    1093 calls checked
  par_store_boundary   23447 calls checked
  par_boundary_cur_dom 833 calls checked
  par_boundary_next_dom 1465 calls checked
  par_read_dominance   15982 calls checked
  par_scale_voicing    5689 calls checked
  par_boundary_value   15982 calls checked
  par_plan_tracks      3391 calls checked
  par_scale_duration   25782 calls checked
  cmd_hex_value        144 calls checked
  ph_append_phrase_pause 360 calls checked
  ph_append_pause      820 calls checked
  pros_append_segment  2307 calls checked
  pros_reset_seg_ptrs  398 calls checked
  pros_make_segments   225 calls checked
  pros_mark_stress     225 calls checked
  pros_mark_phrases    225 calls checked
  pros_store_phrase_len 407 calls checked
  pros_phrase_gen      225 calls checked
  pros_declination     407 calls checked
  pros_declination_step 407 calls checked
  ph_lengthen_count    1139 calls checked
  1 mid-clause flush calls skipped (golden_hybrid covers them)
  2 unwound calls compared
```

  - The six new lines are `par_setup_voice 113`, `par_play_start 173`, `par_reset_tracks 173`, `par_next_segment 3391`, `par_read_segment 3391` and `par_plan_tracks 3391`. `par_read_segment` and `par_plan_tracks` equal `par_next_segment` (one each per segment start, sampled alike), and `par_reset_tracks` equals `par_play_start`.
  - The other 42 lines are as Task 5 left them. The `1 mid-clause flush` and `2 unwound` lines are unchanged.
- `golden_hybrid`: `45 corpus files x 49 modes (all, then each routine alone)`, all identical.
- The full run:
  - all 32 suites ok (`32 suite(s), 0 failure(s), 0 skip(s)`), with no warnings and the firmware boundary check passing;
  - `firmware_data` at `47692 converted bytes compared with the ROM, 0 differ`;
  - `suite_synth` printing `corpus peak plain 20522 (-4.1 dBFS)` and `corpus peak b11 16038 (-6.2 dBFS)` (as Task 2 left them).
  - The proof runs took between 4 min 37 s and 5 min 40 s, the build included.

If `diff_lts` reports a mismatch, find the root cause in the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

Then confirm spec criterion 5: the CLI's hybrid output must equal the rom engine's for every new text. `effects` is added because its @T effects run `par_read_segment` on pointers into `cmd_sound_data` and read past each effect's arrays. Each text is read from its corpus file. The outputs go to `build/` (git-ignored), always with `-o`:

```bash
SP=build/m44a_cli
mkdir -p "$SP"
for t in pitch_extremes voicing_levels fast_effect effects; do
  ./build/apollo2.exe --engine rom --text-file tests/corpus/$t.txt -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file tests/corpus/$t.txt -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done
rm -rf "$SP"
```

Expected: four lines, `pitch_extremes same`, `voicing_levels same`, `fast_effect same` and `effects same`, with no `failed` or `DIFFERS` line. (The WAV sizes in the proof run were 42444, 103644, 13444 and 85644 bytes.)

- [ ] **Step 6: Commit**

```bash
git add src/firmware/par_start.h src/firmware/par_start.c src/firmware/par_segment.h src/firmware/par_segment.c src/firmware/par_addr.h src/firmware/cmd_addr.h src/firmware/cmd_apply.c src/firmware/cmd_apply.h src/engine/thunks.c tests/test_fw_par.c tests/test_fw_cmd.c
git commit -m "par_read_segment, par_plan_tracks, par_next_segment, par_reset_tracks, par_play_start and par_setup_voice in C, with their hooks; the C cmd_apply_pitch calls the C par_setup_voice directly (M4.4a 2.2-2.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files (none of this task's).

---

### Task 7: Parent spec rev 12, M4.4a status and README (criterion 8)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 12**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 12)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 12)* Revision 12 records milestone 4.4a: the playback set-up in C (segment reading, pitch scaling, the boundary and dominance pass, track planning, the voice set-up) and the voice and @T tables converted (design: docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.3b done**`). Replace its last sentence, `Next: M4.4, the parameter tracks (playback).`, with:

   `Next: M4.4, the parameter tracks (playback), in two plans (M4.4a, M4.4b). *(rev 12)* **M4.4a done** <DATE>: the playback set-up runs as C (par_play_start, par_next_segment and the 17 routines under them, and par_setup_voice; 48 routines hooked in all), and the C cmd_apply_pitch calls the C par_setup_voice directly; the voice tables, par_target_bias, frm_snapshot_map and the @T tables are converted (47,692 bytes in all); three new corpus goldens (rom engine) reach the pitch extremes, the @B voicing levels and a speed-mapped duration in a @T effect; diff_lts and golden_hybrid (45 files x 49 modes) pass. Next: M4.4b, the frame loop (par_play, the track steppers, the command events, boot_loop_play).`

   Replace `<DATE>` with the date of your commit (`date +%F`).

- [ ] **Step 2: M4.4a spec status and As built**

In `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-07-m4-4a-playback-setup.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the ROM is more precise than, or differs from, the sections above. The ROM wins; ``ghidra740/`` is not edited.` (one pair of backquotes around `ghidra740/`). Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–6 of the plan, keeping only those that contradict or sharpen the spec (among them the order and names of the voice tables' rows 3 and 4, if Task 3 named them differently from §3);
- the code:
  - the thunk outputs as finally declared for the 20 new hooks (read the rows and comments in `src/engine/thunks.c`) and why;
  - the C file layout (`par_segment.c`, `pros_play.c`, `par_boundary.c`, `par_start.c`, `par_addr.h`, `fw_ops.h`) and the static helpers of the larger routines;
  - the RAM names actually added to `fw.h`, if they differ from §2.6;
  - the converted data's C form (types and files), if it differs from §3;
- the corpus texts as finally committed, if any differs from §4.1.

Every bullet must be true of the committed code and the dumps. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md`. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
the segment appenders and the declination (M4.3a), and the stress and phrase passes with pros_make_segments
(M4.3b), so all of prosody before playback; the rest still runs on the emulated CPU.
```

with:

```
the segment appenders and the declination (M4.3a), the stress and phrase passes with pros_make_segments
(M4.3b), so all of prosody before playback, and the playback set-up: segment reading, pitch scaling, the
boundary and dominance pass, track planning and the voice set-up (M4.4a); the rest still runs on the emulated CPU.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all 32 suites ok.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md README.md
git commit -m "Spec rev 12: milestone 4.4a done (the playback set-up in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
