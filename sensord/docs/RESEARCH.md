# Research notes

Status: **F** read in a source, **I** inferred, **G** guess. Checked 2026-11-04. Nothing here was tested on a Tab S7+.

## What mainline needs for the SLPI

| Item | Value | Status | Source |
|---|---|---|---|
| Remoteproc node | `slpi: remoteproc@5c00000`, `qcom,sm8250-slpi-pas`, `status = "disabled"` | F | Linux v7.3 `sm8250.dtsi:3091` |
| Driver data | `sdm845_slpi_resource_init`: firmware `slpi.mdt`, PAS id 12, ssctl 0x16, PDs lcx and lmx, `auto_boot` | F | `qcom_q6v5_pas.c:1382,1893` |
| Carveout | `slpi_mem` at 0x89200000, size 0x1500000: repo patch 0010 matches stock | F | repo `0010`, stock `kona.dtsi:460` |
| FastRPC | `fastrpc` child, label `sdsp`, `qcom,non-secure-domain`, three compute CBs, inside the SLPI node | F | `sm8250.dtsi:3129` |
| Device node | `/dev/fastrpc-sdsp` | I | label and `qcom,non-secure-domain` in `fastrpc.c` |
| Sensor PD attach | `FASTRPC_IOCTL_INIT_ATTACH_SNS` | F | `fastrpc.c:2321`, hexagonrpc `data/hexagonrpcd-sdsp.service.in` |
| PD mapper | `msm/slpi/root_pd` and `sensor_pd` listed for sm8250 | F | `qcom_pd_mapper.c:579` |
| Kernel config | `QCOM_Q6V5_PAS`, `QCOM_FASTRPC`, `QRTR_SMD`, `RPMSG_QCOM_GLINK_SMEM`, `QCOM_SYSMON`, `INPUT_UINPUT` are all `=m` | F | repo `gts7xl_defconfig` |
| Firmware name | Sibling boards use `qcom/sm8250/Samsung/r8q/slpi.mbn`; the repo's own convention gives `qcom/sm8250/gts7xl/slpi.mbn` | F | `sm8250-samsung-r8q.dts:23` |

**Does the repo have it?** No. It has the carveout but no `&slpi` node. `linux-patches/0040` adds it (`patches/` in the tarball). The DT compiles: `cpp` + `dtc` over the board file rebuilt from all repo patches gave a DTB with the node `okay`. A kernel build was not run.

**README note on `hexagonrpcd`:** the repo records starting the SLPI by hand, so the firmware probably boots on this tablet, and `hexagonrpcd` stopping at `sns_registry`. The next section explains that stop.

## The crucial precedent: Tab S8+ (SM8450)

