# M4.0 Replacement Foundation and Letter-to-Sound Pilot — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Check the emulator oracle against an independent CPU reference, build the hybrid replacement infrastructure (hooks, ROM calls, a firmware context on the shared RAM image, converted readable tables), and port the nine letter-to-sound routines to C so that the `hybrid` engine reproduces every golden frame stream byte for byte.

**Architecture:**
- `src/firmware/` holds the decompiled C. It reads and writes the firmware's own 16 KB RAM image through named addresses. It reads ROM data only from committed, readable tables, which an encoder lays out at their original addresses.
- `src/machine/` gains hooks that run a C function at a ROM routine's entry, plus a way to call back into ROM.
- `src/engine/thunks.c` maps registers to C arguments for each ported routine.
- A live differential test compares every ported routine with its ROM original at real call sites. A hybrid golden test checks the end-to-end frames.

**Tech Stack:**
- C11, built with MSVC 2022 (`build.bat`, `/W4 /WX`).
- Python 3.13 for the one-time tools (`tools/harte_subset.py`, `tools/gen_fw_ram.py`, `tools/extract_tables.py`).
- Ghidra annotations (`ghidra740/annotations/*.txt`) as the naming source.

**Spec:** `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (M4.0 spec). Parent: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 6).

## Global Constraints

**Build and run**
- Build and test with `cmd //c ".\build.bat test [SUITE_FILTER]"` from the repo root. In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional). Fix warnings; never add `/wd` flags. The single exception is the `setjmp` pragma named in Task 7.
- Every `.c` file under `src/` and `tests/` must have a **unique basename**, because `build.bat` puts all objects in `build\`.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers.
- Audio stays 10 kHz native, with no resampling.

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. The converted tables are readable C (strings, flags, list references). The one annotated raw byte (`unused_9824`) is the only literal.
- The Harte vectors are committed as `tests/data/cpu6502_vectors.dat`. That is `.dat`, not `.bin`: `*.bin` is git-ignored.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/`. This plan only reads the annotations.

**Correctness rules**
- The goldens in `tests/golden/*.frm` stay unchanged. If a CPU fix in Task 1 changes any golden, stop and report it; do not regenerate.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header (spec §3.1). `build.bat` checks this from Task 4 on.
- The ported C keeps the ROM's behaviour exactly, quirks included: the unreachable "LOOP" rule, the suffix matcher's early fail on a space entry, and the stale `tn_pos` after a failed right context.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## File Map

| File | Task | Responsibility |
|---|---|---|
| `tools/harte_subset.py` | 1 | fetch and sample the 6502 vectors |
| `tests/data/cpu6502_vectors.dat`, `.LICENSE` | 1 | committed sample and its MIT notice |
| `tests/test_cpu_harte.c` | 1 | `suite_cpu_harte` |
| `src/engine/rom_id.h/.c`, `tests/test_rom_id.c` | 2 | MD5, the ROM identity check and CLI policy |
| `src/machine/machine.h/.c`, `tests/test_machine_hooks.c` | 3 | mapper, step, hooks, ROM call, IRQ hold, clone |
| `tools/gen_fw_ram.py`, `src/firmware/fw_ram.h` | 4 | RAM names from the annotations |
| `src/firmware/fw.h/.c`, `tests/test_fw.c` | 4 | firmware context: RAM, data image, host |
| `build.bat` | 4 | firmware boundary check, new source dirs |
| `tools/extract_tables.py` | 5 | one-time ROM → readable C converter |
| `src/firmware/data/fw_data.h`, `fw_data.c` | 5 | table types and the encoder (data image) |
| `src/firmware/data/tn_rule_data.c`, `lts_rule_data_aj.c`, `lts_rule_data_kz.c`, `lts_table_data.c` | 5 | converted tables (generated, then reviewed) |
| `tests/test_firmware_data.c` | 5 | `suite_firmware_data` |
| `src/firmware/tn_rules.h/.c`, `src/firmware/lts.h/.c`, `tests/test_fw_lts.c` | 6 | the nine ported routines |
| `src/engine/rom_engine.h/.c` | 7 | `rom_engine_run` with a setup callback |
| `src/engine/thunks.h/.c`, `tests/test_diff_lts.c` | 7 | replacement table, bridge, live differential test |
| `src/engine/hybrid_engine.h/.c`, `src/render/render.h/.c`, `src/apps/apollo2.c`, `tests/test_golden_hybrid.c` | 8 | hybrid engine, `--engine hybrid`, hybrid goldens |
| `docs/superpowers/specs/*.md`, `README.md` | 9 | spec rev 7, M4.0 status, README |

`tests/suites.h` gains one `SUITE(...)` line per new suite, appended at the end, in task order.

---

### Task 1: 6502 reference vectors (spec §2.1)

**Files:**
- Create: `tools/harte_subset.py`, `tests/data/cpu6502_vectors.dat` (generated), `tests/data/cpu6502_vectors.LICENSE`, `tests/test_cpu_harte.c`
- Modify: `tests/suites.h` (append `SUITE(suite_cpu_harte)`), `.gitattributes` (add `*.dat binary`)

**Interfaces:**
- Consumes: `cpu740_step`, `CPU740_*` flags (`src/cpu740/cpu740.h`), `tests/cpu_harness.h` (`g_mem`, `g_cpu`, `g_bus`).
- Produces: `suite_cpu_harte`. The file format is below, and no other task reads it.

The user approved this download (about 640 MB, MIT licence), once, into the git-ignored `build/harte/`.

- [ ] **Step 1: Write the converter**

`tools/harte_subset.py`:

```python
#!/usr/bin/env python3
"""One-time converter: Tom Harte's 6502 SingleStepTests (MIT) -> tests/data/cpu6502_vectors.dat.

usage: python tools/harte_subset.py [--fetch] [--per-op N]
  --fetch     download the documented-opcode files (about 640 MB) into build/harte/ first
  --per-op N  vectors kept per opcode (default 300)

Filters (M4.0 spec section 2.1), each a documented 740 difference:
  - BRK ($00) and SED ($F8) are excluded: cpu740 faults on them by design (spec section 3), the ROM never uses them.
  - ADC/SBC vectors whose initial P has D set are skipped: cpu740 faults there by design, the ROM only clears D.
  - PLP/RTI vectors that pull a status byte with bit 5 set are skipped: bit 5 is the 740's T flag, and
    cpu740 faults when PLP/RTI restore T = 1 (by design).
Record format (little-endian): magic b"HARTE740", u32 count, then per vector:
  u8 opcode, state initial, state final;  state = u16 pc, u8 s, a, x, y, p, u8 n, n * (u16 addr, u8 value)
"""
import json
import os
import struct
import sys
import urllib.request

URL = "https://raw.githubusercontent.com/SingleStepTests/65x02/main/6502/v1/{:02x}.json"
SRC = os.path.join("build", "harte")
OUT = os.path.join("tests", "data", "cpu6502_vectors.dat")

# The 151 documented NMOS 6502 opcodes.
DOCUMENTED = sorted({
    0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71,              # ADC
    0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31,              # AND
    0x0A, 0x06, 0x16, 0x0E, 0x1E,                                # ASL
    0x90, 0xB0, 0xF0, 0x30, 0xD0, 0x10, 0x50, 0x70,              # branches
    0x24, 0x2C, 0x00,                                            # BIT, BRK
    0x18, 0xD8, 0x58, 0xB8,                                      # CLC CLD CLI CLV
    0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1,              # CMP
    0xE0, 0xE4, 0xEC, 0xC0, 0xC4, 0xCC,                          # CPX CPY
    0xC6, 0xD6, 0xCE, 0xDE, 0xCA, 0x88,                          # DEC DEX DEY
    0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51,              # EOR
    0xE6, 0xF6, 0xEE, 0xFE, 0xE8, 0xC8,                          # INC INX INY
    0x4C, 0x6C, 0x20,                                            # JMP JSR
    0xA9, 0xA5, 0xB5, 0xAD, 0xBD, 0xB9, 0xA1, 0xB1,              # LDA
    0xA2, 0xA6, 0xB6, 0xAE, 0xBE, 0xA0, 0xA4, 0xB4, 0xAC, 0xBC,  # LDX LDY
    0x4A, 0x46, 0x56, 0x4E, 0x5E, 0xEA,                          # LSR NOP
    0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11,              # ORA
    0x48, 0x08, 0x68, 0x28,                                      # PHA PHP PLA PLP
    0x2A, 0x26, 0x36, 0x2E, 0x3E, 0x6A, 0x66, 0x76, 0x6E, 0x7E,  # ROL ROR
    0x40, 0x60,                                                  # RTI RTS
    0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1,              # SBC
    0x38, 0xF8, 0x78,                                            # SEC SED SEI
    0x85, 0x95, 0x8D, 0x9D, 0x99, 0x81, 0x91,                    # STA
    0x86, 0x96, 0x8E, 0x84, 0x94, 0x8C,                          # STX STY
    0xAA, 0xA8, 0xBA, 0x8A, 0x9A, 0x98,                          # TAX TAY TSX TXA TXS TYA
})
assert len(DOCUMENTED) == 151
EXCLUDED = {0x00: "BRK", 0xF8: "SED"}
ADC_SBC = {0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71, 0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1}


def keep(op, vec):
    ini = vec["initial"]
    if op in ADC_SBC and ini["p"] & 0x08:
        return False
    if op in (0x28, 0x40):   # PLP, RTI: the status byte comes from $0100 + S + 1
        ram = {a: v for a, v in ini["ram"]}
        if ram.get(0x100 + ((ini["s"] + 1) & 0xFF), 0) & 0x20:
            return False
    return True


def state(s):
    ram = s["ram"]
    if len(ram) > 255:
        raise ValueError("more than 255 RAM entries")
    out = struct.pack("<HBBBBBB", s["pc"], s["s"], s["a"], s["x"], s["y"], s["p"], len(ram))
    for addr, val in ram:
        out += struct.pack("<HB", addr, val)
    return out


def main():
    fetch = "--fetch" in sys.argv
    per_op = int(sys.argv[sys.argv.index("--per-op") + 1]) if "--per-op" in sys.argv else 300
    ops = [op for op in DOCUMENTED if op not in EXCLUDED]
    os.makedirs(SRC, exist_ok=True)
    records = []
    for op in ops:
        path = os.path.join(SRC, "{:02x}.json".format(op))
        if not os.path.exists(path):
            if not fetch:
                sys.exit("missing %s: run with --fetch" % path)
            print("fetching", path)
            urllib.request.urlretrieve(URL.format(op), path)
        with open(path, encoding="utf-8") as f:
            vectors = json.load(f)
        n = 0
        for vec in vectors:
            if keep(op, vec):
                records.append(struct.pack("B", op) + state(vec["initial"]) + state(vec["final"]))
                n += 1
                if n == per_op:
                    break
        if n < per_op:
            sys.exit("opcode %02X: only %d vectors pass the filters" % (op, n))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"HARTE740" + struct.pack("<I", len(records)) + b"".join(records))
    print("wrote %s: %d vectors, %d opcodes" % (OUT, len(records), len(ops)))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Fetch and convert**

Run: `python tools/harte_subset.py --fetch`
Expected: `wrote tests/data/cpu6502_vectors.dat: 44700 vectors, 149 opcodes`. The file is about 2 MB. The download is slow, so let it run.

- [ ] **Step 3: Add the licence notice and the binary attribute**

Create `tests/data/cpu6502_vectors.LICENSE`:

```text
tests/data/cpu6502_vectors.dat is a sample of the 6502 v1 tests from
https://github.com/SingleStepTests/65x02 (Tom Harte and contributors), converted by
tools/harte_subset.py. The source is distributed under the MIT License:

MIT License

Copyright (c) Thomas Harte and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Before committing, check the copyright line against the repo's actual LICENSE file: `curl -s https://raw.githubusercontent.com/SingleStepTests/65x02/main/LICENSE`. If the line differs, use the repo's exact wording.

Append to `.gitattributes`: `*.dat binary`

- [ ] **Step 4: Write the suite**

`tests/test_cpu_harte.c`:

```c
/* Tom Harte's 6502 SingleStepTests (MIT; tests/data/cpu6502_vectors.LICENSE): an independent check of
   cpu740 on the 6502 base set (M4.0 spec section 2.1). tools/harte_subset.py applies the filters; the masks
   here are the 740's documented differences from the 6502:
   - P bit 5 is the 740's T flag, not a constant 1: it is cleared in the initial state and not compared.
   - P bit 4 (B) is not stored by the 6502; cpu740 keeps it set. It is not compared in P.
   - The status byte PHP pushes is compared with bit 5 masked (T = 0 on the 740, 1 on the 6502).
   Cycle counts are not compared: cpu740's counts are approximate (spec section 3). */
#include <stdio.h>
#include <stdlib.h>
#include "test.h"
#include "cpu_harness.h"

typedef struct { uint16_t pc; uint8_t s, a, x, y, p, n; const uint8_t *ram; } hstate;

static const uint8_t *parse_state(const uint8_t *q, const uint8_t *end, hstate *st)
{
    if (end - q < 8)
        return NULL;
    st->pc = (uint16_t)(q[0] | q[1] << 8);
    st->s = q[2];
    st->a = q[3];
    st->x = q[4];
    st->y = q[5];
    st->p = q[6];
    st->n = q[7];
    q += 8;
    if (end - q < 3 * st->n)
        return NULL;
    st->ram = q;
    return q + 3 * st->n;
}

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0 ? malloc((size_t)size) : NULL;
    *len = data ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    return data;
}

void suite_cpu_harte(void)
{
    size_t len = 0;
    uint8_t *data = read_all("tests/data/cpu6502_vectors.dat", &len);
    CHECK(data != NULL);
    if (!data)
        return;
    CHECK(len >= 12 && memcmp(data, "HARTE740", 8) == 0);
    uint32_t count = (uint32_t)(data[8] | data[9] << 8 | data[10] << 16 | (uint32_t)data[11] << 24);
    const uint8_t *q = data + 12, *end = data + len;
    static unsigned runs[256], fails[256];
    memset(runs, 0, sizeof runs);
    memset(fails, 0, sizeof fails);
    unsigned reported = 0;
    for (uint32_t i = 0; i < count && q; i++) {
        hstate in, out;
        uint8_t op = *q++;
        q = parse_state(q, end, &in);
        if (q)
            q = parse_state(q, end, &out);
        CHECK(q != NULL);
        if (!q)
            break;
        memset(&g_mem, 0, sizeof g_mem);
        for (unsigned k = 0; k < in.n; k++)
            g_mem.mem[in.ram[3 * k] | in.ram[3 * k + 1] << 8] = in.ram[3 * k + 2];
        memset(&g_cpu, 0, sizeof g_cpu);
        g_cpu.pc = in.pc;
        g_cpu.s = in.s;
        g_cpu.a = in.a;
        g_cpu.x = in.x;
        g_cpu.y = in.y;
        g_cpu.p = (uint8_t)(in.p & ~CPU740_T);
        int cycles = cpu740_step(&g_cpu, &g_bus);
        bool ok = cycles > 0 && g_cpu.pc == out.pc && g_cpu.s == out.s && g_cpu.a == out.a &&
                  g_cpu.x == out.x && g_cpu.y == out.y &&
                  ((g_cpu.p ^ out.p) & ~(CPU740_T | CPU740_B)) == 0;
        for (unsigned k = 0; k < out.n; k++) {
            uint16_t addr = (uint16_t)(out.ram[3 * k] | out.ram[3 * k + 1] << 8);
            uint8_t mask = (op == 0x08 && addr == 0x100 + in.s) ? (uint8_t)~CPU740_T : 0xFF;
            if ((g_mem.mem[addr] ^ out.ram[3 * k + 2]) & mask)
                ok = false;
        }
        runs[op]++;
        if (!ok) {
            fails[op]++;
            if (reported++ < 20)
                fprintf(stderr, "  $%02X at %04X: got PC=%04X S=%02X A=%02X X=%02X Y=%02X P=%02X (%d cycles), "
                        "want PC=%04X S=%02X A=%02X X=%02X Y=%02X P=%02X\n", op, in.pc, g_cpu.pc, g_cpu.s,
                        g_cpu.a, g_cpu.x, g_cpu.y, g_cpu.p, cycles, out.pc, out.s, out.a, out.x, out.y, out.p);
        }
    }
    CHECK(q == end);
    unsigned ops = 0;
    for (int op = 0; op < 256; op++) {
        if (runs[op])
            ops++;
        if (fails[op]) {
            fprintf(stderr, "  opcode $%02X: %u of %u vectors fail\n", op, fails[op], runs[op]);
            g_test_failures++;
        }
    }
    printf("  %u vectors over %u opcodes\n", (unsigned)count, ops);
    CHECK_EQ(ops, 149);
    free(data);
}
```

Append `SUITE(suite_cpu_harte)` to `tests/suites.h`.

- [ ] **Step 5: Run the suite**

Run: `cmd //c ".\build.bat test cpu_harte"`
Expected: `suite_cpu_harte ok`, with 44700 vectors over 149 opcodes.

**If any opcode fails**, it is a finding, never a new filter by default. Resolve each failing opcode in one of two ways:
- **(a) A cpu740 bug.** Fix it in `src/cpu740/cpu740.c`, add a focused unit test to `tests/test_cpu_base.c`, and run the full suite (`cmd //c ".\build.bat test"`).
  - If `suite_golden` still passes, continue.
  - If any golden changes, **stop** and report BLOCKED with the opcode, the fix and the first differing golden frame. Goldens are regenerated only after the user reviews (spec §7.1).
- **(b) A genuine 740 difference.** Confirm it against MAME's `src/devices/cpu/m6502/m740.cpp` and its `m6502` base. Then write the rule in **both** the converter docstring (as a filter) and this suite's header comment (as a mask), with the source cited.

- [ ] **Step 6: Commit**

