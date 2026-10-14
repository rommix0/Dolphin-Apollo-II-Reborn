# M5b The Native-Only Program and the Emulator as the Test Oracle — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `src/` the product: `apollo2.exe` builds from `src/` alone and speaks through the native engine with no ROM. Move the emulator under `tests/oracle/`, where it serves only the tests and the three analysis tools. Prove mechanically and repeatably that nothing in the product needs the ROM.

**Architecture:** The tasks run in a different order from the spec's outline (§5.3). The move comes third, because the product can build from `src/` alone only once the CLI no longer uses the emulator.
- **The render API and the board's reports (Task 1).**
  - `render_text_native` takes the caller's `native_engine`.
  - `render_options.engine` moves into the emulator-side `render_text` as a parameter, and its callers pass it.
  - `board_print_fault` and `board_print_unmodelled` are added.
  - The new ROM-free `suite_render_native` covers several renders in one process.
- **The CLI (Task 2).**
  - `apollo2` becomes native-only: no `--engine`, `--rom` or `--rom-any`, the board's fault and log reports, and the new exit-code line.
  - Its WAVs are byte-identical to the M5a CLI's.
  - `tests/check_cli.bat` checks the program itself.
- **The move (Task 3).**
  - `git mv` the emulator to `tests/oracle/`: cpu740, machine, the rom and hybrid engines, thunks, rom_id, `render.c/.h`, and the three tool apps. The subfolders mirror `src/`, so no `#include` line changes.
  - `build.bat` builds in three steps:
    - the product from `src\` alone, with `/Isrc` and its objects in `build\product\`;
    - the oracle tools under their old names;
    - the tests with `/Isrc /Itests\oracle`.
  - The `build\noemu` check is removed.
- **The no-ROM proof (Task 4).**
  - `test_rom()` records each call and honours `APOLLO2_NO_ROM`.
  - The suites are marked `SUITE`/`SUITE_ROM`, and the runner enforces the marks.
  - `build.bat test-norom` is added.
  - The one-time run happens in a worktree with no ROM file.
- **Docs (Task 5).** Parent spec rev 20, the M5b spec's As built section, and the README.
- **Unchanged.** The firmware C, the board's behaviour (it only gains two print functions), the machine model, the synthesizer, the goldens and every existing suite's output.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`), cmd batch.

**Spec:** `docs/superpowers/specs/2026-10-14-m5b-native-cli-design.md` (M5b spec). Its foundation is the M5a spec, `docs/superpowers/specs/2026-10-13-m5a-native-host-design.md` (the board, the native engine, `render_text_native`, the build check; §6 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 19; R1, §3, §8 milestone 5).

## Global Constraints

**Build and run**
- Build and test from the repo root with the Bash tool (Git Bash): `cmd //c ".\build.bat test [SUITE_FILTER]"`. From Task 4 on, also `cmd //c ".\build.bat test-norom [SUITE_FILTER]"`.
- In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout. The one-time no-ROM run (Task 4) is made in a worktree without it.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional).
  - Fix warnings in code. Never add `/wd` flags.
  - Pragmas: only the existing 4611 ones (`thunks.c`, `native_engine.c`).
- Every `.c` file under `src/` and `tests/` (including `tests/oracle/`) must have a **unique basename**.
- `build.bat` keeps CRLF line endings (`.gitattributes`). Never edit it while a build it started is running.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers. The one exception is `--audio-check`, which plays nothing.
- Audio stays 10 kHz native, with no resampling.
- The full suite takes about 16 minutes. Use a suite filter while iterating and the full run at each task's end, in the background (a foreground Bash call is capped at 600000 ms).

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `*.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs.
- Stage files by explicit path only. Never use `git add -A` or `git add .`. A moved file is staged with `git mv`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`, `apollo2.png`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`).
  - `ghidra740\coverage.bat` (`build\apollo2_coverage.exe`), `ghidra740\validate.bat` (`build\cpu740_vectors.exe`) and `tools/calibrate.py` (`build/apollo2_frames.exe`) must keep finding their tools under the same names. `tools/` does not change.

**Correctness rules**
- The goldens in `tests/golden/*.frm` stay byte-identical. No new golden. If anything changes a golden, stop and report.
- The machine model, the CPU core, the firmware C, the converted data, the synthesizer and the board's behaviour do not change.
  - Moved files change only where this plan says. Today that is nothing beyond their location and, for `render.c/.h`, the engine parameter.
  - `src/board/board.c/.h` only gain `board_print_fault` and `board_print_unmodelled`.
- Nothing under `src/` includes an oracle header (anything now under `tests/oracle/`). The product build (`/Isrc` only) enforces this.
- The existing suites keep their output:
  - `diff_main`: 66 runs, 129 idles, 31,533 frame events;
  - `diff_native`: 69 runs, 135 idles, 40,329 frame events;
  - `golden_native`: 48 goldens, 23,281 frames;
  - `firmware_data`: 49,104 bytes.
- For every text and option, `apollo2 -o FILE` writes byte for byte the WAV the M5a CLI writes with its default engine (M5b spec §3.4).
- The oracle tools keep their options (`--rom`, `--rom-any`, `APOLLO2_ROM`) and their output.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's rule for this session.

## Review Focus

These are the inputs and conditions most likely to bite a user that the spec implies but no other test exercises. Each line has a test in the task that owns the code.

1. **Old command lines.** A script written for M4/M5a passes `--engine rom`, `--engine hybrid`, `--rom PATH` or `--rom-any`. It must get the usage text and exit code 1, with no WAV written and nothing played, never a silent run that ignores the option. Test: `tests/check_cli.bat` (Task 2), run by `build.bat test-norom` (Task 4).
2. **No ROM anywhere, or a stale `APOLLO2_ROM`.**
   - Cases: `apollo2` run from a folder with no ROM file, with `APOLLO2_ROM` unset, set to a missing file, or set to a file that is not the ROM.
   - Each must exit 0 and write the WAV it writes in the repo root.
   - Test: `tests/check_cli.bat` (Task 2).
3. **A text that faults the firmware.**
   - Neither fault can reach the CLI. Its text is a C string (an argument, or the first line of `--text-file`, which ends at its first NUL), so it cannot carry the NUL byte the NUL bug needs. Its 1500-character limit keeps the input (with the `@K1 @R7 ` prefix and the CR) far below the 4095 bytes the serial queue holds.
   - Through the engine the CLI uses (`render_text_native` on one `native_engine`; the NUL bug through `native_engine_run_input` on that engine), the NUL-bug input must end `ENGINE_FAULT` at $A893, and 4091 spaces must be the input overflow, each with no sample. `board_print_fault` must print `firmware fault at $A893 after 819837 cycles` and `firmware fault: input longer than the serial queue after 819809 cycles`.
   - On `ENGINE_FAULT` the CLI returns 2 before it writes or plays anything, so no WAV is written and a file already at the `-o` path is left as it was.
   - Test: `suite_render_native` (Task 1).
4. **Every option combination, identical to before.** The WAV must equal the M5a CLI's byte for byte for each of:
   - `--voice b`, `--stream raw`, `--prosody 0`, `--prosody 7`, `--hyp b11,b13`;
   - a text with device commands (`@W5 @F6`);
   - a 1500-character `--text-file`.
   1501 characters must still exit 1. Test: the reference renders made at the start of Task 2 with the exe as it is there (Task 1 does not change its output), compared byte for byte at its end and again after the move in Task 3; the 1501 characters in `tests/check_cli.bat` (Task 2).
5. **Filtered and mis-marked test runs.**
   - `build.bat test-norom golden` matches `suite_golden` and `suite_golden_hybrid` (ROM suites) and `suite_golden_native` (ROM-free); `build.bat test-norom diff_main` matches only `suite_diff_main`. Both must pass, the ROM suites with their ROM parts skipped.
   - A suite whose mark is wrong (ROM-free but asking for the ROM, or ROM-marked but never asking) must fail in every mode.
   - Test: the runner's verdict is a pure function, unit-tested over every combination of mark, ROM request, skips and mode, in `suite_smoke` (Task 4), so the suite count stays as the spec has it. The filtered runs are Run steps in Task 4.

---

### Task 1: The render API (`render_text_native` on the caller's engine, the engine a parameter of `render_text`), the board's reports (`board_print_fault`, `board_print_unmodelled`) and `suite_render_native` (M5b spec §3.2, §3.3, §4.5)

**Facts checked / what was verified while proving the plan** (`src/render/render.c/.h`, `src/render/render_native.c/.h`, `src/engine/native_engine.c/.h`, `src/board/board.c/.h`, `src/machine/machine.c`, `src/apps/apollo2.c`, `tests/test_render.c`, `tests/test_golden_hybrid.c`, `tests/test_native.c`; everything below was run):
- **The callers.** `grep -rn "render_text\|\.engine\b\|RENDER_ENGINE" src tests` finds `render_text` in `src/apps/apollo2.c` (one call; `opt.engine` set by `--engine`), `tests/test_render.c` (one hybrid call in `same_on_native`, through a copy of the options with `engine` set, and six ROM calls in `suite_render`) and `tests/test_golden_hybrid.c` (`test_render_matches_rom`: the ROM engine, then `opt.engine = RENDER_ENGINE_HYBRID` and the hybrid). `render_text_native` is called twice, both in `tests/test_render.c`. Nothing else reads `render_options.engine`.
- **The engine's state.** `render_text_native` mallocs a `native_engine` per call. `native_engine_run_input` wipes the engine it is given (`memset`) and calls `board_init` at every run, so an engine the caller keeps starts each run afresh. `native_engine.h` includes only `board/board.h`, `engine/frame_list.h` and `engine/native_host.h`, which `build.bat` copies to `build\noemu`: `render_native.h` can include it and `render_native.c` still passes the M5a syntax check.
- **The faults and their addresses** (`board.h`, `board.c`, `native_engine.c`): `BOARD_FAULT_VECTOR` (`board_call`, `fault_addr` = the address with no C) and `BOARD_FAULT_FIRMWARE` (`run_entries`: `f->fault_addr` when the C faulted, else the entry) have an address. `BOARD_FAULT_INPUT_OVERFLOW` is set by the native engine's idle handler at the first idle, `BOARD_FAULT_DME` by `board_write_sfr`, `BOARD_FAULT_ESCAPED` by a wait's step or a bank entry; none of these three sets `fault_addr`.
- **The machine's reports** (`machine.c`): `machine_dump` prints `machine fault: <kind>` (with `(at $xxxx)` for a firmware fault), the CPU registers, the last instructions and then `machine_print_unmodelled`, whose lines are `unmodelled read|write $xxxx (first at PC $xxxx)` and `unmodelled log full: N more accesses not listed`. The board logs no PC (`board_access` is an address and a direction).
- **The two faults through one engine** (observed): `"@A"`, a NUL and the CR through `native_engine_run_input` end `ENGINE_FAULT` with `BOARD_FAULT_FIRMWARE` at `ROM_CMD_RUN_NEXT` ($A893) after 819,837 cycles, with no frame. `render_text_native` with 4091 spaces (`"@R4 "`, the spaces and the CR: 4096 bytes, one more than the 4095 the 4096-entry ring holds) ends `ENGINE_FAULT` with `BOARD_FAULT_INPUT_OVERFLOW` after 819,809 cycles, at the first idle. A freshly zeroed engine gives the same.
- **The unmodelled log** (`board_read_sfr`, `board_write_sfr`, `m50734_is_modelled`): $DA is unmodelled both ways; a read of `M50734_UART_TX` ($E4) is logged; each address and direction is logged once. No corpus text makes an unmodelled access on native.
- **No collision**: `grep -rn "board_print_\|suite_render_native\|test_render_native" src tests` prints nothing before this task. `board.c` does not include `<stdio.h>`; `board.h` gains it for `FILE`.

**Files:**
- Create: `tests/test_render_native.c`
- Modify: `src/board/board.h`, `src/board/board.c`, `src/render/render_native.h`, `src/render/render_native.c`, `src/render/render.h`, `src/render/render.c`, `src/apps/apollo2.c`, `tests/test_render.c`, `tests/test_golden_hybrid.c`, `tests/suites.h`

**Interfaces:**
- Consumes: `native_engine`, `native_engine_speak`, `native_engine_run_input` (`engine/native_engine.h`); `board`, `board_fault`, `board_read_sfr`, `board_write_sfr`, `M50734_UART_TX` (`board/board.h`); `ROM_CMD_RUN_NEXT` (`firmware/cmd_addr.h`); `test_read_file` (`tests/rom_fixture.h`).
- Produces (Tasks 2–4 use them):

```c
/* src/render/render_native.h: render_engine and render_options.engine are gone; it includes engine/native_engine.h */
typedef struct { char voice; render_stream stream; int prosody; synth_options synth; } render_options;
engine_status render_text_native(native_engine *e, const char *text, const render_options *opt, render_result *out);
/* src/render/render.h (tests/oracle/render/render.h from Task 3) */
typedef enum { RENDER_ENGINE_ROM, RENDER_ENGINE_HYBRID } render_engine;
machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text,
                           const render_options *opt, render_result *out);
