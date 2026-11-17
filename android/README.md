# AOSP 17 on the Galaxy Tab S7+ Wi-Fi (mainline kernel)

Plain AOSP 17 (`android-17.0.0_r1`) for the **Galaxy Tab S7+ Wi-Fi, SM-T970 (gts7xlwifi)**,
on this project's mainline Linux 7.3 kernel instead of Samsung's 4.19. It uses no Samsung
or Qualcomm userspace: Mesa freedreno for the GPU, drm_hwcomposer for the display, and
small HALs of our own for power, sensors and vibration. Firmware is read from the tablet's
own partitions at runtime. LineageOS 24.0 is the next step.

**Experimental: this is a bring-up build, not a daily driver.** Only the SM-T970 is
supported. The LTE models (SM-T975/T976, gts7xl) are not, and the zip refuses to install
on them.

## What works

| | State | Notes |
|---|---|---|
| Display, 120 Hz, brightness | ✅ | Native panel driver; 120 Hz by default, Smooth display toggle |
| GPU | ✅ | Adreno 650, Mesa freedreno (GLES 3.2) |
| Touchscreen | ✅ | |
| Wi-Fi, Bluetooth | ✅ | |
| Speakers, media audio | ✅ | Each tablet's own factory calibration, read from `sec_efs` at boot |
| Sensors | ✅ | Auto-rotate, auto-brightness, accelerometer, gyroscope; magnetometer not calibrated yet |
| Charging, USB (adb) | 🟡 | Charges, but the battery icon says "discharging" |
| Battery details | ✅ | Design capacity from Samsung's power profile, cycle count in About > Battery information |
| Vibration | ✅ | Fixed-voltage motor, so short pulses instead of strength levels |
| Sleep | 🟡 | Suspends, but uses about 170 mA with the screen off (an SoC-level limit, also on Linux) |
| Cameras, microphone, fingerprint, GPS, HDMI | ❌ | Not done on Android yet |
| Encryption | ❌ | **`/data` is not encrypted.** SELinux is permissive, and the build is userdebug (adb root works) |

## Install

You need:
- An SM-T970 with an **unlocked bootloader** and **TWRP** on `recovery`. Any TWRP for
  gts7xlwifi works. This project's TWRP (`twrp-linux/`) also boots on the mainline kernel.
- The zip (`gts7xl-aosp17-<date>.zip`) and its `.sha256` file. Check the zip with
  `sha256sum -c gts7xl-aosp17-<date>.zip.sha256`.

Keep a full TWRP backup or the stock firmware for Odin, so you can go back.

1. Boot TWRP: power off, then hold **Power + Volume Up**.
2. **First install only** (coming from One UI, Linux or another ROM):
   **Wipe > Format Data**, type `yes`. This erases internal storage, so copy your files
   off first.
