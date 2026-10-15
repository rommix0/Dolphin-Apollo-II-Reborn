# Milestone 2 — Software Apollo 2 v0: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `apollo2 "text" -o out.wav` speaks through the emulated original firmware and a from-scratch Holmes-style parallel-formant synthesizer at 10 kHz. It also plays live audio, and is calibrated against published RP vowel formants and the Juno reference.

**Architecture:**
- Milestone 1 delivered `cpu740/`, `machine/` and `engine/`, which turn text into 20-byte DSP frames.
- M2 adds a raw DSP-port capture to `machine/`.
- `synth/` turns frames into 10 kHz samples: a source, five resonators, and parameter mapping through `calib.h`.
- `audio/` writes WAV files and plays live audio.
- `render/` joins engine and synth.
- `src/apps/apollo2.c` is the CLI.
- `tools/calibrate.py` fits `calib.h` and writes a calibration report.
- The golden corpus grows to cover the documented control codes.

**Tech Stack:**
- C11 built with MSVC 2022 Build Tools through `build.bat`.
- `winmm.lib`, the Windows waveOut API, for live playback.
- Python 3.13 (numpy, scipy, librosa, matplotlib) and ffmpeg for calibration only.

**Spec:** `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (revision 3; Task 11 makes it revision 4). Read §1.4 (frame format), §2 (requirements), §4 (machine model), §5 (synthesizer) and §7 (testing) before starting.

## Global Constraints

- **Language and build:**
  - C11, built only through `build.bat`.
  - Flags: `/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /Isrc /D_CRT_SECURE_NO_WARNINGS`.
  - Code must compile warning-free at `/W4`. In particular:
    - no non-constant aggregate initialisers (C4204);
    - no `(uint8_t)~CONST` casts (C4310); write `(uint8_t)(0xFF ^ CONST)`;
    - no signed/unsigned loop comparisons (C4018);
    - every float literal carries an `f` suffix when it initialises or combines with a `float` (C4305).
- **Running from Git Bash:**
  - `cmd //c ".\build.bat test"` builds and runs every suite.
  - `cmd //c ".\build.bat test <filter>"` runs only the suites whose name contains `<filter>`.
  - The `.\` is required: this environment sets `NoDefaultCurrentDirectoryInExePath`.
- **Audio (spec R2):**
  - Mono, 16-bit, **10,000 Hz native**.
  - No resampling anywhere in our code. Live playback hands 10 kHz audio to the OS.
  - The calibration tool decodes the Juno reference at its native rate and never resamples it.
- **Frames stay bit-exact (spec R3, §7.1):**
  - The six M1 golden files (`hello`, `prose`, `numbers`, `punctuation`, `letters_k_to_z`, `abbreviations`) must stay byte-identical.
  - No task changes how the firmware runs.
- **Calibration constants (spec §5):** every synthesizer constant lives in `src/synth/calib.h`, which `tools/calibrate.py` generates. Code never hard-codes a calibrated value.
- **ROM handling (spec R5):**
  - The raw ROM is never committed. It is read from `APOLLO2_ROM`, or from `./UK_V2.01_4.04_ROM.BIN` by default.
  - Tests that need it print `SKIP` without it.
  - The Juno reference `dolphin_juno_demo.mp3` is never committed either.
- **Never silently accept unmodelled behaviour (spec §4):** unmodelled accesses are logged, and runs that log any fail the golden and speech tests.
- **Commits:** every commit message ends with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`, whatever model writes it.

## Facts this plan relies on (verified 2026-10-01 against the ROM in the M1 emulator)

- **Commands and frame bytes.** Each command, prepended to `_AR.`, changes only these bytes:

  | Command | Bytes changed | Effect |
  |---|---|---|
  | `@K1` | FN, F1, F2, F3 | Voice B, the "non-male" speaker table |
  | `@Mhh` | b11 | The mark-space ratio, clamped by F0 |
  | `@$o` | b16 | Voicing source: `$82` default, `$58` spike, `$9B` cut-down, `$BA` reduced-HF |
  | `@$o` | b17 | Voice filter: `$4A` male, `$54` female |
  | `@Bd` | b9 (V) | Degree of voicing |
  | `@Fh` | b10 (F0) | Pitch; also b11, and at high pitch F1/F2 |
  | `@Ah` (volume) | none | — |

- **Vowel frames.** `_EE.` and the other `_XX.` phonetic vowels produce long runs of one steady frame. The steady voice-A frames for the 11 RP monophthongs are:

  | Symbol | Wells | ALF | FN | A1 | F1 | A2 | F2 | A3 | F3 | AHF | V |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | EE | i | 48 | 32 | 41 | 8 | 40 | 40 | 46 | 40 | 50 | 63 |
  | I | ɪ | 46 | 32 | 49 | 10 | 49 | 35 | 48 | 32 | 46 | 63 |
  | E | e | 46 | 32 | 46 | 19 | 50 | 31 | 48 | 30 | 48 | 63 |
  | AA | æ | 46 | 32 | 45 | 30 | 50 | 23 | 48 | 30 | 46 | 63 |
  | AR | ɑː | 50 | 32 | 50 | 25 | 53 | 9 | 42 | 34 | 42 | 63 |
  | O | ɒ | 46 | 32 | 48 | 20 | 48 | 8 | 37 | 36 | 36 | 63 |
  | AW | ɔː | 48 | 32 | 50 | 16 | 47 | 5 | 38 | 29 | 44 | 63 |
  | OO | ʊ | 45 | 32 | 46 | 10 | 44 | 8 | 32 | 22 | 44 | 63 |
  | UU | uː | 44 | 32 | 44 | 9 | 27 | 7 | 25 | 23 | 42 | 63 |
  | U | ʌ | 43 | 32 | 43 | 27 | 43 | 15 | 39 | 36 | 29 | 63 |
  | ER | ɜː | 47 | 32 | 49 | 18 | 48 | 18 | 45 | 30 | 45 | 63 |

- **Calibration seed.** Least squares against Wells (1962) RP male means (Table 1) gives:

  | Parameter | Linear fit | Log fit | Chosen |
  |---|---|---|---|
  | F1 | Hz = 139.4 + 21.55·code, RMS 6.0 % | RMS 8.7 % | Linear |
  | F2 | Hz = 593.3 + 44.59·code, RMS 5.3 % | RMS 8.4 % | Linear |
  | F3 | RMS 4.9 % | Hz = 1877.5·e^(0.01015·code), RMS 4.8 % | Log |

  - **Amplitudes:** 0.949 dB per code, with F2 −12.8 dB and F3 −26.7 dB relative to F1 (RMS 3.5 dB). This confirms the spec's "about 1 dB per code".
  - **FN:** almost constant at 32 (22–32 across the corpus, 37 with `@K1`), including in nasals. No reference exists for it, so its map is hypothesis H-FN: 8 Hz per code, about 256 Hz.
  - **F0:** voiced frames of the six plain-text goldens have b10 quantiles 28 / 32 / 35 (5th / 50th / 95th percentile). The Juno snippet (5–25 s, pyin) gives 63.6 / 111.3 / 226.5 Hz. A log-linear fit gives `ln Hz = −0.9122 + 0.1792·code`.
- **DSP reset and raw stream:**
  - The DSP reset `$B38B` pulses P2.0 high (`CLB0`, `SEB0`, `CLB0 $F0`). It is called when speech starts (`$B323`) and when it stops (`$B398`).
  - For "Hello world.", "zoo" and the prose sentence:
    - the raw DSP stream has as many frames as the produced stream;
    - raw[0] is the buffer left over from before the first swap;
    - raw[i] equals produced[i−1];
    - the last produced frame is never played;
    - there are no replays and exactly two DSP resets;
    - every raw frame ends in `FFFF`.
- **Queries and other commands:**
  - Query commands (`@W?`, `@L`, …) send no UART bytes in the model, because replies need the host's RTS/CTS handshake.
  - `@W?@A?@V? Hello world.` produces exactly `tests/golden/hello.frm`.
  - Booting with empty text produces exactly 41 frames.
  - All 19 control-code corpus lines in Task 3 run to idle with no unmodelled accesses.

## File Map

| File | Responsibility | Task |
|---|---|---|
| `tests/test_main.c` | Fail when a suite filter matches nothing | 1 |
| `src/machine/m50734.h/.c` | `m50734_is_write_only` ($E4) | 1 |
| `src/machine/machine.h/.c` | Overflow counter for the unmodelled log, input-overflow fault, raw DSP capture with DSP-reset re-alignment | 1, 2 |
| `src/engine/rom_engine.h/.c` | Input-overflow fault, `rom_engine_speak_streams` (produced + raw) | 1, 2 |
| `tests/test_raw_stream.c` | Raw stream vs produced stream on the real ROM | 2 |
| `tests/corpus_list.h` | Corpus names shared by the golden and synth tests | 3 |
| `tests/rom_fixture.h/.c` | Shared test helpers: `test_read_file`, `test_parse_hex` | 3, 6 |
| `tests/corpus/*.txt`, `tests/golden/*.frm` | 19 control-code texts plus a boot golden | 3 |
| `src/synth/resonator.h/.c` | Peak-normalised second-order resonator | 4 |
| `src/synth/source.h/.c` | Impulse or Rosenberg voicing, noise, V mixing with pitch-modulated noise | 5 |
| `src/synth/calib.h` | Generated calibration constants | 6, 10 |
| `src/synth/params.h/.c` | Frame bytes → physical parameters, hypothesis switches | 6 |
| `src/synth/synth.h/.c` | Five-channel parallel bank, amplitude ramps, alternating signs, int16 output | 7 |
| `src/audio/wav.h/.c` | 16-bit mono WAV writer | 8 |
| `src/audio/playback.h/.c` | Blocking waveOut playback and format query | 8 |
| `src/render/render.h/.c` | text → engine → frame stream → samples | 9 |
| `src/apps/apollo2.c` | The `apollo2` CLI (spec R1) | 9 |
| `build.bat` | New library dirs, `winmm.lib`, `apollo2.exe` | 4, 8, 9 |
| `tools/calibrate.py`, `tools/data/*` | Calibration fit and report | 10 |
| `docs/calibration/*` | Generated calibration report | 10 |
| `docs/superpowers/specs/...design.md`, `README.md` | Spec revision 4, usage | 9, 11 |

---

### Task 1: Hardening carried over from M1

**Files:**
- Modify: `tests/test_main.c`, `src/machine/m50734.h`, `src/machine/m50734.c`, `src/machine/machine.h`, `src/machine/machine.c`, `src/engine/rom_engine.c`
- Test: `tests/test_m50734.c`, `tests/test_machine.c`, `tests/test_rom_speak.c`

**Interfaces:**
- Produces:
  - `bool m50734_is_write_only(uint8_t addr);` returns true only for `$E4`. A read of it is logged as unmodelled.
  - The fault value `MFAULT_INPUT_OVERFLOW`, appended to `machine_fault`.
  - The field `unsigned unmodelled_dropped` in `machine`.
  - The test runner exits 1 when a filter matches no suite.

- [ ] **Step 1: Write the failing tests**

In `tests/test_m50734.c`, add at the end of `test_misc_registers`:
```c
    CHECK(m50734_is_write_only(M50734_UART_TX));
    CHECK(!m50734_is_write_only(M50734_UART_RX));
```

In `tests/test_machine.c`, add this function above `void suite_machine(void)` and call it last in `suite_machine`:
```c
static void test_unmodelled_log_limits(void)
{
    memset(g_rom, 0, sizeof g_rom);
    set_vector(0xFFFE, 0x9000);
    boot();
    rd(M50734_UART_TX);                          /* write-only register: a read is unmodelled */
    CHECK_EQ(g_m.unmodelled_count, 1);
    CHECK_EQ(g_m.unmodelled[0].addr, M50734_UART_TX);
    CHECK(!g_m.unmodelled[0].write);
    for (int i = 0; i < 70; i++)
        wr((uint16_t)(0x9000 + i), 0);           /* 70 distinct ROM-space writes */
    CHECK_EQ(g_m.unmodelled_count, MACHINE_MAX_UNMODELLED);
    CHECK_EQ(g_m.unmodelled_dropped, 71 - MACHINE_MAX_UNMODELLED);
}
```

In `tests/test_rom_speak.c`, add this function above `void suite_rom_speak(void)` and call it last in `suite_rom_speak`:
```c
/* Text that does not fit the serial queue is a fault with its own reason, not a silent truncation. */
static void test_input_overflow(void)
{
    static char text[MACHINE_QUEUE_SIZE + 1];
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    memset(text, 'a', MACHINE_QUEUE_SIZE);       /* the queue holds MACHINE_QUEUE_SIZE - 1 bytes */
    text[MACHINE_QUEUE_SIZE] = '\0';
    frame_list fl = { 0 };
    CHECK_EQ(rom_engine_speak(&g_m, rom, len, text, false, &fl), MACHINE_FAULT);
    CHECK_EQ(g_m.fault, MFAULT_INPUT_OVERFLOW);
    CHECK_EQ(fl.count, 0);
    frame_list_free(&fl);
}
```

- [ ] **Step 2: Run and confirm they fail**

Run: `cmd //c ".\build.bat test"`
Expected: the build fails. The compiler reports `m50734_is_write_only` as undefined, and `unmodelled_dropped` and `MFAULT_INPUT_OVERFLOW` as unknown identifiers.

- [ ] **Step 3: Implement**

`src/machine/m50734.h`: add after the `m50734_is_modelled` declaration:
```c
bool m50734_is_write_only(uint8_t addr);   /* $E4 UART TX: reading it is an unmodelled access */
```

`src/machine/m50734.c`: add after `m50734_is_modelled`:
```c
bool m50734_is_write_only(uint8_t addr)
{
    return addr == M50734_UART_TX;
}
```

`src/machine/machine.h`:
- Replace the `machine_fault` typedef with:
```c
typedef enum { MFAULT_NONE, MFAULT_CPU, MFAULT_DME, MFAULT_ESCAPED_PC, MFAULT_INPUT_OVERFLOW } machine_fault;
```
- Directly after the field `unsigned unmodelled_count;`, add:
```c
    unsigned unmodelled_dropped;           /* accesses not logged because the log was full (repeats included) */
```

`src/machine/machine.c`:

1. In `note_unmodelled`, give the `if (m->unmodelled_count < MACHINE_MAX_UNMODELLED) { ... }` block an else branch:
```c
    } else {
        m->unmodelled_dropped++;
    }
```

2. In `bus_read`, replace the SFR check line:
```c
        if (!m50734_is_modelled((uint8_t)a) || m50734_is_write_only((uint8_t)a))
            note_unmodelled(m, a, false);
```

3. In `machine_dump`, replace `k_faults` with:
```c
    static const char *const k_faults[] = {
        "none", "cpu", "DME (data-memory strobe) enabled", "escaped PC (SFR space or deselected ROM bank)",
        "input longer than the serial queue",
    };
```
   At the end of `machine_dump`, after the unmodelled loop, add:
```c
    if (m->unmodelled_dropped)
        fprintf(out, "unmodelled log full: %u more accesses not listed\n", m->unmodelled_dropped);
```

`src/engine/rom_engine.c`: in `rom_engine_speak_cfg`, replace
```c
        else
            st = MACHINE_FAULT;   /* text longer than the input queue */
```
with
```c
        else {
            m->fault = MFAULT_INPUT_OVERFLOW;   /* text longer than the input queue */
            st = MACHINE_FAULT;
        }
```

`tests/test_main.c`: insert directly before the final `printf("%d suite(s), ...` line:
```c
    if (filter && run == 0) {
        fprintf(stderr, "no suite matches \"%s\"\n", filter);
        return 1;
    }
```

- [ ] **Step 4: Run and confirm they pass**

Run: `cmd //c ".\build.bat test"`
Expected: PASS. All suites are `ok` with 0 failures, and `suite_rom_speak` includes the overflow test.

Run: `cmd //c ".\build.bat test nosuchsuite"; echo "exit $?"`
Expected: `no suite matches "nosuchsuite"` and `exit 1`.

Run: `git diff --stat tests/golden`
Expected: no output, because the goldens are unchanged.

- [ ] **Step 5: Commit**

