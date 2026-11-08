#!/bin/sh
# Overnight suspend-drain logger for the Tab S7+ (gts7xlwifi).
#
#   sudo sh tools/drain-log.sh install   # once, before bed (unplugged)
#   ...suspend as usual (power key / cover), leave it overnight...
#   sudo sh tools/drain-log.sh report    # in the morning -> /root/drain-report.tgz
#   sudo sh tools/drain-log.sh uninstall # removes the hook
#
# A systemd sleep hook snapshots battery, suspend/s2idle stats, Qualcomm
# low-power residency (qcom_stats), wakeup sources and interrupts right before
# every suspend and right after every resume, so spurious wakes show up as extra
# pre/post pairs with their cause.
set -u
LOG=/var/log/gts7xl-drain.log
HOOK=/usr/lib/systemd/system-sleep/gts7xl-drain

snap() {
	echo "=== $1 wall=$(date -Is) boottime_s=$(cut -d' ' -f1 /proc/uptime)"
	for p in /sys/class/power_supply/*; do
		[ -d "$p" ] || continue
		printf -- "--- psu %s:" "$(basename "$p")"
		for f in status online capacity voltage_now current_now current_avg charge_counter charge_now energy_now temp; do
			[ -r "$p/$f" ] && printf " %s=%s" "$f" "$(cat "$p/$f" 2>/dev/null)"
		done
		echo
	done
	for f in /sys/power/suspend_stats/*; do
		[ -r "$f" ] && echo "--- suspend_stats $(basename "$f")=$(cat "$f" 2>/dev/null | tr '\n' ' ')"
	done
	echo "--- mem_sleep=$(cat /sys/power/mem_sleep 2>/dev/null) pm_wakeup_irq=$(cat /sys/power/pm_wakeup_irq 2>/dev/null)"
	if [ -d /sys/kernel/debug/qcom_stats ]; then
		for f in /sys/kernel/debug/qcom_stats/*; do
			[ -r "$f" ] && echo "--- qcom_stats $(basename "$f"): $(cat "$f" 2>/dev/null | tr '\n' ' ')"
		done
	else
		echo "--- qcom_stats: not present"
	fi
	echo "--- wakeup_sources (active_since/event_count>0):"
	awk 'NR==1 || $2>0 || $3>0' /sys/kernel/debug/wakeup_sources 2>/dev/null
	echo "--- interrupts:"
	cat /proc/interrupts
}

case "${1:-}" in
install)
	mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
	echo 1 > /sys/power/pm_debug_messages 2>/dev/null   # wakeup reasons in dmesg
	mkdir -p "$(dirname "$HOOK")"
	cat > "$HOOK" <<HOOKEOF
#!/bin/sh
# \$1 = pre|post, \$2 = suspend|hibernate|...
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
sh "$(readlink -f "$0")" snap "\$1-\$2" >> "$LOG" 2>&1
HOOKEOF
	chmod 755 "$HOOK"
	: > "$LOG"
	snap install >> "$LOG" 2>&1
	echo "Installed. Unplug, suspend as usual, run 'report' in the morning."
	echo "Battery now: $(cat /sys/class/power_supply/*/capacity 2>/dev/null | tr '\n' ' ')%"
	;;
snap)
	snap "${2:-manual}"
	;;
report)
	snap report >> "$LOG" 2>&1
	D=$(mktemp -d)
	cp "$LOG" "$D/drain.log"
	dmesg > "$D/dmesg.txt" 2>&1
	journalctl -b --no-pager -g 'suspend|resume|sleep|wake|s2idle|PM:' > "$D/journal.txt" 2>&1
	uname -a > "$D/uname.txt"
	tar -C "$D" -czf /root/drain-report.tgz .
	rm -rf "$D"
	grep -c '^=== pre' "$LOG" | sed 's/^/suspend cycles logged: /'
	grep -E '^=== |capacity=' "$LOG" | grep -E '^=== |battery' | head -40
	echo "Send /root/drain-report.tgz"
	;;
uninstall)
	rm -f "$HOOK"
	echo 0 > /sys/power/pm_debug_messages 2>/dev/null
	echo "Hook removed; log kept at $LOG"
	;;
*)
	sed -n '2,13p' "$0"
	exit 1
	;;
esac
