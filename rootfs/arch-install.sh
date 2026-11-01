#!/bin/sh
# Run on the tablet from Alpine. Builds Arch in /arch beside Alpine.
# Boot it with: touch /.arch  (remove /.arch from TWRP to return to Alpine)
set -e
T=${1:-/root/arch.tar.gz}; A=/arch
apk add -q libarchive-tools
mkdir -p $A
[ -e $A/usr/bin/pacman ] || bsdtar -xpf "$T" -C $A
mountpoint -q $A/proc || mount -t proc proc $A/proc
mountpoint -q $A/sys || mount --rbind /sys $A/sys
mountpoint -q $A/dev || mount --rbind /dev $A/dev
rm -f $A/etc/resolv.conf; cp /etc/resolv.conf $A/etc/resolv.conf
sed -i 's/^CheckSpace/#CheckSpace/' $A/etc/pacman.conf
grep -q '^DisableSandbox' $A/etc/pacman.conf || sed -i '/^\[options\]/a DisableSandbox' $A/etc/pacman.conf
cat > $A/tmp/in.sh <<'C'
set -e
pacman-key --init
pacman-key --populate archlinuxarm
pacman -Rdd --noconfirm linux-aarch64 || true
pacman -Syu --noconfirm --needed --overwrite '/etc/dconf/*' gdm gnome-shell gnome-session gnome-settings-daemon gnome-control-center gnome-console gnome-text-editor gnome-system-monitor nautilus gnome-keyring gvfs xdg-desktop-portal-gnome adwaita-fonts networkmanager pulseaudio pulseaudio-alsa alsa-utils vlc krita mesa swh-plugins sudo python-pyqt5 psmisc
userdel -r alarm 2>/dev/null || true
id user 2>/dev/null || useradd -m -G wheel,video,input,audio,tty,network -s /bin/bash user
passwd -d user; passwd -d root
echo '%wheel ALL=(ALL:ALL) NOPASSWD: ALL' > /etc/sudoers.d/wheel
echo gts7xl > /etc/hostname
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen; locale-gen; echo LANG=en_US.UTF-8 > /etc/locale.conf
systemctl disable systemd-networkd systemd-resolved sshd 2>/dev/null || true
systemctl enable NetworkManager gdm systemd-timesyncd gts7xl-hw gts7xl-serial
systemctl set-default graphical.target
systemctl mask alsa-restore alsa-state
systemctl mask suspend.target sleep.target hibernate.target hybrid-sleep.target suspend-then-hibernate.target
ln -sf /dev/null /etc/udev/rules.d/90-alsa-restore.rules
dconf update
C
# Carry over the hardware setup
cp -a /lib/modules/7.3.0-gts7xl $A/usr/lib/modules/
cp -a /lib/firmware/. $A/usr/lib/firmware/
mkdir -p $A/usr/local/sbin $A/usr/local/bin $A/etc/modprobe.d $A/etc/udev/rules.d
cp -a /usr/local/sbin/. $A/usr/local/sbin/
cp -a /usr/local/bin/. $A/usr/local/bin/
cp -a /etc/modprobe.d/. $A/etc/modprobe.d/
cp -a /etc/udev/rules.d/. $A/etc/udev/rules.d/
mkdir -p $A/etc/NetworkManager/system-connections $A/etc/NetworkManager/conf.d
cp -a /etc/NetworkManager/system-connections/. $A/etc/NetworkManager/system-connections/
sed -i '/^interface-name=/d' $A/etc/NetworkManager/system-connections/*
printf '[keyfile]\nunmanaged-devices=interface-name:usb0\n' > $A/etc/NetworkManager/conf.d/usb0.conf
printf 'SUBSYSTEM=="sound", KERNEL=="card0", ENV{PULSE_IGNORE}="1"\n' > $A/etc/udev/rules.d/89-gts7xl-pulse-ignore.rules
mkdir -p $A/etc/gdm $A/etc/dconf/db/local.d $A/etc/dconf/profile
cp -a /etc/gdm/custom.conf $A/etc/gdm/custom.conf
cp -a /etc/dconf/db/local.d/. $A/etc/dconf/db/local.d/
cp -a /etc/dconf/profile/. $A/etc/dconf/profile/
mkdir -p $A/etc/systemd/logind.conf.d
printf '[Login]\nHandlePowerKey=ignore\n' > $A/etc/systemd/logind.conf.d/power.conf
# Confirmed good sound: stock gain with the limiter in front
install -m755 "$(dirname "$0")/audio-up.arch" $A/usr/local/sbin/audio-up
sed -e '/audio-up/d' /etc/local.d/gpu.start > $A/usr/local/sbin/gts7xl-hw
chmod 755 $A/usr/local/sbin/gts7xl-hw
cat > $A/etc/systemd/system/gts7xl-hw.service <<'U'
[Unit]
Description=Tablet GPU and Wi-Fi modules
DefaultDependencies=no
After=systemd-udevd.service
Before=display-manager.service NetworkManager.service
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/gts7xl-hw
ExecStartPost=/bin/sh -c '/usr/local/sbin/audio-up >/var/log/audio-up.log 2>&1 &'
[Install]
WantedBy=sysinit.target
U
cat > $A/etc/systemd/system/gts7xl-serial.service <<'U'
[Unit]
Description=USB serial shell
DefaultDependencies=no
[Service]
ExecStart=/usr/bin/agetty -n -l /bin/sh 115200 ttyGS0
Restart=always
RestartSec=1
[Install]
WantedBy=sysinit.target
U
chroot $A /bin/bash /tmp/in.sh
# linux-firmware ships a generic amp firmware at the same path; keep Samsung's
sed -i 's|^#\?NoExtract *=.*|NoExtract = usr/lib/firmware/cirrus/cs35l41-dsp1-spk-prot.*|' $A/etc/pacman.conf
cp -a /lib/firmware/cirrus/cs35l41-dsp1-spk-prot.* $A/usr/lib/firmware/cirrus/
sed -i 's/^load-module module-suspend-on-idle/#&/' $A/etc/pulse/default.pa
rm -f $A/tmp/in.sh
for h in /home/user/.local /home/user/.config /home/user/Music; do [ -e $h ] && cp -a $h $A/home/user/ ; done
chroot $A chown -R user:user /home/user
sync
echo ARCH_INSTALL_DONE