```bash
git add tests/test_main.c src/machine/m50734.h src/machine/m50734.c src/machine/machine.h src/machine/machine.c src/engine/rom_engine.c tests/test_m50734.c tests/test_machine.c tests/test_rom_speak.c
git commit -m "Harden runner, unmodelled log and input overflow (M1 follow-ups)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Raw DSP-port capture with re-alignment on the DSP reset

**Files:**
- Modify: `src/machine/machine.h`, `src/machine/machine.c`, `src/engine/rom_engine.h`, `src/engine/rom_engine.c`, `tests/test_machine.c`, `tests/suites.h`
- Create: `tests/test_raw_stream.c`

**Interfaces:**
- Consumes: `MFAULT_INPUT_OVERFLOW` (Task 1).
- Produces:
  - `APOLLO_DSP_PORT` (`0x8000`).
  - `void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user);`
  - Machine counters `raw_frames`, `dsp_resets` and `raw_discarded` (all `uint64_t`).
  - `machine_status rom_engine_speak_streams(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg, const char *text, frame_list *produced, frame_list *raw);` collects the speaking phase only. `raw` may be NULL. The raw counters are reset after boot.

- [ ] **Step 1: Write the failing tests**

In `tests/test_machine.c`, add above `void suite_machine(void)`:
```c
static uint8_t g_raw[4][APOLLO_FRAME_BYTES];
static int g_raw_calls;

static void on_raw(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    (void)user;
    if (g_raw_calls < 4)
        memcpy(g_raw[g_raw_calls], frame, APOLLO_FRAME_BYTES);
    g_raw_calls++;
}

/* Bytes at $8000 group into 20-byte frames; a rising edge on P2.0 (DSP reset) restarts at byte 0. */
static void test_raw_capture(void)
{
    static const uint8_t code[] = {
        0x3C, 0x01, 0xF1,          /* $9000 LDM #$01,$F1   P2.0 is an output (low) */
        0xA2, 0x00,                /*       LDX #$00 */
        0x8E, 0x00, 0x80,          /* $9005 STX $8000      25 bytes 0..24 to the DSP port */
        0xE8,                      /*       INX */
        0xE0, 0x19,                /*       CPX #$19 */
        0xD0, 0xF8,                /*       BNE $9005 */
        0x0F, 0xF0,                /*       SEB0 $F0        DSP reset pulse */
        0x1F, 0xF0,                /*       CLB0 $F0 */
        0xA2, 0x64,                /*       LDX #$64 */
        0x8E, 0x00, 0x80,          /* $9013 STX $8000      20 bytes 100..119 */
        0xE8,                      /*       INX */
        0xE0, 0x78,                /*       CPX #$78 */
        0xD0, 0xF8,                /*       BNE $9013 */
        0x4C, 0x1B, 0x90,          /* $901B JMP $901B */
    };
    memset(g_rom, 0, sizeof g_rom);
    put(0x9000, code, sizeof code);
    set_vector(0xFFFE, 0x9000);
    boot();
    g_raw_calls = 0;
    machine_set_raw_output(&g_m, on_raw, NULL);
    CHECK_EQ(machine_run(&g_m, 5000), MACHINE_CYCLE_LIMIT);
    CHECK_EQ(g_raw_calls, 2);
    CHECK_EQ(g_m.raw_frames, 2);
    CHECK_EQ(g_m.dsp_resets, 1);
    CHECK_EQ(g_m.raw_discarded, 5);              /* bytes 20..24 were cut by the reset */
    CHECK_EQ(g_m.port_writes, 45);
    CHECK_EQ(g_raw[0][0], 0);
    CHECK_EQ(g_raw[0][19], 19);
    CHECK_EQ(g_raw[1][0], 100);
    CHECK_EQ(g_raw[1][19], 119);
}
```
Call `test_raw_capture();` at the end of `suite_machine`.

Create `tests/test_raw_stream.c`:
```c
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static machine g_m;

/* What the DSP actually received (spec §1.3). raw[0] is the buffer left over from before the
   first swap. After that each raw frame is the next produced frame, or a replay of the previous
   one when the main loop was late. The last produced frame may be cut off by the final DSP reset. */
void suite_raw_stream(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    frame_list produced = { 0 }, raw = { 0 };
    machine_status st = rom_engine_speak_streams(&g_m, rom, len, NULL, "Hello world.", &produced, &raw);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(produced.count > 0);
    CHECK(raw.count > 0);
    CHECK(g_m.dsp_resets >= 2);                  /* speech start ($B323) and stop ($B398) */
    CHECK_EQ(g_m.raw_discarded, 0);
    size_t aligned = 0;
    for (size_t i = 0; i < raw.count; i++)
        if (raw.frames[i][18] == 0xFF && raw.frames[i][19] == 0xFF)
            aligned++;
    CHECK_EQ(aligned, raw.count);                /* every frame ends with the $FFFF word */

    size_t next = 0, replays = 0;
    for (size_t i = 1; i < raw.count; i++) {
        if (next < produced.count && memcmp(raw.frames[i], produced.frames[next], APOLLO_FRAME_BYTES) == 0) {
            next++;
        } else if (next > 0 && memcmp(raw.frames[i], produced.frames[next - 1], APOLLO_FRAME_BYTES) == 0) {
            replays++;
        } else {
            fprintf(stderr, "  raw frame %zu is neither produced frame %zu nor a replay\n", i, next);
            g_test_failures++;
            break;
        }
    }
    CHECK(next + 1 >= produced.count);
    printf("  raw: %zu frames for %zu produced (%zu matched, %zu replays), %llu DSP resets\n",
           raw.count, produced.count, next, replays, (unsigned long long)g_m.dsp_resets);
    frame_list_free(&produced);
    frame_list_free(&raw);
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_raw_stream)
```

- [ ] **Step 2: Run and confirm they fail**

Run: `cmd //c ".\build.bat test"`
Expected: the build fails. The compiler reports `machine_set_raw_output` and `rom_engine_speak_streams` as undefined, and `raw_frames`, `dsp_resets` and `raw_discarded` as unknown fields.

- [ ] **Step 3: Implement**

`src/machine/machine.h`:

Add below `#define APOLLO_FRAME_BYTES 20`:
```c
#define APOLLO_DSP_PORT         0x8000   /* DSP data register (write only) */
```
Add these fields after `void *user;`:
```c
    machine_frame_fn on_raw;               /* raw DSP-port frames (machine_set_raw_output) */
    void *raw_user;
    uint8_t raw_buf[APOLLO_FRAME_BYTES];
    unsigned raw_pos;                      /* bytes of the current raw frame received so far */
    bool dsp_reset_level;                  /* last P2.0 pin level */
    uint64_t raw_frames;                   /* complete raw frames delivered */
    uint64_t dsp_resets;                   /* P2.0 rising edges: DSP reset pulses from $B38B */
    uint64_t raw_discarded;                /* partial-frame bytes dropped at a DSP reset */
```
Add after the `machine_set_outputs` declaration:
```c
/* Raw DSP-port capture (spec §4). Bytes written to $8000 are grouped into 20-byte frames the way
   the DSP receives them (even byte of each word first). A DSP reset pulse on P2.0 restarts the
   grouping at byte 0. Unlike the produced frames, this stream depends on CPU timing. */
void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user);
```

`src/machine/machine.c`:

Add `#define P2_DSP_RESET 0x01` next to the other `P2_` defines.

In `bus_write`, replace the SFR branch body with:
```c
    if (is_sfr(a)) {
        if (!m50734_is_modelled((uint8_t)a))
            note_unmodelled(m, a, true);
        m50734_write(&m->sfr, (uint8_t)a, v);
        if ((m->sfr.p0_func & 0x20) && (m->sfr.port_dir[0] & 0x20))
            m->fault = MFAULT_DME;   /* data-memory strobe: never used by the Apollo ROM */
        bool reset = (m50734_port_pins(&m->sfr, 2) & P2_DSP_RESET) != 0;
        if (reset && !m->dsp_reset_level) {   /* rising edge: the DSP restarts at word 0 */
            m->dsp_resets++;
            m->raw_discarded += m->raw_pos;
            m->raw_pos = 0;
        }
        m->dsp_reset_level = reset;
        return;
    }
```
In `bus_write`, replace the `if (a == 0x8000) { ... }` block with:
```c
    if (a == APOLLO_DSP_PORT) {
        m->port_writes++;
        m->raw_buf[m->raw_pos++] = v;
        if (m->raw_pos == APOLLO_FRAME_BYTES) {
            m->raw_pos = 0;
            m->raw_frames++;
            if (m->on_raw)
                m->on_raw(m->raw_user, m->raw_buf);
        }
        return;
    }
```
In `machine_init`, add directly after `m50734_reset(&m->sfr, cfg->port_in);`:
```c
    m->dsp_reset_level = (m50734_port_pins(&m->sfr, 2) & P2_DSP_RESET) != 0;
```
After `machine_set_outputs`, add:
```c
void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user)
{
    m->on_raw = on_raw;
    m->raw_user = user;
}
```

`src/engine/rom_engine.h`: add before `#endif`:
```c
/* Like rom_engine_speak_cfg without boot frames. It also collects the raw DSP-port stream (frames
   as the DSP received them, re-aligned at every DSP reset, ISR replays included) into raw when
   raw != NULL. Both lists cover the speaking phase only, as do the raw_* counters of m. */
machine_status rom_engine_speak_streams(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                        const char *text, frame_list *produced, frame_list *raw);
```

`src/engine/rom_engine.c`: replace the whole body of `rom_engine_speak_cfg` (from its signature to its closing brace) with this shared implementation plus the three public wrappers. Keep `rom_engine_speak` as it is, still calling `rom_engine_speak_cfg`:
```c
static machine_status speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                            const char *text, bool include_boot, frame_list *out, frame_list *raw)
{
    frame_sink sink, raw_sink;
    machine_status st;
    sink.list = out;
    sink.enabled = include_boot;
    raw_sink.list = raw;
    raw_sink.enabled = false;
    if (!machine_init(m, rom, len, cfg))
        return MACHINE_FAULT;
    machine_set_outputs(m, on_frame, &sink);
    if (raw)
        machine_set_raw_output(m, on_frame, &raw_sink);
    st = machine_run(m, ROM_ENGINE_BOOT_CYCLES);
    if (st == MACHINE_IDLE) {
        m->frames_produced = 0;
        m->lower_window_reads = 0;
        m->raw_frames = 0;
        m->dsp_resets = 0;
        m->raw_discarded = 0;
        sink.enabled = true;
        raw_sink.enabled = true;
        if (machine_feed_serial(m, (const uint8_t *)text, strlen(text)) &&
            machine_feed_serial(m, (const uint8_t *)"\r", 1))
            st = machine_run(m, ROM_ENGINE_SPEAK_CYCLES);
        else {
            m->fault = MFAULT_INPUT_OVERFLOW;   /* text longer than the input queue */
            st = MACHINE_FAULT;
        }
    }
    machine_set_outputs(m, NULL, NULL);       /* the sinks live on this stack frame */
    machine_set_raw_output(m, NULL, NULL);
    return st;
}

machine_status rom_engine_speak_cfg(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    const char *text, bool include_boot, frame_list *out)
{
    return speak(m, rom, len, cfg, text, include_boot, out, NULL);
}

machine_status rom_engine_speak_streams(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                        const char *text, frame_list *produced, frame_list *raw)
{
    return speak(m, rom, len, cfg, text, false, produced, raw);
}
```

- [ ] **Step 4: Run and confirm they pass**

Run: `cmd //c ".\build.bat test"`
Expected: PASS, with 11 suites and 0 failures. The `suite_raw_stream` line reads `raw: 105 frames for 105 produced (104 matched, 0 replays), 2 DSP resets`.
If the matched count, replays or resets differ, the test still passes as long as its assertions hold. Report the observed line in your report.

Run: `git diff --stat tests/golden`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/machine/machine.h src/machine/machine.c src/engine/rom_engine.h src/engine/rom_engine.c tests/test_machine.c tests/test_raw_stream.c tests/suites.h
git commit -m "Capture the raw DSP-port stream, re-aligned on the P2.0 DSP reset

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Golden corpus for the control codes, plus a boot golden

**Files:**
- Create: `tests/corpus_list.h`, 19 files `tests/corpus/<name>.txt`, and `tests/golden/<name>.frm` (generated) for each new name plus `boot`.
- Modify: `tests/rom_fixture.h`, `tests/rom_fixture.c`, `tests/test_golden.c`