[aaronsb/sm-x800-linux](https://github.com/aaronsb/sm-x800-linux) at `6f3248548`, `docs/12-sensors.md`, walked this path on hardware in September 2026. Different SoC and tablet, so treat these as strong hints for us (**I**):

1. The firmware asks the host for a local interface `sns_registry`, method 0, `get_property(in name, rout value)`, and reads `ro.revision`. Upstream `hexagonrpcd` lacks it.
2. It **writes**: `sns_reg_version`, and `temp.json` renamed into the registry. Refused writes are retried once a second. Read-only serving alone never finishes first init. Writes stop on later boots.
3. It reads the revision from `/sys/class/sensors/ssc_core/ssc_hw_rev`, a Samsung path named in `sns_reg_config`.
4. With a complete registry its sensor process crashed 13 s after every boot, 6 of 6 (`sns_com_port_i2c.c:241 ... I2C_ERROR_TRANSFER_TIMEOUT`), until the two rails from the stock SLPI node (`sensor_vdd`, `sensor_vddio`) were declared always-on.
5. Once initialised the SLPI keeps serving without the daemon, and restarting the daemon while the SLPI runs upsets the session.
6. The stock `persist` partition holds a registry with this unit's factory calibration, which can be copied in.
7. `Dead lead` in their tools README: a keyboard MCU string naming "SLPI" is that firmware's name for its own host I2C port. This bears on your open trackpad lead (**I**).

**Consequence:** `--trace-fastrpc` is read-only as asked, but `--serve` must allow writes. It allows them only under `mnt/vendor/persist/sensors`, using `openat2(RESOLVE_BENEATH)` for confinement.

**Rails on this tablet:** the stock `qcom,ssc@5c00000` node in `kona.dtsi:4583` has only `vdd_cx` (L11A) and `vdd_mx` (L4A), both already covered by the lcx and lmx PDs. It names no sensor rail, unlike SM8450. The JSON names `/pmic/client/sensor_vddio`, whose mapping lives in firmware. So no rail is in the patch. Whether one is needed is unknown (**top uncertainty**), and the test plan watches for that crash signature.

## Stock firmware layout

| What | Where | Status | Source |
|---|---|---|---|
| `slpi.mdt`, `slpi.b*` | `apnhlos` partition, `image/` | **G**: adsp is there on this tablet (repo `gts7xl-firstboot`), and S8+ takes slpi from there | repo, x800 `fw-extract.sh` |
| Sensor JSON, `sns_reg_config` | `/vendor/etc/sensors/{config/,sns_reg_config}` | F, file list | LineageOS `android_device_samsung_gts7xlwifi` `proprietary-files.txt:140` |
| Registry | `/mnt/vendor/persist/sensors/registry/registry` | F | `output=` in stock `sns_reg_config`; LineageOS sm8250-common `init.samsung.rc:206` |
| Registry partition | `persist`, ext4 | **G**: from the mount path and S8+ | |

`sns_reg_config` as shipped (read from `Bush-cat/android_vendor_samsung_gts7xlwifi`, not copied here):

    file=hw_platform=/sys/devices/soc0/hw_platform
    file=platform_subtype=/sys/devices/soc0/platform_subtype
    file=platform_subtype_id=/sys/devices/soc0/platform_subtype_id
    file=platform_version=/sys/devices/soc0/platform_version
    file=soc_id=/sys/devices/soc0/soc_id
    file=revision=/sys/class/sensors/ssc_core/ssc_hw_rev
    file=output=/mnt/vendor/persist/sensors/registry/registry

These values select which JSON applies, so wrong ones can silently give no sensors:

| Value | Used | Status |
|---|---|---|
| `soc_id` | live, else 356 (`kona_*.json`) | F |
| `hw_platform` | `MTP`: stock DT `qcom,kona-mtp`, board-id 8; JSON accepts MTP, Surf, RCM, QRD | I |
| `platform_subtype`, `_id`, `platform_version` | `Unknown`, 0, 65536 | G |
| `revision` | `androidboot.revision` from the cmdline, else 6 | G. Matters: `kona_ak991x_0` is revs 0-1, `_2` is 2-5, `_6` is 6+ |

Sensors, from those JSON files: LSM6DSO accel/gyro on SSC bus type 3 instance 1, 0x6b, I3C address 10; AK0991x magnetometer on SSC I2C instance 2, 0x0c. VEML3328 light is listed (`kona_veml3328_*.json` in `proprietary-files.txt`); its bus was not read. Stock orientation `x: -y` starts `lsm6dso_0_platform.orient`. Stock HALs: `sensors.ssc.so`, `sensors.grip.so`, `sensors.flicker.so`.

## Sensors not behind the SLPI

Sources: Samsung's tree as carried by [LineageOS/android_kernel_samsung_sm8250](https://github.com/LineageOS/android_kernel_samsung_sm8250) at `8fd4a48`, `arch/arm64/boot/dts/samsung/`. [shezik/samsung-kernel-T970XXS7DXH1](https://github.com/shezik/samsung-kernel-T970XXS7DXH1) says it is the plain Samsung release; it was found but not compared, so the LineageOS copy is what was read. Board files `gts7xl/kona-sec-gts7xlwifi-r02..r06.dtsi`.

| Sensor | Connection | File |
|---|---|---|
| Hall, flip cover | TLMM 157, flags 0x1 (active low), no bias, code 21 (Samsung-specific, no upstream switch has it) | `drivers/eif_gts7xl_common.dtsi:73` |
| Hall, Wacom cover | TLMM 110, flags 0, no bias, code 21 | same, `:91` |
| `hall_logical` | virtual: no GPIO of its own, code 21. Likely fed by the keyboard driver (I), so this may be why neither pin reacts to the keyboard cover | same, `:84` |
| Grip, main: ABOV `a96t3x6` at 0x20 | bit-banged I2C, SDA 112, SCL 113; IRQ 67, LDO enable 64 | `drivers/kona-sec-gts7xl-grip-r00.dtsi`, r02 overrides |
| Grip, Wi-Fi: `a96t3x6_wifi` at 0x20 | bit-banged I2C, SDA 44, SCL 45; IRQ 51, LDO enable 75 | same; firmware `abov/a96t356_gts7xl_wifi1.bin` |
| Thermistors | PM8150/PM8150L ADC, the repo reads some | `gts7l/kona-sec-gts7-pm-common.dtsi` |

No light sensor, accelerometer or magnetometer node exists on a CPU bus in the gts7xlwifi files searched. No flicker sensor node was found either; it may sit on top of the SSC (G). There is no mainline `a96t3x6` driver in the v7.3 files searched (not exhaustive).

## Not done

- `gts7xl-firstboot` and the installer do not call the extractor yet. That is one line once step 2 of the test plan works.
- No kernel patch beyond the node; no rail, since none is known.
- Light and compass reach `--stream`, not `iio-sensor-proxy`.

## Protocol facts used

Reverse tunnel (Qualcomm's own listener is the authority):

- Sequence: open the device, attach, `remotectl_open("adsp_default_listener")` on handle 0, call its method 0, then `adsp_listener` on handle 3: `init2` (3), `next2` (4), `get_in_bufs2` (5). Inline request buffer 256 bytes.
- Request stream, each input: u32 length; if nonzero, align to 8 and data. Then one unpadded u32 per output giving its size. `inc/listener_buf.h`, `src/listener_android.c:252`.
- Reply stream: u32 length; if nonzero, align to 8 and data. Lengths must equal the requested ones (`VERIFY` in `unpack_out_bufs`).
- `apps_std` method numbers: order in `idl/apps_std.idl`; ids above 30 go in the first primary word, with `sc` method 31.
- `sns_registry` layout: x800 patch 0006, from disassembling the SM8450 skeleton.

SSC, from libssc (reverse-engineered) and libqmi:

- QRTR service 400 (`QMI_SERVICE_SSC` 0x190). QMI request 0x20: TLV 0x01 data (u16 length prefix), TLV 0x10 report type (1 = large). Indications 0x21 and 0x22: TLV 0x01 client id, TLV 0x02 data (u16 prefix). `libqmi data/qmi-service-ssc.json`.
- Request: 1 uid {1 low, 2 high: fixed64}, 2 msg id (fixed32), 3 config {1 processor=1, 2 delivery}, 4 request {2 msg bytes, 3 passive}. Report: 1 uid, repeated 2 {1 id fixed32, 2 ts fixed64, 3 bytes}.
- SUID service uid `0xABAB...AB` both halves. Ids: SUID 512, reply 768; attributes 1, reply 128; enable 513 (`{1 rate float}`) or 514 for on-change; disable 10; sample 1025 `{repeated 1 float, 2 accuracy}`. `libssc data/*.proto`, `src/libssc-common-private.h`.
- Delivery value 1 for no-wakeup: **G**. libssc names only `WAKEUP = 0`.
- A real captured indication in libssc's docs decodes correctly in `tests/test.c`, so the framing is checked against one real message, but only one, from another SoC.

iio-sensor-proxy (`src/drv-input-accel.c`, `data/80-iio-sensor-proxy.rules`): see README.

## Licences

| Project | Licence | Used for |
|---|---|---|
| hexagonrpc `da7a374` | GPL-3.0+ (`COPYING`); `libhexagonrpc/*.c` LGPL-3.0+ | read for facts, no code taken |
| libssc `033e2a3` | GPL-3.0+, including its `.proto` files | read for facts; **`.proto` files not vendored**, messages are hand-encoded |
| libqmi `3454c2c` | ships GPL-2 and LGPL-2.1 texts; per-file terms not checked | ids from `qmi-service-ssc.json` |
| iio-sensor-proxy `ca0d5b0` | GPL-3.0 | behaviour read, no code |
| qualcomm/fastrpc `a56e9d4` | BSD-3-Clause | wire format and method tables, no code |
| x800 patches | repo carries two licences; these patch GPL-3 hexagonrpc | facts only |
| Linux UAPI headers | GPL-2.0 WITH Linux-syscall-note | included at build time |
| Samsung DT, stock JSON | read only | not copied; no blobs in this package |

**Decision: MIT.** All code here was written fresh, with its own structure. Wire formats and message numbers are facts, not expression. Having read GPL sources closely, a clean-room claim would be wrong, so if you want no argument at all, relicense as GPL-3.0-or-later: nothing here prevents it. This is a judgement, not legal advice.
