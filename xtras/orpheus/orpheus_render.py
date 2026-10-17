# Render Apollo frames (.frm, hex lines) through the emulated Orpheus synth back end (a3s.dll) at 10 kHz, with the
# orpheus_bal mapping (orpheus_map.py). Usage: orpheus_render.py IN.frm OUT.wav
import os
import sys
import struct
import time
import wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a3s_emu import make_synth, alloc, call, mu, rd
import orpheus_map as M


def render_params(params):
    """16-short Orpheus frames -> int16 samples: 2320ce70(p, volume, 1) then 2320e170(first, out) per frame."""
    o = make_synth(M.FS)
    fr = alloc(32)
    out = alloc(2 * 2000)
    pcm = []
    for k, p in enumerate(params):
        mu.mem_write(fr, struct.pack('<16h', *p))
        call(0x2320ce70, o, (fr, M.VOLUME, 1))
        n = call(0x2320e170, o, (1 if k == 0 else 0, out))
        pcm += struct.unpack('<%dh' % n, rd(out, 2 * n))
    return pcm


def map_frames(frames):
    c = M.f0_centre(frames)
    return [M.apollo_to_orpheus(f, c) for f in frames]


def write_wav(path, pcm):
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(M.FS)
    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))
    w.close()


if __name__ == '__main__':
    t = time.time()
    frames = M.read_frm(sys.argv[1])
    pcm = render_params(map_frames(frames))
    write_wav(sys.argv[2], pcm)
    clip = sum(1 for x in pcm if x in (32767, -32768))
    print(sys.argv[2], len(frames), "frames, peak", max(abs(x) for x in pcm), "clipped", clip,
          "%.1fs" % (time.time() - t))