**Interfaces:**
- Consumes: `rom_engine_speak`, `rom_engine_speak_cfg`, `print_unmodelled`, `test_rom`.
- Produces:
  - `CORPUS_NAMES` (a comma-separated list of string literals), in `tests/corpus_list.h`.
  - `char *test_read_file(const char *path);` (malloc'ed and NUL-terminated, read in binary mode; NULL if missing), in `tests/rom_fixture.h`.
  - Golden files for all corpus names and for `boot`.

- [ ] **Step 1: Write the corpus files**

Each file holds exactly one line, ending in `\n`, with no trailing spaces. Create all of them with `printf` so the bytes are exact (the `mute` line contains the Ctrl-X byte `\x18`):
```bash
cd tests/corpus
printf '%s\n' '@K1 The quick brown fox jumps over the lazy dog.' > voice_b.txt
printf '%s\n' '@V2 One. @V3 Two. @V4 Three. @V5 Four. @V6 Five. @V1 Six.' > presets.txt
printf '%s\n' '@$1 Hello. @$2 Hello. @$5 Hello. @$6 Hello. @$0 Hello.' > source_filter.txt
printf '%s\n' '@M00 Deep voice. @M3F Thin voice. @M16 Normal.' > mark_space.txt
printf '%s\n' '@B1 Whisper this. @B4 Half voiced. @B8 Full voice.' > breathy.txt
printf '%s\n' '@F2 Low pitch. @FE High pitch. @F8 Normal. @F+ Up. @F- Down.' > pitch.txt
printf '%s\n' '@W1 Slow speech. @W9 Fast speech. @W3 Normal speech.' > speed.txt
printf '%s\n' '@R0 Flat and calm. @R9 Very excited!' > prosody.txt
printf '%s\n' '@S1 Spell me.@S0 Back to words.' > spell.txt
printf '%s\n' '@P1 Hello, world! Yes; no: maybe?@P0' > punct_mode.txt
printf '%s\n' '@Q5 One two three. @Q0 @D2 Four. @Db Five.' > pauses.txt
printf '%s\n' '_K_AE_T _A_GP_E_N _Z_OO. @X1 K AE T.@X0' > phonetic.txt
printf '%s\n' '@H1 The cat sat on the mat with a hat.@H0' > hypermode.txt
printf '%s\n' '@T3 @T7 @TB @TF @Tx Done.' > effects.txt
printf '%s\n' '@u105+ @u203- Hello world.' > formant_mod.txt
printf '%s\n' '@?' > version.txt
printf '%s\n' '@A3 Quiet. @AF Loud. @AA Normal.' > volume.txt
printf 'Hello there friend\x18 gone.\n' > mute.txt
printf '%s\n' '@W?@A?@V? Hello world.' > queries.txt
cd ../..
```
Check: `wc -l tests/corpus/*.txt` reports 1 line for each of the 25 files, and `od -c tests/corpus/mute.txt | head -2` shows `030` (octal 0x18) after `friend`.

`tests/corpus_list.h`:
```c
#ifndef CORPUS_LIST_H
#define CORPUS_LIST_H
/* Golden corpus (spec §7.1): tests/corpus/<name>.txt -> tests/golden/<name>.frm.
   The first six are plain text from milestone 1. The rest exercise the control codes of
   docs/apollo2.txt section 6. tools/calibrate.py uses only the plain-text six for its F0 fit. */
#define CORPUS_NAMES \
    "hello", "prose", "numbers", "punctuation", "letters_k_to_z", "abbreviations", \
    "voice_b", "presets", "source_filter", "mark_space", "breathy", "pitch", "speed", \
    "prosody", "spell", "punct_mode", "pauses", "phonetic", "hypermode", "effects", \
    "formant_mod", "version", "volume", "mute", "queries"
#endif
```

- [ ] **Step 2: Move the file reader into the fixture, and rewrite the golden test**

`tests/rom_fixture.h`: add before `#endif`:
```c
/* Whole file as a NUL-terminated malloc'ed string (binary mode, no CRLF translation); NULL if missing. */
char *test_read_file(const char *path);
```

`tests/rom_fixture.c`: add at the end:
```c
char *test_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *s = malloc((size_t)size + 1);
    size_t n = s ? fread(s, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!s)
        return NULL;
    s[n] = '\0';
    return s;
}
```

Replace `tests/test_golden.c` entirely with:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static const char *const k_corpus[] = { CORPUS_NAMES };

static machine g_m;

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

/* Speaks text (with the boot frames if include_boot), then compares the frames with
   tests/golden/<name>.frm, or rewrites that file in update mode. */
static void check_golden(const uint8_t *rom, size_t len, const char *name, const char *text,
                         bool include_boot, bool update)
{
    char frm_path[128];
    snprintf(frm_path, sizeof frm_path, "tests/golden/%s.frm", name);
    frame_list fl = { 0 };
    machine_status st = rom_engine_speak(&g_m, rom, len, text, include_boot, &fl);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    CHECK(fl.count > 0);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    char *hex = frame_list_hex(&fl);

    if (update) {
        if (st == MACHINE_IDLE && fl.count > 0 && g_m.unmodelled_count == 0) {
            CHECK(write_file(frm_path, hex));
            printf("  updated %s (%zu frames)\n", frm_path, fl.count);
        } else {
            fprintf(stderr, "  NOT updating %s: %s\n", frm_path,
                    st != MACHINE_IDLE ? "run did not reach idle"
                    : fl.count == 0 ? "no frames produced" : "unmodelled accesses logged");
            g_test_failures++;
        }
    } else {
        char *want = test_read_file(frm_path);
        if (!want) {
            fprintf(stderr, "  missing %s: run once with APOLLO2_UPDATE_GOLDEN=1 and review\n", frm_path);
            g_test_failures++;
        } else {
            if (strcmp(hex, want) != 0) {
                report_diff(name, hex, want);
                g_test_failures++;
            }
            free(want);
        }
    }
    free(hex);
    frame_list_free(&fl);
}

/* Spec section 4: the produced-frame stream does not depend on CPU timing. Scale the cycles the
   peripheral timers see (1/4 = timers 4x slower relative to the CPU, 3/1 = 3x faster). */
static void test_timing_invariance(const uint8_t *rom, size_t len)
{
    static const uint32_t k_scale[][2] = { { 1, 4 }, { 3, 1 } };
    char *text = test_read_file("tests/corpus/hello.txt");
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(text != NULL);
    CHECK(want != NULL);
    if (text && want) {
        strip_newline(text);
        for (size_t i = 0; i < sizeof k_scale / sizeof k_scale[0]; i++) {
            machine_config cfg;
            frame_list fl = { 0 };
            machine_default_config(&cfg);
            cfg.cycle_num = k_scale[i][0];
            cfg.cycle_den = k_scale[i][1];
            machine_status st = rom_engine_speak_cfg(&g_m, rom, len, &cfg, text, false, &fl);
            if (st != MACHINE_IDLE)
                machine_dump(&g_m, stderr);
            CHECK_EQ(st, MACHINE_IDLE);
            print_unmodelled(&g_m);
            CHECK_EQ(g_m.unmodelled_count, 0);
            char *hex = frame_list_hex(&fl);
            if (strcmp(hex, want) != 0) {
                fprintf(stderr, "  timer scale %u/%u:", (unsigned)cfg.cycle_num, (unsigned)cfg.cycle_den);
                report_diff("hello", hex, want);
                g_test_failures++;
            }
            printf("  timer scale %u/%u: %zu frames, %llu CPU cycles\n", (unsigned)cfg.cycle_num,
                   (unsigned)cfg.cycle_den, fl.count, (unsigned long long)g_m.cpu.cycles);
            free(hex);
            frame_list_free(&fl);
        }
    }
    free(text);
    free(want);
}

/* Query commands (@c?) only queue a reply for the host (which needs the RTS/CTS handshake);
   they must not change the speech. */
static void test_queries_are_silent(void)
{
    char *queries = test_read_file("tests/golden/queries.frm");
    char *hello = test_read_file("tests/golden/hello.frm");
    CHECK(queries != NULL);
    CHECK(hello != NULL);
    if (queries && hello)
        CHECK(strcmp(queries, hello) == 0);
    free(queries);
    free(hello);
}

void suite_golden(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    bool update = getenv("APOLLO2_UPDATE_GOLDEN") != NULL && getenv("APOLLO2_UPDATE_GOLDEN")[0] != '\0';

    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char txt_path[128];
        snprintf(txt_path, sizeof txt_path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(txt_path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        check_golden(rom, len, k_corpus[c], text, false, update);
        free(text);
    }
    check_golden(rom, len, "boot", "", true, update);   /* the startup announcement */
    if (!update) {
        test_queries_are_silent();
        test_timing_invariance(rom, len);
    }
}
```
(This also fixes the M1 follow-up: an empty `APOLLO2_UPDATE_GOLDEN` no longer means update mode.)

- [ ] **Step 3: Run and confirm it fails**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL. There is one `missing tests/golden/<name>.frm` line for each of the 19 new names and for `boot`. `test_queries_are_silent` fails because `queries.frm` is missing.

- [ ] **Step 4: Generate the new goldens and review them**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: 26 `updated tests/golden/<name>.frm (N frames)` lines, every N > 0, and 0 failures.

Then check:
- `git status --short tests/golden` lists only the 20 new files as `??`.
- `git diff --stat tests/golden` prints nothing, because the six M1 goldens are byte-identical.
- `wc -l tests/golden/boot.frm` is 41.
- `cmp tests/golden/queries.frm tests/golden/hello.frm` prints nothing.
- Every new file is LF-only, has 40 uppercase hex digits per line, and has every line ending in `FFFF`:
```bash
for f in tests/golden/*.frm; do grep -cv '^[0-9A-F]\{36\}FFFF$' "$f" | grep -qv '^0$' && echo "BAD $f"; grep -c $'\r' "$f" | grep -qv '^0$' && echo "CR $f"; done; echo checked
```
  This must print only `checked`.

If a run does not reach idle, or logs unmodelled accesses, do NOT drop the corpus line silently. Report it with the dump (`DONE_WITH_CONCERNS`).

- [ ] **Step 5: Run everything and commit**

Run: `cmd //c ".\build.bat test"`
Expected: PASS, all suites `ok`.

```bash
git add tests/corpus_list.h tests/rom_fixture.h tests/rom_fixture.c tests/test_golden.c tests/corpus tests/golden
git commit -m "Extend the golden corpus to the documented control codes; add a boot golden

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Synth resonator

**Files:**
- Create: `src/synth/params.h` (structural constants only, completed in Task 6), `src/synth/resonator.h`, `src/synth/resonator.c`, `tests/test_resonator.c`
- Modify: `build.bat` (library directory list), `tests/suites.h`

**Interfaces:**
- Produces:
  - In `src/synth/params.h`: `SYNTH_RATE` (10000), `SYNTH_FRAME_SAMPLES` (100), `SYNTH_FRAME_BYTES` (20).
  - In `src/synth/resonator.h`:
    - `typedef struct { float a, b, c, y1, y2; } resonator;`
    - `void resonator_reset(resonator *r);` clears the state only.
    - `void resonator_set(resonator *r, float f_hz, float bw_hz, float fs);` sets coefficients, keeps the state, and gives gain 1 at `f_hz`.
    - `float resonator_step(resonator *r, float x);`

- [ ] **Step 1: Write the failing test**

`tests/test_resonator.c`:
```c
#include <math.h>
#include "test.h"
#include "synth/params.h"
#include "synth/resonator.h"

#define TEST_PI 3.14159265358979

enum { N = 2048 };

/* |H(f)| measured from the impulse response by a direct DFT. */
static double response(const float *h, double f)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < N; i++) {
        re += h[i] * cos(2.0 * TEST_PI * f * i / SYNTH_RATE);
        im -= h[i] * sin(2.0 * TEST_PI * f * i / SYNTH_RATE);
    }
    return sqrt(re * re + im * im);
}

/* Spec §7.2: the impulse response peaks at the target within 2 %, with the expected bandwidth. */
static void check(float f_hz, float bw_hz)
{
    static float h[N];
    resonator r;
    resonator_reset(&r);
    resonator_set(&r, f_hz, bw_hz, (float)SYNTH_RATE);
    for (int i = 0; i < N; i++)
        h[i] = resonator_step(&r, i == 0 ? 1.0f : 0.0f);

    double best_f = 0.0, best = 0.0;
    for (double f = 0.8 * f_hz; f <= 1.2 * f_hz && f < SYNTH_RATE / 2.0; f += 1.0) {
        double m = response(h, f);
        if (m > best) {
            best = m;
            best_f = f;
        }
    }
    double lo = best_f, hi = best_f;
    while (lo > 1.0 && response(h, lo) > best / sqrt(2.0))
        lo -= 1.0;
    while (hi < SYNTH_RATE / 2.0 && response(h, hi) > best / sqrt(2.0))
        hi += 1.0;
    double gain_db = 20.0 * log10(best);
    bool ok = fabs(best_f - f_hz) <= 0.02 * f_hz && fabs(gain_db) <= 0.3 && fabs((hi - lo) - bw_hz) <= 0.15 * bw_hz;
    if (!ok)
        fprintf(stderr, "  resonator %.0f/%.0f Hz: peak %.0f Hz at %.2f dB, -3 dB width %.0f Hz\n",
                f_hz, bw_hz, best_f, gain_db, hi - lo);
    CHECK(ok);
}

static void test_state_kept_across_set(void)
{
    resonator r;
    resonator_reset(&r);
    resonator_set(&r, 500.0f, 80.0f, (float)SYNTH_RATE);
    resonator_step(&r, 1.0f);
    float y1 = r.y1;
    resonator_set(&r, 700.0f, 80.0f, (float)SYNTH_RATE);   /* frequencies change once per frame */
    CHECK(r.y1 == y1);
    resonator_reset(&r);
    CHECK(r.y1 == 0.0f && r.y2 == 0.0f);
}

