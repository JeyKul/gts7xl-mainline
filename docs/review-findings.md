# Review findings

Branch `review-cleanup`, from a source read only: nothing here has run on hardware. Clear-cut fixes are in commits on the branch; everything below was left unchanged.

Sources, as in `docs/usbc-dp-research.md`: `S:` Samsung T970 tree, `DT:` its `kona-sec-gts7xlwifi-eur-overlay-r06.dts`, `L:` v7.3.

## Camera broken in the latest kernel

**Result: no clear-cut cause in the camera's supplies, so no fix commit. The tests below separate what's left.**

What the series says, from `git am` of all patches and a diff of the results:
- From the last panel patch (0042) to 0050, the board DTS changes by 41 added lines and no removed ones: `&slpi` (0043), L8C (0044), L4C, L10A, L16A (0045). No camera node, supply or pin changed.
- `drivers/media` has not changed since 0039.
- The sensors use only `cam_vio` (gpio37), `cam_rear_vana` (gpio39), `cam_front_vana` (gpio38), `cam_wide_vana` (gpio42), plus L5A and L9A for CAMSS. Rear GPIOs match `DT:10686` on (mclk 94, reset 73, VANA 39, VIO 37). None of the new rails is a camera supply in this DT. Whether a VANA or VIO switch is fed from L4C, L10A or L16A can't be told without a schematic.

Suspects, most likely first:
1. **A DTB built while 0046 was bad.** Commit `48466d3` put the `rb_*` readback nodes into 0046. NOTES records that they stop `regulators-0` probing, and that holds L5A and L9A, the CAMSS `vdda-phy` and `vdda-pll` supplies. `fa6993e` removed them; a DTB from main today has none. Any DTB or kernel tree built between those two commits would lose the cameras along with the panel. Check: `ls /sys/kernel/debug/regulator/ | grep rb_` should print nothing.
2. **Modules not matching the kernel.** `VIDEO_QCOM_CAMSS`, `I2C_QCOM_CCI`, `VIDEO_S5K3M5`, `VIDEO_S5K_TAB` and `VIDEO_DW9807_VCM` are all `=m`, while touch and the pogo keyboard are built in. Flashing a new boot image without the matching modules would take out the cameras and little else. Unverified: I can't see what's installed.
3. **SLPI running (0043).** New since the cameras last worked, and the only one of these changes that starts firmware. No mechanism known.
4. **Userspace.** Arch is rolling, and libcamera or GStreamer may have updated.
5. **Dropping `regulator_ignore_unused`** (NOTES, `f13b1dd`). Unlikely: the repo's defconfig never had it (`git log -S`), so only local builds change. `regulator_late_cleanup` also skips RPMh rails nobody has voted on, and the fixed VANA rails start off.

Tests, in order. Each one rules out a suspect:
```
uname -r; lsmod | grep -E 'camss|cci|s5k|dw9807'
sudo dmesg | grep -iE 's5k|cci|camss|csiphy|dw9807|disagrees|invalid module|exec format'
sudo ls /sys/kernel/debug/regulator/ | grep -c rb_
cam -l; media-ctl -p | grep -E 'entity|s5k' | head
grep -E 'libcamera|gstreamer' /var/log/pacman.log | tail -5
```
- Modules missing or `disagrees`: suspect 2. Install the modules from the same build.
- Chip ID or I2C errors from a sensor: power or CCI. Read the rails with `sudo cat /sys/kernel/debug/regulator/regulator_summary`. Then boot a DTB from before 0043 with the same Image: if the camera returns, it's in 0043-0045, and adding them back one at a time gives the commit.
- Sensors probe and `cam -l` lists them, but no frames: `sudo systemctl stop 'gts7xl-sensord*'`, then stop the SLPI under `/sys/class/remoteproc/`, and retest with `cam -c 1 --capture=5`.
- `cam` works but the app doesn't: userspace, not the kernel.

