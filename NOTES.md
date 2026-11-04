# Notes

Short dated findings, newest first. Status lives in README.md.

## 2026-11-04: USB-C and DP research (branch usbc-dp, nothing run on the tablet)
- Plan and protocol in docs/usbc-dp-research.md. Patches 0050-0052 are untested.
- Found in stock: PS5169 redriver at i2c14 0x28 on gpio40/41, which our DT reserves. Needed for USB3 and DP, not USB 2.0.
- Tab S8+ port (aaronsb/sm-x800-linux) has a working MAX77705 Type-C driver: source attach needs opcode 0x55 data 3.

## 2026-11-04
- SLPI sensor bisect (configs and registry entries removed, 60-70 s per run): crash at t+10 s with every set except bridge_driver_0 + pah_8011 + veml3328 all removed (no crash, twice). Any two of those still crash. With all three gone: no crash but `--list` shows 0 sensors. Backup of the original registry and configs on the tablet: /var/lib/gts7xl-sensord-backup; restored afterwards.
- SLPI crash: not unused regulators (regulator_ignore_unused, crash unchanged). Coredump text: `sns_thread_manager.c:437: SEE Sensor exceeded processing time limit`, the thread watchdog; no sensor named in readable strings. Non-verbose --serve: crash unchanged. --list at t+4 s: SSC up (node 9 port 13), only `registry` listed, crash before t+8 s. Dump: research/sensord/slpi.core (local, 21 MB).
- sensord step 5: stock soc0 values read in TWRP (platform_subtype `strange`, subtype_id 2; served guesses were wrong, fixed in the serve dir). With writes allowed, temp.json renames succeed (49) and sns_reg_version is written, no sns_registry call seen. Same crash ~41 s in: EF:sensor_process UTMR_CLNT_1 0x107c sns_thread_ma; remoteproc recovered the SLPI.
- Panel mode 1 default, msm loads first: boot clean in log. sensord steps 1-4: tests pass, SLPI boots with the `&slpi` node (local commit, becomes 0043), trace: 59 vendor configs, 193 registry entries, soc0 and ssc_hw_rev read; denied writes to temp.json and sns_reg_version retried; sensor_process crashed about 41 s after start (EF:sensor_process UTMR_CLNT_1 sns_thread_ma). Trace in research/sensord (local).
- Panel handoff test, 5 boots (modes 1,1,2,2,0): all reach desktop, no DSI errors. Only boot 2 (mode 1) clean by eye; the others showed one glitch line at the start. Log: research/display/handoff-test.txt (local).
- Reviewed branches. `panel-handoff`: patches apply and build, installed for testing (backups in /var/lib/*.before-handoff), boot test pending. `sensord`: reads stock partitions read-only, implements the `sns_registry` interface hexagonrpcd lacked; untested. Both add a patch 0040: renumber on merge.
- CLAUDE.md added. Optional modules come from the `gts7xl` pacman repo (`modules-repo/`).

## 2026-11-04: SLPI crash fixed by sensor rail L8C
- Stock SSC has `sensor_vdd-supply = <&pm8150a_l8>` (1.8 V). Mainline had no ldo8 on pmic c.
- Added `vreg_l8c_1p8` always-on. Full registry + config set: no watchdog crash, SLPI stays running.
- `--list`: accel/gyro lsm6dso, mag ak0991x, gravity, rotv, game_rv, gyro_cal, mag_cal, amd, tilt. Light (veml3328) still missing.

## 2026-11-04: regulator_ignore_unused dropped
- Removed from CONFIG_CMDLINE. Wi-Fi, display, audio, 15 video nodes and 11 SLPI sensors still OK.

## 2026-11-04: rails L4C/L10A/L16A test
- Samsung overlay sets all three always-on. Added them; user saw something break, went to TWRP. Kernel log clean (irq 156 "nobody cared" is old). Reverted; DT kept at research/sensord/dts-rails-l4c-l10a-l16a.dts.
- gpio67 (stock proxy_int, no pull): no effect on veml3328. Reverted.
- Cause of the blank panel: the forced voltages. Readback (rpmh_read) of the bootloader's votes: L10A 2.96 V, L16A 2.704 V, L4C 1.808 V. Forcing 2.9 V/2.8 V blanks the panel. Without min/max the rails read "unknown" and regulator-output can't probe.
- Fix (0045): all three always-on at the bootloader voltages, HPM. Panel OK, no deferred probes, L8C on, SLPI stable, 12 sensors including ambient_light (veml3328_0). Still missing: the second veml3328 (bus 5), rgb, bridge_driver_0 (bus 7, 0x2a), device_orient.

## 2026-11-04: shutdown hang, auto-rotate, rail readback
- iio-sensor-proxy hung 90 s on stop: its own libssc driver (udev sets IIO_SENSOR_PROXY_TYPE=ssc-* on fastrpc-sdsp) waits for hexagonrpcd. 81-gts7xl-sensord-no-libssc.rules clears it; stop takes 28 ms.
- 71-gts7xl-sensord-start.rules was never installed on the tablet, so sensord never autostarted. Installed; fs + accel services start at boot.
- Auto-rotate: sensord --uinput -> iio-sensor-proxy input-accel; monitor-sensor reports all 4 orientations.
- kwin "Atomic commit failed: EBUSY" 2-22 per boot, only during startup/use, 0 when idle. Not the rails. Open.
- Readback of undefined rails (name-only nodes for L1/3/7/11/15/18A, L1/2/3/6/9/10/11C, +/- BOB): both boots had a dark panel, and SSH didn't answer on the second. Defining these nodes is not side-effect free. Dropped. DT kept at research/sensord/dts-readback-allrails.dts.

## 2026-11-04: trackpad test C (live SLPI vs stopped)
- 60 s each, finger moving, /dev/input/event5 SYN_REPORT gaps >150 ms (research/../gaps.py logic).
- Run 1: serving 2854 frames, 12 gaps, 8 s stalled (max 2.7 s) | stopped 721 frames, 43 gaps, 47 s stalled (max 5.4 s).
- Run 2 (reversed): stopped 1076 frames, 49 gaps, 42 s (max 5.7 s) | serving 2242 frames, 23 gaps, 31 s, but one gap was 22 s (~9 s without it).
- Supports cause 1 (MCU blocks on a silent SLPI) in both orders. Trackpad still stalls with the SLPI serving: bridge_driver_0 (bus 7, 0x2a, the likely lid-angle link) never probes. Next: get bridge_driver_0 up.

## 2026-11-04: light, rotation, tablet mode, trackpad axes
- Trackpad: raw x/y are already right (0046). The old rotation was wrong.
- Light: veml3328 reacts (8 to 3200 lux). kwin 6.8 uses iio-sensor-proxy ClaimLight. New ssc-light-bridge IIO device (0047) plus gts7xl-sensord-light service (stream -> in_illuminance_input every 0.5 s). monitor-sensor --light shows real values. Restart=always: at boot the SSC registers light late.
- Rotation: landscape with the camera on top reads +g on sensor X. ACCEL_MOUNT_MATRIX "0, 1, 0; -1, 0, 0; 0, 0, 1" gives normal.
- Tablet mode: SW_TABLET_MODE switch on the pogo driver (0048), = !conn, 2 s debounce. Detach -> 1, attach -> 0, tested.

## 2026-11-04: brightness floor, installer, docs
- 0% brightness nearly blanked the panel: 0049 maps 0..2047 onto min_brightness (64)..2047. User: 0% now fine.
- KWin learned autoBrightnessCurve [[0,1]] (0 lux -> 100%) and stuck at 100%. Fixed by editing it with sddm stopped (KWin rewrites on exit).
- Installer now builds and installs sensord (gcc only during the build), adds iio-sensor-proxy, enables gts7xl-sensord-setup (extract once + start the SLPI). Extractor defaults set to the tested strange/2/65536/r06.
- Docs: docs/sensors.md, docs/keyboard-cover.md, display.md brightness; README status rows updated.

## 2026-11-04: S Pen Pro reconnect loop
- Not Bluetooth instability. The pen's bond went stale: the pen answers its button characteristic (9ee7ba08-...) with ATT Insufficient Authorization, and the daemon retried Connect every 5 s ("Operation Not Authorized").
- SMP re-pairing alone isn't enough: the pen only authorizes the button service for a bond made in pairing mode (Pro: hold the button; built-in pen: charge toggle).
- gts7xl-spen now drops the bond after 3 auth failures, toggles the charger before pairing, and re-claims the default agent. User held the button: connected, battery 20%.

## 2026-11-04: pen calibration
- gts7xl-pen-calibrate (GTK4, 4 targets) fitted x -16..26732, y -5..16703 (was 0..26750 x 0..16700). Applied as /etc/udev/hwdb.d/61-gts7xl-pen.hwdb; needs python-cairo.
- 3x3 recalibration: x 4..26754, y 10..16705. Edge residuals up to ~9-13 px with mixed signs (bottom corners opposite in y, right edge mid -13 x): non-linear, an affine matrix does not help. Hover fixed (0050): 0x80 in range, 0x10 tip, 0x01 leave.

## 2026-11-04: bad DT in 0046 (fixed)
- My `git commit -a` for 0046 swept in the uncommitted readback rail nodes (rb_*), which leave the panel dark/green and stop regulators-0 probing (ldo14/ldo9 suppliers missing). Every DTB built from the tree since had them; my test images used a saved DTB, so it showed only when the usbc-dp test rebuilt the DTB. 0046 regenerated without the DT part.
