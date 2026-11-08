#!/bin/sh
# tuned calls this with start or stop; gts7xl-power does the work
case "$1" in start) exec /usr/local/sbin/gts7xl-power set powersave ;; esac
exit 0