## Kernel and DT, not changed

| Where | Issue | Evidence | Suggested fix | Risk |
|---|---|---|---|---|
| 0045, `vreg_l16a_2p8` | 2.704 V is below stock's range of 2.8-3.0 V | `DT:10076-10078`. NOTES: bootloader readback 2.704 V; forcing 2.8 V blanks the panel | None now. Find out what the rail feeds. Follows the bootloader value rule in `docs/sensors.md` | Panel |
| 0001 `fb_panel` `width-mm`, 0009 comment and `input_abs_set_res` | 273 mm, against 267 mm in the panel driver. 12.4 in at 16:10 gives 267x167 | Calculated. Stock value not found under the S6TUUM3 panel node | Use 267 in both, after measuring | Low: DPI until msm loads, pen resolution |
| `samsung-w90xx.c` `W90XX_MAX_X/Y` | 26750x16700, against stock's 22656x14160 | `wacom,max_coords` in `DT:` under `wacom@56`. Calibration here fits about 26754x16705 | Leave it: measurement beats the DT. Find out why they differ | None now |
| `samsung-stm32-pogo.c` `pogo_irq` | The MCU check, with `msleep(200)` and six reads, runs in the interrupt thread, blocking key events meanwhile | Code | Queue it on a workqueue, as the removed `check_ic_work` seems to have intended | Trackpad stalls: test with the cover |
| `adreno_gpu.c`, `r8q_zap_dyn` | The buffer is never freed, including on load failures before auth | Comment at `out:` covers only success | Free on the error paths | Zap handling: test GPU |
| `a6xx_gpu.c`, `r8q_zap_secvid` | Defaults to true in the code, with `=0` set only in `rootfs/overlay/etc/modprobe.d/msm-gpu.conf`. A kernel with no overlay gets the riskier path | Code | Default false | GPU bring-up |
| `sm8250.c` `sm8250_snd_hw_params` | `prefix[0]` dereferences `name_prefix` with no check. This board always sets it; others using the driver with TDM may not | Code | Skip when NULL | Low |
| `kmod/gts7xl-panel-ctl` | `ctl->off` is set even if the 0x28 command fails | Code | Any fix changes panel behaviour, so it is left | Panel |

## Checked, no problem found

- Reserved memory: all `reg` values match `DT:8170-8274` and `:13254`; no overlaps, 0x80b00000 to 0x90a00000.
- I2C addresses and buses: touch se5 0x49, pen se17 0x56, pogo se9 0x2a, amps se11 0x40-0x43, MAX77705 se0 0x66.
- IRQs: touch gpio15, pen gpio136, pogo gpio31, level low; conn gpio43, both edges. Amps gpio84.
- GPIOs: pogo swclk 172, reset 174; pen fwe 6; hall 157 active low. MAX77705 line, level low, pm8150l gpio11 (`DT:11112-11115`).

## Not reviewed

`s5k3m5.c` beyond its supplies, register tables, the rest of the DSI and audio patches, `uefi/`, and vendored `v4l2loopback`. Voltages of the RPMh rails other than L16A were not compared with `DT:`.

## Userspace, not changed

From a read-only pass, numbered as found; the gaps are the entries fixed on this branch. Each entry was confirmed by reading the code, but only the fixed ones were re-read for this document.

## 2. UNCERTAIN: `deploy.py` writes outside boot, dtbo and userdata, and never checks the result
**File:** `tools/deploy.py:21-22`

**Issue:** it mounts `/dev/sda31` read-write and copies the kernel to `EFI/BOOT/BOOTAA64.EFI`. `CLAUDE.md:14` says "Only boot, dtbo and userdata". The only guard is a blkid label match on `GTS7ESP`. No other file in the repo identifies sda31 (`sda31` and `GTS7ESP` appear only in this tool).

