# Sensors

On the Tab S7+ the motion, compass and light sensors are not wired to the CPU. They sit on I2C/I3C buses owned by the sensor DSP (SLPI), and the only way to read them is to ask the DSP. This page explains how that works on mainline Linux, how to set it up, and what is still missing.

## What works

| Sensor | Chip | Bus (DSP side) | State |
|---|---|---|---|
| Accelerometer + gyroscope | ST LSM6DSO | I3C 1 | ✅ auto-rotate through iio-sensor-proxy; gyro as IIO `ssc-gyro` (0072, tested 2026-11-08) |
| Magnetometer | AKM AK0991x | DSP | ✅ listed, streams; raw as IIO `ssc-magn`, heading as `ssc-compass` → iio-sensor-proxy Compass (0072, tested 2026-11-08) |
| Ambient light | Vishay VEML3328 (instance 0) | I2C 4, 0x10 | ✅ auto-brightness through iio-sensor-proxy |
| Virtual: gravity, rotation vector, game rotation vector, gyro/mag calibration, motion detect, tilt | DSP algorithms | | ✅ listed |
| Second light sensor (veml3328_1) | VEML3328 | I2C 5, 0x10 | ❌ never probes |
| RGB channel | VEML3328 | | ❌ not listed |
| `bridge_driver_0` (probably the keyboard cover lid-angle link) | unknown | I2C 7, 0x2a, irq 59 | ❌ never probes |
| `device_orient` | DSP algorithm | | ❌ not listed (not needed, see below) |
| Hall sensor for the cover | gpio157 | CPU | 🟡 lid switch, no reaction to the keyboard cover |

Check on the tablet with `sudo gts7xl-sensord --list`. Stream with `sudo gts7xl-sensord --stream accel` (also `gyro`, `mag`, `rotv`, `ambient_light`).

## How it fits together

```
 SLPI DSP  ──QRTR──>  gts7xl-sensord --uinput  ──>  uinput accelerometer  ──>  iio-sensor-proxy  ──>  KWin / Mutter / Phosh
   ^  │                gts7xl-sensord-light     ──>  ssc-light-bridge (IIO)  ──>        "              ──>  auto-brightness
   │  │                gts7xl-sensord-motion    ──>  ssc-motion-bridge (IIO: ssc-gyro, ssc-magn, ssc-compass ──> iio-sensor-proxy Compass ──> GeoClue)
   │  └─FastRPC──>    gts7xl-sensord --serve   (answers the DSP's file and registry requests)
   remoteproc boots it with the tablet's own slpi.mbn
```

1. **Kernel** (`linux-patches/0043`, `0044`, `0045`): enables the SLPI remoteproc and FastRPC, and keeps the sensor power rails on (next section).
2. **Firmware and config**: `gts7xl-sensord-extract` copies `slpi.mbn` and the sensor registry/config from this tablet's own stock partitions, read-only. Nothing is redistributed. It runs once, from `gts7xl-sensord-setup.service`, then starts the DSP.
3. **`gts7xl-sensord --serve`** (`gts7xl-sensord-fs.service`): while booting, the DSP asks the CPU for files (registry, configs, `soc0` platform info) over a reverse FastRPC tunnel. Android's `sscrpcd` normally does this; sensord does it from `/var/lib/gts7xl-sensord`.
4. **`gts7xl-sensord --uinput`** (`gts7xl-sensord-accel.service`): streams the accelerometer at 20 Hz into a uinput device called `gts7xl-sensord accelerometer`. A udev rule tags it `ID_INPUT_ACCELEROMETER` with the measured mount matrix, and iio-sensor-proxy's input-accel driver works out the orientation.
5. **`gts7xl-sensord-motion`** (`gts7xl-sensord-motion@{gyro,mag,compass}.service`, `linux-patches/0072`): the same idea for the motion sensors. `ssc-motion-bridge` makes three IIO devices:
   - `ssc-gyro`: `in_anglvel_{x,y,z}_input` in rad/s, from the SSC `gyro` sensor (as the DSP reports it; `gyro_cal` bias not applied).
   - `ssc-magn`: `in_magn_{x,y,z}_input` in gauss (SSC `mag` µT / 100; `mag_cal` bias not applied).
   - `ssc-compass`: `in_rot_from_north_magnetic_tilt_comp_raw` in millidegrees (scale 0.001) plus a buffer. The heading comes from the DSP's fused rotation vector (`rotv`, which uses its own magnetometer calibration): the direction the top edge (camera edge in landscape, sensor +X) points, clockwise from magnetic north. iio-sensor-proxy's `iio-buffer-compass` driver reads the buffer; its Compass D-Bus interface is meant for GeoClue. `rotv` streams only while a reader has the buffer enabled.
   Gyro and magnetometer stream all the time (10 Hz requested, or the DSP's minimum if higher) and are written at most 10 times a second. No desktop reads a gyroscope; any IIO reader (`iio_info`, `iio_attr`, plain `cat`) can. To turn one off: `sudo systemctl mask gts7xl-sensord-motion@gyro`.
6. **`gts7xl-sensord-light`** (`gts7xl-sensord-light.service`): streams ambient light and writes the lux value into `ssc-light-bridge` (`linux-patches/0047`), a tiny IIO device with a writable `in_illuminance_input`. iio-sensor-proxy treats it like any IIO light sensor.
7. udev starts all of this whenever `/dev/fastrpc-sdsp` appears (`71-gts7xl-sensord-start.rules`), so it also recovers after a DSP restart.

No Android userspace runs: no hexagonrpcd, no libssc, no sensors HAL.

## Power rails

The DSP crashed with `SEE Sensor exceeded processing time limit` until the sensors were powered. Samsung's DT turns these rails on:

| Rail | Voltage | Why | Patch |
|---|---|---|---|
| L8C (`pm8150a_l8`) | 1.8 V | `sensor_vdd` of Samsung's `qcom,ssc` node; without it the DSP watchdog fires | 0044 |
| L4C (`pm8150a_l4`) | 1.808 V | always-on in Samsung's DT; the light sensor needs one of these three | 0045 |
| L10A (`pm8150_l10`) | 2.96 V | " | 0045 |
| L16A (`pm8150_l16`) | 2.704 V | " | 0045 |

**Rule: only use voltages read back from the bootloader or taken from Samsung's DT, never round numbers.** Forcing L10A to 2.9 V and L16A to 2.8 V (both inside Samsung's ranges) blanked the panel. These rails probably also feed the panel or touch, and over-volting them could damage hardware. To read what the bootloader set: define the rail with only `regulator-name`, boot, and read `/sys/class/regulator/*/microvolts` (the RPMh driver reads the current vote back). Then pin exactly that value.

