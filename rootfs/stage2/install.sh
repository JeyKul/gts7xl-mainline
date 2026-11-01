#!/bin/sh
# Install Arch onto userdata. Runs in RAM. The UI passes the choices in /tmp/choices.
# Without /dry it formats userdata, which erases everything on it.
set -eu
. /tmp/choices
step() { echo "$1" > /tmp/step; echo "installer: $1" > /dev/kmsg; }
fail() { echo "FAILED: $1" > /tmp/step; echo "installer failed: $1" > /dev/kmsg; exit 1; }
trap 'fail "step did not finish, see /tmp/install.log"' EXIT

A=/target
mkdir -p $A
if [ -e /dry ]; then
	step "Test mode: installing to RAM, data is not touched"
	mount -t tmpfs -o size=6G tmpfs $A
else
	step "Formatting data"
	# Never format on a guess: the kernel must call this partition userdata
	D=
	for u in /sys/class/block/*/uevent; do
		if grep -qx 'PARTNAME=userdata' "$u"; then
			D=/dev/$(basename "$(dirname "$u")")
		fi
	done
	[ -n "$D" ] && [ -b "$D" ] || fail "userdata partition not found"
	mkfs.ext4 -F -q -L userdata "$D"
	mount -t ext4 -o rw,noatime "$D" $A
fi

U=http://os.archlinuxarm.org/os
step "Downloading Arch Linux ARM"
cd /tmp
curl -fsSL -o arch.tar.gz.md5 $U/ArchLinuxARM-aarch64-latest.tar.gz.md5
curl -fsSL -o ArchLinuxARM-aarch64-latest.tar.gz $U/ArchLinuxARM-aarch64-latest.tar.gz
step "Checking the download"
md5sum -c arch.tar.gz.md5 >/dev/null || fail "download is damaged"
step "Unpacking Arch Linux"
bsdtar -xpf ArchLinuxARM-aarch64-latest.tar.gz -C $A
rm -f ArchLinuxARM-aarch64-latest.tar.gz

step "Adding tablet support"
cp -a /gts7xl/overlay/. $A/
chmod 755 $A/usr/local/sbin/* $A/usr/lib/systemd/system-sleep/gts7xl-bluetooth
tar -xzf /gts7xl/modules.tar.gz -C $A/usr
sed -i 's/^CheckSpace/#CheckSpace/' $A/etc/pacman.conf
grep -q '^DisableSandbox' $A/etc/pacman.conf || sed -i '/^\[options\]/a DisableSandbox' $A/etc/pacman.conf
sed -i 's|^#\?NoExtract *=.*|NoExtract = usr/lib/firmware/cirrus/cs35l41-dsp1-spk-prot.*|' $A/etc/pacman.conf
rm -f $A/etc/resolv.conf; cp /etc/resolv.conf $A/etc/resolv.conf
for d in proc sys dev; do mount --bind /$d $A/$d; done
cp /gts7xl/arch-setup.sh $A/tmp/arch-setup.sh
umask 077
grep -E '^NEW_(USER|PASS)=' /tmp/choices > $A/tmp/gts7xl-user
umask 022
sed -i '/^NEW_PASS/d' /tmp/choices

step "Installing packages, this takes a while"
chroot $A /bin/bash /tmp/arch-setup.sh >> /tmp/install.log 2>&1 || fail "package setup failed"
rm -f $A/tmp/arch-setup.sh $A/etc/resolv.conf $A/var/cache/pacman/pkg/* 2>/dev/null || true

# Arch key setup leaves gpg-agent running, which keeps /dev busy
chroot $A /usr/bin/gpgconf --homedir /etc/pacman.d/gnupg --kill all 2>/dev/null || true
fuser -k $A/dev 2>/dev/null || true
sleep 1
for d in dev sys proc; do umount $A/$d || umount -l $A/$d; done
sync
[ -e /dry ] && { du -sh $A > /tmp/dry-size; umount $A; }
[ -e /dry ] || umount $A
trap - EXIT
echo done > /tmp/step-done
step "Done"
