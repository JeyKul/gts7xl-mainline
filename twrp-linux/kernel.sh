#!/bin/bash
# The TWRP kernel: LineageOS sm8250 4.19 (lineage-22.1) with the Tab S7+ Wi-Fi config
# plus btrfs. Usage: kernel.sh <work dir>; prints the Image.gz path for build.sh.
# Needs AOSP clang r416183b (clang 12) in CLANG_DIR.
set -euo pipefail
W=$(realpath -m "$1"); CLANG_DIR=${CLANG_DIR:-$HOME/toolchains/clang-r416183b}
[ -d "$W/.git" ] || git clone -q --depth 1 -b lineage-22.1 https://github.com/JeyKul/android_kernel_samsung_sm8250 "$W"
cd "$W"
export PATH=$CLANG_DIR/bin:$PATH
M=(make -s O=out ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu-)
"${M[@]}" vendor/kona-sec-perf_defconfig vendor/debugfs.config vendor/samsung/gts7xlwifi.config </dev/null >/dev/null
# dm-crypt is already in TWRP's own kernel; btrfs is what it lacks
scripts/config --file out/.config -e BTRFS_FS -e BTRFS_FS_POSIX_ACL -e CRYPTO_ZSTD -e DM_CRYPT -e CRYPTO_XTS \
	-e CRYPTO_USER_API_SKCIPHER -e CRYPTO_USER_API_HASH
"${M[@]}" olddefconfig >/dev/null
"${M[@]}" -j"$(nproc)" Image.gz
echo "$W/out/arch/arm64/boot/Image.gz"
