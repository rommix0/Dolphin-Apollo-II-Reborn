# M4.1 Text Normalisation in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the clause-to-phonetic-text driver `tn_convert_clause` ($B1E6) and the phonetic append `tn_append_phonetic` ($B154) to C, convert the five length-prefixed phonetic strings to readable C, and prove both routines with the live differential test and the hybrid goldens. That includes a new long-clause golden that exercises the mid-clause flush.

**Architecture:**
- Two new C files hold the routines: `src/firmware/tn_convert.c` (new) and `src/firmware/tn_rules.c` (extended). Both run on the firmware's RAM image through the M4.0 `fw` context.
- ROM routines that are not ported yet (`isr_par_ready`, `cmd_run_due`, `pros_make_segments`, `boot_loop_play`) are reached through `fw_rom_call`, which now takes a cycle budget per call.
- `src/engine/thunks.c` hooks the two routines, bringing the table to 11.
- `diff_lts` compares every non-flushing call with the ROM. `golden_hybrid` checks 27 files × 12 modes and asserts that the flush path ran.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`) and Python 3 (`tools/extract_tables.py`).

**Spec:** `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md` (M4.1 spec). Its foundation is `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (including §8 "As built"). The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 7).

## Global Constraints

**Build and run**
- Build and test with `cmd //c ".\build.bat test [SUITE_FILTER]"` from the repo root, using the Bash tool (Git Bash).
- In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional). Fix warnings in code. Never add `/wd` flags or pragmas; the only pragma allowed is the existing 4611 one in `thunks.c`.
- Every `.c` file under `src/` and `tests/` must have a **unique basename**, because `build.bat` puts all objects in `build\`.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers.
- Audio stays 10 kHz native, with no resampling.

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C (strings, flags, list references).
- Stage files by explicit path only. Never use `git add -A` or `git add .`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking the C against the ROM (git-ignored, read-only) are in the main checkout: `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt`.

**Correctness rules**
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The single new golden, `long_clause.frm`, is generated once by the rom engine in Task 1. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.1 spec §2):
  - the `$75` and `$78` scratch writes;
  - the phonetic-mode `tn_key_pos` lag;
  - the X flow through ROM calls;
  - first-letter spelling depending on `tn_key_pos ≠ 0` (inside `lts_convert_char`, unchanged).

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## File Map

| File | Task | Responsibility |
|---|---|---|
| `tests/corpus/long_clause.txt`, `tests/golden/long_clause.frm`, `tests/corpus_list.h` | 1 | the long-clause corpus text and its rom-engine golden |
| `src/firmware/fw.h`, `src/firmware/fw.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `src/firmware/tn_rules.c`, `tests/test_fw.c`, `tests/test_fw_lts.c` | 2 | ROM-call budget per call |
| `tools/extract_tables.py`, `src/firmware/data/phonetic_text_data.c`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c` | 3 | the five phonetic strings as readable C |
| `src/firmware/tn_convert.h`, `src/firmware/tn_convert.c`, `src/firmware/fw.h`, `src/firmware/fw.c`, `tests/test_fw_tn.c`, `tests/suites.h` | 4 | `tn_convert_clause` in C, with unit tests |
| `src/engine/thunks.c`, `src/engine/hybrid_engine.h`, `src/engine/hybrid_engine.c`, `tests/test_diff_lts.c`, `tests/test_golden_hybrid.c` | 5 | the `tn_convert_clause` thunk, flush skip in `diff_lts`, flush assertion in `golden_hybrid` |
| `src/firmware/tn_rules.h`, `src/firmware/tn_rules.c`, `src/firmware/lts.c`, `src/engine/thunks.c`, `tests/test_fw_tn.c`, `tests/test_fw_lts.c` | 6 | `tn_append_phonetic` in C, and its thunk |
| `docs/superpowers/specs/*.md`, `README.md` | 7 | parent spec rev 8, M4.1 status, README |

---

### Task 1: The long-clause corpus text and its golden (spec §5.3)

**Files:**
- Create: `tests/corpus/long_clause.txt`, and `tests/golden/long_clause.frm` (generated)
- Modify: `tests/corpus_list.h`

**Interfaces:**
- Produces: the corpus name `"long_clause"`, which every `CORPUS_NAMES` user picks up: `suite_golden`, `diff_lts`, `golden_hybrid`, `suite_synth`'s corpus peak, and `apollo2_coverage`. Tasks 5's assertions refer to it by that name.

The text was checked while writing this plan: 60 single digits separated by spaces form one clause whose phonetic text passes `$FA`. `apollo2_coverage` shows `$B1FF` executed. A long sentence of words does not reach `$FA`.

- [ ] **Step 1: Write the corpus text**

`tests/corpus/long_clause.txt` holds exactly this one line, ending with a newline:

```
1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0
```

- [ ] **Step 2: Add it to the corpus list**

In `tests/corpus_list.h`, replace the last line of the `CORPUS_NAMES` macro:

```c
    "formant_mod", "version", "volume", "mute", "queries"
```

with:

```c
    "formant_mod", "version", "volume", "mute", "queries", "long_clause"
```

and append to the comment block above it (after the line ending `for its F0 fit.`) a new line:

```c
   long_clause (M4.1) is one clause long enough to fill tn_phonetic, so tn_convert_clause speaks it mid-clause. */
```

The comment's closing `*/` moves to that new last line.

- [ ] **Step 3: Run the golden suite to see it fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL with `missing tests/golden/long_clause.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review`.

- [ ] **Step 4: Generate the golden with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: `updated tests/golden/long_clause.frm (N frames)`, plus the same "updated" lines for the existing goldens, which are rewritten with identical bytes.

Then run `git status --short tests/golden`. Expected: exactly one line, `?? tests/golden/long_clause.frm`. If any existing `.frm` shows as modified, STOP and report: the oracle is not deterministic, and nothing may be committed.

- [ ] **Step 5: Confirm the flush path is reached**

Run:
```bash
./build/apollo2_coverage.exe -o build/long_clause.cov tests/corpus/long_clause.txt
grep -c "^x rom B1FF$" build/long_clause.cov
```
Expected: `1`. If it is `0`, STOP and report.

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected: all 28 suites ok.
- `suite_synth` now includes `long_clause` in its corpus peak. If it reports clipping or a peak outside −12…−1 dBFS, STOP and report. Do not change any calibration.
- `diff_lts` and `golden_hybrid` also run the new file, and must stay green: the C routines of M4.0 do not care about clause length.

- [ ] **Step 7: Commit**

```bash
git add tests/corpus/long_clause.txt tests/golden/long_clause.frm tests/corpus_list.h
git commit -m "Corpus: long_clause, one clause that fills tn_phonetic (mid-clause flush), golden from the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: A cycle budget per ROM call (spec §3.1)

