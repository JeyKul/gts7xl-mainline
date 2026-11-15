#!/bin/bash
# Flash super (data part, verified chunks) and optionally boot. Only in TWRP.
set -u
adb devices | grep -q "recovery$" || { echo "NOT IN TWRP - nothing flashed"; exit 1; }
O=/mnt/builddrive/aosp17/out; P=$O/target/product/gts7xlwifi; C=/mnt/builddrive/superchunks
$O/host/linux-x86/bin/simg2img $P/super.img /mnt/builddrive/super.raw
rm -rf $C; mkdir -p $C
for i in 0 1 2 3; do dd if=/mnt/builddrive/super.raw of=$C/c$i bs=1M skip=$((i*256)) count=256 status=none; done
for i in 0 1 2 3; do
	adb push $C/c$i /tmp/c >/dev/null 2>&1
	l=$(sha256sum < $C/c$i | cut -c1-12)
	r=$(adb shell "dd if=/tmp/c of=/dev/block/by-name/super bs=1048576 seek=$((i*256)) conv=notrunc 2>/dev/null; sync; dd if=/dev/block/by-name/super bs=1048576 skip=$((i*256)) count=256 2>/dev/null | sha256sum; rm /tmp/c" | cut -c1-12)
	echo "super chunk $i: $([ "$l" = "$r" ] && echo OK || echo DIFF)"
done
if [ "${1:-}" = boot ]; then
	B=$P/boot.img; S=$(stat -c %s $B)
	adb push $B /tmp/aboot.img >/dev/null 2>&1
	r=$(adb shell "dd if=/dev/zero of=/dev/block/by-name/boot bs=1048576 count=68 2>/dev/null; dd if=/tmp/aboot.img of=/dev/block/by-name/boot bs=1048576 2>/dev/null; sync; head -c $S /dev/block/by-name/boot | sha256sum" | cut -c1-12)
	echo "boot: $([ "$r" = "$(sha256sum < $B | cut -c1-12)" ] && echo OK || echo DIFF)"
fi
adb shell "dd if=/dev/zero of=/dev/block/by-name/param bs=1 count=1 conv=notrunc 2>/dev/null; sync"
