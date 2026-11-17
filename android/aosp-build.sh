#!/bin/bash
# Sync the device tree in, then build everything for the Tab S7+.
# Usage: [AOSP=<checkout>] aosp-build.sh [make targets]   (log in <checkout>/build.log)
HERE=$(dirname "$(realpath "$0")")
cd "${AOSP:-/mnt/builddrive/aosp17}" || exit 1
"$HERE/sync-to-aosp.sh" .
source build/envsetup.sh >/dev/null
lunch gts7xlwifi-cp2a-userdebug >/dev/null
# Plain `m` leaves super.img stale
[ $# -eq 0 ] && set -- droid superimage
m -j24 "$@" > build.log 2>&1
echo "BUILD-EXIT $?" >> build.log
