# Help wanted: unsolved problems

Things we tried and could not solve (yet). If you know SM8250, Samsung firmware or the parts below, an issue or PR is very welcome.
Each entry lists what we know, what we ruled out, and where to look. Details and dates are in `NOTES.md`.

None of these blocks daily use. See the "Missing internals" box in `TODO.md` for the full polish list.

## Deep sleep: CX/AOSS collapse never happens

- **Symptom:** suspended (s2idle) draws about 130 mA; stock One UI suspends (`deep`) at 15–30 mA. `qcom_stats` `aosd`/`cxsd` count stays 0, DDR never enters its sleep state.
- **Ruled out:** apps-side clock and interconnect votes, GPU, ADSP/CDSP/SLPI, PCIe, LPASS, display RSC (mode 2 works, doesn't help alone), AOSS overrides, sending `ipa_pc`/`npu_on` to AOP by hand.
- **Current suspects:** subsystems stock runs and we don't (NPU, CVP, SPSS, Venus), or a TZ/AOP-side condition. IPA + uC now run on mainline (2026-11-15), still being measured.
- **Wanted:** anyone who got `cxsd` counting on any SM8250 device with mainline, or knows what AOP needs to collapse CX.

## NPU firmware: TrustZone rejects PAS init

- `qcom_scm_pas_init_image` for PAS 23 (`npu.mdt` from `apnhlos`) fails with -EINVAL. CVP (PAS 26) loads on the same path.
- Likely needs the NPU core powered (npucc clocks, NPU GDSC) before TZ accepts it. Mainline has no sm8250 NPU driver.

## Pogo keyboard trackpad drops after under a second

- The keyboard keeps working and the trackpad stops. Same on the stock kernel under TWRP, fine on One UI.
- Not caused by: BOB regulator, bus speed or MCU pins.
- Lead: the sensor DSP stack (`sns_registry` in `hexagonrpcd`); the MCU watches a second I2C bus towards it.

## USB-C controller sometimes reads blank

- MAX77705 reads `fe 00 00`, survives a reboot, is cleared by a replug. While blank it charges at 5 V. Cause unknown.

## Fingerprint

- The sensor sits behind a TrustZone trusted app. There is no known open path.

## Ultrawide camera

- Colours and geometry are a little off; tuning data is not understood yet.

## HDMI on the second DP pin mapping

- DP alt mode does not work on every adapter. One UI's HDMI also failed in our test build.

## Error codes we could not decode

- TZ PAS init returning `a0 = 0xffcfffbb` (−0x300045) for wrongly signed IPA firmware. The meaning of that code family would help with NPU.
