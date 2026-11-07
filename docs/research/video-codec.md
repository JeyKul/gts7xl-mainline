# Video decode/encode (Venus / Iris, SM8250)

Verdict: **upstream-supported, high impact, low-medium effort.** Just enable the node, supply the tablet's own signed firmware, test with V4L2/GStreamer.

## Facts
- Mainline node: `venus: video-codec@aa00000` `compatible "qcom,sm8250-venus"`, status disabled; power-domains MVS0C/MVS0 + RPMh MX/MMCX, OPP 240/338/366/444 MHz, interconnects cpu-cfg + video-mem, iommu 0x2100, `memory-region = <&video_mem>`. [sm8250.dtsi:4332-4400](https://github.com/torvalds/linux/blob/v7.3/arch/arm64/boot/dts/qcom/sm8250.dtsi#L4332). Matches Samsung's vidc: reg 0xaa00000 irq 174, clocks axi0/mvs0c/mvs0, allowed rates 240/338/366/444 MHz, iommu 0x2100 [kona-vidc.dtsi](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/vendor/qcom/kona-vidc.dtsi).
- Drivers in v7.3: **venus** (`drivers/media/platform/qcom/venus`, `sm8250_res`: HFI 6xx, VPU IRIS2, 4 VPP pipes, enc+dec, default fw `qcom/vpu-1.0/venus.mbn`) [core.c:1024-1055](https://github.com/torvalds/linux/blob/v7.3/drivers/media/platform/qcom/venus/core.c#L1024); and **iris** (`qcom,sm8250-venus` also in `iris_probe.c:372`, gen1 HFI, decoder-focused). Both match the same compatible: build only one. Repo has `CONFIG_VIDEO_QCOM_VENUS=m` (defconfig:928) and not iris → venus will bind. Prefer venus (has encoder).
- Board: `video_mem` = 0x8d900000 size 0x500000 already set from Samsung's map (patch 0010). Samsung's kernel has no mem change. DT node not enabled yet (README: ⬜); no board adds `&venus` firmware-name except elish (`qcom/sm8250/xiaomi/elish/venus.mbn`) and MTP/RB5 (default `vpu-1.0`).
- Firmware: production-fused devices need Qualcomm-signed `venus.mbn` matching the device's keys. linux-firmware's `qcom/vpu-1.0/venus.mbn` works on dev boards (RB5/MTP; guess for this tablet: **may be rejected**). Use the tablet's own: Samsung vendor/firmware or `apnhlos`/`firmware-mnt` (look for `venus.mdt/venus.b0x`, `vidc.*`; same partitions the repo already reads for adsp/slpi/a650). Do **not** commit.
- Userspace: V4L2 **stateful** m2m devices `/dev/video-decoder` and `/dev/video-encoder` (nodenames above). Formats: H.264, HEVC, VP8, VP9 decode; H.264/HEVC encode (guess; check `v4l2-ctl -d /dev/video-decoder --list-formats-out`). No AV1 on SM8250 (Venus 6xx IRIS2 predates AV1 decode: **no AV1** — guess ✔ common knowledge). No DRM-secure playback (L1) without TZ cooperation.

## Userspace
- GStreamer: stateful V4L2 uses `v4l2h264dec`/`v4l2h265dec` (`gstreamer`'s `v4l2` plugin in gst-plugins-good), `v4l2h264enc`. Test: `gst-launch-1.0 filesrc location=x.mp4 ! qtdemux ! h264parse ! v4l2h264dec ! videoconvert ! waylandsink`. For dmabuf zero-copy to GPU: `v4l2h264dec ! glimagesink`/`waylandsink` with dmabuf (Venus NV12 usually; UBWC not exposed – plain NV12 or QC08C `NV12_UBWC`? depends, test).
- ffmpeg: `ffmpeg -c:v h264_v4l2m2m -i in.mp4 -f null -` (stateful m2m: decoder `*_v4l2m2m`; encoder `h264_v4l2m2m`). Use `-vf scale` on CPU for NV12→yuv.
- Browsers: Chromium on Linux uses VA-API, not V4L2 stateful (Chromium's V4L2 decoder is ChromeOS-only, and `V4L2StatefulVideoDecoder` is behind flags and only in ChromeOS builds – guess). Firefox: VA-API only (FFmpeg VAAPI). A V4L2→VAAPI shim (`libva-v4l2-request`) targets stateless; none for stateful. So **browser HW decode likely unavailable**; mpv (`--hwdec=v4l2m2m-copy`), VLC (v4l2 plugin), Kodi, GStreamer players work. Firefox 125+ has no V4L2 path either.
- Hantro/Venus on Plasma/GNOME: Totem/Showtime (GStreamer) with `v4l2h264dec` rank raised: `GST_PLUGIN_FEATURE_RANK=v4l2h264dec:MAX,v4l2h265dec:MAX`.

## Staged plan
1. Extract firmware (read-only) from tablet partitions into `/lib/firmware/qcom/sm8250/gts7xl/venus.mbn` (+ split files if `.mdt`+`.bNN`; the loader takes `.mbn` or `.mdt`). Test: `file` shows ELF, size ~ a few MB.
2. DT: `&venus { status = "okay"; firmware-name = "qcom/sm8250/gts7xl/venus.mbn"; };` (same style as elish). Build with venus=m, boot. Test: `dmesg | grep -i venus`, `ls /dev/video*`, `v4l2-ctl --list-devices` shows `Qualcomm Venus video decoder/encoder`.
3. If firmware load fails (auth error from TZ `pas_auth_and_reset`): try linux-firmware `vpu-1.0`; if still rejected the device's TZ wants Samsung-signed image → use Samsung's; check `video_mem` base/size vs TZ expectation (stock 0x8d900000+5 MB; patch 0010 matches).
4. Decode test: GStreamer pipeline above with a 1080p H.264 file; CPU load should be <10 %. Then 4K60 HEVC (bw table tops out at 4096x2160@60) and VP9.
5. Encode: `gst-launch-1.0 v4l2src ... ! v4l2h264enc ! mp4mux` (also webcam/screen recording).
6. Power: confirm clocks/GDSC gate after stop (`/sys/kernel/debug/clk/clk_summary | grep mvs`, genpd summary), and no interconnect vote stuck (feeds suspend.md).
7. Desktop: mpv config `hwdec=v4l2m2m` and Wayland dmabuf output.

## Risks
- Firmware rejected by TZ (signing) – recoverable, node simply fails to probe.
- IOMMU faults if SMMU SIDs differ (stock uses 0x2100/0x2101/0x2103/0x2104; mainline only 0x2100 mask 0x400 – covers 0x2100/0x2104). Non-fatal faults in Samsung config.
- Memory-region base: wrong `video_mem` hangs firmware boot; stock-verified.
- Browser HW decode not realistic; set expectations (software decode in browsers on 8 cores is OK to 1080p).
