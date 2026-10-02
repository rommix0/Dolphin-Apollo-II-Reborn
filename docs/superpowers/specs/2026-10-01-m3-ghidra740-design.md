# M3 — Ghidra 740 Module and Project — Design

**Date:** 2026-10-01
**Status:** Implemented 2026-10-02 (branch `m3-ghidra740`). Every done criterion of §6 passes in
the headless project and in the GUI project. The text below describes what was built; the
"Rulings during implementation" section (§11) lists the decisions taken while building it.
Revised 2026-10-02: the module is installed automatically, and the rebuild targets the user's open
Ghidra project over MCP as well as a headless scratch project.
**Parent spec:** `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 5; rev 6 records this milestone), §6 steps 1–2
and §8 milestone 3: *"Ghidra module and project: full code coverage and labelled routines."*
**Goal:** A Ghidra processor module for the MELPS 740, checked against our `cpu740` oracle, and a
Ghidra project of `UK_V2.01_4.04_ROM.BIN` that is rebuilt from committed text annotations. In it all
code is disassembled, every byte is classified, and every function and table has a name. M4 ports
routines to C using these names and boundaries.

## 1. Decisions (user, 2026-10-01)

- **Annotations are the source of truth.** The labels, functions, data types and comments live in
  committed, diffable text files under `ghidra740/annotations/`. The same Ghidra scripts rebuild a
  program from the ROM plus these files in two places (§5.4):
  - the user's open GUI project `apollo_disasm` (`apollo_ghidra/`), over MCP;
  - a headless scratch project under `build/ghidra740/`, for the automated check and dumps.

  Both projects stay machine-local and git-ignored. Edits made interactively, in the GUI or over
  MCP, are exported back into the text files.
- **The module is installed automatically** *(revised 2026-10-02; this replaces the extension zip)*.
  `ghidra740\install.bat`:
  - compiles the `.sla`;
  - mirrors the module folder, together with its `ghidra_scripts/`, into the user's Ghidra extension
    directory, `%APPDATA%\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740`, and writes `repo.txt`
    there, naming the checkout;
  - mirrors the same module into the headless runs' own settings directory,
    `build\ghidra740\settings\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740`.

  The installed `m740.sla` is replaced only when it changes, so a reinstall while the GUI is open
  never leaves the GUI without its language. Headless runs set `XDG_CONFIG_HOME` to
  `build\ghidra740\settings` (in `ghidra_env.bat`), so they never touch the GUI's settings: a
  headless start used to recreate the GUI's `osgi\felixcache` and break its script loading.

  Nothing is copied into `C:\ghidra_12.1.3_PUBLIC`. Ghidra registers processor languages only at
  startup, so after the first install, or after any change to the SLEIGH files, the GUI must be
  restarted. Script changes need only a reinstall. Headless runs always start a fresh JVM, so they
  pick up both kinds of change.
- **Labelling depth:**
  - every function gets a subsystem-prefixed name and a one-line plate comment;
  - every ROM table gets a name, an extent and an element type;
  - the key RAM variables get names.

  Per-routine signatures and inline commentary are left to M4, done as each routine is ported.

## 2. Environment

- Ghidra 12.1.3 at `C:\ghidra_12.1.3_PUBLIC`. It needs JDK 21 or newer; JDK 23 and JDK 27 are
  installed. Headless runs use `support\analyzeHeadless.bat`, and the SLEIGH compiler is
  `support\sleigh.bat`.
- Scripts are written in Java. They are GhidraScripts kept in `ghidra740/module/MELPS740/ghidra_scripts/`,
  so no PyGhidra install is needed. Because they ship inside the installed extension, Ghidra puts them
  on its script path automatically:
  - the GUI runs them by name, over MCP, through a `run_script_inline` wrapper whose body calls
    `GhidraScript.runScript("X.java")` (`run_ghidra_script` copies a single file, so the shared
    `Annot.java` would be missing); the wrapper needs a fresh class name on every call whose body
    changes, because Ghidra's bundle cache fails on a reused one;
  - headless runs find them the same way.
- **MCP:** the bridge (`ghidra-mcp`) runs scripts only when a program is open. It can import files
  into the open project (`import_file`). It leaks a DomainFile consumer per script run and per
  import or open, so a program it has touched cannot be deleted until Ghidra restarts (§5.4).
- **Locating Ghidra:** every `ghidra740\*.bat` uses `%GHIDRA_INSTALL_DIR%` only when it holds the
  needed `support\*.bat`, and otherwise `C:\ghidra_12.1.3_PUBLIC` (the user's environment holds a
  stale `GHIDRA_INSTALL_DIR`). `ghidra_env.bat`, shared by all four Ghidra bats, also
  passes the project directory as its 8.3 short path, because `analyzeHeadless` rejects a path with
  a directory that starts with `.` (a `.worktrees` checkout).
- The scripts and tools that need Ghidra are **not** part of `build.bat test`. They have their own
  entry points (§7). `build.bat test` stays runnable without Ghidra.
- Everything Ghidra produces from the ROM (the project, dumps, reports) goes to `build/` or
  `apollo_ghidra/`, both git-ignored. What gets committed contains addresses and our own names and
  comments, never ROM bytes (parent spec R5).

## 3. The SLEIGH module (`ghidra740/module/`)

```
ghidra740/module/MELPS740/
  extension.properties     name, description, version=12.1.3
  Module.manifest
  ghidra_scripts/          Annot (shared format code), BuildProject, CheckProject, DumpProject,
                           ExportAnnotations, ResetProject, ValidateModule (.java)
  data/languages/m740.slaspec   derived from Ghidra's 6502.slaspec (not 65c02: the opcode maps differ)
  data/languages/m740.ldefs     id MELPS740:LE:16:default
  data/languages/m740.pspec     program counter only; SFR and vector labels come from annotations (sfr.txt, boot.txt)
  data/languages/m740.cspec     JSR/RTS calling convention, stack in page 1
