import os
import bisect,collections,struct
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'a3s_static.py')).read())
# data references to code addresses (vtables, function pointers)
dref=collections.defaultdict(list)
for s in pe.sections:
    if s.Name.startswith(b'.text'): continue
    b=rd(IB+s.VirtualAddress, s.SizeOfRawData)
    for o in range(0,len(b)-3,4):
        v=struct.unpack_from('<I',b,o)[0]
        if T0<=v<T1: dref[v].append(IB+s.VirtualAddress+o)
# immediate references in code (push offset / mov reg, imm)
iref=collections.defaultdict(list)
for i in insns:
    for m in re.findall(r'0x[0-9a-f]{8}',i.op_str):
        v=int(m,16)
        if T0<=v<T1 and i.mnemonic!='call' and not i.mnemonic.startswith('j'): iref[v].append(i.address)
allf=sorted(set(funcs)|set(dref)|set(iref))
lo,hi=[int(x,16) for x in sys.argv[1:3]]
for f in allf:
    if lo<=f<hi:
        hx=lambda xs: ' '.join('%08x'%c for c in xs)
        print('%08x callers=%s dref=%s iref=%s'%(f,hx(sorted(calls.get(f,()))),hx(dref.get(f,[])),hx(iref.get(f,[]))))
