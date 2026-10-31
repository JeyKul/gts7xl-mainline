#!/usr/bin/env python3
# Update the tablet's UEFI and kernel from running Linux over USB.
import os,sys,socket,subprocess,hashlib,time,re
W=os.path.dirname(os.path.abspath(__file__)); H=os.path.expanduser('~/gts7xl-mainline')
def ts(c,to=30):
    return subprocess.run(['python3','-I',W+'/ts.py',c,str(to)],capture_output=True,text=True,timeout=to+15).stdout
def sha(p):
    return hashlib.sha256(open(p,'rb').read()).hexdigest()
addr='172.16.42.1'
def send(path,remote):
    ts(f'rm -f {remote}; killall nc 2>/dev/null; setsid sh -c "exec busybox nc -l -p 9000 > {remote} 2>/dev/null </dev/null" &\nsleep 2; pidof nc || ps | grep -c "nc -l"',8)
    s=socket.create_connection((addr,9000),timeout=15); t=time.time()
    with open(path,'rb') as f:
        while (b:=f.read(1<<20)): s.sendall(b)
    s.close(); print(os.path.basename(path),os.path.getsize(path),'in %.1fs'%(time.time()-t)); sz=os.path.getsize(path)
    print(ts(f'i=0; while [ "$(stat -c %s {remote})" != {sz} ] && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done; stat -c %s {remote}; killall nc 2>/dev/null',70).strip()[:40])
    r=ts(f'sha256sum {remote} | cut -d" " -f1',60)
    if sha(path) not in r: sys.exit('CHECKSUM MISMATCH for '+path+': '+r)
K=sys.argv[1]; U=sys.argv[2] if len(sys.argv)>2 else None
send(K,'/tmp/Image')
print(ts('''[ "$(blkid /dev/sda31 | grep -c GTS7ESP)" = 1 ] || { echo NO_ESP; exit 1; }
mkdir -p /mnt/e; mount -t vfat /dev/sda31 /mnt/e && cp /tmp/Image /mnt/e/EFI/BOOT/BOOTAA64.EFI && sync; sha256sum /mnt/e/EFI/BOOT/BOOTAA64.EFI | cut -c1-16; umount /mnt/e; echo esp done''',60))
if U:
    send(U,'/tmp/u.img'); n=os.path.getsize(U)
    r=ts(f'''[ "$(dd if=/dev/sda22 bs=8 count=1 2>/dev/null)" = "ANDROID!" ] || {{ echo NOT_BOOT; exit 1; }}
[ "$(blockdev --getsize64 /dev/sda22)" = 71303168 ] || {{ echo BAD_SIZE; exit 1; }}
dd if=/dev/zero of=/dev/sda22 bs=1048576 2>/dev/null; dd if=/tmp/u.img of=/dev/sda22 bs=4096 2>/dev/null; sync
dd if=/dev/sda22 bs=2048 count={n//2048} 2>/dev/null | sha256sum | cut -d" " -f1''',60)
    if sha(U) not in r: sys.exit('BOOT READBACK MISMATCH: '+r)
    print('boot verified')
print('sha kernel',sha(K)[:16])