ghidra740/install.bat          sleigh.bat → .sla, then mirror the module into %APPDATA%\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740
                               and into build\ghidra740\settings\...\Extensions\MELPS740 (§1)
ghidra740/ghidra_env.bat       shared by the headless bats: Ghidra location, 8.3 project path, XDG_CONFIG_HOME
```

- **Base:** the 6502 instruction set as Ghidra's 6502 module models it. The 740 extensions all
  occupy opcode slots that the 6502 leaves undefined, so they are added as exact-opcode
  constructors:
  - BBS/BBC on A and on zero page, SEB/CLB on A and on zero page;
  - LDM, TST, COM, INC A / DEC A, BRA;
  - CLT/SET, WIT/STP;
  - `JSR (zp)`, `JMP (zp)`, and the special-page call `JSR \$FFxx`;
  - MUL, DIV and RRF, for completeness.
- **Opcode map authority:** `src/cpu740/optable.c`, which M1 validated. The module and `optable.c`
  must agree on every one of the 256 opcodes: mnemonic, addressing mode and length.
- **T flag:** fixed at 0, as in parent §1.1. SET and CLT decode. SET's p-code sets the flag, but
  T-mode addressing is not modelled. SET in the ROM is a finding of the opcode census (§6,
  criterion 6), reported in the check and the spec rev, not a criterion failure; the final census
  found none.
- **Decimal mode:** not modelled, as in Ghidra's 6502 and our `cpu740` (which faults on D=1
  arithmetic). Validation vectors use D=0.
- **Stack:** SP is 16-bit with SH = 1, as in Ghidra's 6502 module, which keeps the decompiler's stack analysis
  working. Stack-page wrap is not modelled; the firmware never wraps, and validation vectors keep S in
  `$10–$EF`. *(Plan ruling 2026-10-02.)*
- **Special instructions:** WIT and STP become custom pcodeops (`wait()`, `stop()`). WIT does not
  end the flow, because in the firmware it falls through after an interrupt.

## 4. Validating the module against `cpu740`

**Vector generator.** A new C tool, `src/apps/cpu740_vectors.c`, is built by `build.bat`. For each
opcode `cpu740` executes, it writes N random single-instruction vectors (default 64 per opcode) to
`build/ghidra740/vectors.txt`. One vector per line holds:
- the instruction address and bytes;
- the starting A, X, Y, S and P (D=0, T=0);
- the memory the instruction reads;
- the resulting A, X, Y, S, P and PC, and the memory it writes;
- `cpu740_disasm`'s text.

The generator seeds the operand bytes and memory so that every addressing mode and both outcomes of
each branch are exercised. Opcodes `cpu740` faults on (MUL, DIV, SET, STP, SED, BRK and the illegal
ones) get disassembly-only lines.

**Ghidra check.** `ValidateModule.java` runs headless on a scratch program in the MELPS740
language, in a temporary project under `build/ghidra740/`. For each vector it:
1. disassembles the instruction and compares the text with `cpu740_disasm`, after normalising
   case, spacing and hex formatting;
2. on non-fault vectors, sets up the state, steps once with Ghidra's `EmulatorHelper`, and compares
   the registers, flags, next PC and memory writes.

It prints one line per mismatch and a summary, and exits non-zero on any mismatch.
`ghidra740\validate.bat` runs the generator and then the script.

**Pass condition:** zero mismatches over all 256 opcodes.

## 5. The project rebuild

### 5.1 Emulator coverage (`apollo2_coverage`)

`machine` gets an optional per-instruction hook (`machine_config.coverage`). A dedicated dev tool,
`apollo2_coverage`, runs boot plus every golden-corpus text with the hook on, and records:

- every executed instruction address, tagged with its space:
  - `rom` for main ROM and the default `$4000–$7FFF` window;
  - `low` for `$4000–$7FFF` while P2.7 = 1, i.e. file `0x0000–0x3FFF`;
  - `ramcode` for `$0200–$03FE`, the boot copy of `$ADFE–$AFFC`;
  - `ram` for any other RAM;
- every observed target of an indirect `JMP`/`JSR`, together with the address of the jump.

The output is the union over all runs, sorted, one record per line. `ghidra740/coverage.bat` runs
the tool and writes `ghidra740/coverage/corpus.cov`. That file is committed: it holds only
addresses.

*(Plan ruling 2026-10-02:* this replaces `apollo2_frames --coverage`, because the corpus texts hold
control bytes that a `.bat` loop cannot pass on a command line.)

Execution in `ram` other than `ramcode` is reported as a finding. None is expected, and the final
`corpus.cov` has none.

Besides the golden corpus, `coverage.bat` runs our own inputs `ghidra740/coverage/extra_index.txt`
and `extra_commands.txt` (`apollo2_coverage` takes extra text files). They reach code the corpus
does not, among it the immediate @I+/@I? handlers that dispatch through `JMP ($4A)` at `$C46A`, so
that jump has observed edges for criterion 4.

### 5.2 Memory blocks (`BuildProject.java`)

| Block | Space | Range | Source |
|---|---|---|---|
| `ZP` | default | `$0000–$00D9` | uninitialised RAM |
| `SFR` | default | `$00DA–$00FF` | volatile, labelled from `sfr.txt` |
| `RAM` | default | `$0100–$3FFF` | uninitialised RAM |
| `ROM_LO` | default | `$4000–$7FFF` | file `0x4000–0x7FFF` (P2.7 = 0) |
| `ROM` | default | `$8000–$FFFF` | file `0x8000–0xFFFF` (bank `$70`) |
| `LOW` | overlay | `$4000–$7FFF` | file `0x0000–0x3FFF` (P2.7 = 1) |
| `RAMCODE` | overlay | `$0200–$03FE` | file `0xADFE–0xAFFC` (boot copy) |

- The DSP port at `$8000` is write-only. It gets an annotated label (`DSP_DATA`) and a comment, not
  a block of its own, because Ghidra does not allow a block to overlap the ROM.
- The other ROM banks (option sockets) are not modelled (parent §9).

### 5.3 Annotation files (`ghidra740/annotations/`)

There is one file per subsystem, following the split in parent §6.6:
- `boot.txt`, `serial.txt`, `command.txt`, `text_norm.txt`, `lts.txt`, `phoneme.txt`,
  `prosody.txt`, `params.txt`, `frame.txt`, `isr.txt`, `bank.txt`;
- `ram.txt` for RAM variables;
- `types.txt` for struct definitions.

Each line is one record: whitespace-separated fields, with the comment taking the rest of the line. Only a
first-line `#` header survives an export. Other `#` lines and blank lines are ignored on load and dropped by
`rebuild.bat --format`. The plan (2026-10-02) fixes the exact field rules, `ptrm1[n]` jumptables (tables of
target − 1) and the extra files `sfr.txt`, `util.txt`, `unused.txt` and `new.txt`.

