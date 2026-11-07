# Thermal: CPU/GPU zones, boost, throttling

Goal (owner): CPU/GPU boost/throttle only from ~85–90 °C; skin/battery stay near Samsung's.

## Mainline v7.3 (sm8250.dtsi)
- 24 zones: `cpu0-3-thermal` (tsens0 1-4), `cpu4-7-top/bottom` (tsens0 7-14), `cluster0/1` (5,6), `gpu-top` (tsens0 15), `gpu-bottom` (tsens1 8), `aoss0/1`, `wlan`, `video`, `mem`, `q6-hvx`, `camera`, `compute`, `npu`. [sm8250.dtsi:6326+](https://github.com/torvalds/linux/blob/v7.3/arch/arm64/boot/dts/qcom/sm8250.dtsi#L6326)
- CPU trips already: passive **90 °C** (hyst 2), passive **95 °C**, critical **110 °C**; both passive trips cool `cpu0-3` (little) with `THERMAL_NO_LIMIT`; CPUs have `#cooling-cells`, `dynamic-power-coefficient`, cpufreq-hw (`qcom,sm8250-cpufreq-epss`, L6297). polling-delay-passive 250 ms.
- No `lmh` node on SM8250 mainline. mainline `drivers/thermal/qcom/lmh.c` compatibles: sdm845, sc8180x, sm8150 only → LMh (hardware limits) not driven. Hardware keeps its own fail-safe limits on its own; Linux throttling is software only.
- GPU: `gpu` has `#cooling-cells`/devfreq cooling (a6xx opp table); check `gpu-top-thermal` has trips/cooling-maps in the tree (grep `gpu-top-thermal`); many boards don't wire GPU cooling (guess).
- Board DT (this repo): PM8150L battery/charger thermistors added (0022); `batt_thm` real, fuel-gauge temp is fake (TODO). `gts7xl-charge-control` keeps charge thermal limits.

## Samsung (kernel + DT)
- Qualcomm vendor zones: `*-usr` zones: passive **115 °C**, **125 °C** (hot/reset type listed as passive in dump); `cpu-1-N-step` (big): cpufreq cap at **75 °C** (`cpu7_notify`), isolate cpu4 at **110 °C**; `cpu-0-N-step` (little): 110 °C; `gpuss-max-step` **95 °C**; `ddr-step` 100; `video-step` 100 °C. [kona-thermal.dtsi:178+, 759, 847, 678](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/vendor/qcom/kona-thermal.dtsi#L847)
- Samsung overlay: PMIC die zones (`pm8150[b/l]_tz`) 95/115/145 °C; SoC isolate maps (cpu4-7) + battery-current-limit (`pm8150b-bcl-lvl0/1`) cooling big cores and GPU by VBAT [kona-thermal-overlay.dtsi](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/vendor/qcom/kona-thermal-overlay.dtsi). LMh nodes: `qcom,limits-dcvs`, `lmh_isense_cdsp`.
- **Skin/battery limits** (Samsung's real user-comfort throttling) are NOT in DT: they live in userspace (`thermal-engine`/Samsung `sec thermal` HAL config, partition vendor; guess). Pull `/vendor/etc/thermal-engine*.conf` / `/vendor/etc/sec_thermal*` from the tablet's own vendor partition (read-only) to get exact skin trips. **Action item**.

## Is 85–90 °C passive start safe?
- Silicon: Qualcomm itself caps CPU junction trips at 115 °C passive / 125 °C critical on this very SoC; tsens readings are junction-ish. 85–90 °C throttle start is well inside that. ✅ for the chip.
- Not safe for everything else: the tablet is passively cooled, SoC at 90 °C ⇒ back-glass/skin >45–50 °C and battery heating. Li-ion: charging above ~45 °C damages; discharge >60 °C degrades. Keep Samsung-like skin/battery trips as the *binding* constraint (guess: skin ≈ 40–43 °C soft, 46–48 hard in Samsung's conf).
- Samsung's own 75 °C big-core cap shows stock is much more conservative; going 85–90 means more heat/less battery, not hardware death.

## Recommended values (with sources)
CPU zones (each of 12 cpu* zones, **step_wise**, polling 250 ms passive, 1000 ms idle):
- `passive0` **85 °C**/hyst 3: cap big (cpu4-7) to ~1.8 GHz-class OPP (via cooling state), little unlimited. (mainline uses 90; Samsung big step 75)
- `passive1` **90 °C**/hyst 3: big cooling to ~1.4 GHz, little capped ~1.4 GHz.
- `hot` **100 °C**: all cores to lowest OPP.
- `critical` **110 °C** (mainline value, below Qualcomm 115/125): poweroff.
- Cooling maps must name **cpu4-7** too: mainline maps only cpu0-3 for cpu0-3 zones; verify cpu4-7 zones map to cpu4-7 (they do in upstream: `cpu4_alert0` etc.; check tree).
GPU (`gpu-top` + `gpu-bottom`): passive **90 °C** cooling `&gpu` (devfreq cooling, states per OPP), second **95 °C** (matches Samsung `gpuss-max-step` 95), critical 110. Needs `gpu` `#cooling-cells=<2>` + `CONFIG_DEVFREQ_THERMAL` (=y in defconfig L771).
Boost "from 85–90": Linux has no boost knob tied to temperature; the clean implementation is **no cap below the first trip**, i.e. leave cpufreq/devfreq max OPP (cpufreq policy max 2.84 GHz big prime) until 85 °C. Governor: `power_allocator` (enabled in defconfig L769) wants `sustainable-power`, `control` trip and DPC coefficients – nicer ramp than step_wise but tuning risk; start with step_wise.
Skin/battery/charger (keep conservative):
- `batt_thm`: keep `gts7xl-charge-control` limits (stop/limit charge ≥ 45 °C, hot-stop ~50 °C – confirm existing values in `rootfs/gts7xl-charge-control`); add DT trip **passive 43 °C** → cap CPU/GPU one step; **critical 60 °C**.
- Third thermistor "reads high" (README): identify (xo_therm/quiet_therm/pa_therm?) via `/sys/class/thermal/*/type` & `/sys/bus/iio/devices/*`; compare with ADC offset; **don't** use it as a trip until verified.
- Add Samsung-equivalent `skin` virtual sensor in userspace (a small daemon like thermald: mean of `quiet`/`xo` thermistors) and write cooling caps via `cpufreq` sysfs.

## LMh
- Hardware on SM8250 (Samsung `qcom,limits-dcvs`, `isens_vref` on pm8150_l5_ao/l12_ao, `lmh_isense_cdsp`; kona-thermal.dtsi:48-60,172). Mainline: no SM8250 LMh support (only sdm845/sc8180x/sm8150 compat). Without it the SoC's *hardware* limits (current/thermal) are firmware-default; no software action needed. Do not try to port LMh first – no payoff.
- BCL (VBAT low) is PMIC (PM8150B) and Samsung maps bcl lvl0/1 → CPU isolate+GPU cap. Mainline has no `bcl` driver for pm8150b (`qcom,bcl`... guess). Risk: brownouts at low battery with unrestricted boost → keep power-domain behavior safe: add userspace cap when battery <15 % (sensord/UPower).

## Staged plan
1. Dump zones: `for z in /sys/class/thermal/thermal_zone*; do echo $(cat $z/type) $(cat $z/temp); done`; stress `stress-ng --cpu 8`, log max temps, battery & skin side temp (IR thermometer on back glass) for 10 min. *Test*: record plateau without any throttle changes.
2. Pull Samsung skin/battery config (read-only, from tablet's vendor partition). *Test*: values documented.
3. Add trips above (CPU only) in board DT; check `cat /sys/class/thermal/thermal_zone*/trip_point_*_temp`. *Test*: stress → freq drops at ~85, `cur_state` of cooling devs increases; temp holds <95.
4. Add GPU maps. *Test*: glmark2/vkmark loop → GPU freq caps at 90.
5. Add skin cap in userspace (43 °C). *Test*: 20 min stress; back-glass ≤ 43–45 °C by IR.
6. Optional: power_allocator.

## Risks
- Higher temp ceiling → battery/glass heat, UFS/PCIe (Wi-Fi) throttle behavior; faster battery aging.
- Wrong zone→cooling map = thermal runaway to critical (poweroff at 110 °C is the net). Test with `stress-ng` while watching, battery ≥50 %.
- tsens sensor offsets unknown on this unit (third thermistor high) – treat all absolute numbers as ±5 °C.