The copy is also unverified: the output goes to `print()`, with a 16-character hash for a person to compare by eye. If the mount or copy fails, the script still ends with `sha kernel ...` and exit 0.

**Fix:** the owner must decide whether this partition is an allowed exception and, if so, record that in `CLAUDE.md`. The check itself is safe to add now, because it only touches the host tool:
```python
r=ts('''...; sha256sum /mnt/e/EFI/BOOT/BOOTAA64.EFI | cut -d" " -f1; umount /mnt/e''',60)
if sha(K) not in r: sys.exit('ESP KERNEL MISMATCH: '+r)
```

**Risk:** the check has none. The policy question is why this is UNCERTAIN.

## 5. UNCERTAIN: the S Pen agent approves every Bluetooth device, not only the pen
**File:** `rootfs/overlay/usr/local/sbin/gts7xl-spen:85-96`, with `:117-118`, `:198`

**Issue:** `RequestConfirmation`, `RequestAuthorization` and `AuthorizeService` all return without checking the device. Returning means approval in BlueZ's agent API. This agent is registered as the default and takes that role back from the desktop's agent (line 198), so the desktop's prompts can be bypassed for other devices too.

Separately, `is_pen()` at `:139` is a substring match, so any device whose name contains "spen" (such as "Aspen ...") is treated as a pen. It gets paired and connected, and `forget_pen()` may remove it.

**Fix:** reject anything that is not the pen, raising `org.bluez.Error.Rejected` from a `dbus.DBusException` subclass with `_dbus_error_name` set. Check `is_pen()` or the known address first, and use `name.startswith("spen")`.

**Risk:** needs a real pen to test. The pen's name or UUIDs may not be visible yet at pairing time, and an over-strict check would break pairing. I did not test how reachable this is from another device.

## 6. Mixed: charge control loses error reports and its 9 V state
**File:** `rootfs/overlay/usr/local/sbin/gts7xl-charge-control:13`, `:38-40`, `:46`, `:58-59`, `:68-69`

**Issue, three related problems:**
- **Limit writes fail silently (`:13`).** `open(path,'w').write(...)` flushes when the object is destroyed, and errors there are dropped. I confirmed this here with `/dev/full`: no exception reached the caller. So if `setlim(500000)` is rejected, the code still requests 9 V at `:68` and believes the limit was set.
- **I2C errors are unguarded (`:46`, `:59`, `:68-69`).** Only the `caps()` call at `:62` is inside `try`. An I2C error in `req()` or the verify step kills the daemon, including on the "too hot, go to 5 V" path.
- **State is lost on restart (`:38`).** After `Restart=always`, `fast=False` even if the port is still at 9 V. Line 58 needs `hot and fast`, so a restarted daemon does not drop to 5 V when hot. The input is limited to 900 mA at that point (line 40), but stays at 9 V until the port is re-negotiated.

**Fix:**
- CLEAR part: use `with open(...)` in `setlim` so failures are raised and logged. A failure then restarts the daemon, which goes through `setlim(SAFE)`.
- UNCERTAIN part (changes charging behaviour, needs testing with a charger): at startup, if `caps(f)[0] != 1`, step down to 5 V first or set `fast=True`. Also wrap the thermal fallback in `try/except OSError` and retry it.

**Risk:** this is charging and thermal logic, so the owner should review it. No voltage or current values need to change.

## 7. CLEAR: a reserved user name wipes userdata, then fails the install
**File:** `rootfs/stage1/s1ui.c:628`, with `rootfs/arch-setup.sh:4`, `:42`

**Issue:** only `root` is refused. `bin`, `daemon`, `mail`, `ftp`, `http`, `nobody`, `dbus` and similar pass `valid_user()`, but `useradd` fails because they are standard Arch system accounts (from my knowledge of Arch, not checked against the rootfs). With `set -e` the install then aborts after the format and the download, so the Android data is already gone and the user has to start again from the installer.