```
# space    addr   kind      name                 type          comment
rom        A61C   func      boot_reset           -             Reset: SFR setup, RAM clear, RAMCODE copy
rom        B1C2   label     lts_select_low_bank  -             SEB7 $F0: rules K–Z live in file 0x0000–0x3FFF
rom        B573   data      b14_table_lo         byte[256]     b14 values, index (ALF−A1−4)·(F1−33)
ram        16A5   data      dsp_frame_a          frame20       ping-pong DSP buffer A
ram        1306   data      dispatch_ram         ptr[16]       RAM jump table, written at run time
```

(The addresses come from the parent spec. The names, types and lengths are only examples.)
A `jumptable` record types a **ROM** table of code pointers. A RAM jump table (`$1306`, `$150B`)
has no bytes in the project. Its targets come from the code that fills it (that ROM source table
gets a `jumptable` record) and from the indirect targets in `corpus.cov`, and each target is
annotated as a `func` or `entry`.

**Kinds:**
- `func`: function entry, name and plate comment;
- `label`: a named location inside code or data;
- `data`: a typed item;
- `jumptable`: a table of code pointers, typed as pointers so the targets become functions;
- `entry`: an extra disassembly seed with no name of its own;
- `comment`: an end-of-line (EOL) comment at the address, with no name; the text is required.

