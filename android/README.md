# AOSP on the Tab S7+ (mainline kernel)

AOSP 17 (`android-17.0.0_r1`) for the Galaxy Tab S7+ Wi-Fi (SM-T970), running on this
project's mainline 7.x kernel instead of Samsung's. LineageOS 24.0 (also Android 17)
is the planned next step once this boots.

## Layout and decisions

| What | Choice | Why |
|---|---|---|
| Base | AOSP's own Qualcomm dev boards (`device/linaro/dragonboard`) for graphics: drm_hwcomposer, Mesa freedreno, minigbm_msm | the open stack that works on near-mainline kernels, nothing from Samsung or Qualcomm userspace |
| Kernel | `build-android`: the Linux config plus AOSP's `v/android-6.6/android-base.config`, cmdline built in | the 6.18 fragments for Android 17 are still empty; Android-common-kernel-only options (ashmem, xt_quota2, incfs, dm-default-key) don't exist in mainline |
| Boot image | header v1, Image.gz with the DTB appended, first-stage ramdisk in `boot` | the bootloader boots exactly this for Linux; there is no `vendor_boot` partition |
| Slots | none (no A/B), no Android recovery | Samsung layout; `recovery` keeps TWRP |
| `super` | 10171187200 bytes, group `gts7xl_dynamic_partitions`: system, system_ext, product, vendor, vendor_dlkm (EROFS) | the tablet's size; stock used `qti_dynamic_partitions` |
| Data | f2fs `userdata`, FBE v2 with software KeyMint, inline crypto through mainline's UFS ICE | no TrustZone keymaster access from mainline; no metadata encryption (dm-default-key) |
| Firmware | `/vendor/firmware` links into the tablet's own `apnhlos` (`/vendor/firmware_mnt`), plus linux-firmware's a650 sqe/gmu and ath11k | no proprietary files in the build |
| Memory sharing | `sys.use_memfd=true` | mainline has no ashmem (found with TWRP on this kernel) |
| Display | one DRM card (no `msm.separate_gpu_kms`) | drm_hwcomposer and Mesa expect the display on card0 |

## Build

```
# kernel
cd src/linux && cp build-bi/.config build-android/.config   # see NOTES.md for the merge
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make O=build-android Image modules qcom/sm8250-samsung-gts7xlwifi.dtb
repo/android/device/samsung/gts7xlwifi/prepare-kernel.sh src/linux src/linux/build-android
# AOSP
ln -s <repo>/android/device/samsung/gts7xlwifi <aosp>/device/samsung/gts7xlwifi
source build/envsetup.sh && lunch gts7xlwifi-trunk_staging-userdebug && m
```

Flashing (`boot`, `super`, formatting `userdata`) replaces One UI and the Linux install:
only with the owner's go-ahead. `backup/super/super.img` restores One UI's system.
