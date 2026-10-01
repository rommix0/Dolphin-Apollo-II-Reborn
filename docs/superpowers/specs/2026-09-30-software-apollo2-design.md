# Software Apollo 2 — Design

**Date:** 2026-09-30
**Status:** Draft, awaiting review
**Goal:** A software Dolphin Apollo 2. Text typed on a PC goes through the real
firmware logic (first emulated, then decompiled to C) and produces DSP frames.
A from-scratch Holmes parallel-formant synthesizer turns the frames into audio.

## 1. Background (established facts)

The facts below come from analysing `UK_V2.01_4.04_ROM.BIN`: a 64 KB AM27C512,
MD5 `74460801a399983b1a929f57e5ab57d7`. "Verified" means it was checked against the
ROM. "Hypothesis" means it still has to be proven by the work in this spec.

### 1.1 Hardware
- **CPU:** Mitsubishi M50734FP. This is a ROM-less MELPS 740 part, a 6502 superset.
  Vectors sit at `$FFF4–$FFFF`: RESET `$A61C` (`$FFFE`), the serial/aux IRQ
  `$C2B4` (`$FFFA`), the timer IRQ `$B2D9` (`$FFF8`), and `$C3E0` (`$FFF4`). The
  others point at RTI. Verified.
- **Opcodes used in the 8.2 KB of traced code:** the 6502 base set plus 740 bit
  ops, LDM, TST, COM, CLT and WIT (once). These never appear: SET (so the T flag is
  never enabled), SED, MUL, DIV, RRF, STP, BRK. Re-check this once code coverage
  is complete.
- **Synthesizer:** an NEC µPD77C20 ("D77C20AC 295", "LSI PFSA") mask-ROM DSP. Its
  program is not available. Its serial output feeds 4094 shift registers and then
  an MX7541 12-bit DAC. This spec replaces it entirely.
- **RAM:** a 32 KB µPD43256 chip. A PAL ("DSD3") decodes addresses. There are three
  empty option-ROM sockets (I5–I7).

### 1.2 Memory map

| Range | Contents | Status |
|---|---|---|
| `$0000–$00BF` | Zero-page RAM | Verified |
| `$00C0–$00FF` | SFRs (`$DE–$FF` observed) | Verified |
| `$0100–$3FFF` | External RAM. Buffers seen at `$0200`, `$0A00`, `$0E00`, `$1000–$20xx` | Extent is a hypothesis |
| `$4000–$7FFF` | ROM file `0x4000–0x7FFF` | Verified (the 26-entry pointer table at `0x4200` is self-consistent) |
| `$8000` write | DSP data port | Verified |
| `$8000–$FFFF` | ROM, banked by P0 (`$F0`) bits 2–6 | Verified |
| file `0x0000–0x3FFF` | Letter-to-sound rule text | How code reaches it is **unknown** (milestone 1, task 1) |

- At boot, `$ADFE–$AFFC` is copied to RAM `$0200–$03FE`. That RAM code switches
  banks and probes `$FFD0 == $78` to detect option ROMs.

### 1.3 Peripherals (observed use)
- **Serial input (`$C33C`, via the `$C2B4` handler):** reads data at `$E5` and status
  at `$E7` (`$1C` = error bits). The control register is `$E6`. Flow-control
  outputs are `$FD` bits 6–7. Received bytes are queued by `$C37E`. An alternative
  input path reads port `$F2` when `$10` bit 0 is set (probably a parallel port; out
  of scope).
- **Timer ISR `$B2D9`:** sends one 16-bit word (even byte first) to `$8000` every
  10 ticks. There are 10 words per frame, played from the ping-pong buffers at
  `$16A5` and `$16B9`.
- **P0 bit 0:** DSP reset, toggled by routine `$B38B`.

### 1.4 DSP frame format (20 bytes = 10 words)