**Fix:**
```c
static int reserved_user(const char *u)
{
	static const char *const names[] = { "root", "bin", "daemon", "mail", "ftp", "http", "nobody", "dbus",
		"polkitd", "rtkit", "gdm", "sddm", "avahi", "colord", "git", "uuidd", NULL };
	if (!strncmp(u, "systemd-", 8))
		return 1;
	for (int i = 0; names[i]; i++)
		if (!strcmp(u, names[i]))
			return 1;
	return 0;
}
```
Then use `if (!reserved_user(copy) && valid_user(copy)) break;`. Line 630's message could also mention reserved names.

**Risk:** none. The list is not exhaustive, so a stronger version would also check the target's `/etc/passwd`.

## 9. CLEAR: Wi-Fi networks with non-ASCII names, quotes or backslashes cannot be joined
**File:** `rootfs/stage1/s1ui.c:324`, `:355`, `:438`

**Issue:** `scan_results` escapes SSIDs, and `wifi_connect` hex-encodes that escaped text as if it were the real name. A network called `Café` is requested as the literal `Caf\xc3\xa9`, so it never connects and the UI reports "Wrong password?". `ssid[64]` can also truncate, because an escaped name can reach 128 characters.

**Evidence:** hostap `src/utils/common.c`: `wpa_ssid_txt()` calls `printf_encode()` with a buffer of `SSID_MAX_LEN * 4 + 1`, and the encoder has cases for `"`, `\`, ESC, `\n`, `\r`, `\t`.

**Fix:** set `char ssid[129]`, and at line 438 write decoded bytes:
```c
static int hexval(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
	       c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Undo wpa_supplicant's printf_encode() and write the raw name as hex */
static void put_ssid_hex(FILE *f, const char *s)
{
	while (*s) {
		int c = (unsigned char)*s++;
		if (c == '\\' && *s) {
			int e = *s++;
			if (e == 'n') c = '\n';
			else if (e == 'r') c = '\r';
			else if (e == 't') c = '\t';
			else if (e == 'e') c = 033;
			else if (e == 'x' && hexval(s[0]) >= 0 && hexval(s[1]) >= 0) {
				c = hexval(s[0]) * 16 + hexval(s[1]);
				s += 2;
			} else c = e;
		}
		fprintf(f, "%02x", c);
	}
}
```

**Risk:** low. Plain ASCII names produce the same output as now. Test with one real non-ASCII network.

## 10. CLEAR: more than 8 display modes means a modeset from uninitialised memory
**File:** `rootfs/stage1/s1ui.c:162-177`

**Issue:** `count_modes` is clamped to 8 before the second `GETCONNECTOR`. The kernel copies modes only if `count_modes >= mode_count` (`drm_connector.c:3723`, upstream master), so with more than 8 it copies nothing and returns the real count. `c.count_modes < nm` is then false, and the code uses the uninitialised stack array `modes[]` for `MODE_CREATE_DUMB` and `SETCRTC`.

It needs a connected output with more than 8 modes. I did not confirm how many the panel reports, so this may not be reachable on the panel alone.

**Fix:**
```c
unsigned nm = c.count_modes;
struct drm_mode_modeinfo *modes = calloc(nm, sizeof(*modes));
if (!modes)
	continue;
c.modes_ptr = (unsigned long)modes;
c.count_props = 0;
c.count_encoders = 0;
if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.count_modes != nm) {
	free(modes);
	continue;
}
```
Remove the `modes[8]` array and the clamp, and free `modes` after `SETCRTC` on both paths, because `sc.mode` is a copy.

**Risk:** none for 8 or fewer modes. The same mode is still chosen.

## 13. CLEAR: `listener_run` exits 0 on startup failure, and `--prop` is overridden by the file
**File:** `sensord/src/listener.c:191`, `:203`, `:208`, `:211`, and `:197-199`

**Issue:**
- `ret = fsrv_init(...)` leaves `ret` at 0, so all three `goto out` failure paths (attach, listener register, listener init) exit with success. The shipped unit uses `Restart=always`, so it still restarts, but the exit status is wrong for anyone running it by hand or checking it.
- Command-line props are added first, then `fsrv_load_props` replaces matching keys. `gts7xl-sensord-extract:119` always writes `ro.revision`, so `--prop ro.revision=X` is silently ignored once extract has run.

**Fix:**
```c
	ret = fsrv_init(&s, o->dir, fd, o->writable, o->verbose);
	if (ret) { ... }
	ret = 1;
	fsrv_load_props(&s, o->dir);
	for (int i = 0; i < o->n_props; i++)
		fsrv_add_prop(&s, o->props[i].k, o->props[i].v);
