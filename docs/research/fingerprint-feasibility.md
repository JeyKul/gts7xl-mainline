# Fingerprint reader on mainline: feasibility study

Date: 2026-11-08. Scope: Goodix GW9558 under-display sensor on the SM-T970
(gts7xlwifi), mainline Linux v7.3 + this repo's patches. This page builds on
[`docs/fingerprint.md`](../fingerprint.md). Read that first.

## Verdict

**Feasible, but hard.** Confidence that enrollment and verification will
work end to end: **medium (about 60 %)**. Confidence that we can load
Samsung's fingerprint TA from Linux and exchange commands with it: **higher
(about 80 %)**.

The plan is to run Samsung's own trusted app (TA) and Samsung's own userspace
library (`libbauthserver.so`). Both come unmodified from the tablet. Three
pieces of work make this cheaper than first thought:

1. **The Linux sensor driver is almost empty.** Samsung's `gw9558x` driver
   handles `GF_IOC_ENABLE_SPI_CLK` by taking a wakelock and nothing else. The
   rest is one power GPIO and one reset GPIO.
2. **Samsung's TA glue calls only five `libQSEEComAPI` functions.** We can
   ship our own `libQSEEComAPI.so` in the bionic root instead of copying the
   downstream `/dev/qseecom` + ION ABI.
3. **A mainline QSEE transport driver already exists.** It does
   split-ELF TA loading and listeners on top of `qcom_tzmem`. It's new (Nov
   2026) and unproven (see below), but it's a ready starting point.

The remaining risks are not about TrustZone:
- **Auth token:** enrollment may require a valid hardware auth token (HAT)
  from the gatekeeper.
- **Illumination:** the optical sensor needs the panel driven into its
  "mask"/HBM mode while it captures.
- **Calibration data:** the factory calibration has to be present (`/efs`).

## 1. What mainline offers today

### `qcom_scm` QSEECOM bits (`drivers/firmware/qcom/qcom_scm.c`, `qcom_qseecom.c`)
- **What exists:** `qcom_scm_qseecom_app_get_id()` (`QSEECOM_TZ_CMD_APP_LOOKUP`)
  and `qcom_scm_qseecom_app_send()` (`APP_SEND`). Mainline has **no app
  start/load, no shutdown and no listener support**.
