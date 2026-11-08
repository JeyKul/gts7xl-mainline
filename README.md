# Galaxy Tab S7+ Wi-Fi (SM-T970, gts7xlwifi) mainline Linux

Linux v7.3 and Arch Linux ARM on the Snapdragon 865 Tab S7+, booted directly from the Samsung bootloader (v1 boot image, gzip kernel, appended DTB).

<p align="center"><a href="https://jeykul.github.io/gts7xl-mainline/"><img alt="progress" src="https://img.shields.io/badge/dynamic/json?url=https://jeykul.github.io/gts7xl-mainline/progress.json&query=%24.score&suffix=%25&label=progress&color=2ea043&style=for-the-badge"> <img alt="usability" src="https://img.shields.io/badge/dynamic/json?url=https://jeykul.github.io/gts7xl-mainline/progress.json&query=%24.usability&label=usability&color=d4a72c&style=for-the-badge"></a><br><b><a href="https://jeykul.github.io/gts7xl-mainline/">📊 Live progress page</a></b></p>

Experimental. It's Linux or Android, not both: installing replaces `boot` and `dtbo` and erases `userdata`, so back up every partition first.

## Status

Key: ✅ working, 🟡 partial, ❌ not working, ⬜ not started or not tested.

### Core

| Hardware | State | Notes |
|---|---|---|
| SoC: Snapdragon 865 (SM8250), CPU cores | ✅ | No CPU frequency work confirmed |
| RAM | ✅ | Reserved memory follows the stock map |
| Storage: UFS | ✅ | Booting and running from `userdata` |
| microSD slot | ✅ | UHS-I SDR104 (sdhc_2, card detect on gpio77) |
| Suspend | 🟡 | s2idle with the native display: power key, timer and keyboard cover wake work; battery drain not measured |
| Thermal sensors and throttling | 🟡 | SoC, battery and charger sensors read; third thermistor reads high; throttling not checked |
| Real-time clock | 🟡 | Driver loaded, runs from 1970; time still set over Wi-Fi |
| Hardware buttons: power, volume | ✅ | Emergency reboot combo works |
| Vibration motor | ✅ | `regulator-haptic`, buzz confirmed |

### Display and input

| Hardware | State | Notes |
|---|---|---|
| Display: 2800x1752 AMOLED, S6TUUM3 | ✅ | Native DSC command mode driver at 60Hz; 120Hz mode offered, not confirmed |
| Backlight, brightness | ✅ | 0% kept readable (`min_brightness`), automatic brightness from the light sensor |
| Touchscreen | ✅ | Multitouch |
| S Pen digitizer | 🟡 | Position and pressure work, range fitted with `gts7xl-pen-calibrate` (3x3 targets, writes a udev hwdb entry), hover works; a few px of non-linear error at the bottom edge; side button, hover, tilt unconfirmed |
| S Pen Bluetooth remote | 🟡 | S Pen Pro button and air gestures work through `gts7xl-spen`, `Pen Actions` app to change them; built-in S Pen untested |
| Fingerprint reader (under display) | ❌ | Goodix GW9558 on SPI owned by TrustZone (gpio20-23; touching them resets the SoC). Only Samsung's trusted app can reach it |
| Pogo keyboard | ✅ | Recovers by itself after trackpad use; tablet mode switch on detach, see [docs/keyboard-cover.md](docs/keyboard-cover.md) |
| Pogo trackpad | ✅ | Works without stalls as of 2026-11-07 (first seen after the PMIC interrupt pin fix, 0051; cause not confirmed) |

### Graphics and DSPs

| Hardware | State | Notes |
|---|---|---|
| GPU: Adreno 650 | ✅ | OpenGL and Vulkan (Turnip; `vkcube` runs well). `vulkaninfo` aborts in its VK_KHR_display check, apps are fine |
| Video decode and encode (Venus) | ✅ | `qcom_iris` driver, firmware copied from apnhlos at first boot. Decode H.264, HEVC, VP9; encode H.264, HEVC (GStreamer `v4l2h264dec` etc., mpv `--hwdec=v4l2m2m-copy`). 720p H.264 decodes at about 550 fps |
| Audio DSP (ADSP) | ✅ | |
| Compute DSP (CDSP) | ⬜ | |
| Sensor DSP (SLPI) | ✅ | Booted with the tablet's own firmware, served by `gts7xl-sensord`, see [docs/sensors.md](docs/sensors.md) |

