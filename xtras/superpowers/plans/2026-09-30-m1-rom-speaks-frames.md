# Milestone 1 — ROM Speaks Frames: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the original Apollo 2 firmware on an emulated M50734 board. Feed it text over its serial input and capture the DSP frames it produces as golden reference streams.

**Architecture:** Three layers, each with its own tests:
- `cpu740/`: an instruction-level 740 CPU core, plus an opcode table and a disassembler.
- `machine/`: the M50734 on-chip peripherals plus the Apollo 2 board (memory map, ROM banking, DSP port, produced-frame capture, serial feed and idle detection).
- `engine/`: drives the machine (boot, feed text, run to idle).

A dev CLI and a golden-corpus test sit on top.

**Tech Stack:** C11, MSVC 2022 Build Tools (`cl`), a plain `build.bat`, a self-made test runner. No third-party libraries.

**Spec:** `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (revision 2). Read §1 (facts), §4 (machine model) and §7.1 (firmware testing) before starting.

## Global Constraints

- **Language and build:**
  - C11, built only through `build.bat`.
  - Flags: `/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /Isrc /D_CRT_SECURE_NO_WARNINGS`.
  - Code must compile warning-free at `/W4`. In particular: no non-constant aggregate initialisers (C4204); no `(uint8_t)~CONST` casts (C4310), so write `(uint8_t)(0xFF ^ CONST)`; no signed/unsigned loop comparisons (C4018).
- **Running from Git Bash:** `cmd //c "build.bat test"` builds and runs all suites. `cmd //c "build.bat test <filter>"` runs the suites whose name contains `<filter>`.
- **ROM handling in M1 (spec R5, rev 3):**
  - The raw ROM image is never committed. It is read at runtime from `APOLLO2_ROM`, or by default from `./UK_V2.01_4.04_ROM.BIN` in the repo root (gitignored).
  - Tests that need the ROM print `SKIP` when it is missing.
  - Golden frame files are committed.
  - M1 converts no ROM tables. That conversion to readable C happens in M4, so that after M5 nothing needs the ROM.
- **Never silently accept unmodelled behaviour (spec §4):**
  - Unmodelled register or ROM-space accesses are logged.
  - Unsupported opcodes (SET, SED, STP, MUL, DIV, BRK), a T flag restored by PLP/RTI, and enabling DME are faults that stop the run.
- **Firmware landmarks (spec §1.3):**
  - `APOLLO_SWAP_PC = 0xB306`: produced-frame capture point.
  - `APOLLO_IDLE_WIT_PC = 0xC602`: the only WIT.
  - `APOLLO_PRODUCER_PTR = 0x2C`: zero-page pointer to the buffer the main loop fills.
  - Frames are 20 bytes.
- **Board defaults (spec §4):** port inputs `P0=$00 P1=$00 P2=$70 P3=$00 P4=$00`.
- **Timing (spec §1.1):** one CPU cycle = 4 crystal clocks. Timer n period = `4*(pre+1)*(reload+1)` CPU cycles.
- **Commits:** end every commit message with the `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` trailer.

## File Map

| File | Responsibility |
|---|---|
| `build.bat` | Builds `build\apollo2_tests.exe` (and `build\apollo2_frames.exe` once it exists); `build.bat test [filter]` also runs the tests |
| `.gitattributes` | Keeps golden `.frm` files byte-exact (no CRLF conversion) |
| `README.md` | Build and run instructions |
| `src/cpu740/optable.h/.c` | 256-entry opcode table: mnemonic + addressing mode, mode lengths, names |
| `src/cpu740/cpu740.h` | CPU state, bus interface, step/reset/IRQ API, disassembler API |
| `src/cpu740/cpu740.c` | Instruction execution, interrupts, faults |
| `src/cpu740/disasm740.c` | Disassembler (used in fault dumps) |
| `src/machine/m50734.h/.c` | On-chip peripherals: ports, timers, UART, interrupt control |
| `src/machine/machine.h/.c` | Apollo 2 board: memory map, banking, DSP port, frame capture, serial feed, idle detection, dumps |
| `src/engine/rom_engine.h/.c` | Boot → feed text → run to idle; collects produced frames; hex formatting |
| `src/apps/apollo2_frames.c` | Dev CLI: text → produced frames as hex |
| `tests/test.h`, `tests/test_main.c`, `tests/suites.h` | Test framework and suite registry |
| `tests/cpu_harness.h` | Flat-memory CPU harness for CPU tests |
| `tests/rom_fixture.h/.c` | Loads the real ROM for ROM-dependent tests |
| `tests/test_*.c` | One suite per unit |
| `tests/corpus/*.txt`, `tests/golden/*.frm` | Golden corpus inputs and expected produced-frame streams |

---

### Task 1: Project scaffold, build script and test runner

**Files:**
- Create: `build.bat`, `.gitattributes`, `README.md`, `tests/test.h`, `tests/test_main.c`, `tests/suites.h`, `tests/test_smoke.c`

**Interfaces:**
- Produces:
  - `CHECK(cond)`, `CHECK_EQ(actual, expected)` (compared as `long long`), `CHECK_STR(actual, expected)` and `SKIP(reason)` (returns from the current function).
  - The globals `g_test_failures` and `g_test_skips`.
  - Suites register as `SUITE(suite_name)` lines in `tests/suites.h`. Each is a `void suite_name(void)` function.

- [ ] **Step 1: Write the smoke suite (the failing test)**

`tests/test_smoke.c`:
```c
#include "test.h"

void suite_smoke(void)
{
    CHECK_EQ(1 + 1, 2);
    CHECK_STR("ok", "ok");
}
```

`tests/suites.h`:
```c
/* One SUITE(name) line per test suite; test_main.c expands this list twice. */
SUITE(suite_smoke)
```

- [ ] **Step 2: Run the build and confirm it fails**

Run: `cmd //c "build.bat test"`
Expected: FAIL. `build.bat` does not exist yet (`'build.bat' is not recognized`).

- [ ] **Step 3: Write the framework, runner and build script**

`tests/test.h`:
```c
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>

extern int g_test_failures;
extern int g_test_skips;

#define CHECK(cond) \
    do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
                        g_test_failures++; } } while (0)

#define CHECK_EQ(actual, expected) \
    do { long long a_ = (long long)(actual), e_ = (long long)(expected); \
         if (a_ != e_) { fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (got 0x%llX, want 0x%llX)\n", \
                                 __FILE__, __LINE__, #actual, #expected, a_, e_); g_test_failures++; } } while (0)

#define CHECK_STR(actual, expected) \
    do { const char *a_ = (actual), *e_ = (expected); \
         if (strcmp(a_, e_) != 0) { fprintf(stderr, "%s:%d: CHECK_STR failed: got \"%s\", want \"%s\"\n", \
                                            __FILE__, __LINE__, a_, e_); g_test_failures++; } } while (0)

#define SKIP(reason) \
    do { fprintf(stderr, "  SKIP: %s\n", reason); g_test_skips++; return; } while (0)

#endif
```

`tests/test_main.c`:
```c
#include <stdio.h>
#include <string.h>
#include "test.h"

int g_test_failures = 0;
int g_test_skips = 0;

#define SUITE(fn) void fn(void);
#include "suites.h"
#undef SUITE

static const struct { const char *name; void (*fn)(void); } k_suites[] = {
#define SUITE(fn) { #fn, fn },
#include "suites.h"
#undef SUITE
};

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : NULL;
    int run = 0;
    for (size_t i = 0; i < sizeof k_suites / sizeof k_suites[0]; i++) {
        if (filter && !strstr(k_suites[i].name, filter))
            continue;
        int before = g_test_failures;
        k_suites[i].fn();
        printf("%-28s %s\n", k_suites[i].name, g_test_failures == before ? "ok" : "FAILED");
        run++;
    }
    printf("%d suite(s), %d failure(s), %d skip(s)\n", run, g_test_failures, g_test_skips);
    return g_test_failures ? 1 : 0;
}
```

`build.bat`:
```bat
@echo off
rem Builds build\apollo2_tests.exe (and build\apollo2_frames.exe once it exists).
rem Usage: build.bat [test [SUITE_FILTER]]
setlocal EnableDelayedExpansion
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /Isrc /D_CRT_SECURE_NO_WARNINGS
set LIBSRC=
for %%d in (src\cpu740 src\machine src\engine) do (
    if exist %%d\*.c set LIBSRC=!LIBSRC! %%d\*.c
)
cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\apollo2_tests.exe tests\*.c !LIBSRC! || exit /b 1
if exist src\apps\apollo2_frames.c (
    cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\apollo2_frames.exe src\apps\apollo2_frames.c !LIBSRC! || exit /b 1
)
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%
```

`.gitattributes`:
```
*.frm -text
*.bat text eol=crlf
```

`README.md`:
```markdown
# Software Apollo 2

A software re-creation of the Dolphin Apollo 2 speech synthesizer. Design:
`docs/superpowers/specs/2026-09-30-software-apollo2-design.md`.

## Build and test

Requires MSVC 2022 Build Tools. From Git Bash in the repo root:

    cmd //c "build.bat test"            # build + run every test suite
    cmd //c "build.bat test cpu"        # only suites whose name contains "cpu"

The original ROM is not part of this repository. Put `UK_V2.01_4.04_ROM.BIN` in the
repo root, or point `APOLLO2_ROM` at it. Tests that need it print SKIP without it.
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test"`
Expected: PASS. The last lines are:
```
suite_smoke                  ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 5: Commit**

```bash
git add build.bat .gitattributes README.md tests/test.h tests/test_main.c tests/suites.h tests/test_smoke.c
git commit -m "Add build script and minimal C test runner

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: 740 opcode table

**Files:**
- Create: `src/cpu740/optable.h`, `src/cpu740/optable.c`, `tests/test_optable.c`
- Modify: `tests/suites.h` (append `SUITE(suite_optable)`)

**Interfaces:**
- Produces (`src/cpu740/optable.h`):
  - the enums `cpu740_mode` (`AM_*`) and `cpu740_mnem` (`M_*`)
  - `typedef struct { uint8_t mnem; uint8_t mode; } cpu740_opinfo;`
  - `const cpu740_opinfo *cpu740_optable(void);`: 256 entries; illegal opcodes have `mnem == M_ILL`
  - `int cpu740_mode_length(int mode);`: total bytes including the opcode
  - `const char *cpu740_mnem_name(int mnem);`

- [ ] **Step 1: Write the failing test**

`tests/test_optable.c`:
```c
#include "test.h"
#include "cpu740/optable.h"

void suite_optable(void)
{
    const cpu740_opinfo *t = cpu740_optable();
    int legal = 0;
    for (int op = 0; op < 256; op++)
        if (t[op].mnem != M_ILL)
            legal++;
    CHECK_EQ(legal, 231);   /* 151 documented 6502 + 64 bit ops + 16 other 740 opcodes */

    CHECK_STR(cpu740_mnem_name(t[0xA9].mnem), "LDA"); CHECK_EQ(t[0xA9].mode, AM_IMM);
    CHECK_STR(cpu740_mnem_name(t[0x91].mnem), "STA"); CHECK_EQ(t[0x91].mode, AM_IZY);
    CHECK_EQ(t[0x89].mnem, M_ILL);                     /* STA # does not exist */
    CHECK_STR(cpu740_mnem_name(t[0x3C].mnem), "LDM"); CHECK_EQ(cpu740_mode_length(t[0x3C].mode), 3);
    CHECK_STR(cpu740_mnem_name(t[0xEF].mnem), "SEB"); CHECK_EQ(t[0xEF].mode, AM_BIT_ZP);
    CHECK_STR(cpu740_mnem_name(t[0x37].mnem), "BBC"); CHECK_EQ(t[0x37].mode, AM_BIT_ZP_REL);
    CHECK_STR(cpu740_mnem_name(t[0xE3].mnem), "BBS"); CHECK_EQ(t[0xE3].mode, AM_BIT_A_REL);
    CHECK_STR(cpu740_mnem_name(t[0x7B].mnem), "CLB"); CHECK_EQ(t[0x7B].mode, AM_BIT_A);
    CHECK_STR(cpu740_mnem_name(t[0x22].mnem), "JSR"); CHECK_EQ(t[0x22].mode, AM_SPG);
    CHECK_STR(cpu740_mnem_name(t[0x02].mnem), "JSR"); CHECK_EQ(t[0x02].mode, AM_ZPI);
    CHECK_STR(cpu740_mnem_name(t[0xB2].mnem), "JMP"); CHECK_EQ(t[0xB2].mode, AM_ZPI);
    CHECK_STR(cpu740_mnem_name(t[0x1A].mnem), "DEC"); CHECK_EQ(t[0x1A].mode, AM_ACC);
    CHECK_STR(cpu740_mnem_name(t[0x3A].mnem), "INC"); CHECK_EQ(t[0x3A].mode, AM_ACC);
    CHECK_STR(cpu740_mnem_name(t[0x80].mnem), "BRA");
    CHECK_STR(cpu740_mnem_name(t[0xC2].mnem), "WIT");
    CHECK_STR(cpu740_mnem_name(t[0x12].mnem), "CLT");
    CHECK_STR(cpu740_mnem_name(M_DIV), "DIV");
    CHECK_EQ(cpu740_mode_length(AM_IMP), 1);
    CHECK_EQ(cpu740_mode_length(AM_IZX), 2);
    CHECK_EQ(cpu740_mode_length(AM_IND), 3);
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_optable)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test optable"`
Expected: FAIL to compile: `cannot open include file 'cpu740/optable.h'`.

- [ ] **Step 3: Implement the table**

`src/cpu740/optable.h`:
```c
#ifndef CPU740_OPTABLE_H
#define CPU740_OPTABLE_H
#include <stdint.h>

typedef enum {
    AM_IMP, AM_ACC, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY, AM_ABS, AM_ABX, AM_ABY,
    AM_IND, AM_IZX, AM_IZY, AM_REL,
    AM_ZPI,         /* JMP/JSR ($zz) */
    AM_SPG,         /* JSR \$FFzz (special page) */
    AM_LDM,         /* LDM #imm,$zz */
    AM_BIT_A,       /* SEB/CLB n,A */
    AM_BIT_ZP,      /* SEB/CLB n,$zz */
    AM_BIT_A_REL,   /* BBS/BBC n,A,rel */
    AM_BIT_ZP_REL,  /* BBS/BBC n,$zz,rel */
    AM_COUNT
} cpu740_mode;

typedef enum {
    M_ILL,
    M_ADC, M_AND, M_ASL, M_BCC, M_BCS, M_BEQ, M_BIT, M_BMI, M_BNE, M_BPL, M_BRA, M_BRK,
    M_BVC, M_BVS, M_CLC, M_CLD, M_CLI, M_CLV, M_CMP, M_CPX, M_CPY, M_DEC, M_DEX, M_DEY,
    M_EOR, M_INC, M_INX, M_INY, M_JMP, M_JSR, M_LDA, M_LDX, M_LDY, M_LSR, M_NOP, M_ORA,
    M_PHA, M_PHP, M_PLA, M_PLP, M_ROL, M_ROR, M_RTI, M_RTS, M_SBC, M_SEC, M_SED, M_SEI,
    M_STA, M_STX, M_STY, M_TAX, M_TAY, M_TSX, M_TXA, M_TXS, M_TYA,
    /* 740 extensions */
    M_BBC, M_BBS, M_CLB, M_SEB, M_CLT, M_SET, M_COM, M_LDM, M_RRF, M_STP, M_TST, M_WIT,
    M_MUL, M_DIV,
    M_COUNT
} cpu740_mnem;

typedef struct { uint8_t mnem; uint8_t mode; } cpu740_opinfo;

const cpu740_opinfo *cpu740_optable(void);
int cpu740_mode_length(int mode);
const char *cpu740_mnem_name(int mnem);

#endif
```

