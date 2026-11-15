#!/bin/bash
# The Android kernel config: this project's Linux config (build-bi) plus AOSP's
# required options (kernel/configs, v/android-6.6: the newest release with a
# non-empty list) plus gts7xl-android.config. Usage: make-config.sh <linux src> <build dir> <aosp kernel/configs>
set -euo pipefail
S=$(realpath "$1"); B=$(realpath -m "$2"); K=$(realpath "$3"); HERE=$(dirname "$(realpath "$0")")
mkdir -p "$B"
cp "$S/build-bi/.config" "$B/.config"
cd "$S"
scripts/kconfig/merge_config.sh -m -O "$B" "$B/.config" "$K/v/android-6.6/android-base.config" "$HERE/gts7xl-android.config"
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -s O="$B" olddefconfig