/* src/board/board.h */
void board_print_fault(const board *b, FILE *out);
void board_print_unmodelled(const board *b, FILE *out);
/* tests/test_render_native.c: suite_render_native, the 45th suite */
```

- [ ] **Step 1: Write the failing test**

**Create `tests/test_render_native.c`:**

```c
/* Several renders through one native engine, as the CLI makes them (M5b spec 4.5; the M5a deferred CLI-level test):
   corpus texts, the NUL bug's fault, an input overflow, then the corpus texts again, voice b and the raw stream among
   them. Each run's result is what a freshly zeroed engine gives for the same input. Then the CLI's reports as exact
   strings: board_print_fault's line for the NUL bug, the overflow and every other fault kind, and
   board_print_unmodelled's lines for injected unmodelled accesses and a full log. Needs no ROM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "render/render_native.h"
#include "firmware/cmd_addr.h"

#define REPORT_PATH "build/test_render_native.txt"
#define OVERFLOW_SPACES 4091   /* "@R4 ", these spaces and the CR: 4096 bytes, one more than the serial queue holds */

static native_engine g_e;       /* every run of the sequence */
static native_engine g_fresh;   /* zeroed before each run: what a fresh process gives */

/* What a report function writes, read back from a file. Caller frees. */
static char *report(void (*print)(const board *b, FILE *out), const board *b)
{
    FILE *f = fopen(REPORT_PATH, "wb");
    CHECK(f != NULL);
    if (!f)
        return NULL;
    print(b, f);
    fclose(f);
    return test_read_file(REPORT_PATH);
}

static void check_report(void (*print)(const board *b, FILE *out), const board *b, const char *want)
{
    char *got = report(print, b);
    CHECK(got != NULL);
    if (got)
        CHECK_STR(got, want);
    free(got);
}

/* One render of text on g_e, then on a zeroed g_fresh: the same status, samples, sample count, frame count and
   clipping, and for a fault the same fault kind and address. Returns g_e's status. */
static engine_status render_same(const char *name, const char *text, const render_options *opt)
{
    render_result a = { 0 }, b = { 0 };
    engine_status sa = render_text_native(&g_e, text, opt, &a);
    memset(&g_fresh, 0, sizeof g_fresh);
    engine_status sb = render_text_native(&g_fresh, text, opt, &b);
    bool same = sa == sb && a.count == b.count && a.frames == b.frames && a.clipped == b.clipped &&
                (a.count == 0 || !memcmp(a.samples, b.samples, a.count * sizeof a.samples[0])) &&
                g_e.b.fault == g_fresh.b.fault && g_e.b.fault_addr == g_fresh.b.fault_addr;
    if (!same) {
        fprintf(stderr, "  render_native %s: used engine %d, %zu samples, fault %d at $%04X; fresh %d, %zu samples, "
                "fault %d at $%04X\n", name, (int)sa, a.count, (int)g_e.b.fault, g_e.b.fault_addr, (int)sb, b.count,
                (int)g_fresh.b.fault, g_fresh.b.fault_addr);
        g_test_failures++;
    }
    render_result_free(&a);
    render_result_free(&b);
    return sa;
}

/* A corpus text through render_same with voice and stream; it must speak. */
static void corpus_same(const char *name, char voice, render_stream stream)
{
    char path[128];
    snprintf(path, sizeof path, "tests/corpus/%s.txt", name);
    char *text = test_read_file(path);
    CHECK(text != NULL);
    if (!text)
        return;
    char *eol = strpbrk(text, "\r\n");   /* the first line, as the CLI reads a --text-file */
    if (eol)
        *eol = '\0';
    render_options opt;
    render_default_options(&opt);
    opt.voice = voice;
    opt.stream = stream;
    CHECK_EQ(render_same(name, text, &opt), ENGINE_IDLE);
    CHECK(g_e.b.frames_produced > 0);
    free(text);
}

/* The NUL bug (M4.6c1 spec 2.5): "@A", a NUL and the CR, which no C string (so no render, no CLI text) can carry, run
   on g_e and on a zeroed g_fresh: the same firmware fault at $A893, no frame. Then its report line. */
static void nul_bug_same(void)
{
    static const uint8_t bytes[] = { '@', 'A', 0 };
    engine_input in = { bytes, sizeof bytes, true };
    frame_list fa = { 0 }, fb = { 0 };
    CHECK_EQ(native_engine_run_input(&g_e, NULL, &in, false, &fa, NULL, NULL), ENGINE_FAULT);
    memset(&g_fresh, 0, sizeof g_fresh);
    CHECK_EQ(native_engine_run_input(&g_fresh, NULL, &in, false, &fb, NULL, NULL), ENGINE_FAULT);
    CHECK_EQ(g_e.b.fault, BOARD_FAULT_FIRMWARE);
    CHECK_EQ(g_e.b.fault_addr, ROM_CMD_RUN_NEXT);
    CHECK(g_fresh.b.fault == g_e.b.fault && g_fresh.b.fault_addr == g_e.b.fault_addr);
    CHECK_EQ(g_fresh.b.cycles, g_e.b.cycles);
    CHECK(fa.count == 0 && fb.count == 0);
    check_report(board_print_fault, &g_e.b, "firmware fault at $A893 after 819837 cycles\n");
    frame_list_free(&fa);
    frame_list_free(&fb);
}

/* Input one byte longer than the serial queue holds, through render: input overflow, no samples. Then its line. */
static void overflow_same(void)
{
    static char spaces[OVERFLOW_SPACES + 1];
    memset(spaces, ' ', OVERFLOW_SPACES);
    render_options opt;
    render_default_options(&opt);
    CHECK_EQ(render_same("overflow", spaces, &opt), ENGINE_FAULT);
    CHECK_EQ(g_e.b.fault, BOARD_FAULT_INPUT_OVERFLOW);
    check_report(board_print_fault, &g_e.b, "firmware fault: input longer than the serial queue after 819809 cycles\n");
}

/* Every other fault kind's line, from a board with the fault set by hand. */
static void fault_lines(void)
{
    static const struct { board_fault fault; const char *want; } k_lines[] = {
        { BOARD_FAULT_NONE, "no firmware fault after 12345678 cycles\n" },
        { BOARD_FAULT_DME, "firmware fault: DME (data-memory strobe) enabled after 12345678 cycles\n" },
        { BOARD_FAULT_ESCAPED, "firmware fault: code run from a deselected ROM bank after 12345678 cycles\n" },
        { BOARD_FAULT_VECTOR, "firmware fault: unmapped vector $D300 after 12345678 cycles\n" },
        { BOARD_FAULT_INPUT_OVERFLOW, "firmware fault: input longer than the serial queue after 12345678 cycles\n" },
        { BOARD_FAULT_FIRMWARE, "firmware fault at $D300 after 12345678 cycles\n" },
    };
    static board b;
    for (size_t i = 0; i < sizeof k_lines / sizeof k_lines[0]; i++) {
        memset(&b, 0, sizeof b);
        b.cycles = 12345678;
        b.fault = k_lines[i].fault;
        b.fault_addr = 0xD300;
        check_report(board_print_fault, &b, k_lines[i].want);
    }
}

/* The unmodelled log: none after a corpus run; accesses injected on the used engine's board (a read and a write of
   $DA, a read of write-only UART_TX), each listed once; a full log's line. */
static void unmodelled_lines(void)
{
    CHECK_EQ(g_e.b.unmodelled_count, 0);
    check_report(board_print_unmodelled, &g_e.b, "");
    board_read_sfr(&g_e.b, 0xDA);
    board_write_sfr(&g_e.b, 0xDA, 0);
    board_read_sfr(&g_e.b, M50734_UART_TX);
    board_read_sfr(&g_e.b, 0xDA);   /* logged once */
    check_report(board_print_unmodelled, &g_e.b,
                 "unmodelled read $00DA\nunmodelled write $00DA\nunmodelled read $00E4\n");
    g_e.b.unmodelled_dropped = 5;
    check_report(board_print_unmodelled, &g_e.b,
                 "unmodelled read $00DA\nunmodelled write $00DA\nunmodelled read $00E4\n"
                 "unmodelled log full: 5 more accesses not listed\n");
}

void suite_render_native(void)
{
    corpus_same("hello", 'a', RENDER_STREAM_PRODUCED);
    corpus_same("numbers", 'b', RENDER_STREAM_PRODUCED);
    corpus_same("prose", 'a', RENDER_STREAM_RAW);
    nul_bug_same();
    overflow_same();
    corpus_same("hello", 'a', RENDER_STREAM_PRODUCED);
    corpus_same("numbers", 'b', RENDER_STREAM_PRODUCED);
    corpus_same("prose", 'a', RENDER_STREAM_RAW);
    unmodelled_lines();
    fault_lines();
    remove(REPORT_PATH);
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_golden_native)
```

with

```c
SUITE(suite_golden_native)
SUITE(suite_render_native)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test render_native"`
Expected: the build fails compiling `tests\test_render_native.c`, since `render_native.h` does not declare `native_engine` yet, `render_text_native` takes three arguments and `board_print_fault` does not exist. Among its errors:

```
tests\test_render_native.c(17): error C2061: syntax error: identifier 'g_e'
tests\test_render_native.c(46): error C2197: 'engine_status render_text_native(const char *,const render_options *,render_result *)': too many arguments for call
tests\test_render_native.c(99): error C2065: 'board_print_fault': undeclared identifier
```

- [ ] **Step 3: Implement**

**`src/board/board.h`**: replace

```c
#include <stdint.h>
#include "board/m50734.h"
```

with

```c
#include <stdint.h>
#include <stdio.h>
#include "board/m50734.h"
```

then replace

```c
bool board_wait_uart_tx(board *b, uint64_t end);

#endif
```

with

```c
bool board_wait_uart_tx(board *b, uint64_t end);
/* The CLI's reports (M5b spec 3.2); the board has no CPU, so they name no PC.
   - board_print_fault: one line, the fault with its address where it has one, and the cycles spent:
     BOARD_FAULT_FIRMWARE   "firmware fault at $A893 after N cycles" (the NUL bug: fault_addr)
     BOARD_FAULT_DME        "firmware fault: DME (data-memory strobe) enabled after N cycles"
     BOARD_FAULT_ESCAPED    "firmware fault: code run from a deselected ROM bank after N cycles"
     BOARD_FAULT_VECTOR     "firmware fault: unmapped vector $D300 after N cycles" (fault_addr)
     BOARD_FAULT_INPUT_OVERFLOW "firmware fault: input longer than the serial queue after N cycles"
     BOARD_FAULT_NONE       "no firmware fault after N cycles"
   - board_print_unmodelled: machine_print_unmodelled without the PC: "unmodelled read $00DA" or
     "unmodelled write $00DA" per logged access, then "unmodelled log full: N more accesses not listed" when accesses
     were dropped. */
void board_print_fault(const board *b, FILE *out);
void board_print_unmodelled(const board *b, FILE *out);

#endif
```

**`src/board/board.c`**: replace (the end of the file)

```c
        if (done)
            return true;   /* the RTS */
    }
    b->handler_due = false;
    return false;
}
```

with

```c
        if (done)
            return true;   /* the RTS */
    }
    b->handler_due = false;
    return false;
}

void board_print_fault(const board *b, FILE *out)
{
    unsigned long long cycles = (unsigned long long)b->cycles;
    switch (b->fault) {
    case BOARD_FAULT_NONE:
        fprintf(out, "no firmware fault after %llu cycles\n", cycles);
        break;
    case BOARD_FAULT_DME:
        fprintf(out, "firmware fault: DME (data-memory strobe) enabled after %llu cycles\n", cycles);
        break;
    case BOARD_FAULT_ESCAPED:
        fprintf(out, "firmware fault: code run from a deselected ROM bank after %llu cycles\n", cycles);
        break;
    case BOARD_FAULT_VECTOR:
        fprintf(out, "firmware fault: unmapped vector $%04X after %llu cycles\n", b->fault_addr, cycles);
        break;
    case BOARD_FAULT_INPUT_OVERFLOW:
        fprintf(out, "firmware fault: input longer than the serial queue after %llu cycles\n", cycles);
        break;
    case BOARD_FAULT_FIRMWARE:
        fprintf(out, "firmware fault at $%04X after %llu cycles\n", b->fault_addr, cycles);
        break;
    }
}

void board_print_unmodelled(const board *b, FILE *out)
{
    for (unsigned i = 0; i < b->unmodelled_count; i++)
        fprintf(out, "unmodelled %s $%04X\n", b->unmodelled[i].write ? "write" : "read", b->unmodelled[i].addr);
    if (b->unmodelled_dropped)
        fprintf(out, "unmodelled log full: %u more accesses not listed\n", b->unmodelled_dropped);
}
```

