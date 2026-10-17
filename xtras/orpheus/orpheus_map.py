# The Apollo -> Orpheus frame mapping (orpheus_bal; spec 2026-10-16-orpheus-synth-analysis.md, verdict), the single
# Python copy. src/orpheus/orp_map.c is its C twin: tests/orpheus/*.par come from here and the suite checks that the C
# gives the same 16 values for every frame.
import math
import numpy as np

FS = 10000
F32 = np.float32
# calib.h, as the float literals the C sees
FN_B = F32(8.0)
F1_A, F1_B = F32(139.364), F32(21.552)
F2_A, F2_B = F32(593.289), F32(44.587)
F3_A, F3_B = F32(1877.49), F32(0.0101545)
F0_RANGE_GAIN, F0_MIN, F0_MAX = F32(2.0), F32(40.0), F32(400.0)
OFFSETS = dict(alf=-10, a1=-10, a2=0, a3=-2, p8=-14, p9=-36)
MSR = 50
VOLUME = 80


def clampf(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def f0_hz(code):
    """params.c params_f0_hz, in float32."""
    period = F32(160.0) - F32(2.0) * F32(code)
    min_period = F32(FS) / F0_MAX
    if period < min_period:
        period = min_period
    return clampf(F32(FS) / period, F0_MIN, F0_MAX)


def f0_centre(frames):
    """params.c params_f0_centre: the lower median b10 code of the clearly voiced frames, as Hz; 0 if none."""
    codes = sorted(f[10] for f in frames if f[9] >= 40 and f[2] > 10)
    return f0_hz(codes[(len(codes) - 1) // 2]) if codes else F32(0.0)


def round_half_up(x):
    return int(math.floor(x + 0.5))


def amp(code, off):
    return 0 if code <= 1 else int(clampf(code + off, 0, 63))


def apollo_to_orpheus(f, centre):
    fn = float(FN_B) * f[1]
    f1 = float(F1_A) + float(F1_B) * f[3]
    f2 = float(F2_A) + float(F2_B) * f[5]
    f3 = float(F3_A) * math.exp(float(F3_B) * f[7])
    freqs = [round_half_up(clampf(x, 50.0, 4500.0)) for x in (fn, f1, f2, f3)]
    f0 = float(f0_hz(f[10]))
    c = float(centre)
    if c > 0.0:
        f0 = clampf(c * math.pow(f0 / c, float(F0_RANGE_GAIN)), float(F0_MIN), float(F0_MAX))
    p13 = round_half_up(f0)
    cx = 2 * p13 - ((2 * p13 - (p13 >> 1)) * MSR) // 100   # 23206847, all terms positive: // is C's /
    p14 = cx * 100 // 60
    p12 = int(clampf(round_half_up((f[9] - 1) * 100 / 62.0), 0, 100))
    o = OFFSETS
    return freqs + [amp(f[0], o['alf']), amp(f[2], o['a1']), amp(f[4], o['a2']), amp(f[6], o['a3']),
                    amp(f[8], o['p8']), amp(f[8], o['p9']), 0, 0, p12, p13, p14, 0]


def read_frm(path):
    return [bytes.fromhex(l.strip()) for l in open(path) if l.strip()]
