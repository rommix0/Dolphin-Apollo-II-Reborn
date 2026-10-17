# Orpheus synthesizer back end (a3s.dll): static analysis

Status: research note, 2026-10-16, branch `dsp_fix`. The back end is ported: `src/orpheus/`, `apollo2 --synth orpheus`
(plan `xtras/superpowers/plans/2026-10-16-orpheus-port.md`; see "The C port" below).

Dolphin's Orpheus (Build 18, product 1.06, `a3s.dll` dated 2000-10) is the software successor of the Apollo 2. Its
synthesizer back end was recovered from `a3s.dll` with capstone (no Ghidra), and its init and render routines run
unchanged under Unicorn, a CPU emulator, with no Windows and no native load of the DLL. The tools are in
`xtras/orpheus/` (`ORPHEUS_A3S` overrides the DLL path), and A/B listening pairs are in `xtras/orpheus/listen/`
(git-ignored WAVs).

## Where it is

| Address | Role |
|---|---|
| `2320bb10` | synth constructor (object 0xb168 bytes, global `[2322bd18]`); builds the config via `2320b150` |
| `2320b150` | config block (0x7f8 bytes) built from immediates: no file or INI is read |
| `2320c620` / `2320c510` | table set-up / reset (the reset runs `2320e350`, the fixed filter design) |
| `2320ce70` | load one frame: 16 shorts; p0-p3 Hz -> centi-Bark (Traunmüller); volume -> output gain |
| `2320e170` | render one frame -> int16 samples (returns the count) |
| `2320d080` | per-parameter jump detection, sub-block smoothing (2-pole), dB -> linear lookup |
| `2320d480` | voiced source; `2320d720` noise and burst; `2320d870` per-channel excitation mix |
| `2320da30` | formant filter bank |
| `23205120` | the engine's frame loop: rule front end (`23206530`/`232068f0`) -> `ce70` -> `e170` |

The rate is a constructor argument and every table follows it. At 10000 Hz a frame is 100 samples in 10 sub-blocks
of 10, the Apollo's own 10 ms frame, so it runs at our rate with no resampling (spec R2).

## Frame: 16 shorts

| p | Meaning | Units / range |
|---|---|---|
| 0-3 | channel 0-3 frequencies (FN, F1, F2, F3) | Hz in, converted to centi-Bark (table 0..1872) |
| 4-11 | channel 0-7 amplitudes | dB codes 0..63, 1 dB per step (63 = 0 dB, 0-1 = off) |
| 12 | degree of voicing | 0..~100; channel k uses p12 + {0,-16,-29,-42,-54,-67,-70,-73}, clamped 0..31 |
| 13 | F0 | Hz (a phase accumulator against fs) |
| 14 | glottal pulse rate | 40..550: the pulse lasts fs/p14 samples. The front end sets (5/3)(2F0 - 1.5F0*MSR/100) |
| 15 | voice source | 0 = shaped 72-step glottal pulse, 1 = impulse (a flat run plus one spike) |

## Signal path, per sub-block

- **Voiced source:** the phase advances by F0 each sample, and each period it starts the 72-step table. The table
  advances at p14 * 72 / fs, holding each value until the next step. The table's mean at that rate is subtracted,
  so the source has no DC. The table position also gives a 0..16 phase index (18-entry table), which drives the
  pitch-synchronous bandwidths below.
- **Noise:** `x = (x*4021 + 1) mod 16384`, centred and scaled by sqrt(1.5*200/fs)/4, then a first-order filter.
  A burst writes one impulse of length fs*5 ms, at 90 % of it.
- **Mix:** each of the 8 channels gets `voiced*sqrt(v/31) + noise*sqrt(1-v/31)`, using its own voicing index v (a
  power-complementary mix, the Holmes per-formant voicing). The result is multiplied by that channel's amplitude.
- **Filter bank:** each resonator is `y = A x + B y1 + C y2`, with output `y + D y1`. B = 2r cos(theta) from the
  centi-Bark cosine table, r = e^(-pi*BW/fs) and C = -r^2. Coefficients change once per sub-block.