**Replace the whole of `src/render/render_native.h` with:**

```c
#ifndef RENDER_RENDER_NATIVE_H
#define RENDER_RENDER_NATIVE_H
/* text -> native engine -> frame stream -> synthesizer -> 10 kHz samples (M5a spec 3.7, M5b spec 3.3), and the part
   render_text (render.h, the tests' emulated engines) shares: the options, the result, the input with its prefix and
   the synthesis. No machine/ or cpu740/ header. */
#include <stddef.h>
#include <stdint.h>
#include "engine/frame_list.h"
#include "engine/native_engine.h"
#include "synth/synth.h"

typedef enum {
    RENDER_STREAM_PRODUCED,   /* the golden, timing-independent stream (default) */
    RENDER_STREAM_RAW,        /* what the DSP received: one frame late, CPU-timing dependent */
} render_stream;

typedef struct {
    char voice;               /* 'a' (default) or 'b': speaker table B, selected with @K1 */
    render_stream stream;
    int prosody;              /* 0-7: sent as @Rn before the text (default 4, the firmware's unscaled
                                 contour; the device default is 3); -1 = send nothing */
    synth_options synth;
} render_options;

typedef struct {
    int16_t *samples;         /* malloc'ed: frames * SYNTH_FRAME_SAMPLES samples at SYNTH_RATE */
    size_t count;
    size_t frames;
    uint64_t clipped;
} render_result;

void render_default_options(render_options *opt);

/* The text an engine gets: prefixed with @K1 for voice b and @Rn for the prosody. Caller frees. */
char *render_input(const char *text, const render_options *opt);

/* Synthesizes fl into out (cleared first) with fl's median pitch as the pitch-range centre; nothing for no frame. */
void render_synthesize(const frame_list *fl, const render_options *opt, render_result *out);

/* render_text on the native engine e, the caller's storage: the same input, the chosen stream synthesized only at
   ENGINE_IDLE (never a partial stream). Each run starts e afresh (native_engine_run_input wipes it); when it returns,
   e->b holds the board's final state: for ENGINE_FAULT its fault and fault_addr, and the unmodelled log. */
engine_status render_text_native(native_engine *e, const char *text, const render_options *opt, render_result *out);

void render_result_free(render_result *r);

#endif
```

**`src/render/render_native.c`**: replace

```c
engine_status render_text_native(const char *text, const render_options *opt, render_result *out)
{
    native_engine *e = malloc(sizeof *e);   /* each call its own engine: nothing carries over */
    if (!e)
        abort();
    char *input = render_input(text, opt);
```

with

```c
engine_status render_text_native(native_engine *e, const char *text, const render_options *opt, render_result *out)
{
    char *input = render_input(text, opt);
```

then replace

```c
    free(input);
    free(e);
    return st;
```

with

```c
    free(input);
    return st;
```

**Replace the whole of `src/render/render.h` with:**

```c
#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> emulated firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3), for the tests: the ROM
   engine or the hybrid. The options, the result and the shared input and synthesis are render_native.h's. */
#include <stddef.h>
#include <stdint.h>
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
#include "render/render_native.h"

typedef enum {
    RENDER_ENGINE_ROM,        /* the original firmware, emulated */
    RENDER_ENGINE_HYBRID,     /* the same, with the ported routines running as C (M4) */
} render_engine;

/* Runs text through engine (prefixed with @K1 for voice b and @Rn for the prosody) and synthesizes the chosen stream,
   with the stream's median pitch as the pitch-range centre. Samples are produced only when the engine reaches
   MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text,
                           const render_options *opt, render_result *out);

#endif
```

**Replace the whole of `src/render/render.c` with:**

```c
#include <stdlib.h>
#include <string.h>
#include "render/render.h"

machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text,
                           const render_options *opt, render_result *out)
{
    char *input = render_input(text, opt);
    frame_list produced = { 0 }, raw = { 0 };
    memset(out, 0, sizeof *out);
    frame_list *raw_out = opt->stream == RENDER_STREAM_RAW ? &raw : NULL;
    machine_status st = engine == RENDER_ENGINE_HYBRID
        ? hybrid_engine_speak(m, rom, len, NULL, NULL, input, false, &produced, raw_out, NULL)
        : rom_engine_speak_streams(m, rom, len, NULL, input, &produced, raw_out);
    if (st == MACHINE_IDLE)
        render_synthesize(opt->stream == RENDER_STREAM_RAW ? &raw : &produced, opt, out);
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
```

**`tests/test_render.c`**: replace

```c
static machine g_m;
```

with

```c
static machine g_m;
static native_engine g_ne;
```

then replace

```c
    render_options hopt = *opt;
    render_result h = { 0 }, n = { 0 };
    hopt.engine = RENDER_ENGINE_HYBRID;
    machine_status hs = render_text(&g_m, rom, len, text, &hopt, &h);
    engine_status ns = render_text_native(text, opt, &n);
```

with

```c
    render_result h = { 0 }, n = { 0 };
    machine_status hs = render_text(&g_m, rom, len, RENDER_ENGINE_HYBRID, text, opt, &h);
    engine_status ns = render_text_native(&g_ne, text, opt, &n);
```

then replace

```c
    CHECK_EQ(render_text_native("Hello world.", &opt, &nat), ENGINE_IDLE);   /* no ROM needed */
```

with

```c
    CHECK_EQ(render_text_native(&g_ne, "Hello world.", &opt, &nat), ENGINE_IDLE);   /* no ROM needed */
```

then replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &a), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &a), MACHINE_IDLE);
```

then replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &b), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &b), MACHINE_IDLE);
```

then replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &voice_b), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &voice_b), MACHINE_IDLE);
```

then replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &raw), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &raw), MACHINE_IDLE);
```

then replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &r3), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &r3), MACHINE_IDLE);
```

then replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &plain), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &plain), MACHINE_IDLE);
```

**`tests/test_golden_hybrid.c`**: replace

```c
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &a), MACHINE_IDLE);
    opt.engine = RENDER_ENGINE_HYBRID;
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &b), MACHINE_IDLE);
```

with

```c
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_ROM, "Hello world.", &opt, &a), MACHINE_IDLE);
    CHECK_EQ(render_text(&g_m, rom, len, RENDER_ENGINE_HYBRID, "Hello world.", &opt, &b), MACHINE_IDLE);
```

**`src/apps/apollo2.c`** (the engine leaves the options; the CLI still runs the emulated engines until Task 2): replace

```c
    bool rom_any = false;
    render_options opt;
```

with

```c
    bool rom_any = false;
    render_engine engine = RENDER_ENGINE_ROM;
    render_options opt;
```

then replace

```c
                opt.engine = RENDER_ENGINE_ROM;
            else if (!strcmp(val, "hybrid"))
                opt.engine = RENDER_ENGINE_HYBRID;
```

with

```c
                engine = RENDER_ENGINE_ROM;
            else if (!strcmp(val, "hybrid"))
                engine = RENDER_ENGINE_HYBRID;
```

then replace

```c
    machine_status st = render_text(&g_machine, rom, sizeof rom, text, &opt, &r);
```

with

