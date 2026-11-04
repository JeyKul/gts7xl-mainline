# CLAUDE.md: gts7xl-mainline

Galaxy Tab S7+ Wi-Fi (SM-T970, SM8250) on mainline Linux + Arch Linux ARM. The tablet is on ADB/USB serial.

## How to work
- Be terse. No summaries of what you did unless asked. Report: what changed, what was tested, what's next. Max ~5 lines.
- Don't ask permission for routine steps (build, flash boot, reboot, read logs). Ask only before anything destructive (see Safety).
- Don't re-read files you've already read this session. Use grep/sed ranges, not whole-file dumps. Pipe noisy output through `tail`/`grep`.
- Never paste full logs or dmesg into the chat. Quote only the lines that matter.
- One hypothesis at a time: say what result would prove it wrong, then test it on the device.
- State lives in files, not chat: update `NOTES.md` (dated, short) after every real finding, so a fresh session can pick up from it.

## Safety (hard rules)
- Never write to: efs, sec_efs, modem*, persist, abl*, xbl*, tz*, hyp*, keymaster*, vbmeta*, or any bootloader partition. Only boot, dtbo and userdata.
- Back up a partition before writing it, the first time.
- Firmware blobs come from the tablet's own partitions at runtime and never get committed to git.

## Build / flash / test
- Kernel: tree in `src/linux` (branch gts7xl, v7.3 + `repo/linux-patches`). `cd src/linux && ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION= make -s O=build -j24 Image modules qcom/sm8250-samsung-gts7xlwifi.dtb`. `CONFIG_INITRAMFS_SOURCE` must be `~/gts7xl-mainline/initramfs`: a good boot image is about 27 MB, 15 MB means no initramfs and no boot.
- Boot image: `gzip -9c Image > out/Image.gz; cat out/Image.gz out/direct.dtb > out/kernel-dtb; mkbootimg --header_version 1 --pagesize 2048 --base 0x10000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --tags_offset 0x00000100 --kernel out/kernel-dtb --ramdisk out/rd -o out/direct.img`. Commit kernel changes and export with `git format-patch -1 --start-number N -o repo/linux-patches`.
- Flash: zero `boot` (sda22, 71303168 bytes, check `ANDROID!` header first), dd the image, read it back and compare sha256 before rebooting. From Linux over SSH (`ssh -F research/ssh_config gts7xl`, sudo) or from TWRP over adb (`/dev/block/by-name/boot`); from TWRP also clear byte 0 of `param`. Modules: copy the .ko to `/lib/modules/7.3.0-gts7xl/...`, run `depmod`.
- Logs: `dmesg` over SSH. If it doesn't boot: TWRP (Power + Vol Up + Vol Down), then `/data/var/log/early/` holds the kernel log of each boot. The serial console only has a shell in the installer.

## Status
The README's status table is the source of truth. Update it when hardware state changes.