```bash
git add tools/harte_subset.py tests/data/cpu6502_vectors.dat tests/data/cpu6502_vectors.LICENSE tests/test_cpu_harte.c tests/suites.h .gitattributes
git commit -m "Harte 6502 vectors: independent check of cpu740 on the 6502 base set

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: ROM identity check (spec §2.2)

**Files:**
- Create: `src/engine/rom_id.h`, `src/engine/rom_id.c`, `tests/test_rom_id.c`
- Modify: `tests/rom_fixture.c`, `src/apps/apollo2.c`, `src/apps/apollo2_frames.c`, `src/apps/apollo2_coverage.c`, `tests/suites.h` (append `SUITE(suite_rom_id)`)

**Interfaces:**
- Produces: `rom_id_md5`, `rom_id_md5_hex`, `rom_id_is_apollo2`, `rom_id_accept` (below), and the `--rom-any` flag on all three CLIs.

- [ ] **Step 1: Write the failing test**

`tests/test_rom_id.c`:

```c
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_id.h"

static void md5_is(const char *msg, const char *want)
{
    char hex[33];
    rom_id_md5_hex((const uint8_t *)msg, strlen(msg), hex);
    CHECK_STR(hex, want);
}

void suite_rom_id(void)
{
    md5_is("", "d41d8cd98f00b204e9800998ecf8427e");
    md5_is("abc", "900150983cd24fb0d6963f7d28e17f72");
    md5_is("The quick brown fox jumps over the lazy dog", "9e107d9d372bb6826bd81d3542a419d6");
    md5_is("12345678901234567890123456789012345678901234567890123456789012345678901234567890",
           "57edf4a22be3c955ac49da2e2107b67a");   /* 80 bytes: two blocks */

    static uint8_t zeros[0x10000];
    CHECK(!rom_id_is_apollo2(zeros, sizeof zeros));
    CHECK(!rom_id_is_apollo2(zeros, 100));
    CHECK(!rom_id_accept("zeros", zeros, sizeof zeros, false, stderr));
    CHECK(rom_id_accept("zeros", zeros, sizeof zeros, true, stderr));   /* --rom-any: a warning only */

    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(rom_id_is_apollo2(rom, len));
}
```

Append `SUITE(suite_rom_id)` to `tests/suites.h`.

Run: `cmd //c ".\build.bat test rom_id"`
Expected: a build failure, because `engine/rom_id.h` does not exist.

- [ ] **Step 2: Implement `rom_id`**

`src/engine/rom_id.h`:

```c
#ifndef ROM_ID_H
#define ROM_ID_H
/* Identity of the ROM image (spec section 1: UK V2.01 4.04, MD5 below; M4.0 spec section 2.2). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ROM_ID_MD5 "74460801a399983b1a929f57e5ab57d7"

void rom_id_md5(const uint8_t *data, size_t len, uint8_t out[16]);
void rom_id_md5_hex(const uint8_t *data, size_t len, char out[33]);
/* True when rom is the 64 KB UK V2.01 4.04 image the goldens were recorded from. */
bool rom_id_is_apollo2(const uint8_t *rom, size_t len);
/* CLI policy: true for that image; for any other image prints a warning to err and returns true when any
   (--rom-any) is set, else prints the reason to err and returns false. */
bool rom_id_accept(const char *path, const uint8_t *rom, size_t len, bool any, FILE *err);

#endif
```

`src/engine/rom_id.c`:

```c
#include <math.h>
#include <string.h>
#include "engine/rom_id.h"

/* MD5 (RFC 1321). The sine table is computed: K[i] = floor(|sin(i + 1)| * 2^32). */
void rom_id_md5(const uint8_t *msg, size_t len, uint8_t out[16])
{
    static const uint8_t k_shift[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };
    uint32_t k[64];
    for (int i = 0; i < 64; i++)
        k[i] = (uint32_t)(fabs(sin(i + 1.0)) * 4294967296.0);
    uint32_t h[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    uint64_t bits = (uint64_t)len * 8;
    size_t total = ((len + 8) / 64 + 1) * 64;   /* message, $80, zeros, 8-byte length */
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++) {
            uint32_t word = 0;
            for (int j = 3; j >= 0; j--) {
                size_t p = off + (size_t)(4 * i + j);
                uint8_t b = p < len ? msg[p] : p == len ? 0x80
                          : p >= total - 8 ? (uint8_t)(bits >> (8 * (p - (total - 8)))) : 0;
                word = word << 8 | b;
            }
            w[i] = word;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            uint32_t x = a + f + k[i] + w[g];
            a = d;
            d = c;
            c = b;
            b = b + (x << k_shift[i] | x >> (32 - k_shift[i]));
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    for (int i = 0; i < 16; i++)
        out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
}

void rom_id_md5_hex(const uint8_t *data, size_t len, char out[33])
{
    static const char k_hex[] = "0123456789abcdef";
    uint8_t d[16];
    rom_id_md5(data, len, d);
    for (int i = 0; i < 16; i++) {
        out[2 * i] = k_hex[d[i] >> 4];
        out[2 * i + 1] = k_hex[d[i] & 15];
    }
    out[32] = '\0';
}

bool rom_id_is_apollo2(const uint8_t *rom, size_t len)
{
    char hex[33];
    if (len != 0x10000)
        return false;
    rom_id_md5_hex(rom, len, hex);
    return strcmp(hex, ROM_ID_MD5) == 0;
}

bool rom_id_accept(const char *path, const uint8_t *rom, size_t len, bool any, FILE *err)
{
    char hex[33];
    if (rom_id_is_apollo2(rom, len))
        return true;
    rom_id_md5_hex(rom, len, hex);
    if (any) {
        fprintf(err, "warning: %s (MD5 %s) is not the UK V2.01 4.04 ROM; frames may differ from the goldens\n",
                path, hex);
        return true;
    }
    fprintf(err, "%s has MD5 %s, not the UK V2.01 4.04 ROM (%s); pass --rom-any to use it anyway\n",
            path, hex, ROM_ID_MD5);
    return false;
}
```

- [ ] **Step 3: Run the test**

Run: `cmd //c ".\build.bat test rom_id"`
Expected: `suite_rom_id ok`.

- [ ] **Step 4: Gate the test fixture on the MD5**

In `tests/rom_fixture.c`, add `#include "engine/rom_id.h"` and replace the whole `test_rom` function with:

```c
const uint8_t *test_rom(size_t *len)
{
    static uint8_t rom[0x10000];
    static int state;   /* 0 = not tried, 1 = loaded, -1 = missing, wrong size or not the UK V2.01 4.04 image */
    if (state == 0) {
        const char *path = getenv("APOLLO2_ROM");
        FILE *f = fopen(path ? path : "UK_V2.01_4.04_ROM.BIN", "rb");
        state = -1;
        if (f) {
            if (fread(rom, 1, sizeof rom, f) == sizeof rom && fgetc(f) == EOF)
                state = 1;
            fclose(f);
        }
        if (state == 1 && !rom_id_is_apollo2(rom, sizeof rom)) {
            char hex[33];
            rom_id_md5_hex(rom, sizeof rom, hex);
            fprintf(stderr, "  ROM %s has MD5 %s, not the UK V2.01 4.04 image (%s): ROM tests are skipped\n",
                    path ? path : "UK_V2.01_4.04_ROM.BIN", hex, ROM_ID_MD5);
            state = -1;
        }
    }
    if (state != 1)
        return NULL;
    *len = sizeof rom;
    return rom;
}
```

- [ ] **Step 5: Add `--rom-any` to the three CLIs**

In each of `src/apps/apollo2.c`, `src/apps/apollo2_frames.c` and `src/apps/apollo2_coverage.c`:
1. add `#include "engine/rom_id.h"` and a `bool rom_any = false;` local in `main`;
2. accept `--rom-any` in the argument loop, beside `--rom`:
   - in `apollo2.c`: `} else if (!strcmp(a, "--rom-any")) { rom_any = true; }`
   - in the other two: `else if (!strcmp(argv[i], "--rom-any")) rom_any = true;`, placed before their generic `-` or text branches;
3. directly after the successful `load_rom(...)` check, add:

```c
    if (!rom_id_accept(rom_path, rom, sizeof rom, rom_any, stderr))
        return 1;
```

`apollo2_coverage.c` keeps its ROM in `g_rom`, so it uses `g_rom, sizeof g_rom` instead.

4. add `"  --rom-any          accept a ROM whose MD5 is not the UK V2.01 4.04 image (with a warning)\n"` to each usage text, after the `--rom` line.

- [ ] **Step 6: Run everything**

Run: `cmd //c ".\build.bat test"`
Expected: every suite ok, goldens unchanged. Then check the refusal on a wrong image:
```bash
head -c 65536 /dev/zero > build/zero.rom
./build/apollo2_frames.exe --rom build/zero.rom hello; echo "exit $?"
```
Expected: the MD5 message and `exit 1`.

- [ ] **Step 7: Commit**

```bash
git add src/engine/rom_id.h src/engine/rom_id.c tests/test_rom_id.c tests/rom_fixture.c tests/suites.h src/apps/apollo2.c src/apps/apollo2_frames.c src/apps/apollo2_coverage.c
git commit -m "ROM identity: MD5 check in the test fixture and the CLIs (--rom-any)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Machine: shared mapper, step, hooks, ROM call, IRQ hold, clone (spec §2.2, §3.2, §3.3)

**Files:**
- Modify: `src/machine/machine.h`, `src/machine/machine.c`, `tests/suites.h` (append `SUITE(suite_machine_hooks)`)
- Create: `tests/test_machine_hooks.c`

**Interfaces:**
- Produces, all in `machine.h` and used by Tasks 7–8:

```c
struct machine;
typedef bool (*machine_hook_fn)(struct machine *m, void *user);
typedef struct { uint16_t addr; machine_hook_fn fn; void *user; } machine_hook_entry;
typedef struct { uint8_t a, x, y, p; } machine_regs;
#define MACHINE_MAX_HOOKS          32
#define MACHINE_ROM_CALL_SENTINEL  0x0000
/* machine_fault gains MFAULT_FIRMWARE (appended last). machine gains:
   machine_hook_entry hooks[MACHINE_MAX_HOOKS]; unsigned hook_count; uint8_t hook_map[0x2000];
   bool irq_hold; uint16_t fault_addr; */
bool machine_hook(machine *m, uint16_t addr, machine_hook_fn fn, void *user);
void machine_clear_hooks(machine *m);
void machine_return(machine *m);
bool machine_step(machine *m);
void machine_hold_irqs(machine *m, bool hold);
bool machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles);
bool machine_finish_routine(machine *m, uint64_t max_cycles);
void machine_clone(machine *dst, const machine *src);
```

- [ ] **Step 1: Write the failing tests**

`tests/test_machine_hooks.c`:

```c
/* Hooks, ROM calls, the interrupt hold and machine_clone (M4.0 spec sections 3.2, 3.3, 5.1), on small
   programs in a synthetic ROM; one test calls a real ROM routine. */
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "machine/machine.h"

static uint8_t g_rom[MACHINE_ROM_SIZE];
static machine g_m, g_c;
static int g_calls;

static void put(uint16_t at, const uint8_t *b, size_t n) { memcpy(g_rom + at, b, n); }
static void vector(uint16_t at, uint16_t to) { g_rom[at] = (uint8_t)to; g_rom[at + 1] = (uint8_t)(to >> 8); }

/* Steps until the PC reaches stop (at most 200 steps). */
static void run_to(machine *m, uint16_t stop)
{
    for (int i = 0; i < 200 && m->cpu.pc != stop; i++)
        CHECK(machine_step(m));
    CHECK_EQ(m->cpu.pc, stop);
}

/* $9000: LDA #$05 / JSR $C100 / STA $10 / JMP $9007;  $C100: ASL A / ASL A / RTS  (A = $14) */
static void load_main(uint16_t callee)
{
    static const uint8_t main_code[] = { 0xA9, 0x05, 0x20, 0x00, 0xC1, 0x85, 0x10, 0x4C, 0x07, 0x90 };
    static const uint8_t shl2[] = { 0x0A, 0x0A, 0x60 };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, main_code, sizeof main_code);
    g_rom[0x9003] = (uint8_t)callee;
    g_rom[0x9004] = (uint8_t)(callee >> 8);
    put(0xC100, shl2, sizeof shl2);
    vector(0xFFFE, 0x9000);
    g_calls = 0;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
}

static bool hook_replace(machine *m, void *user)
{
    (void)user;
    g_calls++;
    m->cpu.a = 0x77;
    machine_return(m);
    return true;
}

static bool hook_observe(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_calls++;
    return false;
}

static void test_hook_replaces_routine(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_replace, NULL));
    CHECK(!machine_hook(&g_m, 0xC100, hook_replace, NULL));   /* already hooked */
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x77);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_m.cpu.s, 0xFF);   /* the emulated RTS popped the return address */
}

static void test_observer_lets_rom_run(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_observe, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x14);
    CHECK_EQ(g_calls, 1);
    machine_clear_hooks(&g_m);
    CHECK_EQ(g_m.hook_count, 0);
}

/* A hook fires only where the address maps to the main ROM file: not in the lower-ROM window. */
static void test_hook_only_in_main_rom(uint8_t p2_latch, uint8_t want, int want_calls)
{
    static const uint8_t code[] = {
        0x3C, 0x00, 0xF0,         /* LDM #p2,$F0  (patched below) */
        0x3C, 0xFF, 0xF1,         /* LDM #$FF,$F1: P2 all outputs */
        0xA9, 0x05,               /* LDA #$05 */
        0x20, 0x00, 0x41,         /* JSR $4100 */
        0x85, 0x10,               /* STA $10 */
        0x4C, 0x0D, 0x90,         /* JMP $900D */
    };
    static const uint8_t shl2[] = { 0x0A, 0x0A, 0x60 }, shl1[] = { 0x0A, 0x60 };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    g_rom[0x9001] = p2_latch;
    put(0x0100, shl2, sizeof shl2);   /* $4100 with the window on (file 0x0100) */
    put(0x4100, shl1, sizeof shl1);   /* $4100 with the window off */
    vector(0xFFFE, 0x9000);
    g_calls = 0;
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(machine_hook(&g_m, 0x4100, hook_replace, NULL));
    run_to(&g_m, 0x900D);
    CHECK_EQ(g_m.ram[0x10], want);
    CHECK_EQ(g_calls, want_calls);
}

static bool hook_calls_rom(machine *m, void *user)
{
    (void)user;
    machine_regs r = { m->cpu.a, 0, 0, m->cpu.p };
    CHECK(machine_rom_call(m, 0xC100, &r, 1000));
    m->cpu.a = (uint8_t)(r.a + 1);
    machine_return(m);
    return true;
}

static bool hook_fault(machine *m, void *user)
{
    (void)user;
    m->fault = MFAULT_FIRMWARE;
    m->fault_addr = 0x1234;
    return true;
}

static void test_rom_call(void)
{
    load_main(0xC100);
    run_to(&g_m, 0x9007);
    uint8_t s = g_m.cpu.s;
    machine_regs r = { 3, 0, 0, g_m.cpu.p };
    CHECK(machine_rom_call(&g_m, 0xC100, &r, 1000));
    CHECK_EQ(r.a, 12);
    CHECK_EQ(g_m.cpu.pc, 0x9007);
    CHECK_EQ(g_m.cpu.s, s);

    load_main(0xC200);   /* a hook at $C200 calls the ROM routine at $C100 */
    CHECK(machine_hook(&g_m, 0xC200, hook_calls_rom, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_m.ram[0x10], 0x15);

    static const uint8_t spin[] = { 0x4C, 0x00, 0xC3 };   /* $C300: JMP $C300 */
    load_main(0xC100);
    put(0xC300, spin, sizeof spin);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    machine_regs r2 = { 0, 0, 0, g_m.cpu.p };
    CHECK(!machine_rom_call(&g_m, 0xC300, &r2, 1000));   /* never returns: the budget ends it */

    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_fault, NULL));
    CHECK_EQ(machine_run(&g_m, 10000), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
}

static bool hook_finish(machine *m, void *user)
{
    (void)user;
    machine_clone(&g_c, m);
    CHECK(machine_finish_routine(&g_c, 1000));
    g_calls++;
    return false;
}

static void test_clone_and_finish(void)
{
    load_main(0xC100);
    CHECK(machine_hook(&g_m, 0xC100, hook_finish, NULL));
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_c.cpu.pc, 0x9005);      /* returned to the STA after the JSR */
    CHECK_EQ(g_c.cpu.a, 0x14);
    CHECK_EQ(g_c.hook_count, 0);        /* a clone has no hooks */
    g_c.bus.write(g_c.bus.ctx, 0x20, 0x99);
    CHECK_EQ(g_c.ram[0x20], 0x99);
    CHECK_EQ(g_m.ram[0x20], 0x00);      /* the clone's bus writes the clone */
}

/* $9000: CLI / NOP / NOP / JMP $9003; timer IRQ vector -> $A000: JMP $A000 */
static void test_irq_hold(bool hold)
{
    static const uint8_t code[] = { 0x58, 0xEA, 0xEA, 0x4C, 0x03, 0x90 };
    static const uint8_t isr[] = { 0x4C, 0x00, 0xA0 };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    put(0xA000, isr, sizeof isr);
    vector(0xFFFE, 0x9000);
    vector(0xFFF8, 0xA000);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.sfr.icr2 = 0x30;   /* timer 3 interrupt enabled and requested */
    machine_hold_irqs(&g_m, hold);
    CHECK(machine_step(&g_m));   /* CLI */
    CHECK(machine_step(&g_m));   /* held: NOP; not held: the interrupt is taken */
    CHECK_EQ(g_m.cpu.pc, hold ? 0x9002 : 0xA000);
}

static void test_rom_call_real_rom(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(machine_init(&g_m, rom, len, NULL));
    machine_regs r = { 0x1F, 0, 0, g_m.cpu.p };
    CHECK(machine_rom_call(&g_m, 0xC656, &r, 1000));   /* util_shl4: A = A * 16 */
    CHECK_EQ(r.a, 0xF0);
    CHECK(r.p & 0x01);                                   /* the last shift carried */
}

