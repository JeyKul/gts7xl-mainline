# Suspend / deep sleep (SM8250 mainline)

State: s2idle works; user measured ~2 %/h over 15h46 (79→48 %), ~0.8 W (TODO.md). Stock Android deep idle is typically ~0.1–0.2 W (guess). Target: <0.25 W s2idle, then `mem` (S3) if firmware allows.

## What mainline has
- PSCI + cpuidle with domain idle: `idle-states` (silver 0x40000004, gold 0x40000004), `domain-idle-states cluster_sleep_0` 0x4100c244, per-CPU `cpu_pd0-7`, `cluster_pd`; PSCI uses OSI (`power-domains` on CPUs). [sm8250.dtsi:340-375, 695-750](https://github.com/torvalds/linux/blob/v7.3/arch/arm64/boot/dts/qcom/sm8250.dtsi#L340)
- Samsung lpm-levels equal values: L3 `llcc-off` psci 0xC24, entry 3263 / exit 6562 / min-res 9987 µs; CPU `rail-pc` mode 4 [kona-pm.dtsi](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/vendor/qcom/kona-pm.dtsi). Matches mainline numbers, so cpuidle tables are fine.
- Needed config (check `config/gts7xl_defconfig`): `CONFIG_ARM_PSCI_CPUIDLE`=y (present, L98), `CONFIG_ARM_PSCI_CPUIDLE_DOMAIN`, `CONFIG_PM_GENERIC_DOMAINS`, `CONFIG_QCOM_RPMH`, `CONFIG_QCOM_RPMHPD`, `CONFIG_QCOM_COMMAND_DB`, `CONFIG_INTERCONNECT_QCOM_SM8250`, `CONFIG_QCOM_STATS` (deep-sleep counters). Verify all =y/m.
- RPMh sleep/wake votes: apps_rsc (sm8250.dtsi:6208) flushes sleep/wake TCS on last-CPU PM; AOSS sleep needs *all* masters (ADSP, CDSP, SLPI, modem-less) not blocking.

## Likely blockers (ordered by suspicion)
1. Remoteprocs held awake: ADSP (audio, CS35L41 amps with runtime PM **off** per TODO), SLPI (sensord streams constantly at 20 Hz accel + light every 0.5 s → SLPI/SSC never idles, and each sample wakes AP via FastRPC/uinput).
2. Always-on rails forced on (L4C/L10A/L16A/L8C, haptic, L5C mic rail — always on per TODO) and un-gated WLAN (QCA6390 PCIe, no ASPM L1ss/BT UART).
3. Display path: panel off but DSI/MDP clocks or `msm` power domain on; check `/sys/kernel/debug/pm_genpd/pm_genpd_summary`.
4. Interconnect votes left at peak by drivers without `icc_set_bw(0)` on suspend (venus off, GPU `a6xx` idle; check `/sys/kernel/debug/interconnect/interconnect_summary`).
5. UFS: auto-hibern8 / link state; `ufshcd` runtime PM; microSD.
6. PCIe0 (Wi-Fi): suspend with link in L1ss; QCA6390 `ath11k` WoWLAN config.
7. No LLCC/DDR deep sleep without RPMh sleep sets → check DDR stats.

## Measuring (fuel gauge)
- Gauge: MAX77705. `/sys/class/power_supply/battery/{current_now,voltage_now,capacity,charge_counter}`; README notes `current_now` scale not confirmed (TODO) → calibrate: run a known load (screen on, fixed brightness) and compare against `upower` energy-rate, or watch `charge_counter` delta.
- Script: unplug USB, screen off, `rtcwake -m freeze -s 600`; log `charge_counter` + `/sys/kernel/debug/qcom_stats/*` + `/sys/power/suspend_stats` before/after; ≥3 runs, 15 min each (gauge is noisy at mA scale).
- Idle counters: `cat /sys/kernel/debug/qcom_stats/{aosd,cxsd,ddr,adsp,cdsp,slpi}` (aosd = AOSS sleep, cxsd = CX collapse). If `aosd` count doesn't increase across a suspend: whole SoC isn't reaching deepest state. `cat /sys/devices/system/cpu/cpu*/cpuidle/state*/usage` for CPU/cluster.
- `/sys/kernel/debug/wakeup_sources`, `/sys/power/pm_wakeup_irq`, `echo 1 > /sys/power/pm_debug_messages`, `pm_print_times`; `dmesg | grep -i "PM:.*failed\|suspend.*abort"`.
- Baseline split: (a) kill sensord + `rmmod` Wi-Fi + stop BT, measure; (b) add back each; delta = cost.

## Staged plan
1. Baseline: 10-min `rtcwake` freeze with everything on, record mA. *Test*: repeatable ±10 %.
2. Enable `QCOM_STATS`, read `aosd/cxsd/ddr` before/after. *Test*: counters increment (= reaching deepest SoC state). If no → blockers 1-7.
3. Stop `gts7xl-sensord-*` services on suspend (systemd `sleep.target` hook, pre/post) – or lower SSC sample rate to wake-up-only. *Test*: mA drop; sensors resume after.
4. Unload/runtime-suspend Wi-Fi & BT on suspend (`rfkill`, `ath11k` WoW off). *Test*: mA drop.
5. ADSP: allow cs35l41 runtime PM again if firmware hibernate bug fixed (TODO notes -ENOMSG). Alternatively power amps off via `pcm` close + keep runtime PM off only while playing.
6. Un-pin non-essential always-on rails ONLY with voltages from docs/sensors.md rule (no round numbers). Do not touch panel-related L10A/L16A/L4C. L5C tie-to-capture (TODO) is safe-ish: check its stock voltage in Samsung DT first.
7. Try `mem_sleep_default=deep` (check `/sys/power/mem_sleep`); on Qualcomm usually only s2idle is exposed (PSCI SYSTEM_SUSPEND not implemented on most Qualcomm TZ) — expect s2idle only (guess).
8. Fix resume-black-screen (TODO "wakes, screen stays black until reboot on firmware framebuffer"); not related to drain but blocks daily use.

## Risks
- Rail changes → blank panel (see sensors.md). Only change rails by readback, one per boot, with TWRP access ready.
- sensord stop during suspend may crash SLPI (watchdog) on resume: restart service, don't re-boot DSP.
