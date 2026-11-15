#!/bin/bash
# The Android kernel config: this project's Linux config (build-bi) plus AOSP's
# required options (kernel/configs, v/android-6.6: the newest release with a
# non-empty list), LineageOS's mainline fragments and gts7xl-android.config. Usage: make-config.sh <linux src> <build dir> <aosp kernel/configs>
set -euo pipefail
S=$(realpath "$1"); B=$(realpath -m "$2"); K=$(realpath "$3"); HERE=$(dirname "$(realpath "$0")")
mkdir -p "$B"
cp "$S/build-bi/.config" "$B/.config"
cd "$S"
# LineageOS's mainline fragments (android_kernel_mainline_configs, lineage-24.0)
# fill what mainline lacks for Android (legacy iptables, cgroup v1, ...)
L=$HERE/lineage-mainline
scripts/kconfig/merge_config.sh -m -O "$B" "$B/.config" "$L/android-base-pre-common.config" "$L/android-base-pre-arm64.config" \
	"$K/v/android-6.6/android-base.config" "$L/common.config" "$HERE/gts7xl-android.config"
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -s O="$B" olddefconfig
