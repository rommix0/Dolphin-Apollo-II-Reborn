# M5b — The native-only program and the emulator as the test oracle — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-14-m5b-native-cli.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 19): R1 (the CLI), §3 (the layout), §8 milestone 5.
- `docs/superpowers/specs/2026-10-13-m5a-native-host-design.md`: the board, the native host and engine, `render_text_native`, the build check of the native files, the no-ROM run (§3, §4, §6 "As built").

**User decisions:**
- (2026-10-13) **M5 is two plans.** M5a (done, merged 2026-10-14): the native host and engine. **M5b** (this spec): the emulator moves under `tests/` as the oracle, `apollo2` becomes native-only, the formal no-ROM proof, the parent spec's last revision of milestone 5.
- (2026-10-13) **Native only; the emulator goes to the tests.** "The program should never need the --engine option at all since the ROM hookup is only for testing purposes once the move is done." The shipped `apollo2` has no `--engine`, `--rom` or `--rom-any`.
- (2026-10-14) **The no-ROM proof is a repeatable mode plus a one-time run.** `build.bat test-norom` runs the suite as if no ROM existed, even with the file in the repo root; the runner requires every ROM-free suite to run in full. The plan's last code task runs the full suite once in a worktree that has no ROM file, and records the result here.
- (2026-10-14) Sections 1–4 below approved as presented.

## 1. Goal

After M5b, `src/` is the product and needs no ROM: `apollo2.exe` builds from `src/` alone and speaks through the native engine. Everything that emulates the M50734 or reads the ROM lives under `tests/oracle/` and serves only the tests and the three analysis tools. The proof that nothing in the product needs the ROM is mechanical and repeatable.

## 2. Where the code goes

### 2.1 The move

`git mv` each file; its subfolder under `tests/oracle/` mirrors its folder under `src/`, so no `#include` line changes (the build adds `/Itests\oracle`).

| From | To |
|---|---|
| `src/cpu740/*` (cpu740, disasm740, optable, vec740) | `tests/oracle/cpu740/` |
| `src/machine/*` (machine, coverage) | `tests/oracle/machine/` |
| `src/engine/{rom_engine,hybrid_engine,thunks,rom_id}.c/.h` | `tests/oracle/engine/` |
| `src/render/render.c/.h` (`render_text` on the emulated engines) | `tests/oracle/render/` |
| `src/apps/{apollo2_frames,apollo2_coverage,cpu740_vectors}.c` | `tests/oracle/apps/` |

Every `.c` basename stays unique across `src/` and `tests/`.

### 2.2 What `src/` holds afterwards

- `src/firmware/` and `src/firmware/data/` (the C firmware and its converted data);
- `src/board/` (m50734, board);
- `src/engine/` (frame_list, native_host, native_engine);
- `src/render/render_native.c/.h`;
- `src/synth/`, `src/audio/`;
- `src/apps/apollo2.c`, the only program in `src/`.

The oracle may include product headers (the machine includes `board/m50734.h`, the hybrid uses the firmware). The product never includes an oracle header.

### 2.3 The build

`build.bat` builds in three steps.

