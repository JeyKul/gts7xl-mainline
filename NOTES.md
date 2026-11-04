# Notes

Short dated findings, newest first. Status lives in README.md.

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