`src/cpu740/optable.c`:
```c
#include <stdbool.h>
#include "cpu740/optable.h"

static cpu740_opinfo g_table[256];
static bool g_built;

static const char *const k_names[M_COUNT] = {
    "???",
    "ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL", "BRA", "BRK",
    "BVC", "BVS", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY", "DEC", "DEX", "DEY",
    "EOR", "INC", "INX", "INY", "JMP", "JSR", "LDA", "LDX", "LDY", "LSR", "NOP", "ORA",
    "PHA", "PHP", "PLA", "PLP", "ROL", "ROR", "RTI", "RTS", "SBC", "SEC", "SED", "SEI",
    "STA", "STX", "STY", "TAX", "TAY", "TSX", "TXA", "TXS", "TYA",
    "BBC", "BBS", "CLB", "SEB", "CLT", "SET", "COM", "LDM", "RRF", "STP", "TST", "WIT",
    "MUL", "DIV",
};

static void set(int op, cpu740_mnem mnem, cpu740_mode mode)
{
    g_table[op].mnem = (uint8_t)mnem;
    g_table[op].mode = (uint8_t)mode;
}

static void build(void)
{
    static const cpu740_mnem alu[8] = { M_ORA, M_AND, M_EOR, M_ADC, M_STA, M_LDA, M_CMP, M_SBC };
    static const struct { int off; cpu740_mode mode; } alu_modes[8] = {
        { 0x01, AM_IZX }, { 0x05, AM_ZP },  { 0x09, AM_IMM }, { 0x0D, AM_ABS },
        { 0x11, AM_IZY }, { 0x15, AM_ZPX }, { 0x19, AM_ABY }, { 0x1D, AM_ABX },
    };
    static const cpu740_mnem shifts[4] = { M_ASL, M_ROL, M_LSR, M_ROR };
    static const struct { int op; cpu740_mnem mnem; } branches[] = {
        { 0x10, M_BPL }, { 0x30, M_BMI }, { 0x50, M_BVC }, { 0x70, M_BVS }, { 0x80, M_BRA },
        { 0x90, M_BCC }, { 0xB0, M_BCS }, { 0xD0, M_BNE }, { 0xF0, M_BEQ },
    };
    static const struct { int op; cpu740_mnem mnem; } implied[] = {
        { 0x00, M_BRK }, { 0x08, M_PHP }, { 0x18, M_CLC }, { 0x28, M_PLP }, { 0x38, M_SEC },
        { 0x40, M_RTI }, { 0x48, M_PHA }, { 0x58, M_CLI }, { 0x60, M_RTS }, { 0x68, M_PLA },
        { 0x78, M_SEI }, { 0x88, M_DEY }, { 0x8A, M_TXA }, { 0x98, M_TYA }, { 0x9A, M_TXS },
        { 0xA8, M_TAY }, { 0xAA, M_TAX }, { 0xB8, M_CLV }, { 0xBA, M_TSX }, { 0xC8, M_INY },
        { 0xCA, M_DEX }, { 0xD8, M_CLD }, { 0xE8, M_INX }, { 0xEA, M_NOP }, { 0xF8, M_SED },
        { 0x12, M_CLT }, { 0x32, M_SET }, { 0x42, M_STP }, { 0xC2, M_WIT },
    };
    int i, j;

    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++)
            if (!(alu[i] == M_STA && alu_modes[j].mode == AM_IMM))
                set(i * 0x20 + alu_modes[j].off, alu[i], alu_modes[j].mode);
    for (i = 0; i < 4; i++) {
        int b = i * 0x20;
        set(b + 0x06, shifts[i], AM_ZP);
        set(b + 0x0A, shifts[i], AM_ACC);
        set(b + 0x0E, shifts[i], AM_ABS);
        set(b + 0x16, shifts[i], AM_ZPX);
        set(b + 0x1E, shifts[i], AM_ABX);
    }
    set(0xE6, M_INC, AM_ZP); set(0xEE, M_INC, AM_ABS); set(0xF6, M_INC, AM_ZPX); set(0xFE, M_INC, AM_ABX);
    set(0xC6, M_DEC, AM_ZP); set(0xCE, M_DEC, AM_ABS); set(0xD6, M_DEC, AM_ZPX); set(0xDE, M_DEC, AM_ABX);
    set(0xA2, M_LDX, AM_IMM); set(0xA6, M_LDX, AM_ZP); set(0xAE, M_LDX, AM_ABS);
    set(0xB6, M_LDX, AM_ZPY); set(0xBE, M_LDX, AM_ABY);
    set(0xA0, M_LDY, AM_IMM); set(0xA4, M_LDY, AM_ZP); set(0xAC, M_LDY, AM_ABS);
    set(0xB4, M_LDY, AM_ZPX); set(0xBC, M_LDY, AM_ABX);
    set(0x86, M_STX, AM_ZP); set(0x8E, M_STX, AM_ABS); set(0x96, M_STX, AM_ZPY);
    set(0x84, M_STY, AM_ZP); set(0x8C, M_STY, AM_ABS); set(0x94, M_STY, AM_ZPX);
    set(0xE0, M_CPX, AM_IMM); set(0xE4, M_CPX, AM_ZP); set(0xEC, M_CPX, AM_ABS);
    set(0xC0, M_CPY, AM_IMM); set(0xC4, M_CPY, AM_ZP); set(0xCC, M_CPY, AM_ABS);
    set(0x24, M_BIT, AM_ZP); set(0x2C, M_BIT, AM_ABS);
    set(0x4C, M_JMP, AM_ABS); set(0x6C, M_JMP, AM_IND); set(0x20, M_JSR, AM_ABS);
    for (i = 0; i < (int)(sizeof branches / sizeof branches[0]); i++)
        set(branches[i].op, branches[i].mnem, AM_REL);
    for (i = 0; i < (int)(sizeof implied / sizeof implied[0]); i++)
        set(implied[i].op, implied[i].mnem, AM_IMP);

    /* 740 extensions */
    set(0x02, M_JSR, AM_ZPI); set(0xB2, M_JMP, AM_ZPI); set(0x22, M_JSR, AM_SPG);
    set(0x3C, M_LDM, AM_LDM); set(0x44, M_COM, AM_ZP); set(0x64, M_TST, AM_ZP); set(0x82, M_RRF, AM_ZP);
    set(0x1A, M_DEC, AM_ACC); set(0x3A, M_INC, AM_ACC);
    set(0x62, M_MUL, AM_ZPX); set(0xE2, M_DIV, AM_ZPX);
    for (i = 0; i < 8; i++) {
        set(0x03 + 0x20 * i, M_BBS, AM_BIT_A_REL);
        set(0x13 + 0x20 * i, M_BBC, AM_BIT_A_REL);
        set(0x07 + 0x20 * i, M_BBS, AM_BIT_ZP_REL);
        set(0x17 + 0x20 * i, M_BBC, AM_BIT_ZP_REL);
        set(0x0B + 0x20 * i, M_SEB, AM_BIT_A);
        set(0x1B + 0x20 * i, M_CLB, AM_BIT_A);
        set(0x0F + 0x20 * i, M_SEB, AM_BIT_ZP);
        set(0x1F + 0x20 * i, M_CLB, AM_BIT_ZP);
    }
}

const cpu740_opinfo *cpu740_optable(void)
{
    if (!g_built) {
        build();
        g_built = true;
    }
    return g_table;
}

int cpu740_mode_length(int mode)
{
    switch (mode) {
    case AM_IMP: case AM_ACC: case AM_BIT_A:
        return 1;
    case AM_ABS: case AM_ABX: case AM_ABY: case AM_IND: case AM_LDM: case AM_BIT_ZP_REL:
        return 3;
    default:
        return 2;
    }
}

const char *cpu740_mnem_name(int mnem)
{
    return (mnem >= 0 && mnem < M_COUNT) ? k_names[mnem] : "???";
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test optable"`
Expected: PASS: `suite_optable  ok`, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add src/cpu740/optable.h src/cpu740/optable.c tests/test_optable.c tests/suites.h
git commit -m "Add Mitsubishi 740 opcode table

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: CPU core — 6502 base instruction set, interrupts, faults

**Files:**
- Create: `src/cpu740/cpu740.h`, `src/cpu740/cpu740.c`, `tests/cpu_harness.h`, `tests/test_cpu_base.c`
- Modify: `tests/suites.h` (append `SUITE(suite_cpu_base)`)

**Interfaces:**
- Consumes: `cpu740_optable()`, the `AM_*`/`M_*` enums.
- Produces (`src/cpu740/cpu740.h`):
  - flag bits `CPU740_C Z I D B T V N` = `0x01 0x02 0x04 0x08 0x10 0x20 0x40 0x80`
  - `cpu740_fault` values: `CPU740_OK`, `CPU740_FAULT_ILLEGAL`, `CPU740_FAULT_UNSUPPORTED`, `CPU740_FAULT_TMODE`
  - `cpu740_bus { void *ctx; uint8_t (*read)(void*, uint16_t); void (*write)(void*, uint16_t, uint8_t); }`
  - `cpu740 { a, x, y, s, p, pc, waiting, irq_lines, cycles, fault, fault_pc, fault_opcode }`
  - `void cpu740_reset(cpu740 *, const cpu740_bus *)`: SP=`$FF` (stack in page 1), P=I|B, PC=[`$FFFE`]
  - `int cpu740_step(cpu740 *, const cpu740_bus *)`: returns cycles; returns 0 when faulted (state frozen, `pc = fault_pc`)
  - `void cpu740_set_irq(cpu740 *, int line, bool asserted)`: line n uses vector `0xFFFC - 2n`; the lowest asserted line wins

- [ ] **Step 1: Write the harness and failing tests**

`tests/cpu_harness.h`:
```c
#ifndef CPU_HARNESS_H
#define CPU_HARNESS_H
/* Flat 64 KB memory for CPU tests. Each test file gets its own copy. */
#include <string.h>
#include "cpu740/cpu740.h"

typedef struct { uint8_t mem[0x10000]; } flatmem;

static flatmem g_mem;
static cpu740 g_cpu;

static inline uint8_t fm_read(void *ctx, uint16_t a) { return ((flatmem *)ctx)->mem[a]; }
static inline void fm_write(void *ctx, uint16_t a, uint8_t v) { ((flatmem *)ctx)->mem[a] = v; }

static const cpu740_bus g_bus = { &g_mem, fm_read, fm_write };

/* Clear memory, copy code to org, point RESET at it, reset the CPU. */
static inline void load(uint16_t org, const uint8_t *code, size_t n)
{
    memset(&g_mem, 0, sizeof g_mem);
    memcpy(g_mem.mem + org, code, n);
    g_mem.mem[0xFFFE] = (uint8_t)org;
    g_mem.mem[0xFFFF] = (uint8_t)(org >> 8);
    cpu740_reset(&g_cpu, &g_bus);
}

/* Execute `steps` steps; returns the total cycles reported. */
static inline int run(int steps)
{
    int total = 0;
    for (int i = 0; i < steps; i++)
        total += cpu740_step(&g_cpu, &g_bus);
    return total;
}
#endif
```

`tests/test_cpu_base.c`:
```c
#include "test.h"
#include "cpu_harness.h"

static void test_reset(void)
{
    static const uint8_t code[] = { 0xEA };
    load(0x8000, code, sizeof code);
    CHECK_EQ(g_cpu.pc, 0x8000);
    CHECK_EQ(g_cpu.s, 0xFF);
    CHECK(g_cpu.p & CPU740_I);
    CHECK_EQ(g_cpu.fault, CPU740_OK);
}

static void test_lda_flags(void)
{
    static const uint8_t code[] = { 0xA9, 0x00, 0xA9, 0x80 };   /* LDA #$00; LDA #$80 */
    load(0x8000, code, sizeof code);
    run(1); CHECK_EQ(g_cpu.a, 0x00); CHECK(g_cpu.p & CPU740_Z); CHECK(!(g_cpu.p & CPU740_N));
    run(1); CHECK_EQ(g_cpu.a, 0x80); CHECK(!(g_cpu.p & CPU740_Z)); CHECK(g_cpu.p & CPU740_N);
}

static void test_adc_sbc(void)
{
    static const uint8_t code[] = {
        0x18, 0xA9, 0x50, 0x69, 0x50,   /* CLC; LDA #$50; ADC #$50 -> $A0, V N, no C */
        0x18, 0xA9, 0xFF, 0x69, 0x01,   /* CLC; LDA #$FF; ADC #$01 -> $00, C Z */
        0x38, 0xA9, 0x05, 0xE9, 0x06,   /* SEC; LDA #$05; SBC #$06 -> $FF, borrow (C clear), N */
    };
    load(0x8000, code, sizeof code);
    run(3); CHECK_EQ(g_cpu.a, 0xA0); CHECK(g_cpu.p & CPU740_V); CHECK(g_cpu.p & CPU740_N); CHECK(!(g_cpu.p & CPU740_C));
    run(3); CHECK_EQ(g_cpu.a, 0x00); CHECK(g_cpu.p & CPU740_C); CHECK(g_cpu.p & CPU740_Z);
    run(3); CHECK_EQ(g_cpu.a, 0xFF); CHECK(!(g_cpu.p & CPU740_C)); CHECK(g_cpu.p & CPU740_N);
}

static void test_compare(void)
{
    static const uint8_t code[] = { 0xA9, 0x05, 0xC9, 0x05, 0xC9, 0x06 };  /* LDA #5; CMP #5; CMP #6 */
    load(0x8000, code, sizeof code);
    run(2); CHECK(g_cpu.p & CPU740_Z); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK(!(g_cpu.p & CPU740_Z)); CHECK(!(g_cpu.p & CPU740_C)); CHECK(g_cpu.p & CPU740_N);
}

static void test_indirect_y(void)
{
    static const uint8_t code[] = { 0xA0, 0x02, 0xB1, 0x10 };   /* LDY #2; LDA ($10),Y */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0x00; g_mem.mem[0x11] = 0x30; g_mem.mem[0x3002] = 0x5A;
    run(2); CHECK_EQ(g_cpu.a, 0x5A);
}

static void test_jsr_rts(void)
{
    static const uint8_t code[] = { 0x20, 0x10, 0x80, 0xEA };   /* $8000 JSR $8010; $8003 NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x8010] = 0x60;                                   /* RTS */
    run(1);
    CHECK_EQ(g_cpu.pc, 0x8010);
    CHECK_EQ(g_mem.mem[0x01FF], 0x80);                          /* return address - 1, high first */
    CHECK_EQ(g_mem.mem[0x01FE], 0x02);
    run(1);
    CHECK_EQ(g_cpu.pc, 0x8003);
    CHECK_EQ(g_cpu.s, 0xFF);
}

static void test_branches(void)
{
    /* $8000 LDA #0; $8002 BEQ +2 -> $8006; $8006 BNE -6 not taken -> $8008 */
    static const uint8_t code[] = { 0xA9, 0x00, 0xF0, 0x02, 0xEA, 0xEA, 0xD0, 0xFA };
    load(0x8000, code, sizeof code);
    run(2); CHECK_EQ(g_cpu.pc, 0x8006);
    run(1); CHECK_EQ(g_cpu.pc, 0x8008);
}

static void test_backward_loop(void)
{
    static const uint8_t code[] = { 0xA2, 0x03, 0xCA, 0xD0, 0xFD };  /* LDX #3; loop: DEX; BNE loop */
    load(0x8000, code, sizeof code);
    run(7);
    CHECK_EQ(g_cpu.x, 0);
    CHECK_EQ(g_cpu.pc, 0x8005);
}

static void test_jmp_indirect_page_wrap(void)
{
    static const uint8_t code[] = { 0x6C, 0xFF, 0x30 };         /* JMP ($30FF) */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x30FF] = 0x34; g_mem.mem[0x3000] = 0x12; g_mem.mem[0x3100] = 0x99;
    run(1);
    CHECK_EQ(g_cpu.pc, 0x1234);
}

static void test_shifts(void)
{
    /* SEC; LDA #$81; ROL A -> $03 C; ROR A -> $81 C; ASL A -> $02 C; LSR A -> $01 no C */
    static const uint8_t code[] = { 0x38, 0xA9, 0x81, 0x2A, 0x6A, 0x0A, 0x4A };
    load(0x8000, code, sizeof code);
    run(3); CHECK_EQ(g_cpu.a, 0x03); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK_EQ(g_cpu.a, 0x81); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK_EQ(g_cpu.a, 0x02); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK_EQ(g_cpu.a, 0x01); CHECK(!(g_cpu.p & CPU740_C));
}

static void test_inc_dec_memory(void)
{
    static const uint8_t code[] = { 0xE6, 0x10, 0xC6, 0x11 };   /* INC $10; DEC $11 */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0xFF; g_mem.mem[0x11] = 0x01;
    run(1); CHECK_EQ(g_mem.mem[0x10], 0x00); CHECK(g_cpu.p & CPU740_Z);
    run(1); CHECK_EQ(g_mem.mem[0x11], 0x00); CHECK(g_cpu.p & CPU740_Z);
}

static void test_php_plp(void)
{
    static const uint8_t code[] = { 0x38, 0x08, 0x18, 0x28 };   /* SEC; PHP; CLC; PLP */
    load(0x8000, code, sizeof code);
    run(4);
    CHECK(g_cpu.p & CPU740_C);
    CHECK(g_cpu.p & CPU740_B);
    CHECK_EQ(g_cpu.s, 0xFF);
}

static void test_irq_entry_and_rti(void)
{
    static const uint8_t code[] = { 0x58, 0xEA, 0xEA };          /* CLI; NOP; NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0xFFF8] = 0x00; g_mem.mem[0xFFF9] = 0x90;          /* line 2 vector -> $9000 */
    g_mem.mem[0x9000] = 0x40;                                    /* RTI */
    run(1);
    cpu740_set_irq(&g_cpu, 2, true);
    CHECK_EQ(run(1), 7);
    CHECK_EQ(g_cpu.pc, 0x9000);
    CHECK(g_cpu.p & CPU740_I);
    CHECK_EQ(g_mem.mem[0x01FF], 0x80);
    CHECK_EQ(g_mem.mem[0x01FE], 0x01);
    CHECK(!(g_mem.mem[0x01FD] & CPU740_B));                      /* hardware IRQ pushes B clear */
    cpu740_set_irq(&g_cpu, 2, false);
    run(1);
    CHECK_EQ(g_cpu.pc, 0x8001);
    CHECK(!(g_cpu.p & CPU740_I));
}

static void test_irq_priority_and_mask(void)
{
    static const uint8_t code[] = { 0xEA, 0x58, 0xEA };          /* NOP (I set by reset); CLI; NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0xFFFA] = 0x00; g_mem.mem[0xFFFB] = 0xA0;          /* line 1 -> $A000 */
    g_mem.mem[0xFFF8] = 0x00; g_mem.mem[0xFFF9] = 0x90;          /* line 2 -> $9000 */
    cpu740_set_irq(&g_cpu, 1, true);
    cpu740_set_irq(&g_cpu, 2, true);
    run(1); CHECK_EQ(g_cpu.pc, 0x8001);                         /* masked: the NOP ran */
    run(1);                                                      /* CLI */
    run(1); CHECK_EQ(g_cpu.pc, 0xA000);                         /* lower line number wins */
}

static void test_faults(void)
{
    static const uint8_t illegal[] = { 0x89 };
    static const uint8_t sed[] = { 0xF8 };
    static const uint8_t brk[] = { 0x00 };
    load(0x8000, illegal, sizeof illegal);
    CHECK_EQ(run(1), 0);
    CHECK_EQ(g_cpu.fault, CPU740_FAULT_ILLEGAL);
    CHECK_EQ(g_cpu.fault_pc, 0x8000);
    CHECK_EQ(g_cpu.fault_opcode, 0x89);
    CHECK_EQ(g_cpu.pc, 0x8000);
    CHECK_EQ(run(1), 0);                                         /* stays faulted */
    load(0x8000, sed, sizeof sed);
    run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, brk, sizeof brk);
    run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
}

void suite_cpu_base(void)
{
    test_reset();
    test_lda_flags();
    test_adc_sbc();
    test_compare();
    test_indirect_y();
    test_jsr_rts();
    test_branches();
    test_backward_loop();
    test_jmp_indirect_page_wrap();
    test_shifts();
    test_inc_dec_memory();
    test_php_plp();
    test_irq_entry_and_rti();
    test_irq_priority_and_mask();
    test_faults();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_cpu_base)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test cpu_base"`
