#!/usr/bin/env python3
# Build-independent: push a boot image (and optional extra file) to running Linux and flash it.
import os,sys,socket,subprocess,hashlib,time
W=os.path.dirname(os.path.abspath(__file__))
def ts(c,to=30): return subprocess.run(['python3','-I',W+'/ts.py',c,str(to)],capture_output=True,text=True,timeout=to+15).stdout
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
addr='172.16.42.1'
def send(path,remote):
    print(ts(f"killall nc 2>/dev/null; rm -f {remote}; cat > /tmp/rx.sh <<'R'\nexec busybox nc -l -p 9000 > {remote} </dev/null\nR\nsetsid sh /tmp/rx.sh >/dev/null 2>&1 </dev/null &\nsleep 2; pidof nc",6).strip()[-12:])
    s=socket.create_connection((addr,9000),timeout=15); s.settimeout(None)
    with open(path,'rb') as f:
        while (b:=f.read(1<<20)): s.sendall(b)
    s.close(); n=os.path.getsize(path)
    ts(f'i=0; while [ "$(stat -c %s {remote})" != {n} ] && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done; killall nc 2>/dev/null',70)
    if sha(path) not in ts(f'sha256sum {remote} | cut -d" " -f1',30): sys.exit('CHECKSUM MISMATCH '+path)
U=sys.argv[1]; send(U,'/tmp/u.img'); n=os.path.getsize(U)
r=ts(f'''[ "$(dd if=/dev/sda22 bs=8 count=1 2>/dev/null)" = "ANDROID!" ] || {{ echo NOT_BOOT; exit 1; }}
[ "$(blockdev --getsize64 /dev/sda22)" = 71303168 ] || {{ echo BAD_SIZE; exit 1; }}
dd if=/dev/zero of=/dev/sda22 bs=1048576 2>/dev/null; dd if=/tmp/u.img of=/dev/sda22 bs=4096 2>/dev/null; sync
dd if=/dev/sda22 bs=2048 count={n//2048} 2>/dev/null | sha256sum | cut -d" " -f1''',60)
if sha(U) not in r: sys.exit('BOOT READBACK MISMATCH '+r)
print('boot verified')
if len(sys.argv)>3:
    send(sys.argv[2],sys.argv[3]); print('extra ok')
