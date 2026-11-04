# Test plan

Nothing here has run on a tablet. Go in order and stop at the first failure. Send back what each step's **Send** names. Keep a USB serial shell handy: step 3 starts new firmware.

Risk: the steps only read stock partitions, and only write `/var/lib/gts7xl-sensord`, `/usr/lib/firmware/qcom/sm8250/gts7xl` and `/usr/local`. The new firmware runs on the SLPI, and it can crash and restart. Audio should be unaffected, but note if it isn't.

## 1. Build and self-test

    pacman -S --needed base-devel linux-api-headers
    make check && make && sudo make install    # in this directory

**Proves:** `132 checks, 0 failures` then `209 checks, 0 failures`, on real aarch64 this time with the file tests included.
**Send:** the last 20 lines.

## 2. Find the firmware and data

    sudo gts7xl-sensord-extract
    ls -la /usr/lib/firmware/qcom/sm8250/gts7xl/slpi.*
    sudo find /var/lib/gts7xl-sensord -type f | wc -l
    cat /var/lib/gts7xl-sensord/sys/class/sensors/ssc_core/ssc_hw_rev
    tr ' ' '\n' < /proc/cmdline | grep -E 'revision|hardware|bootloader'
    cat /sys/devices/soc0/soc_id /sys/devices/soc0/revision

**Proves:** `firmware:`, `vendor:` and `persist:` lines without `FAILED`; `slpi.mbn` plus `slpi.b00` and so on; a few dozen or more files, with `.../persist/sensors/registry/registry/` not empty if stock had one.
**Check:** the board revision line. If it says GUESS and you know the board, rerun with `--revision N`.
**Send:** all output, and `ls /dev/disk/by-partlabel/ | tr '\n' ' '` if anything failed.

## 3. Boot the SLPI

