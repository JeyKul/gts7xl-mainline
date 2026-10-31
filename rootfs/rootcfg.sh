export TMPDIR=/tmp
T=$(readlink -f /dev/block/by-name/userdata)
for m in /sdcard /data /storage/emulated; do umount -l $m 2>/dev/null; done
mkdir -p /tmp/r; mount -t ext4 $T /tmp/r || { echo MOUNT_FAIL; exit 1; }
mount --bind /dev /tmp/r/dev; mount -t proc proc /tmp/r/proc; mount -t sysfs sys /tmp/r/sys
R=/tmp/r
cat > $R/etc/inittab <<'I'
::sysinit:/sbin/openrc sysinit
::sysinit:/sbin/openrc boot
::wait:/sbin/openrc default
tty1::respawn:/sbin/getty -n -l /usr/local/bin/autologin 38400 tty1
tty2::respawn:/sbin/getty 38400 tty2
ttyGS0::respawn:/sbin/getty -n -l /bin/sh 115200 ttyGS0
::ctrlaltdel:/sbin/reboot
::shutdown:/sbin/openrc shutdown
I
mkdir -p $R/usr/local/bin
printf '#!/bin/sh\nexec /bin/login -f user\n' > $R/usr/local/bin/autologin; chmod 755 $R/usr/local/bin/autologin
echo gts7xl > $R/etc/hostname
echo '/dev/sda36 / ext4 rw,noatime 0 1' > $R/etc/fstab
printf 'https://dl-cdn.alpinelinux.org/alpine/v3.22/main\nhttps://dl-cdn.alpinelinux.org/alpine/v3.22/community\n' > $R/etc/apk/repositories
cat > $R/tmp.sh <<'C'
#!/bin/sh
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
addgroup -S seat 2>/dev/null
adduser -D -s /bin/sh user 2>/dev/null
for g in video input seat tty audio; do addgroup user $g 2>/dev/null; done
passwd -d user >/dev/null 2>&1; passwd -d root >/dev/null 2>&1
for s in devfs dmesg udev udev-trigger; do rc-update add $s sysinit; done
for s in hostname bootmisc sysctl modules; do rc-update add $s boot; done
for s in udev-settle seatd; do rc-update add $s default; done
for s in mount-ro killprocs; do rc-update add $s shutdown; done
id user
C
chmod +x $R/tmp.sh
for f in /proc/mounts; do :; done
chroot $R /tmp.sh 2>&1 | tail -6; rm -f $R/tmp.sh
U=$R/home/user; mkdir -p $U/.config/sway
cat > $U/.profile <<'P'
export XDG_RUNTIME_DIR=/tmp/run-user
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
if [ "$(tty)" = /dev/tty1 ]; then
	export WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 LIBSEAT_BACKEND=seatd
	exec sway > "$HOME/sway.log" 2>&1
fi
P
cat > $U/.config/sway/config <<'P'
include /etc/sway/config
output * scale 2
xwayland disable
exec foot
P
chroot $R /bin/chown -R user:user /home/user
chroot $R /bin/sh -c "ls /etc/runlevels/default /etc/runlevels/sysinit | tr \"\\n\" \" \""; cat $R/etc/inittab | grep -c ttyGS0
ls -la $R/etc/runlevels/default | tail -3 | cut -c30-90; ls $R/etc/sway/config
umount $R/dev $R/proc $R/sys; sync; umount $R && echo unmounted