| Word | Low byte | High byte |
|---|---|---|
| 0 | b0 ALF (low-frequency amplitude) | b1 FN (nasal/low resonance frequency) |
| 1 | b2 A1 | b3 F1 |
| 2 | b4 A2 | b5 F2 |
| 3 | b6 A3 | b7 F3 |
| 4 | b8 AHF (high-frequency amplitude) | b9 V (degree of voicing, 1..63) |
| 5 | b10 F0 (very likely; smoothed prosody/4) | b11 global setting clamped by F0 (hypothesis: pulse width) |
| 6 | b12 constant `$17` | b13 transition flags (bit 7 spectral jump, bit 6 onset, bit 4 multi-channel drop, bit 3 F0 jump) |
| 7 | b14 | b15: b14–b15 are a 16-bit value from tables `$B573`/`$B673`, indexed by (ALF−A1−4)·(F1−33) |
| 8 | b16 | b17: b16–b17 are the voice setting (`$1815` via `$BAEC`/`$BAF0`) |
| 9 | `$FF` | `$FF` |

- **Phoneme tables:** voice A has its count at `$D469` (N = 86) and data at `$D46A`;
  voice B has its count at `$E5E2` and data at `$E5E3`. Each table has 2 rows of N,
  then parameters P1..P10 spaced 5N apart. P1..P10 map to bytes 1, 3, 5, 7, 0, 2,
  4, 6, 8, 9 through the slot map at `$B797`. Values are clamped to 1..63.
- **Symbols to phonemes:** pointer tables at `$9D74` (ASCII) and `$9E74` (≥`$80`).
  Each entry is a list of (phoneme, max duration, min duration) triples ending in
  `$FF`. The decoder is at `$D31B`. Punctuation produces phoneme `$3B` (pause).

## 2. Requirements
- **R1 — CLI:** `apollo2 "text" [-o out.wav] [--voice a|b] [--engine rom|hybrid|native]`.
  It writes a WAV file, or plays live audio when `-o` is omitted. Text enters
  through the firmware's real serial input path, so device control codes work.
- **R2 — Audio:** mono, 16-bit, **10,000 Hz native**. No resampling anywhere in our
  code. Live playback hands 10 kHz audio to the OS.
- **R3 — Bit-exact firmware:** for every input in the test corpus, the `hybrid` and
  `native` engines emit exactly the same frame stream as the `rom` engine.
- **R4 — Synth acceptance:** intelligible speech in the Holmes/JSRU character, judged
  by the user's listening tests. Objective checks are in §7.2.
- **R5 — No Dolphin bytes in the repo:** ROM-derived tables are generated at build
  time from the user's ROM file.
- **R6 — Toolchain:** portable C11, built with MSVC 2022 Build Tools (via
  `vcvars64.bat`) on Windows 10. Python 3.13 (numpy, scipy, librosa) for tools and
  calibration reports only.

## 3. Architecture

```
            ┌───────────── apollo2 CLI ─────────────┐
 text ─────►│ engine: rom | hybrid | native         │──► frames ──► synth/ ──► WAV / live
            └──┬──────────────┬─────────────────────┘
               │              │
          machine/  ◄──── firmware/ (C hooks, hybrid)
               │
           cpu740/
```

| Unit | Responsibility | Interface | Depends on |
|---|---|---|---|
| `cpu740/` | Instruction-exact 740 core (incl. T flag, decimal mode, cycle counts) | `cpu740_reset`, `cpu740_step(cpu*, const bus*) -> cycles`, `cpu740_irq(cpu*, vector)` | — |
| `machine/` | Apollo 2 board: memory map, banking, SFRs, serial RX, timer, DSP port, hooks | `machine_init(rom, len)`, `machine_feed_serial(bytes, n)`, `machine_run(max_cycles) -> status`, `machine_set_frame_sink(cb)`, `machine_hook(addr, thunk)` | cpu740 |
| `firmware/` | Decompiled firmware in C on a shared memory image | per-routine C functions + thunks; `fw_native_speak(bytes, n, frame_sink)` | machine (hybrid only) |
| `synth/` | Holmes parallel-formant synthesizer | `synth_init(cfg)`, `synth_frame(const uint8_t f[20], int16_t out[100])` | — |
| `apollo2/` | CLI, WAV writer, live playback (waveOut) | R1 | all |
| `ghidra740/` | Ghidra SLEIGH module for the 740 | Ghidra extension | — |
| `tools/` | `extract_tables.py`, `calibrate.py`, corpus/golden runners | scripts | Python |
| `tests/` | unit, differential, golden-frame and synth tests | `apollo2_tests.exe`: runs all suites, non-zero exit on any failure | all |