**Types:**
- `byte`, `word` (little-endian), `ptr` (16-bit, creates a reference), `char`/`string`;
- `T[n]` arrays of any of these;
- structs named in `types.txt`, which uses the same line format with
  `struct NAME` / `field OFFSET NAME TYPE` records. Fields are primitive types or arrays of them,
  never structs.
- There is no pointer type into the `LOW` overlay: `ptr` targets resolve in the default space. Tables
  of LOW addresses, such as the second level of the `$4200` letter-to-sound index (the K–Z entries
  point into `LOW`), are typed `word[n]`. A LOW-pointer type is a possible follow-up.

**Script behaviour:**
- `BuildProject.java` refuses malformed lines, duplicate addresses and names, and overlapping data
  items, reporting file and line.
- `ExportAnnotations.java` writes the current project back into the files. A record keeps the file
  it came from, records made in the GUI land in `new.txt` (to be moved to their subsystem's file by
  hand), and every file is kept in canonical sorted order, so that a round trip with no edits changes
  nothing. `write` deletes the annotation files it no longer produces.

### 5.4 Build order (`BuildProject.java`)

The program is always created the same way: the ROM is imported raw, with language
`MELPS740:LE:16:default`, base `$0000` and no auto-analysis. The program is named after the file,
`UK_V2.01_4.04_ROM.BIN`. The scripts find the repo through the `repo.txt` that `install.bat` writes
into the installed extension.
`BuildProject.java` then runs on that freshly imported program:

1. Replace the single imported block with the blocks of §5.2. Their bytes are taken from the imported
   file image.
2. Apply `types.txt`, then the data and jumptable records, then the labels and comments.
3. Disassemble from:
   - the reset and interrupt vectors;
   - every `func`, `entry` and `jumptable` target;
   - every address in `corpus.cov`.

   A direct `JSR`/`JMP` from outside `RAMCODE` into `$0200–$03FE` is retargeted to the `RAMCODE`
   overlay: boot copies `$ADFE` there, and nothing else executes in that RAM range.
4. No auto-analysis runs. Code comes only from the seeds above, which keeps the rebuild
   deterministic. *(Plan ruling 2026-10-02.)* BuildProject also turns every analyzer off in the
   program's analysis options, because the GUI's background auto-analysis otherwise turned labels
   into functions after a build.