Expected: FAIL to compile: `cannot open include file 'cpu740/cpu740.h'`.

- [ ] **Step 3: Implement the core**

`src/cpu740/cpu740.h`:
```c
#ifndef CPU740_H
#define CPU740_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CPU740_C = 0x01, CPU740_Z = 0x02, CPU740_I = 0x04, CPU740_D = 0x08,
    CPU740_B = 0x10, CPU740_T = 0x20, CPU740_V = 0x40, CPU740_N = 0x80,
};

typedef enum {
    CPU740_OK = 0,
    CPU740_FAULT_ILLEGAL,      /* opcode the M50734 does not implement */
    CPU740_FAULT_UNSUPPORTED,  /* SET/SED/STP/MUL/DIV/BRK: verified unused by the Apollo ROM */
    CPU740_FAULT_TMODE,        /* T flag restored as 1 by PLP/RTI */
} cpu740_fault;

typedef struct {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} cpu740_bus;

typedef struct {
    uint8_t a, x, y, s, p;
    uint16_t pc;
    bool waiting;          /* executed WIT: halted until any IRQ line is asserted */
    uint32_t irq_lines;    /* bit n set = line n asserted; vector = 0xFFFC - 2n */
    uint64_t cycles;
    cpu740_fault fault;
    uint16_t fault_pc;
    uint8_t fault_opcode;
} cpu740;

void cpu740_reset(cpu740 *cpu, const cpu740_bus *bus);
int cpu740_step(cpu740 *cpu, const cpu740_bus *bus);
void cpu740_set_irq(cpu740 *cpu, int line, bool asserted);

#endif
```

`src/cpu740/cpu740.c`:
```c
#include "cpu740/cpu740.h"
#include "cpu740/optable.h"

/* Approximate cycle counts per addressing mode; only the timer pacing depends on them. */
static const uint8_t k_mode_cycles[AM_COUNT] = {
    /* IMP ACC IMM ZP ZPX ZPY ABS ABX ABY IND IZX IZY REL ZPI SPG LDM BA BZ BAR BZR */
       2,  2,  2,  3, 4,  4,  4,  4,  4,  5,  6,  5,  2,  5,  5,  4,  2, 5, 4,  5,
};

typedef struct {
    uint16_t ea;      /* effective address; for AM_IMM the address of the operand byte */
    uint16_t target;  /* branch target */
    uint8_t imm;      /* LDM immediate */
    uint8_t bit;      /* bit number for SEB/CLB/BBS/BBC */
} operand;

static uint8_t rd(const cpu740_bus *b, uint16_t a) { return b->read(b->ctx, a); }
static void wr(const cpu740_bus *b, uint16_t a, uint8_t v) { b->write(b->ctx, a, v); }
static uint8_t fetch(cpu740 *c, const cpu740_bus *b) { return rd(b, c->pc++); }

static void push(cpu740 *c, const cpu740_bus *b, uint8_t v)
{
    wr(b, (uint16_t)(0x0100 | c->s), v);
    c->s--;
}

static uint8_t pull(cpu740 *c, const cpu740_bus *b)
{
    c->s++;
    return rd(b, (uint16_t)(0x0100 | c->s));
}

static void set_flag(cpu740 *c, uint8_t f, bool on)
{
    if (on)
        c->p |= f;
    else
        c->p &= (uint8_t)(0xFF ^ f);
}

static void set_nz(cpu740 *c, uint8_t v)
{
    set_flag(c, CPU740_N, (v & 0x80) != 0);
    set_flag(c, CPU740_Z, v == 0);
}

static uint16_t read16_zp(const cpu740_bus *b, uint8_t zp)
{
    return (uint16_t)(rd(b, zp) | (rd(b, (uint8_t)(zp + 1)) << 8));
}

void cpu740_reset(cpu740 *c, const cpu740_bus *b)
{
    c->a = c->x = c->y = 0;
    c->s = 0xFF;
    c->p = CPU740_I | CPU740_B;
    c->waiting = false;
    c->irq_lines = 0;
    c->cycles = 0;
    c->fault = CPU740_OK;
    c->fault_pc = 0;
    c->fault_opcode = 0;
    c->pc = (uint16_t)(rd(b, 0xFFFE) | (rd(b, 0xFFFF) << 8));
}

void cpu740_set_irq(cpu740 *c, int line, bool asserted)
{
    uint32_t bit = 1u << line;
    if (asserted)
        c->irq_lines |= bit;
    else
        c->irq_lines &= ~bit;
}

static int take_irq(cpu740 *c, const cpu740_bus *b)
{
    int line = 0;
    while (!(c->irq_lines & (1u << line)))
        line++;
    uint16_t vec = (uint16_t)(0xFFFC - 2 * line);
    push(c, b, (uint8_t)(c->pc >> 8));
    push(c, b, (uint8_t)c->pc);
    push(c, b, (uint8_t)(c->p & (0xFF ^ CPU740_B)));
    c->p |= CPU740_I;
    c->pc = (uint16_t)(rd(b, vec) | (rd(b, (uint16_t)(vec + 1)) << 8));
    return 7;
}

static operand decode(cpu740 *c, const cpu740_bus *b, uint8_t op, int mode)
{
    operand o = { 0 };
    uint8_t lo, hi, zp;
    int8_t off;
    o.bit = (uint8_t)(op >> 5);
    switch (mode) {
    case AM_IMM: o.ea = c->pc++; break;
    case AM_ZP:  o.ea = fetch(c, b); break;
    case AM_ZPX: o.ea = (uint8_t)(fetch(c, b) + c->x); break;
    case AM_ZPY: o.ea = (uint8_t)(fetch(c, b) + c->y); break;
    case AM_ABS: lo = fetch(c, b); hi = fetch(c, b); o.ea = (uint16_t)(lo | hi << 8); break;
    case AM_ABX: lo = fetch(c, b); hi = fetch(c, b); o.ea = (uint16_t)((lo | hi << 8) + c->x); break;
    case AM_ABY: lo = fetch(c, b); hi = fetch(c, b); o.ea = (uint16_t)((lo | hi << 8) + c->y); break;
    case AM_IND: {
        lo = fetch(c, b); hi = fetch(c, b);
        uint16_t ptr = (uint16_t)(lo | hi << 8);
        uint16_t ptr_hi = (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));  /* 6502 page wrap */
        o.ea = (uint16_t)(rd(b, ptr) | rd(b, ptr_hi) << 8);
        break;
    }
    case AM_IZX: zp = (uint8_t)(fetch(c, b) + c->x); o.ea = read16_zp(b, zp); break;
    case AM_IZY: zp = fetch(c, b); o.ea = (uint16_t)(read16_zp(b, zp) + c->y); break;
    case AM_REL:
    case AM_BIT_A_REL:
        off = (int8_t)fetch(c, b);
        o.target = (uint16_t)(c->pc + off);
        break;
    case AM_ZPI: zp = fetch(c, b); o.ea = read16_zp(b, zp); break;
    case AM_SPG: o.ea = (uint16_t)(0xFF00 | fetch(c, b)); break;
    case AM_LDM: o.imm = fetch(c, b); o.ea = fetch(c, b); break;
    case AM_BIT_ZP: o.ea = fetch(c, b); break;
    case AM_BIT_ZP_REL:
        o.ea = fetch(c, b);
        off = (int8_t)fetch(c, b);
        o.target = (uint16_t)(c->pc + off);
        break;
    default: break;   /* AM_IMP, AM_ACC, AM_BIT_A: no operand bytes */
    }
    return o;
}

static void adc(cpu740 *c, uint8_t v)
{
    unsigned sum = (unsigned)c->a + v + (c->p & CPU740_C);
    set_flag(c, CPU740_V, ((~(c->a ^ v) & (c->a ^ sum)) & 0x80) != 0);
    set_flag(c, CPU740_C, sum > 0xFF);
    c->a = (uint8_t)sum;
    set_nz(c, c->a);
}

static void compare(cpu740 *c, uint8_t reg, uint8_t v)
{
    set_flag(c, CPU740_C, reg >= v);
    set_nz(c, (uint8_t)(reg - v));
}

static uint8_t shift(cpu740 *c, int mnem, uint8_t v)
{
    uint8_t carry_in = (uint8_t)(c->p & CPU740_C);
    uint8_t r;
    switch (mnem) {
    case M_ASL: set_flag(c, CPU740_C, (v & 0x80) != 0); r = (uint8_t)(v << 1); break;
    case M_ROL: set_flag(c, CPU740_C, (v & 0x80) != 0); r = (uint8_t)((v << 1) | carry_in); break;
    case M_LSR: set_flag(c, CPU740_C, (v & 0x01) != 0); r = (uint8_t)(v >> 1); break;
    default:    set_flag(c, CPU740_C, (v & 0x01) != 0); r = (uint8_t)((v >> 1) | (carry_in << 7)); break;
    }
    set_nz(c, r);
    return r;
}

static bool branch_taken(const cpu740 *c, int mnem)
{
    switch (mnem) {
    case M_BPL: return !(c->p & CPU740_N);
    case M_BMI: return (c->p & CPU740_N) != 0;
    case M_BVC: return !(c->p & CPU740_V);
    case M_BVS: return (c->p & CPU740_V) != 0;
    case M_BCC: return !(c->p & CPU740_C);
    case M_BCS: return (c->p & CPU740_C) != 0;
    case M_BNE: return !(c->p & CPU740_Z);
    case M_BEQ: return (c->p & CPU740_Z) != 0;
    default:    return true;   /* BRA */
    }
}

int cpu740_step(cpu740 *c, const cpu740_bus *b)
{
    if (c->fault != CPU740_OK)
        return 0;
    if (c->waiting) {
        if (!c->irq_lines) {
            c->cycles += 4;
            return 4;
        }
        c->waiting = false;   /* any asserted line ends WIT, even while I is set */
    }
    if (c->irq_lines && !(c->p & CPU740_I)) {
        int n = take_irq(c, b);
        c->cycles += (uint64_t)n;
        return n;
    }

    uint16_t op_pc = c->pc;
    uint8_t op = fetch(c, b);
    const cpu740_opinfo info = cpu740_optable()[op];
    operand o = decode(c, b, op, info.mode);
    int cycles = k_mode_cycles[info.mode];
    cpu740_fault fault = CPU740_OK;
    uint8_t v;

    switch (info.mnem) {
    case M_LDA: c->a = rd(b, o.ea); set_nz(c, c->a); break;
    case M_LDX: c->x = rd(b, o.ea); set_nz(c, c->x); break;
    case M_LDY: c->y = rd(b, o.ea); set_nz(c, c->y); break;
    case M_STA: wr(b, o.ea, c->a); break;
    case M_STX: wr(b, o.ea, c->x); break;
    case M_STY: wr(b, o.ea, c->y); break;

    case M_ADC:
    case M_SBC:
        if (c->p & CPU740_D) {   /* decimal mode is never used by the Apollo ROM */
            fault = CPU740_FAULT_UNSUPPORTED;
            break;
        }
        v = rd(b, o.ea);
        adc(c, info.mnem == M_ADC ? v : (uint8_t)~v);
        break;
    case M_AND: c->a &= rd(b, o.ea); set_nz(c, c->a); break;
    case M_ORA: c->a |= rd(b, o.ea); set_nz(c, c->a); break;
    case M_EOR: c->a ^= rd(b, o.ea); set_nz(c, c->a); break;
    case M_CMP: compare(c, c->a, rd(b, o.ea)); break;
    case M_CPX: compare(c, c->x, rd(b, o.ea)); break;
    case M_CPY: compare(c, c->y, rd(b, o.ea)); break;
    case M_BIT:
        v = rd(b, o.ea);
        set_flag(c, CPU740_N, (v & 0x80) != 0);
        set_flag(c, CPU740_V, (v & 0x40) != 0);
        set_flag(c, CPU740_Z, (c->a & v) == 0);
        break;

    case M_ASL: case M_ROL: case M_LSR: case M_ROR:
        if (info.mode == AM_ACC) {
            c->a = shift(c, info.mnem, c->a);
        } else {
            v = rd(b, o.ea);
            wr(b, o.ea, shift(c, info.mnem, v));
            cycles += 2;
        }
        break;
    case M_INC:
    case M_DEC: {
        int delta = info.mnem == M_INC ? 1 : -1;
        if (info.mode == AM_ACC) {   /* 740 INC A / DEC A */
            c->a = (uint8_t)(c->a + delta);
            set_nz(c, c->a);
        } else {
            v = (uint8_t)(rd(b, o.ea) + delta);
            wr(b, o.ea, v);
            set_nz(c, v);
            cycles += 2;
        }
        break;
    }

    case M_INX: c->x++; set_nz(c, c->x); break;
    case M_INY: c->y++; set_nz(c, c->y); break;
    case M_DEX: c->x--; set_nz(c, c->x); break;
    case M_DEY: c->y--; set_nz(c, c->y); break;
    case M_TAX: c->x = c->a; set_nz(c, c->x); break;
    case M_TAY: c->y = c->a; set_nz(c, c->y); break;
    case M_TXA: c->a = c->x; set_nz(c, c->a); break;
    case M_TYA: c->a = c->y; set_nz(c, c->a); break;
    case M_TSX: c->x = c->s; set_nz(c, c->x); break;
    case M_TXS: c->s = c->x; break;

    case M_CLC: set_flag(c, CPU740_C, false); break;
    case M_SEC: set_flag(c, CPU740_C, true); break;
    case M_CLI: set_flag(c, CPU740_I, false); break;
    case M_SEI: set_flag(c, CPU740_I, true); break;
    case M_CLD: set_flag(c, CPU740_D, false); break;
    case M_CLV: set_flag(c, CPU740_V, false); break;
    case M_SED: fault = CPU740_FAULT_UNSUPPORTED; break;
    case M_NOP: break;

    case M_BPL: case M_BMI: case M_BVC: case M_BVS:
    case M_BCC: case M_BCS: case M_BNE: case M_BEQ: case M_BRA:
        if (branch_taken(c, info.mnem)) {
            c->pc = o.target;
            cycles += 1;
        }
        break;

    case M_JMP:
        c->pc = o.ea;
        if (info.mode == AM_ABS)
            cycles = 3;
        break;
    case M_JSR:
        push(c, b, (uint8_t)((c->pc - 1) >> 8));   /* address of the instruction's last byte */
        push(c, b, (uint8_t)(c->pc - 1));
        c->pc = o.ea;
        cycles = info.mode == AM_ABS ? 6 : info.mode == AM_ZPI ? 7 : 5;
        break;
    case M_RTS: {
        uint8_t lo = pull(c, b);
        uint8_t hi = pull(c, b);
        c->pc = (uint16_t)((lo | hi << 8) + 1);
        cycles = 6;
        break;
    }
    case M_RTI: {
        c->p = (uint8_t)(pull(c, b) | CPU740_B);
        uint8_t lo = pull(c, b);
        uint8_t hi = pull(c, b);
        c->pc = (uint16_t)(lo | hi << 8);
        cycles = 6;
        if (c->p & CPU740_T)
            fault = CPU740_FAULT_TMODE;
        break;
    }
    case M_PHA: push(c, b, c->a); cycles = 3; break;
    case M_PHP: push(c, b, c->p); cycles = 3; break;
    case M_PLA: c->a = pull(c, b); set_nz(c, c->a); cycles = 4; break;
    case M_PLP:
        c->p = (uint8_t)(pull(c, b) | CPU740_B);
        cycles = 4;
        if (c->p & CPU740_T)
            fault = CPU740_FAULT_TMODE;
        break;
    case M_BRK: fault = CPU740_FAULT_UNSUPPORTED; break;   /* never used: usually a runaway PC */

    default: fault = CPU740_FAULT_ILLEGAL; break;
    }

    if (fault != CPU740_OK) {
        c->fault = fault;
        c->fault_pc = op_pc;
        c->fault_opcode = op;
        c->pc = op_pc;
        return 0;
    }
    c->cycles += (uint64_t)cycles;
    return cycles;
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test cpu_base"`
Expected: PASS: `suite_cpu_base  ok`, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add src/cpu740/cpu740.h src/cpu740/cpu740.c tests/cpu_harness.h tests/test_cpu_base.c tests/suites.h
git commit -m "Add 740 CPU core with 6502 base instructions, IRQs and faults

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: CPU core — 740 extension instructions

