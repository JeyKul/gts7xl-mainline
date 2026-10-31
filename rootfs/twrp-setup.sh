set -u
T=$(readlink -f /dev/block/by-name/userdata); echo "userdata=$T $(blockdev --getsize64 $T)"
grep PARTNAME /sys/class/block/$(basename $T)/uevent
for m in /sdcard /data /storage/emulated; do umount -l $m 2>/dev/null; done
grep -E "$(basename $T)" /proc/mounts | cut -c1-60
which mke2fs mkfs.ext4 make_ext4fs 2>/dev/null
(mke2fs -F -t ext4 -b 4096 -L gts7root -m 1 $T 2>&1 || mkfs.ext4 -F -b 4096 -L gts7root -m 1 $T 2>&1) | tail -2
mkdir -p /tmp/r; mount -t ext4 $T /tmp/r || { echo MOUNT_FAIL; exit 1; }
df -h /tmp/r | tail -1
tar xf /tmp/pkgs.tar -C /tmp/inst || { echo TAR_FAIL; exit 1; }
for d in dev proc sys; do mkdir -p /tmp/r/$d; done; mount --bind /dev /tmp/r/dev; mount -t proc proc /tmp/r/proc; mount -t sysfs sys /tmp/r/sys
mkdir -p /tmp/r/etc/apk; cp -r /tmp/inst/keys /tmp/r/etc/apk/; 
/tmp/inst/apk.static --root /tmp/r --initdb --keys-dir /tmp/inst/keys --no-network --no-progress add /tmp/inst/pkgs/*.apk 2>&1 | tail -6
echo "apk rc=$?"; ls /tmp/r/sbin/init /tmp/r/usr/bin/sway /tmp/r/usr/bin/foot 2>&1 | cut -c1-60; du -sh /tmp/r 2>/dev/null | cut -f1
