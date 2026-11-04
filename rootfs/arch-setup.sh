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
DESKTOP=${DESKTOP:-gnome}
BASE="networkmanager pulseaudio pulseaudio-alsa pulseaudio-bluetooth bluez bluez-utils alsa-utils mesa swh-plugins sudo psmisc python python-dbus iio-sensor-proxy python-gobject gtk4 libcamera libcamera-tools gst-plugin-libcamera gst-plugin-gtk4 gst-plugins-base gst-plugins-good gst-python v4l-utils vulkan-icd-loader vulkan-freedreno vulkan-tools linux-firmware"
GNOME="gdm gnome-shell gnome-session gnome-settings-daemon gnome-control-center gnome-console gnome-text-editor gnome-system-monitor nautilus gnome-keyring gvfs xdg-desktop-portal-gnome adwaita-fonts"
case "$DESKTOP" in
gnome) PKGS="$GNOME"; DM=gdm ;;
kde) PKGS="plasma-desktop plasma-nm plasma-pa kscreen konsole dolphin sddm"; DM=sddm ;;
kde-mobile) PKGS="plasma-mobile plasma-keyboard plasma-nm plasma-pa konsole dolphin sddm"; DM=sddm ;;
phosh) PKGS="phosh phoc squeekboard gdm gnome-control-center gnome-console nautilus gnome-keyring adwaita-fonts"; DM=gdm ;;
*) echo "unknown desktop $DESKTOP"; exit 1 ;;
esac
pacman -Syu --noconfirm --needed --overwrite '/etc/dconf/*' $BASE $PKGS

# Optional kernel modules, see modules-repo/ in the project
grep -q '^\[gts7xl\]' /etc/pacman.conf || cat >> /etc/pacman.conf <<'CONF'

[gts7xl]
SigLevel = Optional TrustAll
Server = https://github.com/JeyKul/gts7xl-mainline/releases/download/modules
CONF
# Sensor DSP client, see sensord/ in the project. gcc only for the build.
if [ -d /tmp/gts7xl-sensord-src ]; then
	pacman -Q gcc >/dev/null 2>&1 && HAD_GCC=1 || HAD_GCC=
	pacman -S --noconfirm --needed gcc make
	make -C /tmp/gts7xl-sensord-src install install-autostart
	[ -n "$HAD_GCC" ] || pacman -Rns --noconfirm gcc
	rm -rf /tmp/gts7xl-sensord-src
fi
userdel -r alarm 2>/dev/null || true
useradd -m -G wheel,video,input,audio,tty,network -s /bin/bash "$NEW_USER"
printf '%s:%s\n' "$NEW_USER" "$NEW_PASS" | chpasswd
unset NEW_PASS
passwd -l root
echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel
sed -i "s/^AutomaticLogin=.*/AutomaticLogin=$NEW_USER/" /etc/gdm/custom.conf
case "$DESKTOP" in
kde|kde-mobile)
	mkdir -p /etc/sddm.conf.d
	cat /etc/environment.gts7xl >> /etc/environment
	if [ "$DESKTOP" = kde ]; then SESSION=plasma; else SESSION=plasma-mobile; fi
	printf '[Autologin]\nUser=%s\nSession=%s\n' "$NEW_USER" "$SESSION" > /etc/sddm.conf.d/autologin.conf
	;;
phosh)
	mkdir -p /var/lib/AccountsService/users
	printf '[User]\nSession=phosh\n' > "/var/lib/AccountsService/users/$NEW_USER"
	;;
esac
sed -i "s/-u user /-u $NEW_USER /; s/su user /su $NEW_USER /g" /usr/local/sbin/audio-up
echo gts7xl > /etc/hostname
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen; locale-gen; echo LANG=en_US.UTF-8 > /etc/locale.conf
systemctl disable systemd-networkd sshd 2>/dev/null || true
systemctl mask alsa-restore alsa-state
# /etc/resolv.conf points at systemd-resolved, and NetworkManager feeds it
systemctl enable systemd-resolved
systemctl enable NetworkManager "$DM" bluetooth systemd-timesyncd \
  gts7xl-firstboot gts7xl-hw gts7xl-bt-addr gts7xl-charge-control gts7xl-charge-log gts7xl-early-log gts7xl-spen gts7xl-webcam \
  $([ -e /etc/systemd/system/gts7xl-sensord-setup.service ] && echo gts7xl-sensord-setup)
systemctl set-default graphical.target
sed -i 's/^load-module module-suspend-on-idle/#&/' /etc/pulse/default.pa
sed -i 's/^#\?AutoEnable=.*/AutoEnable=true/' /etc/bluetooth/main.conf
mkdir -p /etc/udev/rules.d; ln -sf /dev/null /etc/udev/rules.d/90-alsa-restore.rules
cp -a /etc/skel/.local "/home/$NEW_USER/"
chown -R "$NEW_USER:$NEW_USER" "/home/$NEW_USER"
dconf update
depmod -a 7.3.0-gts7xl