void suite_resonator(void)
{
    check(256.0f, 80.0f);     /* FN */
    check(310.0f, 70.0f);     /* F1 */
    check(1500.0f, 90.0f);    /* F2 */
    check(2500.0f, 140.0f);   /* F3 */
    check(3500.0f, 300.0f);   /* fixed fourth formant */
    test_state_kept_across_set();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_resonator)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c ".\build.bat test resonator"`
Expected: FAIL to compile: `cannot open include file 'synth/params.h'`.

- [ ] **Step 3: Implement**

`build.bat`: replace the line `for %%d in (src\cpu740 src\machine src\engine) do (` with:
```bat
for %%d in (src\cpu740 src\machine src\engine src\synth src\audio src\render) do (
```

`src/synth/params.h`. Task 6 adds the rest of this header:
```c
#ifndef SYNTH_PARAMS_H
#define SYNTH_PARAMS_H
/* Holmes-style parallel-formant synthesizer: structural constants and frame -> parameter mapping (spec §5). */
#include <stdbool.h>
#include <stdint.h>

#define SYNTH_RATE          10000   /* Hz: the firmware's DSP tick; spec R2, never resampled */
#define SYNTH_FRAME_SAMPLES 100     /* one 10 ms frame */
#define SYNTH_FRAME_BYTES   20

#endif
```

`src/synth/resonator.h`:
```c
#ifndef SYNTH_RESONATOR_H
#define SYNTH_RESONATOR_H
/* Second-order digital resonator (Klatt 1980 form), normalised to unity gain at its centre
   frequency, so the amplitude controls of a parallel bank set the spectral peak levels directly. */

typedef struct {
    float a, b, c;     /* y[n] = a*x[n] + b*y[n-1] + c*y[n-2] */
    float y1, y2;
} resonator;

void resonator_reset(resonator *r);                                   /* clears the state only */
void resonator_set(resonator *r, float f_hz, float bw_hz, float fs);  /* coefficients; state kept */
float resonator_step(resonator *r, float x);

#endif
```

`src/synth/resonator.c`:
```c
#include <math.h>
#include "synth/resonator.h"

#define PI_F 3.14159265358979f

void resonator_reset(resonator *r)
{
    r->y1 = 0.0f;
    r->y2 = 0.0f;
}

void resonator_set(resonator *r, float f_hz, float bw_hz, float fs)
{
    float rad = expf(-PI_F * bw_hz / fs);
    float theta = 2.0f * PI_F * f_hz / fs;
    r->c = -rad * rad;
    r->b = 2.0f * rad * cosf(theta);
    /* |1 - b e^-jw - c e^-2jw| at w = theta, so the gain at f_hz is exactly 1 */
    float re = 1.0f - r->b * cosf(theta) - r->c * cosf(2.0f * theta);
    float im = r->b * sinf(theta) + r->c * sinf(2.0f * theta);
    r->a = sqrtf(re * re + im * im);
}

float resonator_step(resonator *r, float x)
{
    float y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c ".\build.bat test resonator"`
Expected: PASS, `suite_resonator ok`. Then run `cmd //c ".\build.bat test"` and expect all suites `ok`.

- [ ] **Step 5: Commit**

```bash
git add build.bat src/synth/params.h src/synth/resonator.h src/synth/resonator.c tests/test_resonator.c tests/suites.h
git commit -m "Add the synthesizer's peak-normalised formant resonator

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Synth excitation source

**Files:**
- Create: `src/synth/source.h`, `src/synth/source.c`, `tests/test_source.c`
- Modify: `tests/suites.h`

**Interfaces:**
- Consumes: `SYNTH_RATE` (Task 4).
- Produces, in `src/synth/source.h`:
  - `typedef struct { uint32_t noise_state; float phase, prev_flow; } source;`
  - `typedef struct { float f0_hz, voicing, open_quotient; } source_params;`
    - `voicing`: 0 = noise only, 1 = voiced only.
    - `open_quotient`: 0 selects the impulse train; > 0 selects a Rosenberg pulse with that open quotient.
  - `void source_init(source *s);`
  - `float source_step(source *s, const source_params *p, float fs);`
  - Every excitation has unit average power.

- [ ] **Step 1: Write the failing test**

`tests/test_source.c`:
```c
#include <math.h>
#include "test.h"
#include "synth/params.h"
#include "synth/source.h"

static source g_src;

static double mean_square(const source_params *p, int n, double *mean)
{
    double sum = 0.0, sq = 0.0;
    source_init(&g_src);
    for (int i = 0; i < n; i++) {
        double x = source_step(&g_src, p, (float)SYNTH_RATE);
        sum += x;
        sq += x * x;
    }
    if (mean)
        *mean = sum / n;
    return sq / n;
}

static void test_deterministic(void)
{
    static const source_params p = { 125.0f, 0.5f, 0.0f };
    source a, b;
    bool same = true;
    source_init(&a);
    source_init(&b);
    for (int i = 0; i < 1000; i++)
        if (source_step(&a, &p, (float)SYNTH_RATE) != source_step(&b, &p, (float)SYNTH_RATE))
            same = false;
    CHECK(same);
}

static void test_noise(void)
{
    static const source_params p = { 100.0f, 0.0f, 0.0f };
    double mean;
    double ms = mean_square(&p, 100000, &mean);
    CHECK(ms > 0.95 && ms < 1.05);
    CHECK(fabs(mean) < 0.02);
}

static void test_impulse_train(void)
{
    static const source_params p = { 100.0f, 1.0f, 0.0f };
    int pulses = 0;
    bool heights_ok = true;
    source_init(&g_src);
    for (int i = 0; i < 10000; i++) {
        float x = source_step(&g_src, &p, (float)SYNTH_RATE);
        if (x != 0.0f) {
            pulses++;
            if (fabsf(x - 10.0f) > 1e-4f)        /* sqrt(period in samples) */
                heights_ok = false;
        }
    }
    CHECK(pulses >= 99 && pulses <= 101);
    CHECK(heights_ok);
    double ms = mean_square(&p, 10000, NULL);
    CHECK(ms > 0.98 && ms < 1.02);
}

static void test_rosenberg(void)
{
    static const source_params p = { 125.0f, 1.0f, 0.3125f };
    double mean;
    double ms = mean_square(&p, 10000, &mean);  /* exactly 125 pitch periods */
    CHECK(ms > 0.85 && ms < 1.15);
    CHECK(fabs(mean * 10000.0) < 0.5);           /* flow derivative sums to ~0 over whole periods */
}

/* Mid-range voicing (voiced fricatives): noise is louder in the first half of the pitch period. */
static void test_pitch_modulated_noise(void)
{
    source_params p = { 100.0f, 0.5f, 0.0f };
    double first = 0.0, second = 0.0;
    source_init(&g_src);
    for (int i = 0; i < 100000; i++) {
        float x = source_step(&g_src, &p, (float)SYNTH_RATE);
        int pos = i % 100;
        if (pos >= 1 && pos < 50)
            first += x * x;
        else if (pos >= 50)
            second += x * x;
    }
    CHECK(first / second > 8.0);
    p.voicing = 0.1f;                            /* mostly noise: no modulation */
    first = second = 0.0;
    source_init(&g_src);
    for (int i = 0; i < 100000; i++) {
        float x = source_step(&g_src, &p, (float)SYNTH_RATE);
        int pos = i % 100;
        if (pos >= 1 && pos < 50)
            first += x * x;
        else if (pos >= 51)
            second += x * x;
    }
    CHECK(first / second > 0.8 && first / second < 1.25);
}

void suite_source(void)
{
    test_deterministic();
    test_noise();
    test_impulse_train();
    test_rosenberg();
    test_pitch_modulated_noise();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_source)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c ".\build.bat test source"`
Expected: FAIL to compile: `cannot open include file 'synth/source.h'`.

- [ ] **Step 3: Implement**

`src/synth/source.h`:
```c
#ifndef SYNTH_SOURCE_H
#define SYNTH_SOURCE_H
/* Excitation for the parallel-formant bank (spec §5). Voicing (impulse train, or a Rosenberg
   pulse when the b11 hypothesis is on) and noise are mixed by the degree of voicing. In the
   mid range (voiced fricatives) the noise is pitch-modulated. Every excitation has unit
   average power, so V changes the voiced/noise balance but not the loudness. */
#include <stdint.h>

typedef struct {
    uint32_t noise_state;   /* LCG state */
    float phase;            /* position in the current pitch period, 0..1 */
    float prev_flow;        /* Rosenberg glottal flow at the previous sample */
} source;

typedef struct {
    float f0_hz;
    float voicing;          /* 0 = noise only .. 1 = voiced only */
    float open_quotient;    /* 0 = unit impulse train; > 0 = Rosenberg pulse with this open quotient */
} source_params;

void source_init(source *s);
float source_step(source *s, const source_params *p, float fs);

#endif
```

`src/synth/source.c`:
```c
#include <math.h>
#include "synth/source.h"

#define PI_F 3.14159265358979f
#define NOISE_SCALE 1.7320508f   /* uniform on [-sqrt(3), sqrt(3)] has unit power */
#define MOD_LOW 0.25f            /* noise level in the second half of the pitch period */
#define MOD_NORM 1.3719887f      /* 1 / sqrt((1 + MOD_LOW^2) / 2): keeps modulated noise at unit power */

void source_init(source *s)
{
    s->noise_state = 0x12345678u;
    s->phase = 0.0f;
    s->prev_flow = 0.0f;
}

static float noise(source *s)
{
    s->noise_state = s->noise_state * 1664525u + 1013904223u;   /* Numerical Recipes LCG */
    return ((float)(s->noise_state >> 8) / 8388608.0f - 1.0f) * NOISE_SCALE;
}

/* Rosenberg (1971) glottal flow over one period (t = 0..1): rises for 2/3 of the open phase,
   falls for 1/3, then stays closed. */
static float flow(float t, float oq)
{
    float tp = oq * (2.0f / 3.0f), tn = oq / 3.0f;
    if (t < tp)
        return 0.5f * (1.0f - cosf(PI_F * t / tp));
    if (t < oq)
        return cosf(PI_F * (t - tp) / (2.0f * tn));
    return 0.0f;
}

float source_step(source *s, const source_params *p, float fs)
{
    float inc = p->f0_hz / fs;          /* pitch periods per sample */
    float t0 = fs / p->f0_hz;           /* period in samples */
    float voiced;
    if (p->open_quotient > 0.0f) {
        float oq = p->open_quotient;
        float tp = oq * (2.0f / 3.0f), tn = oq / 3.0f;
        /* the flow derivative has mean square (pi^2/8)(1/tp + 1/tn) / t0^2 per sample */
        float k = t0 / sqrtf(PI_F * PI_F / 8.0f * (1.0f / tp + 1.0f / tn));
        float g = flow(s->phase, oq);
        voiced = (g - s->prev_flow) * k;
        s->prev_flow = g;
    } else {
        voiced = s->phase < inc ? sqrtf(t0) : 0.0f;   /* one impulse per period: unit average power */
    }
    float n = noise(s);
    if (p->voicing > 0.25f && p->voicing < 0.75f)     /* voiced fricative: noise follows the glottal cycle */
        n *= (s->phase < 0.5f ? 1.0f : MOD_LOW) * MOD_NORM;
    s->phase += inc;
    if (s->phase >= 1.0f)
        s->phase -= 1.0f;
    return sqrtf(p->voicing) * voiced + sqrtf(1.0f - p->voicing) * n;
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c ".\build.bat test source"`, then `cmd //c ".\build.bat test"`
Expected: PASS for both.

- [ ] **Step 5: Commit**

```bash
git add src/synth/source.h src/synth/source.c tests/test_source.c tests/suites.h
git commit -m "Add the synthesizer excitation: impulse/Rosenberg voicing, noise, V mixing

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Calibration header and frame-to-parameter mapping

**Files:**
- Create: `src/synth/calib.h`, `src/synth/params.c`, `tests/test_params.c`
- Modify: `src/synth/params.h`, `tests/rom_fixture.h`, `tests/rom_fixture.c`, `tests/suites.h`

**Interfaces:**
- Consumes: `SYNTH_*` constants (Task 4).
- Produces:
  - In `src/synth/calib.h`, the macros listed in Step 3. Task 10's generator must write exactly the same set of names.
  - In `src/synth/params.h`:
    - `typedef struct { bool b11_pulse_width, b13_reset; } synth_options;`
    - `typedef struct { float fn_hz, f1_hz, f2_hz, f3_hz, alf, a1, a2, a3, ahf, voicing, f0_hz, open_quotient; bool reset; } synth_params;`
    - `void params_from_frame(const uint8_t frame[SYNTH_FRAME_BYTES], const synth_options *opt, synth_params *p);`
  - In `tests/rom_fixture.h`: `bool test_parse_hex(const char *hex, uint8_t *out, size_t n);`

- [ ] **Step 1: Write the failing test**

`tests/rom_fixture.h`: add before `#endif`:
```c
#include <stdbool.h>
/* Parses 2*n hex digits into out. Returns false on a non-hex character. */
bool test_parse_hex(const char *hex, uint8_t *out, size_t n);
```
`tests/rom_fixture.c`: add at the end:
```c
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool test_parse_hex(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int hi = hex_digit(hex[2 * i]), lo = hi < 0 ? -1 : hex_digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}
```

`tests/test_params.c`:
```c
#include <math.h>
#include "test.h"
#include "rom_fixture.h"
#include "synth/calib.h"
#include "synth/params.h"

/* Steady voice-A frames of _AR (hard) and _EE (heed), recorded from the ROM (plan "Facts"). */
static const char k_ar[] = "3220321935092A222A3F231417000000824AFFFF";
static const char k_ee[] = "3020290828282E28323F231417000000824AFFFF";

static void map(const char *hex, const synth_options *opt, synth_params *p, uint8_t f[SYNTH_FRAME_BYTES])
{
    CHECK(test_parse_hex(hex, f, SYNTH_FRAME_BYTES));
    params_from_frame(f, opt, p);
}

/* The calibrated maps stay near Wells (1962) RP male means: hard 677/1083/2540, heed 285/2373/3088 Hz. */
static void test_frequencies(void)
{
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    map(k_ar, &off, &p, f);
    CHECK(p.f1_hz > 600.0f && p.f1_hz < 760.0f);
    CHECK(p.f2_hz > 950.0f && p.f2_hz < 1220.0f);
    CHECK(p.f3_hz > 2300.0f && p.f3_hz < 2800.0f);
    CHECK(p.fn_hz > 200.0f && p.fn_hz < 320.0f);   /* H-FN: a low resonance near 256 Hz */
    map(k_ee, &off, &p, f);
    CHECK(p.f1_hz > 250.0f && p.f1_hz < 350.0f);
    CHECK(p.f2_hz > 2100.0f && p.f2_hz < 2650.0f);
    CHECK(p.f3_hz > 2600.0f && p.f3_hz < 3300.0f);
}

static void test_amplitudes(void)
{
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    CHECK(CAL_DB_PER_CODE > 0.8f && CAL_DB_PER_CODE < 1.1f);   /* spec §5: about 1 dB per code */
    map(k_ar, &off, &p, f);
    f[2] = 30;
    params_from_frame(f, &off, &p);
    float a30 = p.a1;
    f[2] = 40;
    params_from_frame(f, &off, &p);
    float want = powf(10.0f, 10.0f * CAL_DB_PER_CODE / 20.0f);
    CHECK(fabsf(p.a1 / a30 - want) < 1e-3f * want);
    f[2] = (uint8_t)CAL_REF_CODE;
    params_from_frame(f, &off, &p);
    CHECK(fabsf(p.a1 - powf(10.0f, CAL_G_A1_DB / 20.0f)) < 1e-4f);
    f[2] = 1;                                   /* the firmware uses 1 for silence */
    params_from_frame(f, &off, &p);
    CHECK(p.a1 == 0.0f);
}

static void test_voicing_and_f0(void)
{
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    map(k_ar, &off, &p, f);
    CHECK(p.voicing == 1.0f);                   /* V = 63 */
    f[9] = 1;
    params_from_frame(f, &off, &p);
    CHECK(p.voicing == 0.0f);
    f[9] = 32;
    params_from_frame(f, &off, &p);
    CHECK(fabsf(p.voicing - 0.5f) < 1e-6f);
    f[10] = 32;                                 /* the median F0 code of the plain corpus */
    params_from_frame(f, &off, &p);
    float f32 = p.f0_hz;
    CHECK(f32 > 80.0f && f32 < 180.0f);
    f[10] = 33;
    params_from_frame(f, &off, &p);
    CHECK(p.f0_hz > f32);
    f[10] = 0;
    params_from_frame(f, &off, &p);
    CHECK(p.f0_hz >= CAL_F0_MIN_HZ);
}

static void test_hypothesis_switches(void)
{
    static const synth_options off = { false, false };
    static const synth_options b11 = { true, false };
    static const synth_options b13 = { false, true };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    map(k_ar, &off, &p, f);
    CHECK(p.open_quotient == 0.0f);
    CHECK(!p.reset);
    params_from_frame(f, &b11, &p);
    CHECK(fabsf(p.open_quotient - 20.0f / 64.0f) < 1e-6f);   /* b11 = $14, the default @M setting */
    f[11] = 0x3F;
    params_from_frame(f, &b11, &p);
    CHECK(fabsf(p.open_quotient - 0.9f) < 1e-6f);            /* clamped */
    f[13] = 0x90;                                            /* bit 7: spectral jump */
    params_from_frame(f, &b13, &p);
    CHECK(p.reset);
    params_from_frame(f, &off, &p);
    CHECK(!p.reset);
    f[13] = 0x40;
    params_from_frame(f, &b13, &p);
    CHECK(!p.reset);
}

void suite_params(void)
{
    test_frequencies();
    test_amplitudes();
    test_voicing_and_f0();
    test_hypothesis_switches();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_params)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c ".\build.bat test params"`
Expected: FAIL to compile: `cannot open include file 'synth/calib.h'`.

- [ ] **Step 3: Implement**

`src/synth/calib.h`. These are the initial values from the plan's "Facts". Task 10 regenerates the file with the same macro names:
```c
#ifndef SYNTH_CALIB_H
#define SYNTH_CALIB_H
/* Synthesizer calibration (spec §5): the ONLY place synthesizer constants live.
   GENERATED by tools/calibrate.py. Edit the generator, not this file. */

/* Frequency codes -> Hz. CAL_x_LOG 0: Hz = A + B*code; 1: Hz = A * exp(B*code). */
#define CAL_FN_LOG 0
#define CAL_FN_A 0.0f
#define CAL_FN_B 8.0f            /* hypothesis H-FN: a low resonance, ~256 Hz at the usual code 32 */
#define CAL_F1_LOG 0
#define CAL_F1_A 139.4f
#define CAL_F1_B 21.55f
#define CAL_F2_LOG 0
#define CAL_F2_A 593.3f
#define CAL_F2_B 44.59f
#define CAL_F3_LOG 1
#define CAL_F3_A 1877.5f
#define CAL_F3_B 0.01015f
#define CAL_F4_HZ 3500.0f        /* the fixed fourth formant, driven by AHF */

/* Bandwidths (Hz), fixed */
#define CAL_BN_HZ 80.0f
#define CAL_B1_HZ 70.0f
#define CAL_B2_HZ 90.0f
#define CAL_B3_HZ 140.0f
#define CAL_B4_HZ 300.0f

/* Amplitude codes -> peak level dB = CAL_DB_PER_CODE * (code - CAL_REF_CODE) + CAL_G_x_DB.
   Code <= 1 switches the channel off. */
#define CAL_DB_PER_CODE 0.949f
#define CAL_REF_CODE 50
#define CAL_G_ALF_DB 0.0f
#define CAL_G_A1_DB 0.0f
#define CAL_G_A2_DB -12.8f
#define CAL_G_A3_DB -26.7f
#define CAL_G_AHF_DB -30.0f

/* F0 code (b10) -> Hz = exp(CAL_F0_LNA + CAL_F0_LNB * code), clamped */
#define CAL_F0_LNA -0.9122f
#define CAL_F0_LNB 0.1792f
#define CAL_F0_MIN_HZ 40.0f
#define CAL_F0_MAX_HZ 400.0f

/* Output level: synth units -> int16 full scale (tuned so the corpus peaks near -3 dBFS) */
#define CAL_MASTER_GAIN 0.5f

#endif
```

`src/synth/params.h`: insert before `#endif`:
```c
/* Synthesizer hypothesis switches (spec §5, step 4). All off = neutral; enabled one at a time
   during listening tests. */
typedef struct {
    bool b11_pulse_width;   /* b11 (@M mark-space ratio) sets the glottal open quotient (Rosenberg pulse) */
    bool b13_reset;         /* b13 bit 7 (spectral jump) clears the resonator states at frame start */
} synth_options;

typedef struct {
    float fn_hz, f1_hz, f2_hz, f3_hz;   /* resonator centre frequencies */
    float alf, a1, a2, a3, ahf;         /* linear peak gains; 0 = channel off */
    float voicing;                      /* V (b9): 1 -> 0 (noise) .. 63 -> 1 (voiced) */
    float f0_hz;
    float open_quotient;                /* 0 = impulse source (see synth_options) */
    bool reset;                         /* clear the resonator states at this frame */
} synth_params;

/* Frame bytes (spec §1.4): 0 ALF, 1 FN, 2 A1, 3 F1, 4 A2, 5 F2, 6 A3, 7 F3, 8 AHF, 9 V, 10 F0,
   11 mark-space ratio, 13 transition flags. Bytes 12 and 14-19 are not used by the synthesizer. */
void params_from_frame(const uint8_t frame[SYNTH_FRAME_BYTES], const synth_options *opt, synth_params *p);
```

`src/synth/params.c`:
```c
#include <math.h>
#include "synth/calib.h"
#include "synth/params.h"

static float map_hz(int log_map, float a, float b, int code)
{
    return log_map ? a * expf(b * (float)code) : a + b * (float)code;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Keeps every resonator below 0.45 * fs. */
static float freq(int log_map, float a, float b, int code)
{
    return clampf(map_hz(log_map, a, b, code), 50.0f, 0.45f * (float)SYNTH_RATE);
}

static float gain(int code, float offset_db)
{
    if (code <= 1)
        return 0.0f;   /* the firmware clamps amplitudes to 1..63 and uses 1 for silence */
    return powf(10.0f, (CAL_DB_PER_CODE * (float)(code - CAL_REF_CODE) + offset_db) / 20.0f);
}

void params_from_frame(const uint8_t f[SYNTH_FRAME_BYTES], const synth_options *opt, synth_params *p)
{
    p->fn_hz = freq(CAL_FN_LOG, CAL_FN_A, CAL_FN_B, f[1]);
    p->f1_hz = freq(CAL_F1_LOG, CAL_F1_A, CAL_F1_B, f[3]);
    p->f2_hz = freq(CAL_F2_LOG, CAL_F2_A, CAL_F2_B, f[5]);
    p->f3_hz = freq(CAL_F3_LOG, CAL_F3_A, CAL_F3_B, f[7]);
    p->alf = gain(f[0], CAL_G_ALF_DB);
    p->a1 = gain(f[2], CAL_G_A1_DB);
    p->a2 = gain(f[4], CAL_G_A2_DB);
    p->a3 = gain(f[6], CAL_G_A3_DB);
    p->ahf = gain(f[8], CAL_G_AHF_DB);
    p->voicing = clampf((float)(f[9] - 1) / 62.0f, 0.0f, 1.0f);
    p->f0_hz = clampf(expf(CAL_F0_LNA + CAL_F0_LNB * (float)f[10]), CAL_F0_MIN_HZ, CAL_F0_MAX_HZ);
    p->open_quotient = opt->b11_pulse_width ? clampf((float)f[11] / 64.0f, 0.1f, 0.9f) : 0.0f;
    p->reset = opt->b13_reset && (f[13] & 0x80) != 0;
}
```

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c ".\build.bat test params"`, then `cmd //c ".\build.bat test"`
Expected: PASS for both.

- [ ] **Step 5: Commit**

```bash
git add src/synth/calib.h src/synth/params.h src/synth/params.c tests/test_params.c tests/rom_fixture.h tests/rom_fixture.c tests/suites.h
git commit -m "Map frame bytes to synthesizer parameters through calib.h

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Parallel-formant synthesizer

**Files:**
- Create: `src/synth/synth.h`, `src/synth/synth.c`, `tests/test_synth.c`
- Modify: `tests/suites.h`. Also `src/synth/calib.h`, but only the `CAL_MASTER_GAIN` value, in Step 4.

**Interfaces:**
- Consumes: `resonator_*` (Task 4), `source_*` (Task 5), `params_from_frame`, `synth_options` and the `CAL_*` macros (Task 6), `test_read_file` and `CORPUS_NAMES` (Task 3), `test_parse_hex` (Task 6).
- Produces, in `src/synth/synth.h`:
  - `typedef struct { synth_options opt; source src; resonator res[5]; float gain[5]; uint64_t clipped; } synth;`
  - `void synth_init(synth *s, const synth_options *opt);` (`opt` NULL means all hypotheses off)
  - `void synth_frame(synth *s, const uint8_t frame[SYNTH_FRAME_BYTES], int16_t out[SYNTH_FRAME_SAMPLES]);`
  - The spec's `synth_init(cfg)` / `synth_frame(f, out)` interface, with an explicit state pointer.

- [ ] **Step 1: Write the failing test**

`tests/test_synth.c`:
```c
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "synth/synth.h"

#define TEST_PI 3.14159265358979

static synth g_s;
static const char k_silence[] = "012001200117011F013F201417201924824AFFFF";
static const char k_ar[] = "3220321935092A222A3F231417000000824AFFFF";
static const char k_ee[] = "3020290828282E28323F231417000000824AFFFF";

static void frame(const char *hex, uint8_t f[SYNTH_FRAME_BYTES])
{
    CHECK(test_parse_hex(hex, f, SYNTH_FRAME_BYTES));
}

static void test_silence_is_zero(void)
{
    uint8_t f[SYNTH_FRAME_BYTES];
    int16_t out[SYNTH_FRAME_SAMPLES];
    bool zero = true;
    frame(k_silence, f);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 5; i++) {
        synth_frame(&g_s, f, out);
        for (int n = 0; n < SYNTH_FRAME_SAMPLES; n++)
            if (out[n] != 0)
                zero = false;
    }
    CHECK(zero);
}

static void test_deterministic(void)
{
    static int16_t a[20 * SYNTH_FRAME_SAMPLES], b[20 * SYNTH_FRAME_SAMPLES];
    uint8_t f[SYNTH_FRAME_BYTES];
    frame(k_ar, f);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 20; i++)
        synth_frame(&g_s, f, a + i * SYNTH_FRAME_SAMPLES);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 20; i++)
        synth_frame(&g_s, f, b + i * SYNTH_FRAME_SAMPLES);
    CHECK(memcmp(a, b, sizeof a) == 0);
}

/* Spec §7.2: a steady vowel frame produces spectral peaks at the calibrated F1-F3 within 5 %.
   Noise excitation (V = 1) and 77 averaged 256-sample segments give a smooth spectrum. FN and the
   fixed fourth formant are switched off. A neighbouring loud formant can tilt the spectrum across
   the search window, so each formant's peak is the local maximum nearest its target, not the
   window maximum. */
static void check_peaks(const char *hex)
{
    enum { FRAMES = 200, SEG = 256, SEGS = FRAMES * SYNTH_FRAME_SAMPLES / SEG, MAXSTEPS = 400 };
    static int16_t pcm[FRAMES * SYNTH_FRAME_SAMPLES];
    static double pw[MAXSTEPS + 1];
    static const synth_options off = { false, false };
    uint8_t f[SYNTH_FRAME_BYTES];
    synth_params p;
    float target[3];
    frame(hex, f);
    f[0] = 1;   /* ALF off */
    f[8] = 1;   /* AHF off */
    f[9] = 1;   /* V = 1: noise only */
    synth_init(&g_s, NULL);
    for (int i = 0; i < FRAMES; i++)
        synth_frame(&g_s, f, pcm + i * SYNTH_FRAME_SAMPLES);
    params_from_frame(f, &off, &p);
    target[0] = p.f1_hz;
    target[1] = p.f2_hz;
    target[2] = p.f3_hz;
    for (int k = 0; k < 3; k++) {
        float lo = 0.85f * target[k];
        int steps = (int)(0.30f * target[k] / 5.0f);
        if (steps > MAXSTEPS)
            steps = MAXSTEPS;
        for (int s = 0; s <= steps; s++) {
            float fr = lo + 5.0f * (float)s;
            pw[s] = 0.0;
            for (int seg = 1; seg < SEGS; seg++) {   /* skip the onset segment */
                double re = 0.0, im = 0.0;
                for (int n = 0; n < SEG; n++) {
                    double w = 0.5 - 0.5 * cos(2.0 * TEST_PI * n / (SEG - 1));
                    double x = w * pcm[seg * SEG + n];
                    double ph = 2.0 * TEST_PI * fr * n / SYNTH_RATE;
                    re += x * cos(ph);
                    im -= x * sin(ph);
                }
                pw[s] += re * re + im * im;
            }
        }
        float best_f = -1.0f;
        for (int s = 1; s < steps; s++) {
            float fr = lo + 5.0f * (float)s;
            if (pw[s] > pw[s - 1] && pw[s] >= pw[s + 1] &&
                (best_f < 0.0f || fabsf(fr - target[k]) < fabsf(best_f - target[k])))
                best_f = fr;
        }
        bool ok = best_f > 0.0f && fabsf(best_f - target[k]) <= 0.05f * target[k];
        if (!ok)
            fprintf(stderr, "  F%d: nearest spectral peak %.0f Hz, calibrated %.0f Hz\n", k + 1, best_f, target[k]);
        CHECK(ok);
    }
}

static void test_b11_switch_changes_voicing(void)
{
    static const synth_options b11 = { true, false };
    static int16_t a[10 * SYNTH_FRAME_SAMPLES], b[10 * SYNTH_FRAME_SAMPLES];
    uint8_t f[SYNTH_FRAME_BYTES];
    frame(k_ar, f);
    synth_init(&g_s, NULL);
    for (int i = 0; i < 10; i++)
        synth_frame(&g_s, f, a + i * SYNTH_FRAME_SAMPLES);
    synth_init(&g_s, &b11);
    for (int i = 0; i < 10; i++)
        synth_frame(&g_s, f, b + i * SYNTH_FRAME_SAMPLES);
    CHECK(memcmp(a, b, sizeof a) != 0);
}

/* Every golden stream synthesizes without clipping. The loudest peaks between -12 and -1 dBFS.
   This uses only committed goldens, so no ROM is needed. */
static void test_corpus_level(void)
{
    static const char *const names[] = { CORPUS_NAMES, "boot" };
    int peak = 0;
    for (size_t c = 0; c < sizeof names / sizeof names[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/golden/%s.frm", names[c]);
        char *hex = test_read_file(path);
        CHECK(hex != NULL);
        if (!hex)
            continue;
        size_t frames = strlen(hex) / (2 * SYNTH_FRAME_BYTES + 1);
        synth_init(&g_s, NULL);
        for (size_t i = 0; i < frames; i++) {
            uint8_t f[SYNTH_FRAME_BYTES];
            int16_t out[SYNTH_FRAME_SAMPLES];
            frame(hex + i * (2 * SYNTH_FRAME_BYTES + 1), f);
            synth_frame(&g_s, f, out);
            for (int n = 0; n < SYNTH_FRAME_SAMPLES; n++)
                if (abs(out[n]) > peak)
                    peak = abs(out[n]);
        }
        if (g_s.clipped)
            fprintf(stderr, "  %s: %llu samples clipped\n", names[c], (unsigned long long)g_s.clipped);
        CHECK_EQ(g_s.clipped, 0);
        free(hex);
    }
    printf("  corpus peak %d (%.1f dBFS)\n", peak, 20.0 * log10(peak / 32767.0));
    CHECK(peak >= 8231);    /* -12 dBFS */
    CHECK(peak <= 29204);   /* -1 dBFS */
}

void suite_synth(void)
{
    test_silence_is_zero();
    test_deterministic();
    check_peaks(k_ar);
    check_peaks(k_ee);
    test_b11_switch_changes_voicing();
    test_corpus_level();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_synth)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c ".\build.bat test synth"`
Expected: FAIL to compile: `cannot open include file 'synth/synth.h'`.

- [ ] **Step 3: Implement**

`src/synth/synth.h`:
```c
#ifndef SYNTH_SYNTH_H
#define SYNTH_SYNTH_H
/* Holmes-style parallel-formant synthesizer (spec §5). Five second-order resonators (FN, F1, F2,
   F3 and a fixed fourth formant) share one excitation. Their outputs are summed with
   alternating signs. Amplitudes ramp linearly across each frame; frequencies update once per
   frame. Output is 16-bit at 10 kHz, 100 samples per frame. */
#include <stdint.h>
#include "synth/params.h"
#include "synth/resonator.h"
#include "synth/source.h"

typedef struct {
    synth_options opt;
    source src;
    resonator res[5];   /* FN, F1, F2, F3, F4 */
    float gain[5];      /* gains reached at the end of the previous frame: the start of each ramp */
    uint64_t clipped;   /* output samples clipped to the int16 range */
} synth;

void synth_init(synth *s, const synth_options *opt);   /* opt NULL = all hypotheses off */
void synth_frame(synth *s, const uint8_t frame[SYNTH_FRAME_BYTES], int16_t out[SYNTH_FRAME_SAMPLES]);

#endif
```

`src/synth/synth.c`:
```c
#include <math.h>
#include <string.h>
#include "synth/calib.h"
#include "synth/synth.h"

static const float k_sign[5] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };   /* spec §5: alternating signs */

void synth_init(synth *s, const synth_options *opt)
{
    memset(s, 0, sizeof *s);
    if (opt)
        s->opt = *opt;
    source_init(&s->src);
}

void synth_frame(synth *s, const uint8_t frame[SYNTH_FRAME_BYTES], int16_t out[SYNTH_FRAME_SAMPLES])
{
    synth_params p;
    source_params sp;
    float freq[5], bw[5], target[5];
    params_from_frame(frame, &s->opt, &p);
    freq[0] = p.fn_hz;
    freq[1] = p.f1_hz;
    freq[2] = p.f2_hz;
    freq[3] = p.f3_hz;
    freq[4] = CAL_F4_HZ;
    bw[0] = CAL_BN_HZ;
    bw[1] = CAL_B1_HZ;
    bw[2] = CAL_B2_HZ;
    bw[3] = CAL_B3_HZ;
    bw[4] = CAL_B4_HZ;
    target[0] = p.alf;
    target[1] = p.a1;
    target[2] = p.a2;
    target[3] = p.a3;
    target[4] = p.ahf;
    for (int k = 0; k < 5; k++) {
        if (p.reset)
            resonator_reset(&s->res[k]);
        resonator_set(&s->res[k], freq[k], bw[k], (float)SYNTH_RATE);
    }
    sp.f0_hz = p.f0_hz;
    sp.voicing = p.voicing;
    sp.open_quotient = p.open_quotient;
    for (int n = 0; n < SYNTH_FRAME_SAMPLES; n++) {
        float x = source_step(&s->src, &sp, (float)SYNTH_RATE);
        float t = (float)(n + 1) / (float)SYNTH_FRAME_SAMPLES;   /* the ramp reaches the target at the frame end */
        float y = 0.0f;
        for (int k = 0; k < 5; k++) {
            float g = s->gain[k] + (target[k] - s->gain[k]) * t;
            y += k_sign[k] * g * resonator_step(&s->res[k], x);
        }
        y *= CAL_MASTER_GAIN * 32767.0f;
        if (y > 32767.0f) {
            y = 32767.0f;
            s->clipped++;
        } else if (y < -32768.0f) {
            y = -32768.0f;
            s->clipped++;
        }
        out[n] = (int16_t)lrintf(y);
    }
    memcpy(s->gain, target, sizeof s->gain);
}
```

- [ ] **Step 4: Run, tune the master gain once, and confirm it passes**

Run: `cmd //c ".\build.bat test synth"`
Read the printed `corpus peak P (x dBFS)` line. If P is outside 8231–29204, or any samples clipped:
- Compute `CAL_MASTER_GAIN_new = 0.5 * 23197 / P`, which targets −3 dBFS. Write it with 4 significant digits and an `f` suffix.
- Change only that one value in `src/synth/calib.h`.
- Re-run. Expect a peak within ±0.5 dB of −3 dBFS.

Expected final result: PASS, with all checks of `suite_synth` `ok`. If the spectral-peak check fails, the stderr line shows the peak found. Diagnose it; do not widen the 5 % tolerance.

Run: `cmd //c ".\build.bat test"`
Expected: all suites `ok`.

- [ ] **Step 5: Commit**

```bash
git add src/synth/synth.h src/synth/synth.c src/synth/calib.h tests/test_synth.c tests/suites.h
git commit -m "Add the parallel-formant synthesizer bank (10 kHz, amplitude ramps, alternating signs)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: WAV writer and live playback

**Files:**
- Create: `src/audio/wav.h`, `src/audio/wav.c`, `src/audio/playback.h`, `src/audio/playback.c`, `tests/test_audio.c`
- Modify: `build.bat` (link `winmm.lib`), `tests/suites.h`

**Interfaces:**
- Produces:
  - In `src/audio/wav.h`: `bool wav_write(const char *path, const int16_t *samples, size_t count, uint32_t rate);` writes mono 16-bit PCM with the canonical 44-byte header.
  - In `src/audio/playback.h`:
    - `bool playback_supported(uint32_t rate);` queries the format without playing.
    - `bool playback_pcm16(const int16_t *samples, size_t count, uint32_t rate, char *err, size_t errlen);` plays and blocks until done.

- [ ] **Step 1: Write the failing test**

`tests/test_audio.c`:
```c
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "audio/playback.h"
#include "audio/wav.h"

static void test_wav_bytes(void)
{
    static const int16_t samples[3] = { 1, -2, 0x1234 };
    static const uint8_t want[] = {
        'R', 'I', 'F', 'F', 42, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x10, 0x27, 0, 0, 0x20, 0x4E, 0, 0, 2, 0, 16, 0,     /* 10000 Hz, 20000 bytes/s, align 2, 16 bit */
        'd', 'a', 't', 'a', 6, 0, 0, 0,
        0x01, 0x00, 0xFE, 0xFF, 0x34, 0x12,
    };
    uint8_t got[64];
    CHECK(wav_write("build/test_audio.wav", samples, 3, 10000));
    FILE *f = fopen("build/test_audio.wav", "rb");
    CHECK(f != NULL);
    if (!f)
        return;
    size_t n = fread(got, 1, sizeof got, f);
    fclose(f);
    CHECK_EQ(n, sizeof want);
    CHECK(memcmp(got, want, sizeof want) == 0);
    CHECK(!wav_write("build/no_such_dir/x.wav", samples, 3, 10000));
}

/* Playback itself is not exercised here (tests must stay silent). The format query must work
   whenever a device exists. */
static void test_playback_query(void)
{
    if (!playback_supported(10000))
        SKIP("no waveOut device accepts 10 kHz mono 16-bit PCM");
    CHECK(playback_supported(10000));
}

void suite_audio(void)
{
    test_wav_bytes();
    test_playback_query();
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_audio)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c ".\build.bat test audio"`
Expected: FAIL to compile: `cannot open include file 'audio/playback.h'`.

- [ ] **Step 3: Implement**

`build.bat`:
- Add `set LIBS=winmm.lib` on the line after `set CFLAGS=...`.
- Append ` %LIBS%` to both `cl` command lines, after `!LIBSRC!`.

`src/audio/wav.h`:
```c
#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Writes mono 16-bit PCM at `rate` Hz with the canonical 44-byte RIFF header. */
bool wav_write(const char *path, const int16_t *samples, size_t count, uint32_t rate);
#endif
```

`src/audio/wav.c`:
```c
#include <stdio.h>
#include "audio/wav.h"

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

bool wav_write(const char *path, const int16_t *samples, size_t count, uint32_t rate)
{
    uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                      1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
    uint32_t data = (uint32_t)(count * 2);
    put32(h + 4, 36 + data);
    put32(h + 24, rate);
    put32(h + 28, rate * 2);
    put32(h + 40, data);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    for (size_t i = 0; ok && i < count; i++) {
        uint8_t b[2];
        put16(b, (uint16_t)samples[i]);
        ok = fwrite(b, 1, 2, f) == 2;
    }
    return fclose(f) == 0 && ok;
}
```

`src/audio/playback.h`:
```c
#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H
/* Live playback through the default waveOut device. Our code hands 10 kHz audio to the OS and
   never resamples it (spec R2). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True if a device accepts mono 16-bit PCM at `rate` (WAVE_FORMAT_QUERY: nothing is played). */
bool playback_supported(uint32_t rate);

/* Plays and blocks until done. On failure returns false with a message in err. */
bool playback_pcm16(const int16_t *samples, size_t count, uint32_t rate, char *err, size_t errlen);

#endif
```

`src/audio/playback.c`:
```c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include "audio/playback.h"

static void pcm_format(WAVEFORMATEX *wf, uint32_t rate)
{
    memset(wf, 0, sizeof *wf);
    wf->wFormatTag = WAVE_FORMAT_PCM;
    wf->nChannels = 1;
    wf->nSamplesPerSec = rate;
    wf->wBitsPerSample = 16;
    wf->nBlockAlign = 2;
    wf->nAvgBytesPerSec = rate * 2;
}

bool playback_supported(uint32_t rate)
{
    WAVEFORMATEX wf;
    pcm_format(&wf, rate);
    return waveOutGetNumDevs() > 0 &&
           waveOutOpen(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_NOERROR;
}

bool playback_pcm16(const int16_t *samples, size_t count, uint32_t rate, char *err, size_t errlen)
{
    WAVEFORMATEX wf;
    HWAVEOUT h;
    WAVEHDR hdr;
    MMRESULT r;
    pcm_format(&wf, rate);
    r = waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        snprintf(err, errlen, "waveOutOpen failed (MMRESULT %u)", (unsigned)r);
        return false;
    }
    memset(&hdr, 0, sizeof hdr);
    hdr.lpData = (LPSTR)samples;
    hdr.dwBufferLength = (DWORD)(count * sizeof samples[0]);
    r = waveOutPrepareHeader(h, &hdr, sizeof hdr);
    if (r == MMSYSERR_NOERROR)
        r = waveOutWrite(h, &hdr, sizeof hdr);
    if (r != MMSYSERR_NOERROR) {
        snprintf(err, errlen, "waveOutWrite failed (MMRESULT %u)", (unsigned)r);
        waveOutUnprepareHeader(h, &hdr, sizeof hdr);
        waveOutClose(h);
        return false;
    }
    while (!(hdr.dwFlags & WHDR_DONE))
        Sleep(10);
    waveOutUnprepareHeader(h, &hdr, sizeof hdr);
    waveOutClose(h);
    return true;
}
```

If `<windows.h>` or `<mmsystem.h>` raises a warning under `/W4 /WX`, wrap only those two includes in `#pragma warning(push, 3)` … `#pragma warning(pop)`. Say so in your report; do not change the global flags.

- [ ] **Step 4: Run and confirm it passes**

Run: `cmd //c ".\build.bat test audio"`, then `cmd //c ".\build.bat test"`
Expected: PASS. `test_playback_query` either passes or prints `SKIP` on a machine without an audio device. Report which one happened.

- [ ] **Step 5: Commit**

```bash
git add build.bat src/audio/wav.h src/audio/wav.c src/audio/playback.h src/audio/playback.c tests/test_audio.c tests/suites.h
git commit -m "Add the WAV writer and waveOut live playback (10 kHz, no resampling)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Render pipeline and the `apollo2` CLI

**Files:**
- Create: `src/render/render.h`, `src/render/render.c`, `src/apps/apollo2.c`, `tests/test_render.c`
- Modify: `build.bat` (build `apollo2.exe`), `tests/suites.h`, `README.md`

**Interfaces:**
- Consumes:
  - `rom_engine_speak_streams` and `frame_list_free` (Task 2).
  - `synth_init`, `synth_frame` and `synth_options` (Tasks 6–7).
  - `wav_write`, `playback_pcm16` and `playback_supported` (Task 8).
- Produces:
  - In `src/render/render.h`:
    - `render_stream { RENDER_STREAM_PRODUCED, RENDER_STREAM_RAW }`
    - `render_options { char voice; render_stream stream; synth_options synth; }`
    - `render_result { int16_t *samples; size_t count, frames; uint64_t clipped; }`
    - `void render_default_options(render_options *);`
    - `machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text, const render_options *opt, render_result *out);`
    - `void render_result_free(render_result *);`
  - `build\apollo2.exe` with the options listed in Step 3.

- [ ] **Step 1: Write the failing test**

`tests/test_render.c`:
```c
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "render/render.h"