void suite_machine_hooks(void)
{
    test_hook_replaces_routine();
    test_observer_lets_rom_run();
    test_hook_only_in_main_rom(0x70, 0x77, 1);   /* window off: $4100 is main ROM, the hook fires */
    test_hook_only_in_main_rom(0xF0, 0x14, 0);   /* window on: $4100 is file 0x0100, no hook */
    test_rom_call();
    test_clone_and_finish();
    test_irq_hold(true);
    test_irq_hold(false);
    test_rom_call_real_rom();
}
```

Append `SUITE(suite_machine_hooks)` to `tests/suites.h`.

Run: `cmd //c ".\build.bat test machine_hooks"`
Expected: a build failure, because `machine_hook` and the rest are undeclared.

- [ ] **Step 2: Extend `machine.h`**

Before the `typedef struct machine {`, add:

```c
#define MACHINE_MAX_HOOKS          32
#define MACHINE_ROM_CALL_SENTINEL  0x0000   /* machine_rom_call's return address: the ROM never runs code at $0000 */

struct machine;
/* A C replacement or observer for the ROM routine at a hooked address (M4.0 spec section 3.2). It returns
   true when it performed the routine (leaving the machine as the routine's RTS would, e.g. through
   machine_return), false to let the ROM routine run. Setting m->fault stops the run. */
typedef bool (*machine_hook_fn)(struct machine *m, void *user);
typedef struct { uint16_t addr; machine_hook_fn fn; void *user; } machine_hook_entry;
typedef struct { uint8_t a, x, y, p; } machine_regs;
```

Append `MFAULT_FIRMWARE` to the end of the `machine_fault` enum, after `MFAULT_INPUT_OVERFLOW`. Add these fields at the end of `struct machine`:

```c
    machine_hook_entry hooks[MACHINE_MAX_HOOKS];
    unsigned hook_count;
    uint8_t hook_map[0x10000 / 8];         /* bit set = hooked address (fast test before the table) */
    bool irq_hold;                         /* no IRQ line reaches the CPU (machine_hold_irqs) */
    uint16_t fault_addr;                   /* MFAULT_FIRMWARE: the address the C code could not read or call */
```

Add these declarations after `machine_run`:

```c
/* One instruction, interrupt entry or hooked routine, with serial delivery, IRQ sync and frame capture as in
   machine_run. Returns false on a fault (m->fault says which). */
bool machine_step(machine *m);
/* Registers fn for the routine at addr. Before each instruction, when the PC is a hooked address that maps to
   the main ROM file under the current P2, the CPU is not waiting and no interrupt is about to be taken, fn
   runs. False when the table is full or addr is already hooked. */
bool machine_hook(machine *m, uint16_t addr, machine_hook_fn fn, void *user);
void machine_clear_hooks(machine *m);
/* Pops a return address and continues after it, like RTS. */
void machine_return(machine *m);
/* While hold is set, no IRQ line reaches the CPU; requests stay pending in the SFRs. */
void machine_hold_irqs(machine *m, bool hold);
/* Calls the routine at addr with A/X/Y/P from regs, as if by JSR from MACHINE_ROM_CALL_SENTINEL, and steps
   until it returns there. Hooks, interrupts (unless held) and frame capture work normally. On success regs
   holds the exit registers, the PC is restored and the other registers keep their exit values. False on a
   fault or when max_cycles pass first. */
bool machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles);
/* Runs the routine the PC has just entered (by JSR) until the stack pointer rises above its value now, i.e.
   the routine returned, also two levels up. False on a fault or when max_cycles pass first. */
bool machine_finish_routine(machine *m, uint64_t max_cycles);
/* Copies src's whole state into dst for a differential run, without hooks, outputs, coverage or the hold. */
void machine_clone(machine *dst, const machine *src);
```

- [ ] **Step 3: Implement it in `machine.c`**

1. **The shared mapper.** Add it after `lower_half_selected`. Then rewrite `bus_read` and `machine_peek` on top of it:

```c
typedef enum { MAP_RAM, MAP_SFR, MAP_ROM, MAP_EMPTY } map_region;

/* Where a read of a lands under the current P2 (spec section 1.2): *offset indexes ram for MAP_RAM and the
   ROM file for MAP_ROM (the lower-ROM window maps $4000-$7FFF to file 0x0000-0x3FFF). */
static map_region map_read(const machine *m, uint16_t a, uint32_t *offset)
{
    if (is_sfr(a))
        return MAP_SFR;
    if (a < 0x4000) {
        *offset = a;
        return MAP_RAM;
    }
    if (a < 0x8000) {
        *offset = lower_half_selected(m) ? a - 0x4000u : a;
        return MAP_ROM;
    }
    if (!main_rom_selected(m))
        return MAP_EMPTY;
    *offset = a;
    return MAP_ROM;
}

static uint8_t bus_read(void *ctx, uint16_t a)
{
    machine *m = ctx;
    uint32_t off = 0;
    switch (map_read(m, a, &off)) {
    case MAP_SFR:
        if (!m50734_is_modelled((uint8_t)a) || m50734_is_write_only((uint8_t)a))
            note_unmodelled(m, a, false);
        return m50734_read(&m->sfr, (uint8_t)a);
    case MAP_RAM:
        return m->ram[off];
    case MAP_ROM:
        if (off != a)
            m->lower_window_reads++;
        return m->rom[off];
    default:
        return 0xFF;
    }
}

uint8_t machine_peek(const machine *m, uint16_t a)
{
    uint32_t off = 0;
    switch (map_read(m, a, &off)) {
    case MAP_RAM:
        return m->ram[off];
    case MAP_ROM:
        return m->rom[off];
    case MAP_SFR:
        return 0;
    default:
        return 0xFF;
    }
}
```

2. **The IRQ hold** in `sync_irqs`: `uint32_t lines = m->irq_hold ? 0 : m50734_irq_lines(&m->sfr);`

3. **Split `machine_run`** into `prepare` and `exec`, and add the hook lookup. Behaviour stays identical when no hooks are set. Replace the whole `machine_run` with:

```c
static void prepare(machine *m)
{
    maybe_deliver(m);
    sync_irqs(m);
}

static const machine_hook_entry *find_hook(const machine *m)
{
    uint16_t pc = m->cpu.pc;
    uint32_t off = 0;
    if (m->hook_count == 0 || !(m->hook_map[pc >> 3] & (1u << (pc & 7))))
        return NULL;
    if (m->cpu.waiting || (m->cpu.irq_lines && !(m->cpu.p & CPU740_I)))
        return NULL;   /* the interrupt is taken first; the hook fires when the routine is entered again */
    if (map_read(m, pc, &off) != MAP_ROM || off != pc)
        return NULL;   /* the lower-ROM window or an option socket, not the main ROM routine */
    for (unsigned i = 0; i < m->hook_count; i++)
        if (m->hooks[i].addr == pc)
            return &m->hooks[i];
    return NULL;
}

/* The body of one machine_run iteration after the idle check. False on a fault. */
static bool exec(machine *m)
{
    if (pc_escaped(m)) {
        m->fault = MFAULT_ESCAPED_PC;
        return false;
    }
    if (m->cpu.pc == APOLLO_SWAP_PC && !m->cpu.waiting)
        capture_frame(m);
    const machine_hook_entry *h = find_hook(m);
    if (h && h->fn(m, h->user))
        return m->fault == MFAULT_NONE;
    m->op_pc = m->cpu.pc;
    m->history[m->history_pos++ % MACHINE_HISTORY] = m->cpu.pc;
    cov_space cov_from = COV_ROM;
    bool cov_indirect = false;
    if (m->coverage) {
        /* Mirrors cpu740_step: a waiting CPU wakes on any IRQ line; an unmasked IRQ is taken
           instead of the instruction at op_pc. Only an executed instruction is recorded. */
        bool wakes = !m->cpu.waiting || m->cpu.irq_lines;
        bool irq = m->cpu.irq_lines && !(m->cpu.p & CPU740_I);
        if (wakes && !irq) {
            uint8_t op = machine_peek(m, m->op_pc);
            cov_from = coverage_space(m, m->op_pc);
            coverage_mark(m->coverage, cov_from, m->op_pc);
            cov_indirect = op == 0x6C || op == 0xB2 || op == 0x02;   /* JMP (abs), JMP (zp), JSR (zp) */
        }
    }
    int cycles = cpu740_step(&m->cpu, &m->bus);
    if (cycles == 0) {
        m->fault = MFAULT_CPU;
        return false;
    }
    if (cov_indirect)
        coverage_add_edge(m->coverage, cov_from, m->op_pc, coverage_space(m, m->cpu.pc), m->cpu.pc);
    uint64_t acc = m->cycle_rem + (uint64_t)cycles * m->cycle_num;
    m->cycle_rem = acc % m->cycle_den;
    m50734_tick(&m->sfr, (int)(acc / m->cycle_den));
    return m->fault == MFAULT_NONE;
}

machine_status machine_run(machine *m, uint64_t max_cycles)
{
    uint64_t end = m->cpu.cycles + max_cycles;
    while (m->cpu.cycles < end) {
        prepare(m);
        if (at_idle(m) && m->q_head == m->q_tail && !m->sfr.uart_rx_full &&
            !(m->sfr.icr2 & ICR2_T3_ENABLE) && !m->cpu.irq_lines)
            return MACHINE_IDLE;
        if (!exec(m))
            return MACHINE_FAULT;
    }
    return MACHINE_CYCLE_LIMIT;
}

bool machine_step(machine *m)
{
    prepare(m);
    return exec(m);
}

bool machine_hook(machine *m, uint16_t addr, machine_hook_fn fn, void *user)
{
    if (m->hook_count == MACHINE_MAX_HOOKS || (m->hook_map[addr >> 3] & (1u << (addr & 7))))
        return false;
    m->hooks[m->hook_count].addr = addr;
    m->hooks[m->hook_count].fn = fn;
    m->hooks[m->hook_count].user = user;
    m->hook_count++;
    m->hook_map[addr >> 3] |= (uint8_t)(1u << (addr & 7));
    return true;
}

void machine_clear_hooks(machine *m)
{
    m->hook_count = 0;
    memset(m->hook_map, 0, sizeof m->hook_map);
}

void machine_return(machine *m)
{
    uint8_t lo = m->ram[0x100 + (uint8_t)(m->cpu.s + 1)];
    uint8_t hi = m->ram[0x100 + (uint8_t)(m->cpu.s + 2)];
    m->cpu.s = (uint8_t)(m->cpu.s + 2);
    m->cpu.pc = (uint16_t)((lo | hi << 8) + 1);
}

void machine_hold_irqs(machine *m, bool hold)
{
    m->irq_hold = hold;
}

bool machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles)
{
    cpu740 *c = &m->cpu;
    uint16_t saved_pc = c->pc, ret = (uint16_t)(MACHINE_ROM_CALL_SENTINEL - 1);   /* RTS adds 1 */
    uint8_t s0 = c->s;
    m->ram[0x100 + c->s] = (uint8_t)(ret >> 8);   /* pushed like JSR: high byte first */
    c->s--;
    m->ram[0x100 + c->s] = (uint8_t)ret;
    c->s--;
    c->a = regs->a;
    c->x = regs->x;
    c->y = regs->y;
    c->p = regs->p;
    c->pc = addr;
    uint64_t end = c->cycles + max_cycles;
    while (!(c->pc == MACHINE_ROM_CALL_SENTINEL && c->s == s0)) {
        if (c->cycles >= end || !machine_step(m))
            return false;
    }
    regs->a = c->a;
    regs->x = c->x;
    regs->y = c->y;
    regs->p = c->p;
    c->pc = saved_pc;
    return true;
}

bool machine_finish_routine(machine *m, uint64_t max_cycles)
{
    uint8_t s0 = m->cpu.s;
    uint64_t end = m->cpu.cycles + max_cycles;
    while (m->cpu.s <= s0) {   /* pushes inside the routine go below s0; its RTS lifts SP above */
        if (m->cpu.cycles >= end || !machine_step(m))
            return false;
    }
    return true;
}

void machine_clone(machine *dst, const machine *src)
{
    memcpy(dst, src, sizeof *dst);
    dst->bus.ctx = dst;
    dst->coverage = NULL;
    dst->on_frame = dst->on_raw = NULL;
    dst->user = dst->raw_user = NULL;
    dst->irq_hold = false;
    machine_clear_hooks(dst);
}
```

4. **`machine_dump`.** Append `"firmware C (a read outside the converted data, or a ROM call that did not return)"` to `k_faults`. After the `MFAULT_CPU` detail line, add:

```c
    if (m->fault == MFAULT_FIRMWARE)
        fprintf(out, " (at $%04X)", m->fault_addr);
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test"`
Expected: every suite ok, including `suite_machine_hooks` and the unchanged `suite_golden`, `suite_machine` and `suite_coverage`. The refactor must not change any golden.

- [ ] **Step 5: Commit**

```bash
git add src/machine/machine.h src/machine/machine.c tests/test_machine_hooks.c tests/suites.h
git commit -m "machine: shared address mapper, machine_step, hooks, ROM calls, IRQ hold and clone (M4.0 3.2-3.3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Firmware core: context, RAM names, build boundary (spec §3.1)

**Files:**
- Create: `tools/gen_fw_ram.py`, `src/firmware/fw_ram.h` (generated), `src/firmware/fw.h`, `src/firmware/fw.c`, `tests/test_fw.c`
- Modify: `build.bat`, `tests/suites.h` (append `SUITE(suite_fw)`)

**Interfaces:**
- Produces, all in `src/firmware/fw.h` and used by Tasks 5–8: the types `fw_regs`, `fw_host`, `fw_image`, `fw`; the `FW_TMP_78` and `FW_RAM_SIZE` constants; the inline accessors `fw_rd8`, `fw_wr8`, `fw_rd16`, `fw_wr16`; and the functions `fw_init`, `fw_rom8`, `fw_mem8`, `fw_ind_y`, `fw_set_low_window`, `fw_rom_call`.
- `src/firmware/fw_ram.h` holds one `#define NAME 0xADDR` per RAM variable of `ghidra740/annotations/ram.txt`, named in upper case. Examples: `TN_POS 0x0051`, `TN_RULE_PTR 0x0020`, `CMD_TEXT 0x1A00`.

- [ ] **Step 1: Generate the RAM names**

`tools/gen_fw_ram.py`:

```python
#!/usr/bin/env python3
"""Writes src/firmware/fw_ram.h, the firmware's RAM names, from ghidra740/annotations/ram.txt (M4.0 spec 3.1).
usage: python tools/gen_fw_ram.py   (rerun after the annotations change and commit the result)"""
SRC = "ghidra740/annotations/ram.txt"
OUT = "src/firmware/fw_ram.h"


def main():
    rows, seen = [], set()
    with open(SRC, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            fields = line.split(None, 5)   # space, address, kind, name, type, comment
            if len(fields) < 5 or fields[0] != "ram" or fields[2] != "data":
                continue
            addr = int(fields[1], 16)
            if addr >= 0x4000 or 0xDA <= addr <= 0xFF:   # RAM only; the SFRs have their own names
                continue
            name = fields[3].upper()
            if name in seen:
                raise SystemExit("duplicate name " + name)
            seen.add(name)
            rows.append((addr, name, fields[4]))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("#ifndef FW_RAM_H\n#define FW_RAM_H\n")
        f.write("/* The firmware's RAM variables, named as in ghidra740/annotations/ram.txt.\n")
        f.write("   Generated by tools/gen_fw_ram.py; do not edit by hand. */\n\n")
        for addr, name, typ in sorted(rows):
            f.write("#define %-28s 0x%04X   /* %s */\n" % (name, addr, typ))
        f.write("\n#endif\n")
    print("wrote %s: %d names" % (OUT, len(rows)))


if __name__ == "__main__":
    main()
```

Run: `python tools/gen_fw_ram.py`
Expected: `wrote src/firmware/fw_ram.h: N names`, with N around 200. Check that it contains `#define TN_POS                       0x0051` and `#define CMD_TEXT                     0x1A00`.

- [ ] **Step 2: Write the failing test**

`tests/test_fw.c`:

```c
/* The firmware context (M4.0 spec section 3.1): RAM access, the converted-data reads and their fault, the
   lower-ROM window and the host calls. Uses a hand-made image, not the converted tables. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"

static fw_image g_img;
static uint8_t g_ram[FW_RAM_SIZE];
static int g_window_calls;
static bool g_window;

static void mark(uint8_t *ok, uint32_t i) { ok[i >> 3] |= (uint8_t)(1u << (i & 7)); }

static void host_window(void *ctx, bool on)
{
    (void)ctx;
    g_window_calls++;
    g_window = on;
}

void suite_fw(void)
{
    fw f;
    memset(&g_img, 0, sizeof g_img);
    memset(g_ram, 0, sizeof g_ram);
    g_img.rom[0x9AB4] = 0xC0;
    mark(g_img.rom_ok, 0x9AB4);
    g_img.rom[0x4102] = 0x11;
    mark(g_img.rom_ok, 0x4102);
    g_img.low[0x0102] = 0x22;   /* $4102 with the window on */
    mark(g_img.low_ok, 0x0102);

    fw_init(&f, g_ram, &g_img, NULL);
    CHECK_EQ(fw_rom8(&f, 0x9AB4), 0xC0);
    CHECK(!f.fault);
    CHECK_EQ(fw_rom8(&f, 0x4102), 0x11);
    fw_set_low_window(&f, true);          /* no host: only the flag */
    CHECK_EQ(fw_rom8(&f, 0x4102), 0x22);
    CHECK_EQ(fw_rom8(&f, 0x9AB4), 0xC0);  /* the window covers only $4000-$7FFF */
    CHECK(!f.fault);
    CHECK_EQ(fw_rom8(&f, 0x9000), 0xFF);  /* not converted */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x9000);

    fw_init(&f, g_ram, &g_img, NULL);
    fw_wr16(&f, TN_RULE_PTR, 0x4100);
    CHECK_EQ(fw_rd8(&f, TN_RULE_PTR), 0x00);
    CHECK_EQ(fw_rd8(&f, TN_RULE_PTR + 1), 0x41);
    CHECK_EQ(fw_rd16(&f, TN_RULE_PTR), 0x4100);
    CHECK_EQ(fw_ind_y(&f, (uint8_t)TN_RULE_PTR, 2), 0x11);   /* ($20),Y */
    fw_wr8(&f, CMD_TEXT + 3, 'A');
    CHECK_EQ(g_ram[0x1A03], 'A');
    CHECK_EQ(fw_mem8(&f, 0x1A03), 'A');
    CHECK(!f.fault);
    fw_mem8(&f, 0x00F0);                  /* an SFR is not data */
    CHECK(f.fault);

    fw_host host = { NULL, NULL, host_window };
    fw_init(&f, g_ram, &g_img, &host);
    fw_set_low_window(&f, true);
    CHECK_EQ(g_window_calls, 1);
    CHECK(g_window);
    CHECK(f.low_window);
    fw_regs r = { 0, 0, 0, 0 };
    CHECK(!fw_rom_call(&f, 0xB154, &r));  /* the host has no rom_call */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB154);

    CHECK_EQ(TN_POS, 0x0051);
    CHECK_EQ(CMD_TEXT, 0x1A00);
    CHECK_EQ(LTS_MATCH_POS, 0x004F);
}
```

