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