**Files:**
- Modify: `src/cpu740/cpu740.c` (add cases to the `switch` in `cpu740_step`)
- Create: `tests/test_cpu_740.c`
- Modify: `tests/suites.h` (append `SUITE(suite_cpu_740)`)

**Interfaces:**
- Consumes: everything from Task 3.
- Produces: execution of `BBS`/`BBC` (on A and on zero page), `SEB`/`CLB` (on A and on zero page), `LDM`, `TST`, `COM`, `RRF`, `CLT` and `WIT`, plus `JSR ($zz)`, `JSR \$FFzz` and `JMP ($zz)`, which go through the existing JMP/JSR cases. `SET`, `STP`, `MUL` and `DIV` fault with `CPU740_FAULT_UNSUPPORTED`.

- [ ] **Step 1: Write the failing tests**

`tests/test_cpu_740.c`:
```c
#include "test.h"
#include "cpu_harness.h"

static void test_bbs_bbc_zp(void)
{
    static const uint8_t code[] = {
        0x07, 0x10, 0x02,   /* $8000 BBS0 $10,$8005 */
        0xEA, 0xEA,
        0x37, 0x10, 0x02,   /* $8005 BBC1 $10,$800A */
        0xEA, 0xEA, 0xEA,
    };
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0x01;
    run(1); CHECK_EQ(g_cpu.pc, 0x8005);   /* bit 0 set: taken */
    run(1); CHECK_EQ(g_cpu.pc, 0x800A);   /* bit 1 clear: taken */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0x02;
    run(1); CHECK_EQ(g_cpu.pc, 0x8003);   /* bit 0 clear: falls through */
}

static void test_bbs_bbc_acc(void)
{
    /* LDA #$80; $8002 BBS7 A,$8005; $8005 BBC7 A,$8008 (not taken -> $8007) */
    static const uint8_t code[] = { 0xA9, 0x80, 0xE3, 0x01, 0xEA, 0xF3, 0x01, 0xEA, 0xEA };
    load(0x8000, code, sizeof code);
    run(2); CHECK_EQ(g_cpu.pc, 0x8005);
    run(1); CHECK_EQ(g_cpu.pc, 0x8007);
}

static void test_seb_clb(void)
{
    /* SEB7 $20; CLB0 $21; LDA #0; SEB3 A; CLB3 A */
    static const uint8_t code[] = { 0xEF, 0x20, 0x1F, 0x21, 0xA9, 0x00, 0x6B, 0x7B };
    load(0x8000, code, sizeof code);
    g_mem.mem[0x21] = 0xFF;
    run(1); CHECK_EQ(g_mem.mem[0x20], 0x80);
    run(1); CHECK_EQ(g_mem.mem[0x21], 0xFE);
    run(2); CHECK_EQ(g_cpu.a, 0x08);
    run(1); CHECK_EQ(g_cpu.a, 0x00);
}

static void test_ldm_tst_com_rrf(void)
{
    /* LDM #$F9,$EF; TST $EF; COM $EF; RRF $EF */
    static const uint8_t code[] = { 0x3C, 0xF9, 0xEF, 0x64, 0xEF, 0x44, 0xEF, 0x82, 0xEF };
    load(0x8000, code, sizeof code);
    run(1); CHECK_EQ(g_mem.mem[0xEF], 0xF9);
    run(1); CHECK(g_cpu.p & CPU740_N); CHECK(!(g_cpu.p & CPU740_Z));
    run(1); CHECK_EQ(g_mem.mem[0xEF], 0x06); CHECK(!(g_cpu.p & CPU740_N));
    run(1); CHECK_EQ(g_mem.mem[0xEF], 0x60);
}

static void test_inc_dec_acc(void)
{
    static const uint8_t code[] = { 0xA9, 0xFF, 0x3A, 0x1A };   /* LDA #$FF; INC A; DEC A */
    load(0x8000, code, sizeof code);
    run(2); CHECK_EQ(g_cpu.a, 0x00); CHECK(g_cpu.p & CPU740_Z);
    run(1); CHECK_EQ(g_cpu.a, 0xFF); CHECK(g_cpu.p & CPU740_N);
}

static void test_jumps(void)
{
    static const uint8_t jsr_zpi[] = { 0x02, 0x4C, 0xEA };      /* $8000 JSR ($4C); $8002 NOP */
    static const uint8_t jsr_spg[] = { 0x22, 0x10, 0xEA };      /* $8000 JSR \$FF10 */
    static const uint8_t jmp_zpi[] = { 0xB2, 0x4A };            /* JMP ($4A) */
    static const uint8_t bra[] = { 0x80, 0xFE };                /* BRA * */

    load(0x8000, jsr_zpi, sizeof jsr_zpi);
    g_mem.mem[0x4C] = 0x00; g_mem.mem[0x4D] = 0x90; g_mem.mem[0x9000] = 0x60;
    run(1); CHECK_EQ(g_cpu.pc, 0x9000);
    run(1); CHECK_EQ(g_cpu.pc, 0x8002);

    load(0x8000, jsr_spg, sizeof jsr_spg);
    g_mem.mem[0xFF10] = 0x60;
    run(1); CHECK_EQ(g_cpu.pc, 0xFF10);
    run(1); CHECK_EQ(g_cpu.pc, 0x8002);

    load(0x8000, jmp_zpi, sizeof jmp_zpi);
    g_mem.mem[0x4A] = 0x34; g_mem.mem[0x4B] = 0x12;
    run(1); CHECK_EQ(g_cpu.pc, 0x1234);

    load(0x8000, bra, sizeof bra);
    run(3); CHECK_EQ(g_cpu.pc, 0x8000);
}

static void test_t_flag(void)
{
    static const uint8_t clt[] = { 0x12 };
    static const uint8_t set[] = { 0x32 };
    static const uint8_t plp_t[] = { 0xA9, 0x20, 0x48, 0x28 };  /* LDA #$20; PHA; PLP */
    load(0x8000, clt, sizeof clt);
    g_cpu.p |= CPU740_T;
    run(1); CHECK(!(g_cpu.p & CPU740_T)); CHECK_EQ(g_cpu.fault, CPU740_OK);
    load(0x8000, set, sizeof set);
    run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, plp_t, sizeof plp_t);
    run(3); CHECK_EQ(g_cpu.fault, CPU740_FAULT_TMODE);
}

static void test_wit(void)
{
    static const uint8_t code[] = { 0x58, 0xC2, 0xEA };          /* CLI; WIT; NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0xFFF8] = 0x00; g_mem.mem[0xFFF9] = 0x90; g_mem.mem[0x9000] = 0x40;
    run(2); CHECK(g_cpu.waiting); CHECK_EQ(g_cpu.pc, 0x8002);
    run(5); CHECK(g_cpu.waiting); CHECK_EQ(g_cpu.pc, 0x8002);   /* halted with no IRQ */
    cpu740_set_irq(&g_cpu, 2, true);
    run(1); CHECK(!g_cpu.waiting); CHECK_EQ(g_cpu.pc, 0x9000);
}

static void test_unsupported(void)
{
    static const uint8_t stp[] = { 0x42 };
    static const uint8_t mul[] = { 0x62, 0x10 };
    static const uint8_t div[] = { 0xE2, 0x10 };
    load(0x8000, stp, sizeof stp); run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, mul, sizeof mul); run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, div, sizeof div); run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
}

void suite_cpu_740(void)
{
    test_bbs_bbc_zp();
    test_bbs_bbc_acc();
    test_seb_clb();
    test_ldm_tst_com_rrf();
    test_inc_dec_acc();
    test_jumps();
    test_t_flag();
    test_wit();
    test_unsupported();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_cpu_740)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test cpu_740"`
Expected: FAIL. The bit-op, LDM, TST, COM, RRF and WIT tests fail with `fault == CPU740_FAULT_ILLEGAL` (they reach the `default:` case). `test_inc_dec_acc` and `test_jumps` already pass.

- [ ] **Step 3: Add the 740 cases**

In `src/cpu740/cpu740.c`, insert these cases into the `switch (info.mnem)` in `cpu740_step`, directly above `default: fault = CPU740_FAULT_ILLEGAL; break;`:
```c
    /* 740 extensions */
    case M_BBS:
    case M_BBC: {
        v = info.mode == AM_BIT_A_REL ? c->a : rd(b, o.ea);
        bool bit_set = ((v >> o.bit) & 1) != 0;
        if (bit_set == (info.mnem == M_BBS)) {
            c->pc = o.target;
            cycles += 2;
        }
        break;
    }
    case M_SEB:
    case M_CLB: {
        uint8_t mask = (uint8_t)(1u << o.bit);
        if (info.mode == AM_BIT_A) {
            c->a = info.mnem == M_SEB ? (uint8_t)(c->a | mask) : (uint8_t)(c->a & ~mask);
        } else {
            v = rd(b, o.ea);
            wr(b, o.ea, info.mnem == M_SEB ? (uint8_t)(v | mask) : (uint8_t)(v & ~mask));
        }
        break;
    }
    case M_LDM: wr(b, o.ea, o.imm); break;
    case M_TST: set_nz(c, rd(b, o.ea)); break;
    case M_COM:
        v = (uint8_t)~rd(b, o.ea);
        set_nz(c, v);
        wr(b, o.ea, v);
        cycles += 2;
        break;
    case M_RRF:
        v = rd(b, o.ea);
        wr(b, o.ea, (uint8_t)((v << 4) | (v >> 4)));
        cycles = 8;
        break;
    case M_CLT: set_flag(c, CPU740_T, false); break;
    case M_WIT: c->waiting = true; break;
    case M_SET: case M_STP: case M_MUL: case M_DIV:
        fault = CPU740_FAULT_UNSUPPORTED;   /* verified unused by the Apollo ROM (spec §1.1) */
        break;
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test cpu"`
Expected: PASS: both `suite_cpu_base` and `suite_cpu_740` are `ok`, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add src/cpu740/cpu740.c tests/test_cpu_740.c tests/suites.h
git commit -m "Add 740 extension instructions to the CPU core

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Disassembler

**Files:**
- Create: `src/cpu740/disasm740.c`, `tests/test_disasm.c`
- Modify: `src/cpu740/cpu740.h` (add the disassembler API), `tests/suites.h` (append `SUITE(suite_disasm)`)

**Interfaces:**
- Consumes: `cpu740_optable()`, `cpu740_mode_length()`, `cpu740_mnem_name()`.
- Produces (in `cpu740.h`):
  - `typedef uint8_t (*cpu740_peek_fn)(void *ctx, uint16_t addr);`
  - `int cpu740_disasm(uint16_t pc, cpu740_peek_fn peek, void *ctx, char *out, size_t outlen);`: returns the instruction length (1 for an illegal opcode, printed as `DB $xx`)
- Output format:
  - `LDM #$F9,$EF`, `BBS0 $10,$A662`, `SEB3 A`, `CLB5 $FE`, `BBS7 A,$8003`
  - `JSR ($4C)`, `JSR \$FF10`, `JMP ($1204)`
  - `LDA ($2A),Y`, `LDA ($10,X)`, `STA $0200,X`, `LDX $10,Y`
  - `ASL A`, `INC A`, `BNE $A639`

- [ ] **Step 1: Write the failing test**