Defining extra rails is not free: a test that defined every unused PM8150/PM8150L LDO (name only, no votes) left the panel dark. That is why the bus 5 and bus 7 sensors are still off.

## iio-sensor-proxy

- Arch's iio-sensor-proxy also has a libssc driver that talks to the DSP itself, through hexagonrpcd. Without hexagonrpcd it hangs, and shutdown waits 90 s for it. `81-gts7xl-sensord-no-libssc.rules` switches that driver off, so sensord stays the only client.
- Accelerometer mount matrix: in landscape with the front camera on top, the sensor reads +g on its X axis. The matrix is `0, 1, 0; -1, 0, 0; 0, 0, 1` (in `70-gts7xl-sensord.rules`).
- Test: `monitor-sensor --accel` and `monitor-sensor --light`. Run them from the desktop session or with sudo; over SSH the D-Bus claim is refused.

## Desktop notes

- **KDE Plasma**: auto-rotate defaults to "only in tablet mode" (`autoRotation: InTabletMode` in `~/.config/kwinoutputconfig.json`). Tablet mode comes from the keyboard cover's switch, see [keyboard-cover.md](keyboard-cover.md). KWin learns the auto-brightness curve from your slider changes. If it learns a bad point, for example 0 lux → 100% at boot before the sensor reports, brightness sticks at 100%. Fix it by editing `autoBrightnessCurve` while KWin is not running (KWin rewrites the file on exit). For example: stop sddm from a console, set `[[0,0.05],[10,0.25],[50,0.45],[200,0.7],[1000,1.0]]` (lux, brightness fraction), then start sddm again.
- **GNOME and Phosh** use the same iio-sensor-proxy interface for rotation and automatic brightness, so the same stack should work there. Not tested yet.

## Troubleshooting

| Symptom | Check |
|---|---|
| `--list` shows only `registry` | `cat /sys/class/remoteproc/*/state`, `journalctl -u gts7xl-sensord-fs`. Is `/var/lib/gts7xl-sensord/props` there? If not, run `sudo gts7xl-sensord-extract`. |
| DSP restarts every ~40 s | A sensor rail is off. Check `/sys/class/regulator/*/name` for `vreg_l8c_1p8` enabled. |
| No rotation | `systemctl status gts7xl-sensord-accel`, then `udevadm info` on the `gts7xl-sensord accelerometer` event node: needs `ID_INPUT_ACCELEROMETER=1`. |
| No light | `lsmod \| grep ssc_light`, `cat /sys/bus/iio/devices/*/in_illuminance_input`, `systemctl status gts7xl-sensord-light`. At boot the DSP registers light late; the service retries every 10 s. |
| No gyro/magnetometer/compass | `lsmod \| grep ssc_motion`, `grep . /sys/bus/iio/devices/*/name`, `systemctl status 'gts7xl-sensord-motion@*'`. Compass: `sudo monitor-sensor --compass`; `journalctl -u iio-sensor-proxy` says if it rejected the buffer. |
| Shutdown hangs 90 s on iio-sensor-proxy | `81-gts7xl-sensord-no-libssc.rules` missing. |

More detail on the protocol and sources: [`sensord/README.md`](../sensord/README.md), [`sensord/docs/RESEARCH.md`](../sensord/docs/RESEARCH.md).