```c
    machine_status st = render_text(&g_machine, rom, sizeof rom, engine, text, &opt, &r);
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test render"`
Expected: the build passes with no warnings (both syntax checks included; on this machine `vcvars64.bat` first prints `'vswhere.exe' is not recognized as an internal or external command,` and `operable program or batch file.`, here and in every build of this plan, which is not the build's output), and

```
  native: 47 corpus texts, 2328100 samples the same as the hybrid's
suite_render                 ok
suite_render_native          ok
2 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 16 minutes)
Expected:

- `45 suite(s), 0 failure(s), 0 skip(s)`, no warnings, both syntax checks passing (967 s in the proof run, the build included);
- `suite_render_native          ok` before the summary, and every other line of the output exactly as at 0b74649, among them: `firmware_data` `49104 converted bytes compared with the ROM, 0 differ`; `diff_main` `66 runs, 129 idles, 31533 frame events compared in all`; `diff_native` `69 runs, 135 idles, 40329 frame events compared in all`; `golden_native` `48 goldens, 23281 frames reproduced on the native engine`; `suite_render` `native: 47 corpus texts, 2328100 samples the same as the hybrid's`; `golden_hybrid` `48 corpus files x 132 modes (all, then each routine alone)`; `suite_golden` `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles`, `hybrid timer scale 1/4: 105 frames, 11678411 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 973321 CPU cycles`, `native timer scale 1/4: 105 frames, 11678411 CPU cycles`, `native timer scale 3/1: 105 frames, 973321 CPU cycles`; `suite_synth` `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`; `diff_lts`, `diff_play` and `diff_serial` line for line;
- every golden unchanged (`git status --short tests/golden` prints nothing).

- [ ] **Step 6: Commit**

```bash
git add tests/test_render_native.c tests/suites.h tests/test_render.c tests/test_golden_hybrid.c src/board/board.h src/board/board.c src/render/render_native.h src/render/render_native.c src/render/render.h src/render/render.c src/apps/apollo2.c
git commit -m "render_text_native on the caller's engine, the engine a parameter of render_text, the board's fault and unmodelled reports, suite_render_native (several renders through one engine, a fault among them, and the report lines) (M5b 3.2, 3.3, 4.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 2: The native-only CLI (`render_text_native` on one static engine, the board's reports, no `--engine`, `--rom` or `--rom-any`), its WAVs byte-identical to the M5a CLI's, `tests/check_cli.bat` (M5b spec §3.1, §3.2, §3.4)

**Facts checked / what was verified while proving the plan** (`src/apps/apollo2.c`, `src/render/render_native.h`, `src/engine/native_engine.h`, `tools/calibrate.py`, `.gitattributes`; everything below was run):
- **What the CLI loses.** Its ROM parts are `load_rom`, the `APOLLO2_ROM` lookup and the default file name, `rom_id_accept` (`engine/rom_id.h`), `MACHINE_ROM_SIZE`, the static `machine`, `machine_dump` and `machine_print_unmodelled`. Without them it includes `audio/playback.h`, `audio/wav.h` and `render/render_native.h` only, none of which is moved in Task 3.
- **Old options.** Every argument that starts with `-` and matches no option goes to `usage()`, which prints the usage text and returns 1. So once `--engine`, `--rom` and `--rom-any` are no longer options, each is a usage error before any text is read, any engine runs or any file is opened.
- **Faults cannot come from the CLI's text.** The text is a C string: an argument, or the first line of `--text-file` (`read_text_file` cuts it at the first CR or LF, and `strpbrk` and `strlen` stop at a NUL). So the NUL bug's NUL never reaches the firmware. `APOLLO2_MAX_TEXT` (1500) plus the `@K1 @R7 ` prefix and the CR is 1509 bytes, far below the 4095 the serial queue holds. On `ENGINE_FAULT` the CLI returns 2 before `wav_write` or playback; `suite_render_native` (Task 1) covers both faults and their lines.
- **The speaking budget.** `NATIVE_SPEAK_CYCLES` is 400,000,000, as `ROM_ENGINE_SPEAK_CYCLES`, which `suite_native` checks; the 1500-character text of the reference set (34 times `the quick brown fox jumps over the lazy dog `, then `dogs`: `diff_native`'s `rf2_longest`) takes 173,088,979 cycles in `diff_native`, 43% of the budget.
- **The reference set**: the M5a plan's Review Focus 4 cases (`--voice b`, `--stream raw`, `--prosody 0`, `--prosody 7`, `--hyp b11,b13`, `@W5 @F6`, the 1500-character `--text-file`) and all 47 corpus texts (`--text-file tests/corpus/<name>.txt`): 54 WAVs, each exit 0. The CLI of Task 1 writes the same bytes as the CLI at 0b74649 for every one of them (checked while proving the plan).
- **`tools/calibrate.py`** runs `build/apollo2.exe` with `--voice`, `--hyp`, `--prosody`, `--text-file` and `-o` only, all kept.
- **`.gitattributes`** has `*.bat text eol=crlf`, which covers `tests/check_cli.bat`; it needs no change. `cmd` runs the script from a CRLF file; `cmd` treats `!` specially only with delayed expansion, which the script turns off.

**Files:**
- Create: `tests/check_cli.bat`
- Modify: `src/apps/apollo2.c`

**Interfaces:**
- Consumes: `render_text_native`, `render_default_options`, `render_result_free`, `native_engine` (Task 1, `render/render_native.h`); `board_print_fault`, `board_print_unmodelled` (Task 1); `playback_supported`, `playback_pcm16`, `wav_write`.
- Produces:

```
build\apollo2.exe [options] TEXT | --text-file FILE   (options: -o FILE, --voice a|b, --hyp LIST, --stream S, --prosody N,
                                                       --audio-check); exit codes 0 ok, 1 usage/file error,
                                                       2 firmware fault, 3 cycle limit, 4 audio error,
                                                       5 unmodelled access logged
tests\check_cli.bat EXE    (from the repo root: prints "check_cli: ok" and exits 0, or one line per failed check,
                            "check_cli: N failure(s)" and exit 1)
```

- [ ] **Step 1: Render the reference WAVs with the M5a CLI**

The CLI as it is after Task 1 runs the ROM engine by default (Task 1 changed none of its output). Render the reference set with it, and keep a copy of the exe for Step 2. Nothing here is committed (`build/` is ignored).

```bash
export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"
cmd //c ".\build.bat"
cp build/apollo2.exe build/apollo2_m5a.exe
DIR=build/m5b_ref
mkdir -p $DIR
{ for i in $(seq 34); do printf 'the quick brown fox jumps over the lazy dog '; done; printf 'dogs'; } > build/m5b_long1500.txt
wc -c < build/m5b_long1500.txt
render() { out=$1; shift; build/apollo2.exe "$@" -o "$DIR/$out.wav" || echo "$out: exit $?"; }
render voice_b --voice b "Is this the second voice? It is."
render stream_raw --stream raw "Hello world."
render prosody0 --prosody 0 "Hello world."
render prosody7 --prosody 7 "Hello world."
render hyp_b11_b13 --hyp b11,b13 "Hello world."
render commands "@W5 @F6 Hello world."
render long1500 --text-file build/m5b_long1500.txt
for f in tests/corpus/*.txt; do render corpus_$(basename $f .txt) --text-file $f; done
ls $DIR/*.wav | wc -l
```

Expected: `1500`, then `54`, and no `exit` line.

- [ ] **Step 2: Write the CLI check and see it fail on the M5a CLI**

**Create `tests/check_cli.bat`** (CRLF line endings, as every `.bat`):

```bat
@echo off
rem Checks the program itself (the M5b plan's Review Focus 1, 2 and 4): the M5a options --engine, --rom and --rom-any
rem are usage errors that write nothing and leave an existing file as it was; the program needs no ROM, run from a
rem folder with no ROM file and APOLLO2_ROM unset, naming a missing file or naming a file that is not the ROM, and
rem writes there the WAV it writes in the repo root; 1501 characters are refused. Every run writes a WAV with -o:
rem nothing is ever played. Usage, from the repo root: tests\check_cli.bat EXE
setlocal EnableExtensions DisableDelayedExpansion
if "%~1"=="" (
    echo usage: tests\check_cli.bat EXE
    exit /b 1
)
set "EXE=%~f1"
set FAILS=0
set "WORK=%TEMP%\apollo2_check_cli_%RANDOM%"
if exist "%WORK%" rmdir /s /q "%WORK%"
mkdir "%WORK%" || exit /b 1

rem The reference: "Hello." rendered in the repo root, with APOLLO2_ROM as the caller has it.
"%EXE%" -o "%WORK%\root.wav" "Hello." 2>"%WORK%\err.txt"
call :expect_rc %errorlevel% 0 "Hello. in the repo root"
call :expect_wav "%WORK%\root.wav" "Hello. in the repo root"

pushd "%WORK%"
echo keep>keep.wav

rem Review Focus 1: the M5a options are unknown options: the usage text, exit code 1, no WAV written.
"%EXE%" --engine rom -o engine_rom.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--engine rom"
call :expect_usage "--engine rom"
call :expect_absent engine_rom.wav "--engine rom"
"%EXE%" --engine hybrid -o engine_hybrid.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--engine hybrid"
call :expect_usage "--engine hybrid"
call :expect_absent engine_hybrid.wav "--engine hybrid"
"%EXE%" --rom UK_V2.01_4.04_ROM.BIN -o rom.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--rom PATH"
call :expect_usage "--rom PATH"
call :expect_absent rom.wav "--rom PATH"
"%EXE%" --rom-any -o rom_any.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--rom-any"
call :expect_usage "--rom-any"
call :expect_absent rom_any.wav "--rom-any"
"%EXE%" --engine rom -o keep.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--engine rom onto an existing file"
call :expect_kept "--engine rom onto an existing file"

rem Review Focus 2: no ROM file here; APOLLO2_ROM unset, naming a missing file, naming a file that is not the ROM.
set APOLLO2_ROM=
"%EXE%" -o unset.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 0 "APOLLO2_ROM unset"
call :expect_same unset.wav "APOLLO2_ROM unset"
set "APOLLO2_ROM=%WORK%\missing.bin"
"%EXE%" -o missing.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 0 "APOLLO2_ROM missing"
call :expect_same missing.wav "APOLLO2_ROM missing"
echo not a ROM>notrom.bin
set "APOLLO2_ROM=%WORK%\notrom.bin"
"%EXE%" -o notrom.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 0 "APOLLO2_ROM not the ROM"
call :expect_same notrom.wav "APOLLO2_ROM not the ROM"
set APOLLO2_ROM=

rem Review Focus 4: 1501 characters are refused (exit 1), and an existing file is left as it was.
set "LONG="
for /l %%i in (1,1,34) do call set "LONG=%%LONG%%the quick brown fox jumps over the lazy dog "
>long1501.txt echo %LONG%dogs.
"%EXE%" --text-file long1501.txt -o keep.wav 2>err.txt
call :expect_rc %errorlevel% 1 "1501 characters"
findstr /b /l /c:"text too long: 1501 characters (at most 1500)" err.txt >nul || call :fail "1501 characters: no 'text too long' line"
call :expect_kept "1501 characters"

popd
rmdir /s /q "%WORK%"
if %FAILS% neq 0 (
    echo check_cli: %FAILS% failure^(s^)
    exit /b 1
)
echo check_cli: ok
exit /b 0

:fail
echo check_cli: %~1
set /a FAILS+=1
exit /b 0

:expect_rc
if "%~1"=="%~2" exit /b 0
call :fail "%~3: exit code %~1, want %~2"
exit /b 0

:expect_usage
findstr /b /l /c:"usage: apollo2 [options] TEXT" err.txt >nul || call :fail "%~1: no usage text"
findstr /b /l /c:"exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged" err.txt >nul || call :fail "%~1: no exit-code line"
exit /b 0

:expect_absent
if exist "%~1" call :fail "%~2: %~1 was written"
exit /b 0

:expect_kept
for %%f in (keep.wav) do if %%~zf neq 6 (
    call :fail "%~1: keep.wav was changed"
    exit /b 0
)
findstr /b /l /x /c:"keep" keep.wav >nul || call :fail "%~1: keep.wav was changed"
exit /b 0

:expect_wav
if not exist "%~1" (
    call :fail "%~2: no WAV written"
    exit /b 0
)
for %%f in ("%~1") do if %%~zf leq 44 call :fail "%~2: the WAV holds no sample"
exit /b 0

:expect_same
call :expect_wav "%~1" "%~2"
if not exist "%~1" exit /b 0
fc /b "%~1" root.wav >nul || call :fail "%~2: the WAV differs from the repo root's"
exit /b 0
```

Run: `cmd //c "tests\check_cli.bat build\apollo2_m5a.exe"`
Expected: exit code 1, with these lines. The M5a CLI renders with the old options (its `--rom` with a missing file is exit 1, but with no usage text), so `--engine rom` overwrites `keep.wav`, which the 1501-character check then finds changed; and it needs the ROM, which the temporary folder does not have:

```
check_cli: --engine rom: exit code 0, want 1
check_cli: --engine rom: no usage text
check_cli: --engine rom: no exit-code line
check_cli: --engine rom: engine_rom.wav was written
check_cli: --engine hybrid: exit code 0, want 1
check_cli: --engine hybrid: no usage text
check_cli: --engine hybrid: no exit-code line
check_cli: --engine hybrid: engine_hybrid.wav was written
check_cli: --rom PATH: no usage text
check_cli: --rom PATH: no exit-code line
check_cli: --rom-any: exit code 0, want 1
check_cli: --rom-any: no usage text
check_cli: --rom-any: no exit-code line
check_cli: --rom-any: rom_any.wav was written
check_cli: --engine rom onto an existing file: exit code 0, want 1
check_cli: --engine rom onto an existing file: keep.wav was changed
check_cli: APOLLO2_ROM unset: exit code 1, want 0
check_cli: APOLLO2_ROM unset: no WAV written
check_cli: APOLLO2_ROM missing: exit code 1, want 0
check_cli: APOLLO2_ROM missing: no WAV written
check_cli: APOLLO2_ROM not the ROM: exit code 1, want 0
check_cli: APOLLO2_ROM not the ROM: no WAV written
check_cli: 1501 characters: keep.wav was changed
check_cli: 23 failure(s)
```

- [ ] **Step 3: Implement**

**Replace the whole of `src/apps/apollo2.c` with:**

```c
/* apollo2: text -> speech through the Apollo 2 firmware, run as native C on a model of the board (no ROM, no emulated
   CPU), and a Holmes-style parallel-formant synthesizer at 10 kHz (spec R1, R2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/playback.h"
#include "audio/wav.h"
#include "render/render_native.h"

#define APOLLO2_MAX_TEXT 1500   /* keeps the speech inside the native engine's speaking budget, NATIVE_SPEAK_CYCLES
                                   (about 200 s) */

static native_engine g_engine;

static int usage(void)
{
    fprintf(stderr,
            "usage: apollo2 [options] TEXT\n"
            "       apollo2 [options] --text-file FILE\n"
            "  -o FILE            write a WAV file (10 kHz, mono, 16-bit) instead of playing live\n"
            "  --voice a|b        speaker table: a = male (default), b = non-male (@K1)\n"
            "  --hyp LIST         synthesizer hypothesis switches, comma-separated: b11,b13\n"
            "  --stream S         frame stream fed to the synthesizer: produced (default) or raw\n"
            "  --prosody N        intonation strength 0-7, sent as @Rn (default 4 = the firmware's\n"
            "                     unscaled contour; the device's own default is 3)\n"
            "  --text-file FILE   read the text from FILE (first line; device control codes allowed)\n"
            "  --audio-check      report whether live 10 kHz playback is available, then exit\n"
            "TEXT may contain the device's @ commands (docs/apollo2.txt section 6); at most %d characters.\n"
            "exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged\n",
            APOLLO2_MAX_TEXT);
    return 1;
}

/* First line of a file (CR/LF stripped) in a static buffer; NULL if unreadable. */
static const char *read_text_file(const char *path)
{
    static char buf[APOLLO2_MAX_TEXT + 2];
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *eol = strpbrk(buf, "\r\n");
    if (eol)
        *eol = '\0';
    return buf;
}

static bool parse_hyp(const char *list, synth_options *o)
{
    char buf[64];
    if (strlen(list) >= sizeof buf)
        return false;
    strcpy(buf, list);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        if (!strcmp(t, "b11"))
            o->b11_pulse_width = true;
        else if (!strcmp(t, "b13"))
            o->b13_reset = true;
        else
            return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *out_path = NULL, *text = NULL, *text_file = NULL;
    render_options opt;
    render_default_options(&opt);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--audio-check")) {
            bool ok = playback_supported(SYNTH_RATE);
            printf("live playback at %d Hz: %s\n", SYNTH_RATE, ok ? "available" : "NOT available");
            return ok ? 0 : 4;
        } else if (!strcmp(a, "-o") && val) {
            out_path = val;
            i++;
        } else if (!strcmp(a, "--voice") && val && (!strcmp(val, "a") || !strcmp(val, "b"))) {
            opt.voice = val[0];
            i++;
        } else if (!strcmp(a, "--hyp") && val) {
            if (!parse_hyp(val, &opt.synth))
                return usage();
            i++;
        } else if (!strcmp(a, "--stream") && val && (!strcmp(val, "produced") || !strcmp(val, "raw"))) {
            opt.stream = !strcmp(val, "raw") ? RENDER_STREAM_RAW : RENDER_STREAM_PRODUCED;
            i++;
        } else if (!strcmp(a, "--prosody") && val && val[0] >= '0' && val[0] <= '7' && val[1] == '\0') {
            opt.prosody = val[0] - '0';
            i++;
        } else if (!strcmp(a, "--text-file") && val) {
            text_file = val;
            i++;
        } else if (a[0] == '-' && a[1] != '\0') {
            return usage();   /* an unknown option, the M5a CLI's --engine, --rom and --rom-any included */
        } else if (!text) {
            text = a;
        } else {
            return usage();
        }
    }
    if ((text == NULL) == (text_file == NULL))
        return usage();
    if (text_file && !(text = read_text_file(text_file))) {
        fprintf(stderr, "cannot read %s\n", text_file);
        return 1;
    }
    if (!*text) {
        fprintf(stderr, "no text to speak (TEXT or the first line of the file is empty)\n");
        return 1;
    }
    if (strlen(text) > APOLLO2_MAX_TEXT) {
        fprintf(stderr, "text too long: %zu characters (at most %d)\n", strlen(text), APOLLO2_MAX_TEXT);
        return 1;
    }

    render_result r;
    engine_status st = render_text_native(&g_engine, text, &opt, &r);
    if (st == ENGINE_FAULT) {   /* no samples: nothing is written or played */
        board_print_fault(&g_engine.b, stderr);
        board_print_unmodelled(&g_engine.b, stderr);
        return 2;
    }
    if (st == ENGINE_CYCLE_LIMIT) {
        fprintf(stderr, "cycle limit reached before the firmware went idle\n");
        return 3;
    }
    if (r.clipped)
        fprintf(stderr, "warning: %llu samples clipped\n", (unsigned long long)r.clipped);
    int rc = 0;
    if (g_engine.b.unmodelled_count || g_engine.b.unmodelled_dropped) {   /* spec section 4: never accept silently */
        fprintf(stderr, "the firmware made accesses the board model does not cover:\n");
        board_print_unmodelled(&g_engine.b, stderr);
        rc = 5;
    }
    if (out_path) {
        if (!wav_write(out_path, r.samples, r.count, SYNTH_RATE)) {
            fprintf(stderr, "cannot write %s\n", out_path);
            rc = 1;
        }
    } else if (r.count > 0) {
        char err[128];
        if (!playback_pcm16(r.samples, r.count, SYNTH_RATE, err, sizeof err)) {
            fprintf(stderr, "live playback failed: %s\n", err);
            rc = 4;
        }
    }
    render_result_free(&r);
    return rc;
}
```

- [ ] **Step 4: Run the CLI check**

Run: `cmd //c ".\build.bat"`, then `cmd //c "tests\check_cli.bat build\apollo2.exe"`
Expected: the build passes with no warnings (both syntax checks included), then

