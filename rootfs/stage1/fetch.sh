#!/bin/sh
# Download the RAM installer and check it against the hash built into this boot image.
# Exit 1: download failed. Exit 2: wrong contents.
read -r URL < /stage2.url || exit 1
read -r SUM < /stage2.sha256 || exit 1
# HTTPS needs a real clock, and this tablet starts in 1970
ntpd -n -q -p pool.ntp.org >/dev/null 2>&1 || ntpd -n -q -p time.cloudflare.com >/dev/null 2>&1
mkdir -p /s2
mountpoint -q /s2 || mount -t tmpfs -o size=2G tmpfs /s2 || exit 1
wget -q -O /s2/stage2.tar.gz "$URL" || exit 1
[ "$(sha256sum /s2/stage2.tar.gz | cut -d' ' -f1)" = "$SUM" ] || { rm -f /s2/stage2.tar.gz; exit 2; }
tar -xzf /s2/stage2.tar.gz -C /s2 || exit 1
rm -f /s2/stage2.tar.gz
