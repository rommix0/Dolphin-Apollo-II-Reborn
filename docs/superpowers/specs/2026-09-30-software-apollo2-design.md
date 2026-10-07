# Software Apollo 2 — Design

**Date:** 2026-09-30
**Status:** Approved. *(rev 11)* Revision 11 records milestone 4.3b: the stress and phrase passes and pros_make_segments in C, so all of prosody before playback runs as C (design: docs/superpowers/specs/2026-10-04-m4-3b-stress-and-phrase-passes-design.md). *(rev 10)* Revision 10 records milestone 4.3a: the segment generator in C (phoneme expansion, the phrase generator, the segment appenders and the declination) and the phoneme and prosody tables converted (design: docs/superpowers/specs/2026-10-03-m4-3a-segment-generator-design.md). *(rev 9)* Revision 9 records milestone 4.2: the command parser in C (matcher, clause scan, dispatcher and every deferred handler), the command tables converted, and the non-local exit for @= (design: docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md). *(rev 8)* Revision 8 records milestone 4.1: text normalisation (tn_convert_clause, tn_append_phonetic) in C, the phonetic strings converted, and a per-call ROM budget (design: docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md). *(rev 7)* Revision 7 records milestone 4.0: the oracle checked against the Harte 6502 vectors, the hybrid replacement infrastructure and the letter-to-sound pilot (design: docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md). *(rev 6)* Revision 6 records milestone 3: the MELPS740 Ghidra module and the annotation-built project, plus the firmware facts the labelling verified (marked *(rev 6)*). Revision 5 records the firmware intonation model and the synthesizer revision chosen in the user's listening test. Revision 4 records what milestone 2 verified (command-driven frame bytes, the raw DSP stream, the calibration). Revision 3 makes the finished project ROM-free (R5). Revision 2 adds facts found while planning milestone 1
(MAME's M50734 register map and the lower-ROM banking); the changes are marked *(rev 2)*.
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
- **Clock and interrupts** *(rev 2)*: the 740 core takes the highest-priority line
  n and uses the vector `$FFFC − 2n`: INT0 `$FFFC` (_INT2 pin), INT1 `$FFFA`
  (_INT1 pin and the UART), INT2 `$FFF8` (timers 1–3), INT3 `$FFF6` (stepper),
  INT4 `$FFF4` (Timer X/CNTR). Lower n wins. The crystal is 8 MHz and one CPU
  cycle is 4 input clocks (2 MHz). Timer n fires every 16·(pre+1)·(reload+1)
  input clocks. The ROM sets timer 3 to pre 0 and reload `$31`, which is 800
  clocks = 200 CPU cycles = **10 kHz**. One frame is 100 ticks, so frames are
  10 ms. Verified (MAME `m740`/`m50734` sources plus the ROM's timer setup).
- **Opcodes used in the 8.2 KB of traced code:** the 6502 base set plus 740 bit
  ops, LDM, TST, COM, CLT and WIT (once). These never appear: SET (so the T flag is
  never enabled), SED, MUL, DIV, RRF, STP, BRK. *(rev 6)* Confirmed over the full code
  (the M3 project's opcode census, every byte of ROM, the lower-ROM window and RAMCODE classified):
  60 mnemonics. The 740-only ones are BBC 79, BBS 36, BRA 44, CLB 42, SEB 27, LDM 156, TST 4,
  CLT 1 and WIT 1 (at `$C602`). None of SET, SED, MUL, DIV, STP or BRK occurs, and neither do RRF
  or COM: the trace's COM above was wrong. The 6502 decimal flag is only ever cleared (CLD 1).
- **Synthesizer:** an NEC µPD77C20 ("D77C20AC 295", "LSI PFSA") mask-ROM DSP. Its
  program is not available. Its serial output feeds 4094 shift registers and then
  an MX7541 12-bit DAC. This spec replaces it entirely.
- **RAM:** a 32 KB µPD43256 chip. A PAL ("DSD3") decodes addresses. There are three
  empty option-ROM sockets (I5–I7).

### 1.2 Memory map

| Range | Contents | Status |
|---|---|---|
| `$0000–$00D9` | Zero-page RAM | Verified *(rev 2: the M50734 SFRs start at `$DA`)* |
| `$00DA–$00FF` | SFRs (§1.3) | Verified |
| `$0100–$3FFF` | External RAM. Buffers seen at `$0200`, `$0A00`, `$0E00`, `$1000–$20xx` | Extent is a hypothesis |
| `$4000–$7FFF` | ROM file `0x4000–0x7FFF` while P2 bit 7 = 0; **file `0x0000–0x3FFF` while P2 bit 7 = 1** | Verified *(rev 2)*: the two-letter rule index at `0x4200` has A–J pointers into `0x5625–0x8FDF` and K–Z pointers `$4010–$77D7`, which land on rule starts only in the lower half. The matcher sets P2.7 (`SEB7 $F0` at `$B1C2`) for K–Z and clears it at `$B25A`. |
| `$8000` write | DSP data port | Verified |
| `$8000–$FFFF` | Main ROM while P2 bits 2–6 = `$70`; any other value selects an empty option socket (reads `$FF`) | Bank values are verified; the "empty socket" behaviour is a hypothesis *(rev 2: port P2, not P0)* |

- At boot, `$ADFE–$AFFC` is copied to RAM `$0200–$03FE`. That RAM code switches
  banks and probes `$FFD0 == $78` to detect option ROMs.
- *(rev 6)* **An observed ROM bug in the letter-to-sound rules.** In the two-letter index, the L-M and
  L-O entries point at the same list (lower ROM at `$4317`, read with P2.7 set). Just before it, at
  `$4310–$4316`, sits a one-rule list that nothing points at: an L-O rule for "LOOP". Verified in
  the emulator on patched copies of the ROM (scratch copies only). The shared L-M entry is harmless,
  because L-M words give identical frames whether it points at that list, at the orphan or at
  nothing. Making the orphan reachable, ahead of the real L-O list, changes only
  loop/loops/looped/loophole/bloop of 32 words tested ("sloop" is unchanged). So the bug is that
  the LOOP rule can never fire: an extra end marker, or a pointer that is 7 bytes off. It is not a
  missing L-M list. The port keeps the ROM's behaviour.

### 1.3 Peripherals *(rev 2: register map from MAME `m50734.cpp`; the UART is inferred from the ROM)*

| Address | Register | ROM use |
|---|---|---|
| `$DC–$E1` | Timers 1–3: prescaler/reload pairs | Timer 2 is a startup delay; timer 3 is the 10 kHz DSP tick |
| `$E4` / `$E5` / `$E6` / `$E7` | UART TX / RX / control (bit 4 = RX interrupt enable) / status (bit 1 = TX ready, bits 2–4 = errors) | Serial input. The RX interrupt shares vector `$FFFA` with _INT1. |
| `$E9` / `$EA` / `$EB` | A/D control / data / **P4 input** | Boot copies `P4 & $0F` (DIP switches) into `$10`; bit 0 = parallel mode |
| `$ED` | P2/P3 function | |
| `$EE`/`$EF`, `$F0`/`$F1`, `$F3`/`$F4`, `$F6`/`$F7` | P3, P2, P1, P0 data/direction. A read returns `(pins & ~dir) \| (latch & dir)`. | **P2**: bit 0 = DSP reset (pulsed by `$B38B`), bits 2–6 = ROM bank, bit 7 = lower-ROM window. **P0** bit 5 = host busy line (polarity set by the P0.2 jumper). P3 bits 1–2 = baud jumpers. |
| `$F2` | P1 latched input (parallel data) | Parallel mode only; out of scope |
| `$F5` | P0 function. Bit 5 together with P0 direction bit 5 enables DME data memory. | Never enables DME. The emulator faults if it ever does. |
| `$FB` | UART baud rate | |
| `$FD` / `$FE` / `$FF` | Interrupt control 3 / 2 / 1: enable bits (even) paired with request bits (odd) | ICR2 bit 4/5 = timer 3 enable/request; ICR1 bit 0/1 = _INT1 |

Registers that are not modelled (`$DA–$DB`, `$E2–$E3`, `$E8`, `$EC`, `$F8–$FA`,
`$FC`) are logged whenever they are accessed.

- **Serial input (`$C33C`, via the `$C2B4` handler):** reads data at `$E5` and status
  at `$E7` (`$1C` = error bits). Received bytes are queued by `$C37E`.
- **Idle:** the firmware's only `WIT` is at `$C602`, inside its wait-for-input helper `$C5FE`.
- **Timer ISR `$B2D9`:** sends one 16-bit word (even byte first) to `$8000` every
  10 ticks. There are 10 words per frame, played from the ping-pong buffers at
  `$16A5` and `$16B9`. At `$B306` it swaps buffers. The buffer the main loop just
  finished is the one the zero-page pointer `$2C/$2D` points to. If the main loop
  hasn't finished by the end of a frame, the ISR replays the old frame.
- **DSP reset** *(rev 4)*: `$B38B` pulses P2.0 high (`CLB0`/`SEB0`/`CLB0 $F0`). It is called
  when speech starts (`$B323`, which also clears `$5A`/`$5C`) and when it stops (`$B398`, which
  also disables timer 3). In the emulator, the raw DSP stream is the produced stream one frame
  late. raw[0] is the buffer left over from before the first swap, and the last produced
  frame is never played. Every raw frame is aligned on its `$FFFF` word. No replays were seen
  for the corpus texts.

### 1.4 DSP frame format (20 bytes = 10 words)

| Word | Low byte | High byte |
|---|---|---|
| 0 | b0 ALF (low-frequency amplitude) | b1 FN (nasal/low resonance frequency) |
| 1 | b2 A1 | b3 F1 |
| 2 | b4 A2 | b5 F2 |
| 3 | b6 A3 | b7 F3 |
| 4 | b8 AHF (high-frequency amplitude) | b9 V (degree of voicing, 1..63) |
| 5 | b10 pitch period: 160 − 2·code samples at 10 kHz (the mark-space clamp at $B930); @F0..@FF = codes 2..63 ≈ 64..294 Hz *(rev 5)* | b11 mark-space ratio (@M, clamped by F0) *(rev 4: verified)*. *(rev 6: with c = the b10 code, `$B930` takes b11 = min(@M, 160 − 2c), unsigned; then 47 − 2c if that is not negative (signed) and not below b11; then at least 11 in every case.)* |
| 6 | b12 constant `$17` | b13 transition flags (bit 7 spectral jump, ~~bit 6 onset, bit 4 multi-channel drop~~, bit 3 F0 jump). *(rev 5: bit 7 is set at stop closures ($C0), releases ($80/$90), onsets after silence and many vowel/sonorant boundaries.)* *(rev 6, verified in code and emulator, 0 mismatches over 147 frames; the rev-5 names of bits 6 and 4 were swapped. `$BA3F` compares this frame with the previous one: bit 7 = a frequency (FN/F1/F2/F3) moved by ≥ 16 or an amplitude (ALF/A1/A2/A3/AHF) by ≥ 12; bit 6 = an amplitude **fell** by ≥ 28, or by ≥ 19 to the silent value 1 (a closure); bit 4 = two or more amplitudes **rose** by ≥ 12 (an onset or release); bit 3 = b10 moved by ≥ 7. Bit 5 is never computed, but the rest-frame template has it set, so the first set-up frames carry it.)* |
| 7 | b14 | b15: b14–b15 are a 16-bit value from tables `$B573`/`$B673`, indexed by (ALF−A1−4)·(F1−33). *(rev 6: the value is 82·index. With d = ALF−A1−4 and e = F1−33, the index is 0 when d or e is ≤ 0 (signed). Otherwise d is compared, signed, with the limit table `$B473`[e] = ⌊255/e⌋: the index saturates at `$FF` when limit − d < 0, i.e. whenever d·e would pass 255. e = 1 (limit 255, negative as a signed byte) always saturates.)* |
| 8 | b16 voicing source (@$: $82 default, $58 spike, $9B cut-down, $BA reduced-HF) | b17 voice filter (@$: $4A male, $54 female) *(rev 4: verified)* |
| 9 | `$FF` | `$FF` |

- *(rev 6)* Rest frames (the voice set-up and the frames after an utterance) skip the b14–b17
  lookups, so they keep the template's b16/b17 whatever @$ selects. Our synthesizer uses only b13
  bit 7, so none of the rev-6 frame facts changes the audio.

- **Commands that change frame bytes** *(rev 4, verified by prefixing `_AR.`)*:

  | Command | Bytes changed |
  |---|---|
  | `@K1` (non-male speaker table) | FN, F1, F2 and F3 only (voice B) |
  | `@Bd` | V |
  | `@Fh` | F0; also b11, and F1/F2 at high pitch |
  | `@Mhh` | b11 |
  | `@$o` | b16 and b17 |
  | `@udhh±` | adds to or subtracts from one parameter |
  | `@Ah` (volume) | none |

- **Intonation model** *(rev 5, from the ROM)*. The phrase generator ($D129–$D3DD) writes per-segment arrays: phoneme
  $0200, duration $0600, pitch target $0A00 and flags $0E00, appended by $CA3B. A segment's target is
  ~~($1C26>>3) + $1C25 + $1C30 + $1C23 + 105 plus the centre~~ *(rev 6, verified)*
  ⌊($1C26 + 4)/8⌋ + $1C25 + $1C30 + $1C23 + 105 + `$16`. The baseline term is rounded, because the
  bit shifted out last goes in through the carry, and each later ADC takes the previous carry, the
  terms being signed. The last term, which $CA3B adds when it stores the target, is not the centre
  but zero-page `$16`, the @F+/@F− offset (8 normally, `$10` after @F+, 0 after @F−). The @F centre
  `$1816` = @F·16 is applied only at playback, by $BDD3. The flags array $0E00 is never read:
  patching its stores gives identical frames. The target is built from the per-symbol flags at $1D00:
  - accented head syllables `$60` get +15;
  - stressed but unaccented syllables `$40` get +8;
  - the nucleus (the last accent) `$67` gets +15 −10, a lower peak;
  - the nuclear tail `$07` gets −15, and the final syllable −20;
  - the onset after an accent gets −15;
  - function words carry no accent (the same classification drives Hypermode @H1);
  - the baseline declines across the phrase ($D3DD/$D40D). *(rev 6, verified: the step is
    (start − end) / a symbol count, at most 25, and the baseline drops by it at every symbol that has
    features. The generator computes the step once per text, at position 0. The divisor is the
    first phrase's symbol count, +1 for the final phrase or a text with no punctuation. So in a text
    of several phrases, only the first has a matched step.)*

  At playback, $BDD3 scales each deviation from 128 by @R/4 around the @F centre: @R4 is the
  generator's own contour, the default @R3 is 0.75 of it, and @R8/@R9 are ignored because R is an octal
  digit. The `?` rise is added at playback. *(rev 6, verified: the scaled deviation is rounded
  ($BE2C). @R is capped by the speed through `cmd_speed_limits`: @W1–@W4 leave it alone, @W5 caps it
  at 4, @W6 at 2, and @W7–@W9 at 0, where the contour is the centre plus the microprosody's ±5, so not
  perfectly flat. The final "at least 4" test is signed (CMP #4 / BPL, then ORA #4): results 0–3
  become 4–7, and bit 2 is also set in every result from `$84` up. A port must reproduce this. The
  unreferenced code at $C131 is dead: a b10 store that rounds with error feedback.)* The emulated CPU was verified against py65 (447k random
  cases over the 149 standard opcodes) and MAME semantics (134k cases for the 740-only ops), so the
  frames carry exactly what V2.01 computes.

- **Phoneme tables:** voice A has its count at `$D469` (N = 86) and data at `$D46A`;
  voice B has its count at `$E5E2` and data at `$E5E3`. Each table has 2 rows of N,
  then parameters P1..P10 spaced 5N apart. P1..P10 map to bytes 1, 3, 5, 7, 0, 2,
  4, 6, 8, 9 through the slot map at `$B797`. Values are clamped to 1..63.
  *(rev 6, verified. Row 0 is the rank and row 1 is never read. Each parameter has 5 rows: target,
  fixed contribution, proportion (eighths), external and internal transition duration. The scheme
  matches the Holmes–Mattingly–Shearme (1964) scheme. The higher-ranked of two neighbours sets the
  boundary value, its fixed contribution plus a proportion of the other's target, and the
  transition durations. The Apollo adds a jump when both ranks are ≥ 29, a per-parameter target bias,
  the @u shares and the clamps. "Clamped to 1..63" is only partly true:
  targets go through a signed clamp, so 0 and `$81–$FF` become 1, and `$40–$80` become 63. A boundary
  with proportion 0 is the clamped fixed contribution. Any other boundary of 0 or ≥ 64 (unsigned)
  becomes 1. Where both ranks are ≥ 29, the boundary is the next phoneme's raw target, with no @u and
  no clamp; every table target lies in 1..63. Voice B differs from voice A only in rows 2, 3, 7, 8, 12,
  13, 17 and 18, the target and fixed contribution of P1–P4.)*
- **Symbols to phonemes:** pointer tables at `$9D74` (ASCII) and `$9E74` (≥`$80`).
  Each entry is a list of (phoneme, max duration, min duration) triples ending in
  `$FF`. The decoder is at `$D31B`. Punctuation produces phoneme `$3B` (pause)
  *(rev 6: only when @D ≠ 0, see below)*.
  *(rev 6, verified in disassembly and emulator: the `$9D74` table is used from symbol `$20` up.
  The slots of `$00–$1F` overlap the symbol-feature table and are never decoded. The punctuation
  symbols' own lists are empty. When @D ≠ 0, the generator ($D1D6) calls `$C9B8` twice per
  punctuation symbol. Each call appends a `$3B` segment whose length is @D after `. ? !`,
  (@D>>1)&$F after `, ; :` and (@D>>2)&$F otherwise, at least 1 ($C9E8). With @D0 there is no
  punctuation pause: "a, b." gives 61/65/67/73 frames at @D0/@D1/@D2/@D4. The clause end appends two
  pauses of length 4 ($D18E). For the 17 phonemes that no ASCII symbol reaches, see §10.)*

## 2. Requirements
- **R1 — CLI:** `apollo2 "text" [-o out.wav] [--voice a|b] [--prosody 0-7] [--engine rom|hybrid|native] [--rom PATH] [--hyp b11,b13] [--stream produced|raw] [--text-file FILE] [--audio-check]`. *(rev 4: `--voice b` sends `@K1`; `--stream raw` is for comparison listening only. rev 5: `--prosody` sends `@Rn`, default 4; the output filters are always on.)* *(rev 7: --engine hybrid runs the ported routines as C; --rom-any accepts a ROM whose MD5 differs.)*
  It writes a WAV file, or plays live audio when `-o` is omitted. Text enters
  through the firmware's real serial input path, so device control codes work.
- **R2 — Audio:** mono, 16-bit, **10,000 Hz native**. No resampling anywhere in our
  code. Live playback hands 10 kHz audio to the OS.
- **R3 — Bit-exact firmware:** for every input in the test corpus, the `hybrid` and
  `native` engines emit exactly the same frame stream as the `rom` engine.
- **R4 — Synth acceptance:** intelligible speech in the Holmes/JSRU character, judged
  by the user's listening tests. Objective checks are in §7.2.
- **R5 — Standalone after M5** *(rev 3, user decision 2026-09-30)*: the finished
  project needs no ROM to build, run or test.
  - The firmware's **code** is decompiled to C.
  - Its **data** (letter-to-sound rules, exception dictionary, phoneme parameter
    tables, symbol tables, lookup tables) is converted once into readable,
    structured C source and committed. Rules become entries such as
    `{ "BOR", "ATOR", "LxBQRxTxR" }`, not hex blobs.
  - The raw ROM image itself is never committed (`*.BIN` stays gitignored).
  - Before M5 the ROM is needed: the `rom`/`hybrid` engines run it, and a check
    verifies every committed table byte-for-byte against it when it is present.
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
| `cpu740/` | Instruction-exact 740 core with approximate cycle counts. *(rev 2)* SET (T mode), SED (decimal), STP, MUL, DIV and BRK are never used by the ROM, so they **fault** instead of being emulated. A T flag restored by PLP/RTI faults; a D flag faults when ADC/SBC executes with it set (rev 4: matches the code). | `cpu740_reset`, `cpu740_step(cpu*, const bus*) -> cycles`, `cpu740_irq(cpu*, vector)` | — |
| `machine/` | Apollo 2 board: memory map, banking, SFRs, serial RX, timer, DSP port, hooks | `machine_init(rom, len)`, `machine_feed_serial(bytes, n)`, `machine_run(max_cycles) -> status`, `machine_set_frame_sink(cb)`, `machine_hook(addr, thunk)` | cpu740 |
| `firmware/` | Decompiled firmware in C on a shared memory image *(rev 7)* src/firmware: the fw context on the RAM image, the converted tables in src/firmware/data, and the ported routines; it builds without machine/ or cpu740/ (build.bat checks). | per-routine C functions + thunks; `fw_native_speak(bytes, n, frame_sink)` | machine (hybrid only) |
| engine/ (hybrid) *(rev 7)* | rom_engine_run with a setup hook; thunks.c, the replacement table and register mapping; hybrid_engine.c | hybrid_engine_speak | machine, firmware |
| `synth/` | Holmes parallel-formant synthesizer | `synth_init(cfg)`, `synth_frame(const uint8_t f[20], int16_t out[100])` | — |
| `audio/` *(rev 4)* | WAV writer, live playback (waveOut) | `wav_write`, `playback_pcm16` | — |
| `render/` *(rev 4)* | Frame stream → synthesizer → 16-bit samples, through the `engine/` rom engine | `render_text` | engine, synth |
| `apps/` *(rev 4)* | `apollo2` CLI (R1) and the `apollo2_frames` dev CLI | R1 | all |
| `ghidra740/` | Ghidra SLEIGH module for the 740 + project scripts; annotations are the source of truth (M3 spec) *(rev 6)* | Ghidra extension; `install`, `validate`, `coverage`, `rebuild`, `dump` `.bat` entry points | — |
| `tools/` | `extract_tables.py` (a one-time converter, ROM → readable C, rev 3), `calibrate.py`, corpus/golden runners | scripts | Python |
| `tests/` | unit, differential, golden-frame and synth tests | `apollo2_tests.exe`: runs all suites, non-zero exit on any failure | all |

**Build:** CMake is not installed, so `build.bat` calls `vcvars64.bat` and then `cl`
with `/std:c11 /W4 /WX /wd4244`. C4244 (implicit narrowing) is disabled because
8-bit truncation is intentional throughout the emulator. It builds `apollo2.exe` and
`apollo2_tests.exe` from committed sources only. The ROM (from `APOLLO2_ROM`,
default `./UK_V2.01_4.04_ROM.BIN`) is read at run time by the `rom`/`hybrid` engines
and by ROM-dependent tests, which print SKIP without it *(rev 3)*.

## 4. Machine model (`machine/`)
- **Memory map:** as §1.2. Every access to an address with no model behind it is
  logged once, with the PC and the address. It is never silently accepted.
- **Banking** *(rev 2)*: P2 bits 2–6 select the bank for `$8000–$FFFF` (main ROM =
  `$70`; empty option sockets read `$FF`). P2 bit 7 maps the lower ROM half into
  `$4000–$7FFF`. Undriven P2 pins read the board pull levels, which default to `$70`,
  so the main ROM is selected at reset.
- **Board inputs** *(rev 2)*: the P0–P4 pin levels (jumpers, DIP switches, pulls)
  come from a `machine_config`. The default is all switches off: serial mode, P2
  pulls at `$70`.
- **Serial RX** *(rev 2)*: an input queue. One byte is presented at `$E5` each time
  the CPU sits in the idle `WIT` at `$C602`, the previous byte has been read, and
  the P0.5 busy line is at its idle level (the level recorded at the first idle).
  The RX request raises INT1 while `$E6` bit 4 is set.
- **Timers** *(rev 2)*: timers 1–3 count CPU cycles. Each period is
  4·(pre+1)·(reload+1). On expiry a timer sets its ICR2 request bit; the interrupt
  line is the AND of the enable and request bits.
- **DSP port** *(rev 4)*: writes to `$8000` are counted and grouped into raw 20-byte frames, re-aligned at every P2.0 DSP-reset rising edge. The raw stream depends on CPU timing and is used only for diagnosis and for `--stream raw` listening. The produced stream stays the golden.
- **Produced frames (the golden stream)** *(rev 2)*: whenever the PC reaches
  `$B306` (the ISR's buffer swap), the 20 bytes at the `$2C/$2D` pointer are
  captured as one produced frame. ISR replays never show up in this stream, so it
  doesn't depend on CPU timing. That is what lets C functions, which take zero
  emulated time, be compared byte for byte.
- **Idle detection** *(rev 2)*: a run ends when the CPU is in the `$C602` `WIT`, the
  input queue is empty, the last byte has been read, the timer 3 interrupt is
  disabled (ICR2 bit 4 = 0; speech switches it off when finished) and no
  interrupt line is asserted.
- **Determinism:** the main loop busy-waits on `$5C` for buffer swaps, so the
  produced-frame stream depends only on input bytes. Golden runs execute as fast as possible.
- **Errors:** an invalid opcode, a wrong ROM size or an escaped PC (into an
  unmodelled region) stops the run with the PC, registers and the last 64
  instructions. Nothing ever falls through to a "plausible" frame stream.

## 5. Synthesizer (`synth/`)

**Signal path (10 kHz, float internally, 16-bit output, 100 samples per frame):**
- The impulse source *(rev 5: the user's choice over Rosenberg and sawtooth pulses)* passes a
  low-end peak (110 Hz, Q 0.6, +12 dB) that restores the fundamental. The peak acts on the voiced
  component only. The noise source is mixed in by V and is never scaled with the voicing.
- When V is mid-range (voiced fricatives), the noise is pitch-modulated.
- The excitation drives parallel second-order resonators: FN×ALF, F1×A1, F2×A2,
  F3×A3, plus a fixed resonator at ~3.5 kHz driven by AHF.
- Resonator outputs are summed with alternating signs, then pass a 4th-order Butterworth low-pass
  at 3.8 kHz that models the DAC's reconstruction filter *(rev 5)*.
- Amplitudes ramp linearly across each frame, or within 16 samples when b13 bit 7 is set.
  Frequencies update once per frame. A channel that was silent starts from a cleared resonator
  *(rev 5: this removes the stop-closure and glide-onset thumps without boundary clicks)*.
- F0 follows the period scale (§1.4). The utterance's pitch movement is widened by a gain of 2.0
  around its median pitch, which render computes *(rev 5, chosen by listening)*.
  *(rev 5)* The gain applies around one centre per utterance, so an explicit `@F` register change
  inside an utterance is widened too (its jump doubles in semitones).

**Files:**
- `params.c`: bytes → physical values, using `calib.h`
- `source.c`, `resonator.c`
- `synth.c`: the resonator bank and summing
- `biquad.h/.c`: RBJ peaking and low-pass sections *(rev 5)*

**Calibration (`calib.h`, the only place the synthesizer's calibrated (fitted) constants live;
structural constants such as the `params.c` ranges and clamps and the `source.c` V gate stay in the code):**
1. **FN/F1/F2/F3:** fit each one as linear or log by least squares. The inputs are
   the ROM vowel targets (named via the symbol table) against published RP male
   vowel formants. The better fit wins. Voice B is the cross-check.
2. **Amplitudes:** start with the hypothesis "about 1 dB per code" (consistent with
   the firmware's 12-step jump threshold). Refine against the long-term average
   spectrum of the Juno reference (`dolphin_juno_demo.mp3`).
3. **F0:** *(rev 5)* not fitted. The firmware's period scale is used, with the pitch-range gain chosen by listening (`CAL_F0_RANGE_GAIN`). The Juno reference contains two voices, so the rev 4 quantile fit was wrong.
4. **Hypothesis switches:** b11 (as pulse width) and b13 (resonator state reset) have a
   switch that defaults to off or neutral. They are enabled one at a time during listening
   tests. *(rev 5: the b13 switch is optional and stacks on top of the always-on 16-sample ramp and mute-reset.)* *(rev 4: b14–b17 have no switch yet, see the results below.)*

**Calibration results** *(rev 4; see docs/calibration/report.md)*:
- *(rev 5)* The reference is now only the Juno's male passages (file time 5–14.4 s and 22.3–25 s; 14.5–22 s
  is a higher voice). The five channel levels get LTAS corrections (`CAL_LTAS_x_DB`) on top of the
  Wells fit. The rev 4 ALF/AHF corrections below came from the mixed snippet and were reset.
- *(rev 5)* Listening-test choices: prosody @R4, pitch-range gain 2.0, impulse source, low-end peak
  110 Hz/Q 0.6/+12 dB (raised from +10 dB after the LTAS refit thinned the low end), 3.8 kHz output
  low-pass, a 16-sample transition ramp and mute-reset.
- F1 and F2 are linear and F3 is logarithmic, fitted to Wells (1962) RP male means (RMS 5–6 %).
- Amplitudes are 0.95 dB per code, confirming the hypothesis, with channel offsets fitted to
  Wells's formant levels.
- ALF and AHF channel offsets were corrected once against the Juno long-term average spectrum (ALF −5.1 dB, AHF −3.8 dB); the corpus then peaks at −4.9 dBFS.
- ~~F0 is log-linear, fitted to the Juno snippet's 5/50/95th percentiles.~~ *(rev 5: superseded by the period scale)*
- FN has no reference. Hypothesis H-FN treats it as a low resonance near 256 Hz.
- The source is a unit-power impulse train. Hypothesis b11 switches to a Rosenberg pulse whose
  open quotient is b11/64.
- Hypothesis switches implemented: b11 (pulse width) and b13 bit 7 (resonator reset).
- *(rev 5)* The b11 (Rosenberg) source is level-matched to the impulse source on the voiced path only,
  by `CAL_B11_GAIN_DB` = −8.43 dB; the noise is not scaled. (Unmatched, the Rosenberg render was
  louder and clipped.) The listening A/B therefore compares timbre, not loudness.
- b14–b15 and b16–b17 are decoded (§1.4) but get no switch until a listening-test hypothesis
  exists. Candidates: the @$ source and filter variants.

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

   *(rev 6)* Steps 1 and 2 are done; the design is
   `docs/superpowers/specs/2026-10-01-m3-ghidra740-design.md`. The module matches `cpu740` on all
   256 opcodes (`validate.bat`: 14336 executed vectors, 32 decode-only opcodes, 0 mismatches). The project
   is rebuilt from `ghidra740/annotations` and passes every M3 done criterion: 0 undefined bytes,
   168 named functions, 280 named data items and 80 named RAM bases, with the three indirect jumps
   resolved.
3. **Replacement:**
   - Each C routine gets a thunk keyed by its ROM entry address. The thunk maps
     A/X/Y/flags to and from C arguments and return values, then emulates the RTS.
   - C can call routines that are still in ROM via `rom_call(addr)`.
   - The default porting order is leaf-first. Top-down is allowed where it is clearer.

   *(rev 7)* Built in M4.0: machine_hook (fires only on main-ROM addresses; the C part of a thunk takes 0 emulated cycles and cannot be interrupted, while a ROM routine it calls through rom_call runs normally, with its cycles and any interrupt that falls due), machine_rom_call (C calling ROM; a routine that does not return within 10 M cycles is a fault), and per-routine thunks that set only the outputs the callers use. A C fault (a read outside the converted data) stops the run as MFAULT_FIRMWARE. *(rev 8)* M4.1: fw_rom_call takes a cycle budget per call; helpers keep 10 M cycles, a call that plays speech (pros_make_segments, boot_loop_play from tn_convert_clause's mid-clause flush) gets 1 G cycles. *(rev 9)* M4.2: a ROM call from C that resets the stack (bank_enter, @= with a bank present) is "unwound": machine_rom_call stops right after the reset, the C call chain is abandoned up to the hook that entered C, and the machine goes on where the ROM went.
4. **Special cases:**
   - The RAM-resident bank code becomes plain C.
   - In native mode, the ISR double buffer becomes a direct frame sink: the
     main-loop wait on `$5C` turns into "emit frame".
5. **Data** *(rev 3)*: each ROM table is converted once into committed, readable
   C under `src/firmware/data/`, at the point a routine that uses it is ported.
   - `tools/extract_tables.py` writes the first draft. It is then annotated by
     hand: field names, phoneme symbols as comments, rules as strings.
   - A `firmware_data` test re-encodes every committed table and compares it
     byte-for-byte with its ROM address range. It runs whenever the ROM is
     present and SKIPs otherwise.
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
  740 extensions, interrupts, and faults on the unsupported instructions *(rev 2)*. *(rev 7)* Plus 44,700 Harte 6502 vectors (149 opcodes; BRK, SED and decimal-mode ADC/SBC excluded, P bits 4 and 5 masked (the 740's B as held and T flags)).
- **Golden corpus:** `tests/corpus/*.txt` covers prose, numbers, dates, abbreviations,
  punctuation, spelling mode and every documented control code. M1 covers plain text. M2 adds every frame-affecting control code, spelling mode, phonetic input, mute (Ctrl-X), a boot golden, and a check that @c? queries leave the speech unchanged *(rev 4)*. The golden files are produced-frame streams (§4) stored
  as hex, one frame per line. They are firmware output, not ROM bytes.
  - The `rom` engine records the golden frame streams.
  - `hybrid` and `native` must match them byte for byte (R3).
  - The goldens are regenerated only when `cpu740/` or `machine/` changes, and only
    after review.
- **Per-function differential tests:** during corpus runs, record entry states
  (registers + RAM) at real call sites. Replay them through the ROM routine and its
  C replacement and compare the exit states byte for byte. *(rev 7)* Implemented live (tests/test_diff_lts.c): the machine is cloned at each sampled routine entry, so no states are stored. *(rev 8)* A call that reaches boot_loop_play (a mid-clause flush) cannot finish with interrupts held; it is skipped and counted, and golden_hybrid covers it through the long_clause golden. *(rev 9)* The flush skip is legal only in tn_convert_clause. A call whose ROM copy resets the stack stops right after the reset in both copies and is compared like any other.

### 7.2 Synthesizer
- Each resonator's impulse response peaks at the target frequency within 2%, with
  the expected bandwidth.
- A steady-vowel frame produces spectral peaks at the calibrated F1–F3, within 5%.
- `tools/calibrate.py` writes a report with plots: vowel formant fit vs RP
  references, F0 distribution vs Juno, average spectrum vs Juno.
- *(rev 5)* Biquad sections match their analytic response, and the peak and low-pass meet their design values.
- *(rev 5)* Transitions: a b13 bit 7 frame reaches its amplitudes within 16 samples; a stop closure
  settles (ring ≤ 0.1 of the vowel peak); a glide onset after silence is no louder than 1.3× its steady state.
- The final acceptance is the user's listening test (R4).

## 8. Milestones
1. **ROM speaks frames:**
   - `cpu740` with its unit tests, plus `machine`.
   - Confirm in the emulator how the rule text in file `0x0000–0x3FFF` is reached (P2.7, §1.2).
   - The `rom` engine produces golden frames for the corpus.
2. **Software Apollo 2 v0:**
   - `synth` and calibration.
   - `apollo2 "text" -o out.wav` works end to end on the `rom` engine.
3. **Ghidra module and project:** full code coverage and labelled routines.
   *(rev 6)* **Done 2026-10-02** (M3 spec; §6 steps 1–2).
4. **Progressive replacement:** routines move to C under differential and golden tests.
   *(rev 7)* **M4.0 done 2026-10-03**: the Harte vectors pass; the nine letter-to-sound routines run as C; the hybrid engine reproduces every golden, with all routines and with each alone. The next plans follow the M4.0 spec section 6: M4.1 text normalisation, M4.2 command parser, M4.3 phoneme expansion and prosody, M4.4 parameter tracks, M4.5 frame builder, M4.6 boot, serial, bank and ISR code. *(rev 8)* **M4.1 done** 2026-10-03: tn_convert_clause and tn_append_phonetic run as C (11 routines); the five phonetic strings are converted; a long_clause golden (rom engine) covers the mid-clause flush; diff_lts and golden_hybrid (27 files x 12 modes) pass. Next: M4.2, the command parser. *(rev 9)* **M4.2 done** 2026-10-03: the command parser runs as C (16 routines hooked, the handlers behind a C dispatcher, bank_cmd_select through the ROM cmd_run_next); the command tables are converted (35,878 bytes in all); five new corpus goldens (rom engine) reach every command; diff_lts and golden_hybrid (32 files x 17 modes) pass; the hybrid engine no longer faults on @=1. Next: M4.3, phoneme expansion and prosody, in two plans (M4.3a, M4.3b). *(rev 10)* **M4.3a done** 2026-10-03: the segment generator runs as C (pros_phrase_gen with the phoneme decoder, the segment appenders, ph_lengthen_count and the declination; 24 routines hooked in all); the phoneme and prosody tables are converted (38,197 bytes in all); seven new corpus goldens (rom engine) reach the phrase pauses, the rising and level baselines and the 16-bit segment-index page wraps; diff_lts and golden_hybrid (39 files x 25 modes) pass. *(rev 11)* **M4.3b done** 2026-10-06: the stress and phrase passes run as C (pros_make_segments, pros_mark_stress, pros_mark_phrases and pros_store_phrase_len; 28 routines hooked in all), and the C tn_convert_clause calls the C pros_make_segments directly; three new corpus goldens (rom engine) reach the linking R, words of five or more syllables, a stressed syllabic consonant at the nucleus and a branch the design thought unreachable ($CCBA); diff_lts and golden_hybrid (42 files x 29 modes) pass. Next: M4.4, the parameter tracks (playback).
5. **Native:** every routine and every table is in committed C. The `native`
   engine builds without `cpu740`/`machine`.
   - **Done when** *(rev 3)*: with no ROM file anywhere, `build.bat test` passes
     and the native engine reproduces every committed golden frame stream.
   - The emulator stays in `tests/` as the oracle. It is needed only to
     regenerate goldens or extend the corpus, and that requires the ROM.

## 9. Out of scope
- Parallel-port input and the virtual serial device. The architecture leaves room
  for a front-end to be added later.
- Option-ROM languages (the sockets are empty in this ROM).
- Recovering the real µPD77C20 program.
- Bit-exact audio. Only the frames are bit-exact.
- Serial replies (@c? queries, @L, @E, indexing) *(rev 4)*. The firmware sends them only under the host's RTS/CTS handshake (CNTR pin), which is not modelled.

## 10. Open questions (resolved inside the milestones above)
- ~~How the firmware reaches the rule text in file `0x0000–0x3FFF`~~: resolved statically (P2.7, §1.2); M1 confirms it in the emulator.
- ~~The timer period~~: resolved, 200 CPU cycles (§1.1).
- The exact external RAM extent (M1 logs any access outside `$0000–$3FFF`).
- The P4 DIP switches beyond bit 0. *(rev 4: the P3 baud jumpers are pins 20/21 in docs/apollo2.txt Appendix 1: 9600 default, 1200, 300, 19200.)*
- The meanings of b14–b15, and how the DSP realises the @$ source/filter variants (listening tests). *(rev 4: b11, b16 and b17 are identified, §1.4. rev 5: b13 bit 7 is handled as a fast amplitude transition, §5; the meanings of bits 6, 4 and 3 are still open.)* *(rev 6: ~~what sets b13 bits 6, 4 and 3~~ is resolved on the firmware side (§1.4): an amplitude fall (closure), a multi-channel rise (onset or release) and a jump in b10. How the DSP uses them, and what b14–b15 mean, are still open.)*
- *(rev 5)* Recordings of real units show deeper function-word dips (4–6 semitones) than V2.01's frames give at any @R/@F. Possible causes are the newer V2.02 EPROM in the user's photos, different settings, or the DSP itself.
- ~~The 17 phoneme indices not reachable from ASCII symbols (6, 7, 22, 23, 25, 40,
  41, 58, 77–85), probably reached via `$9E74` (M3).~~ *(rev 6)* Resolved by M3, and only 6 of the
  17 come through `$9E74`. Symbol `$83` gives 7, `$84` gives 6, `$87` gives 21 and 22, and `$8C`
  gives 40; these four come only from @X phonetic text. Symbol `$98` gives 21 and 23 (from the
  letter-to-sound rules, e.g. MEGATON), and `$C0` gives 41 (e.g. LEVEL, NOVEL). Phonemes 77 and
  82–84 come only from the @T sound-effect arrays. Phonemes 25, 58, 78–81 and 85 are never reached
  by any path. Verified in the emulator.
