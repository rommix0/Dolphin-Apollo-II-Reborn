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
    build/apollo2.exe "@W5 @F6 Faster and lower."     # device commands work in the text
    build/apollo2.exe --text-file story.txt -o story.wav
    build/apollo2.exe --audio-check                   # is live 10 kHz playback available?

Text goes through the original firmware's serial input (emulated), so the `@` commands in
`docs/apollo2.txt` section 6 work. `--hyp b11,b13` enables synthesizer hypotheses for
listening tests (see `docs/calibration/report.md`).