**Files:**
- Modify: `src/firmware/fw.h`, `src/firmware/fw.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `src/firmware/tn_rules.c`, `tests/test_fw.c`, `tests/test_fw_lts.c`

**Interfaces:**
- Produces (in `src/firmware/fw.h`, used by Tasks 4–6):

```c
#define FW_ROM_CALL_BUDGET         10000000ULL     /* a ROM helper that does not return within it is a fault */
#define FW_ROM_CALL_SPEECH_BUDGET  1000000000ULL   /* a ROM call that plays speech */
/* fw_host.rom_call */ bool (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget);
```

- `FW_ROM_CALL_BUDGET` is removed from `src/engine/thunks.h`. `thunks.h` includes `firmware/fw.h`, so its users still see it.

- [ ] **Step 1: Write the failing test**

In `tests/test_fw.c`:
- Add this function above `void suite_fw(void)`:

```c
static uint64_t g_budget;

static bool host_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    r->a = 0x42;
    g_budget = budget;
    return true;
}
```

- Replace the lines:

```c
    fw_regs r = { 0, 0, 0, 0 };
    CHECK(!fw_rom_call(&f, 0xB154, &r));  /* the host has no rom_call */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB154);
```

with:

```c
    fw_regs r = { 0, 0, 0, 0 };
    CHECK(!fw_rom_call(&f, 0xB154, &r, FW_ROM_CALL_BUDGET));  /* the host has no rom_call */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB154);

    fw_host caller = { NULL, host_call, host_window };   /* the budget reaches the host unchanged */
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(fw_rom_call(&f, 0xCA92, &r, FW_ROM_CALL_SPEECH_BUDGET));
    CHECK_EQ(g_budget, FW_ROM_CALL_SPEECH_BUDGET);
    CHECK_EQ(r.a, 0x42);
    CHECK(fw_rom_call(&f, 0xC5D6, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(g_budget, FW_ROM_CALL_BUDGET);
    CHECK(!f.fault);
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw"`
Expected: a compile error, because `fw_rom_call` takes 3 arguments and `FW_ROM_CALL_SPEECH_BUDGET` is undefined.

- [ ] **Step 3: Implement**

`src/firmware/fw.h`:
- After the `#define FW_TMP_78 ...` line, add:

```c
/* Cycle budgets of a ROM call from C (M4.1 spec 3.1): a helper that does not return within FW_ROM_CALL_BUDGET
   is a fault; a call that plays speech (prosody and playback) gets FW_ROM_CALL_SPEECH_BUDGET. */
#define FW_ROM_CALL_BUDGET         10000000ULL
#define FW_ROM_CALL_SPEECH_BUDGET  1000000000ULL
```

- In `fw_host`, replace the `rom_call` member and its comment with:

```c
    /* Runs the routine at addr, still in ROM, to its RTS, within budget cycles. A, X and Y in and out; the
       routine starts with the machine's current flags, and p returns its exit flags. False if it faulted or
       did not return. */
    bool (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
```

- Replace the `fw_rom_call` declaration and its comment with:

```c
/* Calls a routine still in ROM through the host, within budget cycles. Without a host, or when the call fails,
   a fault. */
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget);
```

`src/firmware/fw.c`: replace `fw_rom_call` with:

```c
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget)
{
    if (!f->host || !f->host->rom_call || !f->host->rom_call(f->host->ctx, addr, regs, budget)) {
        fault(f, addr);
        return false;
    }
    return true;
}
```

`src/engine/thunks.h`: delete the line `#define FW_ROM_CALL_BUDGET 10000000ULL   /* cycles a ROM routine called from C may take (spec 3.3) */`.

`src/engine/thunks.c`: replace the first three lines of `host_rom_call`:

```c
static bool host_rom_call(void *ctx, uint16_t addr, fw_regs *r)
{
    fw_bridge *b = ctx;
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    bool ok = machine_rom_call(b->m, addr, &mr, FW_ROM_CALL_BUDGET);
```

with:

```c
static bool host_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw_bridge *b = ctx;
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    bool ok = machine_rom_call(b->m, addr, &mr, budget);
```

`src/firmware/tn_rules.c`: in `tn_append_phonetic_rom`, change `fw_rom_call(f, TN_APPEND_PHONETIC, &r);` to `fw_rom_call(f, TN_APPEND_PHONETIC, &r, FW_ROM_CALL_BUDGET);`.

`tests/test_fw_lts.c`: change the mock's signature to `static bool mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)`, and add `(void)budget;` as its first statement.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `cmd //c ".\build.bat test"`
Expected: `suite_fw` ok; all 28 suites ok, with no warnings.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/fw.h src/firmware/fw.c src/engine/thunks.h src/engine/thunks.c src/firmware/tn_rules.c tests/test_fw.c tests/test_fw_lts.c
git commit -m "fw_rom_call takes a cycle budget per call: strict for helpers, a speech budget for playback (M4.1 3.1)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The phonetic strings as readable C (spec §4)

**Files:**
- Modify: `tools/extract_tables.py`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c`
- Create: `src/firmware/data/phonetic_text_data.c` (generated by the tool, then reviewed)

**Interfaces:**
- Produces (in `fw_data.h`): the type `fw_phonetic_text`, the array `fw_phonetic_texts[]` and its count `fw_phonetic_text_count`, and encoder step 6.

The ROM framing was checked while writing this plan:
- The engine and language texts are a length byte (31) followed by 31 characters, 32 bytes in all.
- `boot_baud_text_19200` is length 1, a space, then a CR outside the count.
- `boot_baud_text_300` is length 12, its text, then a CR outside the count.
- `boot_baud_text_1200` is length 15; its counted text ends with the CR.

So each string is stored as `text` (counted) plus `tail` (the bytes after it, which are not counted).

- [ ] **Step 1: Write the failing test**

In `tests/test_firmware_data.c`:
- Replace the `k_rom` line with:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0x989C }, { 0x9AB4, 0x9BB4 }, { 0xACC7, 0xACE8 },
                               { 0xF75B, 0xF77B } };
```

- In `test_coverage`, replace both occurrences of `in(k_rom, 3, a)` with `in(k_rom, sizeof k_rom / sizeof k_rom[0], a)`.
- In `test_counts`, after `CHECK_EQ(fw_affix_list_count, 3);`, add:

```c
    CHECK_EQ(fw_phonetic_text_count, 5);
```

- In `test_against_rom`, after the `printf` line, add:

```c
    CHECK_EQ(compared, 35311);   /* M4.0's 35,214 plus the five phonetic texts (97 bytes) */
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: a compile error, because `fw_phonetic_text_count` is undeclared.

- [ ] **Step 3: Declare the type and the encoder step**

`src/firmware/data/fw_data.h`:
- After the `fw_affix_list` typedef, add:

```c
/* A length-prefixed phonetic text (M4.1 spec 4): the length byte strlen(text), the text, then tail, the bytes the
   ROM keeps after the counted text (a CR, or nothing). size: the annotated bytes in all. */
typedef struct { const char *name; uint16_t addr, size; const char *text, *tail; } fw_phonetic_text;
```

- After `extern const size_t fw_affix_list_count;`, add:

```c
extern const fw_phonetic_text fw_phonetic_texts[];
extern const size_t fw_phonetic_text_count;
```

`src/firmware/data/fw_data.c`: in `fw_image_build`, before `return e.ok;`, add:

```c
    /* 6. The length-prefixed phonetic texts (M4.1 spec 4). */
    for (size_t i = 0; i < fw_phonetic_text_count; i++) {
        const fw_phonetic_text *t = &fw_phonetic_texts[i];
        size_t n = strlen(t->text);
        if (n == 0 || n > 0xFF)
            fail(&e, "%s: text length %u", t->name, (unsigned)n);
        uint32_t at = t->addr;
        put(&e, false, at++, (uint8_t)n);
        at = put_text(&e, false, at, t->text, false);
        at = put_text(&e, false, at, t->tail, false);
        if (at - t->addr != t->size)
            fail(&e, "%s: %u bytes, the annotation says %u", t->name, (unsigned)(at - t->addr), t->size);
    }
```

- [ ] **Step 4: Extend the converter and generate the data file**

`tools/extract_tables.py`:
- After the `AFFIXES = [...]` line, add:

```python
# (name, address, size), as annotated in ghidra740/annotations/command.txt and boot.txt (M4.1 spec 4)
PHONETIC_TEXTS = [
    ("cmd_version_engine_text", 0x9804, 32), ("boot_baud_text_19200", 0xACC7, 3),
    ("boot_baud_text_300", 0xACCA, 14), ("boot_baud_text_1200", 0xACD8, 16),
    ("cmd_version_lang_text", 0xF75B, 32),
]
```

- Above `def main():`, add:

```python
def write_phonetic_texts(rom):
    """src/firmware/data/phonetic_text_data.c: each text as its counted text plus the bytes after it (tail)."""
    lines = [HEADER.replace("M4.0 spec section 4", "M4.1 spec section 4") +
             "/* name, address, annotated size, counted text, tail (bytes after the count) */",
             "const fw_phonetic_text fw_phonetic_texts[] = {"]
    for name, addr, size in PHONETIC_TEXTS:
        n = rom[addr]
        assert 0 < n and 1 + n <= size, "%s: length %d does not fit %d bytes" % (name, n, size)
        text, tail = rom[addr + 1:addr + 1 + n], rom[addr + 1 + n:addr + size]
        lines.append('    { "%s", 0x%04X, %d, %s, %s },' % (name, addr, size, cstr(text), cstr(tail)))
    lines += ["};", "const size_t fw_phonetic_text_count = sizeof fw_phonetic_texts / sizeof fw_phonetic_texts[0];"]
    with open(os.path.join(OUT, "phonetic_text_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote phonetic_text_data.c in", OUT)
```

- In `main()`, replace the line:

```python
    path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("APOLLO2_ROM", "UK_V2.01_4.04_ROM.BIN")
```

with:

```python
    args = [a for a in sys.argv[1:] if a != "--phonetic-only"]
    path = args[0] if args else os.environ.get("APOLLO2_ROM", "UK_V2.01_4.04_ROM.BIN")
```

- After the line `    assert len(rom) == 0x10000, "need the 64 KB ROM"`, add:

```python
    write_phonetic_texts(rom)
    if "--phonetic-only" in sys.argv:   # M4.1: leave the reviewed M4.0 tables alone
        return
```

- In the module docstring, change the usage line to `usage: python tools/extract_tables.py [--phonetic-only] [ROM]   (default $APOLLO2_ROM, else UK_V2.01_4.04_ROM.BIN)`.

Run: `python tools/extract_tables.py --phonetic-only "$APOLLO2_ROM"`
Expected: `wrote phonetic_text_data.c in src/firmware/data`. `git status --short src/firmware/data` must show only `?? src/firmware/data/phonetic_text_data.c`, because the M4.0 table files are untouched.

The generated file must read (review it against this):

```c
/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.1 spec section 4), then reviewed.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"

/* name, address, annotated size, counted text, tail (bytes after the count) */
const fw_phonetic_text fw_phonetic_texts[] = {
    { "cmd_version_engine_text", 0x9804, 32, "      TU    PyNf  ZERO  WQN   .", "" },
    { "boot_baud_text_19200", 0xACC7, 3, " ", "\015" },
    { "boot_baud_text_300", 0xACCA, 14, "tRE HuNDRxD ", "\015" },
    { "boot_baud_text_1200", 0xACD8, 16, "TWeLV HuNDRxD \015", "" },
    { "cmd_version_lang_text", 0xF75B, 32, "      Fo|   PyNf  ZERO  Fo|   .", "" },
};
const size_t fw_phonetic_text_count = sizeof fw_phonetic_texts / sizeof fw_phonetic_texts[0];
```

If the tool's output differs from this, the ROM wins. Commit the tool's output and report the difference.

- [ ] **Step 5: Run the tests**

Run: `cmd //c ".\build.bat test firmware_data"`, then `cmd //c ".\build.bat test"`
Expected: `35311 converted bytes compared with the ROM, 0 differ`, `suite_firmware_data` ok, and all 28 suites ok.

- [ ] **Step 6: Commit**

```bash
git add tools/extract_tables.py src/firmware/data/phonetic_text_data.c src/firmware/data/fw_data.h src/firmware/data/fw_data.c tests/test_firmware_data.c
git commit -m "Converted data: the five length-prefixed phonetic texts as readable C (M4.1 4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `tn_convert_clause` in C (spec §2.2)

**Files:**
- Create: `src/firmware/tn_convert.h`, `src/firmware/tn_convert.c`, `tests/test_fw_tn.c`
- Modify: `src/firmware/fw.h`, `src/firmware/fw.c`, `tests/suites.h` (append `SUITE(suite_fw_tn)`)

**Interfaces:**
- Consumes:
  - Task 2's `fw_rom_call(f, addr, regs, budget)`, `FW_ROM_CALL_BUDGET` and `FW_ROM_CALL_SPEECH_BUDGET`;
  - M4.0's `tn_say_char(fw *, uint8_t) → uint8_t` (`tn_rules.h`), `lts_convert_char(fw *, uint8_t)` (`lts.h`), `fw_set_low_window`, `LTS_CHAR_CLASS`, `CC_LETTER` and `CC_VOWEL` (`data/fw_data.h`).
- Produces (used by Task 5):

```c
/* tn_convert.h */
#define ROM_ISR_PAR_READY       0xC5D6
#define ROM_CMD_RUN_DUE         0xA880
#define ROM_PROS_MAKE_SEGMENTS  0xCA92
#define ROM_BOOT_LOOP_PLAY      0xA7D4
#define TN_PHONETIC_FLUSH       0xFA
void tn_convert_clause(fw *f);
/* fw.h */
#define FW_TMP_75 0x0075
/* struct fw gains: */ unsigned tn_flushes;
```

The C below was checked against `rom_B1E6_tn_convert_clause.txt`. In it, X is the CPU's X register.
- `isr_par_ready` preserves A, X and Y (`rom_C5D6`).
- `cmd_run_due` reads X and returns it unchanged, because every handler ends at `cmd_run_done`, which restores X (`rom_A880`, `rom_A893`).
- `pros_make_segments` and `boot_loop_play` both start by loading A from memory (`rom_CA92`, `rom_A7D4`). The C passes A = `tn_phon_len` and X as the ROM has them at `$B201`, and Y = 0.

Step 3 has you confirm in the dumps that neither routine reads Y before writing it.

- [ ] **Step 1: Write the failing tests**

`tests/test_fw_tn.c`:

```c
/* Unit checks of tn_convert_clause on the converted data, with a mock host that records every ROM call. The
   mock's tn_append_phonetic ($B154) follows the ROM routine. The full proof is the live differential test
   (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/tn_convert.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

typedef struct { uint16_t addr; uint8_t a, x; uint64_t budget; } rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

static bool mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
    if (g_ncalls < MAX_CALLS) {
        g_calls[g_ncalls].addr = addr;
        g_calls[g_ncalls].a = r->a;
        g_calls[g_ncalls].x = r->x;
        g_calls[g_ncalls].budget = budget;
    }
    g_ncalls++;
    if (addr == 0xB154) {   /* tn_append_phonetic, still in ROM until Task 6 */
        if (r->a && fw_rd8(f, CMD_VOICE)) {
            uint8_t x = fw_rd8(f, TN_PHON_LEN), y = r->y;
            for (uint8_t n = r->a; n; n--) {
                y++;
                fw_wr8(f, (uint16_t)(TN_PHONETIC + x), fw_ind_y(f, (uint8_t)TN_RULE_PTR, y));
                x++;
            }
            fw_wr8(f, TN_PHON_LEN, x);
        }
        r->a = 0xFF;
        return true;
    }
    return addr == ROM_ISR_PAR_READY || addr == ROM_CMD_RUN_DUE || addr == ROM_PROS_MAKE_SEGMENTS ||
           addr == ROM_BOOT_LOOP_PLAY;   /* registers come back unchanged, as these leave X alone */
}

