# Fingerprint reader

Status: not working on Linux, and parked. This page records what the working
Android stack does, so the port can start from facts instead of guesses.

## Hardware

- **Sensor:** Goodix GW9558, an optical sensor under the display.
- **Bus:** SPI on QUP SE7 (gpio20-23). The bus belongs to TrustZone.
  - The application processor only switches the sensor's clock, power and reset.
  - Touching those four pins from Linux resets the SoC. That's why the board
    device tree reserves them (`gpio-reserved-ranges = <20 4>`).
- **No sensor data on the Linux side:** images, matching and templates all stay
  inside TrustZone. Samsung's trusted application (TA) is the only code that
  talks to the sensor.

## What Android does

These notes come from an enrollment under Android 15 (a One UI based ROM)
with root, on 2026-11-07. Enrollment progressed normally. The logcat and dmesg
captures are kept locally; they are not in this repository.

### Kernel interfaces

- **`/dev/goodix_fp`:** Samsung's `fps_gw9558` driver. The ioctls seen during
  enrollment:

  | ioctl | Use |
  |---|---|
  | `GF_IOC_ENABLE_SPI_CLK` | votes the SPI clock (25 MHz) for TrustZone, in cycles of about 0.5 s during a capture |
  | `GF_IOC_DISABLE_SPI_CLK` | releases the vote |
  | `GF_IOC_ENABLE_POWER` | sensor power on |
  | `GF_IOC_DISABLE_POWER` | sensor power off |
  | `GF_IOC_RESET` | resets the sensor |

  The driver's debug line reads `ldo: 1, sleep: 1, tz: 1 type: GW9558`.
- **`/dev/qseecom`:** Qualcomm's downstream interface for loading and calling
  TrustZone apps.
  - The TAs `fingerpr` and `securefp` come from `/vendor/firmware_mnt/image`
    (the modem firmware partition).
  - `authhat` loads briefly during enrollment
    (`qseecom_load_app: App with id 38 (authhat)`).
- **`/dev/ion`:** the shared buffers between Android and the TA.

### Userspace

- **Service:** `vendor.samsung.hardware.biometrics.fingerprint@3.0-service`,
  which loads the legacy HAL `fingerprint.default.so`.
- **Libraries:**
  - `libQSEEComAPI.so`: QSEECOM client
  - `libbauthserver.so` and `libbauthtzcommon.so`: Samsung's `BAuth_*` API
  - `libgf_in_system_lib.so`: Goodix
- **Also running:** `qseecomd` (storage listeners, so the TA can save
  templates) and `vendor.qti.hardware.qseecom@1.0-service`.
- **Log tags:**
  - `bauth_service`
  - `bauth_FPBAuthService` (fpop / opcode)
  - `bauth_TLC_Communicator` (FP commands 0x2-0x4)
  - `bauth_FPGDXOPTBAuthSensorControl` (Goodix optical sensor control)

## What Linux would need

1. **Kernel: a `/dev/qseecom` with the downstream ABI.** It has to load an app
   from its `.mdt`/`.bXX` files, send commands and serve listeners, using
   dma-buf instead of ION. Mainline's `qcom_scm`/`qcomtee` do not provide this
   ABI. This port is most of the work.
2. **Kernel: a small `goodix_fp` character device** with the five ioctls above:
   clock vote, power and reset. The SPI bus stays TrustZone's.
3. **Userspace:**
   - `qseecomd` for the storage listeners.
   - The vendor HAL, driven outside Android the same way the GPS HAL is (see
     `gnss/hal.c`: a small bionic client in a root built from the tablet's
     own partitions).
   - A bridge from that client to fprintd, so PAM and the lock screen can use
     it.

Vendor code is acceptable here, because no native path exists: the sensor is
only reachable through Samsung's TA.

**Estimate:** weeks, mostly the qseecom port. The GPS work made the userspace
half easier: the bionic root, the stub-linked helpers and the
`libhardware_legacy` stand-in can all be reused.
