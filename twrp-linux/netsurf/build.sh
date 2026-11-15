#!/bin/bash
# NetSurf for the recovery: framebuffer frontend with a DRM/evdev surface
# (src/libnsfb-surface-drm.c) and downloads (src/fb-download.c), built in an arm64
# Alpine container (podman + qemu binfmt). Usage: build.sh <work dir>
# Result: <work dir>/netsurf-pkg.tar.xz, a small root for a chroot (build.sh in
# twrp-linux takes it as NETSURF_PKG).
set -euo pipefail
W=$(realpath -m "$1"); HERE=$(dirname "$(realpath "$0")")
V=3.11
mkdir -p "$W"
if [ ! -d "$W/netsurf-all" ]; then
	curl -fsSL -o "$W/netsurf-all.tar.gz" https://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-$V.tar.gz
	tar -xzf "$W/netsurf-all.tar.gz" -C "$W" && mv "$W/netsurf-all-$V" "$W/netsurf-all"
	(cd "$W/netsurf-all" && patch -p1 < "$HERE/netsurf-$V-gts7xl.patch")
fi
cp "$HERE/src/libnsfb-surface-drm.c" "$W/netsurf-all/libnsfb/src/surface/drm.c"
cp "$HERE/src/fb-download.c" "$W/netsurf-all/netsurf/frontends/framebuffer/download.c"
cp "$HERE/src/fb-download.h" "$W/netsurf-all/netsurf/frontends/framebuffer/download.h"
cp "$HERE/start.html" "$HERE/pkg.sh" "$W/"
cat > "$W/build-in.sh" <<'IN'
#!/bin/sh
set -e
apk add -q build-base git perl perl-html-parser bison flex gperf pkgconf curl-dev openssl-dev libpng-dev \
	libjpeg-turbo-dev libwebp-dev expat-dev freetype-dev zlib-dev linux-headers libdrm-dev font-dejavu coreutils >/dev/null
cd /w/netsurf-all
# No JavaScript (too big to help with modern sites), no SVG/RISC OS sprites/PDF
make -j"$(nproc)" TARGET=framebuffer NETSURF_FB_FONTLIB=freetype \
	NETSURF_FB_FONTPATH=/usr/share/fonts/dejavu NETSURF_USE_DUKTAPE=NO \
	NETSURF_USE_NSSVG=NO NETSURF_USE_ROSPRITE=NO NETSURF_USE_HARU_PDF=NO \
	PREFIX=/usr DESTDIR=/w/out install
sh /w/pkg.sh
IN
chmod 755 "$W/build-in.sh"
podman run --rm --platform linux/arm64 -v "$W":/w:Z docker.io/library/alpine:3.22 /w/build-in.sh
ls -l "$W/netsurf-pkg.tar.xz"
