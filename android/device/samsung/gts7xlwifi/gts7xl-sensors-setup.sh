#!/vendor/bin/sh
# The sensor DSP (SLPI) asks the CPU for its registry and configs over a
# FastRPC tunnel: lay them out like stock Android has them, then serve them
# (gts7xl-sensord, see repo/sensord and docs/sensors.md)
R=/data/vendor/gts7xl-sensord
REG=$R/mnt/vendor/persist/sensors
mkdir -p $R/vendor/etc $REG/registry/registry $R/sys/devices/soc0 $R/sys/class/sensors/ssc_core
rm -rf $R/vendor/etc/sensors
cp -r /vendor/etc/sensors $R/vendor/etc/
# First boot: this tablet's registry from persist, mounted read only
# (noload: no journal replay). The DSP updates the copy, never persist.
if [ -z "$(ls $REG/registry/registry)" ]; then
	M=/mnt/vendor/persist-ro
	if mount -t ext4 -o ro,noload,nosuid,nodev /dev/block/by-name/persist $M; then
		[ -d $M/sensors ] && cp -r $M/sensors/. $REG/
		umount $M
		rm -f $REG/registry/sns_reg_version
	fi
fi
s() { echo "$2" > $R/sys/$1; }
s devices/soc0/soc_id "$(cat /sys/devices/soc0/soc_id 2>/dev/null || echo 356)"
s devices/soc0/revision "$(cat /sys/devices/soc0/revision 2>/dev/null || echo 2.1)"
# Values tested on an SM-T970 (board r06): the DSP picks the right configs
s devices/soc0/hw_platform MTP
s devices/soc0/platform_subtype strange
s devices/soc0/platform_subtype_id 2
s devices/soc0/platform_version 65536
s class/sensors/ssc_core/ssc_hw_rev 6
echo ro.revision=6 > $R/props
for i in $(seq 1 60); do [ -e /dev/fastrpc-sdsp ] && break; sleep 1; done
exec /vendor/bin/gts7xl-sensord --serve $R