static void mock_window(void *ctx, bool on)
{
    (void)ctx;
    g_window = on;
}

static void setup_n(const char *text, size_t n)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_window = false;
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
}

static void setup(const char *text) { setup_n(text, strlen(text)); }

static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Every helper call has the strict budget. */
static void check_helper_budgets(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_ISR_PAR_READY || g_calls[i].addr == ROM_CMD_RUN_DUE)
            CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}

/* the spoken name of c: the output of the first rule of its list */
static const char *name_of(uint8_t c) { return tn_char_lists[c]->rules[0].output; }

static void test_phonetic_mode(void)
{
    setup("Ab c");
    g_ram[CMD_PHONETIC] = 1;
    g_ram[CMD_EV_COUNT] = 9;
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[TN_PHON_LEN], 4);
    CHECK(memcmp(g_ram + TN_PHONETIC, "Ab c", 4) == 0);
    CHECK_EQ(g_ram[TN_POS], 4);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 4);
    /* tn_key_pos lags in phonetic mode: cmd_run_due runs at 0, then at k-1 and k for each later k; and there is
       no final call */
    static const uint8_t want_x[] = { 0, 0, 1, 1, 2, 2, 3 };
    CHECK_EQ(count(ROM_CMD_RUN_DUE), 7);
    unsigned k = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_CMD_RUN_DUE && k < 7)
            CHECK_EQ(g_calls[i].x, want_x[k++]);
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void test_copy_and_final_run_due(void)
{
    setup(".");
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[TN_PHON_LEN], 1);
    CHECK_EQ(g_ram[TN_PHONETIC], '.');
    CHECK_EQ(g_ram[FW_TMP_75], 0);              /* X saved around the copy */
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK_EQ(count(ROM_CMD_RUN_DUE), 2);        /* at 0, then once more at the clause end */
    CHECK_EQ(g_calls[g_ncalls - 1].addr, ROM_CMD_RUN_DUE);
    CHECK_EQ(g_calls[g_ncalls - 1].x, 1);
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void test_punct_spoken(void)
{
    setup(".");
    g_ram[CMD_PUNCT] = 1;                       /* @P1 */
    tn_convert_clause(&g_f);
    const char *name = name_of('.');
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(name));
    CHECK(memcmp(g_ram + TN_PHONETIC, name, strlen(name)) == 0);
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK(!g_f.fault);
}

