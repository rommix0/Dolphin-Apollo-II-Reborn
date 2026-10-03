# M4.0 — Replacement foundation and letter-to-sound pilot — Design

**Status:** implemented 2026-10-03 (plan docs/superpowers/plans/2026-10-03-m4-0-replacement-foundation.md)
**Parent spec:** `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 7): §3 architecture, §6 steps 3–6, §7.1, §8 milestone 4.
**User decisions (2026-10-03):**
- M4 is split into plans. M4.0 hardens the oracle, builds the replacement infrastructure and ports one pilot subsystem end to end. Each later subsystem gets its own short plan (M4.1, M4.2, …).
- Tom Harte's 6502 SingleStepTests (MIT) are downloaded once. A sample is committed as a compact test file.
- The pilot is letter-to-sound.
- Approach A: the C code runs on the firmware's own memory image.
- The verification plan in §5.

## 1. Goal

When M4.0 is done:
- the emulator oracle is checked against an independent CPU reference;
- the `hybrid` engine exists, and in it the letter-to-sound routines run as C;
- that C reproduces every committed golden frame stream byte for byte (R3);
- each ported routine matches its ROM original at every call site the corpus exercises;
- the C reads the rule tables only from committed, readable C (R5), which is verified byte for byte against the ROM.

## 2. Oracle hardening (before any porting)

The goldens come from our own emulator, so a CPU bug would be frozen into them and then faithfully reproduced by the C port. This part closes that gap.

### 2.1 6502 reference vectors
- **Source:** github.com/SingleStepTests/65x02, folder `6502/v1` (MIT). Only the files of the 151 documented NMOS opcodes are downloaded (about 640 MB), once, into `build/harte/`, which is git-ignored.
- **Converter:** `tools/harte_subset.py` writes `tests/data/cpu6502_vectors.bin`:
  - 300 vectors per opcode, the first 300 in file order that pass the filters below;
  - a fixed binary record per vector: the initial and final registers, and the initial and final RAM pairs, at most 8 per vector;
  - the MIT notice is committed beside it as `tests/data/cpu6502_vectors.LICENSE`.
- **Suite:** `cpu_harte` runs each vector on `cpu740` with a flat 64 KB test bus (`tests/cpu_harness.h`). It compares the final registers and every listed RAM byte. Cycle counts are not compared, because cpu740's counts are approximate (spec §3).
- **740 differences.** These are filtered or masked by rule, and each rule is written out in the converter and in the suite header:
  - **Decimal mode.** A vector whose initial P has D set is skipped for ADC and SBC. cpu740 faults there by design, and the ROM only ever clears D.
  - **SED (`$F8`) and BRK (`$00`).** Excluded, because cpu740 faults on them by design (spec §3) and the ROM never uses them.
  - **P bit 5.** On the 740 this is the T flag, not the 6502's constant 1. So:
    - PLP and RTI vectors that would restore T = 1 are skipped, because cpu740 faults there by design;
    - for pushes (PHP, and the P byte that BRK pushes), bit 5 of the pushed byte is compared as the 740 defines it.
  - **Other mismatches.** Any other mismatch is a finding, not a filter. It is checked against MAME's `m740`/`m6502` sources and resolved in one of two ways:
    1. a cpu740 fix, after which the goldens are regenerated and reviewed (spec §7.1);
    2. a documented 740 difference, which becomes a new written rule.
- The 740-only instructions keep their existing unit tests and the M3 SLEIGH cross-check. No independent reference exists for them.

### 2.2 Machine follow-ups carried into M4.0
- **One address-mapping helper.** It is shared by `bus_read`, `machine_peek` and the new hooks (§3.2). It resolves a CPU address and the current P2 state to a region: RAM, an SFR, main ROM, the lower-ROM window, an empty option socket, or the DSP port.
- **ROM identity check.** The MD5 `74460801a399983b1a929f57e5ab57d7` (spec §1) is checked in `tests/rom_fixture.c` and in the CLIs:
  - when it differs, the ROM-dependent suites SKIP with a message;
  - the CLIs exit with an error;
  - the CLIs' `--rom-any` flag lets a deliberately patched scratch ROM through, with a warning.

The other deferred M1 and M2 follow-ups stay deferred.

## 3. Architecture

### 3.1 Units

| Unit | Responsibility | Depends on |
|---|---|---|
| `src/firmware/fw.h` | `fw` context: the RAM image pointer, the converted data image, the host interface | — |
| `src/firmware/fw_ram.h` | RAM names generated from `ghidra740/annotations/ram.txt`, e.g. `#define TN_POS 0x0051` | — |
| `src/firmware/data/*.c` | Readable converted tables (§4) | `fw.h` |
| `src/firmware/fw_data.c` | Encoder: lays the tables out into the data image at their original addresses | data |
| `src/firmware/lts.c`, `tn_rules.c` | The ported routines (§3.4) | `fw.h` |
| `src/engine/thunks.c` | Register ↔ C mapping per ported routine. The replacement table. | machine, firmware |
| `src/engine/hybrid_engine.c` | `--engine hybrid`: the ROM engine plus every hook in the replacement table | machine, thunks |
| `src/machine/` | adds `machine_hook`, `machine_rom_call` and the interrupt hold | cpu740 |

