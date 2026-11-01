#!/bin/bash
# Build the Arch Linux ARM root filesystem for the Tab S7+ Wi-Fi.
# Run as root on aarch64, or on x86_64 with qemu-user-static binfmt.
# Usage: arch-install.sh <ArchLinuxARM-aarch64-latest.tar.gz> <modules.tar.gz> <output.tar.gz>
set -euo pipefail

TARBALL=$(realpath "$1"); MODULES=$(realpath "$2"); OUT=$(realpath -m "$3")
HERE=$(dirname "$(realpath "$0")")
A=$(mktemp -d /var/tmp/gts7xl-rootfs.XXXXXX)
trap 'umount -R "$A/proc" "$A/sys" "$A/dev" 2>/dev/null || true; rm -rf --one-file-system "$A"' EXIT

bsdtar -xpf "$TARBALL" -C "$A"
mount -t proc proc "$A/proc"
mount --rbind /sys "$A/sys"
mount --rbind /dev "$A/dev"
rm -f "$A/etc/resolv.conf"; cp -L /etc/resolv.conf "$A/etc/resolv.conf"

sed -i 's/^CheckSpace/#CheckSpace/' "$A/etc/pacman.conf"
grep -q '^DisableSandbox' "$A/etc/pacman.conf" || sed -i '/^\[options\]/a DisableSandbox' "$A/etc/pacman.conf"
# linux-firmware ships a generic amp firmware at the same path as the tablet's own
sed -i 's|^#\?NoExtract *=.*|NoExtract = usr/lib/firmware/cirrus/cs35l41-dsp1-spk-prot.*|' "$A/etc/pacman.conf"

# The tablet's config must exist before services are enabled
cp -a "$HERE/overlay/." "$A/"
# GPU firmware from linux-firmware: Arch's packages don't provide these two here
LFW=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom
mkdir -p "$A/usr/lib/firmware/qcom"
for f in a650_sqe.fw a650_gmu.bin; do curl -fsSL -o "$A/usr/lib/firmware/qcom/$f" "$LFW/$f"; done
chmod 755 "$A"/usr/local/sbin/* "$A/usr/lib/systemd/system-sleep/gts7xl-bluetooth"
mkdir -p "$A/usr/lib/modules"
tar -xzf "$MODULES" -C "$A/usr" --strip-components=0 lib/modules 2>/dev/null || tar -xzf "$MODULES" -C "$A"
install -m755 "$HERE/arch-setup.sh" "$A/tmp/arch-setup.sh"
printf "NEW_USER=user\nNEW_PASS=user\n" > "$A/tmp/gts7xl-user"
chroot "$A" /bin/bash /tmp/arch-setup.sh
chroot "$A" /usr/bin/gpgconf --homedir /etc/pacman.d/gnupg --kill all 2>/dev/null || true
rm -f "$A/tmp/arch-setup.sh" "$A/etc/resolv.conf"
rm -rf "$A"/var/cache/pacman/pkg/* "$A"/etc/ssh/ssh_host_*

umount -R "$A/proc" "$A/sys" "$A/dev"
tar --numeric-owner --xattrs --acls -C "$A" -czf "$OUT" .
echo "Wrote $OUT"
