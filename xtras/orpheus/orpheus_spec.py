import os
import sys, wave, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
names=sys.argv[2:]; fig,ax=plt.subplots(len(names),2,figsize=(14,3*len(names)),squeeze=False)
for r,n in enumerate(names):
    for c,kind in enumerate(('ours','orpheus')):
        w=wave.open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'listen','%s_%s.wav')%(n,kind))
        x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)
        ax[r][c].specgram(x,NFFT=256,Fs=10000,noverlap=200,cmap='magma',vmin=-20)
        ax[r][c].set_title('%s - %s'%(n,kind))
        ax[r][c].set_ylim(0,5000)
plt.tight_layout(); plt.savefig(sys.argv[1],dpi=70)
