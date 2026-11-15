#!/bin/bash
# Build a TWRP recovery image that can open the Linux install (LUKS2 + btrfs).
# Usage: [AVB_KEY=testkey_rsa4096.pem] [MAINLINE_DTB=board.dtb] build.sh <stock TWRP recovery.img> <Image.gz> <stage 1 Alpine root> <out.img>
# Keeps TWRP's ramdisk and appends a second cpio with cryptsetup, btrfs (Alpine,
# musl libraries included) and linux-data.
# Without MAINLINE_DTB: a 4.19 kernel with btrfs, TWRP's dtb and recovery dtbo kept.
# With MAINLINE_DTB: this project's mainline kernel (built with the recovery config,
# cmdline built in), packed like its boot image: v1 header, DTB appended, no dtbo.
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
if [ -n "${MAINLINE_DTB:-}" ]; then
	# The USB data switch, set the same way stage 1 does it
	install -m755 "${USBROUTE:-$HERE/../../initramfs/bin/usbroute}" "$O/system/bin/usbroute"
	# Recovery init only reads init.recovery.<hardware>.rc: TWRP's, plus an import of ours
	mkdir -p "$T/rd"
	(cd "$T/rd" && gzip -dc "$T/ramdisk" | cpio -id --quiet init.recovery.qcom.rc system/etc/recovery.fstab)
	# Mainline names the bus soc@0: TWRP's rc waited 5 s for platform/soc/ and then
	# failed to mount persist, efs and apnhlos
	{ sed 's|/dev/block/platform/soc/|/dev/block/platform/soc@0/|g' "$T/rd/init.recovery.qcom.rc"; echo; echo "import /init.recovery.gts7xl.rc"; } > "$O/init.recovery.qcom.rc"
	install -m644 "$HERE/init.recovery.gts7xl.rc" "$O/init.recovery.gts7xl.rc"
	# Android's FBE needs QSEECOM and keymaster, which mainline doesn't have: without
	# the flags TWRP stops waiting for a keymaster that never starts
	mkdir -p "$O/system/etc"
	sed -E '/ \/data /{s/,?(fileencryption|keydirectory|checkpoint)=[^, ]*//g; s/,inlinecrypt//g}' \
		"$T/rd/system/etc/recovery.fstab" > "$O/system/etc/recovery.fstab"
	install -m755 "$HERE/mainline/gts7xl-fw.sh" "$O/system/bin/gts7xl-fw.sh"
	install -m755 "$HERE/mainline/gts7xl-reboot.sh" "$O/system/bin/gts7xl-reboot.sh"
	"${CC:-aarch64-linux-gnu-gcc}" -static -Os -o "$O/system/bin/gts7xl-reboot" "$HERE/mainline/gts7xl-reboot.c"
	# GPU microcode, redistributable, from linux-firmware; the zap shader comes from apnhlos
	LFW=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom
	mkdir -p "$O/linux-tools/firmware/qcom"
	for f in a650_sqe.fw a650_gmu.bin; do curl -fsSL -o "$O/linux-tools/firmware/qcom/$f" "$LFW/$f"; done
fi
(cd "$O" && find . | sort | cpio -o -H newc -R 0:0 --quiet) | gzip -9 > "$T/extra"
# The kernel unpacks concatenated archives in order: TWRP's, then ours
cat "$T/ramdisk" "$T/extra" > "$T/ramdisk-all"
read -r OSV OSP < "$T/args"
OSV=$(printf '%d.%d.%d' $((OSV >> 14)) $(((OSV >> 7) & 0x7f)) $((OSV & 0x7f)))
OSP=$(printf '%04d-%02d' $(((OSP >> 4) + 2000)) $((OSP & 0xf)))
if [ -n "${MAINLINE_DTB:-}" ]; then
	# TWRP rotates touch itself (built for Samsung's driver, which reports the panel's
	# raw portrait axes): drop the swap/invert the Linux DTB adds for the desktop
	cp "$(realpath "$MAINLINE_DTB")" "$T/rec.dtb"
	TS=$(python3 -I - "$T/rec.dtb" <<'PY'
import subprocess,sys
d=sys.argv[1]
def walk(p):
    for k in subprocess.run(['fdtget','-l',d,p],capture_output=True,text=True).stdout.split():
        q=p.rstrip('/')+'/'+k
        if 'fts1ba90a' in subprocess.run(['fdtget','-t','s',d,q,'compatible'],capture_output=True,text=True).stdout: return q
        r=walk(q)
        if r: return r
print(walk('/') or '')
PY
)
	[ -n "$TS" ] || { echo "no touchscreen node in the DTB"; exit 1; }
	for p in touchscreen-swapped-x-y touchscreen-inverted-x touchscreen-inverted-y; do fdtput -d "$T/rec.dtb" "$TS" $p 2>/dev/null || true; done
	cat "$K" "$T/rec.dtb" > "$T/kernel-dtb"
	mkbootimg --header_version 1 --pagesize 2048 --base 0x10000000 --kernel_offset 0x00008000 \
		--ramdisk_offset 0x01000000 --tags_offset 0x00000100 \
		--os_version "$OSV" --os_patch_level "$OSP" \
		--kernel "$T/kernel-dtb" --ramdisk "$T/ramdisk-all" -o "$OUT"
else
mkbootimg --header_version 2 --pagesize 4096 --base 0x00000000 --kernel_offset 0x00008000 \
	--ramdisk_offset 0x02000000 --tags_offset 0x01e00000 --dtb_offset 0x01f00000 \
	--os_version "$OSV" --os_patch_level "$OSP" --cmdline "$(cat "$T/cmdline")" \
	--kernel "$K" --ramdisk "$T/ramdisk-all" --recovery_dtbo "$T/dtbo" --dtb "$T/dtb" -o "$OUT"
fi
# Same AVB footer as the input: TWRP builds sign with AOSP's public test key
# (testkey_rsa4096.pem from external/avb, pass it as AVB_KEY)
if avbtool info_image --image "$IN" >/dev/null 2>&1; then
	[ -n "${AVB_KEY:-}" ] || { echo "input has an AVB footer: set AVB_KEY"; exit 1; }
	PSIZE=$(avbtool info_image --image "$IN" | awk '/^Image size:/ {print $3}')
	avbtool add_hash_footer --image "$OUT" --partition_size "$PSIZE" --partition_name recovery \
		--algorithm SHA256_RSA4096 --key "$AVB_KEY"
fi
ls -l "$OUT"