static void test_vowelless_word_spelled(void)
{
    setup(" BCD");                              /* the leading space puts the word at tn_key_pos 1 */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[CMD_SPELL], 3);              /* automatic spelling */
    char want[64] = " ";
    strcat(want, name_of('B'));
    strcat(want, name_of('C'));
    strcat(want, name_of('D'));
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(want));
    CHECK(memcmp(g_ram + TN_PHONETIC, want, strlen(want)) == 0);
    CHECK_EQ(g_ram[TN_IN_WORD] & 1, 1);
    CHECK_EQ(g_ram[TN_POS], 4);
    CHECK(!g_f.fault);
}

static void test_digit_to_rules(void)
{
    setup("7");
    tn_convert_clause(&g_f);
    CHECK(g_ram[TN_PHON_LEN] > 0);
    CHECK_EQ(g_ram[TN_IN_WORD] & 1, 1);         /* digits set tn_in_word like letters */
    CHECK(!g_window);                           /* CLB7 P2 after the rules */
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK(!g_f.fault);
}

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
    check_helper_budgets();
    CHECK(!g_f.fault);
}

void suite_fw_tn(void)
{
    CHECK(fw_image_get() != NULL);
    test_phonetic_mode();
    test_copy_and_final_run_due();
    test_punct_spoken();
    test_vowelless_word_spelled();
    test_digit_to_rules();
    test_flush();
}
```

Append `SUITE(suite_fw_tn)` as the last line of `tests/suites.h`.

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_tn"`
Expected: a compile error, because `firmware/tn_convert.h` is missing (C1083).

- [ ] **Step 3: Implement**

`src/firmware/fw.h`:
- After the `#define FW_TMP_78 ...` line, add:

```c
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X */
```

- In `struct fw`, after `jmp_buf *trap;`, add:

```c
    unsigned tn_flushes;           /* mid-clause flushes by tn_convert_clause (a statistic for the tests) */
```

`src/firmware/fw.c`: in `fw_init`, after `f->trap = NULL;`, add `f->tn_flushes = 0;`.

`src/firmware/tn_convert.h`:

