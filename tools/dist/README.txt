Dolphin Apollo II Reborn
========================

A software re-creation of the Dolphin Apollo 2 speech synthesizer: its original firmware, ported to C, drives the
Orpheus synthesizer back end at 10 kHz (--synth holmes: the Holmes-style one). No ROM is needed.
Windows 10 or later, 64-bit.

Open a command prompt in this folder:

    apollo2.exe "Hello world."                  play live
    apollo2.exe -o hello.wav "Hello world."     write a WAV (10 kHz, mono, 16-bit)
    apollo2.exe --voice b "Hello world."        second speaker table (@K1)
    apollo2.exe --prosody 3 "Hello world."      intonation 0-7 (@Rn); default 4, the device's own is 3
    apollo2.exe --synth holmes "Hello world."   the Holmes-style synth (default: orpheus)
    apollo2.exe "@W5 @F6 Faster and lower."     device commands (manual.txt section 6)
    apollo2.exe --text-file story.txt -o story.wav
    apollo2.exe -o minus.wav -- "-5 degrees."   -- ends the options
    apollo2.exe --audio-check                   is live 10 kHz playback available?

A text holds at most 1500 characters. apollo2.exe with no arguments lists every option.

Exit codes: 0 ok, 1 usage or file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodeled access logged.
