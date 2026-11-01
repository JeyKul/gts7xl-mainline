#!/bin/bash
# Package the build for flashing. Usage: package.sh <boot.img> <rootfs.tar.gz> <output dir>
set -euo pipefail
BOOT=$(realpath "$1"); ROOTFS=$(realpath "$2"); OUT=$(realpath -m "$3")
HERE=$(dirname "$(realpath "$0")")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$OUT"

# TWRP: the full install
cp -r "$HERE/twrp/." "$T/"
ln -s "$BOOT" "$T/boot.img"; ln -s "$ROOTFS" "$T/rootfs.tar.gz"
(cd "$T" && zip -0 -q -r "$OUT/gts7xlwifi-arch-twrp.zip" META-INF boot.img rootfs.tar.gz)

# Odin: boot and dtbo only. The system itself is installed from TWRP
O=$(mktemp -d); trap 'rm -rf "$T" "$O"' EXIT
lz4 -q -B6 --content-size "$BOOT" "$O/boot.img.lz4"
head -c 10485760 /dev/zero > "$O/dtbo.img"
lz4 -q -B6 --content-size --rm "$O/dtbo.img" "$O/dtbo.img.lz4"
tar --format=ustar -C "$O" -cf "$OUT/gts7xlwifi-boot-odin.tar" boot.img.lz4 dtbo.img.lz4
md5sum -t "$OUT/gts7xlwifi-boot-odin.tar" | sed 's|  *.*/|  |' >> "$OUT/gts7xlwifi-boot-odin.tar"
mv "$OUT/gts7xlwifi-boot-odin.tar" "$OUT/gts7xlwifi-boot-odin.tar.md5"
(cd "$OUT" && sha256sum gts7xlwifi-arch-twrp.zip gts7xlwifi-boot-odin.tar.md5 > SHA256SUMS)
