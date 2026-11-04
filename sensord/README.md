# gts7xl-sensord

Sensor stack for the Galaxy Tab S7+ Wi-Fi on mainline Linux. Motion, compass and light sensors sit behind the sensor DSP (SLPI). This serves the DSP the files it asks for, talks to its sensor service, and publishes the accelerometer for `iio-sensor-proxy`. Written in C with no dependencies beyond libc and kernel headers.

**Untested on hardware.** See [TEST-PLAN.md](TEST-PLAN.md).

    gts7xl-sensord --trace-fastrpc DIR   serve DIR read-only, log every call
    gts7xl-sensord --serve DIR           serve DIR; only mnt/vendor/persist/sensors is writable
    gts7xl-sensord --list                print the sensors the DSP knows
    gts7xl-sensord --stream accel|gyro   print samples
    gts7xl-sensord --uinput              accelerometer as an input device

Build: `make`. Tests, no hardware needed: `make check`. Install: `make install`, later `make install-autostart`.

## Layout

- `src/wire.c`: FastRPC listener stream format
- `src/fsrv.c`: what the DSP calls: `apps_std`, `apps_mem`, `remotectl`, `sns_registry`
- `src/listener.c`: the reverse tunnel loop over `/dev/fastrpc-sdsp`
- `src/pb.c`, `src/ssc.c`: protobuf and QMI messages
- `src/client.c`: SSC client over a QRTR socket
- `src/main.c`: command line, streaming, uinput
- `tools/gts7xl-sensord-extract`: copies this tablet's firmware and sensor files, read-only
- `dist/`: systemd units and udev rule
- `linux-patches/0040` in the repo enables the DSP in the device tree; the tarball carries it in `patches/`
- `docs/RESEARCH.md`: facts with sources, licence decisions

## Why uinput

`iio-sensor-proxy` already has an `input-accel` driver, chosen by a udev property on any device with `INPUT_PROP_ACCELEROMETER`. It reads `ABS_X/Y/Z` by `EVIOCGABS` every 700 ms at 256 counts per g, and takes `ACCEL_MOUNT_MATRIX` from udev. That needs no kernel driver and no change to the Arch package. libinput ignores such devices. The alternatives cost more: there is no way to make a real IIO device from userspace, and its `ssc-accel` driver needs a build against libssc, libqmi and glib. Limit: this covers the accelerometer only. Light and compass have no equivalent input path.

## Licence

MIT, see `LICENSE`. Protocol facts came from GPL projects, no code did. Details in `docs/RESEARCH.md`.
