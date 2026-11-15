#!/system/bin/sh
# GPU firmware for the mainline kernel: microcode from the ramdisk, the zap shader
# from the tablet's own apnhlos (mounted by TWRP's rc), under one firmware path.
F=/tmp/gts7xl-fw/qcom
mkdir -p $F/sm8250
cp /linux-tools/firmware/qcom/* $F/
for f in /vendor/firmware_mnt/image/a650_zap.*; do
	[ -f "$f" ] || continue
	b=${f##*/}
	[ "$b" = a650_zap.mdt ] && b=a650_zap.mbn
	cp "$f" $F/sm8250/$b
done