static machine g_m;

static int peak_of(const render_result *r)
{
    int peak = 0;
    for (size_t i = 0; i < r->count; i++)
        if (abs(r->samples[i]) > peak)
            peak = abs(r->samples[i]);
    return peak;
}

void suite_render(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    render_options opt;
    render_result a = { 0 }, b = { 0 }, voice_b = { 0 }, raw = { 0 };
    render_default_options(&opt);

    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &a), MACHINE_IDLE);
    CHECK_EQ(a.frames, 105);                                 /* tests/golden/hello.frm */
    CHECK_EQ(a.count, 105 * SYNTH_FRAME_SAMPLES);
    CHECK_EQ(a.clipped, 0);
    CHECK(peak_of(&a) > 1000);                               /* audible speech, not silence */

    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &b), MACHINE_IDLE);
    CHECK(a.count == b.count && memcmp(a.samples, b.samples, a.count * sizeof a.samples[0]) == 0);

    opt.voice = 'b';
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &voice_b), MACHINE_IDLE);
    CHECK(voice_b.count != a.count || memcmp(a.samples, voice_b.samples, a.count * sizeof a.samples[0]) != 0);

    opt.voice = 'a';
    opt.stream = RENDER_STREAM_RAW;
    CHECK_EQ(render_text(&g_m, rom, len, "Hello world.", &opt, &raw), MACHINE_IDLE);
    CHECK(raw.frames > 0);
    CHECK_EQ(raw.count, raw.frames * SYNTH_FRAME_SAMPLES);

    render_result_free(&a);
    render_result_free(&b);
    render_result_free(&voice_b);
    render_result_free(&raw);
    CHECK(a.samples == NULL && a.count == 0);
}
```
Append to `tests/suites.h`:
```c
SUITE(suite_render)
```

- [ ] **Step 2: Run and confirm it fails**

Run: `cmd //c ".\build.bat test render"`
Expected: FAIL to compile: `cannot open include file 'render/render.h'`.