**Build:** CMake is not installed, so `build.bat` calls `vcvars64.bat` and then `cl`
with `/std:c11 /W4 /WX`. It builds `apollo2.exe` and `apollo2_tests.exe`, after
first running `tools/extract_tables.py` against the ROM path given in
`APOLLO2_ROM` (default: `./UK_V2.01_4.04_ROM.BIN`).

## 4. Machine model (`machine/`)
- **Memory map:** as §1.2. Every access to an address with no model behind it is
  logged once, with the PC and the address. It is never silently accepted.
- **Banking:** P0 bits 2–6 select the bank for `$8000–$FFFF`. Empty option sockets
  read `$FF`.
- **Serial RX:** an input queue. A byte is presented at `$E5` and the IRQ is raised
  only while the flow-control line (`$FD`) permits.
- **Timer:** raises the `$FFF8` IRQ every *N* CPU cycles. *N* is a constant derived
  from the SFR setup and checked against the 10 ms/frame assumption.
- **DSP port:** writes to `$8000` are collected. A pulse on P0 bit 0 (DSP reset)
  re-aligns the capture. Every 20 bytes after that become one frame for the sink.
- **Idle detection:** a run ends when the input queue is empty, the CPU has hit the
  `WIT` idle point, and 20 consecutive silent frames (200 ms) have been emitted.
- **Determinism:** the main loop busy-waits on `$5C` for buffer swaps, so the frame
  stream depends only on input bytes. Golden runs execute as fast as possible.
- **Errors:** an invalid opcode, a wrong ROM size or an escaped PC (into an
  unmodelled region) stops the run with the PC, registers and the last 64
  instructions. Nothing ever falls through to a "plausible" frame stream.

## 5. Synthesizer (`synth/`)

**Signal path (10 kHz, float internally, 16-bit output, 100 samples per frame):**
- The pulse source (shaped by F0 and b11) and the noise source are mixed by V.
- When V is mid-range (voiced fricatives), the noise is pitch-modulated.
- The excitation drives parallel second-order resonators: FN×ALF, F1×A1, F2×A2,
  F3×A3, plus a fixed resonator at ~3.5 kHz driven by AHF.
- Resonator outputs are summed with alternating signs.
- Amplitudes ramp linearly across each frame. Frequencies update once per frame.

**Files:**
- `params.c`: bytes → physical values, using `calib.h`
- `source.c`, `resonator.c`
- `synth.c`: the resonator bank and summing

**Calibration (`calib.h`, the only place constants live):**
1. **FN/F1/F2/F3:** fit each one as linear or log by least squares. The inputs are
   the ROM vowel targets (named via the symbol table) against published RP male
   vowel formants. The better fit wins. Voice B is the cross-check.
