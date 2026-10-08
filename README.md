# Software Apollo 2

A software re-creation of the Dolphin Apollo 2 speech synthesizer. Design:
`docs/superpowers/specs/2026-09-30-software-apollo2-design.md`.

## Build and test

Requires MSVC 2022 Build Tools. From Git Bash in the repo root:

    cmd //c ".\build.bat test"            # build + run every test suite
    cmd //c ".\build.bat test cpu"        # only suites whose name contains "cpu"

The original ROM is not part of this repository. Put `UK_V2.01_4.04_ROM.BIN` in the
repo root, or point `APOLLO2_ROM` at it. Tests that need it print SKIP without it.

## Frames from the original firmware

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

Text goes through the original firmware's serial input (emulated), so the `@` commands in
`docs/apollo2.txt` section 6 work. `--hyp b11,b13` enables synthesizer hypotheses for
listening tests (see `docs/calibration/report.md`).

The voice is shaped by a low-end peak on the voiced source and a 3.8 kHz low-pass that models the
device's DAC filter; both are always on.

## Hybrid engine (milestone 4)

`apollo2 --engine hybrid "text" -o out.wav` runs the original firmware with the ported routines replaced by C
(src/firmware). Today these are the letter-to-sound routines (M4.0), text normalisation, tn_convert_clause
and tn_append_phonetic (M4.1), the command parser: the @ command matcher, the clause scan, the dispatcher
and every deferred command handler (M4.2), the segment generator: phoneme expansion, the phrase generator,
the segment appenders and the declination (M4.3a), the stress and phrase passes with pros_make_segments
(M4.3b), so all of prosody before playback, the playback set-up: segment reading, pitch scaling, the
boundary and dominance pass, track planning and the voice set-up (M4.4a), the frame loop: par_play, the track
steppers, the command events and boot_loop_play (M4.4b), and the frame builder: the rest frame, the snapshot, the
mark-space clamp, the formant raise and the b13-b17 bytes (M4.5); the rest (the boot, serial, bank and interrupt
code) still runs on the emulated CPU.
The hybrid engine must reproduce every golden frame stream byte for byte (`build.bat test golden_hybrid`),
`build.bat test diff_lts` compares each ported routine with the ROM at real call sites, and
`build.bat test diff_play` compares the two that wait on the timer interrupt (par_play, boot_loop_play) with the
ROM at every frame swap, with interrupts running.

The converted tables in src/firmware/data come from `python tools/extract_tables.py` (run once, then
reviewed); `build.bat test firmware_data` checks them against the ROM. RAM names in src/firmware/fw_ram.h come
from `python tools/gen_fw_ram.py` (rerun after the Ghidra annotations change).

A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the CLIs unless `--rom-any` is given.

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
