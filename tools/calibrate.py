"""Synthesizer calibration (spec section 5). Fits src/synth/calib.h and writes docs/calibration/.

  python tools/calibrate.py fit [--ltas] [--master-gain G] [--b11-gain-db G]   refit src/synth/calib.h
  python tools/calibrate.py set [NAME=VALUE ...]            rewrite calib.h with overrides (no fitting)
  python tools/calibrate.py report                          plots + report + listening WAVs

Inputs:
- build/apollo2_frames.exe and the ROM, for the steady vowel frames;
- tests/golden/*.frm, for the report's F0 distribution;
- the male passages of dolphin_juno_demo.mp3, decoded by ffmpeg at its native rate (nothing is resampled,
  spec R2);
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
JUNO_MALE = [(0.0, 9.4), (17.3, 20.0)]   # male passages within the snippet (file time 5-14.4 s and 22.3-25 s);
                                         # 14.5-22 s is a higher voice and is excluded
LTAS_GRID = np.arange(100, 3801, 25, dtype=float)   # up to the output low-pass
CHANNEL_BANDS = [('ALF', 200, 330), ('A1', 350, 800), ('A2', 900, 2000), ('A3', 2100, 3000), ('AHF', 3100, 3700)]
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
    ('', None), ('/* Per-channel corrections from the long-term average spectrum of the Juno\'s male passages\n'
                 '   (calibrate.py fit --ltas). Added to CAL_G_x_DB, which the Wells (1962) fit owns. */', None),
    ('CAL_LTAS_ALF_DB', 'f'), ('CAL_LTAS_A1_DB', 'f'), ('CAL_LTAS_A2_DB', 'f'), ('CAL_LTAS_A3_DB', 'f'),
    ('CAL_LTAS_AHF_DB', 'f'),
    ('', None), ('/* Transitions: frames with b13 bit 7 set (closures, releases, onsets, some vowel/sonorant\n'
                 '   boundaries) reach their amplitudes within this many samples instead of across the frame. */', None),
    ('CAL_STEP_RAMP_SAMPLES', 'int'),
    ('', None), ('/* F0: b10 is a pitch period of 160 - 2*code samples (ROM $B930; params.c). The pitch movement\n'
                 '   of an utterance is widened by CAL_F0_RANGE_GAIN around its median (render passes the centre). */', None),
    ('CAL_F0_RANGE_GAIN', 'f'), ('CAL_F0_MIN_HZ', 'f'), ('CAL_F0_MAX_HZ', 'f'),
    ('', None), ('/* Voiced-source low-end peak (restores the fundamental) and the output reconstruction low-pass\n'
                 '   (4th-order Butterworth, like the DAC\'s analog filter); both chosen by listening. */', None),
    ('CAL_PEAK_HZ', 'f'), ('CAL_PEAK_Q', 'f'), ('CAL_PEAK_DB', 'f'), ('CAL_LPF_HZ', 'f'),
    ('', None), ('/* Output level: synth units -> int16 full scale (the corpus must peak between -12 and -1 dBFS\n'
                 '   without clipping; see suite_synth) */', None),
    ('CAL_MASTER_GAIN', 'f'), ('CAL_B11_GAIN_DB', 'f'),
]
NOTES = {'CAL_FN_B': 'hypothesis H-FN: a low resonance, ~256 Hz at the usual code 32',
         'CAL_F4_HZ': 'the fixed fourth formant, driven by AHF',
         'CAL_B11_GAIN_DB': 'level match of the b11 (Rosenberg) voiced source to the impulse source',
         'CAL_F0_RANGE_GAIN': 'chosen by listening: 2.0 doubles the firmware contour in semitones',
         'CAL_STEP_RAMP_SAMPLES': 'chosen by listening: no closure thump, no boundary clicks',}

# Seeds for constants that a calib.h written by an older generator does not have yet.
DEFAULTS = {'CAL_PEAK_HZ': 110.0, 'CAL_PEAK_Q': 0.6, 'CAL_PEAK_DB': 10.0, 'CAL_LPF_HZ': 3800.0,
            'CAL_B11_GAIN_DB': 0.0, 'CAL_F0_RANGE_GAIN': 2.0, 'CAL_STEP_RAMP_SAMPLES': 16,
            'CAL_LTAS_ALF_DB': 0.0, 'CAL_LTAS_A1_DB': 0.0, 'CAL_LTAS_A2_DB': 0.0, 'CAL_LTAS_A3_DB': 0.0,
            'CAL_LTAS_AHF_DB': 0.0}


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
    for name, value in DEFAULTS.items():
        vals.setdefault(name, value)
    return vals


def write_calib(vals):
    out = ['#ifndef SYNTH_CALIB_H', '#define SYNTH_CALIB_H',
           "/* Synthesizer calibration (spec section 5): the only place the synthesizer's",
           '   calibrated (fitted) constants live (structural constants stay in params.c and source.c).',
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


def f0_hz(vals, codes):
    """b10 codes -> Hz through the firmware period scale (params.c params_f0_hz)."""
    period = np.maximum(160.0 - 2.0 * np.asarray(codes, float), 10000.0 / vals['CAL_F0_MAX_HZ'])
    return np.clip(10000.0 / period, vals['CAL_F0_MIN_HZ'], vals['CAL_F0_MAX_HZ'])


def golden_f0_hz(vals):
    """F0 of the plain corpus's voiced frames as the synthesizer plays them: the period scale,
    widened by CAL_F0_RANGE_GAIN around each utterance's lower median (render.c, params.c)."""
    out = []
    for name in PLAIN_CORPUS:
        frames = [bytes.fromhex(line.strip()) for line in open(os.path.join(ROOT, 'tests', 'golden', name + '.frm'))]
        codes = sorted(f[10] for f in frames if f[9] >= 40 and f[2] > 10)
        if not codes:
            continue
        raw = f0_hz(vals, [f[10] for f in frames if f[9] >= 40 and f[2] > 10])
        centre = f0_hz(vals, [codes[(len(codes) - 1) // 2]])[0]
        out.append(np.clip(centre * (raw / centre) ** vals['CAL_F0_RANGE_GAIN'],
                           vals['CAL_F0_MIN_HZ'], vals['CAL_F0_MAX_HZ']))
    return np.concatenate(out)


def juno_male_audio():
    """The Juno snippet's male passages, decoded at the file's native rate (nothing is resampled)."""
    rate = int(subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                               'stream=sample_rate', '-of', 'csv=p=0', JUNO],
                              capture_output=True, text=True, check=True).stdout.strip())
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(JUNO_START_S), '-t', str(JUNO_DUR_S), '-i', JUNO,
                          '-ac', '1', '-f', 's16le', '-'], capture_output=True, check=True).stdout
    y = np.frombuffer(raw, np.int16).astype(np.float32) / 32768.0
    return np.concatenate([y[int(a * rate):int(b * rate)] for a, b in JUNO_MALE]), rate


