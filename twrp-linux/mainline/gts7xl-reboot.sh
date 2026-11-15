#!/system/bin/sh
# Lock the Linux data if it is open, then reboot straight through the kernel
/system/bin/linux-data off >/dev/null 2>&1
exec /system/bin/gts7xl-reboot "$1"
