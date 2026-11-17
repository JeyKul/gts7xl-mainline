#!/bin/bash
# Pack the last AOSP build into a zip that TWRP can flash.
# Usage: make-zip.sh [out.zip]   (default: /mnt/builddrive/gts7xl-aosp17-<date>.zip)
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
O=${AOSP_OUT:-/mnt/builddrive/aosp17/out}; P=$O/target/product/gts7xlwifi
OUT=$(realpath -m "${1:-/mnt/builddrive/gts7xl-aosp17-$(date +%Y%m%d).zip}")
T=$(mktemp -d -p "$(dirname "$OUT")"); trap 'rm -rf "$T"' EXIT
# super holds data only up to its last partition's end: write just that much
"$O/host/linux-x86/bin/simg2img" "$P/super.img" "$T/full.raw"
END=$("$O/host/linux-x86/bin/lpdump" "$T/full.raw" | awk '/ linear super / { e = $6 + $3 - $1 + 1; if (e > m) m = e } END { print m * 512 }')
[ "$END" -gt 0 ] || { echo "no extents in super"; exit 1; }
END=$(( (END + 1048575) / 1048576 * 1048576 ))
head -c "$END" "$T/full.raw" > "$T/super.raw"; rm "$T/full.raw"
cp "$P/boot.img" "$T/boot.img"
echo "$END" > "$T/super-size"
(cd "$T" && sha256sum boot.img super.raw > sha256)
mkdir -p "$T/META-INF/com/google/android"
install -m755 "$HERE/zip/update-binary" "$T/META-INF/com/google/android/update-binary"
echo "# see update-binary" > "$T/META-INF/com/google/android/updater-script"
rm -f "$OUT"
(cd "$T" && zip -q -1 "$OUT" META-INF/com/google/android/update-binary META-INF/com/google/android/updater-script sha256 super-size boot.img super.raw)
(cd "$(dirname "$OUT")" && sha256sum "$(basename "$OUT")") > "$OUT.sha256"
ls -l "$OUT"
