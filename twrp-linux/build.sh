#!/bin/bash
# Build a TWRP recovery image that can open the Linux install (LUKS2 + btrfs).
# Usage: [AVB_KEY=testkey_rsa4096.pem] build.sh <stock TWRP recovery.img> <Image.gz with BTRFS_FS=y> <stage 1 Alpine root> <out.img>
# Keeps TWRP's ramdisk, dtb and recovery dtbo; swaps the kernel and appends a
# second cpio with cryptsetup, btrfs (Alpine, musl libraries included) and linux-data.
set -euo pipefail
IN=$(realpath "$1"); K=$(realpath "$2"); R=$(realpath "$3"); OUT=$(realpath -m "$4")
HERE=$(dirname "$(realpath "$0")")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -I - "$IN" "$T" <<'P'
import struct,sys
d=open(sys.argv[1],'rb').read(); o=sys.argv[2]
assert d[:8]==b'ANDROID!' and struct.unpack_from('<I',d,40)[0]==2, 'need a v2 boot image'
pg=struct.unpack_from('<I',d,36)[0]; r=lambda n:(n+pg-1)//pg*pg
ks,rs=struct.unpack_from('<I',d,8)[0],struct.unpack_from('<I',d,16)[0]
dos=struct.unpack_from('<I',d,1632)[0]; dbs=struct.unpack_from('<I',d,1648)[0]
off=pg+r(ks); open(o+'/ramdisk','wb').write(d[off:off+rs]); off+=r(rs)
open(o+'/dtbo','wb').write(d[off:off+dos]); off+=r(dos)
open(o+'/dtb','wb').write(d[off:off+dbs])
os_version=struct.unpack_from('<I',d,44)[0]
cmd=d[64:64+512].split(b'\0')[0]+d[608:608+1024].split(b'\0')[0]
open(o+'/args','w').write('%d %d\n'%(os_version>>11, os_version&0x7ff))
open(o+'/cmdline','wb').write(cmd)
P
# Alpine tools with the libraries they need, resolved inside the Alpine root
O=$T/ov; mkdir -p "$O/linux-tools/bin" "$O/linux-tools/lib"
python3 -I - "$R" "$O/linux-tools" <<'P'
import subprocess,sys,os,shutil
R,T=sys.argv[1],sys.argv[2]
def res(p):
    for _ in range(20):
        f=os.path.join(R,p.lstrip('/'))
        if not os.path.islink(f): return f
        t=os.readlink(f); p=t if t.startswith('/') else os.path.join(os.path.dirname(p),t)
    raise SystemExit('symlink loop '+p)
def find(n):
    for d in ('/lib','/usr/lib'):
        if os.path.exists(res(d+'/'+n)): return res(d+'/'+n)
    raise SystemExit('missing '+n)
seen=set()
def deps(p):
    for l in subprocess.run(['readelf','-d',p],capture_output=True,text=True,check=True).stdout.splitlines():
        if '(NEEDED)' in l:
            n=l.split('[')[1].split(']')[0]
            if n not in seen and not n.startswith('libc.musl'): seen.add(n); deps(find(n))
for b in ('/sbin/cryptsetup','/sbin/btrfs'):
    shutil.copy(res(b),T+'/bin/'+os.path.basename(b)); deps(res(b))
for n in seen: shutil.copy(find(n),T+'/lib/'+n)
shutil.copy(res('/lib/ld-musl-aarch64.so.1'),T+'/lib/ld-musl-aarch64.so.1')
P
mkdir -p "$O/system/bin"
install -m755 "$HERE"/system/bin/* "$O/system/bin/"
(cd "$O" && find . | sort | cpio -o -H newc -R 0:0 --quiet) | gzip -9 > "$T/extra"
# The kernel unpacks concatenated archives in order: TWRP's, then ours
cat "$T/ramdisk" "$T/extra" > "$T/ramdisk-all"
read -r OSV OSP < "$T/args"
OSV=$(printf '%d.%d.%d' $((OSV >> 14)) $(((OSV >> 7) & 0x7f)) $((OSV & 0x7f)))
OSP=$(printf '%04d-%02d' $(((OSP >> 4) + 2000)) $((OSP & 0xf)))
mkbootimg --header_version 2 --pagesize 4096 --base 0x00000000 --kernel_offset 0x00008000 \
	--ramdisk_offset 0x02000000 --tags_offset 0x01e00000 --dtb_offset 0x01f00000 \
	--os_version "$OSV" --os_patch_level "$OSP" --cmdline "$(cat "$T/cmdline")" \
	--kernel "$K" --ramdisk "$T/ramdisk-all" --recovery_dtbo "$T/dtbo" --dtb "$T/dtb" -o "$OUT"
# Same AVB footer as the input: TWRP builds sign with AOSP's public test key
# (testkey_rsa4096.pem from external/avb, pass it as AVB_KEY)
if avbtool info_image --image "$IN" >/dev/null 2>&1; then
	[ -n "${AVB_KEY:-}" ] || { echo "input has an AVB footer: set AVB_KEY"; exit 1; }
	PSIZE=$(avbtool info_image --image "$IN" | awk '/^Image size:/ {print $3}')
	avbtool add_hash_footer --image "$OUT" --partition_size "$PSIZE" --partition_name recovery \
		--algorithm SHA256_RSA4096 --key "$AVB_KEY"
fi
ls -l "$OUT"
