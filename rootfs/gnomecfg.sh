mkdir -p /etc/udev/rules.d /etc/gdm /etc/dconf/db/local.d/locks /etc/dconf/profile /etc/conf.d
cat > /etc/udev/rules.d/61-mutter-primary-gpu.rules <<X
ACTION=="add|change", SUBSYSTEM=="drm", KERNEL=="card*", DRIVERS=="adreno|msm", TAG+="mutter-device-preferred-primary"
X
cat > /etc/gdm/custom.conf <<X
[daemon]
AutomaticLoginEnable=True
AutomaticLogin=user
WaylandEnable=true
X
printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
cat > /etc/dconf/db/local.d/00-gts7xl-power <<X
[org/gnome/settings-daemon/plugins/power]
sleep-inactive-ac-type='nothing'
sleep-inactive-battery-type='nothing'
power-button-action='nothing'
power-saver-profile-on-low-battery=false
[org/gnome/desktop/session]
idle-delay=uint32 0
[org/gnome/desktop/interface]
scaling-factor=uint32 2
[org/gnome/desktop/peripherals/touchpad]
send-events='disabled'
X
dconf update 2>&1 | head -1
sed -i '/^tty1::/d' /etc/inittab
echo 'rc_after="local dbus elogind"' > /etc/conf.d/gdm
for s in elogind polkit gdm; do rc-update add $s default 2>&1 | cut -c1-40; done
adduser gdm video 2>/dev/null; addgroup gdm input 2>/dev/null; addgroup gdm seat 2>/dev/null
adduser user netdev 2>/dev/null; addgroup user plugdev 2>/dev/null
rc-update del seatd default 2>&1 | cut -c1-40
grep -c ^tty1 /etc/inittab; sync
