#!/bin/sh
# Runs inside the arm64 Alpine container: NetSurf with its libraries, fonts and
# certificates as a small root for a chroot, packed to /w/netsurf-pkg.tar.xz
set -e
apk add -q curl openssl libpng libjpeg-turbo libwebp expat freetype zlib font-dejavu ca-certificates-bundle xz binutils >/dev/null
P=/w/pkg; rm -rf $P; mkdir -p $P/lib $P/bin $P/usr/share/fonts/dejavu $P/etc/ssl/certs $P/dev $P/proc $P/sys $P/tmp $P/root $P/sdcard
B=/w/out/usr/bin/netsurf-fb
cp $B $P/bin/; strip $P/bin/netsurf-fb
ldd $B | awk '/=>/{print $3}' | while read l; do cp -L $l $P/lib/; done
cp -L /lib/ld-musl-aarch64.so.1 $P/lib/
cp -a /w/out/usr/share/netsurf $P/usr/share/
cp /w/start.html $P/usr/share/netsurf/gts7xl-start.html
for f in DejaVuSans DejaVuSans-Bold DejaVuSans-Oblique DejaVuSans-BoldOblique DejaVuSerif DejaVuSerif-Bold DejaVuSansMono DejaVuSansMono-Bold; do
	cp /usr/share/fonts/dejavu/$f.ttf $P/usr/share/fonts/dejavu/
done
cp /etc/ssl/certs/ca-certificates.crt $P/etc/ssl/certs/
tar -C $P -cJf /w/netsurf-pkg.tar.xz .
du -sh $P; ls -l /w/netsurf-pkg.tar.xz