`tests/test_disasm.c`:
```c
#include "test.h"
#include "cpu740/cpu740.h"

static uint8_t g_img[0x10000];

static uint8_t peek(void *ctx, uint16_t a)
{
    (void)ctx;
    return g_img[a];
}

static void expect(uint16_t pc, const uint8_t *bytes, size_t n, const char *want, int want_len)
{
    char text[48];
    memset(g_img, 0, sizeof g_img);
    memcpy(g_img + pc, bytes, n);
    int len = cpu740_disasm(pc, peek, NULL, text, sizeof text);
    CHECK_STR(text, want);
    CHECK_EQ(len, want_len);
}

void suite_disasm(void)
{
    /* Encodings taken from the Apollo ROM listing */
    expect(0xA61C, (const uint8_t[]){ 0x3C, 0xF9, 0xEF }, 3, "LDM #$F9,$EF", 3);
    expect(0xA629, (const uint8_t[]){ 0x12 }, 1, "CLT", 1);
    expect(0xA657, (const uint8_t[]){ 0x07, 0x10, 0x08 }, 3, "BBS0 $10,$A662", 3);
    expect(0xB2E5, (const uint8_t[]){ 0xB1, 0x2A }, 2, "LDA ($2A),Y", 2);
    expect(0xB2E7, (const uint8_t[]){ 0x8D, 0x00, 0x80 }, 3, "STA $8000", 3);
    expect(0xBB9D, (const uint8_t[]){ 0x6C, 0x04, 0x12 }, 3, "JMP ($1204)", 3);
    expect(0xC46A, (const uint8_t[]){ 0xB2, 0x4A }, 2, "JMP ($4A)", 2);
    expect(0x0313, (const uint8_t[]){ 0x02, 0x4C }, 2, "JSR ($4C)", 2);
    expect(0x8000, (const uint8_t[]){ 0x22, 0x10 }, 2, "JSR \\$FF10", 2);
    expect(0x8000, (const uint8_t[]){ 0xE3, 0x01 }, 2, "BBS7 A,$8003", 2);
    expect(0x8000, (const uint8_t[]){ 0x6B }, 1, "SEB3 A", 1);
    expect(0xB2D9, (const uint8_t[]){ 0xBF, 0xFE }, 2, "CLB5 $FE", 2);
    expect(0x8000, (const uint8_t[]){ 0x0A }, 1, "ASL A", 1);
    expect(0x8000, (const uint8_t[]){ 0x3A }, 1, "INC A", 1);
    expect(0x8000, (const uint8_t[]){ 0xB6, 0x10 }, 2, "LDX $10,Y", 2);
    expect(0x8000, (const uint8_t[]){ 0xA1, 0x10 }, 2, "LDA ($10,X)", 2);
    expect(0xA60E, (const uint8_t[]){ 0x9D, 0x00, 0x02 }, 3, "STA $0200,X", 3);
    expect(0xA63C, (const uint8_t[]){ 0xD0, 0xFB }, 2, "BNE $A639", 2);
    expect(0x8000, (const uint8_t[]){ 0x89 }, 1, "DB $89", 1);
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_disasm)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test disasm"`
Expected: FAIL to link: `unresolved external symbol cpu740_disasm`.

- [ ] **Step 3: Implement**

Add to `src/cpu740/cpu740.h`, just before `#endif`:
```c
/* Disassembler: side-effect-free byte reader + text output. Returns the instruction length. */
typedef uint8_t (*cpu740_peek_fn)(void *ctx, uint16_t addr);
int cpu740_disasm(uint16_t pc, cpu740_peek_fn peek, void *ctx, char *out, size_t outlen);
```

`src/cpu740/disasm740.c`:
```c
#include <stdio.h>
#include "cpu740/cpu740.h"
#include "cpu740/optable.h"

int cpu740_disasm(uint16_t pc, cpu740_peek_fn peek, void *ctx, char *out, size_t outlen)
{
    uint8_t op = peek(ctx, pc);
    const cpu740_opinfo info = cpu740_optable()[op];
    uint8_t b1 = peek(ctx, (uint16_t)(pc + 1));
    uint8_t b2 = peek(ctx, (uint16_t)(pc + 2));
    uint16_t word = (uint16_t)(b1 | b2 << 8);
    uint16_t rel2 = (uint16_t)(pc + 2 + (int8_t)b1);   /* target of a 2-byte relative branch */
    uint16_t rel3 = (uint16_t)(pc + 3 + (int8_t)b2);   /* target of BBS/BBC n,$zz,rel */
    const char *n = cpu740_mnem_name(info.mnem);
    int bit = op >> 5;

    if (info.mnem == M_ILL) {
        snprintf(out, outlen, "DB $%02X", op);
        return 1;
    }
    switch (info.mode) {
    case AM_IMP:        snprintf(out, outlen, "%s", n); break;
    case AM_ACC:        snprintf(out, outlen, "%s A", n); break;
    case AM_IMM:        snprintf(out, outlen, "%s #$%02X", n, b1); break;
    case AM_ZP:         snprintf(out, outlen, "%s $%02X", n, b1); break;
    case AM_ZPX:        snprintf(out, outlen, "%s $%02X,X", n, b1); break;
    case AM_ZPY:        snprintf(out, outlen, "%s $%02X,Y", n, b1); break;
    case AM_ABS:        snprintf(out, outlen, "%s $%04X", n, word); break;
    case AM_ABX:        snprintf(out, outlen, "%s $%04X,X", n, word); break;
    case AM_ABY:        snprintf(out, outlen, "%s $%04X,Y", n, word); break;
    case AM_IND:        snprintf(out, outlen, "%s ($%04X)", n, word); break;
    case AM_IZX:        snprintf(out, outlen, "%s ($%02X,X)", n, b1); break;
    case AM_IZY:        snprintf(out, outlen, "%s ($%02X),Y", n, b1); break;
    case AM_REL:        snprintf(out, outlen, "%s $%04X", n, rel2); break;
    case AM_ZPI:        snprintf(out, outlen, "%s ($%02X)", n, b1); break;
    case AM_SPG:        snprintf(out, outlen, "%s \\$FF%02X", n, b1); break;
    case AM_LDM:        snprintf(out, outlen, "%s #$%02X,$%02X", n, b1, b2); break;
    case AM_BIT_A:      snprintf(out, outlen, "%s%d A", n, bit); break;
    case AM_BIT_ZP:     snprintf(out, outlen, "%s%d $%02X", n, bit, b1); break;
    case AM_BIT_A_REL:  snprintf(out, outlen, "%s%d A,$%04X", n, bit, rel2); break;
    case AM_BIT_ZP_REL: snprintf(out, outlen, "%s%d $%02X,$%04X", n, bit, b1, rel3); break;
    default:            snprintf(out, outlen, "%s ?", n); break;
    }
    return cpu740_mode_length(info.mode);
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test disasm"`
Expected: PASS: `suite_disasm  ok`.

- [ ] **Step 5: Commit**

```bash
git add src/cpu740/cpu740.h src/cpu740/disasm740.c tests/test_disasm.c tests/suites.h
git commit -m "Add 740 disassembler for fault dumps

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: M50734 on-chip peripherals

**Files:**
- Create: `src/machine/m50734.h`, `src/machine/m50734.c`, `tests/test_m50734.c`
- Modify: `tests/suites.h` (append `SUITE(suite_m50734)`)

**Interfaces:**
- Produces (`src/machine/m50734.h`):
  - Line numbers: `M50734_LINE_INT2PIN=0`, `M50734_LINE_INT1=1` (the _INT1 pin and UART RX, vector `$FFFA`), `M50734_LINE_TIMER=2` (`$FFF8`), `M50734_LINE_STEP=3`, `M50734_LINE_TX_CNTR=4` (`$FFF4`).
  - Address constants `M50734_*`: `UART_TX=$E4`, `UART_RX=$E5`, `UART_CTRL=$E6`, `UART_STATUS=$E7`, `AD_CTRL=$E9`, `AD_DATA=$EA`, `P4=$EB`, `P23_FUNC=$ED`, `P3=$EE`, `P3_DIR=$EF`, `P2=$F0`, `P2_DIR=$F1`, `P1_LATCH=$F2`, `P1=$F3`, `P1_DIR=$F4`, `P0_FUNC=$F5`, `P0=$F6`, `P0_DIR=$F7`, `UART_BAUD=$FB`, `ICR3=$FD`, `ICR2=$FE`, `ICR1=$FF`, `SFR_FIRST=$DA`.
  - `struct m50734` with public fields: `port_latch[4]`, `port_dir[4]`, `port_in[5]`, `p0_func`, `icr1/icr2/icr3`, `uart_ctrl`, `uart_rx_full`, `uart_tx_last`, `uart_tx_count`.
  - `void m50734_reset(m50734 *p, const uint8_t port_in[5]);`
  - `bool m50734_is_modelled(uint8_t addr);`
  - `uint8_t m50734_read(m50734 *p, uint8_t addr);`: reading `$E5` clears RX-full
  - `void m50734_write(m50734 *p, uint8_t addr, uint8_t v);`
  - `void m50734_tick(m50734 *p, int cpu_cycles);`
  - `uint32_t m50734_irq_lines(const m50734 *p);`
  - `uint8_t m50734_port_pins(const m50734 *p, int port);`: `(in & ~dir) | (latch & dir)`
  - `bool m50734_uart_receive(m50734 *p, uint8_t byte);`: false while the previous byte is unread

- [ ] **Step 1: Write the failing test**

`tests/test_m50734.c`:
```c
#include "test.h"
#include "machine/m50734.h"

static m50734 g_p;
static const uint8_t k_in[5] = { 0x00, 0x00, 0x70, 0x06, 0x0A };

static void test_ports(void)
{
    m50734_reset(&g_p, k_in);
    CHECK_EQ(m50734_read(&g_p, M50734_P2), 0x70);          /* all inputs: pins follow the board */
    m50734_write(&g_p, M50734_P2, 0xF1);
    m50734_write(&g_p, M50734_P2_DIR, 0xFD);
    CHECK_EQ(m50734_port_pins(&g_p, 2), 0xF1);             /* bit 1 is an input and reads 0 */
    CHECK_EQ(m50734_read(&g_p, M50734_P2), 0xF1);
    CHECK_EQ(m50734_read(&g_p, M50734_P2_DIR), 0xFD);
    CHECK_EQ(m50734_read(&g_p, M50734_P3) & 0x06, 0x06);   /* P3 jumpers */
    CHECK_EQ(m50734_read(&g_p, M50734_P4), 0x0A);
}

static void test_timer3_period(void)
{
    m50734_reset(&g_p, k_in);
    m50734_write(&g_p, 0xE0, 0x00);                        /* timer 3 prescaler */
    m50734_write(&g_p, 0xE1, 0x31);                        /* reload: 4*(0+1)*(0x31+1) = 200 cycles */
    m50734_tick(&g_p, 199);
    CHECK_EQ(g_p.icr2 & 0x20, 0);
    m50734_tick(&g_p, 1);
    CHECK_EQ(g_p.icr2 & 0x20, 0x20);                       /* request set even while disabled */
    CHECK_EQ(m50734_irq_lines(&g_p), 0);
    m50734_write(&g_p, M50734_ICR2, 0x30);                 /* enable + request */
    CHECK_EQ(m50734_irq_lines(&g_p), 1u << M50734_LINE_TIMER);
    m50734_write(&g_p, M50734_ICR2, 0x10);                 /* ISR clears the request (CLB5 $FE) */
    CHECK_EQ(m50734_irq_lines(&g_p), 0);
    m50734_tick(&g_p, 200);
    CHECK_EQ(g_p.icr2 & 0x20, 0x20);                       /* fires again one period later */
    m50734_write(&g_p, M50734_ICR2, 0xFF);
    CHECK_EQ(m50734_read(&g_p, M50734_ICR2), 0x3F);        /* ICR2 is 6 bits wide */
}

static void test_uart_rx_tx(void)
{
    m50734_reset(&g_p, k_in);
    CHECK(m50734_uart_receive(&g_p, 'A'));
    CHECK_EQ(m50734_irq_lines(&g_p), 0);                   /* RX interrupt not enabled */
    CHECK_EQ(m50734_read(&g_p, M50734_UART_STATUS), 0x03); /* RX full + TX ready */
    m50734_write(&g_p, M50734_UART_CTRL, 0xD1);            /* the ROM's value: bit 4 = RX int */
    CHECK_EQ(m50734_irq_lines(&g_p), 1u << M50734_LINE_INT1);
    CHECK(!m50734_uart_receive(&g_p, 'B'));                /* previous byte unread */
    CHECK_EQ(m50734_read(&g_p, M50734_UART_RX), 'A');
    CHECK_EQ(m50734_irq_lines(&g_p), 0);
    CHECK_EQ(m50734_read(&g_p, M50734_UART_STATUS), 0x02);
    m50734_write(&g_p, M50734_UART_TX, 0x55);
    CHECK_EQ(g_p.uart_tx_count, 1);
    CHECK_EQ(g_p.uart_tx_last, 0x55);
}

static void test_misc_registers(void)
{
    m50734_reset(&g_p, k_in);
    m50734_write(&g_p, M50734_ICR1, 0x03);                 /* _INT1 enable + request */
    CHECK_EQ(m50734_irq_lines(&g_p), 1u << M50734_LINE_INT1);
    CHECK(m50734_is_modelled(0xE5));
    CHECK(m50734_is_modelled(0xFF));
    CHECK(!m50734_is_modelled(0xDA));
    CHECK(!m50734_is_modelled(0xFC));
    m50734_write(&g_p, 0xDA, 0x12);
    CHECK_EQ(m50734_read(&g_p, 0xDA), 0x12);               /* unmodelled: plain storage */
    m50734_write(&g_p, M50734_AD_CTRL, 0x01);
    CHECK_EQ(m50734_read(&g_p, M50734_AD_CTRL), 0x05);     /* conversion completes at once */
    m50734_write(&g_p, M50734_P23_FUNC, 0xFF);
    CHECK_EQ(m50734_read(&g_p, M50734_P23_FUNC), 0xC7);
}

void suite_m50734(void)
{
    test_ports();
    test_timer3_period();
    test_uart_rx_tx();
    test_misc_registers();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_m50734)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test m50734"`
Expected: FAIL to compile: `cannot open include file 'machine/m50734.h'`.

- [ ] **Step 3: Implement**

`src/machine/m50734.h`:
```c
#ifndef M50734_H
#define M50734_H
/* Mitsubishi M50734 on-chip peripherals at zero page $DA-$FF.
   Register semantics follow MAME's m50734.cpp; the UART is inferred from the Apollo ROM. */
#include <stdbool.h>
#include <stdint.h>

/* IRQ lines as seen by cpu740 (vector = 0xFFFC - 2*line) */
enum {
    M50734_LINE_INT2PIN = 0,
    M50734_LINE_INT1 = 1,      /* _INT1 pin and UART receive: $FFFA */
    M50734_LINE_TIMER = 2,     /* timers 1-3: $FFF8 */
    M50734_LINE_STEP = 3,      /* stepper counters: $FFF6 */
    M50734_LINE_TX_CNTR = 4,   /* Timer X / CNTR: $FFF4 */
    M50734_LINE_COUNT = 5,
};

enum {
    M50734_SFR_FIRST = 0xDA,
    M50734_UART_TX = 0xE4, M50734_UART_RX = 0xE5, M50734_UART_CTRL = 0xE6, M50734_UART_STATUS = 0xE7,
    M50734_AD_CTRL = 0xE9, M50734_AD_DATA = 0xEA, M50734_P4 = 0xEB, M50734_P23_FUNC = 0xED,
    M50734_P3 = 0xEE, M50734_P3_DIR = 0xEF, M50734_P2 = 0xF0, M50734_P2_DIR = 0xF1,
    M50734_P1_LATCH = 0xF2, M50734_P1 = 0xF3, M50734_P1_DIR = 0xF4, M50734_P0_FUNC = 0xF5,
    M50734_P0 = 0xF6, M50734_P0_DIR = 0xF7, M50734_UART_BAUD = 0xFB,
    M50734_ICR3 = 0xFD, M50734_ICR2 = 0xFE, M50734_ICR1 = 0xFF,
};

typedef struct m50734 {
    uint8_t port_latch[4], port_dir[4];   /* P0..P3 */
    uint8_t port_in[5];                   /* board input levels for P0..P4 */
    uint8_t p0_func, p23_func, uart_baud, ad_ctrl;
    uint8_t icr1, icr2, icr3;             /* $FF, $FE, $FD: enable (even) / request (odd) bit pairs */
    uint8_t timer_pre[3], timer_reload[3];
    int32_t timer_count[3];               /* CPU cycles until the next expiry */
    bool timer_running[3];
    uint8_t uart_ctrl, uart_rx, uart_tx_last;
    bool uart_rx_full;
    uint32_t uart_tx_count;
    uint8_t other[0x100 - M50734_SFR_FIRST];   /* storage for registers that are not modelled */
} m50734;

void m50734_reset(m50734 *p, const uint8_t port_in[5]);
bool m50734_is_modelled(uint8_t addr);
uint8_t m50734_read(m50734 *p, uint8_t addr);
void m50734_write(m50734 *p, uint8_t addr, uint8_t v);
void m50734_tick(m50734 *p, int cpu_cycles);
uint32_t m50734_irq_lines(const m50734 *p);
uint8_t m50734_port_pins(const m50734 *p, int port);
bool m50734_uart_receive(m50734 *p, uint8_t byte);

#endif
```