5. Create functions at the `func` addresses and at every call target, then apply the names and plate
   comments.
6. `BuildProject.java` writes its errors to `build/ghidra740/build.txt`. `CheckProject.java` (§6) reports
   them as criterion 0 and writes `build/ghidra740/check.txt`.

There are two targets, built by the same scripts:

- **Headless scratch** (`ghidra740\rebuild.bat`). This one is used by the automated loop, the check
  and the dumps.
  - It runs `analyzeHeadless` on `build/ghidra740/project/`, importing the ROM and running
    `BuildProject.java` and then `CheckProject.java` as post-scripts.
  - It never touches the GUI project, so it works while Ghidra is open.
- **The GUI project** `apollo_disasm` (`apollo_ghidra/`), the user's working view, rebuilt over MCP
  while Ghidra is open. The program is refreshed in place, never deleted and imported again, because
  ghidra-mcp's leaked consumers keep it "in use" (§2). The scripts run through the
  `run_script_inline` wrapper (§2); `ghidra740/README.md` has the exact procedure:
  1. `ExportAnnotations.java check` exports the program to `build/ghidra740/export/` and compares it
     with the annotation files. When the working tree already holds newer labelling commits, GUI
     edits are detected by comparing the export's sorted record set with the annotations at the
     commit the GUI was last built from (sorted, because labelling moves records between files).
     Any edits are exported with `write` and committed first.
  2. `ResetProject.java` returns the program to its just-imported state: it removes every memory
     block (and with them all code, data, symbols, functions, comments, bookmarks, references and
     overlays) and every data type, and keeps the imported file bytes. It refuses a program whose
     language is not `MELPS740:LE:16:default`. Then `BuildProject.java` and `CheckProject.java`.
  3. `ExportAnnotations.java check` again must pass; only then is the program saved.

  The first build imports the ROM once (`import_file`, the language above, `auto_analyze=false`)
  and then runs steps 2–3. The program stays open in the CodeBrowser. The headless target keeps
  the fresh `-overwrite` import, and `rebuild.bat --reset` proves that reset plus build gives the
  same program as a fresh import plus build.

## 6. Done criteria for M3 (`CheckProject.java`)

M3 is done when the check reports all of these, both from `ghidra740\rebuild.bat` and in the GUI
project:

1. **Module:** `validate.bat` passes, with zero mismatches.
2. **Dynamic coverage:** every address in `corpus.cov` is the start of an instruction inside a
   function, in the right space. An absent or empty `corpus.cov` fails the criterion.
3. **Static coverage:**
   - every byte of `ROM_LO`, `ROM`, `LOW` and `RAMCODE` is defined as either code or typed data;
   - padding and unused areas are annotated as data and named `unused_XXXX`;
   - there are no undefined bytes, disassembly-conflict bookmarks or flows into data.
4. **Indirect flow:**
   - `JMP ($4A)` and the RAM jump tables `$1306` and `$150B` are resolved, so every target is a
     function. As built, the required set is every indirect jump through zero-page `$4A` plus every
     indirect jump that has edges in `corpus.cov`; for these a comment does not count. The RAM-table
     dispatches are `JMP ($1489)` at `$A8BA` and `JMP ($1204)` at `$BB9D`, and `JMP ($4A)` at `$C46A`
     gets its edges from the extra coverage inputs (§5.1);
   - every other indirect jump has an annotation (a `jumptable` or a comment) explaining its
     targets. The final code has no other.
5. **Names:**
   - no function keeps a default name (`FUN_…`), and every function has a plate comment;
   - every data item in the ROM blocks has a non-default name;
   - every RAM address the code uses as a pointer or buffer base has a name.
6. **Opcode census:** the opcode-usage claim in parent §1.1 is re-checked over the full code, and
   the counts are written to the check report. SET, SED, MUL, DIV, STP or BRK in the final code is
   a finding, which is recorded in the spec rev.
7. **Open question:** the 17 phoneme indices that ASCII symbols cannot reach (parent §10) are
   traced through `$9E74`. The answer is recorded in the spec rev.