def juno_f0(y, sr):
    import librosa
    f0, voiced, _ = librosa.pyin(y, fmin=60, fmax=300, sr=sr, frame_length=4096, hop_length=sr // 100)
    return f0[voiced & ~np.isnan(f0)]


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
    if args.ltas:
        ltas = json.load(open(LTAS_JSON))
        if ltas.get('applied'):
            sys.exit('ltas.json was already applied; run "report" again first')
        for ch, d in ltas['deltas_db'].items():
            vals[f'CAL_LTAS_{ch}_DB'] += d
        ltas['applied'] = True
        json.dump(ltas, open(LTAS_JSON, 'w'), indent=1)
        print('LTAS applied: ' + ', '.join(f'{ch} {d:+.1f} dB' for ch, d in ltas['deltas_db'].items()))
    if args.master_gain is not None:
        vals['CAL_MASTER_GAIN'] = args.master_gain
    if args.b11_gain_db is not None:
        vals['CAL_B11_GAIN_DB'] = args.b11_gain_db
    write_calib(vals)
    print(f'wrote {os.path.relpath(CALIB_H, ROOT)}; rebuild with cmd //c ".\\build.bat test"')


def cmd_set(args):
    """Rewrites calib.h from its current values plus NAME=VALUE overrides. No fitting, so neither the
    ROM nor ffmpeg is needed; with no overrides it only regenerates the file (new TEMPLATE entries
    take their DEFAULTS)."""
    vals = read_calib()
    names = {name for name, kind in TEMPLATE if kind}
    for item in args.assign:
        name, _, value = item.partition('=')
        if name not in names or not value:
            sys.exit(f'unknown or malformed assignment {item!r}; known names: {", ".join(sorted(names))}')
        vals[name] = float(value)
    write_calib(vals)
    print(f'wrote {os.path.relpath(CALIB_H, ROOT)}; rebuild with cmd //c ".\\build.bat test"')


def render(text_file, wav, extra=()):
    subprocess.run([APOLLO2_EXE, *extra, '--text-file', text_file, '-o', wav], check=True, cwd=ROOT)
    from scipy.io import wavfile
    rate, x = wavfile.read(wav)
    return x.astype(np.float32) / 32768.0, rate


def ltas_db(x, sr, nperseg):
    """Long-term average spectrum on LTAS_GRID, in dB relative to its mean over 200-3700 Hz."""
    from scipy.signal import welch
    f, p = welch(x, sr, nperseg=nperseg)
    db = 10 * np.log10(np.interp(LTAS_GRID, f, p) + 1e-20)
    return db - db[(LTAS_GRID >= 200) & (LTAS_GRID <= 3700)].mean()


def ltas_deltas(s_db, j_db):
    """Per-channel correction (dB): minus the mean synth-minus-Juno difference over the channel's band."""
    diff = s_db - j_db
    return {ch: float(np.clip(-diff[(LTAS_GRID >= lo) & (LTAS_GRID <= hi)].mean(), -12, 12))
            for ch, lo, hi in CHANNEL_BANDS}


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
    ours = golden_f0_hz(vals)
    y, sr = juno_male_audio()
    jf0 = juno_f0(y, sr)
    fig, ax = plt.subplots(figsize=(7, 4))
    bins = np.arange(50, 320, 10)
    ax.hist(jf0, bins, alpha=0.5, density=True, label='Juno, male passages (pyin)')
    ax.hist(ours, bins, alpha=0.5, density=True, label='plain corpus: period scale + range gain')
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
        if name == 'prose':
            plain_prose = x
    prose = os.path.join(ROOT, 'tests', 'corpus', 'prose.txt')
    render(prose, os.path.join(LISTEN_DIR, 'prose_voice_b.wav'), ['--voice', 'b'])
    b11_prose, _ = render(prose, os.path.join(LISTEN_DIR, 'prose_hyp_b11.wav'), ['--hyp', 'b11'])
    rms_db = lambda x: 20 * np.log10(np.sqrt(np.mean(x ** 2)))
    b11_diff = float(rms_db(b11_prose) - rms_db(plain_prose))
    render(prose, os.path.join(LISTEN_DIR, 'prose_hyp_b13.wav'), ['--hyp', 'b13'])
    render(prose, os.path.join(LISTEN_DIR, 'prose_prosody3.wav'), ['--prosody', '3'])
    s_db = ltas_db(np.concatenate(synth), rate, 1024)
    j_db = ltas_db(y, sr, 4096)
    deltas = ltas_deltas(s_db, j_db)
    json.dump({'deltas_db': deltas, 'applied': False, 'grid_hz': LTAS_GRID.tolist(),
               'synth_db': s_db.tolist(), 'juno_db': j_db.tolist()}, open(LTAS_JSON, 'w'), indent=1)
    band_rows = [f'| {ch} | {lo}-{hi} | {-deltas[ch]:+.1f} | {deltas[ch]:+.1f} | {vals[f"CAL_LTAS_{ch}_DB"]:+.1f} |'
                 for ch, lo, hi in CHANNEL_BANDS]
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot(LTAS_GRID, s_db, label='synth, plain corpus')
    ax.plot(LTAS_GRID, j_db, label='Juno, male passages')
    ax.set_xlabel('Hz')
    ax.set_ylabel('dB re mean 200-3700 Hz')
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(REPORT_DIR, 'ltas.png'), dpi=90)
    plt.close(fig)

    q = lambda a: ' / '.join(f'{v:.0f}' for v in np.percentile(a, [5, 50, 95]))
    md = [
        '# Synthesizer calibration report', '',
        'Generated by `tools/calibrate.py report` from the current `src/synth/calib.h`. Reference:',
        'Wells (1962) RP male means (`tools/data/`). Juno: the male passages of `dolphin_juno_demo.mp3` '
        '(file time 5-14.4 s and 22.3-25 s).', '',
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
        f'AHF {vals["CAL_G_AHF_DB"]:+.1f}. '
        f'LTAS corrections (dB): ' + ', '.join(f'{ch} {vals[f"CAL_LTAS_{ch}_DB"]:+.1f}' for ch, _, _ in CHANNEL_BANDS) + '.',
        '',
        '## F0', '',
        f'Period scale (ROM $B930), range gain {vals["CAL_F0_RANGE_GAIN"]:.1f}. 5th/50th/95th percentile: '
        f'Juno male {q(jf0)} Hz; plain corpus {q(ours)} Hz.', '',
        '![f0](f0.png)', '',
        '## Long-term average spectrum', '',
        '| Channel | Band (Hz) | synth − Juno (dB) | Suggested correction (dB) | Applied (CAL_LTAS_x_DB) |',
        '|---|---|---|---|---|', *band_rows, '',
        'Apply the suggested corrections with `calibrate.py fit --ltas`.', '',
        '![ltas](ltas.png)', '',
        '## Listening files (spec R4)', '',
        'Rendered to `build/listening/` (not committed): every corpus text, plus `prose_voice_b`, '
        '`prose_hyp_b11` and `prose_hyp_b13` for the hypothesis switches, and `prose_prosody3` '
        '(the device\'s default prosody).', '',
        f'b11 render level vs plain: {b11_diff:+.1f} dB (RMS of `prose_hyp_b11` minus `prose`; '
        f'CAL_B11_GAIN_DB {vals["CAL_B11_GAIN_DB"]:+.1f} dB).', '',
    ]
    with open(os.path.join(REPORT_DIR, 'report.md'), 'w', newline='\n', encoding='utf-8') as f:
        f.write('\n'.join(md))
    print(f'wrote {os.path.relpath(os.path.join(REPORT_DIR, "report.md"), ROOT)} and {len(synth)} plain renders; '
          f'LTAS suggests ' + ', '.join(f'{ch} {d:+.1f}' for ch, d in deltas.items()) + ' dB')
    print(f'b11 render level vs plain: {b11_diff:+.2f} dB; suggested '
          f'--b11-gain-db {vals["CAL_B11_GAIN_DB"] - b11_diff:.2f}')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    fit = sub.add_parser('fit')
    fit.add_argument('--ltas', action='store_true', help='add the five channel corrections from the last report')
    fit.add_argument('--master-gain', type=float, default=None)
    fit.add_argument('--b11-gain-db', type=float, default=None, help='level match of the b11 hypothesis render')
    sub.add_parser('report')
    st = sub.add_parser('set')
    st.add_argument('assign', nargs='*', metavar='NAME=VALUE')
    args = p.parse_args()
    {'fit': cmd_fit, 'report': cmd_report, 'set': cmd_set}[args.cmd](args)


if __name__ == '__main__':
    main()
