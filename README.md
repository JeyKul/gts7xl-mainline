# Galaxy Tab S7+ Wi-Fi (SM-T970, gts7xlwifi) mainline Linux

Linux v7.3 on the Snapdragon 865 Tab S7+, booted directly from the Samsung bootloader
(v1 boot image, gzip kernel, appended DTB) or through
[Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) UEFI. The UEFI approach follows
[r8q-arch](https://github.com/Itzkristis/r8q-arch) for the Galaxy S20 FE.

Experimental. It replaces `boot`, `dtbo` and `cache`, so back up every partition first.

## Status

| Part | State |
|---|---|
| Boot, screen (firmware framebuffer), UFS | Working |
| USB gadget (serial + NCM) | Working, connects about 85s after boot with no replug |
| `reboot recovery` to TWRP | Working |
| MAX77705 charger, fuel gauge | Working, limited to 500mA / 4.35V |
| Pogo keyboard | Works until the trackpad is touched |
| Pogo trackpad | Touch crashes the MCU, under investigation |
| Touchscreen | Working, multitouch |
| Sway on Alpine v3.22 (Vulkan renderer) | Installed, replaced by GNOME at boot |
| Adreno 650 GPU (OpenGL, Vulkan) | Working, `glxgears` about 100 FPS |
| Wi-Fi (QCA6390, ath11k) | Connected, internet works |
| GNOME 48 (gdm autologin) | Running; GPU use not confirmed |
| Audio | Not started |

## Desktop

Alpine and Sway live on `userdata` (formatted ext4, which erases Android data). In TWRP, run `rootfs/twrp-setup.sh` with the packages pushed to `/tmp`, then `rootfs/rootcfg.sh`. The initramfs switches to it at boot, with a fallback shell if it's missing. The serial shell and the `user` account have no password.

## GPU

`msm` is a module with `separate_gpu_kms=1 r8q_zap_dyn=1 r8q_zap_secvid=0`, loaded from `/etc/local.d/gpu.start` and never by udev. It needs the Samsung-signed `a650_zap` files from your own `apnhlos` partition in `/lib/firmware/qcom/sm8250/` (the `.mdt` renamed to `a650_zap.mbn`), which aren't published here. The module must match the running kernel.

## Emergency reboot

Press Power + Vol Up + Vol Down together, release all: reboots to recovery.

## Layout

- `linux-patches/`: apply to Linux v7.3
- `config/gts7xl.config`: merge on top of arm64 defconfig
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