Build and flash a kernel with `linux-patches/0043-gts7xlwifi-enable-the-sensor-DSP.patch` (branch `sensord`, or `git am patches/0001-*.patch` from the tarball). After boot:

    dmesg | grep -iE 'remoteproc|slpi|fastrpc|5c00000|dsps'
    for r in /sys/class/remoteproc/*; do echo "$r $(cat $r/name) $(cat $r/state)"; done
    ls -l /dev/fastrpc-*

If `slpi.mbn` arrived after the module loaded: `echo start | sudo tee /sys/class/remoteproc/remoteprocN/state`.

**Proves:** one remoteproc `running` at `5c00000`, and `/dev/fastrpc-sdsp` exists.
**Send:** the output. `qcom_scm` or `-22`/`-13` errors mean TrustZone refused the image or the memory map is off.

## 4. See what the firmware asks for, read-only

    # restart the SLPI so it asks again; change N for your SLPI:
    R=/sys/class/remoteproc/remoteprocN; echo stop | sudo tee $R/state; sleep 1; echo start | sudo tee $R/state
    sudo gts7xl-sensord --trace-fastrpc /var/lib/gts7xl-sensord | tee trace.log
    # after 60 s: Ctrl-C
    grep -cE '^\[fs\] open' trace.log
    grep -E 'FAILED|DENY-WRITE|UNKNOWN|sns_registry|\[rpc\].*-> (14|20)' trace.log | sort | uniq -c | sort -rn | head -40

**Proves:** `attached to sensor PD`, then many `[fs] open` lines naming `/vendor/etc/sensors/...` and `/sys/...`. On the S8+ the firmware also tries to write, so `DENY-WRITE` on `sns_reg_version` or `temp.json` is expected here, as is a slow retry loop. This mode can't complete init, and on another SoC refused writes crashed the DSP, so watch for that.
**If** `attach to sensor PD` fails: rerun with `--root-pd` and say so.
**If** `dlerr` on `adsp_default_listener`: the session is not up yet, wait and retry.
**Send:** `trace.log` whole. This is the most valuable file in the plan.

## 5. Serve for real

    R=/sys/class/remoteproc/remoteprocN; echo stop | sudo tee $R/state; sleep 1; echo start | sudo tee $R/state
    sudo gts7xl-sensord --serve /var/lib/gts7xl-sensord --verbose | tee serve.log &
    sleep 90
    sudo ls /var/lib/gts7xl-sensord/mnt/vendor/persist/sensors/registry/ | head
    sudo ls /var/lib/gts7xl-sensord/mnt/vendor/persist/sensors/registry/registry | wc -l
    grep -E 'sns_registry|UNKNOWN|-> 20' serve.log | sort | uniq -c | sort -rn | head -20
    dmesg | tail -30

**Proves:** `get_property "ro.revision"`, a registry that fills or stays unchanged, no ongoing writes after about a minute, and no `crash` or recovery lines 15 s in.
**Key failure:** a crash with `I2C_ERROR_TRANSFER_TIMEOUT` is the missing rail case. Send that, and try next: on stock or in TWRP, `cat /sys/kernel/debug/regulator/regulator_summary` with sensors active, to name the rail.
**Send:** `serve.log`, the `dmesg` tail.

## 6. List the sensors

Leave `--serve` running.

    sudo gts7xl-sensord --list

**Proves:** `SSC service at node N port M`, then `accel[0]` with `name: lsm6dso` (or similar) and `gyro[0]`.
**Failures:**
- `not found: Timed out`: service 400 not published, send `serve.log` and `dmesg`.
- All types `none`: the registry or JSON selection is wrong. Recheck step 2's revision and `soc0` values.
- `lookup failed: Timed out` for every type: service is up but silent, as on the S8+ before init. See step 5.

**Send:** full output.

## 7. Stream

    sudo gts7xl-sensord --stream accel --count 20
    sudo gts7xl-sensord --stream gyro --count 20

**Proves:** the accel magnitude is near 9.8 at rest and axes change when tilted; gyro is near 0 at rest and moves with rotation. Units for gyro are probably rad/s, not confirmed.
**Send:** output from both, with how the tablet lay.

## 8. uinput and iio-sensor-proxy

    sudo pacman -S --needed iio-sensor-proxy
    sudo udevadm control --reload
    sudo gts7xl-sensord --uinput &
    grep -l 'gts7xl-sensord accelerometer' /sys/class/input/event*/device/name
    udevadm info /dev/input/eventN | grep -E 'ACCEL|IIO_SENSOR'
    sudo systemctl restart iio-sensor-proxy
    monitor-sensor --accel

**Proves:** `ID_INPUT_ACCELEROMETER=1`, `IIO_SENSOR_PROXY_TYPE=input-accel`, and `Accelerometer orientation changed` lines as you turn the tablet.

**Fix the matrix:** the panel is landscape-native, so holding it landscape should say `normal`. Record the raw axes in each pose with `--stream accel --count 1`, then set `ACCEL_MOUNT_MATRIX` in `/etc/udev/rules.d/70-gts7xl-sensord.rules`. The S8+ needed `"0, 1, 0; -1, 0, 0; 0, 0, 1"`; start there, as this one may match. Reload and replug by restarting `--uinput`.

**Send:** `udevadm info` output, `monitor-sensor` output, poses with raw values.

## 9. Run as services and survive a reboot

Stop the `--serve` and `--uinput` you started by hand, then:

    sudo make install-autostart
    sudo systemctl daemon-reload
    sudo reboot
    systemctl status gts7xl-sensord-fs gts7xl-sensord-accel --no-pager
    journalctl -b -u gts7xl-sensord-fs --no-pager | tail -30
    monitor-sensor --accel

**Proves:** both active via the udev rule, GNOME rotates, and `journalctl` stays quiet after init.
**Then:** suspend once, wake, and check that orientation still updates. Measure battery drain over an hour. Delivery mode `NO_WAKEUP` is an assumption, so if suspend misbehaves, note it. Streaming runs all the time in this version.
**Send:** the output, and drain compared with your earlier 2%/h.

## What the test-suite does not prove

`make check` drives the program against a simulated DSP and SSC written from the same reading of the protocol, so it finds logic bugs but cannot find a misread protocol. Only the one captured message in `tests/test.c` is real.
