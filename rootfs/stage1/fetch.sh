#!/bin/sh
# Download the RAM installer and check it against the hash built into this boot image.
# Exit 1: download failed. Exit 2: wrong contents. The reason goes to /tmp/fetch.err.
# Links come from the manifests, then from the link built into this image, so moving a
# release breaks nothing: installer.json in the repo (edited by hand) and on the
# Installer release (kept by CI). One installer per line, keyed by its sha256.
err() { echo "$1" > /tmp/fetch.err; echo "fetch: $1" > /dev/kmsg 2>/dev/null; }
rm -f /tmp/fetch.err
read -r SUM < /stage2.sha256 || { err "no installer hash in this boot image"; exit 1; }
URL=; [ -r /stage2.url ] && read -r URL < /stage2.url
MANIFESTS=$(cat /stage2.manifest 2>/dev/null)
[ -n "$MANIFESTS" ] || MANIFESTS="https://raw.githubusercontent.com/JeyKul/gts7xl-mainline/main/installer.json https://github.com/JeyKul/gts7xl-mainline/releases/download/Installer/installer.json"

# HTTPS needs a real clock, and this tablet starts in 1970. Never earlier than this
# image was built, which is enough for certificates; NTP then makes it exact.
B=$(cat /build.time 2>/dev/null); [ -n "$B" ] && [ "$(date +%s)" -lt "$B" ] && date -s "@$B" >/dev/null
# Right after the Wi-Fi join the resolver may not answer yet
for i in 1 2 3 4 5 6 7 8 9 10; do nslookup github.com >/dev/null 2>&1 && break; sleep 1; done
nslookup github.com >/dev/null 2>&1 || { err "no DNS: Wi-Fi joined, but names do not resolve"; exit 1; }
timeout 15 ntpd -n -q -p pool.ntp.org >/dev/null 2>&1 || timeout 15 ntpd -n -q -p time.cloudflare.com >/dev/null 2>&1

mkdir -p /s2
mountpoint -q /s2 || mount -t tmpfs -o size=2G tmpfs /s2 || { err "cannot make the RAM disk"; exit 1; }

# A manifest can fail right after the join while the link settles: try each a few times
LINKS=
for m in $MANIFESTS; do
	for try in 1 2 3; do
		if wget -q -T 20 -O /tmp/manifest.json "$m" 2>/tmp/manifest.err; then
			l=$(grep "\"$SUM\"" /tmp/manifest.json | sed -n 's/.*"url": *"\([^"]*\)".*/\1/p')
			echo "fetch: manifest $m: $(echo $l | wc -w) link(s)" > /dev/kmsg 2>/dev/null
			LINKS="$LINKS $l"
			break
		fi
		echo "fetch: manifest $m failed: $(tail -n1 /tmp/manifest.err)" > /dev/kmsg 2>/dev/null
		sleep 2
	done
done
LINKS="$LINKS $URL"
[ -n "$(echo $LINKS)" ] || { err "this installer is not listed in the manifest"; exit 1; }

why="download failed"
for u in $LINKS; do
	for try in 1 2 3; do
		rm -f /s2/stage2.tar.gz
		if ! wget -q -T 30 -O /s2/stage2.tar.gz "$u"; then
			why="download failed: $u"; sleep 2; continue
		fi
		if [ "$(sha256sum /s2/stage2.tar.gz | cut -d' ' -f1)" != "$SUM" ]; then
			why="damaged: $u"; break
		fi
		tar -xzf /s2/stage2.tar.gz -C /s2 || { err "cannot unpack the installer"; exit 1; }
		rm -f /s2/stage2.tar.gz
		exit 0
	done
done
rm -f /s2/stage2.tar.gz
err "$why"
case "$why" in damaged*) exit 2 ;; esac
exit 1