```c
#ifndef FW_TN_CONVERT_H
#define FW_TN_CONVERT_H
/* The clause-to-phonetic-text driver of text normalisation (ROM $B1E6), decompiled (M4.1 spec section 2.2). */
#include "fw.h"

/* Routines still in ROM that tn_convert_clause calls */
#define ROM_ISR_PAR_READY       0xC5D6   /* parallel mode: ready for the next host byte; A, X, Y preserved */
#define ROM_CMD_RUN_DUE         0xA880   /* X = a clause position: run the deferred commands due there; X kept */
#define ROM_PROS_MAKE_SEGMENTS  0xCA92   /* prosody of tn_phonetic */
#define ROM_BOOT_LOOP_PLAY      0xA7D4   /* play the utterance */
#define TN_PHONETIC_FLUSH       0xFA     /* tn_phon_len at which tn_phonetic is spoken mid-clause */

/* $B1E6: convert cmd_text (boot_clause_len characters) to phonetic text in tn_phonetic: per character the
   deferred commands due, then a verbatim copy (@X), the rules (letters and digits; a word with no vowel before
   its end is spelled), or a copy or spoken name (space and punctuation). A nearly full tn_phonetic is spoken
   mid-clause and the conversion goes on from tn_resume_pos. */
void tn_convert_clause(fw *f);

#endif
```

`src/firmware/tn_convert.c`:

```c
#include "tn_convert.h"
#include "lts.h"
#include "tn_rules.h"
#include "data/fw_data.h"

static uint8_t char_class(fw *f, uint8_t c) { return fw_rom8(f, (uint16_t)(LTS_CHAR_CLASS + c)); }
static uint8_t text_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(CMD_TEXT + x)); }

/* A ROM helper that reads only X (isr_par_ready, cmd_run_due); returns the X it leaves. */
static uint8_t rom_call_x(fw *f, uint16_t addr, uint8_t x)
{
    fw_regs r = { 0, x, 0, 0 };
    fw_rom_call(f, addr, &r, FW_ROM_CALL_BUDGET);
    return r.x;
}

/* tn_phonetic[tn_phon_len++] = c */
static void put_phonetic(fw *f, uint8_t c)
{
    uint8_t n = fw_rd8(f, TN_PHON_LEN);
    fw_wr8(f, (uint16_t)(TN_PHONETIC + n), c);
    fw_wr8(f, TN_PHON_LEN, (uint8_t)(n + 1));
}

void tn_convert_clause(fw *f)
{
    fw_wr8(f, TN_RESUME_POS, 0);
    for (;;) {                                                  /* a pass over tn_phonetic: $B1E9 */
        uint8_t x = 0;
        fw_wr8(f, CMD_EV_COUNT, x);
        fw_wr8(f, TN_PHON_LEN, x);
        x = fw_rd8(f, TN_RESUME_POS);
        fw_wr8(f, TN_POS, x);
        fw_wr8(f, TN_KEY_POS, x);
        for (;;) {                                              /* per character: $B1F6 */
            x = rom_call_x(f, ROM_ISR_PAR_READY, x);
            if (fw_rd8(f, TN_PHON_LEN) >= TN_PHONETIC_FLUSH)
                break;                                          /* nearly full: speak it now */
            for (;;) {                                          /* $B20A: the deferred commands up to tn_pos */
                x = rom_call_x(f, ROM_CMD_RUN_DUE, fw_rd8(f, TN_KEY_POS));
                if (x >= fw_rd8(f, TN_POS))
                    break;
                fw_wr8(f, TN_KEY_POS, (uint8_t)(fw_rd8(f, TN_KEY_POS) + 1));
            }
            uint8_t c = text_at(f, x);
            if (fw_rd8(f, CMD_PHONETIC)) {                      /* @X: copied verbatim; tn_key_pos lags */
                put_phonetic(f, c);
                fw_wr8(f, TN_POS, ++x);
                if (x >= fw_rd8(f, BOOT_CLAUSE_LEN))
                    return;                                     /* no final cmd_run_due here */
                continue;
            }
            bool to_rules = false;
            bool copy = false;
            if (char_class(f, c) & CC_LETTER) {
                fw_wr8(f, TN_KEY_POS, x);
                if (!(fw_rd8(f, TN_IN_WORD) & 1) && !(fw_rd8(f, CMD_SPELL) & 1)) {
                    /* $B23A: a word starts; it is spelled (cmd_spell = 3) when a non-letter comes before any vowel */
                    for (uint8_t i = x;; i++) {
                        uint8_t cl = char_class(f, text_at(f, i));
                        if (!(cl & CC_LETTER)) {
                            fw_wr8(f, CMD_SPELL, 3);
                            break;
                        }
                        if (cl & CC_VOWEL)
                            break;
                    }
                }
                to_rules = true;
            } else {                                            /* $B264: not a letter */
                fw_wr8(f, TN_IN_WORD, fw_rd8(f, TN_IN_WORD) & 0xFE);
                if (fw_rd8(f, CMD_SPELL) & 2)
                    fw_wr8(f, CMD_SPELL, 0);                    /* automatic spelling ends */
                if (c == ' ') {
                    uint8_t pos = fw_rd8(f, TN_POS);
                    if ((fw_rd8(f, CMD_SPELL) & 1) && pos != 0 && (uint8_t)(pos + 1) != fw_rd8(f, BOOT_CLAUSE_LEN))
                        x = tn_say_char(f, c);                  /* spell mode: the space is spoken */
                    else
                        copy = true;
                } else if (c >= '0' && c <= '9') {
                    fw_wr8(f, TN_KEY_POS, x);
                    to_rules = true;                            /* digits go to the rules like letters */
                } else if (fw_rd8(f, CMD_PUNCT)) {
                    x = tn_say_char(f, c);                      /* @P1: spoken by name */
                } else {
                    copy = true;
                }
            }
            if (to_rules) {                                     /* $B256 */
                lts_convert_char(f, c);
                fw_set_low_window(f, false);                    /* CLB7 P2 */
                x = (uint8_t)(fw_rd8(f, TN_POS) - 1);           /* the rules moved tn_pos */
                fw_wr8(f, TN_IN_WORD, fw_rd8(f, TN_IN_WORD) | 1);
            } else if (copy) {                                  /* $B2A5: X saved in $75 around the copy */
                fw_wr8(f, FW_TMP_75, x);
                put_phonetic(f, c);
                x = fw_rd8(f, FW_TMP_75);
            }
            fw_wr8(f, TN_POS, ++x);                             /* $B297 */
            if (x >= fw_rd8(f, BOOT_CLAUSE_LEN)) {
                rom_call_x(f, ROM_CMD_RUN_DUE, x);
                return;
            }
        }
        /* $B1FF: speak tn_phonetic, then convert on from here with an empty buffer */
        fw_wr8(f, TN_RESUME_POS, x);
        fw_regs r = { fw_rd8(f, TN_PHON_LEN), x, 0, 0 };
        fw_rom_call(f, ROM_PROS_MAKE_SEGMENTS, &r, FW_ROM_CALL_SPEECH_BUDGET);
        fw_rom_call(f, ROM_BOOT_LOOP_PLAY, &r, FW_ROM_CALL_SPEECH_BUDGET);
        f->tn_flushes++;
    }
}
```