Append `SUITE(suite_fw)` to `tests/suites.h`.

- [ ] **Step 3: Write `fw.h` and `fw.c`**

`src/firmware/fw.h`:

```c
#ifndef FW_H
#define FW_H
/* Decompiled Apollo 2 firmware on the firmware's own memory image (M4.0 spec section 3).
   Nothing under src/firmware/ includes machine/ or cpu740/ headers: build.bat syntax-checks this directory
   without the src include path. */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include "fw_ram.h"

#define FW_RAM_SIZE  0x4000
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters */

typedef struct { uint8_t a, x, y, p; } fw_regs;

/* The machine or native host. In hybrid runs it is the emulator (src/engine/thunks.c). */
typedef struct fw_host {
    void *ctx;
    /* Runs the routine at addr, still in ROM, to its RTS. A, X and Y in and out; the routine starts with the
       machine's current flags, and p returns its exit flags. False if it faulted or did not return. */
    bool (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs);
    /* P2 bit 7, the lower-ROM window, as SEB7/CLB7 $F0 set and clear it. */
    void (*set_low_window)(void *ctx, bool on);
} fw_host;

/* Converted ROM data at its original addresses (built by src/firmware/data/fw_data.c). */
typedef struct fw_image {
    uint8_t rom[0x10000];          /* $4000-$FFFF with the window off; index = CPU address */
    uint8_t rom_ok[0x10000 / 8];   /* bit set = converted byte */
    uint8_t low[0x4000];           /* $4000-$7FFF with the window on; index = CPU address - $4000 */
    uint8_t low_ok[0x4000 / 8];
} fw_image;

typedef struct fw {
    uint8_t *ram;                  /* the firmware's RAM $0000-$3FFF (the SFR bytes $DA-$FF are not data) */
    const fw_image *img;
    const fw_host *host;           /* NULL: no ROM to call, no port to drive */
    bool low_window;               /* P2 bit 7: $4000-$7FFF read the LOW overlay */
    bool fault;                    /* a read outside the converted data, or a failed ROM call */
    uint16_t fault_addr;
    jmp_buf *trap;                 /* when set, a fault longjmps here and the routine is abandoned */
} fw;

/* RAM by name; a < FW_RAM_SIZE. */
static inline uint8_t fw_rd8(const fw *f, uint16_t a) { return f->ram[a]; }
static inline void fw_wr8(fw *f, uint16_t a, uint8_t v) { f->ram[a] = v; }
static inline uint16_t fw_rd16(const fw *f, uint16_t a) { return (uint16_t)(f->ram[a] | f->ram[a + 1] << 8); }
static inline void fw_wr16(fw *f, uint16_t a, uint16_t v)
{
    f->ram[a] = (uint8_t)v;
    f->ram[a + 1] = (uint8_t)(v >> 8);
}

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host);
/* A ROM data byte as the CPU reads it at addr (>= $4000) under the current window. A byte outside the
   converted data is a fault: it longjmps to f->trap when set, else reads $FF. */
uint8_t fw_rom8(fw *f, uint16_t addr);
/* A data byte anywhere: RAM below $4000 (an SFR address is a fault), else fw_rom8. */
uint8_t fw_mem8(fw *f, uint16_t addr);
/* (zp),Y: the byte at the little-endian pointer in zero page zp, plus y. */
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y);
/* SEB7/CLB7 P2: sets the window and tells the host. */
void fw_set_low_window(fw *f, bool on);
/* Calls a routine still in ROM through the host. Without a host, or when the call fails, a fault. */
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs);

#endif
```

`src/firmware/fw.c`:

```c
#include "fw.h"

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host)
{
    f->ram = ram;
    f->img = img;
    f->host = host;
    f->low_window = false;
    f->fault = false;
    f->fault_addr = 0;
    f->trap = NULL;
}

static void fault(fw *f, uint16_t addr)
{
    if (!f->fault) {
        f->fault = true;
        f->fault_addr = addr;
    }
    if (f->trap)
        longjmp(*f->trap, 1);
}

static bool converted(const uint8_t *ok, uint32_t i) { return (ok[i >> 3] >> (i & 7)) & 1; }

uint8_t fw_rom8(fw *f, uint16_t addr)
{
    const fw_image *img = f->img;
    if (img && addr >= 0x4000) {
        if (f->low_window && addr < 0x8000) {
            if (converted(img->low_ok, addr - 0x4000u))
                return img->low[addr - 0x4000u];
        } else if (converted(img->rom_ok, addr)) {
            return img->rom[addr];
        }
    }
    fault(f, addr);
    return 0xFF;
}

uint8_t fw_mem8(fw *f, uint16_t addr)
{
    if (addr >= 0x4000)
        return fw_rom8(f, addr);
    if (addr >= 0xDA && addr <= 0xFF) {
        fault(f, addr);
        return 0xFF;
    }
    return f->ram[addr];
}

uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y)
{
    return fw_mem8(f, (uint16_t)(fw_rd16(f, zp) + y));
}

void fw_set_low_window(fw *f, bool on)
{
    f->low_window = on;
    if (f->host && f->host->set_low_window)
        f->host->set_low_window(f->host->ctx, on);
}

bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs)
{
    if (!f->host || !f->host->rom_call || !f->host->rom_call(f->host->ctx, addr, regs)) {
        fault(f, addr);
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Add the firmware boundary and the new source dirs to `build.bat`**

Replace the `set LIBSRC=` block (the line and its `for` loop) with:

```bat
rem M4.0 spec 3.1: src\firmware builds without the emulator. Syntax-check it without /Isrc, so an include of
rem a machine\ or cpu740\ header cannot be found.
set FWSRC=
for %%d in (src\firmware src\firmware\data) do (
    if exist %%d\*.c set FWSRC=!FWSRC! %%d\*.c
)
if defined FWSRC (
    cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS !FWSRC! || exit /b 1
)
set LIBSRC=
for %%d in (src\cpu740 src\machine src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
    if exist %%d\*.c set LIBSRC=!LIBSRC! %%d\*.c
)
```

- [ ] **Step 5: Run the tests and check the boundary**

Run: `cmd //c ".\build.bat test fw"`
Expected: `suite_fw ok`.

Prove the boundary: temporarily add `#include "machine/machine.h"` at the top of `src/firmware/fw.c` and run `cmd //c ".\build.bat"`.
Expected: the build fails with C1083 (cannot open include file). Remove the line again, and rebuild to confirm it passes.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_fw_ram.py src/firmware/fw_ram.h src/firmware/fw.h src/firmware/fw.c tests/test_fw.c tests/suites.h build.bat
git commit -m "firmware core: fw context on the RAM image, RAM names from the annotations, build boundary

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Converted tables, encoder and `firmware_data` (spec §4)

**Files:**
- Create: `tools/extract_tables.py`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c`
- Generate, review and commit: `src/firmware/data/tn_rule_data.c`, `src/firmware/data/lts_rule_data_aj.c`, `src/firmware/data/lts_rule_data_kz.c`, `src/firmware/data/lts_table_data.c`
- Modify: `tests/suites.h` (append `SUITE(suite_firmware_data)`)

**Interfaces:**
- Consumes: `fw_image` (Task 4).
- Produces, in `src/firmware/data/fw_data.h` and used by Tasks 6–8:
  - the table types `fw_rule`, `fw_rule_list`, `fw_rule_table`, `fw_affix_list`;
  - the pinned address macros `TN_CHAR_RULES_LO`, `TN_CHAR_RULES_HI`, `LTS_PAIR_INDEX`, `LTS_PAIRS_A`, `UNUSED_9824`, `LTS_SUFFIXES`, `LTS_SIBILANTS`, `LTS_AT_CONSONANTS`, `LTS_CHAR_CLASS`;
  - the class bits `CC_*`;
  - the data `tn_char_lists[256]`, `lts_pair_lists[26]` and `lts_char_class[256]`;
  - the functions `bool fw_image_build(fw_image *img, char *err, size_t errlen)` and `const fw_image *fw_image_get(void)`.

The ROM format was checked while writing this plan. Each table holds back-to-back rule lists. A rule is:
- the left context, bytes ≥ `$1E`, nearest character first;
- a count n < `$1E`;
- the right context, bytes ≥ `$1E`, whose first n characters are consumed;
- a length m < `$1E`;
- m output bytes.

Each list ends with `$FF`. The tables hold 531 lists and 3,766 rules. Every pointer lands on a list start, and n ≤ the right-context length always. The output bytes are `$20`–`$C0` and never `$00`. Exactly two lists are unreferenced: `tn_rules_high`'s trailing empty list at `$5622`, and the LOOP list at LOW `$4310`. The affix lists are each `$00 $0D $0A` (entry `$0D $0A`)… `$00`.

- [ ] **Step 1: Write the header**

`src/firmware/data/fw_data.h`:

```c
#ifndef FW_DATA_H
#define FW_DATA_H
/* Readable firmware tables converted from the ROM (M4.0 spec section 4), each pinned to its annotated ROM
   address. fw_data.c encodes them into the data image the C code reads (fw_rom8). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../fw.h"

/* One rule (format: the annotation of tn_rules_punct). left: the context before the key, in reading order
   (the ROM stores it nearest character first); consumed: the characters after the key the rule consumes (the
   first n of its right context); right: the rest of the right context; output: phonetic text for tn_phonetic. */
typedef struct { const char *left, *consumed, *right, *output; } fw_rule;
typedef struct { const fw_rule *rules; uint16_t count; } fw_rule_list;   /* the ROM ends each list with $FF */
/* Rule lists laid out back to back from addr (in the LOW overlay when low), size bytes in all. */
typedef struct {
    const char *name;
    uint16_t addr, size;
    bool low;
    const fw_rule_list *const *lists;
    uint16_t count;
} fw_rule_table;
/* A context-symbol list (% & @): $00 CR LF, then each entry followed by CR LF, then $00. */
typedef struct { const char *name; uint16_t addr; const char *const *entries; uint8_t count; } fw_affix_list;

/* Pinned addresses (ghidra740/annotations: text_norm.txt, lts.txt) */
#define TN_CHAR_RULES_LO   0x4000   /* low byte of each character's list address */
#define TN_CHAR_RULES_HI   0x4100
#define LTS_PAIR_INDEX     0x4200   /* the address of lts_pairs_a..z per key letter */
#define LTS_PAIRS_A        0x4234   /* lts_pairs_a..z: 26 tables of 26 words, 52 bytes apart */
#define UNUSED_9824        0x9824   /* a $00 before lts_suffixes that no code reads */
#define LTS_SUFFIXES       0x9825
#define LTS_SIBILANTS      0x9861
#define LTS_AT_CONSONANTS  0x9884
#define LTS_CHAR_CLASS     0x9AB4

/* lts_char_class bits */
#define CC_LETTER       0x80   /* also the apostrophe and the underscore */
#define CC_VOWEL        0x40   /* A E I O U Y (also the underscore and lower-case x) */
#define CC_FRONT_VOWEL  0x20   /* E I Y */
#define CC_NASAL        0x10   /* M N */
#define CC_VOICED       0x08   /* B D G L M N R V W */
#define CC_CONSONANT    0x04
#define CC_HIGH         0x02   /* some codes above $7F only */
#define CC_PUNCT        0x01   /* punctuation or symbol */

extern const fw_rule_table tn_rules_ctrl, tn_rules_punct, tn_rules_digits, tn_rules_punct2, lts_rules_letters,
    tn_rules_symbols3, tn_rules_lower, tn_rules_symbols4, tn_rules_high;
extern const fw_rule_table lts_rules_a, lts_rules_b, lts_rules_c, lts_rules_d, lts_rules_e, lts_rules_f,
    lts_rules_g, lts_rules_h, lts_rules_i, lts_rules_j, lts_rules_k, lts_rules_l, lts_rules_m, lts_rules_n,
    lts_rules_o, lts_rules_p, lts_rules_q, lts_rules_r, lts_rules_s, lts_rules_t, lts_rules_u, lts_rules_v,
    lts_rules_w, lts_rules_x, lts_rules_y, lts_rules_z;
extern const fw_rule_list *const lts_pairs_a[26], *const lts_pairs_b[26], *const lts_pairs_c[26],
    *const lts_pairs_d[26], *const lts_pairs_e[26], *const lts_pairs_f[26], *const lts_pairs_g[26],
    *const lts_pairs_h[26], *const lts_pairs_i[26], *const lts_pairs_j[26], *const lts_pairs_k[26],
    *const lts_pairs_l[26], *const lts_pairs_m[26], *const lts_pairs_n[26], *const lts_pairs_o[26],
    *const lts_pairs_p[26], *const lts_pairs_q[26], *const lts_pairs_r[26], *const lts_pairs_s[26],
    *const lts_pairs_t[26], *const lts_pairs_u[26], *const lts_pairs_v[26], *const lts_pairs_w[26],
    *const lts_pairs_x[26], *const lts_pairs_y[26], *const lts_pairs_z[26];

extern const fw_rule_list *const tn_char_lists[256];          /* tn_char_rules_lo/hi */
extern const fw_rule_list *const *const lts_pair_lists[26];   /* [key - 'A'][second - 'A'], NULL = none */
extern const fw_rule_table *const fw_rule_tables[];           /* every rule table, in address order per space */
extern const size_t fw_rule_table_count;
extern const fw_affix_list fw_affix_lists[];
extern const size_t fw_affix_list_count;
extern const uint8_t lts_char_class[256];

/* Encodes every table into img at its ROM address. False with a reason in err on any inconsistency. */
bool fw_image_build(fw_image *img, char *err, size_t errlen);
/* The image, built on first use; NULL (reason printed to stderr) if it does not build. */
const fw_image *fw_image_get(void);

#endif
```

- [ ] **Step 2: Write the converter**

`tools/extract_tables.py`:

```python
#!/usr/bin/env python3
"""One-time converter (M4.0 spec 4.2): the ROM's character rules, letter-to-sound rules, affix lists and
character classes -> readable C in src/firmware/data/. The output is a first draft: review it, annotate it by
hand if useful, and commit it. Nothing runs this at build or test time; tests/test_firmware_data.c checks the
committed C against the ROM.
usage: python tools/extract_tables.py [ROM]   (default $APOLLO2_ROM, else UK_V2.01_4.04_ROM.BIN)"""
import os
import sys

OUT = os.path.join("src", "firmware", "data")
# (name, address, size), as annotated in ghidra740/annotations/text_norm.txt and lts.txt
CHAR_TABLES = [
    ("tn_rules_ctrl", 0x477C, 96), ("tn_rules_punct", 0x47DC, 166), ("tn_rules_digits", 0x4882, 255),
    ("tn_rules_punct2", 0x4981, 69), ("lts_rules_letters", 0x49C6, 1610), ("tn_rules_symbols3", 0x5010, 295),
    ("tn_rules_lower", 0x5137, 78), ("tn_rules_symbols4", 0x5185, 55), ("tn_rules_high", 0x51BC, 1129),
]
PAIR_TABLES = [
    ("a", 0x5625, 3062), ("b", 0x621B, 1346), ("c", 0x675D, 2807), ("d", 0x7254, 1163), ("e", 0x76DF, 2148),
    ("f", 0x7F43, 698), ("g", 0x81FD, 782), ("h", 0x850B, 862), ("i", 0x8869, 1834), ("j", 0x8F93, 97),
    ("k", 0x4010, 131), ("l", 0x4093, 876), ("m", 0x43FF, 1279), ("n", 0x48FE, 580), ("o", 0x4B42, 1707),
    ("p", 0x51ED, 2353), ("q", 0x5B1E, 201), ("r", 0x5BE7, 1268), ("s", 0x60DB, 2503), ("t", 0x6AA2, 1194),
    ("u", 0x6F4C, 628), ("v", 0x71C0, 614), ("w", 0x7426, 706), ("x", 0x76E8, 74), ("y", 0x7732, 165),
    ("z", 0x77D7, 91),
]
AFFIXES = [("lts_suffixes", 0x9825, 60), ("lts_sibilants", 0x9861, 35), ("lts_at_consonants", 0x9884, 24)]
CLASS_BITS = [(0x80, "CC_LETTER"), (0x40, "CC_VOWEL"), (0x20, "CC_FRONT_VOWEL"), (0x10, "CC_NASAL"),
              (0x08, "CC_VOICED"), (0x04, "CC_CONSONANT"), (0x02, "CC_HIGH"), (0x01, "CC_PUNCT")]
HEADER = ("/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.0 spec section 4), then reviewed.\n"
          "   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */\n"
          "#include <stddef.h>\n#include \"fw_data.h\"\n\n")


def cstr(b):
    s = '"'
    for c in b:
        if c in (0x22, 0x5C, 0x3F):           # " \ ? (no trigraphs)
            s += "\\" + chr(c)
        elif 0x20 <= c < 0x7F:
            s += chr(c)
        else:
            s += "\\%03o" % c
    return s + '"'


