import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'a3s_static.py')).read())
import collections
stats=collections.defaultdict(collections.Counter)
for i in insns:
    f=func_of(i.address); m=i.mnemonic
    stats[f]['n']+=1
    if m.startswith('f'): stats[f]['fpu']+=1
    if m in('imul','idiv','mul'): stats[f]['mul']+=1
    if m in('sar','shl','shr'): stats[f]['sh']+=1
print(len(funcs),"funcs")
rows=sorted(stats.items(), key=lambda kv:-(kv[1]['fpu']*3+kv[1]['mul']*3))
for f,c in rows[:45]:
    print("%08x n=%5d fpu=%4d mul=%4d sh=%4d callers=%d %s"
          %(f,c['n'],c['fpu'],c['mul'],c['sh'],len(calls.get(f,())),exports.get(f,'')))
