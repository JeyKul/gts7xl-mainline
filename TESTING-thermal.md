# Testing: thermal sensors and throttling

## What changed

1. `gts7xl-thermal-check` (rootfs overlay, `/usr/local/sbin`): lists every
   thermal zone (type, temperature, trip points, governor, bound cooling
   devices), the cooling devices (`cur_state/max_state`) and the cpufreq
   policies, then runs one busy process per core and samples every 2 s.
   Read-only apart from the load. `--emulate` (opt-in) also writes
   `emul_temp` of `cpu4-top-thermal` = 92 C for 15 s and sets it back to 0.
2. `linux-patches/0068` (DT only): thermal zones `batt-thermal`,
   `chg-thermal`, `blkt-thermal` from the three PM8150L thermistors through
   `generic-adc-thermal` (already `=m` in the defconfig), **no trips, no
   polling**, so nothing new can throttle or shut down. `blkt-thermal` gets
   a lookup table that fixes the high reading (below).
3. CPU/GPU trips: **unchanged**, on purpose (below).

### Why the third thermistor read high

`blkt_thm` (PM8150L GPIO4, `ADC5_GPIO4_100K_PU`) is not a 100k NTC on the
100k pull-up like the other two. Samsung's `sec-battery` node
(`kona-sec-gts7xlwifi-eur-overlay-r06.dts`, `io-channels` 0x4d/0x4f/0x55 =
adc-temp/adc-chg-temp/adc-blkt-temp) carries a voltage → temperature table for
each. Run through mainline's 100k table:

| real | batt_thm table | chg_thm table | blkt_thm table |
|---|---|---|---|
| 25 C | 920 mV → reads 25.8 C | 929 mV → 25.4 C | **439 mV → 52.0 C** |
| 40 C | 614 mV → 40.9 C | 621 mV → 40.5 C | 297 mV → 64.3 C |
| 0 C | 1441 mV → 1.2 C | 1446 mV → 0.9 C | 683 mV → 37.3 C |

So batt/chg are right and blkt reads ~25 C too high at room temperature.
`blkt-thermal` maps the ADC driver's value back through Samsung's table
(90 C … -20 C, clamped outside). The raw IIO channel labelled `blkt_thm`
keeps showing the wrong (100k-table) value; nothing in the rootfs reads it
(`gts7xl-charge-control` uses only batt_thm/chg_thm).

### Why the CPU trips are not changed

Mainline sm8250.dtsi already has, for every CPU zone (cpu0-3, cpu4-7
top/bottom): passive 90 C and 95 C (step_wise, 250 ms polling while
passive), critical 110 C; little-core zones cool cpu0-3, big/prime zones cool
cpu4-7. GPU zones: passive 85 C → GPU devfreq, critical 110 C. That already
is "start throttling around 85-90 C". Samsung is more conservative (big cores
capped at 75 C, isolate at 110 C, GPU step at 95 C); Qualcomm's own junction
limits are 115/125 C. Battery/skin limits are not in Samsung's DT either (sec
battery driver + thermal HAL), and here they stay in `gts7xl-charge-control`
(38/50 C battery, 60/80 C charger) and `gts7xl-power` (skin 39/41/46 C).
What was never checked is whether the CPU cooling actually engages; that is
what the test below shows. Only if it doesn't is a DT change justified.

## Test

Kernel: build with 0068 (DTB change only; no new module needed, but
`generic-adc-thermal.ko` must be installed — it is, it's `=m` already).
Script: `sudo install -m755 rootfs/overlay/usr/local/sbin/gts7xl-thermal-check /usr/local/sbin/`.

1. Sensors (no load):

       gts7xl-thermal-check --list | grep -A0 -E 'batt|chg|blkt'
       grep . /sys/bus/iio/devices/*/in_temp*_label    # old raw channels, for comparison

   **Success:** three zones `batt-thermal`, `chg-thermal`, `blkt-thermal`;
   at room temperature, idle, all three within a few degrees of each other
   (blkt ~25-35 C, no longer ~50 C). batt-thermal equals what
   `gts7xl-charge-control` logs as `batt=`.

2. Throttling (tablet on a table, battery > 50 %, cover open):

       sudo systemctl stop gts7xl-power      # its skin limiter would also cap clocks
       sudo gts7xl-thermal-check --duration 300 | tee /tmp/thermal.txt
       sudo systemctl start gts7xl-power

   Watch the hottest-zone column. **Success:** either the CPUs reach 90 C and
   `cpufreq cooling engaged: yes` with the limit MHz dropping for the policy
   of that zone, temperatures holding near 90-95 C; or they plateau below 90 C
   ("not exercised", nothing wrong — report the plateau). **Failure:** "passed
   its passive trip but no cpufreq cooling state rose".

3. If step 2 never reaches 90 C, check the cooling path without heat:

       sudo gts7xl-thermal-check --emulate --duration 40

   **Success:** after "emulated at 92 C", `cpu4=`/`cpu7=` states rise above 0
   and the limit of policy4/policy7 drops; after "emulation off" they return
   to 0 within a few seconds. The script writes `emul_temp` back to 0 even on
   Ctrl-C; to be sure afterwards:
   `cat /sys/class/thermal/thermal_zone*/emul_temp` should be all 0 (a reboot
   also clears it).

Send `/tmp/thermal.txt` (peak temperature, whether cooling engaged).

## Revert

Drop 0068 from the build (DTB only) and delete
`/usr/local/sbin/gts7xl-thermal-check`. Nothing else was changed on the
system.
