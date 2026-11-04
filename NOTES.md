# Notes

Short dated findings, newest first. Status lives in README.md.

## 2026-11-04
- sensord step 5: stock soc0 values read in TWRP (platform_subtype `strange`, subtype_id 2; served guesses were wrong, fixed in the serve dir). With writes allowed, temp.json renames succeed (49) and sns_reg_version is written, no sns_registry call seen. Same crash ~41 s in: EF:sensor_process UTMR_CLNT_1 0x107c sns_thread_ma; remoteproc recovered the SLPI.
- Panel mode 1 default, msm loads first: boot clean in log. sensord steps 1-4: tests pass, SLPI boots with the `&slpi` node (local commit, becomes 0043), trace: 59 vendor configs, 193 registry entries, soc0 and ssc_hw_rev read; denied writes to temp.json and sns_reg_version retried; sensor_process crashed about 41 s after start (EF:sensor_process UTMR_CLNT_1 sns_thread_ma). Trace in research/sensord (local).
- Panel handoff test, 5 boots (modes 1,1,2,2,0): all reach desktop, no DSI errors. Only boot 2 (mode 1) clean by eye; the others showed one glitch line at the start. Log: research/display/handoff-test.txt (local).
- Reviewed branches. `panel-handoff`: patches apply and build, installed for testing (backups in /var/lib/*.before-handoff), boot test pending. `sensord`: reads stock partitions read-only, implements the `sns_registry` interface hexagonrpcd lacked; untested. Both add a patch 0040: renumber on merge.
- CLAUDE.md added. Optional modules come from the `gts7xl` pacman repo (`modules-repo/`).
