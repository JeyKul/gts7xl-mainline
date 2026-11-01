#!/bin/sh
# Enter the downloaded installer. Run inside stage 1's chroot.
for d in proc sys dev; do mkdir -p /s2/$d; mount --bind /$d /s2/$d; done
mkdir -p /s2/tmp /s2/run
cp /etc/resolv.conf /s2/etc/resolv.conf
[ -e /dry ] && touch /s2/dry
chroot /s2 /s1ui --stage2
