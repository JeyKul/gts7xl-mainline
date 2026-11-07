# GPS / GNSS

Verdict: **probably yes, and it does not need the modem.** The Wi-Fi-only SM-T970 has a *standalone Broadcom GNSS chip on a UART*, not the SM8250 modem-DSP GNSS. Mainline already has a binding for this chip family. Effort medium (find UART pins + chip power sequence), impact medium (maps/navigation outdoors), risk low.

## Facts
- Samsung DT (Wi-Fi model): `gps { compatible = "samsung,exynos54xx-bcm4753"; gpios = <&tlmm 0x5b 0>, <&tlmm 0x37 0>; status = "okay"; }` → **BCM4753**, gpio91 and gpio55 (reset/enable or host-wake; guess which). [overlay-r06.dts:10102](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts#L10102)
- Paired UART node: `qcom,qup_uart@98c000` (`qcom,msm-geni-serial-hs`, clocks 0x5e/0x84/0x85, pinctrl default/active/sleep, wakeup byte 0xfd, `interrupts-extended` incl. gpio 0x7a=122 as wake) at line 10107-10125 of the same file. Mainline: 0x98c000 is QUP **SE3** (`i2c3`/`spi3` nodes exist; no `uart3` node → add `serial@98c000` with `qcom,geni-uart` and flow control pins) [sm8250.dtsi:1528-1553](https://github.com/torvalds/linux/blob/v7.3/arch/arm64/boot/dts/qcom/sm8250.dtsi#L1528).
- Samsung DT has **no** modem/MPSS nodes: grep for `pil_modem`/`mss`/`mba_mem` finds none (only `pil_ipa_*`, `pil_cdsp_mem`); mainline needs a `remoteproc_mpss` + MBA for GNSS-via-modem, which this model never boots (guess: Samsung ships no `modem.mbn` payload in the Wi-Fi SKU's `apnhlos`; verify `ls /mnt/apnhlos | grep -i 'modem\|mba\|qdsp6m'`). The QMI cooling nodes (`qmi-tmd-devices` in kona-thermal.dtsi:58) are generic Qualcomm dtsi, not evidence of a modem.
- Mainline GNSS: `Documentation/devicetree/bindings/gnss/brcm,bcm4751.yaml` (v7.3) lists `brcm,bcm4751/4752/4753`, UART or I2C, props `vdd-auxin-supply`, `vddio-supply`, `reset-gpios`, `enable-gpios`, `current-speed`; UART needs CTS/RTS. [binding](https://github.com/torvalds/linux/blob/v7.3/Documentation/devicetree/bindings/gnss/brcm%2Cbcm4751.yaml). Kernel driver for it: **verify** in `drivers/gnss/` (v7.3 Kconfig shows serial/sirf/ubx/mtk/usb drivers; a bcm4751 driver may be from Linus Walleij's series — check `grep -rn bcm4751 drivers/gnss`). If absent, the chip can still be used from userspace over `/dev/ttyMSMx`.
- Chip protocol: Broadcom BCM4751/4753 speak **NMEA 0183 + BRCM proprietary** (patchram needed? BCM47xx GNSS downloads firmware (`.cpe`/patch) after power-up on Android via `gpsd` HAL, uses `glgps`/`gpslogd`, `/vendor/etc/gps/gpsconfig.xml`, `/vendor/firmware/*.cpe` – guess). Without that download the chip may only emit nothing/ROM boot NMEA.
- Tablet has no cellular → no A-GPS/SUPL by modem; use Wi-Fi/NTP time and `gpsd`+ephemeris download (e.g. via `chronyd`/`gpsd` + `agps`/`pygpsclient` AssistNow isn't BCM). TTFF cold may be 1-3 min; fine for navigation.
- No LTE → UART GNSS is the *only* positioning other than Wi-Fi location (Mozilla/Google location service with BSSIDs via GeoClue; works today with ath11k scan results).

## Staged plan
1. **Wi-Fi location now** (zero kernel): `geoclue` + `gnome-maps`/KDE; verify BSSID database lookup works (privacy: sends BSSIDs to MLS/Google).
2. Read-only recon on stock Android (no flashing): `adb shell ls /vendor/firmware | grep -i 'gps\|gnss\|cpe\|glgps'`, `/vendor/etc/gps/*`, `getprop | grep -i gps`, which tty (`ls -l /dev/ttyHS*`; `qcom,msm-geni-serial-hs` ⇒ `ttyHS<n>`). Test: note tty name, baud (usually 115200→3 Mbaud after patch), flow control.
3. DT: enable `&uart3` (new `serial@98c000`) with RTS/CTS pinctrl from Samsung pinctrl (the three states 0x91-0x96; read pins from Samsung `kona-pinctrl.dtsi`/overlay: ap_gps uart pins gpio 14-17 for SE3? **guess – extract**), plus `gnss { compatible = "brcm,bcm4753"; enable-gpios = <&tlmm 91 GPIO_ACTIVE_HIGH>; reset-gpios = <&tlmm 55 GPIO_ACTIVE_LOW>; vdd-auxin-supply=...; vddio-supply=...; }` (**do not guess supply voltages**; read back or take from Samsung DT per docs/sensors.md rule; if no regulator is specified in Samsung's `gps` node, rails are shared/always-on).
4. Test: `stty -F /dev/ttyMSM? 115200 crtscts; cat /dev/ttyMSM?` after toggling enable; expect NMEA `$GP...` (maybe only after firmware download). If silence: try other baud (921600, 3000000), swap enable/reset polarity (check `gpio` line states in Samsung overlay: both gpios *output*?).
5. If firmware download needed: reuse Android blobs (read from tablet partitions, never committed) + userspace loader (reverse/patchram à la `brcm_patchram_plus`; BCM47xx GNSS patch protocol is *different* from BT HCI – unclear; **probably dead end** if so → fallback: keep chip powered with factory ROM NMEA only).
6. `gpsd -n /dev/ttyMSM?`, `cgps`, `gpsmon`; GeoClue reads gpsd. Test outdoors, clear sky, 5 min cold start, ≥4 sats.

## Risks
- Wrong gpio output on 91/55 (is one of them an *input* host-wake? gpio55 vs 91 roles unknown) – low harm but check pinctrl default states first; power rail unknown → no voltage forcing.
- UART pins 0x98c000 may be shared with other function (check no other consumer; gpio14-17 range overlaps? guess).
- Firmware download protocol unknown (could end the project at step 4).
- Don't touch `modem*`/`efs`/`sec_efs` partitions; read-only.

## Not applicable
- MPSS/QMI/`qcom,gnss-lpa`: skip; no modem on this SKU.
