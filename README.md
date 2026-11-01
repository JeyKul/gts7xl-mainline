# Galaxy Tab S7+ Wi-Fi (SM-T970, gts7xlwifi) mainline Linux

Linux v7.3 on the Snapdragon 865 Tab S7+, booted directly from the Samsung bootloader
(v1 boot image, gzip kernel, appended DTB) or through
[Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) UEFI. The UEFI approach follows
[r8q-arch](https://github.com/Itzkristis/r8q-arch) for the Galaxy S20 FE.

Experimental. It replaces `boot`, `dtbo` and `cache`, so back up every partition first.

## Status

Key: ✅ working, 🟡 partial, ❌ not working, ⬜ not started or not tested.

### Core

| Hardware | State | Notes |
|---|---|---|
| SoC: Snapdragon 865 (SM8250), CPU cores | ✅ | No CPU frequency work confirmed |
| RAM | ✅ | Reserved memory follows the stock map |
| Storage: UFS | ✅ | Booting and running from `userdata` |
| microSD slot | ⬜ | |
| Suspend | 🟡 | s2idle with the native display: power key and timer wake work; battery drain not measured |
| Thermal sensors and throttling | 🟡 | SoC, battery and charger sensors read; third thermistor reads high; throttling not checked |
| Real-time clock | 🟡 | Driver loaded, runs from 1970; time still set over Wi-Fi |
| Hardware buttons: power, volume | ✅ | Emergency reboot combo works |
| Vibration motor | ⬜ | |

### Display and input

| Hardware | State | Notes |
|---|---|---|
| Display: 2800x1752 AMOLED, S6TUUM3 | ✅ | Native DSC command mode driver at 60Hz; 120Hz mode offered, not confirmed |
| Backlight, brightness | ✅ | Panel backlight control |
| Touchscreen | ✅ | Multitouch |
| S Pen digitizer | 🟡 | Position and pressure work, range hand-calibrated; side button, hover, tilt unconfirmed |
| S Pen Bluetooth remote | ⬜ | |
| Fingerprint reader (under display) | ⬜ | |
| Pogo keyboard | 🟡 | Works until the trackpad is touched |
| Pogo trackpad | ❌ | Touch crashes the keyboard MCU |

### Graphics and DSPs

| Hardware | State | Notes |
|---|---|---|
| GPU: Adreno 650 | ✅ | OpenGL; `glxgears` about 100 FPS |
| Video decode and encode (Venus) | ⬜ | |
| Audio DSP (ADSP) | ✅ | |
| Compute DSP (CDSP) | ⬜ | |
| Sensor DSP (SLPI) | ⬜ | |

### Audio

| Hardware | State | Notes |
|---|---|---|
| 4 speakers, CS35L41 amps | ✅ | 4.0, protection firmware and factory calibration loaded; stock gain, software limiter in front |
| Microphones | ⬜ | |
| USB-C headphones | ⬜ | |

### Connectivity

| Hardware | State | Notes |
|---|---|---|
| Wi-Fi: QCA6390 | ✅ | |
| Bluetooth: QCA6390 | ✅ | Audio to a speaker works; RFCOMM profiles not built yet |
| GPS | ⬜ | |
| USB gadget (serial, network) | ✅ | |
| USB host | ⬜ | |
| DisplayPort over USB-C | ⬜ | |

### Power

| Hardware | State | Notes |
|---|---|---|
| Battery, fuel gauge: MAX77705 | ✅ | |
| Charging: MAX77705 | ✅ | 900mA without USB-PD |
| Fast charging | 🟡 | 9V USB-PD, about 15W, through `rootfs/gts7xl-charge-control`; replug not fully tested. Higher power not done |

### Sensors

| Hardware | State | Notes |
|---|---|---|
| Accelerometer, auto-rotate | ⬜ | |
| Gyroscope | ⬜ | |
| Magnetometer | ⬜ | |
| Ambient light | ⬜ | |
| Hall sensor (cover) | ⬜ | |

### Cameras

| Hardware | State | Notes |
|---|---|---|
| Rear: 13 MP wide | ⬜ | |
| Rear: 5 MP ultrawide | ⬜ | |
| Front: 8 MP | ⬜ | |
| Flash LED | ⬜ | |

## Installing

Not tested end to end yet. Requires an unlocked bootloader and TWRP. This erases all data on the tablet.

1. In TWRP: Wipe, Format Data, then reboot to recovery. `ext4` is the tested filesystem; `f2fs` is supported by the kernel but not tested as the root filesystem.
2. Flash `gts7xlwifi-arch-twrp.zip` and reboot to system.
3. The first boot copies firmware and calibration from this tablet's own partitions. Log: `/var/log/gts7xl-firstboot.log`.
4. Log in as `user`, password `user`, and change the password.

`gts7xlwifi-boot-odin.tar.md5` holds only `boot` and a blank `dtbo`, for going back to the kernel from download mode without reinstalling.

Android: restore `boot`, `dtbo` and `cache` from your backup, then format `userdata`.

## Firmware and legal notice

The installer prefers each tablet's own firmware. GPU, audio DSP and amplifier firmware, plus speaker calibration and the Bluetooth address, are copied at first boot from the tablet's own `apnhlos`, `vendor`, `sec_efs` and `efs` partitions, which are only read. If `apnhlos` or `vendor` lack the files, for example on another ROM, first boot downloads them from the `Installer` release of this repository. Those firmware files belong to Samsung and Qualcomm, are not covered by this project's license, and are not relicensed by being hosted here. Speaker calibration and the Bluetooth address are specific to each tablet and always come from that tablet.

## GPU

`msm` is a module with `separate_gpu_kms=1 r8q_zap_dyn=1 r8q_zap_secvid=0`, loaded from `/etc/local.d/gpu.start` and never by udev. It needs the Samsung-signed `a650_zap` files from your own `apnhlos` partition in `/lib/firmware/qcom/sm8250/` (the `.mdt` renamed to `a650_zap.mbn`), which aren't published here. The module must match the running kernel.

## Emergency reboot

Press Power + Vol Up + Vol Down together, release all: reboots to recovery.

## Layout

- `linux-patches/`: apply to Linux v7.3
- `config/gts7xl_defconfig`: full kernel configuration
- `.github/workflows/build.yml`: manual build of the boot image and modules
- `uefi/`: copy over a Mu-Silicium checkout
- `initramfs/init`: bring-up init, add a static aarch64 busybox as `bin/busybox`
- `tools/`: serial shell helper, USB deploy, test programs

## Not included

The UEFI build also expects `Binaries/gts7xlwifi`: a copy of the S20 FE blobs (`Binaries/r8q`) with the board drivers replaced by ones from your own `xbl`, extracted with `tools/fvx.py`. Vendor firmware isn't published here.


The UEFI build needs `Resources/DTBs/gts7xlwifi.dtb`, a dump of your own device's running
tree (`dtc -I fs /sys/firmware/devicetree/base`), and the kernel DTB in `FdtBlob/`. These
aren't published because a live dump contains device identifiers.

## Findings

- Samsung's bootloader falls back to download mode when it rejects an image.
- `boot` must be zeroed before writing, and the first 4KB of `dtbo` blanked.
- Entering TWRP with the keys sets byte 0 of `param` to 02, and it must be cleared to boot normally.
- The framebuffer is 2800x1752.
- `max77705` routes the USB data lines; without command `0x06 0x09` the host needs a replug.

## Keyboard notes

Touching the trackpad makes the MCU send a few valid frames, then stop, with gpio43 bouncing about 10 times a second. Typing alone is fine. Unsolicited I2C polling also upsets the MCU, so don't poll it. The pin overrides tried earlier made no difference.

## License and credit

Free to use, change and share; just mention [JeyKul](https://github.com/JeyKul). See [LICENSE](LICENSE) (MIT).

Exceptions: `linux-patches/` and `panel-samsung-s6tuum3.c.wip` change or build on Linux, so they stay under GPL-2.0. `uefi/` builds on Mu-Silicium and follows its license.