`src/machine/m50734.c`:
```c
#include <string.h>
#include "machine/m50734.h"

/* 16 crystal clocks per prescaler step; one CPU cycle = 4 crystal clocks. */
static int32_t timer_period(const m50734 *p, int n)
{
    return 4 * (int32_t)(p->timer_pre[n] + 1) * (int32_t)(p->timer_reload[n] + 1);
}

void m50734_reset(m50734 *p, const uint8_t port_in[5])
{
    memset(p, 0, sizeof *p);
    memcpy(p->port_in, port_in, sizeof p->port_in);
    memset(p->timer_pre, 0xFF, sizeof p->timer_pre);
    memset(p->timer_reload, 0xFF, sizeof p->timer_reload);
}

bool m50734_is_modelled(uint8_t a)
{
    return (a >= 0xDC && a <= 0xE1) || (a >= 0xE4 && a <= 0xE7) || (a >= 0xE9 && a <= 0xEB) ||
           (a >= 0xED && a <= 0xF7) || a == 0xFB || a >= 0xFD;
}

uint8_t m50734_port_pins(const m50734 *p, int port)
{
    return (uint8_t)((p->port_in[port] & ~p->port_dir[port]) | (p->port_latch[port] & p->port_dir[port]));
}

/* Data/direction pairs: P3 $EE/$EF, P2 $F0/$F1, P1 $F3/$F4, P0 $F6/$F7. Returns -1 otherwise. */
static int port_of(uint8_t a, bool *is_dir)
{
    switch (a) {
    case M50734_P3: *is_dir = false; return 3;
    case M50734_P3_DIR: *is_dir = true; return 3;
    case M50734_P2: *is_dir = false; return 2;
    case M50734_P2_DIR: *is_dir = true; return 2;
    case M50734_P1: *is_dir = false; return 1;
    case M50734_P1_DIR: *is_dir = true; return 1;
    case M50734_P0: *is_dir = false; return 0;
    case M50734_P0_DIR: *is_dir = true; return 0;
    default: return -1;
    }
}

uint8_t m50734_read(m50734 *p, uint8_t a)
{
    bool is_dir;
    int port;
    if (a < M50734_SFR_FIRST)
        return 0;
    port = port_of(a, &is_dir);
    if (port >= 0)
        return is_dir ? p->port_dir[port] : m50734_port_pins(p, port);
    if (a >= 0xDC && a <= 0xE1)
        return (a & 1) ? p->timer_reload[(a - 0xDC) >> 1] : p->timer_pre[(a - 0xDC) >> 1];
    switch (a) {
    case M50734_UART_RX: p->uart_rx_full = false; return p->uart_rx;
    case M50734_UART_CTRL: return p->uart_ctrl;
    case M50734_UART_STATUS: return (uint8_t)(0x02 | (p->uart_rx_full ? 0x01 : 0x00));
    case M50734_AD_CTRL: return p->ad_ctrl;
    case M50734_AD_DATA: return 0x00;
    case M50734_P4: return (uint8_t)(p->port_in[4] & 0x0F);
    case M50734_P23_FUNC: return p->p23_func;
    case M50734_P1_LATCH: return p->port_in[1];
    case M50734_P0_FUNC: return p->p0_func;
    case M50734_UART_BAUD: return p->uart_baud;
    case M50734_ICR3: return p->icr3;
    case M50734_ICR2: return p->icr2;
    case M50734_ICR1: return p->icr1;
    default: return p->other[a - M50734_SFR_FIRST];
    }
}

void m50734_write(m50734 *p, uint8_t a, uint8_t v)
{
    bool is_dir;
    int port;
    if (a < M50734_SFR_FIRST)
        return;
    port = port_of(a, &is_dir);
    if (port >= 0) {
        if (is_dir)
            p->port_dir[port] = v;
        else
            p->port_latch[port] = v;
        return;
    }
    if (a >= 0xDC && a <= 0xE1) {
        int n = (a - 0xDC) >> 1;
        if (a & 1)
            p->timer_reload[n] = v;
        else
            p->timer_pre[n] = v;
        p->timer_running[n] = true;          /* a write (re)starts the timer with a full period */
        p->timer_count[n] = timer_period(p, n);
        return;
    }
    switch (a) {
    case M50734_UART_TX: p->uart_tx_last = v; p->uart_tx_count++; break;
    case M50734_UART_RX: case M50734_UART_STATUS: case M50734_AD_DATA:
    case M50734_P4: case M50734_P1_LATCH:
        break;                               /* read-only */
    case M50734_UART_CTRL: p->uart_ctrl = v; break;
    case M50734_AD_CTRL: p->ad_ctrl = (uint8_t)((v & 0x03) | 0x04); break;
    case M50734_P23_FUNC: p->p23_func = (uint8_t)(v & 0xC7); break;
    case M50734_P0_FUNC: p->p0_func = v; break;
    case M50734_UART_BAUD: p->uart_baud = v; break;
    case M50734_ICR3: p->icr3 = v; break;
    case M50734_ICR2: p->icr2 = (uint8_t)(v & 0x3F); break;
    case M50734_ICR1: p->icr1 = v; break;
    default: p->other[a - M50734_SFR_FIRST] = v; break;
    }
}

void m50734_tick(m50734 *p, int cpu_cycles)
{
    for (int n = 0; n < 3; n++) {
        if (!p->timer_running[n])
            continue;
        p->timer_count[n] -= cpu_cycles;
        while (p->timer_count[n] <= 0) {
            p->icr2 |= (uint8_t)(1u << (2 * n + 1));   /* timer n+1 request bit */
            p->timer_count[n] += timer_period(p, n);
        }
    }
}

uint32_t m50734_irq_lines(const m50734 *p)
{
    uint32_t lines = 0;
    if (p->icr1 & (p->icr1 >> 1) & 0x04)
        lines |= 1u << M50734_LINE_INT2PIN;
    if ((p->icr1 & (p->icr1 >> 1) & 0x01) || (p->uart_rx_full && (p->uart_ctrl & 0x10)))
        lines |= 1u << M50734_LINE_INT1;
    if (p->icr2 & (p->icr2 >> 1) & 0x15)
        lines |= 1u << M50734_LINE_TIMER;
    if (p->icr1 & (p->icr1 >> 1) & 0x50)
        lines |= 1u << M50734_LINE_STEP;
    if (p->icr3 & (p->icr3 >> 1) & 0x50)
        lines |= 1u << M50734_LINE_TX_CNTR;
    return lines;
}

bool m50734_uart_receive(m50734 *p, uint8_t byte)
{
    if (p->uart_rx_full)
        return false;
    p->uart_rx = byte;
    p->uart_rx_full = true;
    return true;
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test m50734"`
Expected: PASS: `suite_m50734  ok`.

- [ ] **Step 5: Commit**

```bash
git add src/machine/m50734.h src/machine/m50734.c tests/test_m50734.c tests/suites.h
git commit -m "Add M50734 peripheral model (ports, timers, UART, interrupt control)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Apollo 2 machine — memory map, banking, frame capture, serial feed, idle

**Files:**
- Create: `src/machine/machine.h`, `src/machine/machine.c`, `tests/test_machine.c`
- Modify: `tests/suites.h` (append `SUITE(suite_machine)`)

**Interfaces:**
- Consumes: `cpu740_*` (Tasks 3–5), `m50734_*` (Task 6).
- Produces (`src/machine/machine.h`):
  - Constants: `MACHINE_ROM_SIZE` (0x10000), `MACHINE_RAM_SIZE` (0x4000), `APOLLO_SWAP_PC` (0xB306), `APOLLO_IDLE_WIT_PC` (0xC602), `APOLLO_PRODUCER_PTR` (0x2C), `APOLLO_FRAME_BYTES` (20).
  - `typedef void (*machine_frame_fn)(void *user, const uint8_t frame[APOLLO_FRAME_BYTES]);`
  - `typedef struct { uint8_t port_in[5]; } machine_config;` and `void machine_default_config(machine_config *cfg);`
  - `machine_status`: `MACHINE_IDLE`, `MACHINE_FAULT`, `MACHINE_CYCLE_LIMIT`. `machine_fault`: `MFAULT_NONE`, `MFAULT_CPU`, `MFAULT_DME`.
  - `struct machine` with public fields `cpu`, `bus`, `sfr`, `ram[]`, `fault`, `frames_produced`, `port_writes`, `lower_window_reads`, `unmodelled[]` (`{addr, pc, write}`), `unmodelled_count`.
  - `bool machine_init(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg);`: `cfg == NULL` means the defaults
  - `void machine_set_outputs(machine *m, machine_frame_fn on_frame, void *user);`
  - `bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n);`
  - `machine_status machine_run(machine *m, uint64_t max_cycles);`
  - `uint8_t machine_peek(const machine *m, uint16_t addr);`: no side effects; SFRs read as 0
  - `void machine_dump(const machine *m, FILE *out);`

- [ ] **Step 1: Write the failing tests**

`tests/test_machine.c`:
```c
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "machine/machine.h"

static uint8_t g_rom[MACHINE_ROM_SIZE];
static machine g_m;
static uint8_t g_first_frame[APOLLO_FRAME_BYTES];
static int g_frame_calls;

static void put(uint16_t addr, const uint8_t *bytes, size_t n) { memcpy(g_rom + addr, bytes, n); }

static void set_vector(uint16_t at, uint16_t target)
{
    g_rom[at] = (uint8_t)target;
    g_rom[at + 1] = (uint8_t)(target >> 8);
}

static void boot(void) { CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL)); }
static uint8_t rd(uint16_t a) { return g_m.bus.read(&g_m, a); }
static void wr(uint16_t a, uint8_t v) { g_m.bus.write(&g_m, a, v); }

static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    (void)user;
    if (g_frame_calls++ == 0)
        memcpy(g_first_frame, frame, APOLLO_FRAME_BYTES);
}

static void test_memory_map(void)
{
    memset(g_rom, 0, sizeof g_rom);
    g_rom[0x0010] = 0x22; g_rom[0x4010] = 0x11; g_rom[0x9000] = 0x33;
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(g_m.cpu.pc, 0x9000);                  /* P2 pulls select the main ROM at reset */
    CHECK_EQ(rd(0x4010), 0x11);                    /* P2.7 = 0: upper half */
    wr(M50734_P2, 0xF0);
    wr(M50734_P2_DIR, 0xFD);                       /* drive bank $70 with bit 7 set */
    CHECK_EQ(rd(0x4010), 0x22);                    /* P2.7 = 1: file 0x0000-0x3FFF */
    CHECK_EQ(g_m.lower_window_reads, 1);
    CHECK_EQ(rd(0x9000), 0x33);                    /* main ROM still selected */
    wr(M50734_P2, 0x50);                           /* another bank: empty option socket */
    CHECK_EQ(rd(0x9000), 0xFF);
    wr(0x0100, 0xAB); wr(0x3FFF, 0xCD);
    CHECK_EQ(rd(0x0100), 0xAB);
    CHECK_EQ(rd(0x3FFF), 0xCD);
    wr(0x8000, 0x42);                              /* DSP data port */
    CHECK_EQ(g_m.port_writes, 1);
    CHECK_EQ(g_m.unmodelled_count, 0);
    wr(0x9000, 0x00);                              /* write into ROM space: logged */
    rd(0x00DA);                                    /* Timer X: not modelled, logged */
    rd(0x00DA);                                    /* each address/direction is logged once */
    CHECK_EQ(g_m.unmodelled_count, 2);
    CHECK_EQ(g_m.unmodelled[0].addr, 0x9000);
    CHECK(g_m.unmodelled[0].write);
    CHECK_EQ(g_m.unmodelled[1].addr, 0x00DA);
    CHECK(!g_m.unmodelled[1].write);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    wr(M50734_P0_DIR, 0x20);
    wr(M50734_P0_FUNC, 0x20);                      /* P05 as DME output: data-memory mode */
    CHECK_EQ(g_m.fault, MFAULT_DME);
}

static void test_frame_capture(void)
{
    static const uint8_t code[] = { 0x3C, 0x00, 0x2C,     /* $9000 LDM #$00,$2C */
                                    0x3C, 0x03, 0x2D,     /*       LDM #$03,$2D */
                                    0x4C, 0x06, 0xB3 };   /*       JMP $B306 */
    static const uint8_t loop[] = { 0x4C, 0x06, 0xB3 };   /* $B306 JMP $B306 */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    put(APOLLO_SWAP_PC, loop, sizeof loop);
    set_vector(0xFFFE, 0x9000);
    boot();
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        g_m.ram[0x0300 + i] = (uint8_t)(i + 1);
    g_frame_calls = 0;
    machine_set_outputs(&g_m, on_frame, NULL);
    CHECK_EQ(machine_run(&g_m, 100), MACHINE_CYCLE_LIMIT);
    CHECK(g_frame_calls >= 1);
    CHECK_EQ(g_m.frames_produced, (uint64_t)g_frame_calls);
    CHECK_EQ(g_first_frame[0], 1);
    CHECK_EQ(g_first_frame[19], 20);
}

static void test_serial_delivery_and_idle(void)
{
    static const uint8_t reset[] = { 0x3C, 0x10, 0xE6,    /* $9000 LDM #$10,$E6  UART RX interrupt on */
                                     0x58,                /*       CLI */
                                     0x4C, 0x02, 0xC6 };  /*       JMP $C602 */
    static const uint8_t idle[] = { 0xC2,                 /* $C602 WIT */
                                    0x4C, 0x02, 0xC6 };   /* $C603 JMP $C602 */
    static const uint8_t isr[] = { 0xA5, 0xE5,            /* $A000 LDA $E5 */
                                   0x9D, 0x00, 0x02,      /*       STA $0200,X */
                                   0xE8,                  /*       INX */
                                   0x40 };                /*       RTI */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, reset, sizeof reset);
    put(APOLLO_IDLE_WIT_PC, idle, sizeof idle);
    put(0xA000, isr, sizeof isr);
    set_vector(0xFFFE, 0x9000);
    set_vector(0xFFFA, 0xA000);
    boot();
    CHECK(machine_feed_serial(&g_m, (const uint8_t *)"HI", 2));
    CHECK_EQ(machine_run(&g_m, 100000), MACHINE_IDLE);
    CHECK_EQ(g_m.ram[0x0200], 'H');
    CHECK_EQ(g_m.ram[0x0201], 'I');
}

static void test_fault_and_dump(void)
{
    static const uint8_t code[] = { 0x89 };              /* illegal opcode */
    char text[4096];
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(machine_run(&g_m, 100), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_CPU);
    CHECK_EQ(g_m.cpu.fault, CPU740_FAULT_ILLEGAL);
    FILE *f = fopen("build/test_dump.txt", "w+");
    CHECK(f != NULL);
    if (!f)
        return;
    machine_dump(&g_m, f);
    rewind(f);
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    CHECK(strstr(text, "illegal opcode") != NULL);
    CHECK(strstr(text, "9000  DB $89") != NULL);
}

static void test_cycle_limit(void)
{
    static const uint8_t code[] = { 0x4C, 0x00, 0x90 };   /* JMP $9000, no WIT */
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    boot();
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_CYCLE_LIMIT);
    CHECK(g_m.cpu.cycles >= 1000);
}

void suite_machine(void)
{
    test_memory_map();
    test_frame_capture();
    test_serial_delivery_and_idle();
    test_fault_and_dump();
    test_cycle_limit();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_machine)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test machine"`
Expected: FAIL to compile: `cannot open include file 'machine/machine.h'`.

- [ ] **Step 3: Implement**

