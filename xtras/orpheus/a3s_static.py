import os
# Tiny static-analysis helper for a3s.dll (x86-32): functions, xrefs, disassembly.
import pefile, capstone, sys, re, collections, struct
F=os.environ.get("ORPHEUS_A3S", r"C:\Users\abart\Desktop\orpheus-classic-sapi5-main\bin\orpheus\a3s.dll")
pe=pefile.PE(F); IB=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
T0=IB+text.VirtualAddress; T1=T0+text.Misc_VirtualSize
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=False
def rd(va,n): return img[va-IB:va-IB+n]
def u32(va): return struct.unpack('<I',rd(va,4))[0]
imports={}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports: imports[i.address]=(i.name or b'#%d'%i.ordinal).decode()
exports={IB+e.address:e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols}
# linear sweep of whole .text
insns=[]
va=T0
code=rd(T0,T1-T0)
off=0
while off<len(code):
    got=False
    for i in md.disasm(code[off:], T0+off):
        insns.append(i); off=i.address+i.size-T0; got=True
    if off<len(code): off+=1
byaddr={i.address:k for k,i in enumerate(insns)}
calls=collections.defaultdict(set); funcs=set(exports)
for i in insns:
    if i.mnemonic=='call' and i.op_str.startswith('0x'):
        t=int(i.op_str,16); calls[t].add(i.address); funcs.add(t)
funcs=sorted(f for f in funcs if T0<=f<T1)
def func_of(a):
    import bisect
    k=bisect.bisect_right(funcs,a)-1
    return funcs[k] if k>=0 else None
def fmt(i):
    s=i.op_str
    def rep(m):
        v=int(m.group(0),16)
        if v in imports: return imports[v]
        if v in exports: return exports[v]
        return m.group(0)
    s=re.sub(r'0x[0-9a-f]+',rep,s)
    return "%08x  %-6s %s"%(i.address,i.mnemonic,s)
