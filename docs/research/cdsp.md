# Compute DSP (CDSP)

Verdict: **easy to boot, little user payoff.** Do after codec. Main value: FastRPC for ML/DSP offload (Hexagon, HVX/HTP); practically only for custom Hexagon code or Qualcomm's AI libs, none usable on mainline desktop software.

## Facts
- Mainline: `cdsp: remoteproc@8300000` `qcom,sm8250-cdsp-pas`, power-domain CX, memory-region `cdsp_mem`, QMP via aoss, glink-edge with `fastrpc` (label "cdsp", compute-cb 1-8), status disabled. [sm8250.dtsi:3795-3890](https://github.com/torvalds/linux/blob/v7.3/arch/arm64/boot/dts/qcom/sm8250.dtsi#L3795). Boards: `&cdsp { firmware-name = "qcom/sm8250/cdsp.mbn"; status = "okay"; }` (MTP:478, RB5:553, elish).
- Repo: `cdsp_mem` already set from Samsung's map: 0x8e300000 size 0x1a00000 (0010); `cdsp_secure_heap` set too. Samsung DT keeps `pil_cdsp_mem`/`cdsp_secure_heap` [overlay-r06.dts:13859,13865](https://github.com/shezik/samsung-kernel-T970XXS7DXH1/blob/dev/arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts#L13859). `CONFIG_QCOM_FASTRPC=m` already (defconfig:304), proven with SLPI.
- Firmware: `cdsp.mbn` from tablet `apnhlos` (`NON-HLOS`: `cdsp.mdt`/`.b*`) or `vendor` (dsp), plus DSP-side libs for FastRPC in `/vendor/dsp/cdsp/`, `/vendor/lib/rfsa/adsp` (skel .so). Same extraction path as the SLPI (`gts7xl-sensord-extract`); keep the repo's "never commit firmware" rule.

## Realistic uses on Linux
1. Boot + idle CDSP (power state matters: suspend.md).
2. FastRPC `/dev/fastrpc-cdsp`: run Hexagon "skel" libs via `fastrpc` libs (`libcdsprpc`, from Linaro `fastrpc` project, open source) – e.g. Qualcomm `hexagon-sdk` examples (needs Hexagon SDK to build skel; skel must be signed: *test-signed skel libs only load if device is unfused/signature check off; production devices need signed PD or **unsigned PD** mode — `remote_session_control` unsigned PD allowed on newer DSPs; guess*).
3. HTP/NPU (Hexagon Tensor Processor on 865: Hexagon 698, HVX + HTA/HMX): needs QNN/SNPE runtime = Qualcomm blobs (proprietary, aarch64 Linux builds exist for QCS-Linux, SM8250/RB5 supported). Possible with rb5 `qnn` packages (LE), license restrictions apply; effort high, desktop value low.
4. Audio/camera algorithms: none.

## Staged plan
1. Extract `cdsp.*` (read-only) to `/lib/firmware/qcom/sm8250/gts7xl/cdsp.mbn`. Test: files present, `file` ELF.
2. DT: `&cdsp { firmware-name = "qcom/sm8250/gts7xl/cdsp.mbn"; status = "okay"; };` (mirror patch 0043 style). Boot, `cat /sys/class/remoteproc/remoteproc*/{name,state}` shows cdsp `running`; `dmesg` no watchdog. Test x3 reboots + `echo stop/start`.
3. `ls /dev/fastrpc-cdsp`, `/dev/fastrpc-cdsp-secure`. Run Linaro `fastrpc` `hello`/`calculator` example only if skel signs fine; otherwise stop here.
4. Power check: `qcom_stats` `cdsp` low-power count; CDSP should power-collapse when unused (feeds suspend.md). If not, stop it when idle (`systemd` on-demand).
5. Optional: hexagonrpcd (for sensor registry – not needed here), QNN.

## Risks
- Very low: remoteproc firmware only. Tablet's TZ rejects bad firmware → no boot, no harm.
- Wrong `cdsp_mem` size breaks PAS auth (already stock-matched).
- Idle power: running DSP w/o clients might cost mW; leave disabled by default until needed.
