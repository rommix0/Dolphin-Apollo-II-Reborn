# Generates, from the Unicorn oracle (a3s.dll's own init and render code, a3s_emu.py):
#   src/orpheus/orp_data.c   the Orpheus synth's state after init at 10 kHz (2320bb10, 2320c620, 2320c510), plus the
#                            final filter's gain for volume 80, as double bit patterns (orp_data.h declares them)
#   tests/orpheus/NAME.par   16 Orpheus parameters per frame (orpheus_map.py, or the stress sequence below)
#   tests/orpheus/NAME.pcm   the oracle's int16 LE output for them
#   tests/orpheus/NAME.frm   the Apollo frames the .par came from (copied from listen/)
# Usage, from the repository root: python xtras/orpheus/orpheus_gendata.py
import os
import shutil
import struct
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
from a3s_emu import make_synth, r32, rd, dbl, i16
import orpheus_map as M
from orpheus_render import render_params, map_frames

ITEMS = ('hello', 'prose', 'breathy', 'long_clause')
FILTERS = [('NOISE', 0xa398), ('FINAL', 0xa400), ('CASC', 0xa468), ('CH0', 0xa4d0), ('CH1', 0xa538),
           ('CH2', 0xa5a0), ('CH3', 0xa608)] + [('FIX%d' % k, 0xa670 + 0x68 * k) for k in range(6)] + \
          [('SMOOTH%d' % k, 0xab50 + 0x68 * k) for k in range(15)]
FILTER_FIELDS = (0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60)   # A B C E D y1 y2


def bits(x):
    return struct.unpack('<Q', struct.pack('<d', x))[0]


def dbl_array(name, values, comment):
    rows = ['    {0x%016XULL},' % bits(v) for v in values]
    return '/* %s */\nconst orp_f64 %s[%d] = {\n%s\n};\n' % (comment, name, len(values), '\n'.join(rows))


def i16_array(name, values, comment):
    rows = []
    for k in range(0, len(values), 12):
        rows.append('    ' + ', '.join('%d' % v for v in values[k:k + 12]) + ',')
    return '/* %s */\nconst int16_t %s[%d] = {\n%s\n};\n' % (comment, name, len(values), '\n'.join(rows))


def capture():
    o = make_synth(M.FS)
    cfg = r32(o + 0x28)
    w = lambda base, a: i16(base + a)
    assert r32(cfg) == M.FS and i16(o + 0x1dc) == 100 and i16(o + 0x1de) == 10 and i16(o + 0x1e0) == 10
    n_cos, n_bw, n_amp, n_vmix = (w(cfg, a) for a in (0x20, 0x22, 0x24, 0x26))
    assert (n_cos, n_bw, n_amp, n_vmix) == (1873, 100, 64, 32)
    assert rd(o + 0xa328, 2) == b'\x01\x01'   # unvoiced and prev_unvoiced start set (2320c510)
    tab = lambda off, n: [dbl(r32(o + off) + 8 * i) for i in range(n)]
    out = []
    out.append(dbl_array('orp_cos2', tab(0x14, n_cos), '2cos(2 pi f/fs) by centi-Bark (object +0x14)'))
    out.append(dbl_array('orp_r', tab(0x18, n_bw), 'r = e^(-pi B/fs), B in 8 Hz steps (+0x18)'))
    out.append(dbl_array('orp_negr2', tab(0x1c, n_bw), '-r^2 (+0x1c)'))
    out.append(dbl_array('orp_amp', tab(0x20, n_amp), 'amplitude code -> linear, 1 dB per step (+0x20)'))
    out.append(dbl_array('orp_vmix', tab(0x24, n_vmix), 'voicing mix weights sqrt(i/31) (+0x24)'))
    out.append(dbl_array('orp_glottal_mean', [dbl(o + 0x378 + 0xff8 * s + 8 * i) for s in range(2) for i in range(53)],
                         'mean of each source table at pulse rate 40 + 10i (+0x378, 2 sources x 53)'))
    filt = []
    for name, base in FILTERS:
        filt += [dbl(o + base + f) for f in FILTER_FIELDS]
    final_a = capture_final_gain()
    filt[FILTER_FIELDS.index(0x30) + 7 * [n for n, _ in FILTERS].index('FINAL')] = final_a
    out.append(dbl_array('orp_filters', filt, 'A B C E D y1 y2 per filter, in orp_data.h ORP_F_* order (FINAL A '
                         'for volume %d)' % M.VOLUME))
    cfg_d = [dbl(cfg + a) for a in (0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80)] + \
            [dbl(cfg + 0x18), dbl(cfg + 0x7d0), dbl(cfg + 0x7d8), dbl(cfg + 0x7e0), dbl(o + 0x338), dbl(o + 0x348)]
    out.append(dbl_array('orp_cfg', cfg_d, 'channel gains 0-7, pitch-sync modulation, widening dB/F1/divisor, '
                         'noise scale, burst amplitude'))
    cfg_i = [w(cfg, 0x88 + 2 * k) for k in range(8)] + [w(cfg, 0xa8 + 2 * k) for k in range(4)] + \
            [w(cfg, 0x7c6), w(cfg, 0x7c8), w(o, 0x330), w(o, 0x332), w(o, 0x334), r32(o + 0x350), r32(o + 0x354)]
    out.append(i16_array('orp_cfg_int', cfg_i, 'orp_data.h ORP_I_*: voicing offsets, widening, holds, noise LCG, '
                         'burst'))
    params = [w(o, 0x24c + 2 * i) for i in range(64)]
    out.append(i16_array('orp_init_params', params, 'current, previous, jump threshold, jump flag x 16 (+0x24c)'))
    out.append(i16_array('orp_glottal', [w(cfg, 0xb8 + 2 * i) for i in range(144)], 'source tables, 2 x 72'))
    out.append(i16_array('orp_phase_map', [w(cfg, 0x658 + 2 * i) for i in range(36)],
                         'source step / 4 -> pitch-synchronous phase 0-16, 2 x 18'))
    out.append(i16_array('orp_bwtab', [i16(r32(o + 0x23c + 4 * k) + 2 * i) for k in range(4) for i in range(17)],
                         'bandwidth (Hz) by phase 0-16, channels 0-3'))
    return out