1. **The product.** `apollo2.exe` is compiled and linked from `src\` alone, with `/Isrc` and no other include root, its objects in a folder of their own (`build\product\`, so they never mix with the test build's objects). Any include of an oracle header fails to compile; any call into oracle code fails to link. This replaces M5a's `build\noemu` syntax check, which it makes redundant. The firmware's own check (`src\firmware` without `/Isrc`) stays.
2. **The oracle tools.** `apollo2_frames.exe`, `apollo2_coverage.exe` and `cpu740_vectors.exe` are built from `tests\oracle\apps\` with `/Isrc /Itests\oracle`, into `build\` under their current names, so `ghidra740\coverage.bat`, `ghidra740\validate.bat` and `tools/calibrate.py` keep working unchanged. They keep their ROM options (`--rom`, `--rom-any`, `APOLLO2_ROM`) and their behaviour.
3. **The tests.** `apollo2_tests.exe` is built from `tests\*.c`, the oracle (`tests\oracle\**` except `apps`) and `src\` with `/Isrc /Itests\oracle`.

Test files change only where this spec says; the goldens and every existing suite's output do not change.

## 3. The program

### 3.1 The command line

`apollo2` keeps `--voice a|b`, `--hyp LIST`, `--stream produced|raw`, `--prosody N`, `--text-file FILE`, `--audio-check`, `-o FILE` and the 1500-character limit (`APOLLO2_MAX_TEXT`; its comment names the native engine's speaking budget).

`--engine`, `--rom` and `--rom-any` are gone, and so are the ROM loading, the MD5 check (`rom_id`) and the `APOLLO2_ROM` lookup. An old option is an unknown option: the usage text and exit code 1. The usage text drops their three lines and its exit-code line becomes:

`exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged`

The header comment says the firmware runs as native C, not emulated.

### 3.2 The run and its reports

The run goes through `render_text_native` only. Reporting uses the board instead of the machine:

- **Firmware fault (exit 2).** A new `void board_print_fault(const board *b, FILE *out)` (in `src/board/board.c`) prints one line naming the fault, its address where it has one, and the cycle count, for example `firmware fault at $A893 after 12345678 cycles` for the NUL bug, and lines for the DME strobe, code run from a deselected bank, an unmapped vector (`$xxxx`) and input overflow. It replaces `machine_dump`, which has no counterpart without a CPU.
- **Cycle limit (exit 3):** unchanged message.
- **Unmodelled access (exit 5).** A new `void board_print_unmodelled(const board *b, FILE *out)` prints the lines `machine_print_unmodelled` prints, without the `(first at PC $xxxx)` part (the board logs no PC; there is none), and the same "log full" line.
- Clipping, the WAV output and live playback are unchanged.

### 3.3 The render API

- `render_text_native` takes the caller's engine:
  `engine_status render_text_native(native_engine *e, const char *text, const render_options *opt, render_result *out);`
  Each run starts the engine afresh (as M5a built and tested it); M5a's malloc per call goes away. When it returns, `e->b` holds the final board state, which the CLI reads for the fault and the log. The CLI keeps one static `native_engine`.
- `render_options.engine` leaves the product. `render_engine` and its values move to the oracle's `tests/oracle/render/render.h`, and the oracle's `render_text` takes the engine as a parameter:
  `machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text, const render_options *opt, render_result *out);`
  Its callers in the tests pass what they set in `opt.engine` today.
- File names stay: `src/render/render_native.c/.h` (product) and `tests/oracle/render/render.c/.h` (oracle).

### 3.4 Behaviour

For every text and option, `apollo2 -o out.wav` writes byte for byte the WAV it wrote before with the default `--engine rom`: native reproduces every golden frame stream and the synthesis is shared (M5a). The plan's CLI task checks this on corpus texts against the WAV the M5a CLI writes.

## 4. The no-ROM proof

### 4.1 One way to ask for the ROM

`test_rom()` (`tests/rom_fixture.c`) is the only place the tests look for the ROM. It changes in two ways:
- every call records that the running suite asked for the ROM (before its cached state is consulted);
- when the environment variable `APOLLO2_NO_ROM` is set to a non-empty value, it returns NULL without looking, even with the file in the repo root or named by `APOLLO2_ROM`.

### 4.2 The suites are marked

In `tests/suites.h` every suite is one of two kinds:
- **`SUITE(fn)`, ROM-free:** today's suites that run in full without the ROM and never ask for it (29: `suite_native` asks for the ROM in its queue-fit comparison with the ROM engine, so it is a ROM suite), plus `suite_render_native` (§4.5): 30 in all. *(Corrected 2026-10-14 from the plan's proof.)*
- **`SUITE_ROM(fn)`, with parts that need the ROM:** rom_boot, rom_speak, golden, raw_stream, render, rom_id, machine_hooks, firmware_data, diff_lts, diff_play, diff_serial, diff_main, golden_hybrid, diff_native, native (15). *(Corrected 2026-10-14 from the plan's proof.)* Their ROM-free parts still run without the ROM.

### 4.3 The runner enforces the marks

`tests/test_main.c` tracks, per suite, its failures, its skips and whether it called `test_rom()`. In every mode:
- a `SUITE` that calls `test_rom()` fails: `marked ROM-free but asked for the ROM`;
- a `SUITE_ROM` that never calls `test_rom()` fails: `marked as needing the ROM but never asked for it`.

When `APOLLO2_NO_ROM` is set, additionally a `SUITE` must end with no skip. The one exception is `suite_audio`'s device skip (`no waveOut device…`), which has nothing to do with the ROM; the runner names `suite_audio` as the one ROM-free suite allowed to skip, rather than allowing skips in general.

The summary gains a line after the existing one:
`ROM-free: <n> suite(s) ran in full; ROM: <m> suite(s) asked for the ROM`
(with a filter, the counts cover the suites run). With the ROM present the full suite is 45 suites, 0 failures, 0 skips.

### 4.4 `build.bat test-norom [filter]`

Builds as `test` does, then:
1. runs `build\apollo2_tests.exe [filter]` with `APOLLO2_NO_ROM=1`;
2. with `APOLLO2_ROM` unset, runs the product:
   - `build\apollo2.exe -o build\norom.wav "Hello."` must exit 0 and write a non-empty file;
   - `build\apollo2.exe --engine rom -o build\norom.wav Hello` must exit 1.

Any failure makes the command fail (non-zero exit). `build.bat test` is unchanged apart from the build steps of §2.3.

### 4.5 `suite_render_native` (new, `tests/test_render_native.c`; ROM-free)

One process, one engine, several renders (the M5a deferred CLI-level test):
- a sequence through one `native_engine`: corpus texts, then a NUL-bug fault, then an input overflow, then corpus texts again, with voice b and a raw stream among them; each result (status, samples, sample count, clip count, and for a fault the fault kind and address) equals what a freshly zeroed engine gives for the same text and options;
- `board_print_fault`'s line for the NUL-bug fault and for input overflow, and `board_print_unmodelled`'s lines for an injected unmodelled access, checked as exact strings (written to a temporary file and read back).

### 4.6 The one-time run

The plan's last code task runs the full suite (`build.bat test`, not `test-norom`) in a fresh worktree that has no ROM file, with `APOLLO2_ROM` unset. That proves norom mode is not hiding a path that reads the file some other way. Its summary lines go into §6 "As built".

## 5. Done criteria, docs, scope

### 5.1 Done when

1. `apollo2.exe` builds from `src\` alone (`/Isrc`, no oracle source or include root); nothing under `src/` includes an oracle header.
2. `build.bat test`: 45 suites, 0 failures, 0 skips with the ROM; every golden byte-identical; `diff_main` (66 runs, 129 idles, 31,533 frame events), `diff_native` (69 runs, 135 idles, 40,329 frame events), `golden_native` (48 goldens, 23,281 frames) and the other suites' counts unchanged.
3. `build.bat test-norom`: passes; 30 ROM-free suites in full, the 15 ROM suites skip only their ROM parts, the product runs pass (`tests/check_cli.bat`). *(Corrected 2026-10-14 from the plan's proof.)*
4. The one-time worktree run with no ROM file passes; its summary is recorded in §6.
5. `ghidra740\coverage.bat`, `ghidra740\validate.bat` and `tools/calibrate.py` find their tools under the same names in `build\`, and the tools behave as before.

### 5.2 Docs

- **README.** The usage section rewritten for the native-only CLI (its options, its exit codes, no ROM needed to build or run `apollo2`). A "Tests and the oracle" section: what needs the ROM (the suites marked `SUITE_ROM`, the three tools under `tests/oracle/apps/`), `APOLLO2_ROM`, `--rom`, `--rom-any` there, and `build.bat test-norom`. The "Native engine (milestone 5)" section loses its sentence beginning "Until milestone 5b".
- **Parent spec rev 20.** R1 and the CLI: native-only, no ROM. §3: the layout, `src/` the product, `tests/oracle/` the emulator. §8 milestone 5: **M5b done**, **milestone 5 complete**.
- **This spec.** Status and §6 "As built", with the one-time run's summary.

### 5.3 Task outline (for the plan)

1. The move: `git mv` per §2.1, `build.bat`'s three steps (§2.3), the noemu check removed; the full suite unchanged (44 suites).
2. The render API (§3.3) and the board's print functions (§3.2), with `suite_render_native` (§4.5).
3. The CLI (§3.1, §3.2).
4. The runner's marks and norom mode (§4.1–§4.4); `build.bat test-norom`; the one-time run (§4.6).
5. Docs (§5.2).

### 5.4 Out of scope

- Any change to the firmware C, the board's behaviour, the machine model or the synthesizer.
- The other M5a deferred items (a ROM call after the deadline, the remaining test gaps): they stay in the follow-ups list.
- Live playback changes, other ROM versions, front ends beyond the CLI.

## 6. As built

Where the code is more precise than, or differs from, the sections above.

- **The order of the work** (§5.3): the plan ran the tasks in another order: the render API and the board's reports, then the CLI, then the move, then the no-ROM proof, then the docs. The program can build from `src\` alone only once the CLI no longer uses the emulator, so the move came third.
- **The move** (§2.1, §2.2), as built: 24 files, each a 100% rename in its commit: `tests/oracle/cpu740/` (`cpu740.c/.h`, `disasm740.c`, `optable.c/.h`, `vec740.c/.h`), `tests/oracle/machine/` (`machine.c/.h`, `coverage.c/.h`), `tests/oracle/engine/` (`rom_engine`, `hybrid_engine`, `thunks` and `rom_id`, each `.c/.h`), `tests/oracle/render/` (`render.c/.h`, changed for the engine parameter before the move) and `tests/oracle/apps/` (`apollo2_frames.c`, `apollo2_coverage.c`, `cpu740_vectors.c`). The empty `src/cpu740` and `src/machine` folders are gone. No `#include` line changed: the moved files' `src`-relative includes are found through `/Itests\oracle`, and so is `apollo2_coverage.c`'s `#include "../../tests/corpus_list.h"` (from `tests/oracle` it names `tests/corpus_list.h`). The 123 `.c` files under `src/` and `tests/` have 123 distinct basenames.
- **The build** (§2.3), as built in `build.bat`, every step stopping the build at its first error:
  - the firmware's check is unchanged: `cl /Zs` of `src\firmware\*.c` and `src\firmware\data\*.c` without `/Isrc`;
  - **the program:** one `cl` with the common flags (`/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /D_CRT_SECURE_NO_WARNINGS`), `/Isrc` as the only include root and `/Fobuild\product\ /Fdbuild\product\`, of `src\apps\apollo2.c` and every `.c` in `src\board`, `src\engine`, `src\firmware`, `src\firmware\data`, `src\synth`, `src\audio` and `src\render` (60 objects), linked with `winmm.lib` into `build\apollo2.exe`;
  - **the oracle tools:** one `cl` per tool, `tests\oracle\apps\<tool>.c` with every `.c` in `tests\oracle\cpu740`, `machine`, `engine` and `render` and the program's folders above, with `/Isrc /Itests\oracle` and the objects in `build\`, into `build\apollo2_frames.exe`, `build\apollo2_coverage.exe` and `build\cpu740_vectors.exe`;
  - **the tests:** `tests\*.c` with the same oracle and program sources, `/Isrc /Itests\oracle`, objects in `build\`, into `build\apollo2_tests.exe`;
  - `build\noemu` and its check are gone. `test [filter]` runs the tests and returns their exit code. `test-norom [filter]` (§4.4) sets `APOLLO2_NO_ROM=1` and runs `build\apollo2_tests.exe [filter]`, then clears `APOLLO2_NO_ROM` and `APOLLO2_ROM` and calls `tests\check_cli.bat build\apollo2.exe`; either failing makes it exit 1.
- **The separation** (§2.3, §5.1 criterion 1), as checked once and not committed: a stray `#include "machine/machine.h"` in `src/render/render_native.c` stops the program's build with `fatal error C1083: Cannot open include file: 'machine/machine.h'`. After the CLI change, no file under `src/` includes an oracle header.
- **The tools' ROM options** (§2.3, §5.2): `cpu740_vectors` reads no ROM and has no ROM options (`-o`, `-n`, `--seed`, `--blank` only). Only `apollo2_frames` and `apollo2_coverage` keep `--rom`, `--rom-any` and `APOLLO2_ROM`, and only they check the ROM's MD5 (`rom_id_accept`). So §2.3's "They keep their ROM options" and §5.2's list of what needs the ROM ("the three tools under `tests/oracle/apps/`") apply to those two only.
- **The tools** (§2.3, §5.1 criterion 5): `ghidra740\coverage.bat`, `ghidra740\validate.bat` and `tools/calibrate.py` are unchanged and find their tools under the same names in `build\`. Their outputs before and after the move are byte-identical: `apollo2_frames "Hello world."`, `apollo2_coverage` over the corpus (`rom 4106, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`) and `cpu740_vectors -n 4` with its blank image (928 vector lines, and the image itself).
- **The render API** (§3.3), as built:
  - `engine_status render_text_native(native_engine *e, const char *text, const render_options *opt, render_result *out);` in `src/render/render_native.h`, which now includes `engine/native_engine.h`. `render_options` is `voice`, `stream`, `prosody` and `synth`.
  - `render_engine` (`RENDER_ENGINE_ROM`, `RENDER_ENGINE_HYBRID`) and `machine_status render_text(machine *m, const uint8_t *rom, size_t len, render_engine engine, const char *text, const render_options *opt, render_result *out);` are in `tests/oracle/render/render.h`.
  - The callers pass the engine: `test_render.c` (the hybrid in `same_on_native` and six ROM renders; its native renders use its own static `native_engine`) and `test_golden_hybrid.c` (`test_render_matches_rom`, the ROM and then the hybrid).
- **The board's reports** (§3.2), as built in `src/board/board.c` (`board.h` gains `<stdio.h>`). `board_print_fault` prints one line, with `b->cycles` as N; only the vector and firmware faults have an address, `fault_addr`, printed as `$xxxx`:
  - `BOARD_FAULT_FIRMWARE`: `firmware fault at $xxxx after N cycles` (`$A893` for the NUL bug);
  - `BOARD_FAULT_VECTOR`: `firmware fault: unmapped vector $xxxx after N cycles`;
  - `BOARD_FAULT_DME`: `firmware fault: DME (data-memory strobe) enabled after N cycles`;
  - `BOARD_FAULT_ESCAPED`: `firmware fault: code run from a deselected ROM bank after N cycles`;
  - `BOARD_FAULT_INPUT_OVERFLOW`: `firmware fault: input longer than the serial queue after N cycles`;
  - `BOARD_FAULT_NONE`: `no firmware fault after N cycles`. The CLI never prints this one: it calls `board_print_fault` only on `ENGINE_FAULT`, which `native_engine_run_input` returns only with a board fault set.

  `board_print_unmodelled` prints `unmodelled read $00DA` or `unmodelled write $00DA` per logged access, then `unmodelled log full: N more accesses not listed` when accesses were dropped. It prints nothing for an empty log.
- **The CLI** (§3.1, §3.2), as built in `src/apps/apollo2.c`:
  - It includes `audio/playback.h`, `audio/wav.h` and `render/render_native.h`, and keeps one static `native_engine g_engine`.
  - Its options are `-o FILE`, `--voice a|b`, `--hyp LIST`, `--stream produced|raw`, `--prosody 0-7`, `--text-file FILE` and `--audio-check`. Any other argument of two or more characters that starts with `-`, and an option with a missing or invalid value, is a usage error found before any text is read or any engine runs: the usage text and exit code 1. That covers `--engine`, `--rom` and `--rom-any`.
  - The usage text ends with the exit-code line of §3.1. `APOLLO2_MAX_TEXT` (1500) is commented as keeping the speech inside the native engine's speaking budget, `NATIVE_SPEAK_CYCLES` (about 200 s).
  - **Exit 1:** usage, `cannot read FILE`, `no text to speak (TEXT or the first line of the file is empty)`, `text too long: N characters (at most 1500)`, and `cannot write FILE`. `--text-file` reads at most 1,501 bytes and keeps the first line, so a longer first line is reported as `text too long: 1501 characters (at most 1500)`.
  - **Exit 2** (`ENGINE_FAULT`): `board_print_fault`, then `board_print_unmodelled` with no lead-in line. The CLI returns before it writes or plays anything, so a file already at the `-o` path is left as it was.
  - **Exit 3:** `cycle limit reached before the firmware went idle`, unchanged.
  - **Exit 5:** after a successful render, any logged or dropped unmodelled access prints `the firmware made accesses the board model does not cover:` (the M5a CLI said "machine model") and `board_print_unmodelled`'s lines. The WAV is still written, or the speech played.
  - **Exit 4:** a failed live playback (`live playback failed: ...`), and `--audio-check` when 10 kHz playback is not available. `--audio-check` prints `live playback at 10000 Hz: available` or `NOT available`.
  - Clipping still prints `warning: N samples clipped`.
- **Faults cannot reach the CLI** (§3.2): its text is a C string (an argument, or the first line of `--text-file`), so it cannot carry the NUL byte the NUL bug needs. 1500 characters with the `@K1 @R7 ` prefix and the CR are 1,509 bytes, far below the 4,095 the serial queue holds. Both faults and their report lines are therefore pinned in `suite_render_native`, through the engine and the render call the CLI uses, not in a CLI run. The speaking budget: the 1500-character reference text (`diff_native`'s `rf2_longest`) takes 173,088,979 cycles in all, boot included, 43% of `NATIVE_SPEAK_CYCLES` (400,000,000), which bounds the speaking phase alone (from the feed).
- **The WAVs** (§3.4): the reference set was rendered with the CLI as it was after the render API change, still on its default ROM engine. Its bytes were the same as the CLI's at 0b74649 for every case, checked while the plan was proven. The set has 54 WAVs, each exit 0:
  - `--voice b` ("Is this the second voice? It is."), and `--stream raw`, `--prosody 0`, `--prosody 7` and `--hyp b11,b13`, each on "Hello world.";
  - "@W5 @F6 Hello world.";
  - a 1500-character `--text-file` (34 times "the quick brown fox jumps over the lazy dog ", then "dogs");
  - the 47 corpus texts through `--text-file`.

  The native-only CLI, run with `APOLLO2_ROM` unset, wrote all 54 byte for byte (`cmp`): once after the CLI change and again after the move.
- **`test_rom()`** (§4.1), as built: every call first sets `g_test_rom_asked` (declared in `tests/test.h`, cleared by the runner before each suite). It then returns NULL at once when `APOLLO2_NO_ROM` is non-empty, before its cached state or any file is looked at. It is called from exactly the files of the 15 `SUITE_ROM` suites, and nothing else in the test program (`tests/*.c` and the oracle sources it links) opens a ROM file; only the oracle tools in `tests/oracle/apps/` open one of their own.
- **The marks** (§4.2): 30 `SUITE` and 15 `SUITE_ROM` in `tests/suites.h`, as §4.2 lists them; `suite_render_native` is the 45th and last line. `test_main.c` expands the list twice, giving each suite its mark (`test_mark`: `TEST_ROM_FREE` or `TEST_NEEDS_ROM`).
- **The runner** (§4.3), as built:
  - The verdict is a pure function in `test_main.c`, declared in `test.h`: `const char *test_verdict(const char *name, test_mark mark, bool asked_rom, int skips, bool no_rom)`. It returns NULL when the mark holds, else the reason: `marked ROM-free but asked for the ROM`, `marked as needing the ROM but never asked for it` (both in every mode), or `marked ROM-free but skipped with APOLLO2_NO_ROM set` (norom mode only).
  - The exception is by name: a `SUITE` that skips in norom mode fails unless it is `suite_audio`, whose only skip is `no waveOut device accepts 10 kHz mono 16-bit PCM`. `no_rom` is `APOLLO2_NO_ROM` set to a non-empty value.
  - A wrong mark prints `  <suite>: <reason>` on stderr, counts one failure and makes the suite `FAILED`.
  - After the existing summary line, `ROM-free: <n> suite(s) ran in full; ROM: <m> suite(s) asked for the ROM` is printed. `<n>` counts the `SUITE` suites run that asked for no ROM and skipped nothing, `<m>` the suites run that called `test_rom()`. A filter still matches a substring of the suite's name.
- **`suite_smoke`** (the plan's Review Focus 5): `test_verdict` over 32 rows: `suite_x` and `suite_audio`, each mark, a ROM request or none, 0 or 1 skip, each mode, each with its expected reason or none. One more call checks that 3 skips in norom mode also fail a `SUITE`.
- **`tests/check_cli.bat EXE`** (§4.4) makes the product runs in its own form. It renders "Hello." from the folder it is called in (the repo root), with `APOLLO2_ROM` as the caller has it (`test-norom` unsets it), into a new folder `%TEMP%\apollo2_check_cli_<random>`. It then runs from that folder, which has no ROM file, and every WAV it writes stays there, not in `build\norom.wav`. The cases:
  - `--engine rom`, `--engine hybrid`, `--rom UK_V2.01_4.04_ROM.BIN` and `--rom-any`: exit 1, the usage text's first line and its exit-code line on stderr, and no WAV written;
  - `--engine rom -o keep.wav` onto an existing file: exit 1 and the file unchanged;
  - `APOLLO2_ROM` unset, naming a missing file, and naming a text file that is not the ROM: exit 0 and a WAV larger than its 44-byte header, byte-identical (`fc /b`) to the first render's;
  - a 1501-character `--text-file`: exit 1, `text too long: 1501 characters (at most 1500)`, and `keep.wav` unchanged.

  It removes the folder, then prints `check_cli: ok` and exits 0, or prints one line per failed check and `check_cli: N failure(s)` and exits 1. On the M5a CLI it gave exactly the 23 expected failures; on the native-only CLI, `check_cli: ok`.
- **`suite_render_native`** (§4.5), as built in `tests/test_render_native.c`, with one static `native_engine` used for every run and a second one zeroed before each comparison run:
  - **The sequence:** the corpus texts `hello` (voice a, produced stream), `numbers` (voice b) and `prose` (raw stream), each the first line of its file and each `ENGINE_IDLE` with frames. Then the NUL bug, then an input overflow, then the same three texts again.
  - **Each render** through `render_text_native` must give, on the used engine and on a zeroed one, the same status, sample count, frame count, clip count, samples, fault kind and fault address.
  - **The NUL bug** ("@A", a NUL and the CR, through `native_engine_run_input`, since no C string carries it) must end `ENGINE_FAULT` with `BOARD_FAULT_FIRMWARE` at `ROM_CMD_RUN_NEXT`, the same on a zeroed engine with the same cycles, and no frame on either. Its line must be `firmware fault at $A893 after 819837 cycles`.
  - **The overflow** (4,091 spaces: with `@R4 ` and the CR, 4,096 bytes) must end `ENGINE_FAULT` with `BOARD_FAULT_INPUT_OVERFLOW`. Its line must be `firmware fault: input longer than the serial queue after 819809 cycles`.
  - **The unmodelled log:** after the last corpus run the log is empty and `board_print_unmodelled` prints nothing. A read and a write of $DA, a read of `UART_TX` ($E4) and a second read of $DA are then injected; the lines must be `unmodelled read $00DA`, `unmodelled write $00DA` and `unmodelled read $00E4`, the second read not logged again. With 5 accesses marked dropped, `unmodelled log full: 5 more accesses not listed` follows.
  - **Every fault kind's line**, from a board with the fault set by hand, 12,345,678 cycles and `fault_addr` $D300.
  - The lines are written to `build/test_render_native.txt`, read back and compared as exact strings; the file is removed at the end.
- **The full suite with the ROM** (§5.1 criterion 2): `45 suite(s), 0 failure(s), 0 skip(s)`, then `ROM-free: 30 suite(s) ran in full; ROM: 15 suite(s) asked for the ROM`, in about 16 minutes. The goldens are unchanged, and the suites print what they printed before: `diff_main` 66 runs, 129 idles, 31,533 frame events; `diff_native` 69 runs, 135 idles, 40,329 frame events; `golden_native` 48 goldens, 23,281 frames; `firmware_data` 49,104 bytes, 0 differ; the native render 47 corpus texts, 2,328,100 samples the same as the hybrid's.
- **`build.bat test-norom`** (§4.4, §5.1 criterion 3), run with `APOLLO2_ROM` naming the ROM, exits 0 with:
  - `45 suite(s), 0 failure(s), 18 skip(s)`;
  - `ROM-free: 30 suite(s) ran in full; ROM: 15 suite(s) asked for the ROM`;
  - `check_cli: ok`.

  The 18 skips are 17 `SKIP: ROM not found (set APOLLO2_ROM)` and `suite_firmware_data`'s `SKIP: ROM not found (set APOLLO2_ROM): converted tables not compared`. They come from 14 of the 15 `SUITE_ROM` suites, three each in `suite_rom_speak` and `suite_machine_hooks`. `suite_native` asks for the ROM, runs its native part and does not skip. Every suite prints `ok`.
- **The filtered norom runs** (the plan's Review Focus 5):
  - `test-norom golden` runs `suite_golden`, `suite_golden_hybrid` and `suite_golden_native`: `3 suite(s), 0 failure(s), 2 skip(s)`, `ROM-free: 1 suite(s) ran in full; ROM: 2 suite(s) asked for the ROM`, `check_cli: ok`.
  - `test-norom diff_main` runs `suite_diff_main` only: `1 suite(s), 0 failure(s), 1 skip(s)`, `ROM-free: 0 suite(s) ran in full; ROM: 1 suite(s) asked for the ROM`, `check_cli: ok`.
  - A deliberate mis-mark, not committed: `suite_native` as `SUITE`, run with the ROM, fails with `marked ROM-free but asked for the ROM`; `suite_board` as `SUITE_ROM` fails with `marked as needing the ROM but never asked for it`, both with the ROM and in norom mode. Each such run exits 1. A ROM-free suite that asks for the ROM in norom mode was not run; `suite_smoke`'s verdict table covers that case.
- **The one-time run** (§4.6, §5.1 criterion 4): a fresh worktree of 0b63f44 with no `*.bin` file in it and no `APOLLO2_*` variable set. `build.bat test` there built with no warning and printed:

  ```
  45 suite(s), 0 failure(s), 18 skip(s)
  ROM-free: 30 suite(s) ran in full; ROM: 15 suite(s) asked for the ROM
  ```

  These are the same suites and skips as `test-norom`. `suite_golden_native` printed `48 goldens, 23281 frames reproduced on the native engine`; it, `suite_render_native` and the other 28 `SUITE` suites passed with no skip. `suite_native` passed without its ROM comparison. The worktree was then removed.
- **The Review Focus** (the plan's header), as built and what each showed:
  1. **Old command lines:** in `check_cli.bat`, each of `--engine rom`, `--engine hybrid`, `--rom PATH` and `--rom-any` gets the usage text and exit 1 with no WAV written, and `--engine rom` onto an existing file leaves it unchanged. The M5a CLI failed these checks; the native-only CLI passes them.
  2. **No ROM, or a stale `APOLLO2_ROM`:** in `check_cli.bat`, run from a folder with no ROM file, with `APOLLO2_ROM` unset, naming a missing file or naming a file that is not the ROM: each exits 0 and writes the WAV the first render wrote. Under `test-norom` that first render also runs with `APOLLO2_ROM` unset.
  3. **A text that faults the firmware:** `suite_render_native` (above). The NUL bug ends at $A893 after 819,837 cycles with no frame, the overflow is `BOARD_FAULT_INPUT_OVERFLOW` after 819,809 cycles, and both match a fresh engine. That the CLI returns 2 before writing or playing anything is in the code; no test runs it, since no CLI text can fault.
  4. **Every option combination:** the 54 reference WAVs (above), byte-identical after the CLI change and after the move; 1501 characters exit 1 in `check_cli.bat`.
  5. **Filtered and mis-marked runs:** `suite_smoke`'s verdict table, the two filtered norom runs and the deliberate mis-mark (above).
