#!/bin/sh
# Install Arch onto userdata. Runs in RAM. The UI passes the choices in /tmp/choices.
# Without /dry it formats userdata, which erases everything on it.
set -eu
. /tmp/choices
# Big downloads over Wi-Fi do drop: carry on from where they stopped
get() { curl -fsSL --retry 30 --retry-delay 3 --retry-all-errors --connect-timeout 20 -C - -o "$1" "$2"; }
step() { echo "$1" > /tmp/step; echo "installer: $1" > /dev/kmsg; }
pct() { echo "$1" > /tmp/percent; }

# Show a file growing towards an expected size as part of overall progress
watch_size() { # file expected_bytes from to
	while :; do
		h=$(stat -c %s "$1" 2>/dev/null || echo 0)
		[ "$2" -gt 0 ] && pct $(( $3 + ($4 - $3) * (h / 1048576) / ($2 / 1048576 + 1) ))
		sleep 1
	done
}
fail() { trap - EXIT; echo "FAILED: $1" > /tmp/step; echo "installer failed: $1" > /dev/kmsg; kill $(jobs -p) 2>/dev/null; exit 1; }
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
	# TWRP's older kernel can't write to orphan_file, which recovery fixes would need
	mkfs.ext4 -F -q -L userdata -O ^orphan_file "$D"
	mount -t ext4 -o rw,noatime "$D" $A
fi

U=http://os.archlinuxarm.org/os
iw dev wlan0 set power_save off 2>/dev/null || true
pct 2
step "Downloading Arch Linux ARM"
cd /tmp
rm -f arch.tar.gz.md5
get arch.tar.gz.md5 $U/ArchLinuxARM-aarch64-latest.tar.gz.md5
LEN=$(curl -fsSLI $U/ArchLinuxARM-aarch64-latest.tar.gz | tr -d '\r' | awk 'tolower($1)=="content-length:" {v=$2} END {print v+0}')
watch_size ArchLinuxARM-aarch64-latest.tar.gz "$LEN" 2 30 &
W=$!
get ArchLinuxARM-aarch64-latest.tar.gz $U/ArchLinuxARM-aarch64-latest.tar.gz || { kill $W 2>/dev/null; fail "download failed, check Wi-Fi and reboot to retry"; }
kill $W 2>/dev/null || true
pct 30
step "Checking the download"
md5sum -c arch.tar.gz.md5 >/dev/null || fail "download is damaged"
pct 31
step "Unpacking Arch Linux"
bsdtar -xpf ArchLinuxARM-aarch64-latest.tar.gz -C $A
rm -f ArchLinuxARM-aarch64-latest.tar.gz

