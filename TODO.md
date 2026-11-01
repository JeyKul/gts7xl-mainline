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

- [x] Reserved memory map matches stock (mainline's was wrong for Samsung)
- [x] ADSP firmware boots (loaded by hand, `qcom_q6v5_pas`)
- [x] All 4 amps probe (i2c11, 0x40-0x43, 1.0uH, 4100mA, slots FL0 FR1 RL2 RR3), loaded by hand
- [ ] Amp firmware from vendor: `cs35l40-spk-dsp1-spk-prot.wmfw` and tuning `.bin`
- [x] Sound card registers: Primary TDM, 4 slots, 48kHz (loaded with `/usr/local/sbin/audio-up`)
- [ ] First sound, very low volume
- [ ] Autoload ADSP at boot
- [ ] Start at low volume; bad amp tuning can harm speakers

### Native display (paused)

- Panel init runs without errors, frames are sent, but the screen shows full stripes
- Ruled out: TE, lane rate, PPS content (2 bytes differ from stock)
- Next suspects: DSC slice layout, missing stock commands, DPU 2-DSC mode
- Fixes GNOME tearing and stutter, brightness, screen off
- [ ] Follow the rules: stages, 20s auto power-off, cold boot between tests
- Work in progress: `display-wip.dts.txt`, `panel-samsung-s6tuum3.c.wip`

## Open bugs

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
- [ ] Suspend (disabled; reset risk reported on S20 FE, untested here)
- [ ] Cameras
- [ ] Retest GPU zap without the dynamic buffer hack, now that the memory map is right

## Cleanup

- [ ] Remove keyboard frame logging and `.noswitch`
- [ ] Decide `i2c9` speed (100kHz test value still set)
- [ ] Package the Krita `lcms2` link properly
- [ ] Update README for direct boot as the main path

## Restoring Android

Restore `boot`, `dtbo` and `cache` from `backup/partitions`, then format `userdata`.
