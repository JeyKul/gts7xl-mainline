#!/system/bin/sh
# Mainline kernel only, at post-fs.
# GPU and Wi-Fi firmware: microcode and ath11k from the ramdisk, the zap shader from the tablet's own
# apnhlos (mounted by TWRP's rc), under one firmware path.
F=/tmp/gts7xl-fw/qcom
mkdir -p $F/sm8250
cp -r /linux-tools/firmware/* /tmp/gts7xl-fw/
for f in /vendor/firmware_mnt/image/a650_zap.*; do
	[ -f "$f" ] || continue
	b=${f##*/}
	[ "$b" = a650_zap.mdt ] && b=a650_zap.mbn
	cp "$f" $F/sm8250/$b
done
# ath11k probed at boot, before its firmware was here: probe it again
for d in /sys/bus/pci/devices/*; do
	[ "$(cat $d/vendor):$(cat $d/device)" = 0x17cb:0x1101 ] || continue
	[ -e $d/driver ] || echo ${d##*/} > /sys/bus/pci/drivers/ath11k_pci/bind
done
# TWRP shows thermal_zone17 as the CPU temperature (Samsung's numbering); on
# mainline that zone is Wi-Fi. Put the prime core's sensor in its place.
for z in /sys/class/thermal/thermal_zone*; do
	[ "$(cat $z/type)" = cpu7-top-thermal ] || continue
	mount --bind $z/temp /sys/class/thermal/thermal_zone17/temp
done
