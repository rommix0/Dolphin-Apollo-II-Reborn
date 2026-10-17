import sys, wave, numpy as np, os, glob
L=sys.argv[1]; T=10**(66/20); M=os.path.join(L,'matched'); os.makedirs(M,exist_ok=True)
for p in sorted(glob.glob(os.path.join(L,'*.wav'))):
    w=wave.open(p); x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)
    g=T/np.sqrt((x*x).mean()); y=x*g; pk=np.abs(y).max()
    if pk>32000: y*=32000/pk
    o=wave.open(os.path.join(M,os.path.basename(p)),'wb'); o.setnchannels(1); o.setsampwidth(2); o.setframerate(10000)
    o.writeframes(np.round(y).astype('<i2').tobytes()); o.close()
    print("%-34s gain %+5.1f dB%s"%(os.path.basename(p),20*np.log10(g)," (peak-limited)" if pk>32000 else ""))