def char_name(c):
    if 0x20 < c < 0x7F and chr(c) not in "'\\":
        return "'%s' ($%02X)" % (chr(c), c)
    return "$%02X" % c


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("APOLLO2_ROM", "UK_V2.01_4.04_ROM.BIN")
    rom = open(path, "rb").read()
    assert len(rom) == 0x10000, "need the 64 KB ROM"

    def rd(a, low):
        return rom[a - 0x4000] if low else rom[a]

    def word(a):
        return rom[a] | rom[a + 1] << 8

    def parse(addr, size, low):
        lists, p, end = [], addr, addr + size
        while p < end:
            start, rules = p, []
            while True:
                left = []
                while rd(p, low) >= 0x1E:
                    left.append(rd(p, low)); p += 1
                n = rd(p, low); p += 1
                right = []
                while rd(p, low) >= 0x1E:
                    right.append(rd(p, low)); p += 1
                assert n <= len(right), "rule at $%04X consumes more than its right context" % start
                m = rd(p, low); p += 1
                out = bytes(rd(p + i, low) for i in range(m)); p += m
                assert 0 not in out, "rule at $%04X has a $00 in its output" % start
                rules.append((bytes(reversed(left)), bytes(right[:n]), bytes(right[n:]), out))
                if rd(p, low) == 0xFF:
                    p += 1
                    break
            lists.append((start, rules))
        assert p == end, "table at $%04X: lists end at $%04X, the annotation says $%04X" % (addr, p, end)
        return lists

    def encode(rules):
        b = bytearray()
        for left, cons, right, out in rules:
            b += bytes(reversed(left)) + bytes([len(cons)]) + cons + right + bytes([len(out)]) + out
        return bytes(b) + b"\xff"

    def emit_list(lines, lid, title, rules):
        lines.append("/* %s */" % title)
        lines.append("static const fw_rule r_%s[] = {" % lid)
        for left, cons, right, out in rules:
            lines.append("    { %s, %s, %s, %s }," % (cstr(left), cstr(cons), cstr(right), cstr(out)))
        lines.append("};")
        lines.append("static const fw_rule_list l_%s = { r_%s, %d };" % (lid, lid, len(rules)))
        lines.append("")

    def emit_table(lines, name, addr, size, low, ids):
        lines.append("static const fw_rule_list *const k_%s[] = {" % name)
        for i in range(0, len(ids), 6):
            lines.append("    " + " ".join("&l_%s," % x for x in ids[i:i + 6]))
        lines.append("};")
        lines.append("const fw_rule_table %s = { \"%s\", 0x%04X, %d, %s, k_%s, %d };"
                     % (name, name, addr, size, "true" if low else "false", name, len(ids)))
        lines.append("")

    # Character rules ($477C-$5624): lists named by the first character that points at them.
    char_ptr = [rom[0x4000 + c] | rom[0x4100 + c] << 8 for c in range(256)]
    refs = {}
    for c, p in enumerate(char_ptr):
        refs.setdefault(p, []).append(c)
    lines = [HEADER]
    for name, addr, size in CHAR_TABLES:
        lists = parse(addr, size, False)
        for start, rules in lists:
            assert rom[start:start + len(encode(rules))] == encode(rules)
        ids = []
        for start, rules in lists:
            if start in refs:
                lid = "c%02x" % refs[start][0]
                title = "%s at $%04X, the list of %s" % (name, start, ", ".join(char_name(c) for c in refs[start]))
            else:
                lid = "unref_%04x" % start
                title = "%s at $%04X: no character points here" % (name, start)
            emit_list(lines, lid, title, rules)
            ids.append(lid)
        emit_table(lines, name, addr, size, False, ids)
    lines.append("const fw_rule_list *const tn_char_lists[256] = {")
    for c in range(256):
        lines.append("    [0x%02X] = &l_c%02x,   /* %s */" % (c, refs[char_ptr[c]][0], char_name(c)))
    lines.append("};")
    with open(os.path.join(OUT, "tn_rule_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    # Two-letter rules: lists named by the pairs that point at them.
    for k in range(26):
        assert word(0x4200 + 2 * k) == 0x4234 + 52 * k, "lts_pair_index is not the plain table sequence"
    pair_ptr = [[word(0x4234 + 52 * k + 2 * s) for s in range(26)] for k in range(26)]
    for group, keys in (("aj", PAIR_TABLES[:10]), ("kz", PAIR_TABLES[10:])):
        lines = [HEADER]
        for key, addr, size in keys:
            k = ord(key) - ord("a")
            low = k >= 10
            lists = parse(addr, size, low)
            prefs = {}
            for s, p in enumerate(pair_ptr[k]):
                if p:
                    prefs.setdefault(p, []).append(s)
            ids, by_addr = [], {}
            for start, rules in lists:
                if start in prefs:
                    lid = "_".join(key + chr(ord("a") + s) for s in prefs[start])
                    title = "lts_rules_%s at %s$%04X: %s" % (key, "LOW " if low else "", start,
                                                             ", ".join((key + chr(ord("a") + s)).upper()
                                                                       for s in prefs[start]))
                else:
                    lid = "%s_unref_%04x" % (key, start)
                    title = "lts_rules_%s at %s$%04X: no pair entry points here" % (key, "LOW " if low else "", start)
                emit_list(lines, lid, title, rules)
                ids.append(lid)
                by_addr[start] = lid
            emit_table(lines, "lts_rules_" + key, addr, size, low, ids)
            lines.append("const fw_rule_list *const lts_pairs_%s[26] = {" % key)
            for s, p in enumerate(pair_ptr[k]):
                if p:
                    assert p in by_addr, "pair %s%s points into a list" % (key, chr(ord("a") + s))
                    lines.append("    ['%s' - 'A'] = &l_%s," % (chr(ord("A") + s), by_addr[p]))
            lines.append("};")
            lines.append("")
        with open(os.path.join(OUT, "lts_rule_data_%s.c" % group), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")

    # Pair index, rule table list, affix lists, character classes.
    lines = [HEADER]
    lines.append("const fw_rule_list *const *const lts_pair_lists[26] = {")
    lines.append("    " + ", ".join("lts_pairs_%s" % chr(ord("a") + k) for k in range(26)) + ",")
    lines.append("};")
    lines.append("")
    lines.append("const fw_rule_table *const fw_rule_tables[] = {")
    for name, _, _ in CHAR_TABLES:
        lines.append("    &%s," % name)
    for key, _, _ in PAIR_TABLES:
        lines.append("    &lts_rules_%s," % key)
    lines.append("};")
    lines.append("const size_t fw_rule_table_count = sizeof fw_rule_tables / sizeof fw_rule_tables[0];")
    lines.append("")
    for name, addr, size in AFFIXES:
        body = rom[addr:addr + size]
        assert body[:3] == b"\x00\r\n" and body[-1:] == b"\x00", name
        entries = body[3:-1].split(b"\r\n")
        assert entries[-1] == b"", name
        entries = entries[:-1]
        lines.append("static const char *const k_%s[] = { %s };" % (name, ", ".join(cstr(e) for e in entries)))
    lines.append("const fw_affix_list fw_affix_lists[] = {")
    for name, addr, size in AFFIXES:
        n = len(rom[addr + 3:addr + size - 1].split(b"\r\n")) - 1
        lines.append("    { \"%s\", 0x%04X, k_%s, %d }," % (name, addr, name, n))
    lines.append("};")
    lines.append("const size_t fw_affix_list_count = sizeof fw_affix_lists / sizeof fw_affix_lists[0];")
    lines.append("")
    lines.append("const uint8_t lts_char_class[256] = {")
    for c in range(256):
        v = rom[0x9AB4 + c]
        if v:
            names = [n for bit, n in CLASS_BITS if v & bit]
            lines.append("    [0x%02X] = %s,   /* %s */" % (c, " | ".join(names), char_name(c)))
    lines.append("};")
    with open(os.path.join(OUT, "lts_table_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote tn_rule_data.c, lts_rule_data_aj.c, lts_rule_data_kz.c, lts_table_data.c in", OUT)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Generate the data, then review and annotate it**

Run: `python tools/extract_tables.py`
Expected: the four files are written, with no assertion error.

Make these hand edits, which are comments only:
- **`lts_rule_data_kz.c`.** Above the list titled `lts_rules_l at LOW $4310: no pair entry points here`, add: `/* The LOOP rule no entry reaches: an observed ROM bug (parent spec rev 6, section 1.2). The port keeps it unreachable. */`. Above the list whose id is `lm_lo`, add: `/* L-M and L-O share this list; see the LOOP list above. */`
- **`tn_rule_data.c`.** Above the list titled `tn_rules_high at $5622`, add: `/* One more empty list that no pointer uses; the two-letter rule lists follow. */`

Open the four files and spot-check that the rules read naturally. Two examples: the `tn_rules_punct` list of `'!'` has output `" ekS6LxMAsxN! "`, and the first rule of `lts_rules_a`'s list `aa` is `{ "", "R", "", "h|" }`.

- [ ] **Step 4: Write the failing `firmware_data` test**

`tests/test_firmware_data.c`:

```c
/* The converted tables (M4.0 spec section 4.3): the encoder builds, covers exactly the converted ranges, and
   (with the ROM) matches the ROM byte for byte. */
#include <stdio.h>
#include "test.h"
#include "rom_fixture.h"
#include "firmware/data/fw_data.h"

typedef struct { uint32_t from, to; } range;   /* [from, to) in CPU addresses */
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9824, 0x989C }, { 0x9AB4, 0x9BB4 } };
static const range k_low[] = { { 0x4010, 0x7832 } };

static bool bit(const uint8_t *ok, uint32_t i) { return (ok[i >> 3] >> (i & 7)) & 1; }

static bool in(const range *r, size_t n, uint32_t a)
{
    for (size_t i = 0; i < n; i++)
        if (a >= r[i].from && a < r[i].to)
            return true;
    return false;
}

static void test_coverage(const fw_image *img)
{
    for (uint32_t a = 0; a < 0x10000; a++) {
        if (bit(img->rom_ok, a) != in(k_rom, 3, a)) {
            fprintf(stderr, "  $%04X: converted %d, expected %d\n", a, bit(img->rom_ok, a), in(k_rom, 3, a));
            g_test_failures++;
            break;
        }
    }
    for (uint32_t a = 0x4000; a < 0x8000; a++) {
        if (bit(img->low_ok, a - 0x4000) != in(k_low, 1, a)) {
            fprintf(stderr, "  LOW $%04X: converted %d, expected %d\n", a, bit(img->low_ok, a - 0x4000),
                    in(k_low, 1, a));
            g_test_failures++;
            break;
        }
    }
}

static void test_counts(void)
{
    unsigned lists = 0, rules = 0;
    for (size_t t = 0; t < fw_rule_table_count; t++) {
        lists += fw_rule_tables[t]->count;
        for (unsigned i = 0; i < fw_rule_tables[t]->count; i++)
            rules += fw_rule_tables[t]->lists[i]->count;
    }
    CHECK_EQ(fw_rule_table_count, 35);
    CHECK_EQ(lists, 531);
    CHECK_EQ(rules, 3766);
    CHECK_EQ(fw_affix_list_count, 3);
}

static void test_against_rom(const fw_image *img, const uint8_t *rom)
{
    unsigned bad = 0, compared = 0;
    for (uint32_t a = 0x4000; a < 0x10000; a++) {
        if (!bit(img->rom_ok, a))
            continue;
        compared++;
        if (img->rom[a] != rom[a] && bad++ < 10)
            fprintf(stderr, "  $%04X: converted %02X, ROM %02X\n", a, img->rom[a], rom[a]);
    }
    for (uint32_t a = 0x4000; a < 0x8000; a++) {
        if (!bit(img->low_ok, a - 0x4000))
            continue;
        compared++;
        if (img->low[a - 0x4000] != rom[a - 0x4000] && bad++ < 10)
            fprintf(stderr, "  LOW $%04X: converted %02X, ROM %02X\n", a, img->low[a - 0x4000], rom[a - 0x4000]);
    }
    printf("  %u converted bytes compared with the ROM, %u differ\n", compared, bad);
    CHECK_EQ(bad, 0);
}

void suite_firmware_data(void)
{
    const fw_image *img = fw_image_get();
    CHECK(img != NULL);
    if (!img)
        return;
    test_coverage(img);
    test_counts();
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM): converted tables not compared");
    test_against_rom(img, rom);
}
```

Append `SUITE(suite_firmware_data)` to `tests/suites.h`.

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: a link failure, because `fw_image_get` and `fw_image_build` are undefined.

- [ ] **Step 5: Write the encoder**

`src/firmware/data/fw_data.c`:

```c
/* Lays the converted tables out at their ROM addresses (M4.0 spec section 4.3). Every rule table must fill
   exactly its annotated size, no byte may be written twice, and every pointer table is computed from where the
   lists landed, so no address is stated twice in the data. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fw_data.h"

#define MAX_LISTS 640

typedef struct {
    fw_image *img;
    const fw_rule_list *list[MAX_LISTS];
    uint16_t addr[MAX_LISTS];
    size_t nlists;
    char *err;
    size_t errlen;
    bool ok;
} enc;

static void fail(enc *e, const char *fmt, ...)
{
    if (e->ok && e->errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e->err, e->errlen, fmt, ap);
        va_end(ap);
    }
    e->ok = false;
}

static void put(enc *e, bool low, uint32_t addr, uint8_t v)
{
    if (addr < 0x4000 || addr > 0xFFFF || (low && addr >= 0x8000)) {
        fail(e, "byte at $%04X is outside its space", (unsigned)addr);
        return;
    }
    uint8_t *mem = low ? e->img->low : e->img->rom, *ok = low ? e->img->low_ok : e->img->rom_ok;
    uint32_t i = low ? addr - 0x4000 : addr;
    if (ok[i >> 3] & (1u << (i & 7))) {
        fail(e, "$%04X%s written twice", (unsigned)addr, low ? " (LOW)" : "");
        return;
    }
    mem[i] = v;
    ok[i >> 3] |= (uint8_t)(1u << (i & 7));
}

static uint32_t put_text(enc *e, bool low, uint32_t at, const char *s, bool context)
{
    for (; *s; s++) {
        if (context && (uint8_t)*s < 0x1E)
            fail(e, "context byte %02X at $%04X is below $1E", (uint8_t)*s, (unsigned)at);
        put(e, low, at++, (uint8_t)*s);
    }
    return at;
}

static uint32_t put_rule_list(enc *e, bool low, uint32_t at, const fw_rule_list *l)
{
    for (uint16_t i = 0; i < l->count; i++) {
        const fw_rule *r = &l->rules[i];
        for (size_t n = strlen(r->left); n-- > 0;) {   /* stored nearest character first */
            if ((uint8_t)r->left[n] < 0x1E)
                fail(e, "context byte below $1E at $%04X", (unsigned)at);
            put(e, low, at++, (uint8_t)r->left[n]);
        }
        size_t nc = strlen(r->consumed), no = strlen(r->output);
        if (nc >= 0x1E || no >= 0x1E)
            fail(e, "rule at $%04X: count or length >= $1E", (unsigned)at);
        put(e, low, at++, (uint8_t)nc);
        at = put_text(e, low, at, r->consumed, true);
        at = put_text(e, low, at, r->right, true);
        put(e, low, at++, (uint8_t)no);
        at = put_text(e, low, at, r->output, false);
    }
    put(e, low, at++, 0xFF);
    return at;
}

static uint16_t list_addr(enc *e, const fw_rule_list *l)
{
    for (size_t i = 0; i < e->nlists; i++)
        if (e->list[i] == l)
            return e->addr[i];
    fail(e, "a pointer table refers to a list in no rule table");
    return 0;
}

static void put_word(enc *e, uint32_t at, uint16_t v)
{
    put(e, false, at, (uint8_t)v);
    put(e, false, at + 1, (uint8_t)(v >> 8));
}

bool fw_image_build(fw_image *img, char *err, size_t errlen)
{
    static enc e;
    memset(img, 0, sizeof *img);
    memset(&e, 0, sizeof e);
    e.img = img;
    e.err = err;
    e.errlen = errlen;
    e.ok = true;

    /* 1. The rule tables, each at its annotated address and exactly its annotated size. */
    for (size_t t = 0; t < fw_rule_table_count; t++) {
        const fw_rule_table *tab = fw_rule_tables[t];
        uint32_t at = tab->addr;
        for (uint16_t i = 0; i < tab->count; i++) {
            if (e.nlists == MAX_LISTS) {
                fail(&e, "more than %d rule lists", MAX_LISTS);
                return false;
            }
            e.list[e.nlists] = tab->lists[i];
            e.addr[e.nlists++] = (uint16_t)at;
            at = put_rule_list(&e, tab->low, at, tab->lists[i]);
        }
        if (at - tab->addr != tab->size)
            fail(&e, "%s: %u bytes, the annotation says %u", tab->name, (unsigned)(at - tab->addr), tab->size);
    }
    /* 2. tn_char_rules_lo/hi: each character's list. */
    for (int c = 0; c < 256; c++) {
        uint16_t a = list_addr(&e, tn_char_lists[c]);
        put(&e, false, TN_CHAR_RULES_LO + (uint32_t)c, (uint8_t)a);
        put(&e, false, TN_CHAR_RULES_HI + (uint32_t)c, (uint8_t)(a >> 8));
    }
    /* 3. lts_pair_index and lts_pairs_a..z ($0000 = no list for the pair). */
    for (int k = 0; k < 26; k++) {
        uint32_t tab = LTS_PAIRS_A + 52u * (uint32_t)k;
        put_word(&e, LTS_PAIR_INDEX + 2u * (uint32_t)k, (uint16_t)tab);
        for (int s = 0; s < 26; s++) {
            const fw_rule_list *l = lts_pair_lists[k][s];
            put_word(&e, tab + 2u * (uint32_t)s, l ? list_addr(&e, l) : 0);
        }
    }
    /* 4. The affix lists of the context symbols % & @, and the stray $00 before them. */
    put(&e, false, UNUSED_9824, 0x00);
    for (size_t i = 0; i < fw_affix_list_count; i++) {
        const fw_affix_list *a = &fw_affix_lists[i];
        uint32_t at = a->addr;
        put(&e, false, at++, 0x00);
        put(&e, false, at++, 0x0D);
        put(&e, false, at++, 0x0A);
        for (uint8_t n = 0; n < a->count; n++) {
            at = put_text(&e, false, at, a->entries[n], false);
            put(&e, false, at++, 0x0D);
            put(&e, false, at++, 0x0A);
        }
        put(&e, false, at, 0x00);
    }
    /* 5. lts_char_class. */
    for (int c = 0; c < 256; c++)
        put(&e, false, LTS_CHAR_CLASS + (uint32_t)c, lts_char_class[c]);
    return e.ok;
}

const fw_image *fw_image_get(void)
{
    static fw_image img;
    static int state;   /* 0 = not built, 1 = built, -1 = failed */
    if (state == 0) {
        char err[160] = "";
        state = fw_image_build(&img, err, sizeof err) ? 1 : -1;
        if (state < 0)
            fprintf(stderr, "firmware data image: %s\n", err);
    }
    return state > 0 ? &img : NULL;
}
```

- [ ] **Step 6: Run the tests**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: `suite_firmware_data ok`, with the message `35214 converted bytes compared with the ROM, 0 differ`. The ranges hold 0x4FF4 + 0x78 + 0x100 + 0x3822 bytes.

Then run `cmd //c ".\build.bat test"`. Expected: all suites ok. The boundary check passes with the data directory included.

- [ ] **Step 7: Commit**

```bash
git add tools/extract_tables.py src/firmware/data tests/test_firmware_data.c tests/suites.h
git commit -m "Converted rule tables, affix lists and character classes as readable C, with the encoder and firmware_data check

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The nine routines in C (spec §3.4)

**Files:**
- Create: `src/firmware/tn_rules.h`, `src/firmware/tn_rules.c`, `src/firmware/lts.h`, `src/firmware/lts.c`, `tests/test_fw_lts.c`
- Modify: `tests/suites.h` (append `SUITE(suite_fw_lts)`)

**Interfaces:**
- Consumes: `fw.h` (Task 4); `fw_data.h`, `fw_image_get` and `tn_char_lists` (Task 5).
- Produces, used by Task 7:

```c
/* tn_rules.h */
#define TN_APPEND_PHONETIC 0xB154
void tn_append_phonetic_rom(fw *f, uint8_t len, uint8_t y);
uint8_t tn_say_char(fw *f, uint8_t c);                       /* $B0C3: returns X = the old tn_pos */
uint8_t tn_rule_skip_text(fw *f, uint8_t *y);                /* $B138: A out, Y in/out */
void tn_rule_skip_field(fw *f, uint8_t a, uint8_t *y);       /* $B142: Y = 0 out */
/* lts.h */
uint8_t lts_affix_next(fw *f, uint8_t *y);                   /* $AF11 */
uint8_t lts_affix_prev(fw *f, uint8_t *y);                   /* $AF1C */
uint8_t lts_sym_vowel(fw *f);                                /* $B007 */
uint8_t lts_match_symbol(fw *f, uint8_t sym, uint8_t y);     /* $B05E: A out (0 = no match); Y preserved */
bool lts_match_list(fw *f);                                  /* $B0E3: true = matched (Z clear); A is $FF */
void lts_convert_char(fw *f, uint8_t c);                     /* $B172 */
```

Each function is a faithful translation of the routine's 740 code. The behaviour notes are in its Ghidra annotation (`ghidra740/annotations/lts.txt`, `text_norm.txt`). Registers become arguments and results. Everything else is RAM, read and written exactly as the ROM does, including scratch `$78`. The code below comes from the disassembly. Keep its order of RAM writes where a later read depends on it.

- [ ] **Step 1: Write the failing tests**

`tests/test_fw_lts.c`:

```c
/* Unit checks of the ported letter-to-sound routines on the converted data, with a mock host whose
   tn_append_phonetic follows the ROM routine ($B154). The full proof is the live differential test (diff_lts). */
#include <string.h>
#include "test.h"
#include "firmware/lts.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static bool g_window_was_on;

static bool mock_rom_call(void *ctx, uint16_t addr, fw_regs *r)
{
    fw *f = ctx;
    if (addr != TN_APPEND_PHONETIC)
        return false;
    if (r->a && fw_rd8(f, CMD_VOICE)) {   /* copy the r->a bytes after (tn_rule_ptr),Y to tn_phonetic */
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

static void mock_window(void *ctx, bool on)
{
    (void)ctx;
    if (on)
        g_window_was_on = true;
}

static void setup(const char *text)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_window_was_on = false;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    size_t n = strlen(text);
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
}

static void test_rule_skips(void)
{
    static const uint8_t rule[] = { 'B', 'A', 0x01, 'C', 'D', 0x02, 'x', 'y', 0xFF };
    setup("");
    memcpy(g_ram + 0x0300, rule, sizeof rule);
    fw_wr16(&g_f, TN_RULE_PTR, 0x0300);
    uint8_t y = 0;
    uint8_t a = tn_rule_skip_text(&g_f, &y);
    CHECK_EQ(a, 1);
    CHECK_EQ(y, 2);
    tn_rule_skip_field(&g_f, a, &y);           /* past the count and the one consumed character */
    CHECK_EQ(y, 0);
    CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), 0x0304);
    CHECK_EQ(g_ram[FW_TMP_78], 3);
    CHECK_EQ(tn_rule_skip_text(&g_f, &y), 2);  /* past "D" to the length */
    CHECK_EQ(y, 1);
}

static void test_match_symbol(void)
{
    setup("CAT ");
    g_ram[LTS_MATCH_DIR] = 1;
    g_ram[LTS_MATCH_POS] = 1;
    CHECK_EQ(lts_match_symbol(&g_f, 'A', 0), 'A');
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);
    CHECK_EQ(lts_match_symbol(&g_f, 'O', 0), 0);
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);         /* a failed letter does not step */
    g_ram[LTS_MATCH_POS] = 1;
    CHECK_EQ(lts_match_symbol(&g_f, '#', 0), 0xFF);   /* one or more vowels: A */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);
    CHECK(lts_match_symbol(&g_f, '!', 0) != 0);       /* a consonant: T */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 3);
    CHECK_EQ(lts_match_symbol(&g_f, ' ', 0), 1);      /* a non-letter: the space */
    CHECK_EQ(lts_match_symbol(&g_f, 'C', 0), 0);      /* at or past the clause end: never */
}

