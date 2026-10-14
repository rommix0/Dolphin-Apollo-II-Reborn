# Software Apollo 2

A software re-creation of the Dolphin Apollo 2 speech synthesizer. Design:
`docs/superpowers/specs/2026-09-30-software-apollo2-design.md`.

## Build and test

Requires MSVC 2022 Build Tools. From Git Bash in the repo root:

    cmd //c ".\build.bat test"            # build + run every test suite
    cmd //c ".\build.bat test cpu"        # only suites whose name contains "cpu"

`apollo2` needs no ROM: it is built from `src/` alone and runs the firmware as native C. The original ROM
is not part of this repository; only the test oracle uses it (see "Tests and the oracle" below). Put
`UK_V2.01_4.04_ROM.BIN` in the repo root, or point `APOLLO2_ROM` at it. Tests that need it print SKIP without it.

    cmd //c ".\build.bat test-norom"      # every suite as if no ROM existed, then the program itself

## Frames from the original firmware

`apollo2_frames` is a test-side tool (tests/oracle/apps): it runs the ROM on the emulator and needs the ROM.

    build/apollo2_frames.exe "Hello world."      # one produced DSP frame per line (hex)
    build/apollo2_frames.exe --boot "Hi"          # include the startup announcement

Golden frame streams for the regression corpus live in `tests/golden/`. Regenerate
them only after reviewing an intentional emulator change:

    APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"

## Speaking text

    build/apollo2.exe "Hello world."                  # play live (10 kHz)
    build/apollo2.exe -o hello.wav "Hello world."     # write a 10 kHz, mono, 16-bit WAV
    build/apollo2.exe --voice b "Hello world."        # second speaker table (@K1)
    build/apollo2.exe --prosody 3 "Hello world."      # intonation 0-7 (@Rn); default 4, the device's own default is 3
    build/apollo2.exe "@W5 @F6 Faster and lower."     # device commands work in the text
    build/apollo2.exe --text-file story.txt -o story.wav
    build/apollo2.exe --audio-check                   # is live 10 kHz playback available?

Exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged.

Text goes through the firmware's serial input (the C firmware on the native engine), so the `@` commands in
`docs/apollo2.txt` section 6 work. `--hyp b11,b13` enables synthesizer hypotheses for
listening tests (see `docs/calibration/report.md`).

The voice is shaped by a low-end peak on the voiced source and a 3.8 kHz low-pass that models the
device's DAC filter; both are always on.

## Hybrid engine (milestone 4)

The hybrid engine (tests/oracle/engine, a test oracle) runs the original firmware with the ported routines replaced by C
(src/firmware). Today these are the letter-to-sound routines (M4.0), text normalisation, tn_convert_clause
and tn_append_phonetic (M4.1), the command parser: the @ command matcher, the clause scan, the dispatcher
and every deferred command handler (M4.2), the segment generator: phoneme expansion, the phrase generator,
the segment appenders and the declination (M4.3a), the stress and phrase passes with pros_make_segments
(M4.3b), so all of prosody before playback, the playback set-up: segment reading, pitch scaling, the
boundary and dominance pass, track planning and the voice set-up (M4.4a), the frame loop: par_play, the track
steppers, the command events and boot_loop_play (M4.4b), and the frame builder: the rest frame, the snapshot, the
mark-space clamp, the formant raise and the b13-b17 bytes (M4.5), and the serial code: the receive and host-line
interrupts, the immediate commands and the reply output (M4.6a), and speech timing: the timer interrupt that feeds
the DSP, the start and stop of DSP output, the frame waits, the volume and the parallel handshake (M4.6b), and
the banks and the command dispatcher: the RAM-resident bank code, @= and cmd_run_next (M4.6c1), and boot, the
main loop and isr_idle (M4.6c2). So every firmware routine now runs as C; the emulated CPU supplies the hardware
(timers, UART, DSP port, interrupts) and runs only three hardware wait loops, plus the ROM's own dispatcher for a
handler address that a NUL byte after a command produces.
The hybrid engine must reproduce every golden frame stream byte for byte (`build.bat test golden_hybrid`),
`build.bat test diff_lts` compares each ported routine with the ROM at real call sites, and
`build.bat test diff_play` compares the routines that wait on the timer interrupt (par_play, boot_loop_play and
the frame waits) with the ROM at every frame event, with interrupts running, and `build.bat test diff_serial`
compares the paths no corpus text reaches (reply transmission, parallel mode, the ring wraps, the timer
interrupt's rare paths, the banks' cold entry and DIP bit 1) through probes injected into both copies, and
`build.bat test diff_main` runs the ROM engine and the all-C hybrid side by side, comparing them at every input
idle and frame event.

The converted tables in src/firmware/data come from `python tools/extract_tables.py` (run once, then
reviewed); `build.bat test firmware_data` checks them against the ROM. RAM names in src/firmware/fw_ram.h come
from `python tools/gen_fw_ram.py` (rerun after the Ghidra annotations change).

A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the oracle tools (`apollo2_frames`,
`apollo2_coverage`, `cpu740_vectors`) unless `--rom-any` is given.

## Native engine (milestone 5)

The native engine (src/engine/native_engine.c) runs the same C firmware with no ROM and no emulated CPU: a
CPU-less board (src/board) supplies the M50734's timers, UART and ports, the interrupts and the DSP port, and
charges exactly the cycles the hybrid engine's emulated CPU spends waiting, so the two agree state for state.
`build.bat test golden_native` reproduces every golden frame stream on it without the ROM, and
`build.bat test diff_native` runs the hybrid and the native engine side by side over every diff_main run,
comparing RAM, the peripheral state and the cycle count at every input idle and frame event. Since milestone 5b
the CLI runs the native engine only and needs no ROM.

## Tests and the oracle

The emulator is the test oracle: `tests/oracle/` holds the 740 CPU core, the machine model, the `rom` and
`hybrid` engines and the three tools that need the ROM (`apollo2_frames`, `apollo2_coverage`,
`cpu740_vectors`, built into `build/` under those names). `apollo2.exe` is compiled and linked from `src/`
alone, so it cannot reach them. In `tests/suites.h` each suite is marked `SUITE` (needs no ROM) or
`SUITE_ROM` (has parts that need the ROM, set by `APOLLO2_ROM` or found in the repo root), and the runner
fails a suite whose mark is wrong. `build.bat test-norom` runs every suite with `APOLLO2_NO_ROM=1`, so the
ROM is never looked for: every `SUITE` must run in full, the `SUITE_ROM` suites skip their ROM parts, and
then `tests/check_cli.bat` runs `apollo2.exe` itself.

## Ghidra project (milestone 3)

`ghidra740/` holds a Ghidra 12.1.3 processor module for the MELPS 740 and the scripts that rebuild
a fully labelled Ghidra project of the ROM from the committed text annotations in
`ghidra740/annotations/`. Those files are the source of truth. See `ghidra740/README.md` for the
details and the GUI procedure. The entry points need Ghidra and the ROM, and are not part of
`build.bat test`:

    cmd //c "ghidra740\install.bat"     # compile the language, install module and scripts (restart Ghidra after SLEIGH changes)
    cmd //c "ghidra740\validate.bat"    # check the module against the cpu740 emulator core
    cmd //c "ghidra740\rebuild.bat"     # rebuild the headless project, write build/ghidra740/check.txt
    cmd //c "ghidra740\dump.bat"        # per-function disassembly and decompiled C in build/ghidra740/dump/