- **Listener requests:** if TZ returns `INCOMPLETE` or `BLOCKED_ON_LISTENER`,
  `qcom_scm_qseecom_call()` only prints a `WARN_ON` ("TODO: Handle incomplete
  and blocked calls").
- **Who uses it:** the only consumer is `qcom_qseecom_uefisecapp`. It talks to
  an app that firmware has **already loaded**.
- **Machine allowlist:** the interface is only created for machines on
  `qcom_scm_qseecom_allowlist`, which is mostly X1E/SC8280XP laptops. On this
  tablet (`CONFIG_QCOM_QSEECOM=y` in `config/gts7xl_defconfig`) dmesg should
  show `qseecom: found qseecom with version 0x…` followed by `untested
  machine, skipping`.
- **Conclusion:** this interface **cannot load a TA from `.mdt`/`.bNN`**.
  - https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_scm.c (see `qcom_scm_qseecom_call`, `qcom_scm_qseecom_allowlist`)
  - https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_qseecom.c

### `qcom_tzmem` (`drivers/firmware/qcom/qcom_tzmem.c`)
- **What it does:** allocates memory for TZ and sets up SHM bridges.
- **SM8250 status:** SM8250 is **not** on `qcom_tzmem_blacklist` (that list
  has sc7180, sc8180x, sdm670, sdm845, sm7125, sm7150, sm8150). Our defconfig
  has `QCOM_TZMEM_MODE_SHMBRIDGE=y`.
- **Consistent with downstream:** the downstream kona kernel also registers
  SHM bridges (`qtee_shmbridge.c`) for its qseecom heaps.
- **For us:** any new driver should allocate its TA, command and listener
  buffers from `qcom_tzmem`.

### `qcomtee` (`drivers/tee/qcomtee`, smcinvoke / object-based QTEE)
- **What it is:** generic object invocation through the TEE subsystem.
- **Userspace:** [quic-teec](https://github.com/quic/quic-teec) provides
  `libqcomtee`. Its test shows `IAppLoader_loadFromBuffer` (service UID 3)
  loading a TA. [minkipc](https://github.com/qualcomm/minkipc) provides
  `qtee_supplicant`, which:
  - reads **split `.mdt`/`.bNN` TAs** (`listeners/libtaautoload/src/taImageReader.cpp`);
  - implements the FS (0xa), GPFS (0x7000), RPMB (0x2000) and time (0xb)
    listeners as callback objects.
- **SM8250 will most likely not probe it.**
  - `qcom_scm_qtee_init()` probes `QCOM_SCM_SMCINVOKE_INVOKE` (cmd **0x02**).
  - The kona downstream `smcinvoke.c` uses `SMCINVOKE_INVOKE_CMD 0x32000600`,
    which is the **legacy cmd 0x00** (`QCOM_SCM_SMCINVOKE_INVOKE_LEGACY`).
    Mainline defines that constant but never uses it.
  - Konrad Dybcio said in review that "platforms requiring
    QCOM_SCM_SMCINVOKE_INVOKE_LEGACY (0x00)" fail this probe
    (https://lore.kernel.org/all/?q=QCOM_SCM_SMCINVOKE_INVOKE_LEGACY).
  - Expect no `qcomtee` platform device and no `/dev/tee*` from it.
    **Unverified on the device;** it's test 0 below.
- **Even with a patched probe, it's the wrong tool.**
  - The argument layout matches: downstream `SMCINVOKE_INVOKE_PARAM_ID 0x224`
    equals mainline `QCOM_SCM_ARGS(4, RW, VAL, RW, VAL)`. So a
    "try cmd 0x00" patch is plausible.
  - But Samsung's TAs are *legacy QSEE apps*. On Android they're loaded with
    `QSEOS_APP_START` (dmesg: `qseecom_load_app: App with id 38 (authhat)`)
    and called with the legacy send command, not object invocation.
  - The CBO listener registration (`IRegisterListenerCBO`, UID 87) is probably
    newer than SM8250's QTEE. **Unverified.**
- **Conclusion:** `qcomtee` is optional to experiment with, not the
  recommended path.
  - https://github.com/torvalds/linux/tree/master/drivers/tee/qcomtee
  - https://github.com/torvalds/linux/blob/master/Documentation/tee/qtee.rst

## 2. The downstream qseecom path

### What the downstream driver is
- **Source:** Samsung's SM8250 kernel, mirrored by LineageOS at
  https://github.com/LineageOS/android_kernel_samsung_sm8250 (branch
  `lineage-24.0`).
- **Size:** `drivers/misc/qseecom.c` is **10,110 lines** (plus
  `compat_qseecom.c`); `smcinvoke.c` is 2,041 lines. About 215 lines of
  qseecom.c touch ION/dma-buf, and it depends on `msm_ion`, `msm-bus`,
  `soc/qcom/scm.h`, ICE/PFK and `qtee_shmbridge`.
- **No Samsung changes:** there are no Samsung-specific hunks in qseecom.c.
- **Not worth porting whole:** about 70 % of it is FDE/ICE keys, CE clocks,
  bus scaling, compat and the old (non-SMC) call convention, none of which we
  need.

### Kona settings that make this smaller
From the qseecom node in `kona.dtsi`:
- `qcom,appsbl-qseecom-support`: the bootloader already notified TZ about
  the app region, so no `APP_REGION_NOTIFICATION` is needed.
- `qcom,commonlib64-loaded-by-uefi`: no cmnlib load is needed.
- `qcom,no-clock-support`: no crypto-engine clock votes are needed.
- `qcom,qsee-reentrancy-support = <2>`: listener waits use
  `CONTINUE_BLOCKED_REQUEST` and the smcinvoke variants of the listener
  calls. `qseecom.c` tries `TZ_OS_REGISTER_LISTENER_SMCINVOKE_ID` first and
  falls back on `-EOPNOTSUPP`.
- `secapp-region` 0x82400000 (+0x3a00000) and the smcinvoke region at
  0x87900000 both sit inside the TZ reservation this repo already makes
  (`0x80b00000 + 0x7900000`, patch 0010).

### SCM calls a minimal driver needs
Owner `QSEE_OS` unless noted:
- `APP_MGR`: `APP_START` (0x01: `.mdt` and `.bNN` concatenated in one
  contiguous buffer), `APP_SHUTDOWN` (0x02), `APP_LOOKUP` (0x03).
- `TZ_APPS` owner: `QSAPP_SEND_DATA` (0x01) and, if used,
  `SEND_DATA_WITH_WHITELIST` (0x06).
- `LISTENER`: `REGISTER` (0x01 / smcinvoke 0x06), `DEREGISTER`,
  `RESPONSE_HANDLER` (0x03 / whitelist variant), `CONTINUE_BLOCKED_REQUEST`
  (smcinvoke 0x07).
- `INFO`: version.
- `WAITQ`: resume/sleep handling.

### Someone has already written this for mainline
- **Project:** [nyl/linux-qseecom-mobile](https://github.com/nyl/linux-qseecom-mobile),
  created 2026-11-04, GPL-2.0, targeting the Fairphone 4 (SM7225).
- **Driver:** `qcom_qseecom_mobile.c`, about 1,450 lines.
  - Split-ELF TA loading into `qcom_tzmem` memory.
  - Multiple resident TAs.
  - `APP_SEND`, WAITQ resume, and re-entrant listener upcalls with a 7-ioctl
    UAPI (`/dev/qseecom_mobile`).
- **Daemon:** `tz-qsee-fingerprintd`, an fprintd-compatible D-Bus service
  with FS (10) and GPFS (28672) listener workers and a plugin per sensor/TA.
- **Caveats:**
  - It finds `__scm_smc_call`/`qcom_scm_lock` through a kprobe-on-kallsyms
    hack. That isn't upstreamable, and we should replace it with a small
    exported helper in our own `qcom_scm` patch.
  - Its claims are untested by us, and the repo is days old.
- **Other searches:** `wrobelda/linux-placeholder` (Xiaomi Pad 5 Pro, SM8250,
  "QSEECOM TEE driver") is an empty placeholder. I found no postmarketOS,
  Linaro or Droidian mainline qseecom port. Halium/Droidian ports keep the
  downstream kernel.
- **Estimate:** reusing nyl's driver structure, about **1–2 weeks** to a
  clean SM8250 driver. A port of downstream qseecom.c would be about 4–6 weeks
  for no benefit.

## 3. Listener services
- **What `qseecomd` provides on Android:** listener daemons for the legacy
  secure FS (0xa), GP FS (0x7000), RPMB (0x2000), time (0xb), and on some
  builds SSD/GPT/MDTP.
- **What the fingerprint stack writes itself:** the Samsung HAL
  (`libbauthserver.so`) does its own file I/O for the blobs the TA
  encrypts. From its strings:
  - `/data/vendor/biometrics/fp/User_%d`, `/%s_%dtmpl.dat`, `calib.dat`,
    `/data/vendor/biometrics/ta/{bta,dta}`;
  - `/efs/biometrics` with `gdxrtcalib.dat` and `egoptbds.dat` (optical
    calibration).
  
  So templates probably don't depend on the secure-FS listener.
- **Still likely needed:** the TA may use RPMB (anti-rollback) or time.
  **Unverified.** The cheap way to find out is to have our driver log every
  `BLOCKED_ON_LISTENER`/`INCOMPLETE` listener ID it sees.
- **Open implementations to borrow from:**
  - minkipc `listeners/librpmbservice` (UFS RPMB through SCSI SECURITY
    PROTOCOL IN/OUT), `libfsservice/{fs,gpfs}`, `libtimeservice`;
  - nyl's `listener_worker.c`;
  - or the tablet's own `qseecomd` in the bionic root, on top of our
    `libQSEEComAPI` replacement (see below).

## 4. Samsung-specific questions, and how LineageOS does it

LineageOS has **working fingerprint on gts7xl/gts7xlwifi**, confirmed by the
device owner. It's an official Lineage device:
[android_device_samsung_gts7xlwifi](https://github.com/LineageOS/android_device_samsung_gts7xlwifi),
built on
[android_device_samsung_sm8250-common](https://github.com/LineageOS/android_device_samsung_sm8250-common).

How LineageOS does it:
- **HAL:** LineageOS builds its **own AIDL HAL**,
  `android.hardware.biometrics.fingerprint-service.samsung`
  ([hardware/samsung `aidl/fingerprint`](https://github.com/LineageOS/android_hardware_samsung/tree/lineage-24.0/aidl/fingerprint)).
  Samsung's `vendor.samsung.hardware.biometrics.fingerprint@3.0-service` is
  not used.
  - `LegacyHAL.cpp` simply `dlopen("libbauthserver.so")` and resolves
    `ss_fingerprint_open/enroll/pre_enroll/post_enroll/authenticate/cancel/enumerate/remove/set_active_group/get_auth_id/request`
    plus `ss_set_notify_callback`.
  - That is a plain C API, ideal for a small bionic client like
    `gnss/hal.c`.
- **Blobs:** from `gts7xlwifi/proprietary-files.txt`: `libbauthserver.so`,
  `libbauthtzcommon.so`, `libgf_in_system_lib.so`, and the sensor-test libs
  that `libbauthserver` links against (`libegis_fp_normal_sensor_test.so`,
  `libqfp_sensortest.so`, `libsynaFpSensorTestNwd.so`). sm8250-common adds
  `libQSEEComAPI.so`, `qseecomd` and the qseecom HIDL service. The TAs stay
  in the firmware partition (`/vendor/firmware_mnt/image`, names `fingerpr`,
  `securefp`, `authhat`; `skeymast`/`keymaster` also appear).
- **Kernel:** Samsung's kernel, unchanged: stock `qseecom.c` and the stock
  `gw9558x` driver. There are no qseecom shims.
- **Sensor driver ([`gw9558x_platform.c`](https://github.com/LineageOS/android_kernel_samsung_sm8250/blob/lineage-24.0/drivers/fingerprint/gw9558x_platform.c)):**
  `gw9558_spi_clk_enable()` only takes a wakelock. TZ owns the SPI clocks.
  The device tree
  ([`fingerprint-sensor_gts7xl_r00.dtsi`](https://github.com/LineageOS/android_kernel_samsung_sm8250/blob/lineage-24.0/arch/arm64/boot/dts/samsung/drivers/fingerprint-sensor_gts7xl_r00.dtsi))
  gives **power enable = gpio117, reset = gpio127**, both pulled down.
- **Properties and init:**
  - `vendor.prop`: `ro.vendor.fingerprint.type=udfps_optical`,
    `sensor_location=876|2614|77` (panel pixels, radius 77),
    `request_touch_event=true`. On finger down, LineageOS sends
    `SEM_REQUEST_TOUCH_EVENT 2`; on finger up, `SEM_REQUEST_TOUCH_EVENT 1`.
  - `init.fingerprint.rc` writes `fod_enable,1,1,0` to `/sys/class/sec/tsp/cmd`
    and creates `/data/vendor/biometrics` and `/efs/biometrics`.
  - `init.samsung.display.rc` exposes `/sys/class/lcd/panel/mask_brightness`.
  - BoardConfig sets `TARGET_USES_FOD_ZPOS` and extra gralloc usage bits, so
    Samsung's display driver puts the panel into the bright "mask" mode
    while the UDFPS layer is shown.
- **Libraries `libbauthserver` reads besides the TA:**
  `/sys/class/lcd/panel/{lcd_type,window_type}`, an `optHbmInterrupt` path,
  `/sys/class/fingerprint/fingerprint/*`, battery temperature, and
  `ro.*` properties. It links `libbinder`; whether it needs a live
  servicemanager is **unverified**.

What this tells us about Samsung-specific blockers:
- **Knox and warranty state:** fingerprint works on LineageOS with an
  unlocked bootloader and Knox tripped. That rules out Knox/warranty
  state, TIMA and secure boot state as blockers for unlock-level use.
  Samsung Pass and similar Knox-bound features do not work there; we don't
  need them.
- **Caller identity:** I found nothing suggesting the TA checks the caller.
  Legacy QSEE apps get commands from the kernel driver and have no notion of
  a caller.
- **Trusted UI:** there's no trusted-UI or secure-display path. The display
  only has to be bright under the finger.
- **The hardware auth token (HAT) is the real open question.**
  - Android enrollment passes a HAT from Gatekeeper, an HMAC over the user's
    verified PIN.
  - `libbauthtzcommon` starts an `authhat` TA and an SKM/QKM keymaster TA.
    This suggests enrollment verifies the HAT in TZ.
  - On Linux the legitimate route is to run the tablet's own gatekeeper HAL
    and TA as well, so the user's PIN produces a real HAT.
  - Whether enrollment works without one is **unverified**. Test it early.
  - Out of scope here: anything that forges or bypasses tokens or keys.

## 5. Cheaper alternatives
- **libhybris/Halium:** the vendor HAL would still need a `/dev/qseecom`
  with the downstream ABI and `/dev/ion`. That's the same kernel work or more
  than our route, plus a full Android container. No saving.
- **Replacing `libQSEEComAPI.so` (recommended):** `libbauthtzcommon.so`
  imports exactly `QSEECom_start_app`, `QSEECom_start_app_V2`,
  `QSEECom_send_cmd`, `QSEECom_shutdown_app` and `QSEECom_set_bandwidth`
  (checked with `readelf` on the gts7xlwifi blob). There's no
  `send_modified_cmd` and no direct ION use. A shim of about 300 lines over
  our own UAPI replaces the entire downstream ABI, ION included.
- **USB fallback:** a libfprint-supported USB reader on USB-C works today
  with zero kernel work
  (https://fprint.freedesktop.org/supported-devices.html).

## Recommended path

The steps are ordered so the riskiest unknown is tested first. Each test is
cheap and can be undone.

| # | Step | Test / exit criterion | Effort |
|---|------|------------------------|--------|
| 0 | **Look at what mainline already found.** `dmesg \| grep -iE 'qseecom\|qcomtee\|tzmem\|shm bridge'`, `ls /dev/tee*`. | qseecom version is printed (confirms the QSEECOM syscall interface); see whether the SHM bridge is enabled and whether `qcomtee` probed (expected: no). | 10 min |
| 1 | **Do app lookups from a debug module.** Call `qcom_scm_qseecom_app_get_id()` for names that may already be resident (`keymaster64`, `skeymast`, `widevine`, `fingerpr`). Or lift the allowlist in a test build. | The SCM call path works with no reset. `-ENOENT` is fine, a hang or reset is not. | 0.5 day |
| 2 | **Riskiest: load `fingerpr` from Linux.** Build nyl's `qcom_qseecom_mobile.ko` (or our trimmed version) and `APP_START` `fingerpr.mdt`+`.b0N` from the mounted firmware partition, then do the same with `securefp` and `authhat`. | TZ returns an app ID. Then `APP_SEND` a harmless command, e.g. the version query bauth sends at open (capture it from Android with qseecom debug logs or the bauth log tags). Log every listener ID TZ asks for. | 2–4 days |
| 3 | **Sensor power.** Use the gpio117/gpio127 sequence from `gw9558x_common.c` (or a 100-line `/dev/goodix_fp` stub that accepts the five ioctls; SPI_CLK = wakelock/no-op). Keep gpio20-23 reserved. If TA capture times out, check that mainline hasn't gated `GCC_QUPV3_WRAP0_S7_CLK`/wrap0 AHB (try `clk_ignore_unused`). | The TA's sensor init or chip-ID command succeeds. | 1–2 days |
| 4 | **`libQSEEComAPI` shim and bionic client.** Write the 5-function shim over the driver UAPI. Write a `gnss/hal.c`-style client that `dlopen`s `libbauthserver.so` the way Lineage's `LegacyHAL.cpp` does. Mount `/efs` (calibration) and a writable `/data/vendor/biometrics`. Provide the sysfs/properties it reads, stubbed where harmless. | `ss_fingerprint_open()` returns 0, and `GET_SENSOR_INFO`/`GET_VERSION` requests answer. | 1 week |
| 5 | **Listeners.** Serve whatever IDs step 2 logged. Reuse minkipc/nyl workers, or the tablet's own `qseecomd` over the shim. | No `BLOCKED_ON_LISTENER` stalls during open, enroll and verify. | 2–5 days |
| 6 | **Illumination.** Add a mask/HBM control to the S6TUUM3 panel driver: take the DCS sequence from Samsung's `ss_dsi_panel` for this panel and expose it as sysfs. Draw a full-white circle at (876, 2614) r = 77 while capturing. Send `SEM_REQUEST_TOUCH_EVENT` from touch-down in that circle. | Capture returns good-quality images (acquired-info codes, not "too dark"/"partial"). | 1–2 weeks |
| 7 | **Auth token.** Call `ss_fingerprint_pre_enroll` and `ss_fingerprint_enroll`. If enrollment rejects the HAT, bring up the tablet's gatekeeper HAL/TA the same way, so the user's PIN produces a genuine token. | Enrollment completes and verification matches. | 0.5 day to find out; 1–3 weeks if gatekeeper is needed |
| 8 | **Desktop integration.** Bridge to fprintd: a libfprint driver talking to our client over a socket, or a `net.reactivated.Fprint` service like nyl's `tz-qsee-fingerprintd`. Then PAM and the lock screen. | `fprintd-enroll`/`fprintd-verify` and `pam_fprintd` work. | 1 week |

**Total:** about 5–9 weeks part-time if steps 2 and 7 go well. Stop after
step 2 if `APP_START` cannot be made to work. Then the only route left is
the USB reader.

## Hard blockers and how to test each cheaply

1. **TA load is refused or the SoC resets** (step 2).
   - Likely causes: SHM bridge or buffer placement, a missing
     region/commonlib notify, or reentrancy handshakes.
   - Mitigations: try with SHM bridge on and off (`qcom_tzmem` default vs a
     test build). Compare against the downstream `qseecom_load_app()`
     argument packing. Check the bootloader still did the app-region notify
     (appsbl flag).
2. **Enrollment requires a genuine HAT** (step 7).
   - Test with the HAL's own error codes before investing in gatekeeper.
3. **The optical capture needs panel modes we don't have** (step 6).
   - Test by checking the image-quality acquired codes with a white circle at
     maximum normal brightness first, then add HBM/mask.
4. **The HAL environment** (step 4): `libbinder` and property expectations.
   - Test by running the client under `strace` in the bionic root and
     stubbing whatever fails.
5. **SE7 clocks gated by mainline** (step 3).
   - Test with `clk_ignore_unused`. If that fixes it, claim the clocks in a
     stub node.

## What I could not verify
- Whether upstream `qcomtee` probes on this TZ (expected not; test 0).
- Whether SM8250 QTEE supports `IAppLoader` or CBO listeners.
- Which listeners `fingerpr` uses.
- Whether enrollment enforces a HAT.
- Whether `libbauthserver` needs a running servicemanager.
- Whether nyl's driver works as claimed on any device.

None of these was tested on hardware. The rest is read from source: mainline
master, Samsung/LineageOS kona kernel `lineage-24.0`, LineageOS device trees,
and `readelf`/`strings` of the gts7xlwifi vendor blobs.

## Links
- Mainline: [qcom_scm.c](https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_scm.c), [qcom_qseecom.c](https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_qseecom.c), [qcom_qseecom_uefisecapp.c](https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_qseecom_uefisecapp.c), [qcom_tzmem.c](https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_tzmem.c), [drivers/tee/qcomtee](https://github.com/torvalds/linux/tree/master/drivers/tee/qcomtee), [qtee.rst](https://github.com/torvalds/linux/blob/master/Documentation/tee/qtee.rst)
- QTEE series review (legacy invoke remark): https://lore.kernel.org/all/?q=QCOM_SCM_SMCINVOKE_INVOKE_LEGACY
- Userspace: [quic-teec](https://github.com/quic/quic-teec), [minkipc / qtee_supplicant](https://github.com/qualcomm/minkipc)
- Mainline QSEE transport + fprintd daemon: https://github.com/nyl/linux-qseecom-mobile
- Samsung SM8250 kernel (LineageOS mirror): [qseecom.c](https://github.com/LineageOS/android_kernel_samsung_sm8250/blob/lineage-24.0/drivers/misc/qseecom.c), [smcinvoke.c](https://github.com/LineageOS/android_kernel_samsung_sm8250/blob/lineage-24.0/drivers/soc/qcom/smcinvoke.c), [fingerprint drivers](https://github.com/LineageOS/android_kernel_samsung_sm8250/tree/lineage-24.0/drivers/fingerprint); Samsung's own source: https://opensource.samsung.com (SM-T970)
- LineageOS: [sm8250-common](https://github.com/LineageOS/android_device_samsung_sm8250-common), [gts7xlwifi](https://github.com/LineageOS/android_device_samsung_gts7xlwifi), [hardware/samsung aidl/fingerprint](https://github.com/LineageOS/android_hardware_samsung/tree/lineage-24.0/aidl/fingerprint)
- Fallback: [libfprint supported devices](https://fprint.freedesktop.org/supported-devices.html), [libhybris](https://github.com/libhybris/libhybris)