static void test_affix_symbols(void)
{
    setup("WALKING ");
    g_ram[LTS_MATCH_DIR] = 1;
    g_ram[LTS_MATCH_POS] = 4;
    CHECK_EQ(lts_match_symbol(&g_f, '%', 9), 1);   /* the suffix ING */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 7);
    CHECK_EQ(g_ram[FW_TMP_78], 9);                 /* Y saved there, as the ROM does */
    CHECK_EQ(fw_rd16(&g_f, LTS_AFFIX_PTR), LTS_SUFFIXES);

    setup("MATCHES");
    g_ram[LTS_MATCH_DIR] = 0;
    g_ram[LTS_MATCH_POS] = 4;                      /* backwards from the H */
    CHECK_EQ(lts_match_symbol(&g_f, '&', 0), 0x0A);   /* the sibilant CH (SH fails first) */
    CHECK_EQ(g_ram[LTS_MATCH_POS], 2);
}

static void test_say_char(void)
{
    setup("A");
    g_ram[TN_POS] = 5;
    CHECK_EQ(tn_say_char(&g_f, 'A'), 5);
    CHECK_EQ(g_ram[TN_POS], 6);
    const char *name = tn_char_lists['A']->rules[0].output;
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(name));
    CHECK(memcmp(g_ram + TN_PHONETIC, name, strlen(name)) == 0);
    CHECK(!g_f.fault);
}

static void test_convert_char_low_window(void)
{
    int key = -1, second = -1;   /* the first K-Z pair that has a list */
    for (int k = 'K' - 'A'; k < 26 && key < 0; k++)
        for (int s = 0; s < 26 && key < 0; s++)
            if (lts_pair_lists[k][s]) {
                key = k;
                second = s;
            }
    CHECK(key >= 0);
    char text[4] = { (char)('A' + key), (char)('A' + second), ' ', 0 };
    setup(text);
    lts_convert_char(&g_f, (uint8_t)text[0]);
    CHECK(g_window_was_on);                        /* the K-Z lists are read with P2.7 set */
    CHECK_EQ(g_ram[LTS_LOW_BANK] & 1, 1);
    CHECK(g_ram[TN_PHON_LEN] > 0);
    CHECK(!g_f.fault);
}

void suite_fw_lts(void)
{
    CHECK(fw_image_get() != NULL);
    test_rule_skips();
    test_match_symbol();
    test_affix_symbols();
    test_say_char();
    test_convert_char_low_window();
}
```

Append `SUITE(suite_fw_lts)` to `tests/suites.h`.

Run: `cmd //c ".\build.bat test fw_lts"`
Expected: a build failure, because `firmware/lts.h` does not exist.

- [ ] **Step 2: Write `tn_rules.h/.c`**

`src/firmware/tn_rules.h`:

```c
#ifndef FW_TN_RULES_H
#define FW_TN_RULES_H
/* The rule-format helpers of text normalisation (ROM $B0C3-$B171), decompiled (M4.0 spec section 3.4). */
#include "fw.h"

#define TN_APPEND_PHONETIC 0xB154   /* still in ROM (spec 3.3): it also serves cmd_put_phonetic */

/* tn_append_phonetic: A = len at (tn_rule_ptr),Y: the len bytes after it go to tn_phonetic (ROM call). */
void tn_append_phonetic_rom(fw *f, uint8_t len, uint8_t y);
/* $B0C3: append the spoken name of c (the output of the first rule of its list); returns X = the old tn_pos,
   and tn_pos += 1. */
uint8_t tn_say_char(fw *f, uint8_t c);
/* $B138: advance *y past the context bytes (>= $1E) of the rule at tn_rule_ptr; returns that count or
   length byte. */
uint8_t tn_rule_skip_text(fw *f, uint8_t *y);
/* $B142: tn_rule_ptr += *y + 1 + a (past the count or length byte at *y and the a bytes it counts); *y = 0. */
void tn_rule_skip_field(fw *f, uint8_t a, uint8_t *y);

#endif
```

`src/firmware/tn_rules.c`:

```c
#include "tn_rules.h"
#include "data/fw_data.h"

static uint8_t rule_byte(fw *f, uint8_t y) { return fw_ind_y(f, (uint8_t)TN_RULE_PTR, y); }

void tn_append_phonetic_rom(fw *f, uint8_t len, uint8_t y)
{
    fw_regs r = { len, 0, y, 0 };
    fw_rom_call(f, TN_APPEND_PHONETIC, &r);   /* returns A = $FF */
}

uint8_t tn_rule_skip_text(fw *f, uint8_t *y)
{
    uint8_t a;
    while ((a = rule_byte(f, *y)) >= 0x1E)
        (*y)++;
    return a;
}

void tn_rule_skip_field(fw *f, uint8_t a, uint8_t *y)
{
    uint8_t y1 = (uint8_t)(*y + 1);
    fw_wr8(f, FW_TMP_78, y1);
    /* CLC/ADC $78 then CLC/ADC tn_rule_ptr: the 8-bit sum a + y + 1 is added to the 16-bit pointer */
    fw_wr16(f, TN_RULE_PTR, (uint16_t)(fw_rd16(f, TN_RULE_PTR) + (uint8_t)(a + y1)));
    *y = 0;
}

uint8_t tn_say_char(fw *f, uint8_t c)
{
    uint8_t y = 0;
    fw_wr8(f, TN_RULE_PTR, fw_rom8(f, (uint16_t)(TN_CHAR_RULES_LO + c)));
    fw_wr8(f, TN_RULE_PTR + 1, fw_rom8(f, (uint16_t)(TN_CHAR_RULES_HI + c)));
    uint8_t n = tn_rule_skip_text(f, &y);   /* past the left context to the count n */
    tn_rule_skip_field(f, n, &y);           /* past n and the n consumed characters */
    tn_rule_skip_text(f, &y);               /* past the rest of the right context to the length */
    tn_append_phonetic_rom(f, rule_byte(f, y), y);
    uint8_t x = fw_rd8(f, TN_POS);
    fw_wr8(f, TN_POS, (uint8_t)(x + 1));
    return x;
}
```

- [ ] **Step 3: Write `lts.h/.c`**

`src/firmware/lts.h`:

```c
#ifndef FW_LTS_H
#define FW_LTS_H
/* Letter-to-sound (ROM $AF11-$B1E5), decompiled (M4.0 spec section 3.4). The behaviour of each routine is
   described in its annotation in ghidra740/annotations/lts.txt. */
#include "fw.h"

/* $AF11: step *y forwards past the next CR LF of the list at lts_affix_ptr; returns the last byte read
   ($0A, or 0 at the $00 list end, where *y stops). */
uint8_t lts_affix_next(fw *f, uint8_t *y);
/* $AF1C: step *y backwards past the previous CR LF ($0D, or 0 at the $00 list start). */
uint8_t lts_affix_prev(fw *f, uint8_t *y);
/* $B007: A = lts_char_class bit 6 of cmd_text[lts_match_pos]; lts_match_pos steps. */
uint8_t lts_sym_vowel(fw *f);
/* $B05E: match one rule context byte; returns 0 for no match. y (the rule index, preserved) is only read by
   the literal symbol and saved to $78 by the affix symbols. */
uint8_t lts_match_symbol(fw *f, uint8_t sym, uint8_t y);
/* $B0E3: try the rules of the list at tn_rule_ptr; true when one matched and its output was appended. */
bool lts_match_list(fw *f);
/* $B172: convert the character c at tn_key_pos. */
void lts_convert_char(fw *f, uint8_t c);

#endif
```

`src/firmware/lts.c`:

