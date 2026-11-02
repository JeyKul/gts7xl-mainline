# Tab S7+ port TODO

Last updated 2026-11-02.

## Working

See the status tables in `README.md`.

## In progress

### Arch Linux base

- [x] Installed in `/arch` on userdata with `rootfs/arch-install.sh`, boots with `/.arch`; remove `/.arch` from TWRP to return to Alpine
- [ ] Limiter back on Arch with `swh-plugins` (ZaMaximX2 is not packaged for Arch ARM); earlier bad sound was `alsa-restore`, not the limiter; minimal distortion at full volume at -15 dB by ear. Amps at stock gain, no bass filter: confirmed perfect by ear.
- [x] `alsa-restore` wrote saved values over the amp DSP controls at boot, which sounded bitcrushed: masked, audio confirmed by ear
- [x] `linux-firmware-cirrus` overwrote the Samsung amp firmware; restored and excluded with `NoExtract`
- [ ] Run `depmod` after copying modules: kmod ignores a hand-made `modules.dep`
- [x] Alpine removed

### Audio (next)

- [x] Reserved memory map matches stock (mainline's was wrong for Samsung)
- [x] ADSP firmware boots (loaded by hand, `qcom_q6v5_pas`)
- [x] All 4 amps probe (i2c11, 0x40-0x43, 1.0uH, 4100mA, slots FL0 FR1 RL2 RR3), loaded by hand
- [x] Amp firmware and tuning loaded on all 4, factory calibration from `sec_efs:/cirrus` applied (driver patch, files in `/lib/firmware/cirrus`, not in repo)
- [x] Sound card registers: Primary TDM, 4 slots, 48kHz (loaded with `/usr/local/sbin/audio-up`)
- [x] First sound and music, speed and pitch correct
- [x] Autoloads at boot, PulseAudio sink `speakers`, amps at stock gain
- [ ] Root cause of distortion unknown; limiter reduces it, amp gain now stock, retest
- [x] Amps fail to wake from hibernate with this firmware (`-ENOMSG`, then `-110`): runtime PM off, PulseAudio idle suspend off
- [x] Limiter sink `limited` (ZaMaximX2, -6 dB) in front of the speakers: distortion a lot better by ear, autoloads
- [ ] S24 sink and the equalizer sink together made audio barely audible; S16 kept, equalizer sink removed
- [ ] `CAL_AMBIENT` reads back 0, stored value 29; unexplained
- [x] 4.0 output, one TDM slot per speaker; positions confirmed by ear
- [ ] Cap removed at user request: stock gain (Analog 17, Digital 817); `CAL_AMBIENT` still unexplained

### Native display

- [x] Works at 60Hz through `msm` and `panel-samsung-s6tuum3`; details in `docs/display.md`
- [x] 120Hz and 60Hz modes offered to GNOME
- [ ] Confirm 120Hz: the last live check still showed 60Hz in use
- [ ] Screen off when idle (the overlay only draws black)
- [ ] Retry suspend with the native driver
- [ ] Remove the unneeded link changes, then try sending the `msm` patch upstream

## Open bugs

- [ ] Tablet powered off at 2% battery (UPower action); Arch keeps the default, Alpine has it disabled

- [ ] Trackpad drops after under a second, keyboard recovers. Same on the stock kernel in TWRP, fine on One UI. Not caused by BOB, bus speed or MCU pins. Open lead: sensor DSP stack needs `sns_registry` in `hexagonrpcd`; the MCU watches a second I2C bus towards it
- [ ] Pen: side button and hover unconfirmed; range is hand-calibrated
- [ ] `su -c` fails on the tablet
- [ ] Charging: 500mA limit, barely gains on PC USB
- [ ] `reboot-recovery` failed once; emergency combo is the reliable path
- [ ] Clock has no saved time, needs Wi-Fi after boot
- [x] Real brightness through `kmod/gts7xl-panel-ctl`
- [ ] Fix tearing by swapping plane addresses during blank

## Next targets

- [x] Bluetooth: pairing and speaker audio work
- [ ] Bluetooth: build RFCOMM, BNEP, HID modules
- [x] Fast charging: 9V through the MAX77705 mailbox (`rootfs/gts7xl-charge-control`), stock limits
- [x] Fast charging: service renegotiates 9V after a replug
- [ ] Fast charging: USB-C controller sometimes reads blank (`fe 00 00`), survived a reboot, cleared by a replug; cause unknown, charges at 5V while blank
- [ ] Fast charging: other chargers, `current_now` scale, a real USB-C driver to handle its interrupts
- [ ] Fast charging: at least 25W, user's goal
- [ ] Battery reads 27.1C from the fuel gauge, which is fake; `batt_thm` is the real value
- [ ] Sleep: s2idle works, about 2%/h measured by the user over 15h46 (79% to 48%), roughly 0.8W; find what keeps it above stock levels
- [ ] IRQ 156 (MAX77705, spmi-gpio 10) fires nonstop and gets disabled about a minute after boot: `nobody cared`
- [ ] Use the RTC offset to keep time across reboots

## Next hardware

- [x] Flashlight, with a `Flashlight` app
- [x] Microphone: sub mic (DMIC3) through the VA macro, +24 dB, PulseAudio source `mic`; voice recording confirmed by the user. DMIC2 only mirrors it, no second mic there
- [ ] Main mic (DMIC1) reads zero: pins, clock and L5C on, cause unknown
- [ ] L5C mic rail is always on; tie it to capture
- [ ] Check UFS `ufshcd_query_attr` errors and `DSI PLL lock failed` warning seen at boot during mic work
- [ ] Cameras

## Not started

- [ ] Sensors: motion, compass and light chips sit on the SLPI bus (`qcom-ssc-block-bus` only covers SDM630 here). Goal is native, minimal Android parts; no direct route known
- [ ] Detect the keyboard cover closing: `gpio157`, `gpio110` and the MCU hall message do not change
- [ ] USB host mode
- [ ] Suspend: wakes, but the screen stays black until reboot on the firmware framebuffer; targets masked on Arch
- [ ] Retest GPU zap without the dynamic buffer hack, now that the memory map is right

## Installer in boot

Decision: Linux or Android, not both. An installer in the boot image starts when no distro is found on `userdata`.

- [x] Stage 1 shows a touch interface through the native display, built by `rootfs/stage1/assemble.sh`
- [x] Touch, Wi-Fi join and reboot from the installer
- [x] Download the Alpine installer into RAM, check its hash, switch to it
- [x] Wi-Fi: networks are listed from the initramfs
- [x] Format `userdata`, download and unpack Arch, create the user, extract firmware
- [x] Full wipe and install from the installer reaches GNOME, tested on the tablet with a locally served installer
- [ ] Options, region and device information
- [x] Desktop choice: GNOME, KDE and KDE Mobile run on the tablet
- [ ] KDE Mobile feels laggy; low priority
- [x] Wi-Fi uses the tablet's real address from `efs`, so the IP stays the same
- [ ] Phosh: shell exits with Wayland protocol error 71 after start, at 60Hz and 120Hz; not offered by the installer
- [x] Reinstall from the desktop
- [ ] ztOS (github.com/Zerone-Laboratories/ztOS, GPL-3.0): build its GSI in CI and write a device layer for this tablet; listed in the installer as not ready
- [x] Hold volume up at power on to open the installer
- [x] Installer progress bars and rounded UI
- [ ] Waydroid: enable binder in the kernel
- [ ] `f2fs` as root filesystem, optional

## Release

- [ ] Run the full GitHub build
- [ ] Test the TWRP zip end to end on a wiped tablet
- [ ] Test the Odin package
- [ ] Test a LineageOS `vendor`

## Cleanup

- [ ] Remove `.noswitch`
- [ ] Remove or keep `gts7xl-early-log`, which writes the kernel log to `/var/log/early`
- [ ] Decide `i2c9` speed (100kHz test value still set)
- [ ] Package the Krita `lcms2` link properly

## Restoring Android

Restore `boot`, `dtbo` and `cache` from `backup/partitions`, then format `userdata`.
