# Suspend drain: findings so far

Device: Tab S7+ (SM8250), kernel 7.3.0-gts7xl #85, Arch, KDE PowerDevil. Notes handed over 2026-11-08 from the parallel research session (drain logger on branch `suspend-drain`).

## Symptom
- `mem_sleep` = `[s2idle]` only, no deep.
- Suspend "succeeds" (suspend_stats success=5, fail=0) but draws -377 to -766 mA asleep; battery 33 -> 27 % fast.
- qcom_stats in every snapshot: aosd, cxsd, ddr Count 0 (all DDR LPM counts 0); apss, display, gpu, modem empty. adsp 2, cdsp 57, slpi 2653, slpi_island 1607.
- So the remote subsystems sleep, but the apps side never reaches AOSS deep sleep: CX/MX/DDR never collapse.

## Ruled out
1. PSCI/cpuidle: PSCIv1.1 with OSI mode; "failed to set PC mode: -3" is benign (firmware is OSI only). CPU PM domain topology in OSI mode.
2. Idle states: silver/gold rail power collapse present, matching mainline sm8250.dtsi (0x40000004, cluster 0x4100c244).
3. Wakeup storm: a 60 s rtc-alarm sleep woke only on the alarm; IRQ deltas tiny (9091000.pmu +9, 980000.i2c +6, 8804000.mmc +2). aosd/cxsd still 0.
4. cmd-db: debugfs dump is empty, but rpmhpd (mx/cx/mmcx with performance states) probes, which needs cmd-db. Treat the empty dump as a debugfs quirk.

## genpd while awake
- on: gpu_gx/gpu_cx, mdss, mmnoc TBUs, usb30_prim, ufs_phy, ufs_card, pcie_0 (ath11k); mx 48, mmcx 256, cx 48 (998000.serial, a84000.i2c).
- off: lcx, lmx, cx_ao, mx_ao, mmcx_ao, gfx, ebi, camera/video GDSCs. adsp, cdsp, slpi running.

## Also seen
- RCU stall during s2idle (CPUs 4-7, pc in cpuidle_enter_s2idle), possibly noise.
- Runtime noise: 980000.i2c ~116k IRQs; `geni_i2c 994000.i2c: error turning SE resources: -13` on resume; `samsung-stm32-pogo 9-002a: header write failed: -13`; dmesg ring wraps in seconds.

## Hypothesis
Everything up to the RPMh sleep vote works, but something keeps an active CX/MX (or DDR/BCM) vote through suspend, so AOSS never applies the sleep set. Suspects:
1. `clk_ignore_unused` on the cmdline.
2. Running remoteprocs (adsp/cdsp/slpi) holding CX votes.
3. GPU GMU or display not dropping their rpmhpd vote.
4. PCIe Wi-Fi (ath11k) keeping pcie_0 and CX up.
5. Apps RSC sleep/wake TCS never used (solver not running).

## Next tests (rerun the 60 s sleep test after each, watch aosd/cxsd)
- Remove `clk_ignore_unused` if present.
- `log_buf_len=4M`, then grep the boot log for cmd-db, rpmh, rsc, aoss, psci, cpuidle.
- Stop remoteprocs one at a time.
- Wi-Fi down / `rmmod ath11k_pci`.
- Right after a sleep: pm_genpd_summary and regulator_summary.
- rpmh trace (`events/rpmh/enable`): no sleep/wake TCS writes means the solver never runs; writes with aosd still 0 means a vote stays high (find the address).
- Compare apps_rsc and its TCS config with mainline sm8250.dtsi.

## Sleep test (root)
```sh
Q=/sys/kernel/debug/qcom_stats
for s in aosd cxsd ddr; do echo $s; cat $Q/$s; done
cp /proc/interrupts /tmp/irq.before
echo +60 > /sys/class/rtc/rtc0/wakealarm
echo mem > /sys/power/state
echo "woke on irq: $(cat /sys/power/pm_wakeup_irq)"
for s in aosd cxsd ddr; do echo $s; cat $Q/$s; done
diff /tmp/irq.before /proc/interrupts
```
