#!/bin/sh
# Enter the downloaded installer. Run inside stage 1's chroot.
for d in proc sys dev; do mkdir -p /s2/$d; mount --bind /$d /s2/$d; done
mkdir -p /s2/tmp /s2/run
cp /etc/resolv.conf /s2/etc/resolv.conf
# Networks that worked, written back to userdata after formatting
[ -f /tmp/saved-wifi ] && install -m600 /tmp/saved-wifi /s2/tmp/saved-wifi
[ -e /dry ] && touch /s2/dry
chroot /s2 /s1ui --stage2