3. **Install** the ROM zip (from microSD, or `adb push <zip> /tmp/`, or
   **Advanced > ADB Sideload**). It checks that the tablet is an SM-T970, writes `boot`
   and `super` and verifies both, blanks the `dtbo` header (otherwise the bootloader
   applies Samsung's overlays to the mainline device tree), and clears the flag that
   makes the bootloader start TWRP again. It doesn't touch data.
4. **GApps (optional):** install a GApps zip for Android 17 arm64 right after it, in
   the same TWRP session (NikGapps core is tested). They go to `product`, which has
   1.5 GB free. Messages about a read-only `/system` are harmless: only NikGapps' log
   and addon.d files go there.
5. **Reboot > System.** The first boot takes a few minutes.

If the first boot after Format Data keeps restarting, delete
`/data/system/locksettings.db` in TWRP (some TWRP builds create it owned by root).

## Updates

Install the new ROM zip, and then GApps again: an update replaces `product`, and
GApps live there. Data stays, so apps and files are kept.

## Going back

- **One UI:** flash the stock firmware with Odin. Or restore your TWRP backup of
  `boot`, `dtbo` and `super`, then format data.
- **Linux (this project):** see the main [README](../README.md).

## Build it yourself

What you need: Linux x86_64 with about 400 GB free and 32 GB of RAM or more, the AOSP
build dependencies, `aarch64-linux-gnu-gcc`, `meson`, `ninja`, `python3-pip` and `zip`.

### 1. Kernel

```
git clone https://github.com/torvalds/linux && cd linux
git checkout v7.3 && git checkout -b gts7xl
git am <this repo>/linux-patches/0*.patch            # 0001..0092, in order
mkdir -p build-android && cp <this repo>/android/kernel/gts7xl-android_defconfig build-android/.config
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make O=build-android olddefconfig
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make O=build-android -j$(nproc) Image modules qcom/sm8250-samsung-gts7xlwifi.dtb
<this repo>/android/device/samsung/gts7xlwifi/prepare-kernel.sh . build-android
```

`prepare-kernel.sh` fills `device/samsung/gts7xlwifi/prebuilt/`: the kernel with its DTB
appended, the modules, and the redistributable linux-firmware files (downloaded).
`kernel/make-config.sh` shows how the defconfig was put together: the Linux config, plus
AOSP's and LineageOS's Android fragments, plus `kernel/gts7xl-android.config`.

### 2. AOSP

```
mkdir aosp17 && cd aosp17
repo init -u https://android.googlesource.com/platform/manifest -b android-17.0.0_r1 --partial-clone
mkdir -p .repo/local_manifests && cp <this repo>/android/manifest/gts7xl.xml .repo/local_manifests/
repo sync -c -j8
pip install --target prebuilts/mesa-build-dep/pydeps mako pyyaml packaging
AOSP=$PWD <this repo>/android/aosp-build.sh          # log: build.log, ends with BUILD-EXIT 0
<this repo>/android/make-zip.sh gts7xl-aosp17.zip    # AOSP_OUT=$PWD/out if it isn't /mnt/builddrive
```

`aosp-build.sh` runs `sync-to-aosp.sh`, which copies the device tree to
`device/samsung/gts7xlwifi`, installs Mesa's meson/ninja wrappers, and applies the
patches under `patches/` once. Then it runs `lunch gts7xlwifi-cp2a-userdebug` and builds
`droid superimage`.

For development there is also `flash-from-twrp.sh` (writes `super`, and `boot` with the
argument `boot`, over adb in TWRP) and `flash-boot-from-android.sh` (writes only `boot`,
from a running Android).

## Layout and decisions

| What | Choice | Why |
|---|---|---|
| Base | AOSP's Qualcomm dev boards (`device/linaro/dragonboard`) for graphics: drm_hwcomposer, Mesa freedreno (LineageOS `external/mesa`), minigbm_msm | the open stack that works on near-mainline kernels |
| Kernel | the Linux config plus AOSP's `v/android-6.6/android-base.config` and LineageOS's mainline fragments, cmdline built in | the 6.18 fragments for Android 17 are still empty; options that only exist in Android's common kernel (ashmem, incfs, dm-default-key) don't exist in mainline |
| Boot image | header v1, Image.gz with the DTB appended, first-stage ramdisk in `boot` | what the bootloader boots for Linux too; there is no `vendor_boot` |
| Slots | none (no A/B), no Android recovery | Samsung's layout; `recovery` keeps TWRP |
| `super` | 10171187200 bytes, group `gts7xl_dynamic_partitions`: system, system_ext, vendor, vendor_dlkm (EROFS), product (ext4, 1.5 GB free for GApps) | the tablet's size; GApps packages can only install to system, system_ext or product |
| Data | f2fs `userdata`, unencrypted for now | no TrustZone keymaster access from mainline yet |
| Firmware | `/vendor/firmware` links into the tablet's own `apnhlos`, plus linux-firmware's GPU, Wi-Fi and Bluetooth files | no proprietary files in the build, except the amp blobs below |
| Memory sharing | `sys.use_memfd=true` | mainline has no ashmem |

`device/samsung/gts7xlwifi/proprietary/` holds the files Android needs from Samsung's
stock firmware (the speaker amps' protection firmware and sensor configs), redistributed
unmodified. Its README says where each one comes from.
