# M5b — The native-only program and the emulator as the test oracle — Design

**Status:** design, awaiting user review
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
- **`SUITE(fn)`, ROM-free:** today's 30 suites that run in full without the ROM, plus `suite_render_native` (§4.5).
- **`SUITE_ROM(fn)`, with parts that need the ROM:** rom_boot, rom_speak, golden, raw_stream, render, rom_id, machine_hooks, firmware_data, diff_lts, diff_play, diff_serial, diff_main, golden_hybrid, diff_native (14). Their ROM-free parts still run without the ROM.

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
3. `build.bat test-norom`: passes; 31 ROM-free suites in full, the 14 ROM suites skip only their ROM parts, the product runs pass.
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