- [ ] **Step 3: Implement**

`src/render/render.h`:
```c
#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3). */
#include <stddef.h>
#include <stdint.h>
#include "engine/rom_engine.h"
#include "synth/synth.h"

typedef enum {
    RENDER_STREAM_PRODUCED,   /* the golden, timing-independent stream (default) */
    RENDER_STREAM_RAW,        /* what the DSP received: one frame late, CPU-timing dependent */
} render_stream;

typedef struct {
    char voice;               /* 'a' (default) or 'b': speaker table B, selected with @K1 */
    render_stream stream;
    synth_options synth;
} render_options;

typedef struct {
    int16_t *samples;         /* malloc'ed: frames * SYNTH_FRAME_SAMPLES samples at SYNTH_RATE */
    size_t count;
    size_t frames;
    uint64_t clipped;
} render_result;

void render_default_options(render_options *opt);

/* Runs text through the rom engine and synthesizes the chosen stream. Samples are produced only
   when the engine reaches MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out);

void render_result_free(render_result *r);

#endif
```

`src/render/render.c`:
```c
#include <stdlib.h>
#include <string.h>
#include "render/render.h"

void render_default_options(render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->voice = 'a';
    opt->stream = RENDER_STREAM_PRODUCED;
}

void render_result_free(render_result *r)
{
    free(r->samples);
    memset(r, 0, sizeof *r);
}

machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out)
{
    static const char k_voice_b[] = "@K1 ";   /* manual §6 @Kb: 1 = non-male speaker table */
    frame_list produced = { 0 }, raw = { 0 };
    size_t n = strlen(text), prefix = opt->voice == 'b' ? sizeof k_voice_b - 1 : 0;
    char *input = malloc(prefix + n + 1);
    if (!input)
        abort();
    memcpy(input, k_voice_b, prefix);
    memcpy(input + prefix, text, n + 1);
    memset(out, 0, sizeof *out);
    machine_status st = rom_engine_speak_streams(m, rom, len, NULL, input, &produced,
                                                 opt->stream == RENDER_STREAM_RAW ? &raw : NULL);
    const frame_list *chosen = opt->stream == RENDER_STREAM_RAW ? &raw : &produced;
    if (st == MACHINE_IDLE && chosen->count > 0) {
        synth s;
        synth_init(&s, &opt->synth);
        out->samples = malloc(chosen->count * SYNTH_FRAME_SAMPLES * sizeof out->samples[0]);
        if (!out->samples)
            abort();
        for (size_t i = 0; i < chosen->count; i++)
            synth_frame(&s, chosen->frames[i], out->samples + i * SYNTH_FRAME_SAMPLES);
        out->frames = chosen->count;
        out->count = chosen->count * SYNTH_FRAME_SAMPLES;
        out->clipped = s.clipped;
    }
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
```

`src/apps/apollo2.c`:
```c
/* apollo2: text -> speech through the original Apollo 2 firmware (emulated) and a Holmes-style
   parallel-formant synthesizer at 10 kHz (spec R1, R2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/playback.h"
#include "audio/wav.h"
#include "render/render.h"

#define APOLLO2_MAX_TEXT 1500   /* keeps the speech inside ROM_ENGINE_SPEAK_CYCLES (about 200 s) */

static machine g_machine;

static int usage(void)
{
    fprintf(stderr,
            "usage: apollo2 [options] TEXT\n"
            "       apollo2 [options] --text-file FILE\n"
            "  -o FILE            write a WAV file (10 kHz, mono, 16-bit) instead of playing live\n"
            "  --voice a|b        speaker table: a = male (default), b = non-male (@K1)\n"
            "  --engine rom       firmware engine (hybrid and native arrive in milestones 4 and 5)\n"
            "  --rom PATH         ROM image (default: $APOLLO2_ROM or UK_V2.01_4.04_ROM.BIN)\n"
            "  --hyp LIST         synthesizer hypothesis switches, comma-separated: b11,b13\n"
            "  --stream S         frame stream fed to the synthesizer: produced (default) or raw\n"
            "  --text-file FILE   read the text from FILE (first line; device control codes allowed)\n"
            "  --audio-check      report whether live 10 kHz playback is available, then exit\n"
            "TEXT may contain the device's @ commands (docs/apollo2.txt section 6); at most %d characters.\n"
            "exit codes: 0 ok, 1 usage/ROM/file error, 2 emulation fault, 3 cycle limit, 4 audio error\n",
            APOLLO2_MAX_TEXT);
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
    static uint8_t rom[MACHINE_ROM_SIZE];
    const char *rom_path = getenv("APOLLO2_ROM");
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
        } else if (!strcmp(a, "--engine") && val) {
            if (strcmp(val, "rom") != 0) {
                fprintf(stderr, "engine '%s' is not available yet: only 'rom' exists before milestones 4 and 5\n", val);
                return 1;
            }
            i++;
        } else if (!strcmp(a, "--rom") && val) {
            rom_path = val;
            i++;
        } else if (!strcmp(a, "--hyp") && val) {
            if (!parse_hyp(val, &opt.synth))
                return usage();
            i++;
        } else if (!strcmp(a, "--stream") && val && (!strcmp(val, "produced") || !strcmp(val, "raw"))) {
            opt.stream = !strcmp(val, "raw") ? RENDER_STREAM_RAW : RENDER_STREAM_PRODUCED;
            i++;
        } else if (!strcmp(a, "--text-file") && val) {
            text_file = val;
            i++;
        } else if (a[0] == '-' && a[1] != '\0') {
            return usage();
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
    if (strlen(text) > APOLLO2_MAX_TEXT) {
        fprintf(stderr, "text too long: %zu characters (at most %d)\n", strlen(text), APOLLO2_MAX_TEXT);
        return 1;
    }
    if (!rom_path)
        rom_path = "UK_V2.01_4.04_ROM.BIN";
    if (!load_rom(rom_path, rom)) {
        fprintf(stderr, "cannot load a %d-byte ROM from %s\n", MACHINE_ROM_SIZE, rom_path);
        return 1;
    }

    render_result r;
    machine_status st = render_text(&g_machine, rom, sizeof rom, text, &opt, &r);
    if (st == MACHINE_FAULT) {
        machine_dump(&g_machine, stderr);
        return 2;
    }
    if (st == MACHINE_CYCLE_LIMIT) {
        fprintf(stderr, "cycle limit reached before the firmware went idle\n");
        return 3;
    }
    if (r.clipped)
        fprintf(stderr, "warning: %llu samples clipped\n", (unsigned long long)r.clipped);
    int rc = 0;
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

`build.bat`: replace the `if exist src\apps\apollo2_frames.c ( ... )` block with:
```bat
for %%a in (apollo2_frames apollo2) do (
    if exist src\apps\%%a.c (
        cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\%%a.exe src\apps\%%a.c !LIBSRC! %LIBS% || exit /b 1
    )
)
```
and change the comment on line 2 to:
```bat
rem Builds build\apollo2_tests.exe, plus build\apollo2_frames.exe and build\apollo2.exe once their sources exist.
```

`README.md`: append:
```markdown
## Speaking text

    build/apollo2.exe "Hello world."                  # play live (10 kHz)
    build/apollo2.exe -o hello.wav "Hello world."     # write a 10 kHz, mono, 16-bit WAV
    build/apollo2.exe --voice b "Hello world."        # second speaker table (@K1)
    build/apollo2.exe "@W5 @F6 Faster and lower."     # device commands work in the text
    build/apollo2.exe --text-file story.txt -o story.wav
    build/apollo2.exe --audio-check                   # is live 10 kHz playback available?

Text goes through the original firmware's serial input (emulated), so the `@` commands in
`docs/apollo2.txt` section 6 work. `--hyp b11,b13` enables synthesizer hypotheses for
listening tests (see `docs/calibration/report.md`).
```

- [ ] **Step 4: Run and confirm it passes, then exercise the CLI**

Run: `cmd //c ".\build.bat test"`
Expected: all suites `ok`, including `suite_render`.

Then run each of these. Do not run `apollo2.exe` without `-o`: it would play sound on the user's speakers.
- `build/apollo2.exe -o build/hello.wav "Hello world."; echo "exit $?"; stat -c %s build/hello.wav`
  Expected: `exit 0` and `21044`, which is 44 + 105 × 100 × 2.
- `build/apollo2.exe --audio-check; echo "exit $?"`
  Expected: `live playback at 10000 Hz: available` and `exit 0`. Report the actual line if it differs.
- `build/apollo2.exe --engine native "Hi"; echo "exit $?"`
  Expected: the "not available yet" message and `exit 1`.
- `build/apollo2.exe --voice b -o build/hello_b.wav "Hello world."; echo "exit $?"`
  Expected: `exit 0`.