```
check_cli: ok
```

and exit code 0.

- [ ] **Step 5: Compare the WAVs with the references**

Run (with `APOLLO2_ROM` unset: the program needs no ROM):

```bash
unset APOLLO2_ROM
DIR=build/m5b_new
mkdir -p $DIR
render() { out=$1; shift; build/apollo2.exe "$@" -o "$DIR/$out.wav" || echo "$out: exit $?"; }
render voice_b --voice b "Is this the second voice? It is."
render stream_raw --stream raw "Hello world."
render prosody0 --prosody 0 "Hello world."
render prosody7 --prosody 7 "Hello world."
render hyp_b11_b13 --hyp b11,b13 "Hello world."
render commands "@W5 @F6 Hello world."
render long1500 --text-file build/m5b_long1500.txt
for f in tests/corpus/*.txt; do render corpus_$(basename $f .txt) --text-file $f; done
for f in build/m5b_ref/*.wav; do cmp "$f" "$DIR/$(basename $f)" || echo "differs: $f"; done
echo compared $(ls build/m5b_ref/*.wav | wc -l)
export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"
```

Expected: only `compared 54`: no `exit` line and no `differs` line.

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected: exactly the summary and suite output of Task 1 Step 5:

- `45 suite(s), 0 failure(s), 0 skip(s)`, no warnings, both syntax checks passing (958 s in the proof run), and every line of the suites' output as in Task 1 Step 5;
- every golden unchanged (`git status --short tests/golden` prints nothing).

- [ ] **Step 7: Commit**

