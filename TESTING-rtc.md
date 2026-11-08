# Testing: real-time clock keeps time across reboots

## What changed

- `gts7xl-rtc-offset` (rootfs overlay, `/usr/local/sbin`): stores
  `offset = wall clock - PMIC RTC counter` in `/var/lib/gts7xl-rtc/offset` and
  adds it back at boot. **Nothing is written to the PMIC**; the RTC stays
  read-only, like on Android.
- `gts7xl-rtc-offset.service`: restore early at boot (before
  `systemd-timesyncd` / `time-set.target`), save at shutdown.
- `gts7xl-rtc-offset-save.path`: save again after each NTP sync, so a crash
  or a hard power-off still leaves a recent offset.
- Saves only when NTP synced the clock this boot (`save --force` to save a
  hand-set clock). Restore never steps the clock backwards, ignores results
  before 2026 (RTC reset by a dead battery), and ignores the offset if the
  counter already holds wall time (optional patch below).
- `rootfs/arch-setup.sh` enables both units on new installs.
- Optional, **not in the CI build**: `linux-patches/optional/rtc-allow-set-time.patch`
  (DT `allow-set-time` on the PM8150 RTC). Test only after the offset works.

## Test the offset service (no kernel change needed)

Copy the four files onto the running system (or reinstall):

    sudo install -m755 rootfs/overlay/usr/local/sbin/gts7xl-rtc-offset /usr/local/sbin/
    sudo install -m644 rootfs/overlay/etc/systemd/system/gts7xl-rtc-offset* /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable --now gts7xl-rtc-offset.service gts7xl-rtc-offset-save.path

1. Does the counter tick and keep counting across a reboot? (the whole idea
   depends on this)

       cat /sys/class/rtc/rtc0/name /sys/class/rtc/rtc0/since_epoch
       sudo gts7xl-rtc-offset status

   With Wi-Fi up, `NTP synced: yes`. Then save once:

       sudo gts7xl-rtc-offset save        # "offset ... saved"

2. Turn Wi-Fi off (so NTP can't fix the clock), power off, wait ~2 minutes,
   power on. Before connecting Wi-Fi:

       journalctl -b -u gts7xl-rtc-offset   # "clock set to ... (RTC n + offset m)"
       date -u                              # compare with a phone
       sudo gts7xl-rtc-offset status        # "drift vs clock" should be 0..2 s

**Success:** after the cold boot the clock is right to within a couple of
seconds before any network, and the journal line above is there.
**Failure signs:** "RTC+offset ... before 2026" (counter reset at power-off,
would mean the counter does not survive), or the time is off by exactly the
time the tablet was off (counter stops while off). Please report which.

3. Crash case: with Wi-Fi on, wait for a sync, then
   `ls -l /var/lib/gts7xl-rtc/offset` should have a fresh mtime
   (written by the path unit, `journalctl -u gts7xl-rtc-offset-save`).

## Optional: allow-set-time (writes the PMIC RTC counter)

Only if you want the counter itself to hold the time. Risk is low: if the
SPMI arbiter does not give Linux write access, the kernel refuses the write
with `-EPERM`; if it does, Android/TWRP will show a different time until
they resync. No partition is touched.

    cd src/linux && git am ~/gts7xl-mainline/linux-patches/optional/rtc-allow-set-time.patch
    # rebuild only the DTB: make ... qcom/sm8250-samsung-gts7xlwifi.dtb, repack boot as usual

After boot, with NTP synced:

    sudo hwclock -w && sudo hwclock -r ; dmesg | grep -i -e rtc -e "disallowed SPMI"
    cat /sys/class/rtc/rtc0/since_epoch ; date +%s     # should now match

**Success:** `hwclock -r` shows today, `since_epoch` ≈ `date +%s`, no
"disallowed SPMI write". Then the kernel's 11-minute sync keeps it updated and
the offset service sees the counter already holds wall time and adds nothing.
**If it fails** (`hwclock: ... Operation not permitted`, dmesg "disallowed
SPMI write"): writes are blocked by the arbiter; drop the patch, the offset
service still does the job. If the counter ever reads 1970 again after a failed
write, `cat /sys/class/rtc/rtc0/since_epoch` twice a few seconds apart and
report whether it still ticks.

## Revert

    sudo systemctl disable --now gts7xl-rtc-offset.service gts7xl-rtc-offset-save.path
    sudo rm -f /etc/systemd/system/gts7xl-rtc-offset* /usr/local/sbin/gts7xl-rtc-offset
    sudo rm -rf /var/lib/gts7xl-rtc

For the optional patch: rebuild the DTB without it. To return the counter to
what Android expects there is nothing to restore: Android re-syncs over the
network.