- `build/apollo2.exe --text-file tests/corpus/mute.txt -o build/mute.wav; echo "exit $?"`
  Expected: `exit 0`.
- `build/apollo2.exe; echo "exit $?"`
  Expected: the usage text and `exit 1`.

- [ ] **Step 5: Commit**

```bash
git add src/render/render.h src/render/render.c src/apps/apollo2.c tests/test_render.c tests/suites.h build.bat README.md
git commit -m "Add the apollo2 CLI: text to 10 kHz WAV or live audio via the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Calibration tool and report

**Files:**
- Create:
  - `tools/data/wells1962_rp_male.csv`, `tools/data/README.md`
  - `tools/calibrate.py`
  - `docs/calibration/report.md`, `docs/calibration/*.png`, `docs/calibration/ltas.json` (all generated)
- Modify: `src/synth/calib.h` (regenerated)

**Interfaces:**
- Consumes:
  - `build/apollo2_frames.exe` with the ROM, for the vowel frames.
  - `build/apollo2.exe`, for the report renders.
  - `tests/golden/*.frm`, `tests/corpus/*.txt`.
  - `dolphin_juno_demo.mp3`, decoded with ffmpeg at its native rate.
  - The `calib.h` macro set from Task 6.
- Produces:
  - `python tools/calibrate.py fit [--ltas] [--master-gain G]` regenerates `src/synth/calib.h`.
  - `python tools/calibrate.py report` writes `docs/calibration/report.md`, `vowels.png`, `f0.png`, `ltas.png` and `ltas.json`, plus listening files in `build/listening/*.wav`.

- [ ] **Step 1: Write the reference data**

`tools/data/wells1962_rp_male.csv` contains Wells (1962), Table 1: mean values for 25 male RP speakers. Columns `a1_db..a3_db` are formant amplitudes in dB relative to F1 of /ɔː/.
```
dolphin,wells,keyword,f1,f2,f3,a1_db,a2_db,a3_db
EE,i,heed,285,2373,3088,-2,-33,-37
I,I,hid,356,2098,2696,5,-19,-26
E,E,head,569,1965,2636,-2,-16,-24
AA,ae,had,748,1746,2460,-6,-11,-24
AR,A,hard,677,1083,2540,-4,-10,-35
O,Q,hod,599,891,2605,0,-6,-35
AW,O,haw'd,449,737,2635,0,-10,-42
OO,U,hood,376,950,2440,4,-10,-36
UU,u,who'd,309,939,2320,2,-22,-41
U,V,Hud,722,1236,2537,-2,-7,-30
ER,3,heard,581,1381,2436,-2,-15,-31
```

`tools/data/README.md`:
```markdown
# Calibration reference data

`wells1962_rp_male.csv`: J. C. Wells (1962), *A study of the formants of the pure vowels of
British English*, MA thesis, University of London. Table 1, "Summary of results: mean values"
(25 male RP speakers), transcribed from
https://www.phon.ucl.ac.uk/home/wells/formants/table-1.htm (archived copy:
http://web.archive.org/web/2018/http://www.phon.ucl.ac.uk/home/wells/formants/table-1.htm).

- `dolphin`: the Apollo 2 phoneme (docs/apollo2.txt Appendix 5) whose keyword matches Wells's.
- `wells`: Wells's SAMPA-style symbol.
- `f1..f3`: formant frequencies in Hz (cps).
- `a1_db..a3_db`: formant amplitudes in dB relative to F1 of /O/ (haw'd).
```

- [ ] **Step 2: Write `tools/calibrate.py`**

```python
"""Synthesizer calibration (spec section 5). Fits src/synth/calib.h and writes docs/calibration/.

  python tools/calibrate.py fit [--ltas] [--master-gain G]   regenerate src/synth/calib.h
  python tools/calibrate.py report                          plots + report + listening WAVs

Inputs:
- build/apollo2_frames.exe and the ROM, for the steady vowel frames;
- tests/golden/*.frm, for the F0 codes;
- dolphin_juno_demo.mp3, decoded by ffmpeg at its native rate (nothing is resampled, spec R2);
- build/apollo2.exe, for the report renders;
- tools/data/wells1962_rp_male.csv, the reference formants and amplitudes.
"""
import argparse
import csv
import glob
import json
import os
import re
import subprocess
import sys
from collections import Counter

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALIB_H = os.path.join(ROOT, 'src', 'synth', 'calib.h')
REPORT_DIR = os.path.join(ROOT, 'docs', 'calibration')
LTAS_JSON = os.path.join(REPORT_DIR, 'ltas.json')
LISTEN_DIR = os.path.join(ROOT, 'build', 'listening')
FRAMES_EXE = os.path.join(ROOT, 'build', 'apollo2_frames.exe')
APOLLO2_EXE = os.path.join(ROOT, 'build', 'apollo2.exe')
JUNO = os.path.join(ROOT, 'dolphin_juno_demo.mp3')
JUNO_START_S, JUNO_DUR_S = 5, 20          # the short snippet the user allowed
WELLS = os.path.join(ROOT, 'tools', 'data', 'wells1962_rp_male.csv')
PLAIN_CORPUS = ['hello', 'prose', 'numbers', 'punctuation', 'letters_k_to_z', 'abbreviations']

# Order and comments of the generated header. Every name must exist (Task 6 macro set).
TEMPLATE = [
    ('/* Frequency codes -> Hz. CAL_x_LOG 0: Hz = A + B*code; 1: Hz = A * exp(B*code). */', None),
    ('CAL_FN_LOG', 'int'), ('CAL_FN_A', 'f'), ('CAL_FN_B', 'f'),
    ('CAL_F1_LOG', 'int'), ('CAL_F1_A', 'f'), ('CAL_F1_B', 'f'),
    ('CAL_F2_LOG', 'int'), ('CAL_F2_A', 'f'), ('CAL_F2_B', 'f'),
    ('CAL_F3_LOG', 'int'), ('CAL_F3_A', 'f'), ('CAL_F3_B', 'f'),
    ('CAL_F4_HZ', 'f'),
    ('', None), ('/* Bandwidths (Hz), fixed */', None),
    ('CAL_BN_HZ', 'f'), ('CAL_B1_HZ', 'f'), ('CAL_B2_HZ', 'f'), ('CAL_B3_HZ', 'f'), ('CAL_B4_HZ', 'f'),
    ('', None),
    ('/* Amplitude codes -> peak level dB = CAL_DB_PER_CODE * (code - CAL_REF_CODE) + CAL_G_x_DB.\n'
     '   Code <= 1 switches the channel off. */', None),
    ('CAL_DB_PER_CODE', 'f'), ('CAL_REF_CODE', 'int'),
    ('CAL_G_ALF_DB', 'f'), ('CAL_G_A1_DB', 'f'), ('CAL_G_A2_DB', 'f'), ('CAL_G_A3_DB', 'f'), ('CAL_G_AHF_DB', 'f'),
    ('', None), ('/* F0 code (b10) -> Hz = exp(CAL_F0_LNA + CAL_F0_LNB * code), clamped */', None),
    ('CAL_F0_LNA', 'f'), ('CAL_F0_LNB', 'f'), ('CAL_F0_MIN_HZ', 'f'), ('CAL_F0_MAX_HZ', 'f'),
    ('', None), ('/* Output level: synth units -> int16 full scale (tuned so the corpus peaks near -3 dBFS) */', None),
    ('CAL_MASTER_GAIN', 'f'),
]
NOTES = {'CAL_FN_B': 'hypothesis H-FN: a low resonance, ~256 Hz at the usual code 32',
         'CAL_F4_HZ': 'the fixed fourth formant, driven by AHF'}


def c_float(v):
    s = f'{v:.6g}'
    if 'e' not in s and '.' not in s:
        s += '.0'
    return s + 'f'


def read_calib():
    vals = {}
    for line in open(CALIB_H):
        m = re.match(r'#define (CAL_\w+) (\S+)', line)
        if m:
            vals[m.group(1)] = float(m.group(2).rstrip('f'))
    return vals


def write_calib(vals):
    out = ['#ifndef SYNTH_CALIB_H', '#define SYNTH_CALIB_H',
           '/* Synthesizer calibration (spec section 5): the ONLY place synthesizer constants live.',
           '   GENERATED by tools/calibrate.py. Edit the generator, not this file. */', '']
    for name, kind in TEMPLATE:
        if kind is None:
            out.append(name)
            continue
        text = str(int(vals[name])) if kind == 'int' else c_float(vals[name])
        line = f'#define {name} {text}'
        if name in NOTES:
            line = f'{line:<34}/* {NOTES[name]} */'
        out.append(line)
    out += ['', '#endif', '']
    with open(CALIB_H, 'w', newline='\n') as f:
        f.write('\n'.join(out))


def read_wells():
    with open(WELLS, newline='') as f:
        return list(csv.DictReader(f))


def run_frames(text):
    out = subprocess.run([FRAMES_EXE, text], capture_output=True, text=True, check=True, cwd=ROOT).stdout
    return [bytes.fromhex(line) for line in out.split()]


def steady_frame(frames):
    """The vowel's target frame: the most common first 10 bytes among frames with A1 > 10."""
    return list(Counter(f[:10] for f in frames if f[2] > 10).most_common(1)[0][0])


def vowel_frames(prefix):
    return {w['dolphin']: steady_frame(run_frames(f'{prefix}_{w["dolphin"]}.')) for w in read_wells()}


def fit_freq(codes, hz):
    """Linear vs log least squares; the smaller RMS relative error wins (spec section 5, step 1)."""
    codes = np.asarray(codes, float)
    hz = np.asarray(hz, float)
    b, a = np.polyfit(codes, hz, 1)
    lin = float(np.sqrt(np.mean(((a + b * codes - hz) / hz) ** 2)))
    bl, al = np.polyfit(codes, np.log(hz), 1)
    log = float(np.sqrt(np.mean(((np.exp(al + bl * codes) - hz) / hz) ** 2)))
    if log < lin:
        return {'log': 1, 'a': float(np.exp(al)), 'b': float(bl), 'rms_pct': 100 * log, 'other_pct': 100 * lin}
    return {'log': 0, 'a': float(a), 'b': float(b), 'rms_pct': 100 * lin, 'other_pct': 100 * log}


def fit_amplitudes(frames, wells):
    """level_k = s * code_k + g_k + c_vowel  (g_1 = 0). Returns s, g2, g3 and the RMS error in dB."""
    rows, rhs = [], []
    for i, w in enumerate(wells):
        c = frames[w['dolphin']]
        for k, (ci, col) in enumerate([(2, 'a1_db'), (4, 'a2_db'), (6, 'a3_db')]):
            r = np.zeros(3 + len(wells))
            r[0] = c[ci]
            if k > 0:
                r[k] = 1.0
            r[3 + i] = 1.0
            rows.append(r)
            rhs.append(float(w[col]))
    a = np.array(rows)
    sol = np.linalg.lstsq(a, np.array(rhs), rcond=None)[0]
    rms = float(np.sqrt(np.mean((a @ sol - np.array(rhs)) ** 2)))
    return {'db_per_code': float(sol[0]), 'g2': float(sol[1]), 'g3': float(sol[2]), 'rms_db': rms}


def golden_f0_codes():
    codes = []
    for name in PLAIN_CORPUS:
        for line in open(os.path.join(ROOT, 'tests', 'golden', name + '.frm')):
            f = bytes.fromhex(line.strip())
            if f[9] >= 40 and f[2] > 10:        # voiced speech frames
                codes.append(f[10])
    return np.array(codes, float)


def juno_audio():
    rate = int(subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                               'stream=sample_rate', '-of', 'csv=p=0', JUNO],
                              capture_output=True, text=True, check=True).stdout.strip())
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(JUNO_START_S), '-t', str(JUNO_DUR_S), '-i', JUNO,
                          '-ac', '1', '-f', 's16le', '-'], capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.int16).astype(np.float32) / 32768.0, rate