```c
#include "lts.h"
#include "tn_rules.h"
#include "data/fw_data.h"

static uint8_t text_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(CMD_TEXT + x)); }
static uint8_t char_class(fw *f, uint8_t c) { return fw_rom8(f, (uint16_t)(LTS_CHAR_CLASS + c)); }
static uint8_t affix_byte(fw *f, uint8_t y) { return fw_ind_y(f, (uint8_t)LTS_AFFIX_PTR, y); }
static uint8_t rule_byte(fw *f, uint8_t y) { return fw_ind_y(f, (uint8_t)TN_RULE_PTR, y); }
static uint8_t clause_len(const fw *f) { return fw_rd8(f, BOOT_CLAUSE_LEN); }

/* INC or DEC lts_match_pos by lts_match_dir bit 0 (1 = forwards, for the right context) */
static void step_match_pos(fw *f)
{
    uint8_t p = fw_rd8(f, LTS_MATCH_POS);
    fw_wr8(f, LTS_MATCH_POS, (uint8_t)((fw_rd8(f, LTS_MATCH_DIR) & 1) ? p + 1 : p - 1));
}

uint8_t lts_affix_next(fw *f, uint8_t *y)
{
    uint8_t a;
    do {
        (*y)++;
        a = affix_byte(f, *y);
        if (a == 0)
            return 0;
    } while (a != 0x0A);
    (*y)++;
    return a;
}

uint8_t lts_affix_prev(fw *f, uint8_t *y)
{
    uint8_t a;
    do {
        (*y)--;
        a = affix_byte(f, *y);
        if (a == 0)
            return 0;
    } while (a != 0x0D);
    (*y)--;
    return a;
}

uint8_t lts_sym_vowel(fw *f)
{
    uint8_t a = char_class(f, text_at(f, fw_rd8(f, LTS_MATCH_POS))) & CC_VOWEL;
    step_match_pos(f);
    return a;
}

/* . ! + : one character whose class has the bit; A = the masked class (0 = no) */
static uint8_t sym_class(fw *f, uint8_t bit)
{
    uint8_t a = char_class(f, text_at(f, fw_rd8(f, LTS_MATCH_POS))) & bit;
    step_match_pos(f);
    return a;
}

/* space: one character that is not a letter */
static uint8_t sym_nonletter(fw *f)
{
    uint8_t a = (char_class(f, text_at(f, fw_rd8(f, LTS_MATCH_POS))) & CC_LETTER) ? 0 : 1;
    step_match_pos(f);
    return a;
}

/* #: one vowel, then any further vowels inside the clause */
static uint8_t sym_vowels(fw *f)
{
    if (lts_sym_vowel(f) == 0)
        return 0;
    for (;;) {
        uint8_t x = fw_rd8(f, LTS_MATCH_POS);
        if (x >= clause_len(f) || !(char_class(f, text_at(f, x)) & CC_VOWEL))
            return 0xFF;
        step_match_pos(f);
    }
}

/* :  zero or more consonants inside the clause */
static uint8_t sym_consonants(fw *f)
{
    for (;;) {
        uint8_t x = fw_rd8(f, LTS_MATCH_POS);
        if (x >= clause_len(f) || !(char_class(f, text_at(f, x)) & CC_CONSONANT))
            return 0xFF;
        step_match_pos(f);
    }
}

/* any other byte: it must equal the text (the ROM reads it again from the rule) */
static uint8_t sym_literal(fw *f, uint8_t y)
{
    uint8_t a = rule_byte(f, y);
    if (a != text_at(f, fw_rd8(f, LTS_MATCH_POS)))
        a = 0;
    step_match_pos(f);
    return a;
}

/* One suffix entry at *y against the text from *x: 1 = matched, 0 = the whole symbol fails (the list end, or
   a space entry before a letter), -1 = this entry does not match. */
static int suffix_entry(fw *f, uint8_t *y, uint8_t *x)
{
    for (;;) {
        uint8_t a = affix_byte(f, *y);
        if (a == 0)
            return 0;
        if (a == 0x0D)
            return 1;
        if (a == ' ')   /* needs a non-letter, which is tested but not consumed */
            return (char_class(f, text_at(f, *x)) & CC_LETTER) ? 0 : 1;
        if (a != text_at(f, *x))
            return -1;
        (*x)++;
        if (*x >= clause_len(f))
            return -1;
        (*y)++;
    }
}

/* %: a suffix of lts_suffixes forwards from lts_match_pos */
static uint8_t sym_suffix(fw *f, uint8_t y_saved)
{
    uint8_t y = 3;   /* the first entry: past the list's $00 and CR LF */
    fw_wr16(f, LTS_AFFIX_PTR, LTS_SUFFIXES);
    fw_wr8(f, FW_TMP_78, y_saved);
    for (;;) {
        uint8_t x = fw_rd8(f, LTS_MATCH_POS);
        int r = suffix_entry(f, &y, &x);
        if (r == 0)
            return 0;
        if (r == 1) {
            fw_wr8(f, LTS_MATCH_POS, x);
            return 1;
        }
        lts_affix_next(f, &y);
    }
}

/* & and @: the entries of a list matched backwards from y (the last character of its last entry) */
static uint8_t affix_match_back(fw *f, uint16_t list, uint8_t y, uint8_t y_saved)
{
    fw_wr16(f, LTS_AFFIX_PTR, list);
    fw_wr8(f, FW_TMP_78, y_saved);
    for (;;) {
        uint8_t x = fw_rd8(f, LTS_MATCH_POS);
        for (;;) {
            uint8_t a = affix_byte(f, y);
            if (a == 0)
                return 0;              /* the list start: nothing matched */
            if (a == 0x0A) {           /* an entry matched completely */
                fw_wr8(f, LTS_MATCH_POS, x);
                return a;
            }
            if (a != text_at(f, x))
                break;
            x--;
            y--;
        }
        lts_affix_prev(f, &y);
    }
}

uint8_t lts_match_symbol(fw *f, uint8_t sym, uint8_t y)
{
    uint8_t pos = fw_rd8(f, LTS_MATCH_POS);
    if (pos >= clause_len(f))
        return 0;
    if (char_class(f, sym) & CC_LETTER) {   /* a letter must equal the text */
        if (sym != text_at(f, pos))
            return 0;
        step_match_pos(f);
        return sym;
    }
    switch (sym) {
    case ' ': return sym_nonletter(f);
    case '#': return sym_vowels(f);
    case '.': return sym_class(f, CC_VOICED);
    case '%': return sym_suffix(f, y);
    case '&': return affix_match_back(f, LTS_SIBILANTS, 0x1F, y);       /* from the H of SH */
    case '@': return affix_match_back(f, LTS_AT_CONSONANTS, 0x14, y);   /* from the H of SH */
    case '!': return sym_class(f, CC_CONSONANT);
    case '+': return sym_class(f, CC_FRONT_VOWEL);
    case ':': return sym_consonants(f);
    default: return sym_literal(f, y);
    }
}

bool lts_match_list(fw *f)
{
    for (;;) {
        uint8_t y = 0, a;
        bool ok = true;
        fw_wr8(f, LTS_MATCH_POS, (uint8_t)(fw_rd8(f, TN_KEY_POS) - 1));
        fw_wr8(f, LTS_MATCH_DIR, 0);
        while ((a = rule_byte(f, y)) >= 0x1E) {   /* the left context, backwards */
            if (lts_match_symbol(f, a, y) == 0) {
                ok = false;
                break;
            }
            y++;
        }
        if (!ok) {
            a = tn_rule_skip_text(f, &y);         /* the rest of the left context */
            tn_rule_skip_field(f, a, &y);         /* the count and the consumed characters */
        } else {
            uint8_t pos = (uint8_t)(fw_rd8(f, TN_KEY_POS) + 1);
            fw_wr8(f, LTS_MATCH_DIR, 1);
            if (fw_rd8(f, LTS_KEY_NEXT))
                pos++;                            /* a two-letter key: the right context starts after both */
            fw_wr8(f, LTS_MATCH_POS, pos);
            a = rule_byte(f, y++);                /* the count n */
            fw_wr8(f, TN_POS, (uint8_t)(a + pos));   /* set now: it stays even when the right context fails */
            while ((a = rule_byte(f, y)) >= 0x1E) {  /* the right context, forwards */
                if (lts_match_symbol(f, a, y) == 0) {
                    ok = false;
                    break;
                }
                y++;
            }
            if (ok) {
                tn_append_phonetic_rom(f, a, y);  /* a = the output length (the ROM jumps there) */
                return true;
            }
        }
        a = tn_rule_skip_text(f, &y);             /* the rest of the right context */
        tn_rule_skip_field(f, a, &y);             /* the length and the output */
        if (rule_byte(f, y) == 0xFF)
            return false;
    }
}

/* the two-letter list of c and the next character; false when there is none or no rule matched */
static bool two_letter(fw *f, uint8_t c)
{
    uint8_t y = (uint8_t)((c - 'A') * 2);
    fw_wr8(f, LTS_PAIR_TABLE, fw_rom8(f, (uint16_t)(LTS_PAIR_INDEX + y)));
    fw_wr8(f, LTS_PAIR_TABLE + 1, fw_rom8(f, (uint16_t)(LTS_PAIR_INDEX + y + 1)));
    uint8_t next = fw_rd8(f, (uint16_t)(CMD_TEXT + 1 + fw_rd8(f, TN_KEY_POS)));
    fw_wr8(f, LTS_KEY_NEXT, next);
    if (next < 'A' || next > 'Z')
        return false;
    y = (uint8_t)((next - 'A') * 2);
    fw_wr8(f, TN_RULE_PTR, fw_ind_y(f, (uint8_t)LTS_PAIR_TABLE, y));
    uint8_t hi = fw_ind_y(f, (uint8_t)LTS_PAIR_TABLE, (uint8_t)(y + 1));
    if (hi == 0)
        return false;   /* no list for this pair; the low byte is already stored, as in the ROM */
    fw_wr8(f, TN_RULE_PTR + 1, hi);
    if (fw_rd8(f, LTS_LOW_BANK) & 1)
        fw_set_low_window(f, true);   /* SEB7 P2: the K-Z lists are in the LOW overlay */
    return lts_match_list(f);
}

void lts_convert_char(fw *f, uint8_t c)
{
    fw_wr8(f, LTS_LOW_BANK, (uint8_t)(fw_rd8(f, LTS_LOW_BANK) & ~1));
    if ((fw_rd8(f, CMD_SPELL) & 1) && fw_rd8(f, TN_KEY_POS) != 0) {   /* spell mode: the character's name */
        tn_say_char(f, c);
        return;
    }
    fw_wr8(f, LTS_KEY_CHAR, c);
    if (c >= 'A') {
        if (c >= 'K')   /* also the codes after Z, which go on to their one-character list */
            fw_wr8(f, LTS_LOW_BANK, (uint8_t)(fw_rd8(f, LTS_LOW_BANK) | 1));
        if (c <= 'Z' && two_letter(f, c))
            return;
    }
    fw_set_low_window(f, false);      /* CLB7 P2 */
    uint8_t k = text_at(f, fw_rd8(f, TN_KEY_POS));
    fw_wr8(f, LTS_KEY_CHAR, k);
    fw_wr8(f, LTS_KEY_NEXT, 0);
    fw_wr8(f, TN_RULE_PTR, fw_rom8(f, (uint16_t)(TN_CHAR_RULES_LO + k)));
    fw_wr8(f, TN_RULE_PTR + 1, fw_rom8(f, (uint16_t)(TN_CHAR_RULES_HI + k)));
    lts_match_list(f);
}
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_lts"`
Expected: `suite_fw_lts ok`.

If a check fails, compare the C against the routine's disassembly before changing a test. Run `cmd //c ghidra740\dump.bat`; the file is `build/ghidra740/dump/rom_<ADDR>_<name>.txt`. Then run `cmd //c ".\build.bat test"`. Expected: all suites ok, and the firmware boundary check passes.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/tn_rules.h src/firmware/tn_rules.c src/firmware/lts.h src/firmware/lts.c tests/test_fw_lts.c tests/suites.h
git commit -m "Letter-to-sound in C: the nine pilot routines on the firmware RAM image (M4.0 3.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Thunks, the bridge and the live differential test (spec §3.2, §5.1)

**Files:**
- Modify: `src/engine/rom_engine.h`, `src/engine/rom_engine.c`, `tests/suites.h` (append `SUITE(suite_diff_lts)`)
- Create: `src/engine/thunks.h`, `src/engine/thunks.c`, `tests/test_diff_lts.c`

**Interfaces:**
- Consumes: Task 3's machine API, Task 4's `fw`, Task 5's `fw_image_get`, Task 6's routines.
- Produces, used by Task 8:

```c
/* rom_engine.h */
typedef void (*engine_setup_fn)(machine *m, void *user);
machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                              engine_setup_fn setup, void *user, const char *text, bool include_boot,
                              frame_list *produced, frame_list *raw);
/* thunks.h */
#define FW_ROM_CALL_BUDGET 10000000ULL
enum { FW_OUT_A = 0x01, FW_OUT_X = 0x02, FW_OUT_Y = 0x04, FW_OUT_Z = 0x08 };
typedef struct fw_bridge { machine *m; fw f; fw_host host; } fw_bridge;
typedef struct { const char *name; uint16_t addr; uint8_t outputs; machine_hook_fn thunk; } fw_replacement;
extern const fw_replacement fw_replacements[];
extern const size_t fw_replacement_count;
bool fw_bridge_init(fw_bridge *b, machine *m);
const fw_replacement *fw_replacement_find(const char *name);
bool fw_install(machine *m, fw_bridge *b, const char *only);
```

- [ ] **Step 1: Give the ROM engine a setup callback**

In `src/engine/rom_engine.h`, add before `rom_engine_speak`:

```c
/* Called once after machine_init, before boot: installs hooks (hybrid engine, differential tests). */
typedef void (*engine_setup_fn)(machine *m, void *user);

/* The engine run: boot to idle, then feed text + CR and run to idle again. setup (may be NULL) runs after
   machine_init. produced gets the produced frames (the boot frames too if include_boot); raw, when not NULL,
   the raw DSP-port frames of the speaking phase. The rom_engine_speak* functions below are this with
   setup = NULL. */
machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                              engine_setup_fn setup, void *user, const char *text, bool include_boot,
                              frame_list *produced, frame_list *raw);
```

In `rom_engine.c`, rename the static `speak` to `rom_engine_run` with the new parameters (`setup` and `user` after `cfg`), and call `if (setup) setup(m, user);` right after the successful `machine_init`. Then update the two callers:
- `rom_engine_speak_cfg` becomes `return rom_engine_run(m, rom, len, cfg, NULL, NULL, text, include_boot, out, NULL);`
- `rom_engine_speak_streams` becomes `return rom_engine_run(m, rom, len, cfg, NULL, NULL, text, false, produced, raw);`

Run: `cmd //c ".\build.bat test golden"`
Expected: ok, with nothing changed.

- [ ] **Step 2: Write the failing differential test**

`tests/test_diff_lts.c`:

```c
/* Live differential test of the ported routines (M4.0 spec section 5.1). Every corpus file runs on the ROM
   engine; at each entry of a ported routine (sampled) the machine is cloned twice: scratch A runs the ROM
   routine and scratch B its C thunk, both with interrupts held. They must agree on the exit PC and SP, the
   routine's declared outputs, the P2 latch and all RAM except the SFRs and the dead stack below the exit SP. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"

#define DIFF_FULL_CALLS 1000      /* every call up to this many per routine and corpus file ... */
#define DIFF_STRIDE     64        /* ... then every 64th */
#define DIFF_BUDGET     10000000ULL
#define DIFF_MAX        16

static const char *const k_corpus[] = { CORPUS_NAMES };
/* Extra text, for this test only (not goldens): words whose rules use the % & @ context symbols. */
static const char *const k_extra[] = {
    "Judges ruled the churches; loose rules, sugar, the nation's fixes, washing, rejoicing and lunches.",
};

typedef struct { const fw_replacement *r; unsigned calls; } observer;

static machine g_m, g_ref, g_c;
static observer g_obs[DIFF_MAX];
static unsigned g_checked[DIFF_MAX], g_mismatches;
static const char *g_source;

static bool compare(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n)
{
    if (a->cpu.pc != b->cpu.pc || a->cpu.s != b->cpu.s) {
        snprintf(why, n, "exit PC/S: ROM %04X/%02X, C %04X/%02X", a->cpu.pc, a->cpu.s, b->cpu.pc, b->cpu.s);
        return false;
    }
    if ((outputs & FW_OUT_A) && a->cpu.a != b->cpu.a) {
        snprintf(why, n, "A: ROM %02X, C %02X", a->cpu.a, b->cpu.a);
        return false;
    }
    if ((outputs & FW_OUT_X) && a->cpu.x != b->cpu.x) {
        snprintf(why, n, "X: ROM %02X, C %02X", a->cpu.x, b->cpu.x);
        return false;
    }
    if ((outputs & FW_OUT_Y) && a->cpu.y != b->cpu.y) {
        snprintf(why, n, "Y: ROM %02X, C %02X", a->cpu.y, b->cpu.y);
        return false;
    }
    if ((outputs & FW_OUT_Z) && ((a->cpu.p ^ b->cpu.p) & CPU740_Z)) {
        snprintf(why, n, "Z: ROM %d, C %d", (a->cpu.p & CPU740_Z) != 0, (b->cpu.p & CPU740_Z) != 0);
        return false;
    }
    if (a->sfr.port_latch[2] != b->sfr.port_latch[2]) {
        snprintf(why, n, "P2 latch: ROM %02X, C %02X", a->sfr.port_latch[2], b->sfr.port_latch[2]);
        return false;
    }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
            continue;
        if (addr >= 0x100 && addr <= 0x100u + a->cpu.s)
            continue;   /* the dead stack below the exit SP */
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return false;
        }
    }
    return true;
}

static bool observe(machine *m, void *user)
{
    observer *o = user;
    unsigned call = o->calls++;
    if (call >= DIFF_FULL_CALLS && call % DIFF_STRIDE != 0)
        return false;
    g_checked[o - g_obs]++;
    machine_clone(&g_ref, m);
    machine_hold_irqs(&g_ref, true);
    machine_clone(&g_c, m);
    machine_hold_irqs(&g_c, true);
    char why[160] = "";
    fw_bridge b;
    if (!machine_finish_routine(&g_ref, DIFF_BUDGET)) {
        snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_ref.fault);
    } else if (!fw_bridge_init(&b, &g_c)) {
        snprintf(why, sizeof why, "no firmware data image");
    } else {
        o->r->thunk(&g_c, &b);
        if (g_c.fault != MFAULT_NONE)
            snprintf(why, sizeof why, "the C faulted (fault %d at $%04X)", (int)g_c.fault, g_c.fault_addr);
        else
            compare(&g_ref, &g_c, o->r->outputs, why, sizeof why);
    }
    if (why[0] && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s call %u (A=%02X X=%02X Y=%02X): %s\n", g_source, o->r->name, call, m->cpu.a,
                m->cpu.x, m->cpu.y, why);
    return false;   /* the ROM routine runs on in m */
}

static void install(machine *m, void *user)
{
    (void)user;
    for (size_t i = 0; i < fw_replacement_count; i++) {
        g_obs[i].r = &fw_replacements[i];
        g_obs[i].calls = 0;   /* the full-call budget is per corpus file */
        CHECK(machine_hook(m, fw_replacements[i].addr, observe, &g_obs[i]));
    }
}

static void run(const uint8_t *rom, size_t len, const char *source, const char *text, bool boot)
{
    frame_list fl = { 0 };
    g_source = source;
    machine_status st = rom_engine_run(&g_m, rom, len, NULL, install, NULL, text, boot, &fl, NULL);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    machine_clear_hooks(&g_m);
    frame_list_free(&fl);
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void suite_diff_lts(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(fw_replacement_count <= DIFF_MAX);
    memset(g_checked, 0, sizeof g_checked);
    g_mismatches = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        run(rom, len, k_corpus[c], text, false);
        free(text);
    }
    run(rom, len, "boot", "", true);
    for (size_t i = 0; i < sizeof k_extra / sizeof k_extra[0]; i++)
        run(rom, len, "extra", k_extra[i], false);
    for (size_t i = 0; i < fw_replacement_count; i++) {
        printf("  %-20s %u calls checked\n", fw_replacements[i].name, g_checked[i]);
        CHECK(g_checked[i] > 0);   /* every ported routine is exercised */
    }
    CHECK_EQ(g_mismatches, 0);
}
```

Append `SUITE(suite_diff_lts)` to `tests/suites.h`.

Run: `cmd //c ".\build.bat test diff_lts"`
Expected: a build failure, because `engine/thunks.h` does not exist.

- [ ] **Step 3: Write the thunks and the bridge**

`src/engine/thunks.h`:

```c
#ifndef ENGINE_THUNKS_H
#define ENGINE_THUNKS_H
/* The replacement table: ROM routines that run as C in the hybrid engine (M4.0 spec section 3.2). Each thunk
   maps the routine's registers to its C function, emulates the RTS and takes no emulated time. */
#include "machine/machine.h"
#include "firmware/fw.h"

#define FW_ROM_CALL_BUDGET 10000000ULL   /* cycles a ROM routine called from C may take (spec 3.3) */

/* The registers and flags a routine's callers use: the thunk sets them, the differential test compares them. */
enum { FW_OUT_A = 0x01, FW_OUT_X = 0x02, FW_OUT_Y = 0x04, FW_OUT_Z = 0x08 };

/* The firmware context of one machine: its RAM, the converted data image, the emulator as host. */
typedef struct fw_bridge { machine *m; fw f; fw_host host; } fw_bridge;

typedef struct {
    const char *name;
    uint16_t addr;
    uint8_t outputs;          /* FW_OUT_* */
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
} fw_replacement;

extern const fw_replacement fw_replacements[];
extern const size_t fw_replacement_count;

/* Binds b to m. False when the data image does not build (the reason is printed). */
bool fw_bridge_init(fw_bridge *b, machine *m);
const fw_replacement *fw_replacement_find(const char *name);
/* Hooks every replacement into m with user b, or only the one named only (NULL = all). False if only names
   none or a hook cannot be added. */
bool fw_install(machine *m, fw_bridge *b, const char *only);

#endif
```

`src/engine/thunks.c`:

