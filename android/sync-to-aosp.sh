#!/bin/bash
# Copy the device tree into an AOSP checkout (the build doesn't follow symlinks
# when it looks for products). Usage: sync-to-aosp.sh <aosp checkout>
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
mkdir -p "$1/device/samsung"
rsync -a --delete "$HERE/device/samsung/gts7xlwifi/" "$1/device/samsung/gts7xlwifi/"
# gts7xl-sensord's C sources, built by device/.../sensord/Android.bp
rsync -a --delete "$HERE/../sensord/src/" "$1/device/samsung/gts7xlwifi/sensord/src/"
