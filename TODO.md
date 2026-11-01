# Tab S7+ port TODO

Last updated 2026-11-01.

## Working

- Boot, UFS, USB serial and network
- GNOME 48 on Alpine, GPU (Adreno 650), Wi-Fi
- Touchscreen with multitouch
- S Pen: position and pressure
- Keyboard, until the trackpad is touched
- Battery reading; charging is slow on PC USB
- Emergency reboot: Power + Vol Up + Vol Down, release
- Krita, VLC

## In progress

### Arch Linux base

- [x] Installed in `/arch` on userdata with `rootfs/arch-install.sh`, boots with `/.arch`; remove `/.arch` from TWRP to return to Alpine
- [ ] Limiter back on Arch with `swh-plugins` (ZaMaximX2 is not packaged for Arch ARM); earlier bad sound was `alsa-restore`, not the limiter; minimal distortion at full volume at -15 dB by ear. Amps at stock gain, no bass filter: user says this is the good setup.
- [x] `alsa-restore` wrote saved values over the amp DSP controls at boot, which sounded bitcrushed: masked, audio confirmed by ear
- [x] `linux-firmware-cirrus` overwrote the Samsung amp firmware; restored and excluded with `NoExtract`
- [ ] Run `depmod` after copying modules: kmod ignores a hand-made `modules.dep`
- [ ] Remove Alpine once Arch is confirmed

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

### Native display (paused)

- Panel init runs without errors, frames are sent, but the screen shows full stripes
- Ruled out: TE, lane rate, PPS content (2 bytes differ from stock)
- Next suspects: DSC slice layout, missing stock commands, DPU 2-DSC mode
- Fixes GNOME tearing and stutter, brightness, screen off
- [ ] Follow the rules: stages, 20s auto power-off, cold boot between tests
- Work in progress: `display-wip.dts.txt`, `panel-samsung-s6tuum3.c.wip`

## Open bugs

- [ ] Tablet powered off at 2% battery (UPower action); Arch keeps the default, Alpine has it disabled

- [ ] Trackpad touch crashes the keyboard MCU (stock survives it; not fixed by the memory map change)
- [ ] Pen: side button and hover unconfirmed; range is hand-calibrated
- [ ] `su -c` fails on the tablet
- [ ] Charging: 500mA limit, barely gains on PC USB
- [ ] `reboot-recovery` failed once; emergency combo is the reliable path
- [ ] Clock has no saved time, needs Wi-Fi after boot
- [ ] Brightness: only a software dim overlay (`dim@gts7xl`, set in extension.js) until native display works

## Not started

- [ ] Sensors (SLPI DSP, rotation)
- [ ] Bluetooth
- [ ] USB host mode
- [ ] Suspend: wakes, but the screen stays black until reboot on the firmware framebuffer; targets masked on Arch
- [ ] Cameras
- [ ] Retest GPU zap without the dynamic buffer hack, now that the memory map is right

## Cleanup

- [ ] Remove keyboard frame logging and `.noswitch`
- [ ] Decide `i2c9` speed (100kHz test value still set)
- [ ] Package the Krita `lcms2` link properly
- [ ] Update README for direct boot as the main path

## Restoring Android

Restore `boot`, `dtbo` and `cache` from `backup/partitions`, then format `userdata`.