pct 42
step "Adding tablet support"
# The project's firmware archive, while there is a network. First boot falls back to
# the tablet's own partitions for anything that isn't here.
FW=https://github.com/JeyKul/gts7xl-mainline/releases/download/Installer/tabs7p.tar
FWSUM=a23600ef20e314ecceef0bc605bffedcea07658bb09f6044f0b6a3c8e3047267
rm -f /tmp/fw.tar
if get /tmp/fw.tar "$FW" && [ "$(sha256sum /tmp/fw.tar | cut -d' ' -f1)" = "$FWSUM" ]; then
	mkdir -p /tmp/fw $A/usr/lib/firmware/qcom/sm8250/gts7xl $A/usr/lib/firmware/cirrus
	tar -xf /tmp/fw.tar -C /tmp/fw
	for f in /tmp/fw/files/image/adsp.*; do
		b=${f##*/}; [ "$b" = adsp.mdt ] && b=adsp.mbn
		cp "$f" "$A/usr/lib/firmware/qcom/sm8250/gts7xl/$b"
	done
	for f in /tmp/fw/files/image/a650_zap.*; do
		b=${f##*/}; [ "$b" = a650_zap.mdt ] && b=a650_zap.mbn
		cp "$f" "$A/usr/lib/firmware/qcom/sm8250/$b"
	done
	cp /tmp/fw/files/firmware/cs35l40-spk-dsp1-spk-prot.wmfw $A/usr/lib/firmware/cirrus/cs35l41-dsp1-spk-prot.wmfw
	cp /tmp/fw/files/firmware/cs35l40-spk-dsp1-spk-prot.bin $A/usr/lib/firmware/cirrus/cs35l41-dsp1-spk-prot.bin
	rm -rf /tmp/fw /tmp/fw.tar
else
	echo "firmware archive unavailable, first boot uses the tablet's partitions"
	rm -f /tmp/fw.tar
fi
cp -a /gts7xl/overlay/. $A/
# A copy of the overlay root also sets the owner of / and system folders
(cd /gts7xl/overlay && find . -print0) | (cd $A && xargs -0 chown -h 0:0)
mkdir -p $A/usr/lib/firmware
cp -a /gts7xl/firmware/. $A/usr/lib/firmware/
chmod 755 $A/usr/local/sbin/* $A/usr/local/bin/* $A/usr/lib/systemd/system-sleep/gts7xl-bluetooth
tar -xzf /gts7xl/modules.tar.gz -C $A/usr
sed -i 's/^CheckSpace/#CheckSpace/' $A/etc/pacman.conf
sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 12/' $A/etc/pacman.conf
grep -q '^DisableSandbox' $A/etc/pacman.conf || sed -i '/^\[options\]/a DisableSandbox' $A/etc/pacman.conf
sed -i 's|^#\?NoExtract *=.*|NoExtract = usr/lib/firmware/cirrus/cs35l41-dsp1-spk-prot.*|' $A/etc/pacman.conf
rm -f $A/etc/resolv.conf; cp /etc/resolv.conf $A/etc/resolv.conf
for d in proc sys dev; do mount --bind /$d $A/$d; done
cp /gts7xl/arch-setup.sh $A/tmp/arch-setup.sh
umask 077
grep -E '^(DESKTOP|NEW_USER|NEW_PASS)=' /tmp/choices > $A/tmp/gts7xl-user
umask 022
sed -i '/^NEW_PASS/d' /tmp/choices

step "Downloading and installing $DESKTOP"
pct 45
# Packages run from 45% to 97%: downloading is measured by the cache, installing by pacman's count
(
	while :; do
		L=$(tr '\r' '\n' < /tmp/install.log 2>/dev/null | grep -E '^\( *[0-9]+/[0-9]+\) installing' | tail -1 | sed -E 's/^\( *([0-9]+)\/([0-9]+)\).*/\1 \2/')
		if [ -n "$L" ]; then
			set -- $L
			pct $(( 75 + 22 * $1 / $2 ))
			echo "Installing packages: $1 of $2" > /tmp/step
		else
			T=$(tr '\r' '\n' < /tmp/install.log 2>/dev/null | sed -nE 's/^Total Download Size: *([0-9]+).*/\1/p' | tail -1)
			H=$(du -sm $A/var/cache/pacman/pkg 2>/dev/null | cut -f1)
			if [ -n "$T" ] && [ "$T" -gt 0 ]; then
				pct $(( 45 + 30 * ${H:-0} / T ))
				echo "Downloading packages: ${H:-0} of $T MB" > /tmp/step
			fi
		fi
		sleep 1
	done
) &
W=$!
chroot $A /bin/bash /tmp/arch-setup.sh >> /tmp/install.log 2>&1 || { kill $W 2>/dev/null; fail "package setup failed"; }
kill $W 2>/dev/null || true
pct 98
rm -f $A/tmp/arch-setup.sh $A/var/cache/pacman/pkg/* 2>/dev/null || true
rm -f $A/etc/resolv.conf
ln -s ../run/systemd/resolve/stub-resolv.conf $A/etc/resolv.conf

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
pct 100
step "Done"