def juno_f0(y, sr):
    import librosa
    f0, voiced, _ = librosa.pyin(y, fmin=60, fmax=300, sr=sr, frame_length=4096, hop_length=sr // 100)
    return f0[voiced & ~np.isnan(f0)]


def fit_f0(codes, juno_hz):
    qs = [5, 50, 95]
    c = np.percentile(codes, qs)
    h = np.percentile(juno_hz, qs)
    b, a = np.polyfit(c, np.log(h), 1)
    return {'lna': float(a), 'lnb': float(b), 'code_q': c.tolist(), 'juno_q': h.tolist()}


def map_hz(vals, name, code):
    a, b = vals[f'CAL_{name}_A'], vals[f'CAL_{name}_B']
    return a * np.exp(b * code) if vals[f'CAL_{name}_LOG'] else a + b * code


def cmd_fit(args):
    vals = read_calib()
    wells = read_wells()
    frames = vowel_frames('')
    for name, ci, col in [('F1', 3, 'f1'), ('F2', 5, 'f2'), ('F3', 7, 'f3')]:
        fit = fit_freq([frames[w['dolphin']][ci] for w in wells], [float(w[col]) for w in wells])
        vals[f'CAL_{name}_LOG'], vals[f'CAL_{name}_A'], vals[f'CAL_{name}_B'] = fit['log'], fit['a'], fit['b']
        print(f'{name}: {"log" if fit["log"] else "linear"} a={fit["a"]:.4g} b={fit["b"]:.4g} '
              f'rms {fit["rms_pct"]:.1f}% (other form {fit["other_pct"]:.1f}%)')
    amp = fit_amplitudes(frames, wells)
    vals['CAL_DB_PER_CODE'], vals['CAL_G_A2_DB'], vals['CAL_G_A3_DB'] = amp['db_per_code'], amp['g2'], amp['g3']
    print(f'amplitude: {amp["db_per_code"]:.3f} dB/code, g2 {amp["g2"]:.1f} dB, g3 {amp["g3"]:.1f} dB, '
          f'rms {amp["rms_db"]:.2f} dB')
    y, sr = juno_audio()
    f0 = fit_f0(golden_f0_codes(), juno_f0(y, sr))
    vals['CAL_F0_LNA'], vals['CAL_F0_LNB'] = f0['lna'], f0['lnb']
    print(f'F0: codes {f0["code_q"]} -> Juno {[round(h, 1) for h in f0["juno_q"]]} Hz; '
          f'ln Hz = {f0["lna"]:.4f} + {f0["lnb"]:.4f} * code')
    if args.ltas:
        ltas = json.load(open(LTAS_JSON))
        if ltas.get('applied'):
            sys.exit('ltas.json was already applied; run "report" again first')
        vals['CAL_G_ALF_DB'] += ltas['alf_delta_db']
        vals['CAL_G_AHF_DB'] += ltas['ahf_delta_db']
        ltas['applied'] = True
        json.dump(ltas, open(LTAS_JSON, 'w'), indent=1)
        print(f'LTAS: ALF {ltas["alf_delta_db"]:+.1f} dB, AHF {ltas["ahf_delta_db"]:+.1f} dB applied')
    if args.master_gain is not None:
        vals['CAL_MASTER_GAIN'] = args.master_gain
    write_calib(vals)
    print(f'wrote {os.path.relpath(CALIB_H, ROOT)}; rebuild with cmd //c ".\\build.bat test"')


def render(text_file, wav, extra=()):
    subprocess.run([APOLLO2_EXE, *extra, '--text-file', text_file, '-o', wav], check=True, cwd=ROOT)
    from scipy.io import wavfile
    rate, x = wavfile.read(wav)
    return x.astype(np.float32) / 32768.0, rate


def ltas_db(x, sr, nperseg, grid):
    from scipy.signal import welch
    f, p = welch(x, sr, nperseg=nperseg)
    db = 10 * np.log10(np.interp(grid, f, p) + 1e-20)
    return db - db[(grid >= 800) & (grid <= 1250)].mean()


def cmd_report(_args):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    os.makedirs(REPORT_DIR, exist_ok=True)
    os.makedirs(LISTEN_DIR, exist_ok=True)
    vals = read_calib()
    wells = read_wells()
    fa, fb = vowel_frames(''), vowel_frames('@K1 ')

    # Vowel formants: ROM codes through the current maps vs Wells; voice B as the cross-check
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))
    rows = []
    for ax, (name, ci, col) in zip(axes, [('F1', 3, 'f1'), ('F2', 5, 'f2'), ('F3', 7, 'f3')]):
        codes = np.array([fa[w['dolphin']][ci] for w in wells], float)
        ref = np.array([float(w[col]) for w in wells])
        grid = np.arange(0, 64)
        ax.plot(grid, map_hz(vals, name, grid), 'k-', label='calib.h map')
        ax.plot(codes, ref, 'bo', label='Wells 1962 (voice A codes)')
        ax.plot([fb[w['dolphin']][ci] for w in wells], ref, 'r^', label='same vowels, voice B codes')
        for w, c, r in zip(wells, codes, ref):
            ax.annotate(w['dolphin'], (c, r), fontsize=7)
        ax.set_title(name)
        ax.set_xlabel('code')
        ax.set_ylabel('Hz')
        ax.legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(os.path.join(REPORT_DIR, 'vowels.png'), dpi=90)
    plt.close(fig)
    for w in wells:
        a, b = fa[w['dolphin']], fb[w['dolphin']]
        pred = [map_hz(vals, n, a[i]) for n, i in [('F1', 3), ('F2', 5), ('F3', 7)]]
        ref = [float(w[k]) for k in ('f1', 'f2', 'f3')]
        err = [100 * (p - r) / r for p, r in zip(pred, ref)]
        rows.append(f'| {w["dolphin"]} | {w["keyword"]} | {a[3]}/{a[5]}/{a[7]} | {b[3]}/{b[5]}/{b[7]} | '
                    f'{ref[0]:.0f}/{ref[1]:.0f}/{ref[2]:.0f} | {pred[0]:.0f}/{pred[1]:.0f}/{pred[2]:.0f} | '
                    f'{err[0]:+.0f}/{err[1]:+.0f}/{err[2]:+.0f} |')

    # F0: corpus codes through the map vs the Juno snippet
    codes = golden_f0_codes()
    y, sr = juno_audio()
    jf0 = juno_f0(y, sr)
    ours = np.clip(np.exp(vals['CAL_F0_LNA'] + vals['CAL_F0_LNB'] * codes), vals['CAL_F0_MIN_HZ'], vals['CAL_F0_MAX_HZ'])
    fig, ax = plt.subplots(figsize=(7, 4))
    bins = np.arange(50, 320, 10)
    ax.hist(jf0, bins, alpha=0.5, density=True, label='Juno snippet (pyin)')
    ax.hist(ours, bins, alpha=0.5, density=True, label='plain corpus b10 via calib.h')
    ax.set_xlabel('F0 (Hz)')
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(REPORT_DIR, 'f0.png'), dpi=90)
    plt.close(fig)

    # Listening files, and the long-term average spectrum vs Juno
    synth = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'tests', 'corpus', '*.txt'))):
        name = os.path.splitext(os.path.basename(path))[0]
        x, rate = render(path, os.path.join(LISTEN_DIR, name + '.wav'))
        if name in PLAIN_CORPUS:
            synth.append(x)
    prose = os.path.join(ROOT, 'tests', 'corpus', 'prose.txt')
    render(prose, os.path.join(LISTEN_DIR, 'prose_voice_b.wav'), ['--voice', 'b'])
    render(prose, os.path.join(LISTEN_DIR, 'prose_hyp_b11.wav'), ['--hyp', 'b11'])
    render(prose, os.path.join(LISTEN_DIR, 'prose_hyp_b13.wav'), ['--hyp', 'b13'])
    grid = np.arange(100, 4501, 25, dtype=float)
    s_db = ltas_db(np.concatenate(synth), rate, 1024, grid)
    j_db = ltas_db(y, sr, 4096, grid)
    diff = s_db - j_db
    alf = float(np.clip(-diff[(grid >= 150) & (grid <= 350)].mean(), -12, 12))
    ahf = float(np.clip(-diff[(grid >= 3200) & (grid <= 3800)].mean(), -12, 12))
    json.dump({'alf_delta_db': alf, 'ahf_delta_db': ahf, 'applied': False,
               'grid_hz': grid.tolist(), 'synth_db': s_db.tolist(), 'juno_db': j_db.tolist()},
              open(LTAS_JSON, 'w'), indent=1)
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot(grid, s_db, label='synth, plain corpus')
    ax.plot(grid, j_db, label='Juno snippet')
    ax.set_xlabel('Hz')
    ax.set_ylabel('dB re 0.8-1.25 kHz')
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(REPORT_DIR, 'ltas.png'), dpi=90)
    plt.close(fig)

    q = lambda a: ' / '.join(f'{v:.0f}' for v in np.percentile(a, [5, 50, 95]))
    md = [
        '# Synthesizer calibration report', '',
        'Generated by `tools/calibrate.py report` from the current `src/synth/calib.h`. Reference:',
        'Wells (1962) RP male means (`tools/data/`). Juno: `dolphin_juno_demo.mp3` '
        f'{JUNO_START_S}-{JUNO_START_S + JUNO_DUR_S} s.', '',
        '## Vowel formants (F1/F2/F3)', '',
        '| Vowel | Keyword | Codes A | Codes B | Wells Hz | calib.h Hz (A) | Error % |',
        '|---|---|---|---|---|---|---|', *rows, '',
        '![vowels](vowels.png)', '',
        f'Maps: F1 {"log" if vals["CAL_F1_LOG"] else "linear"}, F2 {"log" if vals["CAL_F2_LOG"] else "linear"}, '
        f'F3 {"log" if vals["CAL_F3_LOG"] else "linear"}. FN is hypothesis H-FN '
        f'({vals["CAL_FN_A"]:.0f} + {vals["CAL_FN_B"]:.1f}·code Hz).', '',
        '## Amplitudes', '',
        f'{vals["CAL_DB_PER_CODE"]:.3f} dB per code; channel offsets (dB): ALF {vals["CAL_G_ALF_DB"]:+.1f}, '
        f'A1 {vals["CAL_G_A1_DB"]:+.1f}, A2 {vals["CAL_G_A2_DB"]:+.1f}, A3 {vals["CAL_G_A3_DB"]:+.1f}, '
        f'AHF {vals["CAL_G_AHF_DB"]:+.1f}.', '',
        '## F0', '',
        f'5th/50th/95th percentile: Juno {q(jf0)} Hz; plain corpus through calib.h {q(ours)} Hz.', '',
        '![f0](f0.png)', '',
        '## Long-term average spectrum', '',
        f'Suggested channel corrections (apply with `calibrate.py fit --ltas`): ALF {alf:+.1f} dB, AHF {ahf:+.1f} dB.', '',
        '![ltas](ltas.png)', '',
        '## Listening files (spec R4)', '',
        'Rendered to `build/listening/` (not committed): every corpus text, plus `prose_voice_b`, '
        '`prose_hyp_b11` and `prose_hyp_b13` for the hypothesis switches.', '',
    ]
    with open(os.path.join(REPORT_DIR, 'report.md'), 'w', newline='\n') as f:
        f.write('\n'.join(md))
    print(f'wrote {os.path.relpath(os.path.join(REPORT_DIR, "report.md"), ROOT)} and {len(synth)} plain renders; '
          f'LTAS suggests ALF {alf:+.1f} dB, AHF {ahf:+.1f} dB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    fit = sub.add_parser('fit')
    fit.add_argument('--ltas', action='store_true', help='apply the ALF/AHF corrections from the last report')
    fit.add_argument('--master-gain', type=float, default=None)
    sub.add_parser('report')
    args = p.parse_args()
    {'fit': cmd_fit, 'report': cmd_report}[args.cmd](args)


if __name__ == '__main__':
    main()
```

- [ ] **Step 3: Fit, rebuild and check that the seed is reproduced**

Run: `python tools/calibrate.py fit`
Expected output, with numbers ±1 in the last digit:
- `F1: linear a=139.4 b=21.55`
- `F2: linear a=593.3 b=44.59`
- `F3: log a=1877 b=0.01015`
- `amplitude: 0.949 dB/code, g2 -12.8 dB, g3 -26.7 dB`
- an F0 line whose Juno quantiles are near `63.6 / 111.3 / 226.5`

Run: `git diff src/synth/calib.h`
Expected: only formatting changes, plus the F0 constants if pyin differs slightly. The F1–F3 maps and amplitude constants must stay the same to 4 significant digits. If they don't, stop and report it.

Run: `cmd //c ".\build.bat test"`. All suites must be `ok`.

- [ ] **Step 4: Report, apply the LTAS correction once, re-tune the level, report again**

1. Run `python tools/calibrate.py report`. Expected: the report is written, 25 corpus WAVs plus 3 extra WAVs are rendered in `build/listening/`, and the script prints the suggested ALF/AHF dB.
2. Run `python tools/calibrate.py fit --ltas`, then `cmd //c ".\build.bat test synth"`.
   If `suite_synth`'s level check fails or reports clipping:
   - read the `corpus peak P` line;
   - run `python tools/calibrate.py fit --master-gain G` with `G = current CAL_MASTER_GAIN * 23197 / P`;
   - note that this second `fit` must not use `--ltas`, which is already applied;
   - rebuild.
3. Run `cmd //c ".\build.bat test"`. All suites must be `ok`.
4. Run `python tools/calibrate.py report` again, so the committed report reflects the final `calib.h`.
5. Open `docs/calibration/report.md` and confirm:
   - it has the vowel table with 11 rows;
   - the three images exist;
   - the F0 quantile line shows the plain corpus within ±15 % of the Juno median.

- [ ] **Step 5: Commit**

```bash
git add tools/calibrate.py tools/data/wells1962_rp_male.csv tools/data/README.md src/synth/calib.h docs/calibration
git commit -m "Add the calibration tool and report (Wells 1962 vowels, Juno F0 and LTAS)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Spec revision 4

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`

**Interfaces:** documentation only. It records the facts verified in M2 and the decisions this plan made. Use the Edit tool for each change below, and mark each new or changed passage *(rev 4)*.

- [ ] **Step 1: Make these edits**

1. **Status line** (line 4). Replace `Revision 3 makes the finished project ROM-free (R5).` with:
   `Revision 4 records what milestone 2 verified (command-driven frame bytes, the raw DSP stream, the calibration). Revision 3 makes the finished project ROM-free (R5).`

2. **§1.3.** Add this bullet after the "Timer ISR `$B2D9`" bullet:
   ```
   - **DSP reset** *(rev 4)*: `$B38B` pulses P2.0 high (`CLB0`/`SEB0`/`CLB0 $F0`). It is called
     when speech starts (`$B323`, which also clears `$5A`/`$5C`) and when it stops (`$B398`, which
     also disables timer 3). In the emulator, the raw DSP stream is the produced stream one frame
     late. raw[0] is the buffer left over from before the first swap, and the last produced
     frame is never played. Every raw frame is aligned on its `$FFFF` word. No replays were seen
     for the corpus texts.
   ```

3. **§1.4.**
   - Replace the word 5 row with:
     `| 5 | b10 F0 (pitch: follows @F; calibrated in §5) | b11 mark-space ratio (@M, clamped by F0) *(rev 4: verified)* |`
   - Replace the word 8 row with:
     `| 8 | b16 voicing source (@$: $82 default, $58 spike, $9B cut-down, $BA reduced-HF) | b17 voice filter (@$: $4A male, $54 female) *(rev 4: verified)* |`
   - Add after the table:
   ```
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
   ```

4. **§2, R1.** Replace the R1 bullet's first line with:
   `- **R1 — CLI:** `apollo2 "text" [-o out.wav] [--voice a|b] [--engine rom|hybrid|native] [--rom PATH] [--hyp b11,b13] [--stream produced|raw] [--text-file FILE] [--audio-check]`. *(rev 4: `--voice b` sends `@K1`; `--stream raw` is for comparison listening only.)*`

5. **§3, the `cpu740/` row.** Replace `A T or D flag restored by PLP/RTI also faults.` with:
   `A T flag restored by PLP/RTI faults; a D flag faults when ADC/SBC executes with it set (rev 4: matches the code).`

6. **§4, the "DSP port" bullet.** Replace it with:
   `- **DSP port** *(rev 4)*: writes to `$8000` are counted and grouped into raw 20-byte frames, re-aligned at every P2.0 DSP-reset rising edge. The raw stream depends on CPU timing and is used only for diagnosis and for `--stream raw` listening. The produced stream stays the golden.`

7. **§5.** Add this subsection before "## 6.":
   ```
   **Calibration results** *(rev 4; see docs/calibration/report.md)*:
   - F1 and F2 are linear and F3 is logarithmic, fitted to Wells (1962) RP male means (RMS 5–6 %).
   - Amplitudes are 0.95 dB per code, confirming the hypothesis, with channel offsets fitted to
     Wells's formant levels.
   - F0 is log-linear, fitted to the Juno snippet's 5/50/95th percentiles.
   - FN has no reference. Hypothesis H-FN treats it as a low resonance near 256 Hz.
   - The source is a unit-power impulse train. Hypothesis b11 switches to a Rosenberg pulse whose
     open quotient is b11/64.
   - Hypothesis switches implemented: b11 (pulse width) and b13 bit 7 (resonator reset).
   - b14–b15 and b16–b17 are decoded (§1.4) but get no switch until a listening-test hypothesis
     exists. Candidates: the @$ source and filter variants.
   ```

8. **§7.1, Golden corpus.** Replace `M1 covers plain text; control codes and spelling mode are added once `docs/apollo2.txt` has been worked through *(rev 2)*.` with:
   `M1 covers plain text. M2 adds every frame-affecting control code, spelling mode, phonetic input, mute (Ctrl-X), a boot golden, and a check that @c? queries leave the speech unchanged *(rev 4)*.`

9. **§9.** Add this bullet:
   `- Serial replies (@c? queries, @L, @E, indexing) *(rev 4)*. The firmware sends them only under the host's RTS/CTS handshake (CNTR pin), which is not modelled.`

10. **§10.**
    - Replace the P4/P3 bullet with:
      `- The P4 DIP switches beyond bit 0. *(rev 4: the P3 baud jumpers are pins 20/21 in docs/apollo2.txt Appendix 1: 9600 default, 1200, 300, 19200.)*`
    - Replace the b11/b13/b14–b17 bullet with:
      `- The meanings of b13 and b14–b15, and how the DSP realises the @$ source/filter variants (listening tests). *(rev 4: b11, b16 and b17 are identified, §1.4.)*`

- [ ] **Step 2: Check and commit**

Run: `grep -c "rev 4" docs/superpowers/specs/2026-09-30-software-apollo2-design.md`
Expected: 10 or more.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md
git commit -m "Spec rev 4: record what milestone 2 verified and decided

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Milestone 2 exit criteria

- `cmd //c ".\build.bat test"` passes every suite with the ROM present. Without it, the ROM suites print SKIP while the synth, params, resonator, source and audio suites still run.
- The six M1 goldens are unchanged. The corpus covers the control codes, plus a 41-frame boot golden.
- `build/apollo2.exe -o out.wav "text"` writes a 10 kHz, mono, 16-bit WAV. `build/apollo2.exe "text"` plays it live.
- `docs/calibration/report.md` shows the vowel fit, F0 and LTAS comparisons. `build/listening/` holds the files for the user's listening test (R4). That test is the milestone's acceptance and happens after execution.

## Later milestones (separate plans)

- **M3 — Ghidra 740 module and project:**
  - SLEIGH extension validated against `cpu740`.
  - Labels, and RAM jump tables resolved to full coverage.
  - **First: a CPU reference-vector suite.** It uses Tom Harte's 6502 SingleStepTests for the documented opcodes (skipping the decimal-mode cases, comparing registers and memory only), plus MAME-derived vectors for the 740 extensions. The goldens remain emulator-self-referential until it lands.
  - The M1 test gaps: addressing modes ABX/ABY/ZPX/ZPY/IZX, BIT/CPX/CPY/SBC-V, pushed P bytes, WIT with I set, BBS/BBC not-taken and backward, and an RTI negative control.
  - A shared address-mapping helper for `bus_read`/`machine_peek`.
  - A ROM MD5 identity check.
- **M4 — Progressive replacement:**
  - per-routine thunks keyed by ROM address and `rom_call()`;
  - differential entry/exit-state tests, with the goldens held byte-exact;
  - ROM tables converted into committed, readable C under `src/firmware/data/`, with a byte-for-byte check.
  - Fact to honour: each utterance's first produced frame is the leftover buffer from before speech.
- **M5 — Native:** all routines and tables in C, and a `native` engine without `cpu740`/`machine`. Done when `build.bat test` passes with no ROM file present.
