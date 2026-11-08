# Testing: gyroscope, magnetometer and compass to userspace

## What changed

- Kernel `linux-patches/0072` + `CONFIG_SSC_MOTION_BRIDGE=m`: module
  `ssc-motion-bridge` with three IIO devices that userspace fills in, like
  `ssc-light-bridge` (0047):
  - `ssc-gyro`: `in_anglvel_{x,y,z}_input` (rad/s)
  - `ssc-magn`: `in_magn_{x,y,z}_input` (gauss)
  - `ssc-compass`: `in_rot_from_north_magnetic_tilt_comp_raw` (millidegrees,
    `_scale` 0.001) plus a buffer, which is what iio-sensor-proxy's
    `iio-buffer-compass` driver reads → `net.hadess.SensorProxy.Compass`
    (meant for GeoClue).
- `sensord/dist/gts7xl-sensord-motion` + `gts7xl-sensord-motion@.service`
  (`@gyro`, `@mag`, `@compass`), started by the existing fastrpc-sdsp udev rule.
  Heading = DSP rotation vector (`rotv`), direction of the top edge (camera
  edge in landscape) clockwise from magnetic north. `rotv` only streams while
  the compass is claimed.
- Accelerometer (uinput) and light paths are untouched.

Built here: the module compiles cleanly for arm64 against v7.3 + 0001-0066
(`W=1`, no warnings); the shell pipelines were run against a fake sensord.
Not run on the tablet.

## Install for a test

Kernel: build with the series (0072 and 0073 included) and the updated defconfig as
usual, flash, then copy the module if you don't install all modules:

    sudo install -Dm644 ssc-motion-bridge.ko /lib/modules/7.3.0-gts7xl/kernel/drivers/iio/imu/ssc-motion-bridge.ko
    sudo depmod -a 7.3.0-gts7xl

Userspace (on the tablet, from this branch's checkout):

    sudo make -C sensord install install-autostart
    sudo udevadm control --reload
    sudo systemctl daemon-reload
    sudo systemctl start gts7xl-sensord-motion@gyro gts7xl-sensord-motion@mag gts7xl-sensord-motion@compass
    # (after a reboot udev starts them by itself)

## Test

    lsmod | grep ssc_motion
    grep . /sys/bus/iio/devices/*/name                  # ssc-gyro, ssc-magn, ssc-compass
    systemctl status 'gts7xl-sensord-motion@*' --no-pager

1. Gyroscope: rotate the tablet while watching

       G=$(dirname $(grep -lx ssc-gyro /sys/bus/iio/devices/*/name))
       watch -n0.2 cat $G/in_anglvel_x_input $G/in_anglvel_y_input $G/in_anglvel_z_input

   **Success:** near 0 at rest (a small constant bias is normal, it's the raw
   DSP value), up to a few rad/s while turning; flat on a table, spinning it
   moves mostly z.

2. Magnetometer:

       M=$(dirname $(grep -lx ssc-magn /sys/bus/iio/devices/*/name))
       watch -n0.5 cat $M/in_magn_x_input $M/in_magn_y_input $M/in_magn_z_input

   **Success:** values of roughly ±0.2–0.6 gauss (Earth's field is 0.25–0.65 G)
   that change when the tablet is turned. A large constant offset means the
   `mag` stream is uncalibrated (expected possible; the compass doesn't use it).

3. Compass through iio-sensor-proxy:

       sudo monitor-sensor --compass

   **Success:** `=== Has compass (heading: …)` and the heading follows the
   tablet when you turn it flat on a table. Compare with a phone compass: the
   value is the direction the camera edge points. If it is off by a fixed 90°
   or 180°, or turns the wrong way, report it (a one-line change in
   `gts7xl-sensord-motion`). Wave the tablet in a figure 8 first if it is off
   by a random amount (DSP magnetometer calibration).
   While `monitor-sensor --compass` runs, `cat .../buffer/enable` on the
   `ssc-compass` device reads 1, and `rotv` stops ~2 s after you quit it.

4. Nothing else changed: `monitor-sensor --accel --light` still reports
   orientation and light, auto-rotate/brightness behave as before.

If iio-sensor-proxy doesn't list the compass:
`journalctl -b -u iio-sensor-proxy | grep -i compass` (a "did not have data
within 0.5s" warning means the re-announce happened before the first heading;
`sudo systemctl restart iio-sensor-proxy` should then pick it up — report it).
`udevadm info /sys/bus/iio/devices/iio:deviceN | grep IIO_SENSOR_PROXY_TYPE`
should show `iio-buffer-compass` for `ssc-compass`.

## Power

Gyro and magnetometer stream continuously (10 Hz requested) like the
accelerometer already does; the compass's `rotv` only while claimed. To turn
one off: `sudo systemctl mask --now gts7xl-sensord-motion@gyro` (or `@mag`).

## Revert

    sudo systemctl stop 'gts7xl-sensord-motion@*'
    sudo rm /usr/local/bin/gts7xl-sensord-motion /etc/systemd/system/gts7xl-sensord-motion@.service
    sudo make -C sensord install-autostart   # from main, restores the old udev rule
    sudo modprobe -r ssc-motion-bridge

and drop 0072 + the defconfig line from the kernel build.
