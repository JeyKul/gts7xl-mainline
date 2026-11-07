# Research roadmap (2026-11-07)

Terse, AI-friendly. Sources: Samsung GPL kernel `shezik/samsung-kernel-T970XXS7DXH1` (dev), Linux v7.3, this repo. Items marked "guess" are unverified. Voltage rule (docs/sensors.md) and CLAUDE.md safety rules apply: no rail changes by round numbers, no writes to efs/modem/persist/tz/etc., firmware read from the tablet, never committed.

| # | Topic | Effort | Impact | Risk | Verdict |
|---|---|---|---|---|---|
| 5 | [video-codec](video-codec.md) | S-M (DT node + extract fw) | High (video, battery) | Low | Do first; upstream venus supports sm8250 |
| 3 | [rtc](rtc.md) | S (userspace) / S (DT `allow-set-time`) | Medium (time at boot, alarms) | Low | Quick win |
| 2 | [suspend](suspend.md) | M-L (measure, iterate) | High (battery) | Medium (rails/DSPs) | Measure first, then stop sensord/Wi-Fi on suspend |
| 4 | [thermal](thermal.md) | M | Medium (perf) | Medium (heat, battery) | Pull Samsung skin config first; 85-90 °C CPU/GPU trips OK for silicon |
| 6 | [cdsp](cdsp.md) | S (boot) | Low | Low | Boot it, little to use |
| 7 | [gps](gps.md) | M | Medium | Low | **Separate BCM4753 GNSS chip on UART** (not modem); mainline has a binding; firmware download protocol is the unknown |
| 8 | [drm-widevine](drm-widevine.md) | S | Medium (streaming) | Low | Works if glibc ≥ 2.36 (check ALARM); L3 only (SD/720p) |
| 1 | [fingerprint](fingerprint.md) | XL | Low | High (SoC reset) | Dead end; use a USB libfprint reader |

## Recommended order
1. **Venus** (video-codec): extract `venus.mbn` from the tablet, `&venus { status="okay"; firmware-name=... }`, test GStreamer/mpv/ffmpeg. Feeds drain numbers too.
2. **RTC** userspace offset now, then `allow-set-time`. Cheap and fixes a daily annoyance.
3. **Suspend measurement**: `qcom_stats` + `charge_counter` baseline, then stop sensord/Wi-Fi around suspend and track mA. Biggest battery win; informs which DSP/rail blocks.
4. **Widevine** (if glibc ok) in parallel: pure userspace.
5. **GPS recon** (read Android `/vendor/firmware`, tty name) – cheap to learn if it is feasible.
6. **Thermal**: get Samsung skin/battery config, then conservative CPU/GPU trips + userspace skin cap.
7. CDSP boot (after codec), fingerprint last/never.

## Cross-cutting
- Firmware extraction path: reuse `gts7xl-sensord-extract` pattern (read-only from `apnhlos`/`vendor`). Needed for venus, cdsp, maybe GNSS.
- Samsung DT used throughout: `arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts` + `vendor/qcom/kona-*.dtsi`.
- Every DT change: one per boot, TWRP at hand (blank-panel risk when new nodes touch shared rails/GPIOs).