| ch | Frequency | Bandwidth (Hz) | Gain A | Notes |
|---|---|---|---|---|
| 0 | p0 | 180 | (1-B-C) * -132.06 | its excitation is ch0 minus ch1 |
| 1 | p1 | 60 -> 260 pitch-synchronous | (1-B-C) * 398.84 | then a fixed first-order shaping section |
| 2 | p2 | 70 -> 170 pitch-synchronous | -11.72 | D = -1 |
| 3 | p3 | 90 -> 190 pitch-synchronous | +11.72 | D = -1 |
| 4 | fixed 3210/3510/3810 Hz cascade | 120/200/120 | -0.586 | p8 |
| 5 | fixed 3970/4345/4620 Hz cascade | 200/240/200 | +1.5 | p9 (about 22 dB hotter than ch4 per code) |
| 6, 7 | higher fixed cascades | | -1.78 / +3.02 | only when fs > 20000 |

  When A(ch0) exceeds A(ch1) by more than 4 dB and F1 is above 350 cBark, F1-F3 widen by up to 120/70/90 Hz
  (cfg 0x7d0-0x7e0). The channels are summed with alternating signs, then go through a one-pole de-emphasis
  (pole 0.995). Its gain is volume * 0.76 at DC. The output is clipped to int16.
- **Smoothing:** each frame, every parameter that moves less than its threshold is low-pass filtered across the
  sub-blocks. The thresholds are 154 cBark (frequencies), 12 dB (amplitudes) and 50 (p12-p14). A larger move is a
  jump: the parameter steps, and its smoothing filter is reset. When a channel's amplitude falls sharply, its
  frequency is held.

## Fit to the Apollo frame

The parameter sets line up almost one to one: FN/F1/F2/F3 -> p0-p3, ALF/A1/A2/A3/AHF -> p4-p8, V -> p12,
F0 -> p13, and the @M mark-space ratio -> p14. The amplitude codes are 6-bit at 1 dB per step, where our
calibration fit found 0.949 dB/code. The voiced source is a shaped pulse or an impulse, which matches our impulse
source and the b11 Rosenberg hypothesis. Orpheus's own front end interpolates phoneme targets (`232068f0`), but that
part is not needed: the Apollo firmware already produces the frames.

The proof-of-concept mapping in `orpheus_render.py`:

- frequencies from `calib.h` (the FN, F1, F2 and F3 fits);
- amplitudes passed through as codes, with p9 = AHF - 22;
- p12 = (V-1)*100/62;
- F0 as `params.c` computes it, including the x2 range gain;
- MSR fixed at 50 and volume 36;
- the median-pitch centre is approximate.

## Listening round 1: breathiness and our output filters

The Orpheus render sounded less dull than ours, but a little breathy. Share of total energy per band (dB), averaged
over the six items in `listen/`:

| Variant | < 300 | 300-1k | 1-2.5k | 2.5-3.5k | 3.5-5k |
|---|---|---|---|---|---|
| ours (peak + low-pass) | -5.6 | -2.2 | -10.1 | -20.4 | -25.2 |
| ours, both filters off | -8.7 | -1.8 | -7.8 | -17.8 | -20.3 |
| orpheus (first mapping) | -3.1 | -3.1 | -23.4 | -29.0 | -22.7 |
| orpheus_bal | -3.3 | -3.1 | -14.9 | -24.8 | -26.8 |

- The Orpheus breathiness is not a filter. The first mapping leaves F2/F3 about 12 dB weak (1-2.5 kHz), while the
  fixed 3.2-4.6 kHz chains, fed from AHF, stay strong. `orpheus_bal` (`alf=-10 a1=-10 a3=-2 p8=-14 p9=-36`,
  volume 80) brings the bands close to ours. These offsets are a listening trial, not a calibration.
