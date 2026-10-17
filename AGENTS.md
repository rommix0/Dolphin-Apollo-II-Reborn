# AGENTS.md

Notes for coding agents working on Dolphin Apollo II Reborn. `README.md` has the user-facing overview, usage and
layout; `xtras/superpowers/specs/2026-09-30-software-apollo2-design.md` is the design, with a revision log of every
milestone.

## What has been done

The Dolphin Apollo 2 (Mitsubishi M50734, a 740-family CPU, and a NEC uPD77C20 DSP) is re-created in C.

- **M1:** a 740 CPU emulator and machine model run the UK V2.01 4.04 ROM and capture its DSP frames; golden frame
  streams for the corpus in `tests/golden/`.
- **M2:** a Holmes-style formant synthesizer at 10 kHz, calibrated against reference recordings and tuned in
  listening tests (`src/synth`, `xtras/calibration/`). The DSP program is lost, so this part is new, not a port.
- **M3:** a Ghidra MELPS 740 module and a fully labelled ROM project rebuilt from `ghidra740/annotations/`.
- **M4:** every firmware routine ported to C (`src/firmware`) and proven against the ROM, routine by routine
  (hybrid engine: the emulator with the C swapped in) and end to end.
- **M5:** a CPU-less board model and native engine run the C firmware with no ROM, cycle-exact with the hybrid.
  The emulator moved to `tests/oracle/` as the test oracle; `apollo2.exe` is built from `src/` alone.
- **After M5:** follow-up cleanups, a CLI hardening pass, `build.bat dist` (a release zip with `apollo2.exe`,
  `README.txt` and `manual.txt`), and the move of docs, the manual, photos and the ROM into `xtras/`.
- **Orpheus back end:** the synthesizer of Dolphin's Orpheus (`a3s.dll`, the Apollo's software successor), recovered
  with capstone and checked against a Unicorn oracle, is ported to `src/orpheus/` (`--synth orpheus`)
  (spec `xtras/superpowers/specs/2026-10-16-orpheus-synth-analysis.md`; tools `xtras/orpheus/`). It is equal to the
  oracle sample for sample, including an x87 80-bit model where the DLL truncates. It is the default synth;
  `--synth holmes` gives M2's.

Still open (all optional): synth sound quality against the Juno reference (formant levels, treble, calibration
refit), what the DSP does with b13 bits 6/4/3 and b14-b15, and test/oracle polish. Out of scope so far: serial
replies, the parallel port, option-ROM languages.

## Build and test

- `cmd //c ".\build.bat"` builds; `test [FILTER]` runs suites; `test-norom` runs them as if no ROM existed, then
  `tests\check_cli.bat`; `dist` builds only the program and the zip. MSVC 2022, C11, `/W4 /WX`.
- The full suite takes about 16 minutes: run it in the background. A clean run prints
  `46 suite(s), 0 failure(s), 0 skip(s)`.
- The ROM is `xtras/UK_V2.01_4.04_ROM.BIN`; `APOLLO2_ROM` overrides the path.
- Never run `apollo2.exe` without `-o FILE` in automation: it plays audio. `--audio-check` is fine.

## Rules

- **Frames are bit-exact.** Never regenerate or edit `tests/golden/*`; a change to the firmware C that alters a
  frame is a bug. Refactors must leave the goldens and the suites' printed counts unchanged, and `apollo2 -o`
  WAVs byte-identical (render the corpus with the old and new exe and compare).
- **Audio is 10 kHz,** mono, 16-bit, with no resampling anywhere.
- **The product needs no ROM.** Nothing under `src/` may include from `tests/` or `tests/oracle/`, and only
  `tests/rom_fixture.c` and the oracle apps `apollo2_frames.c`/`apollo2_coverage.c` may look for the ROM.
  `build.bat` enforces both. Each suite in `tests/suites.h` is `SUITE` (ROM-free) or `SUITE_ROM`.
- **Ghidra:** `ghidra740/annotations/` is the source of truth. Do not modify the Ghidra install or the user's GUI
  project `apollo_ghidra/`.
- **Files:** UTF-8 or ASCII; LF for sources, CRLF for `*.bat` (`.gitattributes`); lines at most 120 columns;
  comments match the surrounding density and style.
- **Process:** new specs and plans go in `xtras/superpowers/`; feature work happens on a branch or worktree and
  is merged only with the user's approval; stage files by explicit path.