def capture_final_gain():
    """2320e170 recomputes the final filter's A from the volume each frame (232028f0): read it after one frame."""
    from a3s_emu import alloc, call, mu
    o = make_synth(M.FS)
    fr = alloc(32)
    buf = alloc(400)
    mu.mem_write(fr, struct.pack('<16h', *([0] * 16)))
    call(0x2320ce70, o, (fr, M.VOLUME, 1))
    call(0x2320e170, o, (1, buf))
    return dbl(o + 0xa400 + 0x30)


def stress():
    """Deterministic frames for the review focus: silence, onsets, jumps, holds, bursts, clipping, source 1."""
    sil = [250, 500, 1500, 2500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 120, 250, 0]
    vowel = [500, 700, 1200, 2500, 50, 58, 52, 45, 40, 20, 0, 0, 100, 110, 229, 0]
    seq = [sil] * 5 + [vowel] * 10
    seq += [vowel[:2] + [2000] + vowel[3:]] * 5                       # F2 jump (> 154 cBark)
    seq += [vowel[:13] + [170, 354, 0]] * 5                            # F0 jump (> 50)
    seq += [[300, 900] + vowel[2:5] + [20] + vowel[6:]] * 4            # A1 drop by 38 with F1 move: hold
    for k in range(3):                                                 # bursts: unvoiced, amplitudes jump up
        seq += [sil] * 3 + [[400, 1600, 2400, 3500, 10, 20, 40 + k, 45, 50, 30, 0, 0, 0, 120, 250, 0]] * 3
    seq += [vowel[:4] + [63] * 6 + [0, 0, 100, 140, 291, 0]] * 2         # over range: clips
    seq += [vowel[:12] + [30, 115, 239, 0]] * 4                        # partial voicing
    seq += [vowel[:14] + [20, 0]] * 3 + [vowel[:14] + [700, 0]] * 3    # pulse rate clamps
    seq += [vowel[:15] + [1]] * 5                                      # voice source 1 (impulse)
    seq += [vowel] * 5 + [sil] * 5
    return seq


def main():
    with open(os.path.join(ROOT, 'src', 'orpheus', 'orp_data.c'), 'w', newline='\n') as f:
        f.write('/* GENERATED by xtras/orpheus/orpheus_gendata.py from the Unicorn oracle (a3s.dll, Orpheus '
                'Build 18):\n   the synth\'s state after init at %d Hz. Edit the generator, not this file. */\n'
                '#include "orpheus/orp_data.h"\n\n' % M.FS)
        f.write('\n'.join(capture()))
    tdir = os.path.join(ROOT, 'tests', 'orpheus')
    os.makedirs(tdir, exist_ok=True)
    jobs = [(n, map_frames(M.read_frm(os.path.join(HERE, 'listen', n + '.frm')))) for n in ITEMS]
    jobs.append(('stress', stress()))
    for name, params in jobs:
        if name != 'stress':
            shutil.copyfile(os.path.join(HERE, 'listen', name + '.frm'), os.path.join(tdir, name + '.frm'))
        with open(os.path.join(tdir, name + '.par'), 'w', newline='\n') as f:
            f.writelines(' '.join('%d' % v for v in p) + '\n' for p in params)
        pcm = render_params(params)
        with open(os.path.join(tdir, name + '.pcm'), 'wb') as f:
            f.write(struct.pack('<%dh' % len(pcm), *pcm))
        print('%-12s %5d frames, clipped %d' % (name, len(params), sum(1 for x in pcm if x in (32767, -32768))))


if __name__ == '__main__':
    main()
