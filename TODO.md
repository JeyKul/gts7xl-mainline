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
- Krita

## In progress

### Audio (next)

- [ ] Map `cs35l41` amps (i2c11, 0x40-0x43), rails, reset and TDM wiring
- [ ] Get ADSP and amp firmware from the tablet's own partitions
- [ ] Bring up ADSP, then amp probe, then machine driver
- [ ] Start at low volume; bad amp tuning can harm speakers

### Native display (paused)

- Panel init runs without errors, frames are sent, but the screen shows full stripes
- Ruled out: TE, lane rate, PPS content (2 bytes differ from stock)
- Next suspects: DSC slice layout, missing stock commands, DPU 2-DSC mode
- Fixes GNOME tearing and stutter, brightness, screen off
- [ ] Follow the rules: stages, 20s auto power-off, cold boot between tests
- Work in progress: `display-wip.dts.txt`, `panel-samsung-s6tuum3.c.wip`

## Open bugs

- [ ] Trackpad touch crashes the keyboard MCU (stock survives it)
- [ ] Pen: side button and hover unconfirmed; range is hand-calibrated
- [ ] `su -c` fails on the tablet
- [ ] Charging: 500mA limit, barely gains on PC USB
- [ ] `reboot-recovery` failed once; emergency combo is the reliable path
- [ ] Clock has no saved time, needs Wi-Fi after boot

## Not started

- [ ] Sensors (SLPI DSP, rotation)
- [ ] Bluetooth
- [ ] USB host mode
- [ ] Suspend (disabled; reset risk reported on S20 FE, untested here)
- [ ] Cameras

## Cleanup

- [ ] Remove keyboard frame logging and `.noswitch`
- [ ] Decide `i2c9` speed (100kHz test value still set)
- [ ] Package the Krita `lcms2` link properly
- [ ] Update README for direct boot as the main path

## Restoring Android

Restore `boot`, `dtbo` and `cache` from `backup/partitions`, then format `userdata`.