```c
#include <string.h>
#include "engine/thunks.h"
#include "firmware/lts.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

#ifdef _MSC_VER
#pragma warning(disable : 4611)   /* setjmp with no C++ objects to destroy: the trap of fw faults */
#endif

#define P2_LOWER_WINDOW 0x80

static bool window_on(const machine *m) { return (m50734_port_pins(&m->sfr, 2) & P2_LOWER_WINDOW) != 0; }

static bool host_rom_call(void *ctx, uint16_t addr, fw_regs *r)
{
    fw_bridge *b = ctx;
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    bool ok = machine_rom_call(b->m, addr, &mr, FW_ROM_CALL_BUDGET);
    r->a = mr.a;
    r->x = mr.x;
    r->y = mr.y;
    r->p = mr.p;
    b->f.low_window = window_on(b->m);
    return ok;
}

/* SEB7/CLB7 $F0: a read-modify-write of P2 through the bus, as the CPU does it */
static void host_set_low_window(void *ctx, bool on)
{
    machine *m = ((fw_bridge *)ctx)->m;
    uint8_t v = m->bus.read(m->bus.ctx, M50734_P2);
    m->bus.write(m->bus.ctx, M50734_P2, (uint8_t)(on ? v | P2_LOWER_WINDOW : v & ~P2_LOWER_WINDOW));
}

bool fw_bridge_init(fw_bridge *b, machine *m)
{
    const fw_image *img = fw_image_get();
    b->m = m;
    b->host.ctx = b;
    b->host.rom_call = host_rom_call;
    b->host.set_low_window = host_set_low_window;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
}

/* Thunk frame: set the fault trap (saving an outer thunk's), sync the window; leave: restore the trap, then
   either report the fault or emulate the RTS. */
static fw *enter(fw_bridge *b, jmp_buf *trap, jmp_buf **saved)
{
    *saved = b->f.trap;
    b->f.trap = trap;
    b->f.low_window = window_on(b->m);
    return &b->f;
}

static bool leave(fw_bridge *b, jmp_buf *saved)
{
    machine *m = b->m;
    b->f.trap = saved;
    if (b->f.fault) {
        if (m->fault == MFAULT_NONE) {
            m->fault = MFAULT_FIRMWARE;
            m->fault_addr = b->f.fault_addr;
        }
        return true;
    }
    machine_return(m);
    return true;
}

static void set_z(machine *m, bool z) { m->cpu.p = (uint8_t)(z ? m->cpu.p | CPU740_Z : m->cpu.p & ~CPU740_Z); }

/* lts_affix_next $AF11: Y in; A, Y out */
static bool t_lts_affix_next(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t y = m->cpu.y;
        m->cpu.a = lts_affix_next(f, &y);
        m->cpu.y = y;
    }
    return leave(user, saved);
}

/* lts_affix_prev $AF1C: Y in; A, Y out */
static bool t_lts_affix_prev(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t y = m->cpu.y;
        m->cpu.a = lts_affix_prev(f, &y);
        m->cpu.y = y;
    }
    return leave(user, saved);
}

/* lts_sym_vowel $B007: A out */
static bool t_lts_sym_vowel(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0)
        m->cpu.a = lts_sym_vowel(f);
    return leave(user, saved);
}

/* lts_match_symbol $B05E: A, Y in; A out, Y preserved */
static bool t_lts_match_symbol(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t a = m->cpu.a, y = m->cpu.y;
        m->cpu.a = lts_match_symbol(f, a, y);
        m->cpu.y = y;
    }
    return leave(user, saved);
}

/* tn_say_char $B0C3: A in; X out */
static bool t_tn_say_char(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t c = m->cpu.a;
        m->cpu.x = tn_say_char(f, c);
    }
    return leave(user, saved);
}

/* lts_match_list $B0E3: A = $FF and Z (set = no rule matched) out */
static bool t_lts_match_list(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        bool matched = lts_match_list(f);
        m->cpu.a = 0xFF;
        set_z(m, !matched);
    }
    return leave(user, saved);
}

/* tn_rule_skip_text $B138: Y in; A, Y out */
static bool t_tn_rule_skip_text(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t y = m->cpu.y;
        m->cpu.a = tn_rule_skip_text(f, &y);
        m->cpu.y = y;
    }
    return leave(user, saved);
}

/* tn_rule_skip_field $B142: A, Y in; Y (= 0) out */
static bool t_tn_rule_skip_field(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t a = m->cpu.a, y = m->cpu.y;
        tn_rule_skip_field(f, a, &y);
        m->cpu.y = y;
    }
    return leave(user, saved);
}

/* lts_convert_char $B172: A in; no register output (tn_convert_clause clears P2.7 and reloads X) */
static bool t_lts_convert_char(machine *m, void *user)
{
    jmp_buf trap, *saved;
    fw *f = enter(user, &trap, &saved);
    if (setjmp(trap) == 0) {
        uint8_t c = m->cpu.a;
        lts_convert_char(f, c);
    }
    return leave(user, saved);
}

const fw_replacement fw_replacements[] = {
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
    { "lts_affix_prev",     0xAF1C, FW_OUT_A | FW_OUT_Y, t_lts_affix_prev },
    { "lts_sym_vowel",      0xB007, FW_OUT_A,            t_lts_sym_vowel },
    { "lts_match_symbol",   0xB05E, FW_OUT_A | FW_OUT_Y, t_lts_match_symbol },
    { "tn_say_char",        0xB0C3, FW_OUT_X,            t_tn_say_char },
    { "lts_match_list",     0xB0E3, FW_OUT_A | FW_OUT_Z, t_lts_match_list },
    { "tn_rule_skip_text",  0xB138, FW_OUT_A | FW_OUT_Y, t_tn_rule_skip_text },
    { "tn_rule_skip_field", 0xB142, FW_OUT_Y,            t_tn_rule_skip_field },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
};
const size_t fw_replacement_count = sizeof fw_replacements / sizeof fw_replacements[0];

const fw_replacement *fw_replacement_find(const char *name)
{
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (!strcmp(fw_replacements[i].name, name))
            return &fw_replacements[i];
    return NULL;
}

bool fw_install(machine *m, fw_bridge *b, const char *only)
{
    if (only) {
        const fw_replacement *r = fw_replacement_find(only);
        return r && machine_hook(m, r->addr, r->thunk, b);
    }
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (!machine_hook(m, fw_replacements[i].addr, fw_replacements[i].thunk, b))
            return false;
    return true;
}
```

Every thunk reads its input registers into locals **before** calling C. A ROM call made from inside the C overwrites the machine's registers.

- [ ] **Step 4: Run the differential test**

Run: `cmd //c ".\build.bat test diff_lts"`
Expected: `suite_diff_lts ok`. Each of the nine routines shows a non-zero `calls checked`, and there are 0 mismatches.

**On a mismatch:**
- The report names the corpus, the routine, the call number, the entry registers and the first difference. Fix the C, not the test. Re-read the routine's disassembly in `build/ghidra740/dump/` (`cmd //c ghidra740\dump.bat`).
- A RAM difference in `$78` or in a pointer usually means an order-of-writes or 8-bit-wrap slip.

**If a routine shows 0 calls checked**, the corpus does not exercise it. Add a few words to `k_extra` that use its context symbol: grep the converted data for rules whose left or right context contains `&`, `@` or `%`.

- [ ] **Step 5: Run everything and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all suites ok, goldens unchanged.

```bash
git add src/engine/rom_engine.h src/engine/rom_engine.c src/engine/thunks.h src/engine/thunks.c tests/test_diff_lts.c tests/suites.h
git commit -m "Thunks, the fw bridge and the live differential test: the nine routines match the ROM at every sampled call

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Hybrid engine, `--engine hybrid` and the hybrid goldens (spec §5.2, criterion 8)

**Files:**
- Create: `src/engine/hybrid_engine.h`, `src/engine/hybrid_engine.c`, `tests/test_golden_hybrid.c`
- Modify: `src/render/render.h`, `src/render/render.c`, `src/apps/apollo2.c`, `tests/suites.h` (append `SUITE(suite_golden_hybrid)`)

**Interfaces:**
- Consumes: `rom_engine_run`, `fw_bridge_init`, `fw_install`, `fw_replacements` (Task 7).
- Produces: `hybrid_engine_speak(...)` (below), `render_options.engine` (`RENDER_ENGINE_ROM`, `RENDER_ENGINE_HYBRID`), and `apollo2 --engine hybrid`.

- [ ] **Step 1: Write the failing test**

`tests/test_golden_hybrid.c`:

```c
/* The hybrid engine reproduces every golden (M4.0 spec section 5.2): with all ported routines active, and with
   each alone. The rendered audio of hybrid equals the rom engine's (criterion 8). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "render/render.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
static machine g_m;

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* Compares the hybrid frames of one corpus file with its golden. */
static void check(const uint8_t *rom, size_t len, const char *only, const char *name, const char *text, bool boot)
{
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return;
    frame_list fl = { 0 };
    machine_status st = hybrid_engine_speak(&g_m, rom, len, NULL, only, text, boot, &fl, NULL);
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
}

static void check_corpus(const uint8_t *rom, size_t len, const char *only)
{
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        check(rom, len, only, k_corpus[c], text, false);
        free(text);
    }
    check(rom, len, only, "boot", "", true);
}

static void test_render_matches_rom(const uint8_t *rom, size_t len)
{
    render_options opt;
    render_result a, b;
    render_default_options(&opt);
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &a), MACHINE_IDLE);
    opt.engine = RENDER_ENGINE_HYBRID;
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &b), MACHINE_IDLE);
    CHECK(a.count > 0);
    CHECK_EQ(a.count, b.count);
    CHECK(a.count == b.count && memcmp(a.samples, b.samples, a.count * sizeof a.samples[0]) == 0);
    render_result_free(&a);
    render_result_free(&b);
}

void suite_golden_hybrid(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    check_corpus(rom, len, NULL);
    for (size_t i = 0; i < fw_replacement_count; i++)
        check_corpus(rom, len, fw_replacements[i].name);
    printf("  %zu corpus files x %zu modes (all, then each routine alone)\n",
           sizeof k_corpus / sizeof k_corpus[0] + 1, fw_replacement_count + 1);
    CHECK_EQ(hybrid_engine_speak(&g_m, rom, len, NULL, "no_such_routine", "hello", false, NULL, NULL),
             MACHINE_FAULT);
    test_render_matches_rom(rom, len);
}
```

Append `SUITE(suite_golden_hybrid)` to `tests/suites.h`.

- [ ] **Step 2: Write the hybrid engine**

`src/engine/hybrid_engine.h`:

```c
#ifndef HYBRID_ENGINE_H
#define HYBRID_ENGINE_H
/* The "hybrid" engine (spec section 3; M4.0 spec section 3): the rom engine with the routines of the
   replacement table (src/engine/thunks.c) running as C. */
#include "engine/rom_engine.h"

/* rom_engine_run with every replacement active, or only the one named only. MACHINE_FAULT (MFAULT_FIRMWARE)
   when the data image does not build or only names no replacement. produced may be NULL (frames dropped). */
machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw);

#endif
```

`src/engine/hybrid_engine.c`:

```c
#include <string.h>
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"

typedef struct {
    fw_bridge bridge;
    const char *only;
    bool ok;
} hybrid_setup;

static void setup(machine *m, void *user)
{
    hybrid_setup *h = user;
    h->ok = fw_bridge_init(&h->bridge, m) && fw_install(m, &h->bridge, h->only);
    if (!h->ok)
        m->fault = MFAULT_FIRMWARE;
}

machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw)
{
    hybrid_setup h;
    frame_list scratch = { 0 };
    memset(&h, 0, sizeof h);
    h.only = only;
    machine_status st = rom_engine_run(m, rom, len, cfg, setup, &h, text, include_boot,
                                       produced ? produced : &scratch, raw);
    machine_clear_hooks(m);   /* the bridge lives on this stack frame */
    frame_list_free(&scratch);
    return h.ok ? st : MACHINE_FAULT;
}
```

- [ ] **Step 3: Select the engine in render and the CLI**

In `src/render/render.h`, add `#include "engine/hybrid_engine.h"` and, before `render_options`, add:

```c
typedef enum {
    RENDER_ENGINE_ROM,        /* the original firmware, emulated (default) */
    RENDER_ENGINE_HYBRID,     /* the same, with the ported routines running as C (M4) */
} render_engine;
```

Add the field `render_engine engine;` to `render_options`, after `stream`. `render_default_options` already zeroes it, which is `RENDER_ENGINE_ROM`.

In `src/render/render.c`, replace the `rom_engine_speak_streams(...)` call with:

```c
    frame_list *raw_out = opt->stream == RENDER_STREAM_RAW ? &raw : NULL;
    machine_status st = opt->engine == RENDER_ENGINE_HYBRID
        ? hybrid_engine_speak(m, rom, len, NULL, NULL, input, false, &produced, raw_out)
        : rom_engine_speak_streams(m, rom, len, NULL, input, &produced, raw_out);
```

Update the comment above `render_text` in `render.h` to say "through the chosen engine".

In `src/apps/apollo2.c`, replace the `--engine` branch with:

```c
        } else if (!strcmp(a, "--engine") && val) {
            if (!strcmp(val, "rom"))
                opt.engine = RENDER_ENGINE_ROM;
            else if (!strcmp(val, "hybrid"))
                opt.engine = RENDER_ENGINE_HYBRID;
            else {
                fprintf(stderr, "engine '%s' is not available: use rom or hybrid (native arrives in milestone 5)\n", val);
                return 1;
            }
            i++;
```

Change the usage line to: `"  --engine E         firmware engine: rom (default) or hybrid (ported routines in C)\n"`.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test golden_hybrid"`
Expected: `suite_golden_hybrid ok`: 26 corpus files × 10 modes. This takes about a minute.

Then run `cmd //c ".\build.bat test"`. Expected: all suites ok.

Then check criterion 8. Always pass `-o`:

```bash
./build/apollo2.exe --engine hybrid "Hello world." -o build/hybrid_hello.wav; echo "exit $?"
./build/apollo2.exe "Hello world." -o build/rom_hello.wav; cmp build/hybrid_hello.wav build/rom_hello.wav && echo identical
```

Expected: `exit 0` and `identical`.

- [ ] **Step 5: Commit**

```bash
git add src/engine/hybrid_engine.h src/engine/hybrid_engine.c src/render/render.h src/render/render.c src/apps/apollo2.c tests/test_golden_hybrid.c tests/suites.h
git commit -m "Hybrid engine: apollo2 --engine hybrid; every golden reproduced with the routines in C, together and alone

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Spec rev 7, M4.0 status and README (criterion 9)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 7**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 7)*.

1. **Status line.** Start it with: `*(rev 7)* Revision 7 records milestone 4.0: the oracle checked against the Harte 6502 vectors, the hybrid replacement infrastructure and the letter-to-sound pilot (design: docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md).`
2. **R1.** Append: `*(rev 7: --engine hybrid runs the ported routines as C; --rom-any accepts a ROM whose MD5 differs.)*`
3. **§3 table, `firmware/` row.** Append to its responsibility: `*(rev 7)* src/firmware: the fw context on the RAM image, the converted tables in src/firmware/data, and the ported routines; it builds without machine/ or cpu740/ (build.bat checks).`
4. **§3 table, new row after `firmware/`:** `| engine/ (hybrid) *(rev 7)* | rom_engine_run with a setup hook; thunks.c, the replacement table and register mapping; hybrid_engine.c | hybrid_engine_speak | machine, firmware |`
5. **§6 step 3.** Append: `*(rev 7)* Built in M4.0: machine_hook (fires only on main-ROM addresses, zero emulated time, no interrupt inside), machine_rom_call (C calling ROM; a routine that does not return within 10 M cycles is a fault), and per-routine thunks that set only the outputs the callers use. A C fault (a read outside the converted data) stops the run as MFAULT_FIRMWARE.`
6. **§7.1.**
   - Append to the CPU unit-tests bullet: `*(rev 7)* Plus 44,700 Harte 6502 vectors (149 opcodes; BRK, SED and decimal-mode ADC/SBC excluded, P bit 5 masked as the 740's T flag).`
   - Append to the per-function differential tests bullet: `*(rev 7)* Implemented live (tests/test_diff_lts.c): the machine is cloned at each sampled routine entry, so no states are stored.`
7. **§8, milestone 4.** Append: `*(rev 7)* **M4.0 done <DATE>**: the Harte vectors pass; the nine letter-to-sound routines run as C; the hybrid engine reproduces every golden, with all routines and with each alone. The next plans follow the M4.0 spec section 6: M4.1 text normalisation, M4.2 command parser, M4.3 phoneme expansion and prosody, M4.4 parameter tracks, M4.5 frame builder, M4.6 boot, serial, bank and ISR code.`

Use today's date for `<DATE>`.

Before writing step 1, check Task 1's result. If it added any documented 740 difference beyond the three listed, name it there too.

- [ ] **Step 2: M4.0 spec status**

In `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`, change the status line to `**Status:** implemented <DATE> (plan docs/superpowers/plans/2026-10-03-m4-0-replacement-foundation.md)`.

- [ ] **Step 3: README**

In `README.md`, after the "Speaking text" section, add a section:

```markdown
## Hybrid engine (milestone 4)

`apollo2 --engine hybrid "text" -o out.wav` runs the original firmware with the ported routines replaced by C
(src/firmware). Today these are the letter-to-sound routines; the rest still runs on the emulated CPU. The
hybrid engine must reproduce every golden frame stream byte for byte (`build.bat test golden_hybrid`), and
`build.bat test diff_lts` compares each ported routine with the ROM at real call sites.

The converted tables in src/firmware/data come from `python tools/extract_tables.py` (run once, then
reviewed); `build.bat test firmware_data` checks them against the ROM. RAM names in src/firmware/fw_ram.h come
from `python tools/gen_fw_ram.py` (rerun after the Ghidra annotations change).

A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the CLIs unless `--rom-any` is given.
```

- [ ] **Step 4: Final run and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all suites ok, with 0 failures. Skips are allowed only for the ROM-less cases, and with the ROM present there should be none.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md README.md
git commit -m "Spec rev 7: milestone 4.0 done (Harte vectors, hybrid infrastructure, letter-to-sound in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