**Boundary rule:** nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` enforces this by compiling `src/firmware/*.c` with only `src/firmware` and the C runtime on the include path. This is what lets M5's native engine build without the emulator.

### 3.2 Hooks and thunks
- `machine_hook(m, addr, fn, user)` registers a C function for a ROM entry address. Before each instruction fetch, `machine_run` checks a 64 K lookup table. A hook fires only when the shared mapper (§2.2) says `addr` is main ROM under the current P2. Option-socket banks and the lower-ROM window never trigger one.
- **What a thunk does:**
  1. reads the routine's input registers;
  2. calls the C function on the shared `fw` context;
  3. writes back the routine's declared outputs (registers and the flags callers branch on);
  4. emulates the RTS.
- The C part of a thunk takes 0 emulated cycles, and no interrupt is taken inside it. A ROM routine it calls (via `rom_call`, §3.3) runs normally, with its real cycles and any interrupt that falls due, exactly as the ROM original would at the same points; the hybrid engine does not hold IRQs. *(rev 7)* The produced-frame stream does not depend on timing: the timer-scale tests already prove this (`hello` at 1/4 and 3/1), so this keeps R3.
- **Declared outputs.** Each routine's contract (inputs, outputs, flags, preserved registers) is taken from its Ghidra annotation. It is written as a comment above its thunk. Registers the annotation calls "destroyed" are left as the C produced them.

### 3.3 Calling ROM from C
- `fw->host->rom_call(host, addr, regs*)` runs a routine that is still in ROM to completion:
  1. it pushes a sentinel return address and sets the PC;
  2. it steps the machine, where hooks and interrupts behave normally, until the sentinel RTS returns;
  3. it reports the exit registers.
- If the routine does not return within a cycle budget (10 M cycles), that is a fault.
- In the pilot, `tn_append_phonetic` **stays in ROM** and is reached through `rom_call`. It also serves `cmd_put_phonetic`, whose `tn_rule_ptr` points at phonetic strings outside the converted tables (the baud and version texts). So porting it would read unconverted ROM. It moves to C with those strings in a later plan.
- `rom_call` also gets a unit test on a small leaf routine (`util_shl4`).

### 3.4 The pilot routines (all main ROM)

| Routine | Entry | Notes |
|---|---|---|
| `lts_affix_next` | `$AF11` | Y in, Y and A out |
| `lts_affix_prev` | `$AF1C` | Y in, Y and A out |
| `lts_sym_vowel` | `$B007` | A out, steps `lts_match_pos` |
| `lts_match_symbol` | `$B05E` | A in and out. It also has the `lts_sym_*` labels, which are internal dispatch targets and become C `switch` cases. |
| `lts_match_list` | `$B0E3` | Z out (no rule matched) |
| `lts_convert_char` | `$B172` | Sets P2.7 for the K–Z lists through the host's SFR write. `tn_convert_clause` (ROM) clears it at `$B25A`. |
| `tn_say_char` | `$B0C3` | X out = the old `tn_pos` |
| `tn_rule_skip_text` | `$B138` | Y in and out, A out |
| `tn_rule_skip_field` | `$B142` | A and Y in, Y = 0 out |

`tn_convert_clause` and everything above it stay in ROM and call into C through the hooks.

The C keeps the ROM's behaviour exactly, including the unreachable "LOOP" rule (spec rev 6, §1.2).

## 4. Converted data (R5)

### 4.1 Coverage
The pilot reads only these tables, so these are converted:

| Table | Address | Space |
|---|---|---|
| `tn_char_rules_lo` / `_hi` | `$4000` / `$4100` | lower ROM half, read with P2.7 clear |
| `lts_pair_index` and `lts_pairs_a..z` | `$4200`–`$477B` | lower ROM half, read with P2.7 clear |
| `tn_rules_*` and `lts_rules_letters` | `$477C`–`$5624` | lower ROM half, read with P2.7 clear |
| `lts_rules_a..j` | `$5625`–`$8FF3` | lower ROM half and main ROM |
| `lts_suffixes`, `lts_sibilants`, `lts_at_consonants` | `$9824`–`$989B` (from `unused_9824`, the `$00` that bounds the suffix list from below) | main ROM |
| `lts_char_class` | `$9AB4` | main ROM |
| `lts_rules_k..z` | LOW `$4010`–`$7831` | read with P2.7 set |

The exact byte ranges come from the annotations. A range that holds bytes no pointer reaches (for example the orphan "LOOP" list at LOW `$4310`, or the trailing empty list in `tn_rules_high`) keeps them as explicit, commented entries, so the range re-encodes completely.

### 4.2 Readable form
- **Rules.** A rule list is an array of entries. Each entry gives the left context in reading order, the key, the characters the rule consumes, the right context, and the phonetic output. The ROM stores the left context nearest character first; the encoder reverses it. Phonetic outputs are printable phonetic text in the ROM, so they become strings.
- **Character-class bits** become named flag combinations per character.
- **Pointer tables.** These are generated by the encoder from the list layout, so the C never states an address twice. The one place that genuinely needs an explicit reference is the L-M/L-O shared list, which says so.
- **First draft.** `tools/extract_tables.py` produces it, and it is then edited by hand: comments, and names from the annotations. The script is a one-time converter. Nothing at build or test time runs Python.

### 4.3 The data image and its check
- **Data image.** At `fw` initialisation, `fw_data.c` encodes the tables into a sparse image covering the converted ranges, in two spaces: one for the main ROM and lower-ROM half, and one for the LOW window.
- **Reading it.** C reads ROM data only through `fw_rom8(fw, addr, low)`. A read outside a converted range is a fault (`fw->fault` set, and the hybrid run stops). That keeps done-criterion 6 honest.
- **`firmware_data` test.** It encodes the image and compares every converted range byte for byte with the ROM. It SKIPs without the ROM.
- **What gets committed.** No ROM bytes as hex blobs, only the readable tables (R5).

## 5. Verification

### 5.1 Live differential test (`diff_lts`)
- Every corpus file runs on the ROM engine. Each time the PC reaches the entry of a ported routine, the test copies the machine state into two scratch machines: the CPU, RAM, SFRs, and the scalar fields `machine.c` names as copyable. The ROM image is shared.
- **Scratch A** runs the ROM routine with interrupts held off (a machine flag; the I bit is untouched). It stops when SP rises above its entry value, which also catches routines that return two levels up.
- **Scratch B** runs the C thunk. It also holds interrupts, which matters for any `rom_call` inside it.
- **Compared:**
  - all RAM `$0000–$3FFF`, except the stack bytes below the exit SP (`$0100–$01FF`) and the SFR range;
  - the exit PC and SP;
  - the routine's declared output registers and flags (§3.2);
  - the P2 latch.
- **Sampling:**
  - every call up to the first 1,000 per routine per corpus file, then every 64th;
  - every ported routine must be checked at least once over the corpus.
- A mismatch reports the corpus file, the routine, the call number, the registers and the first differing addresses.

### 5.2 Hybrid golden test (`golden_hybrid`)
- With all ported routines active, `hybrid` reproduces all 26 committed goldens, including `boot`, byte for byte.
- Each routine is also run alone over the corpus, to catch interactions between them.

### 5.3 Other tests
- `firmware_data` (§4.3).
- `cpu_harte` (§2.1).
- Unit tests for:
  - `machine_hook`: it fires only in main ROM, and the RTS is emulated;
  - `rom_call`: it returns, and the budget fault works;
  - the interrupt hold;
  - the shared mapper;
  - the MD5 check.

### 5.4 Done criteria
1. `cpu_harte` passes. Every filter and mask is a written rule with a reason (§2.1). If a CPU fix changed the goldens, they were regenerated and reviewed.
2. The ROM MD5 is checked in the fixture and the CLIs.
3. `diff_lts`: 0 mismatches, and every routine in §3.4 was checked.
4. `golden_hybrid`: all 26 goldens are reproduced with all routines active, and with each routine alone.
5. `firmware_data` passes.
6. `src/firmware/` builds with no `machine/` or `cpu740/` headers, and no `fw_rom8` fault occurs in any test (no unexpected fault; tests of the fault path trigger it on purpose).
7. `build.bat test` is green. The goldens are unchanged, unless a reviewed CPU fix under criterion 1 changed them.
8. `apollo2 --engine hybrid "text" -o out.wav` works.
9. The parent spec gets rev 7. It records M4.0 as done, and the M4.x sequence below.

## 6. After M4.0 (outline, not designed here)
Each step gets its own short plan, using the infrastructure above:
- **M4.1** text normalisation: `tn_convert_clause`, `tn_append_phonetic` and the phonetic strings.
- **M4.2** command parser.
- **M4.3** phoneme expansion and prosody.
- **M4.4** parameter tracks.
- **M4.5** frame builder.
- **M4.6** the remaining boot, serial and bank code. The ISR and DSP-buffer logic goes last, because in M5 it becomes the direct frame sink (spec §6 step 4).

The order follows the call graph from the pilot outwards. Each plan may revise it.

## 7. Out of scope for M4.0
- Porting anything beyond §3.4.
- The native engine (M5).
- Changes to the synthesizer or the CLI beyond `--engine hybrid`.
- Re-annotating Ghidra. Names are taken from the M3 annotations as they are. If the port shows an annotation is wrong, the fix goes into `ghidra740/annotations` with a rebuild, following the M3 refresh procedure.

## 8. As built
Where the implementation differs from the text above:
- The Harte vectors are committed as `tests/data/cpu6502_vectors.dat` (not `.bin`; `*.bin` is git-ignored), with a variable-length record (not a fixed record with at most 8 RAM pairs).
- The Harte harness loads P as `(in.p & ~T) | B`, and the compare masks P bits 4 (B) and 5 (T) and bit 5 of the byte PHP pushes. These are 740-vs-6502 architectural definitions (bit 5 is the 740's T flag; cpu740 holds B set), not bug filters.
- The encoder lives at `src/firmware/data/fw_data.c` (not `src/firmware/fw_data.c`).
- `fw_rom8` takes `(f, addr)`; the lower-ROM window state lives in the fw context (set by `fw_set_low_window`), not a third argument.
- The shared mapper (`machine_map_read`, public in `machine.h` for its unit test) reports four regions: RAM, SFR, ROM (the lower-ROM window is a ROM read at a different offset) and empty socket. The write-only DSP port at `$8000` reads as main ROM.
- `machine_clone` copies the ROM image rather than sharing it, and clears hooks, sinks and the IRQ hold.
- A hook that returns true without moving the PC and without a fault stops the run as `MFAULT_FIRMWARE` (guard against a zero-cycle spin). On a firmware fault inside a thunk, the PC is restored to the hooked entry. `rom_engine_run` returns `MACHINE_FAULT` before booting if its setup callback set a fault (e.g. an unknown `--only` routine name).
- Each hybrid run uses a fresh fw bridge (its fault flag is sticky); the bridge counts thunk executions and `golden_hybrid` asserts that every mode ran C.
- The C part of a thunk takes zero emulated cycles (a ROM routine it calls through `rom_call` runs with its real cycles and interrupts), so the produced frames equal the rom engine's but the raw DSP stream's timing under `--engine hybrid` can differ.
- `diff_lts` also speaks a few extra non-corpus texts, a superset of "over the corpus".
- Criterion 6's "no `fw_rom8` fault occurs in any test" means no unexpected fault in the differential and golden runs; `test_fw` and the thunk fault-path tests trigger faults on purpose.
