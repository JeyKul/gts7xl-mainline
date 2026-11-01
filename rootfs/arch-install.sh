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

cat > "$A/tmp/in.sh" <<'C'
set -euo pipefail
pacman-key --init
pacman-key --populate archlinuxarm
pacman -Rdd --noconfirm linux-aarch64 || true
pacman -Syu --noconfirm --needed --overwrite '/etc/dconf/*' \
  gdm gnome-shell gnome-session gnome-settings-daemon gnome-control-center gnome-console \
  gnome-text-editor gnome-system-monitor nautilus gnome-keyring gvfs xdg-desktop-portal-gnome \
  adwaita-fonts networkmanager pulseaudio pulseaudio-alsa pulseaudio-bluetooth bluez bluez-utils \
  alsa-utils mesa swh-plugins sudo psmisc python
userdel -r alarm 2>/dev/null || true
useradd -m -G wheel,video,input,audio,tty,network -s /bin/bash user
echo 'user:user' | chpasswd
passwd -l root
echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel
echo gts7xl > /etc/hostname
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen; locale-gen; echo LANG=en_US.UTF-8 > /etc/locale.conf
systemctl disable systemd-networkd systemd-resolved sshd 2>/dev/null || true
systemctl mask alsa-restore alsa-state
systemctl enable NetworkManager gdm bluetooth systemd-timesyncd \
  gts7xl-firstboot gts7xl-hw gts7xl-bt-addr gts7xl-charge-control gts7xl-charge-log gts7xl-early-log
systemctl set-default graphical.target
sed -i 's/^load-module module-suspend-on-idle/#&/' /etc/pulse/default.pa
sed -i 's/^#\?AutoEnable=.*/AutoEnable=true/' /etc/bluetooth/main.conf
mkdir -p /etc/udev/rules.d; ln -sf /dev/null /etc/udev/rules.d/90-alsa-restore.rules
cp -a /etc/skel/.local /home/user/
chown -R user:user /home/user
dconf update
depmod -a 7.3.0-gts7xl
C

# The tablet's config must exist before services are enabled
cp -a "$HERE/overlay/." "$A/"
chmod 755 "$A"/usr/local/sbin/* "$A/usr/lib/systemd/system-sleep/gts7xl-bluetooth"
mkdir -p "$A/usr/lib/modules"
tar -xzf "$MODULES" -C "$A/usr" --strip-components=0 lib/modules 2>/dev/null || tar -xzf "$MODULES" -C "$A"
chroot "$A" /bin/bash /tmp/in.sh
rm -f "$A/tmp/in.sh" "$A/etc/resolv.conf"
rm -rf "$A"/var/cache/pacman/pkg/* "$A"/etc/ssh/ssh_host_*

umount -R "$A/proc" "$A/sys" "$A/dev"
tar --numeric-owner --xattrs --acls -C "$A" -czf "$OUT" .
echo "Wrote $OUT"
