#!/bin/bash
# Copy the device tree into an AOSP checkout (the build doesn't follow symlinks
# when it looks for products). Usage: sync-to-aosp.sh <aosp checkout>
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
mkdir -p "$1/device/samsung"
rsync -a --delete "$HERE/device/samsung/gts7xlwifi/" "$1/device/samsung/gts7xlwifi/"
# gts7xl-sensord's C sources, built by device/.../sensord/Android.bp
rsync -a --delete "$HERE/../sensord/src/" "$1/device/samsung/gts7xlwifi/sensord/src/"
# Mesa's meson/ninja wrappers (external/mesa looks in prebuilts/mesa-build-dep/bin)
mkdir -p "$1/prebuilts/mesa-build-dep/bin"
install -m755 "$HERE/prebuilts-mesa-build-dep/meson" "$HERE/prebuilts-mesa-build-dep/ninja" "$1/prebuilts/mesa-build-dep/bin/"
[ -d "$1/prebuilts/mesa-build-dep/pydeps/mako" ] || [ -n "${GTS7XL_PYDEPS:-}" ] ||
	echo "warning: no Python modules for Mesa: pip install --target $1/prebuilts/mesa-build-dep/pydeps mako pyyaml packaging"
# AOSP patches, once: patches/<project path with _ for />/*.patch
for d in "$HERE"/patches/*/; do
	p=$(basename "$d" | tr _ /)
	for f in "$d"*.patch; do
		if git -C "$1/$p" apply --check "$f" 2>/dev/null; then
			git -C "$1/$p" apply "$f" && echo "applied $(basename "$f") to $p"
		fi
	done
done