The check report lists each criterion with PASS/FAIL and the offending addresses. Criterion 0
reports BuildProject's errors from `build.txt`, one `ERROR` line per problem. Criterion 1 is checked
by `validate.bat` (the report says so). Criteria 6 and 7 are recorded in the parent spec's rev 6.

Final result (2026-10-02, headless and GUI): `RESULT PASS`; 4022 executed addresses, 0 undefined
bytes, 3 indirect jumps resolved, 168 functions, 280 data items and 78 RAM bases named, 60
mnemonics in the census and none of SET/SED/MUL/DIV/STP/BRK; `validate.bat` 0 mismatches.

## 7. Workflow and entry points

| Entry point | Needs | Does |
|---|---|---|
| `build.bat test` | MSVC | As before, plus unit tests for the coverage hook and the vector writer |
| `ghidra740\install.bat` | Ghidra | Compiles the `.sla` and mirrors the module and scripts into the user's extension directory and the headless settings directory (§1); a GUI restart is needed after SLEIGH changes |
| `ghidra740\validate.bat` | Ghidra + installed extension | Writes the vectors, runs `ValidateModule.java` headless |
| `ghidra740\coverage.bat` | ROM | Writes `ghidra740/coverage/corpus.cov` |
| `ghidra740\rebuild.bat [--format\|--roundtrip\|--reset]` | ROM, Ghidra + extension | Rebuilds the headless scratch project `build/ghidra740/project/`, runs `CheckProject.java`; `--format` rewrites the annotations canonically, `--roundtrip` runs the §8 round trip, `--reset` rebuilds through `ResetProject.java` as the GUI does |
| `ghidra740\dump.bat` | the scratch project | `DumpProject.java`: per-function disassembly + decompiled C → `build/ghidra740/dump/` |
| MCP: `run_script_inline` wrapper running `ResetProject.java` + `BuildProject.java` + `CheckProject.java` | Ghidra open, extension installed | Refreshes `UK_V2.01_4.04_ROM.BIN` in place in the open GUI project (§5.4) |
| MCP: `run_script_inline` wrapper running `ExportAnnotations.java check\|write` | Ghidra open | Detects, or writes back, GUI/MCP edits to the annotation files |

**The labelling loop.** It runs one subsystem at a time and works without the GUI, so subagents
can do it:
1. Run `dump.bat`.
2. Read the dumped disassembly and decompiled C, alongside the parent spec, the M1/M2 findings and
   the emulator.
3. Edit that subsystem's annotation file.
4. Run `rebuild.bat` and read `check.txt`. Repeat until the subsystem's addresses pass criteria 3–5.
   Before committing, run `rebuild.bat --format` and then `rebuild.bat --roundtrip`.
5. Refresh the GUI project over MCP (§5.4), so the user sees the new names.

The GUI and MCP stay available for exploration. Anything named there is brought back with
`ExportAnnotations.java` over MCP before it is committed, and before the GUI program is refreshed.
Refreshing the GUI program resets it, so anything not exported is lost.

**Subsystem order:**
1. boot, bank and ISR (mostly known from M1);
2. serial and command;
3. text normalisation;
4. letter-to-sound;
5. phoneme expansion;
6. prosody (the intonation model in parent §1.4);
7. parameter tracks;
8. frame builder.

## 8. Testing

- **C unit tests** (in `build.bat test`):
  - the coverage hook records the right PCs and spaces on a small hand-assembled program, including
    a P2.7 switch and a run from RAM;
  - the coverage writer's merge is a stable, sorted union;
  - the vector writer's output for a fixed seed is deterministic and parses back.
- **Module:** `validate.bat` (§4).
- **Project:** `CheckProject.java` (§6).
- **Round trip:** a rebuild followed by an export, with no edits in between, leaves the annotation
  files byte-identical. `ExportAnnotations.java` takes an output directory. `rebuild.bat --roundtrip`
  exports the scratch project to `build/ghidra740/export/` and compares the result with
  `ghidra740/annotations/`.