`src/machine/machine.h`:
```c
#ifndef MACHINE_H
#define MACHINE_H
/* The Apollo 2 board: M50734 + 64 KB EPROM + RAM + DSP port (spec §1.2, §4). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cpu740/cpu740.h"
#include "machine/m50734.h"

#define MACHINE_ROM_SIZE        0x10000
#define MACHINE_RAM_SIZE        0x4000   /* $0000-$3FFF; $00DA-$00FF are SFRs instead */
#define MACHINE_HISTORY         64
#define MACHINE_MAX_UNMODELLED  64
#define MACHINE_QUEUE_SIZE      4096

/* Firmware landmarks in UK V2.01 4.04 (spec §1.3) */
#define APOLLO_SWAP_PC          0xB306   /* timer ISR, about to swap DSP frame buffers */
#define APOLLO_IDLE_WIT_PC      0xC602   /* the firmware's only WIT: idle, waiting for input */
#define APOLLO_PRODUCER_PTR     0x2C     /* zero-page pointer to the buffer the main loop fills */
#define APOLLO_FRAME_BYTES      20

typedef void (*machine_frame_fn)(void *user, const uint8_t frame[APOLLO_FRAME_BYTES]);

typedef struct { uint8_t port_in[5]; } machine_config;   /* P0..P4 board input levels */

typedef enum { MACHINE_IDLE, MACHINE_FAULT, MACHINE_CYCLE_LIMIT } machine_status;
typedef enum { MFAULT_NONE, MFAULT_CPU, MFAULT_DME } machine_fault;

typedef struct { uint16_t addr, pc; bool write; } machine_access;

typedef struct machine {
    cpu740 cpu;
    cpu740_bus bus;
    m50734 sfr;
    uint8_t rom[MACHINE_ROM_SIZE];
    uint8_t ram[MACHINE_RAM_SIZE];
    machine_fault fault;
    uint16_t op_pc;                        /* address of the instruction being executed */
    uint8_t queue[MACHINE_QUEUE_SIZE];     /* serial input ring buffer */
    size_t q_head, q_tail;
    bool busy_level_known;
    uint8_t busy_idle_level;               /* P0.5 level at the first idle = "not busy" */
    machine_frame_fn on_frame;
    void *user;
    uint64_t frames_produced;
    uint64_t port_writes;                  /* bytes written to the DSP port at $8000 */
    uint64_t lower_window_reads;           /* $4000-$7FFF reads served from file 0x0000-0x3FFF */
    uint16_t history[MACHINE_HISTORY];     /* PCs of the most recent instructions */
    unsigned history_pos;
    machine_access unmodelled[MACHINE_MAX_UNMODELLED];
    unsigned unmodelled_count;
} machine;

void machine_default_config(machine_config *cfg);
bool machine_init(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg);
void machine_set_outputs(machine *m, machine_frame_fn on_frame, void *user);
bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n);
machine_status machine_run(machine *m, uint64_t max_cycles);
uint8_t machine_peek(const machine *m, uint16_t addr);
void machine_dump(const machine *m, FILE *out);

#endif
```

`src/machine/machine.c`:
```c
#include <string.h>
#include "machine/machine.h"

#define P2_BANK_MASK  0x7C
#define P2_MAIN_BANK  0x70
#define P2_LOWER_HALF 0x80
#define P0_BUSY       0x20
#define ICR2_T3_ENABLE 0x10

void machine_default_config(machine_config *cfg)
{
    /* Switches off (serial mode); P2 pulled to $70 so the main ROM is selected at reset. */
    static const uint8_t k_default[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };
    memcpy(cfg->port_in, k_default, sizeof k_default);
}

static bool is_sfr(uint16_t a) { return a >= M50734_SFR_FIRST && a <= 0x00FF; }

static bool main_rom_selected(const machine *m)
{
    return (m50734_port_pins(&m->sfr, 2) & P2_BANK_MASK) == P2_MAIN_BANK;
}

static bool lower_half_selected(const machine *m)
{
    return (m50734_port_pins(&m->sfr, 2) & P2_LOWER_HALF) != 0;
}

static void note_unmodelled(machine *m, uint16_t addr, bool write)
{
    for (unsigned i = 0; i < m->unmodelled_count; i++)
        if (m->unmodelled[i].addr == addr && m->unmodelled[i].write == write)
            return;
    if (m->unmodelled_count < MACHINE_MAX_UNMODELLED) {
        machine_access *a = &m->unmodelled[m->unmodelled_count++];
        a->addr = addr;
        a->pc = m->op_pc;
        a->write = write;
    }
}

static uint8_t bus_read(void *ctx, uint16_t a)
{
    machine *m = ctx;
    if (is_sfr(a)) {
        if (!m50734_is_modelled((uint8_t)a))
            note_unmodelled(m, a, false);
        return m50734_read(&m->sfr, (uint8_t)a);
    }
    if (a < 0x4000)
        return m->ram[a];
    if (a < 0x8000) {
        if (lower_half_selected(m)) {
            m->lower_window_reads++;
            return m->rom[a - 0x4000];
        }
        return m->rom[a];
    }
    return main_rom_selected(m) ? m->rom[a] : 0xFF;
}

static void bus_write(void *ctx, uint16_t a, uint8_t v)
{
    machine *m = ctx;
    if (is_sfr(a)) {
        if (!m50734_is_modelled((uint8_t)a))
            note_unmodelled(m, a, true);
        m50734_write(&m->sfr, (uint8_t)a, v);
        if ((m->sfr.p0_func & 0x20) && (m->sfr.port_dir[0] & 0x20))
            m->fault = MFAULT_DME;   /* data-memory strobe: never used by the Apollo ROM */
        return;
    }
    if (a < 0x4000) {
        m->ram[a] = v;
        return;
    }
    if (a == 0x8000) {
        m->port_writes++;
        return;
    }
    note_unmodelled(m, a, true);
}

uint8_t machine_peek(const machine *m, uint16_t a)
{
    if (is_sfr(a))
        return 0;
    if (a < 0x4000)
        return m->ram[a];
    if (a < 0x8000)
        return lower_half_selected(m) ? m->rom[a - 0x4000] : m->rom[a];
    return main_rom_selected(m) ? m->rom[a] : 0xFF;
}

bool machine_init(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg)
{
    machine_config def;
    if (len != MACHINE_ROM_SIZE)
        return false;
    if (!cfg) {
        machine_default_config(&def);
        cfg = &def;
    }
    memset(m, 0, sizeof *m);
    memcpy(m->rom, rom, len);
    m50734_reset(&m->sfr, cfg->port_in);
    m->bus.ctx = m;
    m->bus.read = bus_read;
    m->bus.write = bus_write;
    cpu740_reset(&m->cpu, &m->bus);
    return true;
}

void machine_set_outputs(machine *m, machine_frame_fn on_frame, void *user)
{
    m->on_frame = on_frame;
    m->user = user;
}

bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        size_t next = (m->q_tail + 1) % MACHINE_QUEUE_SIZE;
        if (next == m->q_head)
            return false;
        m->queue[m->q_tail] = bytes[i];
        m->q_tail = next;
    }
    return true;
}

static bool at_idle(const machine *m)
{
    return m->cpu.waiting && m->cpu.pc == (uint16_t)(APOLLO_IDLE_WIT_PC + 1);
}

/* Spec §4: one byte per idle WIT, after the previous byte was read, while P0.5 is not busy. */
static void maybe_deliver(machine *m)
{
    if (!at_idle(m) || m->sfr.uart_rx_full || m->q_head == m->q_tail)
        return;
    uint8_t busy = (uint8_t)(m50734_port_pins(&m->sfr, 0) & P0_BUSY);
    if (!m->busy_level_known) {
        m->busy_idle_level = busy;
        m->busy_level_known = true;
    }
    if (busy != m->busy_idle_level)
        return;
    m50734_uart_receive(&m->sfr, m->queue[m->q_head]);
    m->q_head = (m->q_head + 1) % MACHINE_QUEUE_SIZE;
}

static void capture_frame(machine *m)
{
    uint8_t frame[APOLLO_FRAME_BYTES];
    uint16_t buf = (uint16_t)(m->ram[APOLLO_PRODUCER_PTR] | m->ram[APOLLO_PRODUCER_PTR + 1] << 8);
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        frame[i] = machine_peek(m, (uint16_t)(buf + i));
    m->frames_produced++;
    if (m->on_frame)
        m->on_frame(m->user, frame);
}

static void sync_irqs(machine *m)
{
    uint32_t lines = m50734_irq_lines(&m->sfr);
    for (int i = 0; i < M50734_LINE_COUNT; i++)
        cpu740_set_irq(&m->cpu, i, ((lines >> i) & 1) != 0);
}

machine_status machine_run(machine *m, uint64_t max_cycles)
{
    uint64_t end = m->cpu.cycles + max_cycles;
    while (m->cpu.cycles < end) {
        maybe_deliver(m);
        sync_irqs(m);
        if (at_idle(m) && m->q_head == m->q_tail && !m->sfr.uart_rx_full &&
            !(m->sfr.icr2 & ICR2_T3_ENABLE) && !m->cpu.irq_lines)
            return MACHINE_IDLE;
        if (m->cpu.pc == APOLLO_SWAP_PC && !m->cpu.waiting)
            capture_frame(m);
        m->op_pc = m->cpu.pc;
        m->history[m->history_pos++ % MACHINE_HISTORY] = m->cpu.pc;
        int cycles = cpu740_step(&m->cpu, &m->bus);
        if (cycles == 0) {
            m->fault = MFAULT_CPU;
            return MACHINE_FAULT;
        }
        m50734_tick(&m->sfr, cycles);
        if (m->fault != MFAULT_NONE)
            return MACHINE_FAULT;
    }
    return MACHINE_CYCLE_LIMIT;
}

static uint8_t peek_cb(void *ctx, uint16_t a) { return machine_peek(ctx, a); }

void machine_dump(const machine *m, FILE *out)
{
    static const char *const k_faults[] = { "none", "cpu", "DME (data-memory strobe) enabled" };
    static const char *const k_cpu_faults[] = {
        "ok", "illegal opcode", "unsupported instruction (SET/SED/STP/MUL/DIV/BRK)", "T flag set",
    };
    const cpu740 *c = &m->cpu;
    fprintf(out, "machine fault: %s", k_faults[m->fault]);
    if (m->fault == MFAULT_CPU)
        fprintf(out, " (%s, opcode $%02X at $%04X)", k_cpu_faults[c->fault], c->fault_opcode, c->fault_pc);
    fprintf(out, "\nA=%02X X=%02X Y=%02X S=%02X P=%02X PC=%04X cycles=%llu waiting=%d\n",
            c->a, c->x, c->y, c->s, c->p, c->pc, (unsigned long long)c->cycles, c->waiting ? 1 : 0);
    unsigned n = m->history_pos < MACHINE_HISTORY ? m->history_pos : MACHINE_HISTORY;
    fprintf(out, "last %u instructions:\n", n);
    for (unsigned i = 0; i < n; i++) {
        uint16_t pc = m->history[(m->history_pos - n + i) % MACHINE_HISTORY];
        char text[48];
        cpu740_disasm(pc, peek_cb, (void *)m, text, sizeof text);
        fprintf(out, "  %04X  %s\n", pc, text);
    }
    for (unsigned i = 0; i < m->unmodelled_count; i++)
        fprintf(out, "unmodelled %s $%04X (first at PC $%04X)\n",
                m->unmodelled[i].write ? "write" : "read", m->unmodelled[i].addr, m->unmodelled[i].pc);
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test machine"`
Expected: PASS: `suite_machine  ok`.

- [ ] **Step 5: Run the whole suite, then commit**

Run: `cmd //c "build.bat test"`
Expected: every suite `ok`, 0 failures.
```bash
git add src/machine/machine.h src/machine/machine.c tests/test_machine.c tests/suites.h
git commit -m "Add Apollo 2 machine model: memory map, banking, frame capture, serial feed

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Boot the real ROM

**Files:**
- Create: `tests/rom_fixture.h`, `tests/rom_fixture.c`, `tests/test_rom_boot.c`
- Modify: `tests/suites.h` (append `SUITE(suite_rom_boot)`)

**Interfaces:**
- Consumes: the `machine_*` API.
- Produces: `const uint8_t *test_rom(size_t *len);` returns the 64 KB ROM loaded from `APOLLO2_ROM` or `UK_V2.01_4.04_ROM.BIN`, or NULL if missing. It is loaded once and cached.

- [ ] **Step 1: Write the failing test**

`tests/rom_fixture.h`:
```c
#ifndef ROM_FIXTURE_H
#define ROM_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
/* Returns the 64 KB Apollo ROM (APOLLO2_ROM or ./UK_V2.01_4.04_ROM.BIN), or NULL if missing. */
const uint8_t *test_rom(size_t *len);
#endif
```

`tests/test_rom_boot.c`:
```c
#include <stdio.h>
#include "test.h"
#include "rom_fixture.h"
#include "machine/machine.h"

static machine g_m;

void suite_rom_boot(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(machine_init(&g_m, rom, len, NULL));
    machine_status st = machine_run(&g_m, 100000000ULL);   /* 50 s of emulated time */
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    for (unsigned i = 0; i < g_m.unmodelled_count; i++)
        fprintf(stderr, "  unmodelled %s $%04X at PC $%04X\n", g_m.unmodelled[i].write ? "write" : "read",
                g_m.unmodelled[i].addr, g_m.unmodelled[i].pc);
    CHECK_EQ(g_m.unmodelled_count, 0);
    printf("  boot: %llu cycles, %llu frames produced, %llu DSP port writes, %u serial bytes sent\n",
           (unsigned long long)g_m.cpu.cycles, (unsigned long long)g_m.frames_produced,
           (unsigned long long)g_m.port_writes, (unsigned)g_m.sfr.uart_tx_count);
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_rom_boot)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test rom_boot"`
Expected: FAIL to link: `unresolved external symbol test_rom`.

- [ ] **Step 3: Implement the fixture**

`tests/rom_fixture.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include "rom_fixture.h"

const uint8_t *test_rom(size_t *len)
{
    static uint8_t rom[0x10000];
    static int state;   /* 0 = not tried, 1 = loaded, -1 = missing or wrong size */
    if (state == 0) {
        const char *path = getenv("APOLLO2_ROM");
        FILE *f = fopen(path ? path : "UK_V2.01_4.04_ROM.BIN", "rb");
        state = -1;
        if (f) {
            if (fread(rom, 1, sizeof rom, f) == sizeof rom && fgetc(f) == EOF)
                state = 1;
            fclose(f);
        }
    }
    if (state != 1)
        return NULL;
    *len = sizeof rom;
    return rom;
}
```

- [ ] **Step 4: Run with the ROM present and confirm it passes**

Run: `cmd //c "build.bat test rom_boot"` (the ROM must be in the repo root)
Expected: PASS: `suite_rom_boot  ok`, followed by a `boot:` line. A startup announcement (if the firmware makes one) shows up as `frames produced` > 0. The run must end `MACHINE_IDLE` with no unmodelled accesses.

If it fails, the dump on stderr shows the fault and the last 64 instructions. Diagnose before changing anything:
- **An `unmodelled` line for an SFR:** add that register to `m50734.c`. Follow MAME's `m50734.cpp` semantics, or use plain storage plus `m50734_is_modelled` if the ROM only writes it at init. Add a unit test in `test_m50734.c` for it.
- **`MACHINE_CYCLE_LIMIT` with the PC in a polling loop:** find the register being polled in the dump. It's usually a peripheral status bit the model never sets.
- **A CPU fault:** check the faulting opcode against the spec's §1.1 opcode list.
- **`MACHINE_CYCLE_LIMIT` while the CPU sits in the `$C602` WIT with ICR2 bit 4 still set:**
  the firmware keeps the 10 kHz DSP tick running while idle, so the idle rule in
  spec §4 is wrong. Change `machine_run`'s idle condition to "in the `$C602` WIT,
  input drained, and no new produced frame for 20 frame periods (20 × 100 timer-3
  periods)". Add a `test_machine.c` case for it, and update spec §4 in the same commit.

Record any model change in the commit message.

- [ ] **Step 5: Commit**

```bash
git add tests/rom_fixture.h tests/rom_fixture.c tests/test_rom_boot.c tests/suites.h
git commit -m "Boot the real Apollo ROM to idle in the emulator

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: ROM engine and speech checks (including the lower-ROM bank)

**Files:**
- Create: `src/engine/rom_engine.h`, `src/engine/rom_engine.c`, `tests/test_rom_speak.c`
- Modify: `tests/suites.h` (append `SUITE(suite_rom_speak)`)

**Interfaces:**
- Consumes: the `machine_*` API and `test_rom()`.
- Produces (`src/engine/rom_engine.h`):
  - `typedef struct { uint8_t (*frames)[APOLLO_FRAME_BYTES]; size_t count, cap; } frame_list;`
  - `void frame_list_free(frame_list *fl);`
  - `char *frame_list_hex(const frame_list *fl);`: malloc'ed; one frame per line, 40 uppercase hex digits plus `\n`
  - `machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text, bool include_boot, frame_list *out);`: boots to idle, resets `frames_produced` and `lower_window_reads`, feeds `text` + `"\r"`, runs to idle
  - `ROM_ENGINE_BOOT_CYCLES` (100,000,000) and `ROM_ENGINE_SPEAK_CYCLES` (400,000,000)

- [ ] **Step 1: Write the failing tests**

`tests/test_rom_speak.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static machine g_m;

