import os,sys,hashlib
x,r=sys.argv[1:]
mine={f[:-4]:os.path.join(x,f) for f in os.listdir(x) if f.endswith('.efi')}
theirs={}
for d,_,fs in os.walk(r):
    for f in fs:
        if f.endswith('.efi'): theirs[f[:-4]]=os.path.join(d,f)
h=lambda p:hashlib.sha256(open(p,'rb').read()).hexdigest()
same=[];diff=[]
for k in sorted(set(mine)&set(theirs)):
    (same if h(mine[k])==h(theirs[k]) else diff).append(k)
print('tablet',len(mine),'s20fe',len(theirs),'identical',len(same))
print('differ',len(diff)); print(' ',' '.join(f"{k}({os.path.getsize(mine[k])//1024}/{os.path.getsize(theirs[k])//1024}K)" for k in diff))
print('only s20fe',sorted(set(theirs)-set(mine)))
print('only tablet',sorted(set(mine)-set(theirs)))