```bash
git add src/apps/apollo2.c tests/check_cli.bat
git commit -m "apollo2 native-only: render_text_native on one static engine, the board's fault and unmodelled reports, no --engine, --rom or --rom-any (an old option is a usage error), its WAVs byte-identical to the M5a CLI's; tests/check_cli.bat (M5b 3.1, 3.2, 3.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 3: The move: the emulator under `tests/oracle/` (cpu740, machine, the rom and hybrid engines, thunks, rom_id, `render_text`, the three tools); `build.bat` in three steps, the program from `src\` alone; the `build\noemu` check removed (M5b spec §2)

**Facts checked / what was verified while proving the plan** (`build.bat`, every `#include` under `src/` and `tests/`, `ghidra740/coverage.bat`, `ghidra740/validate.bat`, `tools/calibrate.py`; everything below was run; Ghidra was not):
- **What moves** (24 files): `src/cpu740/` (`cpu740.c/.h`, `disasm740.c`, `optable.c/.h`, `vec740.c/.h`), `src/machine/` (`machine.c/.h`, `coverage.c/.h`), `src/engine/{rom_engine,hybrid_engine,thunks,rom_id}.c/.h`, `src/render/render.c/.h` and `src/apps/{apollo2_frames,apollo2_coverage,cpu740_vectors}.c`. `git mv` leaves `src/cpu740` and `src/machine` as empty folders on disk; they are removed.
- **What stays in `src/`** includes no oracle header: after Task 2, `git grep -n -E '#include "(machine|cpu740)/|engine/(rom_engine|hybrid_engine|thunks|rom_id)\.h|render/render\.h' -- src` prints nothing. The oracle includes product headers (`machine.h` includes `board/m50734.h`, `thunks.h` the firmware's, `render.h` `render/render_native.h`), found through `/Isrc`.
- **No `#include` line changes.** The moved files keep their `src`-relative include lines (`machine/machine.h`, `engine/rom_engine.h`, …), found through `/Itests\oracle`. `apollo2_coverage.c`'s `#include "../../tests/corpus_list.h"` still resolves: from `/Itests\oracle` it is `tests/oracle/../../tests/corpus_list.h`, that is `tests/corpus_list.h`.
- **Unique basenames**: the 123 `.c` files under `src/` and `tests/` have 123 distinct basenames, so the test build's objects in `build\` never collide; the program's are in `build\product\`.
- **The program's build** compiles `src\apps\apollo2.c` with `src\board`, `src\engine`, `src\firmware`, `src\firmware\data`, `src\synth`, `src\audio` and `src\render` (60 objects) with `/Isrc` only. An include of an oracle header fails it: with a stray `#include "machine/machine.h"` in `src/render/render_native.c` the build stops with `fatal error C1083: Cannot open include file: 'machine/machine.h'` (Step 4, reverted). The firmware's own check (`src\firmware` without `/Isrc`) stays; M5a's `build\noemu` check of `src\board` and the native files is what the program's build now enforces, so it goes.
- **The tools** keep their names in `build\`: `ghidra740\coverage.bat` runs `build\apollo2_coverage.exe`, `ghidra740\validate.bat` `build\cpu740_vectors.exe`, `tools/calibrate.py` `build/apollo2_frames.exe` and `build/apollo2.exe`; each calls `build.bat` (the two scripts) or expects a built tree (`calibrate.py`) and finds the tools there. Their outputs before and after the move are byte-identical: `apollo2_frames "Hello world."`, `apollo2_coverage` over the corpus (`rom 4106, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`) and `cpu740_vectors -n 4` with its blank image (928 vector lines).
- **`build.bat` runs while cmd reads it**: edit it only when no build or suite started from it is running.

**Files:**
- Move (`git mv`): the 24 files above, to the same subfolder under `tests/oracle/`
- Modify: `build.bat`

**Interfaces:**
- Consumes: Tasks 1–2 (`apollo2.c` uses only `src/`).
- Produces:

```
src/                 the program: apps/apollo2.c, board/, engine/{frame_list,native_host,native_engine}, firmware/,
                     render/render_native, synth/, audio/
tests/oracle/        the emulator: cpu740/, machine/, engine/{rom_engine,hybrid_engine,thunks,rom_id},
                     render/render, apps/{apollo2_frames,apollo2_coverage,cpu740_vectors}
build.bat            1. build\apollo2.exe from src\ alone (/Isrc, objects in build\product\)
                     2. build\apollo2_frames.exe, build\apollo2_coverage.exe, build\cpu740_vectors.exe
                        (/Isrc /Itests\oracle)
                     3. build\apollo2_tests.exe (tests\*.c, the oracle, src\; /Isrc /Itests\oracle)
```

- [ ] **Step 1: Record the tools' output before the move**

```bash
mkdir -p build/m5b_tools_pre
build/apollo2_frames.exe "Hello world." > build/m5b_tools_pre/frames.txt
build/apollo2_coverage.exe -o build/m5b_tools_pre/corpus.cov
build/cpu740_vectors.exe -o build/m5b_tools_pre/vectors.txt -n 4 --blank build/m5b_tools_pre/blank.bin
```

Expected (with `APOLLO2_ROM` exported):

```
rom 4106, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges
928 vector lines written to build/m5b_tools_pre/vectors.txt
```

- [ ] **Step 2: Move the files**

```bash
mkdir -p tests/oracle/cpu740 tests/oracle/machine tests/oracle/engine tests/oracle/render tests/oracle/apps
for f in cpu740.c cpu740.h disasm740.c optable.c optable.h vec740.c vec740.h; do git mv src/cpu740/$f tests/oracle/cpu740/$f; done
for f in machine.c machine.h coverage.c coverage.h; do git mv src/machine/$f tests/oracle/machine/$f; done
for f in rom_engine.c rom_engine.h hybrid_engine.c hybrid_engine.h thunks.c thunks.h rom_id.c rom_id.h; do git mv src/engine/$f tests/oracle/engine/$f; done
for f in render.c render.h; do git mv src/render/$f tests/oracle/render/$f; done
for f in apollo2_frames.c apollo2_coverage.c cpu740_vectors.c; do git mv src/apps/$f tests/oracle/apps/$f; done
rmdir src/cpu740 src/machine
git status --short | grep -c "^R"
ls src src/apps src/engine src/render
```

Expected: `24`, then

```
src:
apps
audio
board
engine
firmware
render
synth

src/apps:
apollo2.c

src/engine:
frame_list.c
frame_list.h
native_engine.c
native_engine.h
native_host.c
native_host.h

src/render:
render_native.c
render_native.h
```

- [ ] **Step 3: `build.bat` in three steps**

**Replace the whole of `build.bat` with** (CRLF line endings):

```bat
@echo off
rem Builds the program build\apollo2.exe from src\ alone, the oracle tools build\apollo2_frames.exe,
rem build\apollo2_coverage.exe and build\cpu740_vectors.exe, and the tests build\apollo2_tests.exe (M5b spec 2.3).
rem Usage: build.bat [test [SUITE_FILTER]]
setlocal EnableDelayedExpansion
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /D_CRT_SECURE_NO_WARNINGS
set LIBS=winmm.lib
rem M4.0 spec 3.1: src\firmware builds without the emulator. Syntax-check it without /Isrc, so an include of
rem a machine\ or cpu740\ header cannot be found.
set FWSRC=
for %%d in (src\firmware src\firmware\data) do (
    if exist %%d\*.c set FWSRC=!FWSRC! %%d\*.c
)
if defined FWSRC (
    cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS !FWSRC! || exit /b 1
)
rem M5b spec 2.3, step 1: the program, compiled and linked from src\ alone with /Isrc as its only include root and its
rem objects in build\product: an include of an oracle header (tests\oracle) cannot be found, and a call into oracle
rem code cannot link.
set PRODSRC=
for %%d in (src\board src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
    if exist %%d\*.c set PRODSRC=!PRODSRC! %%d\*.c
)
if not exist build\product mkdir build\product
cl %CFLAGS% /Isrc /Fobuild\product\ /Fdbuild\product\ /Febuild\apollo2.exe src\apps\apollo2.c !PRODSRC! %LIBS% || exit /b 1
rem Step 2: the oracle tools, from tests\oracle\apps, under the names ghidra740\coverage.bat, ghidra740\validate.bat and
rem tools\calibrate.py run them by.
set ORACLESRC=
for %%d in (tests\oracle\cpu740 tests\oracle\machine tests\oracle\engine tests\oracle\render) do (
    if exist %%d\*.c set ORACLESRC=!ORACLESRC! %%d\*.c
)
for %%a in (apollo2_frames apollo2_coverage cpu740_vectors) do (
    cl %CFLAGS% /Isrc /Itests\oracle /Fobuild\ /Fdbuild\ /Febuild\%%a.exe tests\oracle\apps\%%a.c !ORACLESRC! !PRODSRC! ^
        %LIBS% || exit /b 1
)
rem Step 3: the tests, with the oracle and the program's sources.
cl %CFLAGS% /Isrc /Itests\oracle /Fobuild\ /Fdbuild\ /Febuild\apollo2_tests.exe tests\*.c !ORACLESRC! !PRODSRC! %LIBS% ^
    || exit /b 1
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%
```

- [ ] **Step 4: Build and check the separation**

Run:

```bash
cmd //c ".\build.bat"
ls build/product/*.obj | wc -l
find src tests -name '*.c' | wc -l
find src tests -name '*.c' | sed 's|.*/||' | sort | uniq -d
git grep -n -E '#include "(machine|cpu740)/|engine/(rom_engine|hybrid_engine|thunks|rom_id)\.h|render/render\.h' -- src
```

Expected: the build passes with no warnings (the firmware's syntax check included), then `60`, then `123`, and the last two commands print nothing.

Then the deliberate check (not committed): a stray oracle include in the program fails its build.

```bash
sed -i 's|#include "render/render_native.h"|#include "render/render_native.h"\n#include "machine/machine.h"|' src/render/render_native.c
cmd //c ".\build.bat" 2>&1 | grep -i "error"
git checkout -- src/render/render_native.c
git status --short src/render/render_native.c
cmd //c ".\build.bat"
```

Expected: `src\render\render_native.c(5): fatal error C1083: Cannot open include file: 'machine/machine.h': No such file or directory`, then nothing from `git status`, and the last build passes.

Then the tools after the move, and the names the scripts run them by:

```bash
mkdir -p build/m5b_tools_post
build/apollo2_frames.exe "Hello world." > build/m5b_tools_post/frames.txt
build/apollo2_coverage.exe -o build/m5b_tools_post/corpus.cov
build/cpu740_vectors.exe -o build/m5b_tools_post/vectors.txt -n 4 --blank build/m5b_tools_post/blank.bin
for f in frames.txt corpus.cov vectors.txt blank.bin; do cmp build/m5b_tools_pre/$f build/m5b_tools_post/$f && echo "same $f"; done
grep -n -F '.exe' ghidra740/coverage.bat ghidra740/validate.bat
grep -n "_EXE = " tools/calibrate.py
```

Expected:

```
rom 4106, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges
928 vector lines written to build/m5b_tools_post/vectors.txt
same frames.txt
same corpus.cov
same vectors.txt
same blank.bin
ghidra740/coverage.bat:10:build\apollo2_coverage.exe -o ghidra740\coverage\corpus.cov %* ghidra740\coverage\extra_index.txt ghidra740\coverage\extra_commands.txt
ghidra740/validate.bat:12:build\cpu740_vectors.exe -o build\ghidra740\vectors.txt -n %N% --blank build\ghidra740\blank.bin || (popd & exit /b 1)
32:FRAMES_EXE = os.path.join(ROOT, 'build', 'apollo2_frames.exe')
33:APOLLO2_EXE = os.path.join(ROOT, 'build', 'apollo2.exe')
```

- [ ] **Step 5: The reference WAVs and the CLI check after the move**

Run:

```bash
unset APOLLO2_ROM
DIR=build/m5b_moved
mkdir -p $DIR
render() { out=$1; shift; build/apollo2.exe "$@" -o "$DIR/$out.wav" || echo "$out: exit $?"; }
render voice_b --voice b "Is this the second voice? It is."
render stream_raw --stream raw "Hello world."
render prosody0 --prosody 0 "Hello world."
render prosody7 --prosody 7 "Hello world."
render hyp_b11_b13 --hyp b11,b13 "Hello world."
render commands "@W5 @F6 Hello world."
render long1500 --text-file build/m5b_long1500.txt
for f in tests/corpus/*.txt; do render corpus_$(basename $f .txt) --text-file $f; done
for f in build/m5b_ref/*.wav; do cmp "$f" "$DIR/$(basename $f)" || echo "differs: $f"; done
echo compared $(ls build/m5b_ref/*.wav | wc -l)
cmd //c "tests\check_cli.bat build\apollo2.exe"
export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"
```

Expected:

```
compared 54
check_cli: ok
```

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected: exactly the summary and suite output of Task 1 Step 5:

- `45 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware's syntax check passing (955 s in the proof run), and every line of the suites' output as in Task 1 Step 5;
- every golden unchanged (`git status --short tests/golden` prints nothing).

- [ ] **Step 7: Commit**

```bash
git add build.bat
git commit -m "The emulator under tests/oracle as the test oracle (cpu740, machine, the rom and hybrid engines, thunks, rom_id, render_text, apollo2_frames, apollo2_coverage, cpu740_vectors); build.bat in three steps: apollo2.exe from src alone, the oracle tools under their names, the tests; the noemu check removed (M5b 2.1-2.3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files (the 24 moves were staged by `git mv`).

---

### Task 4: The no-ROM proof: `test_rom()` records each call and honours `APOLLO2_NO_ROM`, the suites marked `SUITE`/`SUITE_ROM`, the runner's verdict, `build.bat test-norom`; the one-time run with no ROM file (M5b spec §4.1–§4.4, §4.6)

**Facts checked / what was verified while proving the plan** (`tests/rom_fixture.c/.h`, `tests/test_main.c`, `tests/suites.h`, `tests/test.h`, every `test_rom(` and `SKIP(` under `tests/`; everything below was run):
- **Who asks for the ROM.** `test_rom()` is called from 15 suites' files and nowhere else: `test_rom_boot.c`, `test_rom_speak.c` (3 calls), `test_golden.c`, `test_raw_stream.c`, `test_render.c`, `test_rom_id.c`, `test_machine_hooks.c` (3), `test_firmware_data.c`, `test_diff_lts.c`, `test_diff_play.c`, `test_diff_serial.c`, `test_diff_main.c`, `test_golden_hybrid.c`, `test_diff_native.c` and **`test_native.c`**. Each of these suites calls it at least once on every run, the ROM present or not (the norom run counts 15).
- **`suite_native` asks for the ROM** (M5b spec §4.2 lists it among the ROM-free suites): its `test_queue_fit` calls `test_rom()` and, with the ROM, also runs the ROM engine on the same input and compares the end and the frames; without the ROM it runs only the native part and never skips. So it is marked `SUITE_ROM`, and the marks are **30 `SUITE` and 15 `SUITE_ROM`** (the spec's 31 and 14 with `suite_native` moved), 45 in all.
- **Skips.** `SKIP` is used only for the missing ROM and in `suite_audio` (`no waveOut device accepts 10 kHz mono 16-bit PCM`); this machine has the device, so `suite_audio` does not skip.
- **Filters.** The runner matches a filter as a substring of the suite's name: `golden` runs `suite_golden`, `suite_golden_hybrid` (`SUITE_ROM`) and `suite_golden_native` (`SUITE`); `diff_main` runs `suite_diff_main` only; `suite_native` runs `suite_native` only.
- **The ROM in a worktree**: `test_rom()` reads `APOLLO2_ROM`, else `UK_V2.01_4.04_ROM.BIN` in the working folder; nothing else in `tests/` opens a ROM file: `grep -rn "fopen" tests` lists the fixture's two (the ROM, and `test_read_file`), `test_cpu_harte.c`'s vector file from `tests/data`, and files the tests write (`build/…`, `test_vec740.c`'s output, `test_golden.c`'s regeneration of a golden). The program reads no ROM at all (Task 2).
- **`build.bat`'s `exit /b %errorlevel%`** after the suite is read when that line runs, so `test` returns the suite's exit code; `test-norom` fails at the first failing part (`|| exit /b 1`).

**Files:**
- Modify: `tests/test.h`, `tests/test_main.c`, `tests/rom_fixture.c`, `tests/rom_fixture.h`, `tests/suites.h`, `tests/test_smoke.c`, `build.bat`

**Interfaces:**
- Consumes: `tests/check_cli.bat` (Task 2); `build.bat`'s three build steps (Task 3).
- Produces:

```c
/* tests/test.h */
extern bool g_test_rom_asked;
typedef enum { TEST_ROM_FREE, TEST_NEEDS_ROM } test_mark;
const char *test_verdict(const char *name, test_mark mark, bool asked_rom, int skips, bool no_rom);
/* tests/suites.h: SUITE(fn) (ROM-free) and SUITE_ROM(fn) (with ROM parts); test_main.c defines both */
```

```
build.bat test-norom [SUITE_FILTER]   builds as test does, runs build\apollo2_tests.exe [SUITE_FILTER] with
                                      APOLLO2_NO_ROM=1, then tests\check_cli.bat build\apollo2.exe with APOLLO2_ROM unset
summary line 2: ROM-free: <n> suite(s) ran in full; ROM: <m> suite(s) asked for the ROM
```

The messages: `marked ROM-free but asked for the ROM`, `marked as needing the ROM but never asked for it` (every mode), `marked ROM-free but skipped with APOLLO2_NO_ROM set` (norom mode; not `suite_audio`). `<n>` counts the `SUITE` suites that ran with no skip and did not ask for the ROM, `<m>` the suites that called `test_rom()`, among the suites run.

No name collides: `grep -rnw "g_test_rom_asked\|test_mark\|test_verdict\|SUITE_ROM\|APOLLO2_NO_ROM\|test-norom" src tests build.bat` prints nothing before this task.

- [ ] **Step 1: Write the failing test**

**Replace the whole of `tests/test_smoke.c` with:**

```c
#include "test.h"

#define FREE_ASKED "marked ROM-free but asked for the ROM"
#define ROM_UNASKED "marked as needing the ROM but never asked for it"
#define FREE_SKIPPED "marked ROM-free but skipped with APOLLO2_NO_ROM set"

/* The runner's verdict (M5b spec 4.3; the M5b plan's Review Focus 5) over every combination of mark, ROM request,
   skips and mode, for an ordinary suite and for suite_audio, the one ROM-free suite allowed a skip. */
static void test_verdict_table(void)
{
    static const struct {
        const char *name;
        test_mark mark;
        bool asked;
        int skips;
        bool no_rom;
        const char *want;
    } k_rows[] = {
        { "suite_x", TEST_ROM_FREE, false, 0, false, NULL },
        { "suite_x", TEST_ROM_FREE, false, 0, true, NULL },
        { "suite_x", TEST_ROM_FREE, false, 1, false, NULL },
        { "suite_x", TEST_ROM_FREE, false, 1, true, FREE_SKIPPED },
        { "suite_x", TEST_ROM_FREE, true, 0, false, FREE_ASKED },
        { "suite_x", TEST_ROM_FREE, true, 0, true, FREE_ASKED },
        { "suite_x", TEST_ROM_FREE, true, 1, false, FREE_ASKED },
        { "suite_x", TEST_ROM_FREE, true, 1, true, FREE_ASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 0, false, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 0, true, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 1, false, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, false, 1, true, ROM_UNASKED },
        { "suite_x", TEST_NEEDS_ROM, true, 0, false, NULL },
        { "suite_x", TEST_NEEDS_ROM, true, 0, true, NULL },
        { "suite_x", TEST_NEEDS_ROM, true, 1, false, NULL },
        { "suite_x", TEST_NEEDS_ROM, true, 1, true, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 0, false, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 0, true, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 1, false, NULL },
        { "suite_audio", TEST_ROM_FREE, false, 1, true, NULL },
        { "suite_audio", TEST_ROM_FREE, true, 0, false, FREE_ASKED },
        { "suite_audio", TEST_ROM_FREE, true, 0, true, FREE_ASKED },
        { "suite_audio", TEST_ROM_FREE, true, 1, false, FREE_ASKED },
        { "suite_audio", TEST_ROM_FREE, true, 1, true, FREE_ASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 0, false, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 0, true, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 1, false, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, false, 1, true, ROM_UNASKED },
        { "suite_audio", TEST_NEEDS_ROM, true, 0, false, NULL },
        { "suite_audio", TEST_NEEDS_ROM, true, 0, true, NULL },
        { "suite_audio", TEST_NEEDS_ROM, true, 1, false, NULL },
        { "suite_audio", TEST_NEEDS_ROM, true, 1, true, NULL },
    };
    for (size_t i = 0; i < sizeof k_rows / sizeof k_rows[0]; i++) {
        const char *got = test_verdict(k_rows[i].name, k_rows[i].mark, k_rows[i].asked, k_rows[i].skips,
                                       k_rows[i].no_rom);
        bool ok = k_rows[i].want ? got != NULL && !strcmp(got, k_rows[i].want) : got == NULL;
        if (!ok) {
            fprintf(stderr, "  verdict row %zu: got \"%s\"\n", i, got ? got : "(none)");
            g_test_failures++;
        }
    }
    CHECK(test_verdict("suite_x", TEST_ROM_FREE, false, 3, true) != NULL);   /* any number of skips */
}

void suite_smoke(void)
{
    CHECK_EQ(1 + 1, 2);
    CHECK_STR("ok", "ok");
    test_verdict_table();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test smoke"`
Expected: the build fails compiling `tests\test_smoke.c`, since `test.h` has no `test_mark`, `test_verdict` or `<stdbool.h>` yet. Among its errors:

```
tests\test_smoke.c(13): error C2061: syntax error: identifier 'test_mark'
tests\test_smoke.c(19): error C2065: 'TEST_ROM_FREE': undeclared identifier
```

- [ ] **Step 3: Implement**

**`tests/test.h`**: replace

```c
#define TEST_H
#include <stdio.h>
#include <string.h>

extern int g_test_failures;
extern int g_test_skips;
```

with

```c
#define TEST_H
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern int g_test_failures;
extern int g_test_skips;
extern bool g_test_rom_asked;   /* test_rom() (rom_fixture.c) was called by the running suite */

/* A suite's mark in suites.h (M5b spec 4.2): SUITE runs in full without the ROM, SUITE_ROM has parts that need it. */
typedef enum { TEST_ROM_FREE, TEST_NEEDS_ROM } test_mark;

/* The runner's verdict on one suite run (M5b spec 4.3; test_main.c): NULL when its mark holds, else the reason it
   fails. A SUITE that asked for the ROM, or a SUITE_ROM that never did, fails in every mode; with no_rom
   (APOLLO2_NO_ROM set) a SUITE that skipped fails too, except suite_audio (its skip is the waveOut device's). */
const char *test_verdict(const char *name, test_mark mark, bool asked_rom, int skips, bool no_rom);
```

**Replace the whole of `tests/test_main.c` with:**

```c
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"

int g_test_failures = 0;
int g_test_skips = 0;
bool g_test_rom_asked = false;

#define SUITE(fn) void fn(void);
#define SUITE_ROM(fn) void fn(void);
#include "suites.h"
#undef SUITE
#undef SUITE_ROM

static const struct { const char *name; void (*fn)(void); test_mark mark; } k_suites[] = {
#define SUITE(fn) { #fn, fn, TEST_ROM_FREE },
#define SUITE_ROM(fn) { #fn, fn, TEST_NEEDS_ROM },
#include "suites.h"
#undef SUITE
#undef SUITE_ROM
};

const char *test_verdict(const char *name, test_mark mark, bool asked_rom, int skips, bool no_rom)
{
    if (mark == TEST_ROM_FREE && asked_rom)
        return "marked ROM-free but asked for the ROM";
    if (mark == TEST_NEEDS_ROM && !asked_rom)
        return "marked as needing the ROM but never asked for it";
    if (no_rom && mark == TEST_ROM_FREE && skips > 0 && strcmp(name, "suite_audio") != 0)
        return "marked ROM-free but skipped with APOLLO2_NO_ROM set";
    return NULL;
}

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : NULL;
    const char *no_rom_env = getenv("APOLLO2_NO_ROM");
    bool no_rom = no_rom_env && *no_rom_env;
    int run = 0, rom_free_full = 0, rom_asked = 0;
    for (size_t i = 0; i < sizeof k_suites / sizeof k_suites[0]; i++) {
        if (filter && !strstr(k_suites[i].name, filter))
            continue;
        int before = g_test_failures, skips_before = g_test_skips;
        g_test_rom_asked = false;
        k_suites[i].fn();
        int skips = g_test_skips - skips_before;
        const char *wrong = test_verdict(k_suites[i].name, k_suites[i].mark, g_test_rom_asked, skips, no_rom);
        if (wrong) {
            fprintf(stderr, "  %s: %s\n", k_suites[i].name, wrong);
            g_test_failures++;
        }
        printf("%-28s %s\n", k_suites[i].name, g_test_failures == before ? "ok" : "FAILED");
        run++;
        rom_asked += g_test_rom_asked;
        rom_free_full += k_suites[i].mark == TEST_ROM_FREE && !g_test_rom_asked && skips == 0;
    }
    if (filter && run == 0) {
        fprintf(stderr, "no suite matches \"%s\"\n", filter);
        return 1;
    }
    printf("%d suite(s), %d failure(s), %d skip(s)\n", run, g_test_failures, g_test_skips);
    printf("ROM-free: %d suite(s) ran in full; ROM: %d suite(s) asked for the ROM\n", rom_free_full, rom_asked);
    return g_test_failures ? 1 : 0;
}
```

**`tests/rom_fixture.h`**: replace

```c
/* Returns the 64 KB Apollo ROM (APOLLO2_ROM or ./UK_V2.01_4.04_ROM.BIN), or NULL if missing or if it is not the UK V2.01 4.04 image (MD5). */
```

with

```c
/* Returns the 64 KB Apollo ROM (APOLLO2_ROM or ./UK_V2.01_4.04_ROM.BIN), or NULL if missing or if it is not the UK V2.01 4.04 image (MD5).
   The only place the tests look for the ROM: every call sets g_test_rom_asked (test.h); with APOLLO2_NO_ROM set to a
   non-empty value it returns NULL without looking (M5b spec 4.1). */
```

**`tests/rom_fixture.c`**: replace

```c
#include "rom_fixture.h"
#include "engine/rom_id.h"
```

with

```c
#include "rom_fixture.h"
#include "test.h"
#include "engine/rom_id.h"
```

then replace

```c
    static int state;   /* 0 = not tried, 1 = loaded, -1 = missing, wrong size or not the UK V2.01 4.04 image */
    if (state == 0) {
```

with

```c
    static int state;   /* 0 = not tried, 1 = loaded, -1 = missing, wrong size or not the UK V2.01 4.04 image */
    g_test_rom_asked = true;   /* M5b spec 4.1: the runner checks the suite's mark against this */
    const char *no_rom = getenv("APOLLO2_NO_ROM");
    if (no_rom && *no_rom)
        return NULL;           /* norom mode: as if no ROM existed, without looking */
    if (state == 0) {
```

**Replace the whole of `tests/suites.h` with:**

```c
/* One line per test suite; test_main.c expands this list twice. SUITE(fn): the suite runs in full without the ROM.
   SUITE_ROM(fn): it has parts that need the ROM (test_rom()), which skip without it. The runner fails a suite whose
   mark is wrong (M5b spec 4.2, 4.3). */
SUITE(suite_smoke)
SUITE(suite_optable)
SUITE(suite_cpu_base)
SUITE(suite_cpu_740)
SUITE(suite_disasm)
SUITE(suite_vec740)
SUITE(suite_m50734)
SUITE(suite_machine)
SUITE(suite_coverage)
SUITE_ROM(suite_rom_boot)
SUITE_ROM(suite_rom_speak)
SUITE_ROM(suite_golden)
SUITE_ROM(suite_raw_stream)
SUITE(suite_resonator)
SUITE(suite_source)
SUITE(suite_params)
SUITE(suite_biquad)
SUITE(suite_synth)
SUITE(suite_audio)
SUITE_ROM(suite_render)
SUITE(suite_cpu_harte)
SUITE_ROM(suite_rom_id)
SUITE_ROM(suite_machine_hooks)
SUITE(suite_fw)
SUITE_ROM(suite_firmware_data)
SUITE(suite_fw_lts)
SUITE_ROM(suite_diff_lts)
SUITE_ROM(suite_diff_play)
SUITE_ROM(suite_diff_serial)
SUITE_ROM(suite_diff_main)
SUITE_ROM(suite_golden_hybrid)
SUITE(suite_fw_tn)
SUITE(suite_fw_cmd)
SUITE(suite_fw_pros)
SUITE(suite_fw_par)
SUITE(suite_fw_frm)
SUITE(suite_fw_ser)
SUITE(suite_fw_dsp)
SUITE(suite_fw_bank)
SUITE(suite_fw_boot)
SUITE(suite_board)
SUITE_ROM(suite_native)
SUITE_ROM(suite_diff_native)
SUITE(suite_golden_native)
SUITE(suite_render_native)
```

**Replace the whole of `build.bat` with** (CRLF line endings):

```bat
@echo off
rem Builds the program build\apollo2.exe from src\ alone, the oracle tools build\apollo2_frames.exe,
rem build\apollo2_coverage.exe and build\cpu740_vectors.exe, and the tests build\apollo2_tests.exe (M5b spec 2.3).
rem Usage: build.bat [test [SUITE_FILTER] | test-norom [SUITE_FILTER]]
setlocal EnableDelayedExpansion
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /D_CRT_SECURE_NO_WARNINGS
set LIBS=winmm.lib
rem M4.0 spec 3.1: src\firmware builds without the emulator. Syntax-check it without /Isrc, so an include of
rem a machine\ or cpu740\ header cannot be found.
set FWSRC=
for %%d in (src\firmware src\firmware\data) do (
    if exist %%d\*.c set FWSRC=!FWSRC! %%d\*.c
)
if defined FWSRC (
    cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS !FWSRC! || exit /b 1
)
rem M5b spec 2.3, step 1: the program, compiled and linked from src\ alone with /Isrc as its only include root and its
rem objects in build\product: an include of an oracle header (tests\oracle) cannot be found, and a call into oracle
rem code cannot link.
set PRODSRC=
for %%d in (src\board src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
    if exist %%d\*.c set PRODSRC=!PRODSRC! %%d\*.c
)
if not exist build\product mkdir build\product
cl %CFLAGS% /Isrc /Fobuild\product\ /Fdbuild\product\ /Febuild\apollo2.exe src\apps\apollo2.c !PRODSRC! %LIBS% || exit /b 1
rem Step 2: the oracle tools, from tests\oracle\apps, under the names ghidra740\coverage.bat, ghidra740\validate.bat and
rem tools\calibrate.py run them by.
set ORACLESRC=
for %%d in (tests\oracle\cpu740 tests\oracle\machine tests\oracle\engine tests\oracle\render) do (
    if exist %%d\*.c set ORACLESRC=!ORACLESRC! %%d\*.c
)
for %%a in (apollo2_frames apollo2_coverage cpu740_vectors) do (
    cl %CFLAGS% /Isrc /Itests\oracle /Fobuild\ /Fdbuild\ /Febuild\%%a.exe tests\oracle\apps\%%a.c !ORACLESRC! !PRODSRC! ^
        %LIBS% || exit /b 1
)
rem Step 3: the tests, with the oracle and the program's sources.
cl %CFLAGS% /Isrc /Itests\oracle /Fobuild\ /Fdbuild\ /Febuild\apollo2_tests.exe tests\*.c !ORACLESRC! !PRODSRC! %LIBS% ^
    || exit /b 1
if /i "%~1"=="test-norom" goto norom
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%

:norom
rem M5b spec 4.4: the suite as if no ROM existed (APOLLO2_NO_ROM: test_rom() returns NULL without looking), then the
rem program itself with APOLLO2_ROM unset (tests\check_cli.bat).
set APOLLO2_NO_ROM=1
build\apollo2_tests.exe %2 || exit /b 1
set APOLLO2_NO_ROM=
set APOLLO2_ROM=
call tests\check_cli.bat build\apollo2.exe || exit /b 1
exit /b 0
```

- [ ] **Step 4: Run the tests, a deliberate mis-mark and the filtered norom runs**

Run: `cmd //c ".\build.bat test smoke"`
Expected: the build passes with no warnings, and

```
suite_smoke                  ok
1 suite(s), 0 failure(s), 0 skip(s)
ROM-free: 1 suite(s) ran in full; ROM: 0 suite(s) asked for the ROM
```

Then the deliberate check (not committed): `suite_native` marked ROM-free and `suite_board` marked as needing the ROM fail, with the ROM and in norom mode.

```bash
sed -i 's/^SUITE_ROM(suite_native)$/SUITE(suite_native)/; s/^SUITE(suite_board)$/SUITE_ROM(suite_board)/' tests/suites.h
cmd //c ".\build.bat"
build/apollo2_tests.exe suite_native 2>&1 | tail -4; echo "exit ${PIPESTATUS[0]}"
build/apollo2_tests.exe board; echo "exit $?"
APOLLO2_NO_ROM=1 build/apollo2_tests.exe board; echo "exit $?"
sed -i 's/^SUITE(suite_native)$/SUITE_ROM(suite_native)/; s/^SUITE_ROM(suite_board)$/SUITE(suite_board)/' tests/suites.h
cmd //c ".\build.bat"
```

Expected:

```
  suite_native: marked ROM-free but asked for the ROM
suite_native                 FAILED
1 suite(s), 1 failure(s), 0 skip(s)
ROM-free: 0 suite(s) ran in full; ROM: 1 suite(s) asked for the ROM
exit 1
  suite_board: marked as needing the ROM but never asked for it
suite_board                  FAILED
1 suite(s), 1 failure(s), 0 skip(s)
ROM-free: 0 suite(s) ran in full; ROM: 0 suite(s) asked for the ROM
exit 1
  suite_board: marked as needing the ROM but never asked for it
suite_board                  FAILED
1 suite(s), 1 failure(s), 0 skip(s)
ROM-free: 0 suite(s) ran in full; ROM: 0 suite(s) asked for the ROM
exit 1
```

and the last build passes; `tests/suites.h` is back as Step 3 wrote it. (A build here may print the linker's note `LINK : build\apollo2_tests.exe not found or not built by the last incremental link; performing full link`, which is not a warning.)

Then the filtered norom runs:

Run: `cmd //c ".\build.bat test-norom golden"`, then `cmd //c ".\build.bat test-norom diff_main"`
Expected: each exits 0, with

```
  SKIP: ROM not found (set APOLLO2_ROM)
  SKIP: ROM not found (set APOLLO2_ROM)
suite_golden                 ok
suite_golden_hybrid          ok
  48 goldens, 23281 frames reproduced on the native engine
suite_golden_native          ok
3 suite(s), 0 failure(s), 2 skip(s)
ROM-free: 1 suite(s) ran in full; ROM: 2 suite(s) asked for the ROM
check_cli: ok
```

and

```
  SKIP: ROM not found (set APOLLO2_ROM)
suite_diff_main              ok
1 suite(s), 0 failure(s), 1 skip(s)
ROM-free: 0 suite(s) ran in full; ROM: 1 suite(s) asked for the ROM
check_cli: ok
```

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `45 suite(s), 0 failure(s), 0 skip(s)`, then `ROM-free: 30 suite(s) ran in full; ROM: 15 suite(s) asked for the ROM`; no warnings, the firmware's syntax check passing (956 s in the proof run);
- every other line of the suites' output as in Task 1 Step 5; every golden unchanged (`git status --short tests/golden` prints nothing).

- [ ] **Step 6: Run the full norom mode**

Run: `cmd //c ".\build.bat test-norom"` (in the background; with the ROM file still named by `APOLLO2_ROM`)
Expected: exit code 0, and

- `45 suite(s), 0 failure(s), 18 skip(s)`, then `ROM-free: 30 suite(s) ran in full; ROM: 15 suite(s) asked for the ROM`, then `check_cli: ok` (20 s in the proof run, the build included);
- the 30 `SUITE` suites run in full, each `ok` (among their lines `corpus peak plain 20522 (-4.1 dBFS)`, `44700 vectors over 149 opcodes` and `48 goldens, 23281 frames reproduced on the native engine`);
- 14 of the 15 `SUITE_ROM` suites skip what needs the ROM (`SKIP: ROM not found (set APOLLO2_ROM)`, `suite_firmware_data`'s with `: converted tables not compared`), 18 skips in all, and each prints `ok`: `suite_rom_boot`, `suite_rom_speak` (3), `suite_golden`, `suite_raw_stream`, `suite_render` (after its native part), `suite_rom_id`, `suite_machine_hooks` (3), `suite_firmware_data`, `suite_diff_lts`, `suite_diff_play`, `suite_diff_serial`, `suite_diff_main`, `suite_golden_hybrid` and `suite_diff_native`; `suite_native` asks for the ROM, runs its native part and does not skip.

- [ ] **Step 7: Commit**

```bash
git add tests/test.h tests/test_main.c tests/rom_fixture.c tests/rom_fixture.h tests/suites.h tests/test_smoke.c build.bat
git commit -m "The no-ROM proof: test_rom records each call and honours APOLLO2_NO_ROM, every suite marked SUITE or SUITE_ROM, the runner's verdict on the marks (unit-tested in suite_smoke), build.bat test-norom with tests/check_cli.bat (M5b 4.1-4.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

- [ ] **Step 8: The one-time run with no ROM file anywhere**

Make a fresh worktree of this commit next to the current one, make sure nothing there can supply the ROM, and run the full suite (`test`, not `test-norom`) in it (in the background):

```bash
ONE="$(cd .. && pwd)/m5b-onetime"
git worktree add --detach "$ONE" HEAD
cd "$ONE"
unset APOLLO2_ROM APOLLO2_NO_ROM
env | grep -c "^APOLLO2_"
find . -iname '*.bin' | wc -l
cmd //c ".\build.bat test"
```

Expected: `0`, `0`, then the build passes with no warnings, and

- `45 suite(s), 0 failure(s), 18 skip(s)`, then `ROM-free: 30 suite(s) ran in full; ROM: 15 suite(s) asked for the ROM`: the same suites, skips and lines as `test-norom` in Step 6 (without `check_cli`), now because no ROM file exists rather than because `APOLLO2_NO_ROM` hides it;
- `suite_render_native`, `suite_golden_native` (`48 goldens, 23281 frames reproduced on the native engine`) and the other 28 `SUITE` suites `ok` with no skip; `suite_native` `ok` with its ROM comparison not run.

Then, from this task's worktree again (where Steps 1–7 ran), remove that worktree:

```bash
ONE="$(cd .. && pwd)/m5b-onetime"
git worktree remove --force "$ONE"
git worktree list | grep -c m5b-onetime
export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"
```

Expected: `0`.

---

### Task 5: Parent spec rev 20, M5b status and README (M5b spec §5.2)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-14-m5b-native-cli-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 20**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 20)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 20)* Revision 20 records milestone 5b and with it the end of milestone 5: apollo2 runs the native engine only and needs no ROM, and the emulator is the test oracle under tests/oracle (design: docs/superpowers/specs/2026-10-14-m5b-native-cli-design.md). `

2. **R1** (the line that begins `- **R1 — CLI:**`). Append at its end:

   ` *(rev 20)* Since M5b apollo2 runs the native engine only and reads no ROM: --engine, --rom and --rom-any are gone (an old option gets the usage text and exit code 1). Exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged.`

3. **§3 Build** (the paragraph that begins `**Build:** CMake is not installed` and ends `which print SKIP without it *(rev 3)*.`). Append at its end:

   ` *(rev 20)* Since M5b, src/ is the product: apollo2.exe is compiled and linked from src/ alone (/Isrc, its objects in build\product), so it cannot reach the emulator. cpu740/, machine/, the rom and hybrid engines, the thunks, rom_id, render_text and the tools apollo2_frames, apollo2_coverage and cpu740_vectors live under tests/oracle/ in the same subfolders (built with /Itests\oracle) and serve only the tests and the analysis scripts; the unit table above describes them where they now live. Each suite is marked ROM-free (SUITE) or with ROM parts (SUITE_ROM) in tests/suites.h and the runner enforces the marks; build.bat test-norom runs the suite as if no ROM existed and then runs the product.`

4. **§8 milestone 5.** After its last bullet (the one that begins `   - *(rev 19)* M5 is two plans:`), add one more bullet at the same indentation (three spaces, then `- `), as one line:

   `- *(rev 20)* **M5b done** <DATE>: the emulator (cpu740, machine, the rom and hybrid engines, thunks, rom_id, render_text and the three analysis tools) moved under tests/oracle; apollo2 builds from src/ alone and runs the native engine only, with no --engine, --rom or --rom-any, reporting a firmware fault and unmodelled accesses from the board; its WAVs are byte-identical to the M5a CLI's; every suite is marked ROM-free or with ROM parts and the runner enforces the marks; build.bat test-norom passes (<NOROM_SUMMARY>), and the full suite passes in a worktree with no ROM file anywhere (<ONETIME_SUMMARY>). **Milestone 5 complete:** with no ROM file anywhere, build.bat test passes and the native engine reproduces every committed golden frame stream.`

   Replace `<DATE>` with the date of your commit (`date +%F`). Replace `<NOROM_SUMMARY>` and `<ONETIME_SUMMARY>` with the summary lines Task 4 observed: the suite counts, failures, skips and the `ROM-free:` line.

- [ ] **Step 2: M5b spec status and As built**

In `docs/superpowers/specs/2026-10-14-m5b-native-cli-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-14-m5b-native-cli.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the code is more precise than, or differs from, the sections above.` Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "Facts checked" lists at the top of Tasks 1–4 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the files moved, and `build.bat`'s steps as built: the product build's flags and object folder, the oracle tools, the tests build, `test-norom`;
  - `render_text_native`'s and the oracle `render_text`'s signatures as built;
  - the lines `board_print_fault` and `board_print_unmodelled` print, per fault kind;
  - the CLI as built: its options, its messages and exit codes;
  - `test_rom()`'s recording and `APOLLO2_NO_ROM`, the marks in `tests/suites.h`, the runner's verdict function and its messages, the `suite_audio` exception;
  - `tests/check_cli.bat`'s cases;
  - `suite_render_native`'s sequence and what it checks;
  - the full suite's summary with the ROM, the `test-norom` summary, and the one-time run's summary (suites, failures, skips, the `ROM-free:` line);
  - the reference WAV comparison of Tasks 2 and 3 (the cases and the result);
  - the Review Focus tests and what each showed.

Every bullet must be true of the committed code. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-13-m5a-native-host-design.md`. The one-time run's summary bullet is required even if nothing else differs.

- [ ] **Step 3: README**

Make these replacements in `README.md`.

1. Replace:

```
The original ROM is not part of this repository. Put `UK_V2.01_4.04_ROM.BIN` in the
repo root, or point `APOLLO2_ROM` at it. Tests that need it print SKIP without it.
```

with:

```
`apollo2` needs no ROM: it is built from `src/` alone and runs the firmware as native C. The original ROM
is not part of this repository; only the test oracle uses it (see "Tests and the oracle" below). Put
`UK_V2.01_4.04_ROM.BIN` in the repo root, or point `APOLLO2_ROM` at it. Tests that need it print SKIP without it.

    cmd //c ".\build.bat test-norom"      # every suite as if no ROM existed, then the program itself
```

2. Replace:

```
## Frames from the original firmware

    build/apollo2_frames.exe "Hello world."      # one produced DSP frame per line (hex)
```

with:

```
## Frames from the original firmware

`apollo2_frames` is a test-side tool (tests/oracle/apps): it runs the ROM on the emulator and needs the ROM.

    build/apollo2_frames.exe "Hello world."      # one produced DSP frame per line (hex)
```

3. Replace:

```
Text goes through the original firmware's serial input (emulated), so the `@` commands in
```

with:

```
Exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged.

Text goes through the firmware's serial input (the C firmware on the native engine), so the `@` commands in
```

4. Replace:

```
`apollo2 --engine hybrid "text" -o out.wav` runs the original firmware with the ported routines replaced by C
```

with:

```
The hybrid engine (tests/oracle/engine, a test oracle) runs the original firmware with the ported routines replaced by C
```

5. Replace:

```
A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the CLIs unless `--rom-any` is given.
```

with:

```
A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the oracle tools (`apollo2_frames`,
`apollo2_coverage`, `cpu740_vectors`) unless `--rom-any` is given.
```

6. Replace:

```
comparing RAM, the peripheral state and the cycle count at every input idle and frame event. Until milestone 5b
the CLI still runs the emulated engines; 5b makes it native-only, with no ROM needed.
```

with:

```
comparing RAM, the peripheral state and the cycle count at every input idle and frame event. Since milestone 5b
the CLI runs the native engine only and needs no ROM.

## Tests and the oracle

The emulator is the test oracle: `tests/oracle/` holds the 740 CPU core, the machine model, the `rom` and
`hybrid` engines and the three tools that need the ROM (`apollo2_frames`, `apollo2_coverage`,
`cpu740_vectors`, built into `build/` under those names). `apollo2.exe` is compiled and linked from `src/`
alone, so it cannot reach them. In `tests/suites.h` each suite is marked `SUITE` (needs no ROM) or
`SUITE_ROM` (has parts that need the ROM, set by `APOLLO2_ROM` or found in the repo root), and the runner
fails a suite whose mark is wrong. `build.bat test-norom` runs every suite with `APOLLO2_NO_ROM=1`, so the
ROM is never looked for: every `SUITE` must run in full, the `SUITE_ROM` suites skip their ROM parts, and
then `tests/check_cli.bat` runs `apollo2.exe` itself.
```

If an old text does not match, the README changed earlier in this plan: fix the old text in this plan from the file and apply it, and say so in the report.

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 16 minutes)
Expected: the summary Task 4 Step 5 observed with the ROM (`45 suite(s), 0 failure(s), 0 skip(s)` and its `ROM-free:` line).

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-14-m5b-native-cli-design.md README.md
git commit -m "Spec rev 20: milestone 5b done and milestone 5 complete (apollo2 native-only, the emulator the test oracle)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