static void print_frames(const frame_list *fl)
{
    char *hex = frame_list_hex(fl);
    fputs(hex, stderr);
    free(hex);
}

/* "zoo" needs the K-Z rules, which live in file 0x0000-0x3FFF behind P2.7 (spec §1.2). */
static void test_zoo_uses_lower_rom_and_sounds_like_z(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    frame_list fl = { 0 };
    machine_status st = rom_engine_speak(&g_m, rom, len, "zoo", false, &fl);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    CHECK(fl.count > 10);
    CHECK(g_m.lower_window_reads > 0);
    /* Phoneme Z targets V (b9) = 36 and AHF (b8) = 56: some frame must be close to that */
    bool z_like = false;
    for (size_t i = 0; i < fl.count; i++)
        if (fl.frames[i][9] >= 28 && fl.frames[i][9] <= 44 && fl.frames[i][8] >= 45)
            z_like = true;
    CHECK(z_like);
    if (!z_like)
        print_frames(&fl);
    printf("  zoo: %zu frames, %llu lower-window reads\n", fl.count,
           (unsigned long long)g_m.lower_window_reads);
    frame_list_free(&fl);
}

static void test_deterministic(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    frame_list a = { 0 }, b = { 0 };
    CHECK_EQ(rom_engine_speak(&g_m, rom, len, "hello world", false, &a), MACHINE_IDLE);
    CHECK_EQ(rom_engine_speak(&g_m, rom, len, "hello world", false, &b), MACHINE_IDLE);
    CHECK(a.count > 0);
    CHECK_EQ(a.count, b.count);
    if (a.count == b.count)
        CHECK(memcmp(a.frames, b.frames, a.count * APOLLO_FRAME_BYTES) == 0);
    frame_list_free(&a);
    frame_list_free(&b);
}

static void test_hex_format(void)
{
    uint8_t frames[2][APOLLO_FRAME_BYTES] = { { 0x01, 0xAB }, { 0xFF } };
    frame_list fl;
    fl.frames = frames;
    fl.count = 2;
    fl.cap = 2;
    char *hex = frame_list_hex(&fl);
    CHECK_STR(hex, "01AB000000000000000000000000000000000000\n"
                   "FF00000000000000000000000000000000000000\n");
    free(hex);
}

void suite_rom_speak(void)
{
    test_hex_format();
    test_zoo_uses_lower_rom_and_sounds_like_z();
    test_deterministic();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_rom_speak)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test rom_speak"`
Expected: FAIL to compile: `cannot open include file 'engine/rom_engine.h'`.

- [ ] **Step 3: Implement the engine**

`src/engine/rom_engine.h`:
```c
#ifndef ROM_ENGINE_H
#define ROM_ENGINE_H
/* The "rom" engine: the original firmware running on the emulated machine (spec §3). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "machine/machine.h"

#define ROM_ENGINE_BOOT_CYCLES  100000000ULL   /* 50 s of emulated time at 2 MHz */
#define ROM_ENGINE_SPEAK_CYCLES 400000000ULL   /* 200 s */

typedef struct {
    uint8_t (*frames)[APOLLO_FRAME_BYTES];
    size_t count, cap;
} frame_list;

void frame_list_free(frame_list *fl);

/* One frame per line, 40 uppercase hex digits + '\n'. Caller frees. */
char *frame_list_hex(const frame_list *fl);

/* Boots m from rom to idle, then feeds text + CR and runs to idle again. Frames produced
   while speaking are appended to out (boot frames too if include_boot). The counters
   frames_produced / lower_window_reads cover the speaking phase only. m stays inspectable. */
machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text,
                                bool include_boot, frame_list *out);

#endif
```

`src/engine/rom_engine.c`:
```c
#include <stdlib.h>
#include <string.h>
#include "engine/rom_engine.h"

typedef struct {
    frame_list *list;
    bool enabled;
} frame_sink;

static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    frame_sink *s = user;
    frame_list *fl = s->list;
    if (!s->enabled)
        return;
    if (fl->count == fl->cap) {
        size_t cap = fl->cap ? fl->cap * 2 : 256;
        void *grown = realloc(fl->frames, cap * sizeof fl->frames[0]);
        if (!grown)
            abort();
        fl->frames = grown;
        fl->cap = cap;
    }
    memcpy(fl->frames[fl->count++], frame, APOLLO_FRAME_BYTES);
}

void frame_list_free(frame_list *fl)
{
    free(fl->frames);
    fl->frames = NULL;
    fl->count = fl->cap = 0;
}

char *frame_list_hex(const frame_list *fl)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char *s = malloc(fl->count * (APOLLO_FRAME_BYTES * 2 + 1) + 1);
    char *p = s;
    if (!s)
        abort();
    for (size_t i = 0; i < fl->count; i++) {
        for (int b = 0; b < APOLLO_FRAME_BYTES; b++) {
            *p++ = k_hex[fl->frames[i][b] >> 4];
            *p++ = k_hex[fl->frames[i][b] & 0x0F];
        }
        *p++ = '\n';
    }
    *p = '\0';
    return s;
}

machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text,
                                bool include_boot, frame_list *out)
{
    frame_sink sink;
    machine_status st;
    sink.list = out;
    sink.enabled = include_boot;
    if (!machine_init(m, rom, len, NULL))
        return MACHINE_FAULT;
    machine_set_outputs(m, on_frame, &sink);
    st = machine_run(m, ROM_ENGINE_BOOT_CYCLES);
    if (st == MACHINE_IDLE) {
        m->frames_produced = 0;
        m->lower_window_reads = 0;
        sink.enabled = true;
        if (machine_feed_serial(m, (const uint8_t *)text, strlen(text)) &&
            machine_feed_serial(m, (const uint8_t *)"\r", 1))
            st = machine_run(m, ROM_ENGINE_SPEAK_CYCLES);
        else
            st = MACHINE_FAULT;   /* text longer than the input queue */
    }
    machine_set_outputs(m, NULL, NULL);   /* sink lives on this stack frame */
    return st;
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c "build.bat test rom_speak"`
Expected: PASS: `suite_rom_speak  ok`, with a line like `zoo: N frames, M lower-window reads` where both N and M are > 0.

If `z_like` fails, the frames are printed to stderr. Before changing anything, check two things:
- Whether `lower_window_reads` is 0. If so, the P2.7 mapping isn't being exercised; check `lower_half_selected` against the `$F0`/`$F1` values in a dump.
- Whether b9/b8 ever approach 36/56. If they're close but outside the window, report the observed maxima rather than widening the bounds silently.

- [ ] **Step 5: Commit**

```bash
git add src/engine/rom_engine.h src/engine/rom_engine.c tests/test_rom_speak.c tests/suites.h
git commit -m "Add rom engine; verify lower-ROM banking and determinism on real speech

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Dev CLI and golden corpus

**Files:**
- Create: `src/apps/apollo2_frames.c`, `tests/test_golden.c`, `tests/corpus/hello.txt`, `tests/corpus/prose.txt`, `tests/corpus/numbers.txt`, `tests/corpus/punctuation.txt`, `tests/corpus/letters_k_to_z.txt`, `tests/corpus/abbreviations.txt`, `tests/golden/*.frm` (generated)
- Modify: `tests/suites.h` (append `SUITE(suite_golden)`), `README.md`

**Interfaces:**
- Consumes: `rom_engine_speak`, `frame_list_hex`, `frame_list_free`, `test_rom`, `machine_dump`.
- Produces:
  - `build\apollo2_frames.exe [--rom PATH] [--boot] TEXT`. Exit codes: 0 ok, 1 usage/ROM error, 2 emulation fault, 3 cycle limit.
  - Golden files `tests/golden/<name>.frm`: firmware output, one produced frame per line. They're regenerated with `APOLLO2_UPDATE_GOLDEN=1`.

- [ ] **Step 1: Write the corpus and the failing golden test**

Create each corpus file with exactly one line of text and no trailing spaces:
- `tests/corpus/hello.txt`: `Hello world.`
- `tests/corpus/prose.txt`: `The quick brown fox jumps over the lazy dog.`
- `tests/corpus/numbers.txt`: `1 2 3 42 1995 3.14`
- `tests/corpus/punctuation.txt`: `Wait, what? Yes! No; maybe: fine.`
- `tests/corpus/letters_k_to_z.txt`: `Zebra, yacht, quiz, vortex, kettle, music, night.`
- `tests/corpus/abbreviations.txt`: `Dr. Smith lives at 10 Downing St. on Jan. 5th.`

`tests/test_golden.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static const char *const k_corpus[] = {
    "hello", "prose", "numbers", "punctuation", "letters_k_to_z", "abbreviations",
};

static machine g_m;

/* Whole file as a NUL-terminated string (binary mode, so no CRLF translation); NULL if missing. */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)size + 1);
    size_t n = s ? fread(s, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!s)
        return NULL;
    s[n] = '\0';
    return s;
}

static bool write_file(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(s, 1, strlen(s), f) == strlen(s);
    return fclose(f) == 0 && ok;
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

static void report_diff(const char *name, const char *got, const char *want)
{
    size_t line = 1, i = 0;
    while (got[i] && got[i] == want[i]) {
        if (got[i] == '\n')
            line++;
        i++;
    }
    fprintf(stderr, "  %s: produced frames differ from golden at frame %zu\n", name, line);
}

void suite_golden(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    bool update = getenv("APOLLO2_UPDATE_GOLDEN") != NULL;

    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char txt_path[128], frm_path[128];
        snprintf(txt_path, sizeof txt_path, "tests/corpus/%s.txt", k_corpus[c]);
        snprintf(frm_path, sizeof frm_path, "tests/golden/%s.frm", k_corpus[c]);
        char *text = read_file(txt_path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);

        frame_list fl = { 0 };
        machine_status st = rom_engine_speak(&g_m, rom, len, text, false, &fl);
        if (st != MACHINE_IDLE)
            machine_dump(&g_m, stderr);
        CHECK_EQ(st, MACHINE_IDLE);
        CHECK(fl.count > 0);
        char *hex = frame_list_hex(&fl);

        if (update) {
            CHECK(write_file(frm_path, hex));
            printf("  updated %s (%zu frames)\n", frm_path, fl.count);
        } else {
            char *want = read_file(frm_path);
            if (!want) {
                fprintf(stderr, "  missing %s: run once with APOLLO2_UPDATE_GOLDEN=1 and review\n", frm_path);
                g_test_failures++;
            } else {
                if (strcmp(hex, want) != 0) {
                    report_diff(k_corpus[c], hex, want);
                    g_test_failures++;
                }
                free(want);
            }
        }
        free(hex);
        frame_list_free(&fl);
        free(text);
    }
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_golden)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c "build.bat test golden"`
Expected: FAIL. There's one `missing tests/golden/<name>.frm` line per corpus entry, and the exit code is 1.

- [ ] **Step 3: Generate and review the goldens, and add the CLI**

Run:
```bash
mkdir -p tests/golden
APOLLO2_UPDATE_GOLDEN=1 cmd //c "build.bat test golden"
```
Expected: six `updated tests/golden/<name>.frm (N frames)` lines, each with N > 0. Review them:
```bash
wc -l tests/golden/*.frm
head -3 tests/golden/hello.frm
```
Expected: every file has N lines of exactly 40 hex digits. Byte 18–19 of every frame (the last 4 hex digits) is `FFFF`, per the template in spec §1.4.

`src/apps/apollo2_frames.c`:
```c
/* Dev CLI: run TEXT through the original firmware (emulated) and print the produced
   DSP frames, one per line, as 40 hex digits (spec §4 "produced frames"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/rom_engine.h"

static machine g_machine;

static int usage(void)
{
    fprintf(stderr, "usage: apollo2_frames [--rom PATH] [--boot] TEXT\n"
                    "  --rom PATH  ROM image (default: $APOLLO2_ROM or UK_V2.01_4.04_ROM.BIN)\n"
                    "  --boot      also print frames produced during boot\n");
    return 1;
}

static bool load_rom(const char *path, uint8_t *rom)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fread(rom, 1, MACHINE_ROM_SIZE, f) == MACHINE_ROM_SIZE && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

int main(int argc, char **argv)
{
    static uint8_t rom[MACHINE_ROM_SIZE];
    const char *rom_path = getenv("APOLLO2_ROM");
    const char *text = NULL;
    bool boot = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            rom_path = argv[++i];
        else if (!strcmp(argv[i], "--boot"))
            boot = true;
        else if (!text)
            text = argv[i];
        else
            return usage();
    }
    if (!text)
        return usage();
    if (!rom_path)
        rom_path = "UK_V2.01_4.04_ROM.BIN";
    if (!load_rom(rom_path, rom)) {
        fprintf(stderr, "cannot load a %d-byte ROM from %s\n", MACHINE_ROM_SIZE, rom_path);
        return 1;
    }

    frame_list fl = { 0 };
    machine_status st = rom_engine_speak(&g_machine, rom, sizeof rom, text, boot, &fl);
    char *hex = frame_list_hex(&fl);
    fputs(hex, stdout);
    free(hex);
    frame_list_free(&fl);
    if (st == MACHINE_FAULT) {
        machine_dump(&g_machine, stderr);
        return 2;
    }
    if (st == MACHINE_CYCLE_LIMIT) {
        fprintf(stderr, "cycle limit reached before the firmware went idle\n");
        return 3;
    }
    return 0;
}
```

Append to `README.md`:
```markdown
## Frames from the original firmware

    build/apollo2_frames.exe "Hello world."      # one produced DSP frame per line (hex)
    build/apollo2_frames.exe --boot "Hi"          # include the startup announcement

Golden frame streams for the regression corpus live in `tests/golden/`. Regenerate
them only after reviewing an intentional emulator change:

    APOLLO2_UPDATE_GOLDEN=1 cmd //c "build.bat test golden"
```

- [ ] **Step 4: Run everything and the CLI**

Run: `cmd //c "build.bat test"`
Expected: PASS. All 10 suites `ok`, 0 failures (0 skips with the ROM present).

Run: `build/apollo2_frames.exe "Hello world." | tr -d '\r' | diff - tests/golden/hello.frm && echo SAME`
(`tr` strips the CRLF that Windows text-mode stdout adds.)
Expected: `SAME`.

- [ ] **Step 5: Commit**

```bash
git add src/apps/apollo2_frames.c tests/test_golden.c tests/corpus tests/golden tests/suites.h README.md
git commit -m "Add apollo2_frames CLI and golden produced-frame corpus

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Milestone 1 exit criteria

- `cmd //c "build.bat test"` passes all 10 suites with the ROM present.
- The real ROM boots to idle with zero unmodelled accesses (Task 8).
- Speaking "zoo" exercises the lower ROM half and produces /z/-like frames (Task 9).
- Golden produced-frame streams for six corpus texts are committed and reproducible (Task 10).

## Later milestones (separate plans, written when M1 is done)

- **M2 — Software Apollo 2 v0:**
  - raw DSP-port capture with re-alignment on the P2.0 reset
  - `synth/` (Holmes parallel formant, 10 kHz) with resonator and spectral tests
  - `tools/calibrate.py` against RP vowel formants and the Juno reference
  - an `apollo2` CLI that writes a 10 kHz WAV and plays live audio
  - corpus extended with the `@` commands from `docs/apollo2.txt`
- **M3 — Ghidra 740 module and project:**
  - SLEIGH extension of the 6502 spec, validated against `cpu740`
  - project setup with all known labels
  - the RAM jump tables (`$1306`, `$150B`, `JMP ($4A)`) resolved to full coverage
- **M4 — Progressive replacement:**
  - per-routine thunks keyed by ROM address and `rom_call()`
  - differential entry/exit-state tests, golden frames held byte-exact
- **M4 also:** each ROM table a ported routine uses is converted into committed, readable C under `src/firmware/data/`, with a byte-for-byte check against the ROM.
- **M5 — Native:** all routines and tables in C, with a `native` engine that doesn't use `cpu740`/`machine`. Done when `build.bat test` passes with no ROM file present. The emulator is kept as the oracle for regenerating goldens.
