#!/bin/bash
# Sync the device tree in, then build everything for the Tab S7+
cd /mnt/builddrive/aosp17
# Mesa's meson build needs mako, kept outside the system Python
export PYTHONPATH=/mnt/builddrive/pydeps${PYTHONPATH:+:$PYTHONPATH}
~/gts7xl-mainline/repo/android/sync-to-aosp.sh .
source build/envsetup.sh >/dev/null
lunch gts7xlwifi-cp2a-userdebug >/dev/null
m -j24 "$@" > build.log 2>&1
echo "BUILD-EXIT $?" >> build.log
