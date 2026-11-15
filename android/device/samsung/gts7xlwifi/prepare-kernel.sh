#!/bin/bash
# Fill prebuilt/ from this project's kernel build for Android (build-android):
# Image.gz with the DTB appended, the modules with modules.load, and the
# redistributable linux-firmware files. Usage: prepare-kernel.sh <linux src> <build dir>
set -euo pipefail
S=$(realpath "$1"); B=$(realpath "$2"); HERE=$(dirname "$(realpath "$0")")
P=$HERE/prebuilt; rm -rf "$P"; mkdir -p "$P/modules" "$P/firmware/qcom" "$P/firmware/qca" "$P/firmware/ath11k/QCA6390/hw2.0"
gzip -9c "$B/arch/arm64/boot/Image" > "$P/Image.gz"
cat "$P/Image.gz" "$B/arch/arm64/boot/dts/qcom/sm8250-samsung-gts7xlwifi.dtb" > "$P/Image.gz-dtb"
rm "$P/Image.gz"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
make -s -C "$S" O="$B" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$T" INSTALL_MOD_STRIP=1 modules_install
find "$T/lib/modules" -name '*.ko' -exec cp {} "$P/modules/" \;
LFW=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain
for f in qcom/a650_sqe.fw qcom/a650_gmu.bin ath11k/QCA6390/hw2.0/amss.bin ath11k/QCA6390/hw2.0/board-2.bin ath11k/QCA6390/hw2.0/m3.bin qca/htbtfw20.tlv qca/htnv20.bin; do
	curl -fsSL -o "$P/firmware/$f" "$LFW/$f"
done
echo "$(ls "$P/modules" | wc -l) modules, kernel $(stat -c %s "$P/Image.gz-dtb") bytes"
