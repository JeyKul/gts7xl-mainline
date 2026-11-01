#!/bin/bash
# Configure Arch inside its own chroot. Run by arch-install.sh and the installer.
# Reads NEW_USER and NEW_PASS from /tmp/gts7xl-user, which is deleted straight away.
set -euo pipefail
. /tmp/gts7xl-user
rm -f /tmp/gts7xl-user
case "$NEW_USER" in
	[a-z_]*) ;;
	*) echo "bad user name"; exit 1 ;;
esac
pacman-key --init
pacman-key --populate archlinuxarm
pacman -Rdd --noconfirm linux-aarch64 || true
pacman -Syu --noconfirm --needed --overwrite '/etc/dconf/*' \
  gdm gnome-shell gnome-session gnome-settings-daemon gnome-control-center gnome-console \
  gnome-text-editor gnome-system-monitor nautilus gnome-keyring gvfs xdg-desktop-portal-gnome \
  adwaita-fonts networkmanager pulseaudio pulseaudio-alsa pulseaudio-bluetooth bluez bluez-utils \
  alsa-utils mesa swh-plugins sudo psmisc python linux-firmware
userdel -r alarm 2>/dev/null || true
useradd -m -G wheel,video,input,audio,tty,network -s /bin/bash "$NEW_USER"
printf '%s:%s\n' "$NEW_USER" "$NEW_PASS" | chpasswd
unset NEW_PASS
passwd -l root
echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel
sed -i "s/^AutomaticLogin=.*/AutomaticLogin=$NEW_USER/" /etc/gdm/custom.conf
sed -i "s/-u user /-u $NEW_USER /; s/su user /su $NEW_USER /g" /usr/local/sbin/audio-up
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
cp -a /etc/skel/.local "/home/$NEW_USER/"
chown -R "$NEW_USER:$NEW_USER" "/home/$NEW_USER"
dconf update
depmod -a 7.3.0-gts7xl
