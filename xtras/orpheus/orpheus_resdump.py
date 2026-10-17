import os
import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a3s_emu import *
import math
o=make_synth(10000); fs=10000
def show(name,base):
    A,B,C,Dx,D,y1,y2=[dbl(o+base+off) for off in (0x30,0x38,0x40,0x48,0x50,0x58,0x60)]
    s="%-10s %06x A=% .5g B=% .5g C=% .5g x48=% .5g D=% .5g"%(name,base,A,B,C,Dx,D)
    if C<0 and B!=0:
        r=math.sqrt(-C); c=B/(2*r)
        if abs(c)<=1: s+="  pole f=%.0fHz bw=%.0fHz"%(math.acos(c)*fs/2/math.pi, -math.log(r)*fs/math.pi)
    print(s)
show('final',0xa400)
show('ch1casc',0xa468)
show('ch0',0xa4d0)
show('ch1',0xa538)
show('ch2',0xa5a0)
show('ch3',0xa608)
for k in range(12): show('fix%d'%k,0xa670+0x68*k)
show('noise',0xa398)
for k in range(15): show('smooth%d'%k,0xab50+0x68*k)
