#!/bin/bash
# Flash only boot (the kernel) from running Android: boot isn't mounted, so
# dd with root is safe there. super (system/vendor) is in use: TWRP only.
set -eu
B=/mnt/builddrive/aosp17/out/target/product/gts7xlwifi/boot.img
S=$(stat -c %s "$B")
timeout 10 adb get-state | grep -q device || { echo "Android not on adb - nothing flashed"; exit 1; }
adb push "$B" /data/local/tmp/aboot.img >/dev/null
r=$(timeout 60 adb shell "su 0 sh -c 'dd if=/data/local/tmp/aboot.img of=/dev/block/by-name/boot bs=1048576 2>/dev/null; sync; head -c $S /dev/block/by-name/boot | sha256sum; rm /data/local/tmp/aboot.img'" | cut -c1-12)
if [ "$r" = "$(sha256sum < "$B" | cut -c1-12)" ]; then echo "boot: OK"; else echo "boot: DIFF - do not reboot, reflash"; exit 1; fi
[ "${1:-}" = noreboot ] || timeout 10 adb reboot