**Faithfulness check.** Before committing, read `rom_B1E6_tn_convert_clause.txt` side by side with this C and confirm every branch and every RAM write. Then read `rom_CA92_pros_make_segments.txt` and `rom_A7D4_boot_loop_play.txt` (including `boot_play_body` at `$B2B3`) far enough to confirm that neither reads Y (or X) before writing it. Record the result in the report. If either reads Y first, report DONE_WITH_CONCERNS with the address; do not guess a value.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_tn"`, then `cmd //c ".\build.bat test"`
Expected: `suite_fw_tn` ok; all 29 suites ok, with no warnings, and the firmware boundary check passing.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/tn_convert.h src/firmware/tn_convert.c src/firmware/fw.h src/firmware/fw.c tests/test_fw_tn.c tests/suites.h
git commit -m "tn_convert_clause in C: the clause-to-phonetic driver, with ROM calls for the helpers and the mid-clause flush (M4.1 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The `tn_convert_clause` thunk, the flush skip in `diff_lts`, and the flush assertion in `golden_hybrid` (spec §3.2, §5.1, §5.2)

**Files:**
- Modify: `src/engine/thunks.c`, `src/engine/hybrid_engine.h`, `src/engine/hybrid_engine.c`, `src/render/render.c` (only if its call no longer compiles), `tests/test_diff_lts.c`, `tests/test_golden_hybrid.c`

**Interfaces:**
- Consumes: Task 4's `tn_convert_clause`, `ROM_ISR_PAR_READY`, `ROM_BOOT_LOOP_PLAY` and `fw.tn_flushes`.
- Produces: the replacement `"tn_convert_clause"` at `0xB1E6` with no declared outputs (10 routines after this task), and:

```c
/* hybrid_engine.h */
typedef struct {
    uint64_t thunk_calls;   /* times a C routine ran instead of the ROM's */
    unsigned tn_flushes;    /* mid-clause flushes by the C tn_convert_clause */
} hybrid_stats;
machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats);
```

- [ ] **Step 1: Write the failing tests**

`tests/test_golden_hybrid.c`:
- Replace `#include "firmware/tn_rules.h"` with `#include "firmware/tn_convert.h"`.
- Replace `check` and `check_corpus` with:

```c
/* Compares the hybrid frames of one corpus file with its golden; returns the mid-clause flushes the C
   tn_convert_clause made. */
static unsigned check(const uint8_t *rom, size_t len, const char *only, const char *name, const char *text,
                      bool boot)
{
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return 0;
    frame_list fl = { 0 };
    hybrid_stats stats = { 0, 0 };
    machine_status st = hybrid_engine_speak(&g_m, rom, len, NULL, only, text, boot, &fl, NULL, &stats);
    g_thunk_calls += stats.thunk_calls;
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    CHECK_EQ(g_m.unmodelled_count, 0);
    char *got = frame_list_hex(&fl);
    if (strcmp(got, want) != 0) {
        size_t line = 1;
        for (size_t i = 0; got[i] && got[i] == want[i]; i++)
            line += got[i] == '\n';
        fprintf(stderr, "  hybrid (%s) %s: frames differ from the golden at frame %zu\n", only ? only : "all", name,
                line);
        g_test_failures++;
    }
    free(got);
    free(want);
    frame_list_free(&fl);
    return stats.tn_flushes;
}

/* The whole corpus in one mode. Matching the goldens proves nothing if no C ever ran, so the mode must have run
   its thunks at least once over the corpus; and where the C tn_convert_clause runs, long_clause must have taken
   its mid-clause flush (M4.1 spec 5.2). */
static void check_corpus(const uint8_t *rom, size_t len, const char *only)
{
    g_thunk_calls = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        unsigned flushes = check(rom, len, only, k_corpus[c], text, false);
        if (!strcmp(k_corpus[c], "long_clause") && (!only || !strcmp(only, "tn_convert_clause")) && flushes == 0) {
            fprintf(stderr, "  hybrid (%s) long_clause: the C tn_convert_clause never flushed\n",
                    only ? only : "all");
            g_test_failures++;
        }
        free(text);
    }
    check(rom, len, only, "boot", "", true);
    if (g_thunk_calls == 0) {
        fprintf(stderr, "  hybrid (%s): no thunk ran over the whole corpus\n", only ? only : "all");
        g_test_failures++;
    }
}
```

- In `test_thunk_faults`, replace the block that starts with the comment `/* (2) a ROM call from the C that does not return */` and ends with `check_stopped(&g_m, &b, r->addr, TN_APPEND_PHONETIC, sp);` with:

```c
    /* (2) a ROM call from the C that does not return: isr_par_ready, tn_convert_clause's first call */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_ISR_PAR_READY, stuck_hook, NULL));
    r = fw_replacement_find("tn_convert_clause");
    CHECK(r != NULL);
    if (!r)
        return;
    g_m.cpu.pc = r->addr;
    sp = g_m.cpu.s;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, ROM_ISR_PAR_READY, sp);
```

`tests/test_diff_lts.c`:
- Add `#include "firmware/tn_convert.h"` after `#include "engine/thunks.h"`.
- After `static const char *g_source;`, add:

```c
static unsigned g_flush_skips;   /* calls abandoned at a mid-clause flush (M4.1 spec 5.1) */
static bool g_flushed;

/* boot_loop_play reached in scratch A: tn_convert_clause flushing mid-clause, which cannot finish with
   interrupts held. Returning true without moving the PC stops the scratch machine (MFAULT_FIRMWARE). */
static bool stop_at_play(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_flushed = true;
    return true;
}
```

- Replace the body of `observe` from `g_checked[o - g_obs]++;` up to (not including) the line `if (!same && g_mismatches++ < 10)` with:

```c
    machine_clone(&g_ref, m);
    machine_hold_irqs(&g_ref, true);
    g_flushed = false;
    CHECK(machine_hook(&g_ref, ROM_BOOT_LOOP_PLAY, stop_at_play, NULL));
    char why[160] = "states differ";
    bool same = false;
    fw_bridge b;
    if (!machine_finish_routine(&g_ref, DIFF_BUDGET)) {
        if (g_flushed) {
            g_flush_skips++;   /* golden_hybrid covers this call end to end */
            return false;
        }
        g_checked[o - g_obs]++;
        snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_ref.fault);
    } else {
        g_checked[o - g_obs]++;
        machine_clone(&g_c, m);
        machine_hold_irqs(&g_c, true);
        if (!fw_bridge_init(&b, &g_c)) {
            snprintf(why, sizeof why, "no firmware data image");
        } else {
            o->r->thunk(&g_c, &b);
            if (g_c.fault != MFAULT_NONE)
                snprintf(why, sizeof why, "the C faulted (fault %d at $%04X)", (int)g_c.fault, g_c.fault_addr);
            else
                same = compare(&g_ref, &g_c, o->r->outputs, why, sizeof why);
        }
    }
```

- In `suite_diff_lts`, after `g_mismatches = 0;`, add `g_flush_skips = 0;`. Before the final `CHECK_EQ(g_mismatches, 0);`, add:

```c
    printf("  %u mid-clause flush calls skipped (golden_hybrid covers them)\n", g_flush_skips);
    CHECK(g_flush_skips > 0);   /* long_clause reaches the flush */
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test golden_hybrid"`
Expected: a compile error, because `hybrid_stats` is undefined.

