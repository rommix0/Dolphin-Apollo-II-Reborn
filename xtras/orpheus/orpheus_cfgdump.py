import os
import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a3s_emu import *
o=make_synth(10000); c=r32(o+0x28)
w=lambda a: i16(c+a)
print("u32 0..0x20:", [r32(c+a) for a in range(0,0x20,4)])
print("words 0x20..0x28:", [w(a) for a in range(0x20,0x28,2)])
print("dbl 0x28..0x88:", [round(dbl(c+a),6) for a in range(0x28,0x88,8)])
print("words 0x88..0xb8:", [w(a) for a in range(0x88,0xb8,2)])
for t in range(2): print("glottal",t, [w(0xb8+t*144+2*i) for i in range(72)])
print("0x1d8..0x658 words:", [w(a) for a in range(0x1d8,0x658,2)])
for t in range(8): print("phase",t,[w(0x658+t*36+2*i) for i in range(18)])
print("0x778..0x7f8 words:", [w(a) for a in range(0x778,0x7c8,2)])
print("0x7c0.. words", [w(a) for a in range(0x7c0,0x7d0,2)], "dbl 0x7d0..", [dbl(c+a) for a in range(0x7d0,0x7f8,8)])