- **Unchanged goldens:** the goldens and all existing suites stay unchanged. M3 does not touch the
  emulator's behaviour, and the coverage hook is off by default.

## 9. Out of scope (later milestones)

- Thunks, `rom_call` and the per-function differential tests: M4 (parent §6.3, §7.1).
- Per-routine signatures (A/X/Y and zero-page inputs and outputs) and decompiler cleanup: done in M4
  as each routine is ported.
- Converting tables to C (`tools/extract_tables.py`): M4/M5 (parent §6.5).
- Parallel-port input and the option-ROM banks (parent §9).

## 10. Risks

- **GUI restarts:** a running Ghidra does not load new or changed SLEIGH languages, so SLEIGH work
  is validated headless, and the GUI is restarted once the module passes. The plan asks the user to
  restart Ghidra at that point. It is the one manual step.
- **JDK 27:** Ghidra 12.1.3 targets JDK 21. If headless runs misbehave on JDK 27, the `.bat`
  wrappers pin JDK 23 through `JAVA_HOME`.
- **Undecodable regions:** some bytes may be unreachable both statically and dynamically and still
  ambiguous, so code and data can't be told apart. These are annotated as data with a comment saying
  why. M4's differential tests will catch any misclassification that matters.
- **Corpus coverage:** the corpus may not reach every routine. Static recursive descent from the
  vectors and jump tables covers the rest. Corpus texts can be added if a routine's behaviour needs
  to be observed, but not to satisfy criterion 3.

## 11. Rulings during implementation

**From the plan (2026-10-02):**
- Coverage comes from a dedicated tool, `apollo2_coverage`, instead of `apollo2_frames --coverage`,
  because the corpus texts hold control bytes that a `.bat` loop cannot pass on a command line (§5.1).
- No Ghidra auto-analysis. Code comes only from the seeds, so the build is deterministic (§5.4).
- SFR and vector labels come from the annotations (`sfr.txt`, `boot.txt`). The pspec holds only the
  program counter.
- Stack-page wrap is not modelled, and the validation vectors keep S in `$10–$EF`. SP is 16-bit with
  SH = 1, as in Ghidra's 6502 module (§3).
- Validation disassembles with `PseudoDisassembler` and steps one `EmulatorHelper` per vector (§4).
- Annotation fields are whitespace-separated, with the comment taking the rest of the line. Only a
  first-line `#` header survives an export (§5.3).
- Besides the subsystem files there are `util.txt` (shared helpers, prefix `util_`), `unused.txt`
  (padding and unreferenced bytes, `unused_XXXX`), `sfr.txt` (SFRs and `DSP_DATA`, no prefix) and
  `new.txt` (records made in the GUI, waiting to be moved).
- `ptrm1[n]` jumptables type tables of target − 1. `entry` records (with their comments) and
  `jumptable` records (`ptr` or `ptrm1`) are marked by `ghidra740` bookmarks, so that they survive
  an export.
- Struct fields are primitive types or arrays of them, never structs.
- The program is named after the ROM file, `UK_V2.01_4.04_ROM.BIN`.
- The scripts find the repo through the `repo.txt` that `install.bat` writes into the installed
  extension.
- BuildProject writes `build/ghidra740/build.txt`, which CheckProject reports as criterion 0.

**Taken during execution:**
- *Locating Ghidra (Task 4).* Every `ghidra740\*.bat` uses `%GHIDRA_INSTALL_DIR%` only when it
  holds the needed `support\*.bat`, and otherwise `C:\ghidra_12.1.3_PUBLIC`. The user's environment
  holds a stale `GHIDRA_INSTALL_DIR`, so the plan's `if not defined` would have failed.
- *`ghidra_env.bat` (Task 6).* One shared helper holds that fallback and the 8.3 short path of the
  project directory (`analyzeHeadless` rejects a `.worktrees` path). `rebuild`, `dump` and `validate`
  call it, and since Task 10d `install.bat` does too.
- *Build errors (Tasks 6–7).* BuildProject writes one `ERROR` line per problem, splitting a
  multi-line message, because criterion 0 reads `build.txt` line by line.
