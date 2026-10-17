import os
import bisect,collections
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'a3s_static.py')).read())
callees=collections.defaultdict(list)
for i in insns:
    if i.mnemonic=='call':
        f=func_of(i.address)
        if i.op_str.startswith('0x'): callees[f].append(int(i.op_str,16))
        elif 'dword ptr [0x' in i.op_str:
            v=int(i.op_str.split('[')[1].rstrip(']'),16); callees[f].append(imports.get(v,hex(v)))
        else: callees[f].append('ind:'+i.op_str)
def name(t): return exports.get(t,'%08x'%t) if isinstance(t,int) else t
seen=set()
def walk(f,d):
    if d>7: return
    print('  '*d+name(f)+('' if f not in seen else ' ...'))
    if f in seen: return
    seen.add(f)
    for t in dict.fromkeys(callees.get(f,[])):
        if isinstance(t,int) and t<0x23210000: walk(t,d+1)
        elif not isinstance(t,int) and d<6: print('  '*(d+1)+t)
for e in sorted(exports, key=lambda a:exports[a]):
    if exports[e] in ('TTS_Open','TTS_Speak','TTS_Append','TTS_SpeakAppend'): walk(e,0)