- [ ] **Step 3: Implement**

`src/engine/hybrid_engine.h`: replace the declaration of `hybrid_engine_speak` and its comment with:

```c
typedef struct {
    uint64_t thunk_calls;   /* times a C routine ran instead of the ROM's */
    unsigned tn_flushes;    /* mid-clause flushes by the C tn_convert_clause */
} hybrid_stats;

/* rom_engine_run with every replacement active, or only the one named only. MACHINE_FAULT (MFAULT_FIRMWARE)
   when the data image does not build or only names no replacement. produced may be NULL (frames dropped).
   Every call binds a fresh firmware bridge: a bridge that faulted is dead, and no state carries over.
   stats (may be NULL) receives what the C did. */
machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats);
```

`src/engine/hybrid_engine.c`:
- Change the last parameter of the definition from `uint64_t *thunk_calls` to `hybrid_stats *stats`.
- Replace

```c
    if (thunk_calls)
        *thunk_calls = h.bridge.thunk_calls;
```

with

```c
    if (stats) {
        stats->thunk_calls = h.bridge.thunk_calls;
        stats->tn_flushes = h.bridge.f.tn_flushes;
    }
```

`src/render/render.c` already passes `NULL` as the last argument, so it compiles unchanged.

`src/engine/thunks.c`:
- Add `#include "firmware/tn_convert.h"` after `#include "firmware/tn_rules.h"`.
- After `lts_convert_char_regs`, add:

```c
/* tn_convert_clause $B1E6: no register input or output (boot_loop_text returns straight after it) */
static void tn_convert_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    tn_convert_clause(f);
}
```

- Add `THUNK(tn_convert_clause)` after `THUNK(lts_convert_char)`.
- Add the table entry after the `lts_convert_char` entry:

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `diff_lts`: the line `tn_convert_clause N calls checked` with N > 0, 0 mismatches, and `K mid-clause flush calls skipped` with K > 0.
- `golden_hybrid`: `27 corpus files x 11 modes`, with no frame differences and no "never flushed" message.
- All 29 suites ok.

If `diff_lts` reports a mismatch for `tn_convert_clause`, find the root cause against `rom_B1E6_tn_convert_clause.txt` and fix the C faithfully. Never loosen the comparison.

- [ ] **Step 5: Commit**

```bash
git add src/engine/thunks.c src/engine/hybrid_engine.h src/engine/hybrid_engine.c tests/test_diff_lts.c tests/test_golden_hybrid.c
git commit -m "Hybrid: tn_convert_clause runs as C; diff_lts skips mid-clause flushes, golden_hybrid asserts long_clause flushed in C

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `tn_append_phonetic` in C, and its thunk (spec §2.1)

**Files:**
- Modify: `src/firmware/tn_rules.h`, `src/firmware/tn_rules.c`, `src/firmware/lts.c`, `src/engine/thunks.c`, `tests/test_fw_tn.c`, `tests/test_fw_lts.c`

**Interfaces:**
- Consumes: `fw_ind_y`, `FW_TMP_78`, `TN_PHON_LEN`, `TN_PHONETIC`, `CMD_VOICE` and `TN_RULE_PTR`.
- Produces: `uint8_t tn_append_phonetic(fw *f, uint8_t len, uint8_t *x, uint8_t *y);`, and the replacement `"tn_append_phonetic"` at `0xB154` with outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y` (11 routines).
- Removes: `tn_append_phonetic_rom` and `TN_APPEND_PHONETIC`. After this task no firmware code calls `$B154` in ROM.

The ROM routine (`rom_B154_tn_append_phonetic.txt`):
- When A = 0, it goes straight to `LDA #$FF / RTS`; X and Y are unchanged.
- Otherwise it does `STA $78`, `LDX tn_phon_len`, `TST cmd_voice`.
  - Voice on: it loops `INY; LDA (tn_rule_ptr),Y; STA $1900,X; INX; DEC $78` until `$78` = 0.
  - In both cases it then does `STX tn_phon_len`, `LDA #$FF`, `RTS`.

`cmd_put_phonetic` restores A and Y after the call. The thunk still sets A, X and Y exactly, so `diff_lts` compares all three.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_tn.c`:
- Delete the whole `if (addr == 0xB154) { ... }` block from `mock_rom_call`, along with its comment. `ctx` is then unused, so also replace the line `fw *f = ctx;` with `(void)ctx;`. Otherwise `/W4 /WX` fails on an unreferenced local.
- Add these tests above `void suite_fw_tn(void)`:

```c
static void put_rule_text(void)   /* a length byte 3, then "abc", at $0300 = tn_rule_ptr */
{
    static const uint8_t text[] = { 3, 'a', 'b', 'c' };
    memcpy(g_ram + 0x0300, text, sizeof text);
    fw_wr16(&g_f, TN_RULE_PTR, 0x0300);
}

static void test_append_voice_on(void)
{
    setup("");
    put_rule_text();
    g_ram[TN_PHON_LEN] = 2;
    uint8_t x = 0x55, y = 0;
    CHECK_EQ(tn_append_phonetic(&g_f, 3, &x, &y), 0xFF);
    CHECK_EQ(x, 5);
    CHECK_EQ(y, 3);                             /* on the last byte copied */
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2, "abc", 3) == 0);
    CHECK_EQ(g_ram[FW_TMP_78], 0);              /* counted down */
    CHECK_EQ(g_ncalls, 0);                      /* no ROM call */
    CHECK(!g_f.fault);
}

static void test_append_voice_off(void)
{
    setup("");
    put_rule_text();
    g_ram[CMD_VOICE] = 0;
    g_ram[TN_PHON_LEN] = 2;
    uint8_t x = 0x55, y = 0;
    CHECK_EQ(tn_append_phonetic(&g_f, 3, &x, &y), 0xFF);
    CHECK_EQ(x, 2);                             /* X = tn_phon_len */
    CHECK_EQ(y, 0);
    CHECK_EQ(g_ram[TN_PHON_LEN], 2);
    CHECK_EQ(g_ram[TN_PHONETIC + 2], 0);
    CHECK_EQ(g_ram[FW_TMP_78], 3);              /* the length stays */
    CHECK(!g_f.fault);
}

static void test_append_empty(void)
{
    setup("");
    put_rule_text();
    g_ram[FW_TMP_78] = 0x99;
    uint8_t x = 0x55, y = 7;
    CHECK_EQ(tn_append_phonetic(&g_f, 0, &x, &y), 0xFF);
    CHECK_EQ(x, 0x55);
    CHECK_EQ(y, 7);
    CHECK_EQ(g_ram[FW_TMP_78], 0x99);
    CHECK_EQ(g_ram[TN_PHON_LEN], 0);
    CHECK(!g_f.fault);
}
```

- In `suite_fw_tn`, add calls to `test_append_voice_on();`, `test_append_voice_off();` and `test_append_empty();` before `test_phonetic_mode();`.
- In the file's header comment, replace `The mock's tn_append_phonetic ($B154) follows the ROM routine. ` with nothing.

