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
| Pogo keyboard | Working, with the `deferred_probe_timeout` workaround |
| Touch, Wi-Fi, GPU, audio | Not started |

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

## Keyboard status

The keyboard failed once the kernel's `sync_state` deadline passed and it dropped votes for unused resources. With `deferred_probe_timeout=600` it works, and the deadline only moves to 10 minutes. The specific resource isn't identified yet, so that is the real fix still to make.