- In our synth the low-pass only trims 3.5-5 kHz (about 2 dB). The low-end peak is what makes it sound dull: without
  it, the level drops about 3 dB and the balance moves toward 1-3.5 kHz.
- Variants: `_ours_nopeak`, `_ours_nolpf` and `_ours_nofilters` come from `frm2wav` (`frm2wav_build.bat`). Its default
  output is byte-identical to `apollo2 -o`. `listen/matched/` holds every file at the same RMS (66 dB re 1 LSB, no
  peak limiting needed), so loudness does not bias the comparison.
- **Verdict (user, 2026-10-16):** `orpheus_bal` sounds best: enough low end, a good high end, not too breathy. It
  is the starting mapping for the C port: the `calib.h` frequency fits, amplitude codes passed through with
  ALF -10, A1 -10, A2 0, A3 -2, AHF -> p8 at -14 and p9 at -36, p12 = (V-1)*100/62, MSR 50, voice source 0.

## Open questions

1. **Per-channel levels.** Orpheus's channel gains were designed for Orpheus's own codes. The Apollo codes may sit
   at different offsets per channel (F3 looks weak in the spectrograms). The `.TTS` phoneme target tables, put
   against the ROM's phoneme tables, could pin down both the offsets and the code -> Hz maps.
2. **Is this the Apollo's DSP?** The design (Bark tables, 6-bit dB amplitudes, per-channel voicing, MSR) fits the
   Apollo's frame bytes closely, but nothing here proves the uPD77C20 program worked the same way.
3. **Licensing.** A C port would embed Dolphin's constants and tables: two 72-step pulses, gains and bandwidths. That
   is a few hundred numbers, like the ROM tables already in `src/firmware/data/`.

## The C port

`src/orpheus/` is the back end in C, selected with `apollo2 --synth orpheus`. The default stays `holmes`, and its WAVs
are byte-identical to before the port.

- `orp_data.c` is generated by `xtras/orpheus/orpheus_gendata.py`: the synth's state after init at 10 kHz, captured
  from the oracle, and the final filter's gain for volume 80. `orp_synth.c` transliterates `ce70`, `e170`, `d080`,
  `d480`, `d720`, `d870` and `da30`. `orp_map.c` is the `orpheus_bal` mapping; `xtras/orpheus/orpheus_map.py` is its
  Python twin.
- **Exact, not close:** `suite_orpheus` compares five oracle renders sample for sample, all equal: `hello`, `prose`,
  `breathy`, the 1677-frame `long_clause` and a stress sequence (jumps, holds, bursts, clipping, source 1). It also
  checks that the C mapping reproduces every `.par` frame.
- **Why an x87 model:** a plain `double` port missed 12 samples of `breathy`. A smoothed track decaying through 1435
  was `1434.9999999999982` on the x87's 80-bit stack and exactly `1435.0` in `double`, so the frequency index
  truncated differently. `orp_x87.c` models the x87's arithmetic (64-bit significand, round to nearest even,
  subnormal stores). It is used only where the DLL truncates or compares an x87 result: the smoothers, Hz -> Bark,
  the widening factor, the amplitude lookup, the bandwidth phase and the DC-normalised gains. The per-sample
  resonators stay in `double`. 3100 Unicorn vectors check the model (`tests/orpheus/x87.txt`,
  `orpheus_x87vectors.py`).
- **Precision control:** until the synth's first asm block runs `fninit`, the x87 is at the MSVC CRT's 53-bit default
  (0x027F); after that it is at 64 bits (0x037F). So the first frame's set-up runs in `double`, and the port tracks
  the switch (`x87_ext`). Unicorn starts at control word 0, so `a3s_emu.py` sets 0x027F for each new synth, as a
  fresh Windows thread would have. Without that, the init tables came out at float precision.

## Possible next step

Calibrate the Apollo -> Orpheus mapping (levels, V, MSR) by listening, against the Juno LTAS, or from the `.TTS`
phoneme tables against the ROM's. Then decide whether `orpheus` becomes the default synth.
