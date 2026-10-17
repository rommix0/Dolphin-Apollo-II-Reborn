import os
import sys,bisect
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'a3s_static.py')).read())
for a in sys.argv[1:]:
    f=int(a,16); k=bisect.bisect_right(funcs,f); end=funcs[k] if k<len(funcs) else T1
    print("==== func %08x  callers: %s"%(f,' '.join('%08x'%c for c in sorted(calls.get(f,())))))
    j=byaddr[f]
    while j<len(insns) and insns[j].address<end:
        i=insns[j]; s=fmt(i)
        if i.mnemonic=='call' and i.op_str.startswith('0x'): s+="   ; -> %s"%exports.get(int(i.op_str,16),'')
        print(s); j+=1
