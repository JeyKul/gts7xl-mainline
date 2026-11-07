# Fingerprint (under-display)

Verdict: **dead end for now** (TrustZone-owned SPI; no mainline driver; no safe route). Revisit only if someone reverse-engineers the Goodix protocol *and* TZ releases the bus.

## Facts
- Chip: Goodix **GW9558** (optical, under AMOLED), SPI. Samsung DT: `gw9558-spi@0` (factory) + `gw9558@0` (normal), `goodix,chip_id = "GW9558"`, `goodix,modelinfo = "T975"`, power gpio 0x75 (=117), reset gpio 0x7f (=127), spi 25 MHz (0x17d7840). [overlay-r06.dts:13124-13160](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts#L13124)
- SPI controller IRQ 0x260; README: SPI gpio20-23 owned by TrustZone, touching them resets the SoC.
- Samsung adds `qcom,qbt_handler` (ipc-gpio 0x17=23, finger-detect gpio) [L7703](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts#L7703): Qualcomm's "BTP" shim; the SPI transfers happen in the TZ trusted app (Samsung ISEE/TEEgris, Goodix TA), Linux only gets an IPC/IRQ.
- Panel DT: `samsung,support-optical-fingerprint`, `fp_green_circle_on/off` DSI cmds (spot illumination, needed for capture) [L12062, L12215](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts#L12062).
- Mainline: no Goodix optical FP driver. libfprint has Goodix *USB/SPI-less* devices only (goodixmoc etc.), none for GW9558 (guess; verify `fprint.freedesktop.org/supported-devices.html`).

## Usable from Linux?
- Not without TZ. Template data and matching live in the secure world; raw image access over SPI is blocked by xPU (the reset on touch = xPU violation → SoC reset).
- Even if TZ handed over SPI: need GW9558 protocol (proprietary, packed images, per-unit calibration in efs/persist), display-spot sync, a libfprint driver, and a secure place for templates. Very large.

## Staged plan (only if pursued)
1. **Read-only survey, no GPIO touching**: list `/dev/qbt*`-like nodes under Android via ADB (userspace HAL `android.hardware.biometrics.fingerprint` blobs in vendor). Test: identify HAL .so + TA name, no writes. Risk none.
2. Dump Android SPI traffic (Android kernel debug / logic probe on FP FPC). Test: capture one finger-detect + one image transaction. Needs hardware tap; skip unless motivated.
3. Check whether TZ can be told to unassign the SPI (qseecom ownership list in Samsung kernel `drivers/misc/qseecom*`). Test: none safe on-device; **never** poke gpio20-23 (SoC reset).
4. Else: fake it: use pen/PIN/password; or an external USB fingerprint reader supported by libfprint (cheap, works today via USB host, ✅).

## Risks
- SoC reset / possible partition damage from touching xPU-protected pins. Stay out.
- Legal/IP: Goodix protocol and Samsung TAs are proprietary blobs; do not commit.

## Recommendation
Skip. Use a USB libfprint reader (e.g. Goodix 55xx USB / Synaptics) if a reader is wanted; password/PAM otherwise.