### Audio

| Hardware | State | Notes |
|---|---|---|
| 4 speakers, CS35L41 amps | ✅ | 4.0, protection firmware and factory calibration loaded; stock gain, software limiter in front |
| Microphones | 🟡 | Sub mic works, main mic reads zero |
| USB-C headphones | 🟡 | Digital USB-C headsets enumerate as a sound card (playback untested); analog adapters not done |

### Connectivity

| Hardware | State | Notes |
|---|---|---|
| Wi-Fi: QCA6390 | ✅ | |
| Bluetooth: QCA6390 | ✅ | Audio to a speaker works; RFCOMM profiles not built yet |
| GPS | ✅ | BCM47531 through Broadcom's gpsd in a small Android root built from the tablet's own partitions (`gts7xl-gnss-extract`). `gts7xl-gnss` serves NMEA to GeoClue and powers the chip only while something reads it. First fix in 39 s by a window. Waits for an agent in KDE: the GeoClue demo agent autostarts |
| Power profiles | ✅ | KDE/GNOME switch through tuned-ppd. `gts7xl-power`: Power Save caps clocks; all profiles step big/prime cores and GPU down when the back (battery thermistor) passes 39/41/46 C; Performance throttles last. Kernel SoC limits unchanged |
| Charging thermal throttling | ✅ | `gts7xl-charge-control`: ramps down from 38 C battery / 60 C charger, stops at 50 / 80 C (Samsung's limits) |
| Waydroid | ✅ | binder via binderfs, PSI, nftables NAT, and what Android's netd needs (policy routing, legacy iptables tables): apps and internet at full speed |
| USB gadget (serial, network) | ✅ | |
| USB host | ✅ | USB 2.0, automatic roles through the MAX77705 Type-C driver (`max77705_usbc`): OTG VBUS for sticks, hubs and headsets, a charger on a hub charges and keeps host. Tray applet "USB-C" picks the gadget mode. See `docs/usbc-dp-research.md` |
| DisplayPort over USB-C | 🟡 | HDMI through a USB-C hub with its charger: picture up to 4K60 (2 lanes, pin D) and audio ("HDMI" output), automatic on plug-in, both plug orientations. Some hubs need a power cycle after a replug; no DP when the tablet powers the hub; USB 3 not yet. See `docs/usbc-dp-research.md` |

### Power

| Hardware | State | Notes |
|---|---|---|
| Battery, fuel gauge: MAX77705 | ✅ | |
| Charging: MAX77705 | ✅ | 900mA without USB-PD |
| Fast charging | 🟡 | 9V USB-PD at 1.65 A, negotiated by the kernel driver; `gts7xl-charge-control` keeps the thermal limits. Higher power not done |

### Sensors

| Hardware | State | Notes |
|---|---|---|
| Accelerometer, auto-rotate | ✅ | LSM6DSO on the sensor DSP, through iio-sensor-proxy |
| Gyroscope | 🟡 | Streams from the DSP; not passed to desktops |
| Magnetometer | 🟡 | AK0991x streams from the DSP; not passed to desktops |
| Ambient light, auto-brightness | ✅ | VEML3328 through `ssc-light-bridge` and iio-sensor-proxy; second light sensor and RGB missing |
| Hall sensor (cover) | ✅ | The Book Cover Keyboard reports closed/open over pogo: closing sleeps, opening wakes. `gpio157` (stock flip-cover hall) never reacts to it and is unused |

### Cameras

| Hardware | State | Notes |
|---|---|---|
| Rear: 13 MP wide | 🟡 | 4128x3096 at 30 fps through libcamera, focus motor works by hand; no autofocus or tuned colour |
| Rear: 5 MP ultrawide | 🟡 | 2576x1932 through `s5k-tab`, colour not tuned |
| Front: 8 MP | 🟡 | 3264x2448 through `s5k-tab`, colour not tuned |
| Flash LED | ✅ | `Flashlight` app, up to 100mA |

## Installing

Every build is a prerelease under [Releases](https://github.com/JeyKul/gts7xl-mainline/releases), named `installer-<run>`. Requires an unlocked bootloader and TWRP. This erases all data on the tablet. So far this was tested on one tablet with a locally served installer; the published build has not been tested end to end.

1. In TWRP: Wipe, Format Data, then reboot to recovery. `ext4` is the tested filesystem.
2. Flash `gts7xlwifi-arch-twrp.zip` for the complete system, or write `gts7xlwifi-boot.img` to `boot` to get only the installer. With the image alone, zero `boot` first, blank the first 4KB of `dtbo` and clear byte 0 of `param`; the zip does this for you.
3. Reboot to system.

The boot image contains a small installer, which needs Wi-Fi. It starts when no system is found, and when volume up is held at power on. It joins Wi-Fi, downloads the rest into RAM, and installs Arch with the desktop you choose: GNOME, KDE or KDE Mobile. KDE Mobile runs but is laggy. A `Reinstall` app on the desktop returns to it.

The zip contains GNOME. Log in as `user`, password `user`, and change the password. The first boot copies firmware and calibration from this tablet's own partitions. Log: `/var/log/gts7xl-firstboot.log`.

`gts7xlwifi-boot-odin.tar.md5` holds only `boot` and a blank `dtbo`, for going back to the kernel from download mode without reinstalling. Not tested yet.

Android: restore `boot`, `dtbo` and `cache` from your backup, then format `userdata`.

## Optional kernel modules

Extra drivers live in a small pacman repo instead of the system, for example:

    sudo pacman -Sy hid-apple hid-magicmouse

To add one, set its option to `=m` in `config/gts7xl_defconfig` and add a line to `modules-repo/packages.txt`. The next build publishes it. Packages match the kernel of the same build, so update the kernel and packages together.

## Firmware and legal notice

The installer downloads GPU, audio DSP and amplifier firmware from the `Installer` release of this repository, and first boot falls back to the tablet's own `apnhlos` and `vendor` partitions. Those firmware files belong to Samsung and Qualcomm, are not covered by this project's license, and are not relicensed by being hosted here. Speaker calibration and the Bluetooth address are specific to each tablet and always come from that tablet.

## Emergency reboot

Press Power + Vol Up + Vol Down together, release all: reboots to recovery.

## Layout

- `linux-patches/`: apply to Linux v7.3
- `config/gts7xl_defconfig`: full kernel configuration
- `.github/workflows/build.yml`: manual build of everything, published as a prerelease
- `rootfs/`: Arch setup, tablet support files, the installer (`stage1`, `stage2`) and packaging
- `docs/display.md`: what the native display needs, brightness
- `docs/sensors.md`: sensor DSP stack, power rails, rotation and light, troubleshooting
- `docs/keyboard-cover.md`: keyboard, trackpad stalls, tablet mode
- `docs/usbc-dp-research.md`: USB-C host and DisplayPort plan, MAX77705 CCIC protocol, safety rules, test results
- `initramfs/init`: first code to run, picks the system or the installer
- `kmod/`: panel register tool used for display research
- `sensord/`: sensor DSP file service and client; accelerometer and light into iio-sensor-proxy
- `modules-repo/`: list of optional module packages
- `tools/`: serial shell helper, USB deploy, test programs
- `uefi/`: earlier Mu-Silicium attempt, not used; copy over a Mu-Silicium checkout. It needs blobs and a dump of your own device tree, which aren't published because they contain device identifiers

## Findings

- Samsung's bootloader falls back to download mode when it rejects an image.
- `boot` must be zeroed before writing, and the first 4KB of `dtbo` blanked.
- Entering TWRP with the keys sets byte 0 of `param` to 02, and it must be cleared to boot normally.
- The framebuffer is 2800x1752.
- `max77705` routes the USB data lines; without command `0x06 0x09` the host needs a replug.

## Keyboard notes

Trackpad use makes the connection line (gpio43) drop about once a second, after about 30 reports at most. The stock kernel in TWRP does the same, One UI does not, and the cause is unknown. Starting the sensor DSP and `hexagonrpcd` did not fix it, though `hexagonrpcd` stops at the missing `sns_registry` interface, so the sensor stack may not be fully up. The MCU firmware reads its own detect pin and watches a second I2C bus towards the sensor DSP. This project never writes firmware to the keyboard.

## License and credit

Free to use, change and share; just mention [JeyKul](https://github.com/JeyKul). See [LICENSE](LICENSE) (MIT).

Exceptions: `linux-patches/` and `kmod/` change or build on Linux, so they stay under GPL-2.0. `uefi/` builds on Mu-Silicium and follows its license.
