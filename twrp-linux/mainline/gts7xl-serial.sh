#!/system/bin/sh
# The forced kernel command line drops the bootloader's serial number; it is still
# in the device tree's bootargs. Set it before USB and adbd use ro.serialno.
for a in $(tr '\0' ' ' < /proc/device-tree/chosen/bootargs); do
	case "$a" in androidboot.serialno=?*)
		setprop ro.boot.serialno "${a#*=}"
		setprop ro.serialno "${a#*=}";;
	esac
done