2. **Amplitudes:** start with the hypothesis "about 1 dB per code" (consistent with
   the firmware's 12-step jump threshold). Refine against the long-term average
   spectrum of the Juno reference (`dolphin_juno_demo.mp3`).
3. **F0:** map b10 → Hz so that the emulator's F0 distribution over the corpus
   matches the Juno reference: median 112 Hz, 5th–95th percentile 74–226 Hz.
4. **Hypothesis switches:** b11 (as pulse width), b13 (resonator state reset) and
   b14–b17 each have a switch that defaults to off or neutral. They are enabled one
   at a time during listening tests.

## 6. Decompilation workflow
1. **`ghidra740/`:** copy Ghidra's 6502 spec and add the 740 extensions:
   - BBS/BBC on A and on zero page
   - SEB/CLB on A and on zero page
   - LDM, TST, COM, INC A / DEC A, BRA
   - CLT/SET, WIT/STP
   - `JSR (zp)`, `JMP (zp)`, and the special-page call `\$FFxx`
   - MUL/DIV/RRF for completeness

   The T flag is assumed to be 0 (§1.1). Validation:
   - Disassembly must match the reference disassembler on all traced instructions.
   - P-code semantics must match `cpu740` on randomised register/memory states,
     using Ghidra's emulator.
2. **Project setup:** apply the memory map; import all known labels (SFRs, buffers,
   tables, slot maps); resolve the RAM jump tables `$1306`, `$150B` and `JMP ($4A)`
   to reach full code coverage.
3. **Replacement:**
   - Each C routine gets a thunk keyed by its ROM entry address. The thunk maps
     A/X/Y/flags to and from C arguments and return values, then emulates the RTS.
   - C can call routines that are still in ROM via `rom_call(addr)`.
   - The default porting order is leaf-first. Top-down is allowed where it is clearer.
4. **Special cases:**
   - The RAM-resident bank code becomes plain C.
   - In native mode, the ISR double buffer becomes a direct frame sink: the
     main-loop wait on `$5C` turns into "emit frame".
5. **Data:** `tools/extract_tables.py` generates C arrays from the user's ROM at
   build time (R5).
6. **Organisation:** one C file per subsystem:
   - command/serial parser
   - text normalisation
   - letter-to-sound rules
   - phoneme expansion
   - prosody
   - parameter tracks
   - frame builder

   Names come from the Ghidra annotations.

## 7. Testing
### 7.1 Firmware (objective, automated)
- **CPU unit tests:** per-instruction vectors for all addressing modes, flags,
  740 extensions, T-flag behaviour and decimal mode.
- **Golden corpus:** `tests/corpus/*.txt` covers prose, numbers, dates, abbreviations,
  punctuation, spelling mode and every documented control code.
  - The `rom` engine records the golden frame streams.
  - `hybrid` and `native` must match them byte for byte (R3).
  - The goldens are regenerated only when `cpu740/` or `machine/` changes, and only
    after review.
- **Per-function differential tests:** during corpus runs, record entry states
  (registers + RAM) at real call sites. Replay them through the ROM routine and its
  C replacement and compare the exit states byte for byte.

### 7.2 Synthesizer
- Each resonator's impulse response peaks at the target frequency within 2%, with
  the expected bandwidth.
- A steady-vowel frame produces spectral peaks at the calibrated F1–F3, within 5%.
- `tools/calibrate.py` writes a report with plots: vowel formant fit vs RP
  references, F0 distribution vs Juno, average spectrum vs Juno.
- The final acceptance is the user's listening test (R4).

## 8. Milestones
1. **ROM speaks frames:**
   - `cpu740` with its unit tests, plus `machine`.
   - Resolve how the rule text in file `0x0000–0x3FFF` is reached.
   - The `rom` engine produces golden frames for the corpus.
2. **Software Apollo 2 v0:**
   - `synth` and calibration.
   - `apollo2 "text" -o out.wav` works end to end on the `rom` engine.
3. **Ghidra module and project:** full code coverage and labelled routines.
4. **Progressive replacement:** routines move to C under differential and golden tests.
5. **Native:** every routine is in C. The `native` engine builds without
   `cpu740`/`machine`. The emulator stays in `tests/` as the oracle.

## 9. Out of scope
- Parallel-port input and the virtual serial device. The architecture leaves room
  for a front-end to be added later.
- Option-ROM languages (the sockets are empty in this ROM).
- Recovering the real µPD77C20 program.
- Bit-exact audio. Only the frames are bit-exact.

## 10. Open questions (resolved inside the milestones above)
- How the firmware reaches the rule text in file `0x0000–0x3FFF` (M1).
- The exact external RAM extent and the timer period in CPU cycles (M1).
- The meanings of b11, b13 and b14–b17 (M2 listening tests).
- The 17 phoneme indices not reachable from ASCII symbols (6, 7, 22, 23, 25, 40,
  41, 58, 77–85), probably reached via `$9E74` (M3).
