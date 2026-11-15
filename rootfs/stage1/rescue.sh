#!/bin/sh
# Rescue actions for a system on btrfs, run by s1ui inside stage 1.
# /mnt/r is the top of the filesystem: @ is the system, @snapshots holds snapper's.
# A go-back renames @ to @before-<time> and makes a new @ from the snapshot, so
# undo only renames back. The kernel follows: each system holds its own boot image.
R=/mnt/r
exec 2>>/tmp/rescue.log
log() { echo "rescue: $*" >&2; echo "rescue: $*" > /dev/kmsg 2>/dev/null; }

tag() { sed -n "s:.*<$1>\(.*\)</$1>.*:\1:p" "$2" | head -n1; }

# Write the boot image that belongs to the system now in @, if it has one
boot_follow() {
	img=
	for f in $R/@/usr/lib/gts7xl/boot/boot-*.img; do [ -f "$f" ] && img=$f; done
	[ -n "$img" ] || { log "no boot image in the system, kernel left as it is"; return 0; }
	part=
	for u in /sys/class/block/*/uevent; do
		grep -qx 'PARTNAME=boot' "$u" && part=/dev/$(basename "$(dirname "$u")")
	done
	[ -b "$part" ] && [ "$(head -c 8 "$part")" = "ANDROID!" ] || { log "boot partition not found"; return 1; }
	isize=$(stat -c %s "$img")
	want=$(sha256sum < "$img")
	[ "$(head -c "$isize" "$part" | sha256sum)" = "$want" ] && { log "boot already matches"; return 0; }
	psize=$(blockdev --getsize64 "$part")
	[ "$isize" -lt "$psize" ] || { log "boot image too big"; return 1; }
	dd if=/dev/zero of="$part" bs=1M count=$((psize / 1048576)) conv=fsync 2>/dev/null
	dd if="$img" of="$part" bs=1M conv=fsync 2>/dev/null
	[ "$(head -c "$isize" "$part" | sha256sum)" = "$want" ] || { log "boot read-back mismatch"; return 1; }
	log "boot partition now $(basename "$img")"
}

# Keep the two newest replaced systems
prune() {
	ls -d $R/@before-* 2>/dev/null | sort -r | awk 'NR > 2' | while read -r o; do
		btrfs subvolume delete -R "$o" >&2 || log "could not delete $o"
	done
}

case "$1" in
list)
	for d in $R/@snapshots/*/; do
		n=${d%/}; n=${n##*/}
		[ -f "$d/info.xml" ] && [ -d "$d/snapshot" ] || continue
		echo "$n|$(tag date "$d/info.xml")|$(tag type "$d/info.xml")|$(tag description "$d/info.xml")"
	done | sort -t'|' -k1,1nr
	;;
rollback)
	case "$2" in '' | *[!0-9]*) exit 2 ;; esac
	S=$R/@snapshots/$2/snapshot
	[ -d "$S" ] || { log "no snapshot $2"; exit 2; }
	old=$R/@before-$(date +%Y%m%d-%H%M%S)
	mv $R/@ "$old" || exit 3
	if ! btrfs subvolume snapshot "$S" $R/@ >&2; then
		mv "$old" $R/@
		log "snapshot $2 could not be copied, nothing changed"
		exit 3
	fi
	log "system is now snapshot $2, previous one in ${old##*/}"
	prune
	boot_follow
	sync
	;;
undo)
	last=$(ls -d $R/@before-* 2>/dev/null | sort -r | head -n1)
	[ -n "$last" ] || { log "nothing to undo"; exit 2; }
	mv $R/@ $R/@undone-$(date +%Y%m%d-%H%M%S) || exit 3
	mv "$last" $R/@ || exit 3
	# The undone system goes the same way as other replaced ones
	for u in $R/@undone-*; do [ -d "$u" ] && mv "$u" "$R/@before-${u##*/@undone-}"; done
	log "returned to ${last##*/}"
	prune
	boot_follow
	sync
	;;
close)
	umount $R 2>/dev/null
	[ -e /dev/mapper/userdata ] && DM_DISABLE_UDEV=1 cryptsetup close userdata
	true
	;;
*)
	echo "usage: rescue.sh list|rollback <n>|undo|close" >&2
	exit 1
	;;
esac
