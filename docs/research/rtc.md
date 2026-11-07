# RTC reads 1970

Verdict: **easy, low risk**. Two options: userspace offset (zero kernel work) or in-kernel offset in a storage that survives reboot.

## Facts
- Samsung: `qcom,pm8150_rtc` = `compatible "qcom,pm8941-rtc"`, irq 0x61, **no** `allow-set-time`, no nvmem cell [overlay-r06.dts:172](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts#L172). So on stock the PMIC RTC counter is read-only from the AP; Android keeps wall time as *offset* (Qualcomm `time_genoff`/ATS, modem/ADSP + persist data) on top of the counter. The counter itself starts at an arbitrary value (reads 1970 on Linux).
- Mainline `rtc-pm8xxx.c` supports a stored offset: `qcom,uefi-rtc-info` (UEFI variable, GPS-epoch offset, 12 bytes), `nvmem-cells = <&rtc_offset>; nvmem-cell-names = "offset"` (4-byte LE Unix offset), and `allow-set-time` to write the PMIC counter directly. [rtc-pm8xxx.c:51-160](https://github.com/torvalds/linux/blob/v7.3/drivers/rtc/rtc-pm8xxx.c#L51), [binding](https://github.com/torvalds/linux/blob/v7.3/Documentation/devicetree/bindings/rtc/qcom-pm8xxx-rtc.yaml)
- The device boots straight from Samsung's bootloader (no UEFI here; `uefi/` Mu-Silicium attempt unused) → `qcom,uefi-rtc-info` has no UEFI runtime variable service → **not usable**.
- Mainline sm8250 has no rtc node in SoC dtsi; PM8150 node in `pm8150.dtsi` provides `rtc@6000` (disabled by default; verify in tree). Repo enabled "Driver loaded, runs from 1970" (README) so it's probing.

## Options
A. **`allow-set-time`**: `hwclock --systohc` after NTP writes the PMIC counter. Counter survives reboots (PMIC always powered, also off-state while battery present). Cost: writes a counter Android/TWRP also reads – harmless if Linux is the only OS; TWRP/Android may then show odd time (Android re-syncs on network, Samsung ATS offset is separate – guess). Safe: counter is a plain seconds register (0x6000 range in PMIC); no partition touched.
B. **nvmem offset** (preserves counter): store offset in a nvmem cell. Candidates: PMIC SDAM (`qcom,spmi-sdam` on PM8150 – check mainline `qcom,spmi-sdam` nodes, pm8150 has SDAM 0x7?? guess), or a file offset emulated via userspace (option C).
C. **Userspace offset** (no kernel change): at shutdown/every NTP sync write `$(date +%s) - rtc_counter` to `/var/lib/rtc-offset`; at early boot (initramfs or systemd unit before `time-sync.target`) `date -s @$((counter + offset))`. Fully safe, loses accuracy by suspend/idle time only if counter is stopped (it isn't). `systemd-timesyncd` also saves a clock stamp `/var/lib/systemd/timesync/clock` and sets time ≥ build/stamp time at boot (already gives approximate time if fs is persistent; check `timedatectl timesync-status`).

## Staged plan
1. *Read*: `cat /sys/class/rtc/rtc0/{since_epoch,name}`, `hwclock -r`; confirm counter ticks and survives a reboot (reboot, read; delta should equal elapsed). Test: delta within ±2 s.
2. Option C first (script + service). Test: reboot offline → time within seconds; alarm `rtcwake -m no -s 60` wakes s2idle (README says timer wake already works).
3. DT: add `allow-set-time;` to the `&pm8150_rtc` node (check label in tree: `&pmk8350_rtc`/`&pm8150_rtc`?). Test: `hwclock -w`, reboot, `hwclock -r` correct, `dmesg | grep rtc` no errors.
4. If the Samsung-side offset conflicts (Android later): restore via TWRP not needed; Android corrects on network.
5. Drop option C once 3 is stable.

## Risks
- Writing the counter: low. Affects Android/TWRP clock until they resync. No partition writes (obeys CLAUDE.md safety rules).
- Don't use `uefi-rtc-info` (no UEFI runtime).
- Check `CONFIG_RTC_DRV_PM8XXX` =y and `CONFIG_RTC_HCTOSYS` (set system clock from rtc0 at boot) – with allow-set-time kernel sets time at boot automatically.