- *Comment records (Tasks 6, 8).* A `comment` record must have text; an empty one would break the
  exact round trip.
- *Criterion 4 (Task 7).* The required-resolved set is every indirect jump through zero-page `$4A`
  plus every indirect jump with `corpus.cov` edges, and a comment does not count for them. This
  follows §6 rather than the plan's uniform comment fallback, and is data-driven rather than a list
  of addresses. `JMP ($4A)` at `$C46A` got its edges from our own coverage inputs
  `ghidra740/coverage/extra_index.txt` and `extra_commands.txt` (§5.1).
- *Criterion 2 (Task 7).* An absent or empty `corpus.cov` fails it, so it can never pass vacuously.
- *SET in the ROM (Task 7).* It stays a criterion-6 FINDING, as §6 words it, not a failure; §3 now
  says the same. The final census has none.
- *Bit-op display (Task 7).* The SLEIGH bit-op mnemonics show the bit as a plain decimal digit
  (`SEB3`, `BBC5`), as `cpu740_disasm` does, not as `SEB0x3`.
- *The installed `.sla` (Task 10).* The mirror used to purge the git-ignored `m740.sla` and
  recompile it on every run. Now `install.bat` excludes the installed `m740.sla` and `repo.txt` from
  the mirror, compiles to `build\ghidra740\m740.new.sla`, and replaces the installed copy only when
  it is missing or differs, so a reinstall during a GUI session never removes the language.
- *ROM flows into RAMCODE (Task 11).* BuildProject retargets direct `JSR`/`JMP` from outside
  `RAMCODE` into `$0200–$03FE` to the `RAMCODE` overlay. Boot copies `$ADFE` there and nothing else
  executes in that range. This is needed for criterion 3.
- *Running scripts in the GUI (Task 10).* Through a `run_script_inline` wrapper that calls
  `GhidraScript.runScript("X.java")`, not `run_ghidra_script`, which copies a single file so
  `Annot.java` is missing. A wrapper whose body changes needs a fresh class name, or Ghidra's
  bundle cache fails.
- *Analysis off (Task 10b).* BuildProject turns every analyzer off in the program's analysis
  options, so the GUI program matches the headless one. To use auto-analysis in the GUI, re-enable
  the analyzers by hand.
- *In-place GUI refresh (Task 10c).* ghidra-mcp leaks DomainFile consumers, so the GUI program
  cannot be deleted while Ghidra runs. `ResetProject.java` therefore replaces §5.4's "delete and
  re-import" for the GUI target only. It removes every memory block and every data type, and keeps
  the imported file bytes. The headless target keeps the `-overwrite` import, and
  `rebuild.bat --reset` checks that both paths give the same program.
- *ResetProject guard (Task 10c).* It refuses any language but `MELPS740:LE:16:default`, and its
  header warns that it is destructive, since it runs over MCP against the current program.
- *Headless settings (Task 10d).* Headless runs and the SLEIGH compile use their own settings
  directory, `build\ghidra740\settings`, through `XDG_CONFIG_HOME`; `-Dapplication.settingsdir` was
  not used, because `sleigh.bat` takes no VM arguments. `install.bat` installs into the GUI's
  extension directory and into that settings directory. A headless start had recreated the GUI's
  `osgi\felixcache` and broken its scripts.
- *GUI-edit detection (Tasks 13–14 refreshes).* Before a refresh, the export is compared with the
  annotations at the commit the GUI was last built from, as sorted record sets, because labelling
  moves records between files.
- *Labelling.* One named record per address, and labels inside data items are allowed (for
  example the 1-based bases such as `frm_snap_base`, read as `base,X`). The Task 11 label at ram
  `$0200` gave way to the prosody array `pros_seg_phoneme`. Tables of LOW addresses are typed
  `word[n]`, because there is no LOW-pointer type (§5.3); that type is a possible follow-up.
  Unreferenced bytes that are not padding (`unused_C675`) go to `unused.txt` with a comment.
- *`frame20.end_marker` (Task 18).* It is `byte[2]`, not `word`, so labels on its second byte
  (`par_formant_ofs_base`, `par_slot_map_base`) keep their comments through the round trip.