```

**Risk:** low. The shipped unit passes no `--prop`, so sensor configuration selection does not change.

## 14. CLEAR: each installer restart leaks about 20 MB of RAM
**File:** `rootfs/stage1/s1ui.c:492-500`, `:540`, `:188-195`

**Issue:** `close_screen` closes the DRM fd but keeps the `scanout` mapping, which holds the dumb buffer alive. `open_fb` then allocates a new `fb` without freeing the old one. That is two buffers of `stride * height` each time the stage 2 installer stops. A `SETCRTC` failure also leaves its mapping behind on each of up to 80 retries. The size assumes a 2800x1752 panel at 4 bytes per pixel, inferred from the UI layout.

After a re-open, `touch_fd` is not checked (`:541`), so a missing touchscreen leaves `wait_tap` waiting forever.

**Fix:** add `static size_t scanout_size;` and set it to `cr.size` after `mmap`. In `close_screen`:
```c
	munmap(scanout, scanout_size);
	scanout = NULL;
	free(fb);
	fb = NULL;
```
Add the same `munmap` when `SETCRTC` fails.

**Risk:** low. `present()` is not called between `close_screen` and `open_fb`.

## 15. CLEAR: the charge log grows without limit
**File:** `rootfs/overlay/usr/local/sbin/gts7xl-charge-log:3-9`, enabled at `rootfs/arch-setup.sh:68`

**Issue:** enabled by default, it starts a new CSV each boot and appends every 5 s with no pruning, roughly 2 MB per day. `gts7xl-early-log.service` already prunes its own logs.

**Fix:** after `mkdir -p /var/log/charge`, add:
```sh
ls -t /var/log/charge/*.csv 2>/dev/null | tail -n +11 | xargs -r rm -f
```

**Risk:** none, unless older logs are wanted. A single long uptime can still grow one file.

## 16. CLEAR: copy-paste and dead code
Remove the dead code, and merge the duplicates when those files are next changed.

- **Flash code duplicated:** `tools/deploy.py:5-18` and `:25-29` duplicate `tools/deployboot.py:5-15` and `:17-21`, including the commands that zero and write the boot partition. A safety fix in one can miss the other. Move them into one shared module. `H` and `re` in `deploy.py` and `time` in `deployboot.py` are unused.
- **Alpine fetch duplicated:** the base download and dependency resolver in `rootfs/stage1/assemble.sh:17-49` and `rootfs/stage2/build.sh:8-38` are the same apart from the package list. Move them into one helper script.
- **`sensord/src/main.c`:** `max_rate` (`:232`, `:247-248`) and `struct stream.done` (`:162`, `:225`) are written but never read. Only the sensor's minimum rate is enforced, not its maximum. Decide whether that is wanted before removing `max_rate`.
- **`rootfs/stage1/s1ui.c`:** the `wait_tap` timeout is always 0 (`:265`, `:287-288`), and `else if (!r)` at `:793` is always true.
- **`tools/fvx.py:56`:** `if t==2: pass` can never run and does nothing.

**Risk:** none for the dead code. For the merges, compare the flash commands line by line, because they write to the boot partition.
