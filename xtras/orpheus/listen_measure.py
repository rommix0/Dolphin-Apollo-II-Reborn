import sys, wave, numpy as np, glob, os
L=sys.argv[1]
def load(p):
    w=wave.open(p); return np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)
def bands(x):
    X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f=np.fft.rfftfreq(len(x),1/10000); t=X.sum()
    return [10*np.log10(X[(f>=a)&(f<b)].sum()/t) for a,b in ((0,300),(300,1000),(1000,2500),(2500,3500),(3500,5000))]
kinds=sys.argv[2].split(',')
print("%-12s %-16s %7s  %s"%("item","variant","rms dB","band share dB: <300 300-1k 1-2.5k 2.5-3.5k 3.5-5k"))
for n in ('hello','long_clause','baseline','breathy','long_words','prose'):
    for k in kinds:
        x=load(os.path.join(L,'%s_%s.wav'%(n,k))); r=20*np.log10(np.sqrt((x*x).mean()))
        print("%-12s %-16s %7.1f  %s"%(n,k,r,' '.join('%6.1f'%b for b in bands(x))))
