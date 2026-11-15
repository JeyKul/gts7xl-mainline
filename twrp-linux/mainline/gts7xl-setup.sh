#!/system/bin/sh
# Mainline kernel only, at post-fs.
# GPU firmware: microcode from the ramdisk, the zap shader from the tablet's own
# apnhlos (mounted by TWRP's rc), under one firmware path.
F=/tmp/gts7xl-fw/qcom
mkdir -p $F/sm8250
cp /linux-tools/firmware/qcom/* $F/
for f in /vendor/firmware_mnt/image/a650_zap.*; do
	[ -f "$f" ] || continue
	b=${f##*/}
	[ "$b" = a650_zap.mdt ] && b=a650_zap.mbn
	cp "$f" $F/sm8250/$b
done
# TWRP shows thermal_zone17 as the CPU temperature (Samsung's numbering); on
# mainline that zone is Wi-Fi. Put the prime core's sensor in its place.
for z in /sys/class/thermal/thermal_zone*; do
	[ "$(cat $z/type)" = cpu7-top-thermal ] || continue
	mount --bind $z/temp /sys/class/thermal/thermal_zone17/temp
done
