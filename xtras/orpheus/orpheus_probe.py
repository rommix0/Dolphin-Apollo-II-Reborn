import os
import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a3s_emu import *
import math
def run(p, nframes=30, vol=1):
    o=make_synth(10000); fr=alloc(32); out=alloc(4000); pcm=[]
    for k in range(nframes):
        mu.mem_write(fr,struct.pack('<16h',*p)); call(0x2320ce70,o,(fr,vol,1))
        n=call(0x2320e170,o,(1 if k==0 else 0,out)); pcm+=struct.unpack('<%dh'%n,rd(out,2*n))
    x=pcm[1000:]; return 20*math.log10(max(1e-9,(sum(v*v for v in x)/len(x))**.5))
base=[250,500,1500,2500, 0,0,0,0,0,0,0,0, 63,120,250,0]
for ch in range(8):
    row=[]
    for a in (40,60,80,99):
        p=list(base); p[4+ch]=a; row.append("%6.1f"%run(p))
    print("amp p%d"%(4+ch), row)
for v in (0,20,40,63):
    p=list(base); p[5]=80; p[12]=v; print("voicing",v,"%6.1f"%run(p))
