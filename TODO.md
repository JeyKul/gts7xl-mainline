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
- [x] Pen: side button, hover and tilt confirmed (2026-11-08); range is hand-calibrated
- [ ] `su -c` fails on the tablet
- [ ] Charging: input limit from CCIStat / BC1.2 without PD (PC port reads 1.4 A while CC says 500 mA)
- [ ] `reboot-recovery` failed once; emergency combo is the reliable path
- [ ] Clock has no saved time, needs Wi-Fi after boot
- [x] Real brightness through `kmod/gts7xl-panel-ctl`
- [ ] Fix tearing by swapping plane addresses during blank

## Next targets

- [x] Bluetooth: pairing and speaker audio work
- [ ] Bluetooth: build RFCOMM, BNEP, HID modules
- [x] Fast charging: 9V through the MAX77705 mailbox (`rootfs/gts7xl-charge-control`), stock limits; since 2026-11-07 the kernel driver (0053) negotiates it and charge-control only does thermal and charge current
- [x] Fast charging: service renegotiates 9V after a replug
- [ ] Fast charging: USB-C controller sometimes reads blank (`fe 00 00`), survived a reboot, cleared by a replug; cause unknown, charges at 5V while blank
- [x] A real USB-C driver to handle its interrupts: `max77705_usbc` (0053), autoloads
- [ ] Fast charging: other chargers, `current_now` scale
- [ ] Fast charging: at least 25W, user's goal
- [ ] Battery reads 27.1C from the fuel gauge, which is fake; `batt_thm` is the real value
- [ ] Sleep: s2idle works, about 2%/h measured by the user over 15h46 (79% to 48%), roughly 0.8W; find what keeps it above stock levels
- [x] IRQ 156 (MAX77705) storm: wrong gpio11 pin config, fixed with stock bias-pull-up + power-source 1 (2026-11-07)
- [x] Use the RTC offset to keep time across reboots (`gts7xl-rtc-offset`, 2026-11-08)

## Later

- [ ] Minimal repo: strip what is no longer used, when the user decides it is time
- [ ] S Pen: test the built-in pen, reporting its charge toggle discovery; handle the Pro's `13` and `ff....` events
- [ ] S Pen rotation actions, as Samsung uses for camera zoom: check which pens report it, add to `gts7xl-spen` and Pen Actions

## Next hardware

- [x] Flashlight, with a `Flashlight` app
- [x] Microphone: sub mic (DMIC3) through the VA macro, +24 dB, PulseAudio source `mic`; voice recording confirmed by the user. DMIC2 only mirrors it, no second mic there
- [ ] Main mic (DMIC1) reads zero: pins, clock and L5C on, cause unknown
- [ ] L5C mic rail is always on; tie it to capture
- [ ] Check UFS `ufshcd_query_attr` errors and `DSI PLL lock failed` warning seen at boot during mic work
- [x] Rear camera: raw frames through CAMSS, 4128x3096; `libcamera` lists it
- [x] Rear camera focus motor: dw9808 through `dw9807-vcm`, position set by hand gives a sharp image
- [x] Camera app with preview, tap to focus and full size photos
- [ ] Rear camera: colour tuning, GPU debayer fails
- [ ] Focus motor powers down when its device closes, so a manual position only holds while something keeps it open
- [x] Front (`s5k4ha`) and ultrawide (`s5k5e9`) cameras through `s5k-tab`, raw frames and chip IDs confirmed
- [x] Colour order and black level confirmed; low resolution sensor modes stop the zoomed preview
- [ ] Camera tuning in good light: auto exposure settles slowly, gain scale of `s5k-tab` unconfirmed, no colour matrix
- [x] `gts7xl-webcam`: front camera as `/dev/video42` for apps such as Telegram, runs only while read
- [ ] Webcam: choose which camera it uses; other apps through PipeWire

## Not started

- [ ] Sensors: motion, compass and light chips sit on the SLPI bus (`sm8250.dtsi` has no `qcom-ssc-block-bus` node; no route proven or ruled out). Goal is native, minimal Android parts; no direct route known
- [ ] Detect the keyboard cover closing: `gpio157`, `gpio110` and the MCU hall message do not change
- [x] USB host mode: automatic roles, OTG VBUS, hubs, sticks, USB-C headset (0051-0054)
- [ ] USB-C: CCIC sends no detach interrupt while sourcing; covered by the VBUS guard, cause unknown
- [ ] USB-C: charger input limit reads a sawtooth while the boost runs
- [ ] USB-C: tray app (`org.gts7xl.usbmode`) check in Plasma; manual role swap (op 0x37) not done
- [x] DP alt mode / HDMI: automatic, both orientations, 4K60, audio (0055-0059, 2026-11-07)
- [ ] DP: tablet as power source (hub without charger): firmware never discovers, Discover Identity jams the mailbox
- [ ] DP: hub sometimes needs a power cycle after a tablet-side replug (phone recovers on the same hub)
- [ ] USB 3 on pin D: dwc3 built in, QMP a module, so dwc3 times out on deferred probe
- [ ] DP pin C (4 lanes): AUX fails after the QMP switches to DP-only
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