In `tests/test_fw_lts.c`:
- Delete `mock_rom_call` entirely, and the line `g_host.rom_call = mock_rom_call;` in `setup`.
- Add `g_host.rom_call = NULL;   /* nothing in letter-to-sound calls ROM any more */` in its place.
- Replace the header comment's first two lines with:

```c
/* Unit checks of the ported letter-to-sound routines on the converted data (no ROM calls: tn_append_phonetic is
   C since M4.1). The full proof is the live differential test (diff_lts). */
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_tn"`
Expected: a compile error, because `tn_append_phonetic` is undeclared.

- [ ] **Step 3: Implement**

`src/firmware/tn_rules.h`: replace

```c
#define TN_APPEND_PHONETIC 0xB154   /* still in ROM (spec 3.3): it also serves cmd_put_phonetic */

/* tn_append_phonetic: A = len at (tn_rule_ptr),Y: the len bytes after it go to tn_phonetic (ROM call). */
void tn_append_phonetic_rom(fw *f, uint8_t len, uint8_t y);
```

with

```c
/* $B154: len = the length byte at (tn_rule_ptr),*y. Unless len is 0: $78 = len, *x = tn_phon_len, and with the
   voice on (cmd_voice) the len bytes after the length byte are appended to tn_phonetic (*y ends on the last one,
   $78 counts down to 0, *x and tn_phon_len advance by len). len = 0 leaves *x, *y and $78 alone. Returns A =
   $FF. */
uint8_t tn_append_phonetic(fw *f, uint8_t len, uint8_t *x, uint8_t *y);
```

`src/firmware/tn_rules.c`: replace `tn_append_phonetic_rom` with:

```c
uint8_t tn_append_phonetic(fw *f, uint8_t len, uint8_t *x, uint8_t *y)
{
    if (len == 0)
        return 0xFF;
    fw_wr8(f, FW_TMP_78, len);
    *x = fw_rd8(f, TN_PHON_LEN);
    if (fw_rd8(f, CMD_VOICE)) {
        do {
            (*y)++;
            fw_wr8(f, (uint16_t)(TN_PHONETIC + *x), rule_byte(f, *y));
            (*x)++;
            fw_wr8(f, FW_TMP_78, (uint8_t)(fw_rd8(f, FW_TMP_78) - 1));
        } while (fw_rd8(f, FW_TMP_78) != 0);
    }
    fw_wr8(f, TN_PHON_LEN, *x);
    return 0xFF;
}
```

In `tn_say_char`, replace `tn_append_phonetic_rom(f, rule_byte(f, y), y);` with:

```c
    uint8_t x0 = 0;                         /* X: reloaded from tn_pos below, as in the ROM */
    tn_append_phonetic(f, rule_byte(f, y), &x0, &y);
```

`src/firmware/lts.c`: in `lts_match_list`, replace `tn_append_phonetic_rom(f, a, y);  /* a = the output length (the ROM jumps there) */` with:

```c
                uint8_t x = 0;                    /* not an output of lts_match_list */
                tn_append_phonetic(f, a, &x, &y); /* a = the output length (the ROM jumps there) */
```

`src/engine/thunks.c`:
- After `tn_rule_skip_field_regs`, add:

```c
/* tn_append_phonetic $B154: A (length), X, Y in; A = $FF, X, Y out (set exactly, though cmd_put_phonetic
   restores A and Y) */
static void tn_append_phonetic_regs(fw *f, cpu740 *c)
{
    uint8_t len = c->a, x = c->x, y = c->y;
    c->a = tn_append_phonetic(f, len, &x, &y);
    c->x = x;
    c->y = y;
}
```

- Add `THUNK(tn_append_phonetic)` after `THUNK(tn_rule_skip_field)`.
- Add the table entry after the `tn_rule_skip_field` entry:

```c
    { "tn_append_phonetic", 0xB154, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_tn_append_phonetic },
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_tn"`, then `cmd //c ".\build.bat test fw_lts"`, then `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_tn` and `suite_fw_lts` ok.
- `diff_lts`: 11 routines, each with more than 0 calls checked and 0 mismatches. `tn_append_phonetic` is reached from ROM `tn_say_char`, ROM `lts_match_list` (via `BCC`) and `cmd_put_phonetic` (the `version` corpus file).
- `golden_hybrid`: `27 corpus files x 12 modes`, all identical.
- All 29 suites ok.

Then confirm the ROM call is gone: `grep -rn "B154\|tn_append_phonetic_rom" src/firmware` must print nothing.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/tn_rules.h src/firmware/tn_rules.c src/firmware/lts.c src/engine/thunks.c tests/test_fw_tn.c tests/test_fw_lts.c
git commit -m "tn_append_phonetic in C: the C routines call it directly; hooked for cmd_put_phonetic (M4.1 2.1)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Parent spec rev 8, M4.1 status and README (criterion 7)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 8**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 8)*.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 8)* Revision 8 records milestone 4.1: text normalisation (tn_convert_clause, tn_append_phonetic) in C, the phonetic strings converted, and a per-call ROM budget (design: docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md). `

2. **§6 step 3** (the paragraph that begins `*(rev 7)* Built in M4.0:`). Append:

   `*(rev 8)* M4.1: fw_rom_call takes a cycle budget per call; helpers keep 10 M cycles, a call that plays speech (pros_make_segments, boot_loop_play from tn_convert_clause's mid-clause flush) gets 1 G cycles.`

3. **§7.1**, the differential-test bullet that ends `so no states are stored.` Append:

   `*(rev 8)* A call that reaches boot_loop_play (a mid-clause flush) cannot finish with interrupts held; it is skipped and counted, and golden_hybrid covers it through the long_clause golden.`

4. **§8 milestone 4** (the paragraph that begins `*(rev 7)* **M4.0 done 2026-10-03**`). Append:

   `*(rev 8)* **M4.1 done** (date of the final commit): tn_convert_clause and tn_append_phonetic run as C (11 routines); the five phonetic strings are converted; a long_clause golden (rom engine) covers the mid-clause flush; diff_lts and golden_hybrid (27 files x 12 modes) pass. Next: M4.2, the command parser.`

- [ ] **Step 2: M4.1 spec status**

In `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-03-m4-1-text-normalisation.md)`

If the implementation differs from the spec anywhere, add a final section `## 7. As built`, listing each difference as one bullet. The Task 4 check of Y in `pros_make_segments` and `boot_loop_play` belongs there. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace the sentence:

`Today these are the letter-to-sound routines; the rest still runs on the emulated CPU.`

with:

`Today these are the letter-to-sound routines (M4.0) and text normalisation, tn_convert_clause and tn_append_phonetic (M4.1); the rest still runs on the emulated CPU.`

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all 29 suites ok.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md README.md
git commit -m "Spec rev 8: milestone 4.1 done (text normalisation in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
