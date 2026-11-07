# USB-C host and DisplayPort Alt Mode: research and plan

Nothing here has run on a tablet. Facts come from source. Unverified claims say so.

Sources, cited below as:
- `S:` Samsung T970 GPL kernel, shezik/samsung-kernel-T970XXS7DXH1, branch `dev`, 0389517ca61a. `DT:` is `arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts` in that tree (decompiled, numeric phandles).
- `L:` torvalds/linux v7.3.
- `X:` aaronsb/sm-x800-linux, 6f3248548258: mainline port of the Tab S8+ (SM8450, same MAX77705). Drivers are GPL-2.0, docs MIT.

## Key findings

1. **Prior art exists.** X has a full mainline MAX77705 Type-C/PD driver (`pmaports-overlay/device/testing/linux-postmarketos-qcom-sm8450/max77705-typec*.patch`, ADR in `docs/architecture/003-*.md`). Its README reports host, gadget and either orientation working on hardware. It has no DP altmode or redriver. Use it as the base for the full driver; the skeleton here (0053) exists to answer board questions with less risk.
2. **There is a redriver: PS5169 at i2c14 0x28** (`DT:13241`, `secdp,redrv = "ps5169"` `DT:10624`). All SuperSpeed and DP lanes appear to pass through it (`S:drivers/redriver/ps5169.c`, called for USB in `S:drivers/usb/dwc3/gadget.c:2554-2559`, for DP in `S:techpack/display/msm/dp/dp_power.c:671-690`). It needs programming. Mainline has no driver.
3. **i2c14 uses gpio40/41, which this board's DT reserves** (`gpio-reserved-ranges = <20 4>, <40 2>`). Stock uses those pins for i2c14 with no reservation (`S:arch/arm64/boot/dts/vendor/qcom/kona-pinctrl.dtsi:4227-4238`, no `gpio-reserved` in `kona.dtsi` or `DT:`). The reservation looks inherited, like the `<40 4>` that was already cut to fit gpio43. It must go before USB3 or DP can work. Accessing a truly secured pin would reset the tablet, so test it on its own.
4. **USB 2.0 does not need the redriver.** D+/D- go through the MAX77705 MUIC switch. Host over USB 2.0 is therefore the cheap first win.
5. **A forced-host DTB alone will not power anything.** The tablet gives no VBUS until stage (b). A self-powered hub also needs upstream VBUS before it connects. See stage (a).
6. **The CCIC will not complete a source attach until opcode 0x55 data 0x03 is sent**, and the D+/D- switch can be left open (found on hardware in X, `max77705-typec-source-attach.patch`). Stock does the same (`S:drivers/ccic/max77705_alternate.c:1107-1118`, `:1234`). A strictly read-only driver would never see a USB device attach, so 0053 sends those two configuration opcodes.
7. **A kernel driver at 0x25 shares the mailbox with `gts7xl-charge-control` and `usbroute`**, which use i2c-dev with `I2C_SLAVE_FORCE`. Nothing serialises them. 0053 keeps its commands rare and checks the echo. The real fix is moving the 9 V logic into the kernel, X's model, in a later stage.

## Safety rules

Code must obey these. 0053 enforces the first three with an opcode allowlist and a regmap write table.

1. Never send firmware-update traffic: no 0xD0 at 0x25 reg 0x21, no opcode 0xD1 RAM test, no `BOOT_FLASH` stream, no `fw_update` attribute. Samsung's path is `S:drivers/mfd/max77705.c:456-978`; do not port it. The CCIC reset (`0x80 = 0x0F` at 0x25, X ADR-003) is out too.
2. Never request a PD voltage above what `gts7xl-charge-control` uses: fixed PDO, 9 V, 1.65 A. No PPS or APDO (0x3A, 0x3B, 0x3C).
3. Otherwise read or configure only. New opcodes join an allowlist with their exact payload.
4. Boost off before any reboot or shutdown. On the S8+ a reboot with the boost running left the tablet dark until a charger was attached (X `max77705-charger-otg-shutdown.patch`). 0052 and 0053 both handle this.
5. Boost and charger input never together. The boost keeps the buck on: OR in the OTG bits (0x5 to 0xF), never mode 0xA, which drops the buck (X ADR-003; stock's mode 0xA path is `S:drivers/battery_v2/max77705_charger.c:1590`). Stock limit is 900 mA (`:906-909`), which mainline's charger already sets at probe.
6. Repo rules still apply: only `boot`, `dtbo`, `userdata`; `dr_mode` experiments in separate DTBs, because the gadget is the debug path.

## MAX77705 CCIC protocol

I2C on bus 0 (`S:drivers/mfd/max77705.c:44-48`): top 0x66, **CCIC/MUIC 0x25**, charger 0x69, gauge 0x36, debug 0x62. Interrupt: one level-low line, pm8150l gpio11 (`DT:11112-11115`, already in the board DT).

Registers at 0x25 (`S:include/linux/ccic/max77705.h:61-91`):

| Reg | Name | Notes |
|---|---|---|
| 00, 01, 09 | HW_REV, FW_REV, FW_MINOR | minor low 3 bits, product id above (`:616-619`) |
| 02-05 | UIC_INT, CC_INT, PD_INT, VDM_INT | read to clear; stock bulk-reads 4 (`S:drivers/mfd/max77705-irq.c:331`) |
| 06, 07, 08 | USBC_STATUS1, USBC_STATUS2 (SYSMsg), BC_STATUS | |
| 0A | CC_STATUS0 | [7:6] CCPinStat, [5:4] CCIStat, [3] VCONN, [2:0] CCStat (`:154-157`) |
| 0B | CC_STATUS1 | [7:6] SBU short, VCONN OCP/SC, VSAFE0V, AttachSrcErr, [1] water, [0] altmode (`:162-168`) |
| 0C | PD_STATUS0 | last PDMsg (`:426-488`) |
| 0D | PD_STATUS1 | [7] DataRole, 1 = DFP, [4] PSRDY, [3:0] FCT ID (`:178-180`) |
| 0E-11 | UIC, CC, PD, VDM masks | 1 = masked |
| 21, 22.., 41 | opcode, data, end | |
| 51.. | opcode echo, response | |

Values:
- CCStat 0 none, **1 tablet is sink, 2 tablet is source**, 3 audio, 4 debug, 5 error, 6 disabled (`:364-373`, `S:drivers/ccic/max77705_cc.c:749-884`).
- CCPinStat 0 none, **1 CC1, 2 CC2**, 3 audio (`:265-274`, `cc.c:518-536`). Hardware twin: gpio65 input, 0 = CC1, 1 = CC2 (`DT:11171`, `S:drivers/usb/dwc3/dwc3-msm.c:3161-3171`). Worth comparing during stage (c).
- CCIStat 1 500 mA, 2 1.5 A, 3 3 A (`:276-282`).

Mailbox (`S:drivers/ccic/max77705_usbc.c:1991-2070`): bulk write `[opcode, data...]` at 0x21, up to 32 data bytes. If shorter, write 0x00 to 0x41 to start. Wait for UIC_INT bit 7 (APCmdRes), then read `[echo, response...]` from 0x51. One at a time, stock timeout 3 s (`max77705.h:503`).

Opcodes (`S:include/linux/ccic/max77705.h:520-587`), those relevant:

| Op | Use | Status here |
|---|---|---|
| 06 CTRL1_W | MUIC switch, 0x09 = COM_USB (`S:include/linux/muic/max77705-muic.h:326-355`). Same as `tools/usbroute.c` | allowed in 0053 |
| 54 FACTORY_TEST | auto VBUS: 0 off, 2 on (`usbc.c:270-296`) | 0 only |
| 55 SET_ALTERNATEMODE | 1 SrcCap, 2 VDM, 3 both (`max77705_alternate.h:551-553`) | 3 only |
| 30, 32 | current source caps, PDO request | userspace today; not in 0053 |
| 48 SET_VDM_REQ | send a VDM | stage (e) |
| 4B GET_VDM_RESP | read VDM by sub-id, 31 bytes (`alternate.c:802-816`) | stage (e) |
| 37 SWAP_REQUEST | role swap | later |
| 25 READ_SBU, 0B-10 CC ctrl | read or adjust | not needed yet |
| D1, 3A-3C, 59 | RAM test, PPS, ADC test | **forbidden** |

Interrupt map (`S:drivers/mfd/max77705-irq.c:75-105`):
- UIC_INT: 7 APCmdRes, 6 SYSMsg, 5 VBUS, 4 VbADC, 3 DCD, 1 ChgTyp, 0 UIDADC.
- CC_INT: 7 VCONN OCP, 6 VSAFE0V, 5 AttachSrcErr, 4 VCONN short or water, **3 CCPinStat, 2 CCIStat, 1 VCONN state, 0 CCStat**.
- PD_INT: 7 PDMsg, 6 PSRDY, **5 DataRole**, **4 VDM Attention, 3 VDM DP Configure, 2 VDM DP Status**, 1 SSAcc, 0 FCT.
- VDM_INT: 0 Discover ID, 1 SVIDs, 2 Modes, 3 Enter.
- Top level INTSRC 0x66 reg 0x22, mask 0x23: bit 0 CHG, 1 TOP, 2 FG, 3 USBC (`S:include/linux/mfd/max77705-private.h:24-27`). Mainline's MFD already exposes these as a regmap-irq domain (`L:drivers/mfd/max77705.c`), so the child uses `interrupts = <3>`.

Mask values: stock probe PD 0x1C, VDM 0xFF (`usbc.c:3926-3927`); when altmode starts VDM 0xF0, PD 0x00 (`alternate.c:1278-1279`). UIC init 0x04, CC init 0x20 (`max77705.h:96-99`). After SYSMsg BOOT_WDT 0x03 or BOOT_POR 0x05 the CCIC has reset: re-write masks and opcodes (`usbc.c:2858-2861`, `:2889-2892`).

Stock quirk: the VBUSDet interrupt with CCStat 0 and VBUS low is treated as a missed CCStat detach (`max77705-irq.c:342-353`). Keep it in mind if detach gets lost. 0053 re-reads levels on each interrupt, but only unmasks CC and DataRole, so check this during stage (c).

### DP altmode flow (`S:drivers/ccic/max77705_alternate.c`)

The AP drives Enter and Configure. Firmware runs discovery once 0x55 = 3.

1. VDM_INT Discover ID: read 4B sub 0x01. VID at offset 8, product VDO at 16 (`:731-760`).
2. Discover SVIDs: 4B sub 0x02. Look for 0xFF01 (`:411-472`).
3. Discover Modes: 4B sub 0x03. Mode VDO: Port_Capability [1:0], DFP_D pins, UFP_D pins (`:473-510`, `max77705_alternate.h:284-297`). Then send **Enter Mode**: op 48 (`:346-360`), VDM header SVID 0xFF01, command 4, object position 1 (`:63-80`).
4. Enter ACK: 4B sub 0x04 (`:534`).
5. Firmware sends DP Status. PD_INT bit 2, 4B sub 0x10: Port_Connected [1:0], Multi_Function_Preference [4], **HPD_State [7], HPD_Interrupt [8]** (`:598-654`, `max77705_alternate.h:300-316`).
6. Pick a pin (`:552-596`). Multi-function preferred: D, B, F, C, E, A. Otherwise C, E, A, D, B, F. Send **Configure**: op 48, command 17, position 1, config VDO with SEL_Configuration [1:0] `num_Cfg_UFP_U_as_UFP_D`, Select_DP_V1p3 = 1, pin bits at [13:8] (`:81-97`, `:362-377`, `max77705_alternate.h:318-345`). Pin bits: A 0x01, B 0x02, C 0x04, D 0x08, E 0x10, F 0x20 (`:439-444`).
7. Configure ACK: PD_INT bit 3, 4B sub 0x11 (`:712-729`). Stock now sets the redriver (2 lanes for B, D, F; else 4, `S:techpack/display/msm/dp/dp_display.c:2937-2945`).
8. **HPD after that: PD_INT bit 4 Attention**, 4B sub 0x06, status VDO with the same HPD_State and HPD_Interrupt bits (`:656-710`). Attention can also carry the first pin selection.

Response buffer offsets differ per message. Copy them from the cited lines; don't guess.

## Board wiring from the Samsung DT

| Item | Value | Source |
|---|---|---|
| CCIC | i2c0 0x25 | `S:drivers/mfd/max77705.c:45` |
| PS5169 redriver | i2c14 0x28 (gpio40/41) | `DT:13232-13246`, `DT:13929` fixup |
| Redriver enable | gpio8 listed, but `combo,enable_control` is absent, so stock never drives it. Leave it alone: this board's DTS has gpio8 as `dmic2_clk` | `DT:13245`, `ps5169.c:179-199` |
| Redriver flip input, CC direction | gpio65, input | `DT:13244`, `DT:11171` |
| DP AUX enable | gpio10; driven 0 when active, high when suspended | `DT:10577-10606`, `DT:10621`, `dp_power.c:794-796` |
| DP AUX select | gpio47; 0 for CC1, 1 for CC2 | `DT:10546-10575`, `DT:10622`, `dp_power.c:745-752` |
| DP supplies | vdda-1p2 L9A, vdda-0p9 L18A, aux-pullup L2A | `DT:10614-10616` plus fixups |
| dwc3 | `maximum-speed = "super-speed"` | `DT:11181` |
| QMP tuning | `qcom,qmp-phy-init-seq` override for USB | `DT:11199` |
| DP swing and pre-emphasis | `secdp,*` tables (board-tuned) | `DT:10627-10640` |

PS5169 sequences (`S:drivers/redriver/ps5169.c:66-151`), after probe's WORK_MODE init:
- Chip ID regs 0xAD, 0xAC; rev 0xAF, 0xAE.
- Bytes in `USB_ONLY`, `DP2_LANE_USB`, `DP_ONLY`, `CLEAR`: reg 0x40 = 0xC0/0xD0, 0xE0/0xF0, 0xA0/0xB0, 0x80 (first value CC1, second CC2). Then 0xA0 and 0xA1 as listed in the source.
- Tuning differs for this tablet: `CONFIG_SEC_GTS7XL_PROJECT` selects 0x52 = 0x30 (`:93-95`). Stock defconfig has `CONFIG_COMBO_REDRIVER_PS5169=y` and `CONFIG_REDRIVER=y`.

Other chips: PS5169's AUX does not switch itself, so gpio47 does (`dp_power.c:661-669`). No FSA4480 or similar was found.

## What mainline has (v7.3)

- **No MAX77705 Type-C driver.** Checked the `L:drivers/usb/typec` tree and the `max77705_devs` MFD cells on v7.3 and linux-next (charger, RGB, haptic only). `tcpci_maxim` is MAX33359/TCPCI only: a different architecture, unusable here, because this CCIC runs PD itself behind a mailbox.
- **No charger OTG regulator**: `L:drivers/power/supply/max77705_charger.c` only sets OTG_ILIM 900 mA. 0052 adds it.
- **No PS5169.** `ps883x` is a different Parade part.
- **QMP combo** `qcom,sm8250-qmp-usb3-dp-phy` registers `typec_switch` and `typec_mux` if `orientation-switch` or `mode-switch` is present (`L:drivers/phy/qualcomm/phy-qcom-qmp-combo.c:4901-4924`, `:5252-5254`) and a DRM aux bridge (`:5286`). `sm8250.dtsi` has `orientation-switch` only; add `mode-switch` on the board. Mux states C/E give DP only, D/F give USB3+DP. Static orientation and mode can come from `data-lanes` on port@0 (`:5121-5149`), useful for a forced test.
- **msm DP**: `mdss_dp`, QMP-parented clocks and `dpu_intf0_out` are all in `sm8250.dtsi`. HPD comes only as a bridge notify (`L:drivers/gpu/drm/msm/dp/dp_display.c:1541`). A next bridge is optional (`:1081-1086`).
- **HPD plumbing**: `drm_dp_hpd_bridge_register` + `drm_aux_hpd_bridge_notify` (`L:drivers/gpu/drm/bridge/aux-hpd-bridge.c`, `L:include/drm/bridge/aux-bridge.h`). `drm_connector_oob_hotplug_event` reaches `drm_bridge_connector_oob_hotplug_event` by connector fwnode.
- **Firmware-managed altmode model to copy**: `L:drivers/usb/typec/ucsi/ucsi_huawei_gaokun.c:300-322, 444-466`. Same shape as this CCIC: `typec_mux_set()` on QMP with `state.mode` and `alt.svid`, then `drm_aux_hpd_bridge_notify()`. Prefer it over `typec_altmode` driver binding, because firmware (not Linux) owns the VDM traffic.
- **Reference boards**: `L:arch/arm64/boot/dts/qcom/qrb5165-rb5.dts:675-682, 1403-1433, 1495-1540` (same SoC, full DP chain, with a redriver node between QMP and connector). X's `sm8450-samsung-gts8pwifi.dts:1346-1361, 1505-1540` for the Samsung HS-only connector graph.
- **Config** (`config/gts7xl_defconfig` through olddefconfig): `USB_DWC3_DUAL_ROLE`, `USB_XHCI_HCD`, `USB_ROLE_SWITCH`, `DRM_MSM_DP`, `DRM_AUX_HPD_BRIDGE=m`, `TYPEC=m`, `TYPEC_DP_ALTMODE=m`, `PHY_QCOM_QMP_COMBO=m` are all on. Not set: `TYPEC_MAX77705`, `REGULATOR_USERSPACE_CONSUMER`.

DP chain to reach: `mdss_dp_out` -> `usb_1_qmpphy_dp_in`; `usb_1_qmpphy_out` -> PS5169 mux or connector port@1 -> max77705 `typec@25/connector`; dwc3 HS -> connector port@0. A PS5169 mux driver sits in the graph like the RB5 redriver.

## VBUS

`CHG_CNFG_00` mode nibble at charger 0x69 reg 0xB7 (`L:include/linux/mfd/max77705-private.h`; modes in `S:drivers/battery_v2/include/charger/max77705_charger.h:153-168`). OTG = bits 1 and 3. 0052: read mode, OR 0xA (0x5 -> 0xF, 0x4 -> 0xE), refuse -EBUSY if `CHG_INT_OK` shows CHGIN. Stock masks CHG and CHGIN interrupts during OTG (`S:drivers/battery_v2/max77705_charger.c:917-922`). 0052 does not: if the CHGIN interrupt storms, add it.

With the boost running the CCIC can see its own boost as input (X ADR-003), so `max77705-charger/online` may read 1 and `gts7xl-charge-control` will act on that. Stop the service for stage (b) tests. Stock's firmware auto-VBUS stays off (`usbc.c:3932`), and the host owns the rail.

## Staged plan

Each step should prove one thing. Keep the default `sm8250-samsung-gts7xlwifi.dtb` flashed between tests; swap only the appended DTB (see CLAUDE.md boot image command). Have TWRP ready. No USB gadget in (a) or (b), so work over Wi-Fi SSH.

**(0) Read-only baseline, no code.** Run over SSH with `i2cget -f -y 0 0x25` on 0x00, 0x01, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E-0x11, with nothing attached, a charger, a USB device and each cable flip.
*Pass:* FW revision noted. CCStat and CCPinStat match CC1 vs CC2, with gpio65 in agreement. Masks show who else is writing.
Note: 0x02-0x05 are clear-on-read, leave them alone for now. In TWRP (stock kernel), `i2cdetect -y -r` on the i2c14 bus should show 0x28, which proves the bus number.
Also check TODO's "IRQ 156 nobody cared". *Hypothesis, unproven:* CCIC interrupts unread hold the shared line low. If reading 0x02-0x05 stops the storm once, it is confirmed, and stage (c) cures it.

**(a) Forced host, USB 2.0.** Patch 0051, `sm8250-samsung-gts7xlwifi-usbhost.dtb`. `/sys/kernel/debug/usb/devices` or `lsusb` shows the xHCI root hub; `usbroute` already ran in `initramfs/init:11`.
The hub problem: nothing on this tablet supplies VBUS yet, and both bus-powered and self-powered hubs wait for upstream VBUS. So the test device has to get VBUS from elsewhere:
- A dock with its PD charger attached. The tablet is then power sink but must be host, which needs a DR swap the firmware may or may not perform (read PD_STATUS1 bit 7). Host is forced on our side either way; if the dock insists on being host, no enumeration.
- Otherwise go straight on to (b), the only reliable path, as used in X.
*Pass:* `dmesg` shows a new high-speed USB device, `lsusb` lists it. *Fail tells you:* no root hub: DT or config. Root hub with no connect: VBUS or routing (rerun `usbroute` after plugging in).

**(b) OTG VBUS by hand.** Patch 0052 and `CONFIG_REGULATOR_USERSPACE_CONSUMER=m`. No charger attached; stop `gts7xl-charge-control`. Start with a low-draw device (flash drive or mouse) and `echo enabled > /sys/devices/platform/usb-vbus-test/state`. Replug if needed.
*Pass:* the device enumerates at high speed, in both orientations. Measure: read `CHG_CNFG_00` back as 0xF or 0xE. Disable leaves mode at 0x5. Then a bus-powered hub, then a USB-C dock's USB part. Reboot once with the boost on as a deliberate shutdown-hook test, with a charger at hand: the S8+ needs one to recover if the hook fails.
Limit stays 900 mA, so a dock with HDMI may brown out. Note it, don't raise it yet.

**(c) Minimal driver.** Patch 0053, `sm8250-samsung-gts7xlwifi-typec.dtb`, `CONFIG_TYPEC_MAX77705=m`. Leave the module out of autoload at first and `modprobe` it by hand over SSH, so a bad boot isn't self-inflicted.
- *c1 (no QMP).* Pass: log shows `CCIC hw .. fw ..`, `/sys/class/typec/port0` exists, `orientation` flips with the plug, `data_role` and `power_role` are right for charger, PC and USB device. USB gadget still enumerates on a PC. USB device gets VBUS by itself and enumerates. Mailbox warnings absent. **9 V charging still works with charge-control running**: 0x55 might alter it, compare with today's numbers. IRQ 156 stays quiet through a plug cycle.
- *c2 (QMP orientation switch):* set `GTS7XL_TYPEC_QMP 1`. This defines L18A, new to this board. NOTES records dark panels from defining undefined rails, though those were name-only nodes. Pass: panel fine, then `dmesg` shows the QMP probe, then orientation reaches the switch (`dev_dbg`/ftrace).
- *Afterwards:* replace 0053 with X's driver plus DP additions, move 9 V into the kernel with the allowlist extended to fixed PDOs, retire the charge-control mailbox code. Not before c1 passes.

**(d) Forced DP, no altmode logic.** Prove the video path before PD. New work, not drafted:
1. Remove `<40 2>` from reservations in a test DTB and add `&i2c14` with `status = "okay"`. Pass: chip IDs read at 0x28. A reset here means the reservation was real, so stop.
2. `&mdss_dp` on; `mdss_dp_out` data-lanes `<0 1>` to `usb_1_qmpphy_dp_in`; `mode-switch` added to QMP; supplies per the table; a debugfs knob in the Type-C driver calling `typec_mux_set` (USB3DP) and `drm_aux_hpd_bridge_notify(connected)`. Sysfs `status` force alone will not do: msm DP only leaves its idle state through a bridge HPD notify (`dp_display.c:1541`).
3. By hand from a test tool: PS5169 WORK_MODE, then 2-lane mode for the plug's orientation. gpio10 low and gpio47 per orientation. CCIC VDM by hand through allowlisted ops 48/4B (Enter, Configure pin D), HPD read from 4B sub 0x10.
*Pass:* AUX reads the DPCD and EDID (`/sys/kernel/debug/dri/*/DP-1/edid`, `dmesg` with `drm.debug=0x100`). Then the link trains and an image appears at low resolution. Use a plain USB-C-to-HDMI adapter before a multi-port dock: fewer lanes, less power.

*Fail tree:* no AUX: gpio10/47 or flip wrong, try both orientations. AUX fine and no link: PS5169 mode or lane mapping; try the opposite. Flicker: `secdp,*` swing tables differ from mainline's QMP defaults.

**(e) Proper altmode.** In the full driver:
1. Allowlist ops 48 (fixed payloads for Enter and Configure on SVID 0xFF01) and 4B. Unmask per `alternate.c:1278-1279`.
2. State machine from the flow above, `typec_mux_set` per the gaokun driver, PS5169 as a small `typec_mux` and `typec_switch` driver, gpio47/gpio10 as part of it.
3. HPD: `drm_dp_hpd_bridge_register` for the connector node, `drm_aux_hpd_bridge_notify` on status and Attention. HPD_Interrupt with HPD still high needs a link-status recheck: confirm how `drm_connector_oob_hotplug_event` conveys it versus a plain notify.
4. 4 lanes (pins C/E): `data-lanes = <0 1 2 3>`, when USB3 stays unused. USB3 itself: restore `usb3-phy`, drop `select-utmi-as-pipe-clk` and `maximum-speed`, set `qcom,qmp-phy-init-seq`-equivalent tuning if needed. A separate later step.
5. VBUS to docks stays capped at 900 mA unless tested. With a charger on the dock, tablet is sink: no boost at all.
*Pass:* replug in either orientation lights HDMI with no intervention. Unplug clears the connector. Suspend/resume keeps USB and DP. A dock's USB2 devices work at the same time. 9 V still negotiates through the dock.

## Draft code

| Patch | Contents | Checked | Not checked |
|---|---|---|---|
| 0051 | `-usbhost.dts`, Makefile | dtc via kernel `make qcom/...dtb`, no warnings | never booted; schema (`dtbs_check`) not run |
| 0052 | charger OTG regulator + shutdown (from X) + usbhost additions | compiled arm64 clang 18, W=1, clean | no hardware here (runs on S8+ in X); not linked |
| 0053 | `max77705_typec.c`, Kconfig, Makefile, `-typec.dts` | compiled to `.o`, W=1, clean, against v7.3 headers with this repo's config; DTB with QMP 0 and 1 builds | **never run**; no modpost or link; no sparse or checkpatch; no DT binding, so schema would reject the compatible |

Compile ran in a sparse v7.3 checkout with `COMPAT_VDSO` off (no 32-bit toolchain here); the DTS patches of the series were applied, the others were not, since none touch these files. 0051-0053 apply cleanly in order on that base. `git am` of the full series was not run.

Default builds: `TYPEC_MAX77705` stays unset, CI builds only the base DTB, and the new DTBs are extra. 0052 does register an unused regulator in every build; that part is live code. Drop it from a default build if you'd rather keep this branch fully inert.

## Top uncertainties

1. Are gpio40/41 really usable on this tablet? All USB3 and DP depend on it. Stock says yes; untested under this TZ state with mainline.
2. Is this tablet's CCIC firmware like the S8+'s 5F.00, and does 0x55 = 3 leave 9 V charging intact? Stage (0) and (c1).
3. Kernel and userspace sharing the mailbox: losing a command is possible until charge-control is retired. 0053 narrows the window and detects it, no more.
4. Does a dock with a PD charger swap so the tablet is host, or does the firmware need 0x37? Affects the common dock-with-charger case the most.
5. 900 mA OTG limit against docks with HDMI, which often draw more. Stock also caps at 900 mA (`max77705_charger.c:906-909`), so a brown-out would be expected behaviour.
6. PS5169 lane and polarity mapping through QMP: stock DP only states `is_DFP`/`is_front`. Expect some trial, both orientations.
7. Whether dwc3's HS-only setup (`select-utmi-as-pipe-clk`, no `usb3-phy`) coexists with QMP running DP only. It should: DP doesn't use the USB side, but sm8250 hasn't shown this combination upstream.
8. Bridge chain: mdss_dp next bridge through QMP's aux bridge to the HPD bridge on a `connector` under an I2C node. Same as RB5 in shape, untested here.

## Results on the tablet (2026-11-04)

Patches renumbered to 0051-0053 on top of main. All tests over Wi-Fi SSH, kernel `usbc-dp` branch.

| Stage | Result |
|---|---|
| (0) baseline | CCIC hw 0x1a, fw 52.00. CC control 1 reads 0x1f (DRP, audio, debug on), same as stock init. With an unpowered hub or a charger on a hub: CCStat 0, no VBUS, no CC interrupts at all. Confirms finding 6: no attach without opcode 0x55 = 3. |
| (a) forced host, `-usbhost.dtb` | ✅ xHCI root hubs `usb1`/`usb2` come up. Panel, Wi-Fi fine. |
| (b) OTG VBUS by hand | ✅ Unpowered Genesys hub → second hub → Intenso stick, all 480M; mouse and other devices work ("usb works everywhere"). Battery draw with that tree ~685 mA. `CHG_CNFG_00` boost path works; regulator refuses with CHGIN present. |
| (b) PC or charger fed into the hub while boost on | ⚠ `max77705-charger/online` reads 1 with the boost running (the chip sees its own boost, as X noted). Boost switched off by hand. Hub stays up on the PC's power, but the tablet does not charge: no attach as sink without the driver. |
| (c1) `max77705_typec` (0053), `-typec.dtb` | 🟡 First attach works: charger on the hub gives `data_role` host, `power_role` sink, hub enumerates and the tablet charges. Replug does nothing. |
| (c1) why replug fails | IRQ 156 (the MAX77705 line, PM8150L gpio11, level low) hits 100001 and is disabled ("nobody cared") at ~77 s, with or without the driver; it has happened on every boot. The driver's own IRQ fires 66-140 times a second: something keeps asserting. |
| (c1) edge-triggered parent IRQ | ❌ No storm, but mailbox commands time out (`firmware VBUS off failed: -110`, `source attach enable failed: -110`): the command-done interrupts get lost. Level is required. |

Other things seen:
- One boot came up with a green panel while the HDMI hub was attached at boot. The next boot without the hub was fine. Hypothesis, unproven: the dock at boot upsets the bootloader's panel handoff. Until known, plug the hub in after boot.
- Do not plug a charger into the hub while the boost is on. The regulator only checks CHGIN when enabling.

## Next steps

1. **IRQ storm.** Log every `ints[]` value in the handler and `INTSRC`/`SYSTEM_INT` on 0x66 for a few seconds. Find the source that re-asserts. Then compare with the S8+ driver (aaronsb/sm-x800-linux), which has working IRQ handling, and preferably move to that driver. Pass: IRQ 156 stays low, and replug of hub, charger and PC all switch roles.
2. **Mailbox lock.** The kernel driver and `gts7xl-charge-control` share the CCIC mailbox with no lock. Move the 9 V PD request into the kernel driver (fixed PDO 9 V / 1.65 A only), then retire the mailbox code in charge-control.
3. **Autoload.** Only after 1 and 2: load `max77705_typec` at boot and make `-typec.dtb` the default. Until then the default DTB stays peripheral mode, because the gadget is the debug path.
4. **c2: orientation.** QMP PHY + L18A. Read L18A's bootloader voltage back first (voltage rule in docs/sensors.md).
5. **DP (d, e).** First the gpio40/41 reservation test for i2c14 and the PS5169 at 0x28 (reset risk, its own boot). Then `mdss_dp`, then the altmode VDM flow.

## Results on the tablet (2026-11-07)

- IRQ storm: wrong pin config on PM8150L gpio11 (bias-disable, power-source 0). Stock's bias-pull-up and power-source 1 fix it (0051): one interrupt per event, 0 unhandled.
- 0053 is now the S8+ port's `max77705-usbc` driver, capped at 9 V / 1.65 A, plus a VBUS guard: the CCIC sends no detach interrupt while the tablet is the source, so CC is polled while sourcing. Autoloads; the board DTB has the port (0054). The `-usbhost` and `-typec` test DTBs are gone.
- Passed: boot attach and hot attach with hub + charger (host + sink, 9 V), hub without charger (tablet sources, ~600 mA from the battery, stable), digital USB-C headset, PC (gadget, SSH over USB), detach in each case.
- `gts7xl-charge-control` no longer touches the mailbox when the driver is present; the initramfs USB retry loop stops once the port exists.
- Next: DP alt mode, starting with the gpio40/41 / i2c14 / PS5169 test.

## Results on the tablet (2026-11-07, evening): first HDMI frame

DP alt mode works through a USB-C hub (Realtek DP bridge, VID 0x0bda, HDMI port 1; its HDMI port 2 shows nothing) to a 4K TV. Link HBR2 x2 (pin D), 3840x2160@60 at 18 bpp, smooth KWin desktop. Done by hand first, then moved into the kernel (in progress, below).

Findings, in order:

1. **gpio40/41 are not secured.** A read of their TLMM config through `/dev/mem` (south tile, 0x0f500000 + 0x1000 * gpio) did not reset the tablet. The `<40 2>` reservation was inherited and can go. Both lines idle high (board pull-ups).
2. **PS5169 at i2c-14 0x28.** Chip ID 0xAD/0xAC = 0x69 0x87, rev 0xAF/0xAE = 0x01 0x0b. Unconfigured it reads 0x40 = 0x40, 0x52 = 0x10.
3. **As power sink and data host the CCIC firmware does not discover by itself.** It waits for the AP to send Discover Identity (stock `max77705_alternate.c:1198-1203`). After that it runs Discover SVIDs and Modes itself, and sends DP Status itself after Enter Mode. The AP sends Enter Mode and DP Configure.
4. **Mailbox payloads** (opcode 0x48 SET_VDM_REQ; first byte is `VDO count | ACK expected << 3`, then the VDM header, then VDOs, little endian):
   - Discover Identity: `09 01 80 00 ff`
   - Enter Mode (SVID 0xff01, position 1): `09 04 81 01 ff`
   - DP Configure, UFP_U as UFP_D, DP v1.3 signalling, pin D: `0a 11 81 01 ff 06 08 00 00` (pin C: `... 06 04 00 00`)
5. **Replies** come from opcode 0x4B GET_VDM_RESP with one sub-id byte: 0x01 ID, 0x02 SVIDs, 0x03 Modes, 0x04 Enter, 0x05 Exit, 0x06 Attention, 0x10 DP Status, 0x11 DP Configure. Layout: `[sub] [status 0x8f] [msg id] [VDM header, 4] [VDOs, 4 each]`. Command type is header bits 7:6 (1 = ACK). Seen from the hub: ID header VDO 0x6ce00bda, SVID 0xff01, mode VDO 0x00000c05 (UFP_D, plug, pins C and D), DP Status 0x0000001a (UFP_D connected, enabled, multi-function preferred), Attention after Configure 0x0000009a (HPD high).
6. **Interrupts** for this flow: VDM_INT bit 0 ID, 1 SVIDs, 2 Modes, 3 Enter; PD_INT bit 2 DP Status, 3 DP Configure, 4 Attention. Stock unmasks VDM_INT_M = 0xf0 and PD_INT_M = 0x00 once alt mode starts. The fourth byte of the bulk interrupt read is VDM_INT.
7. **Orientation** for the PS5169: CCPinStat in CC_STATUS0 [7:6] (1 = CC1) agrees with gpio65 (0 = CC1). Stock: `is_front = !gpio65`.
8. **PS5169 programming** (stock `drivers/redriver/ps5169.c`): pulse 0x9d = 0x80, 10 ms, 0x9d = 0x00, then the WORK_MODE list (ends with this tablet's 0x52 = 0x30, 0x5e = 0x06). Mode in 0x40: USB3 0xc0, DP 4 lanes 0xa0, DP 2 lanes + USB3 0xe0, clear 0x80; +0x10 for CC2. With DP: 0xa0 = 0x00, 0xa1 = 0x04; otherwise 0xa0 = 0x02, 0xa1 = 0x00.
9. **AUX path**: the PS5169 does not switch AUX. gpio47 selects the side (0 = CC1, 1 = CC2) and gpio10 enables it (active low). Mainline's `gpio-sbu-mux` matches this exactly.
10. **QMP and DP**: `usb_1_qmpphy` needs L18A. Samsung's DT asks 912000 µV for this PHY (`qcom,vdd-voltage-level`), so L18A is 912000 fixed. `mdss_dp_out` data-lanes `<0 1>` to `usb_1_qmpphy_dp_in`, and the QMP `port@0` to the connector `port@1`. msm DP leaves idle only on a bridge HPD notify, so the Type-C driver registers an aux-hpd bridge on the connector node. EDID reads over AUX with only that.

Manual bring-up, kept for debugging: `research/dp-up.sh` and `research/dp-test-dt.patch` in the work tree (not in this repo).

### In the kernel (0055-0059)

- 0055 `ps5169` (`drivers/usb/typec/mux/ps5169.c`): typec switch + mux + DRM aux bridge between the QMP and the connector, modelled on `nb7vpq904m`. It forwards only DP states to the QMP: dropping the QMP to USB3-only on detach and back to USB3+DP on attach left link training failing (clock recovery) until reboot.
- 0056 `max77705_usbc`: VDM state machine on the interrupts above. Sink + DFP sends Discover Identity once PSRDY is set; Modes -> Enter; DP Status -> pin D when multi-function is preferred, else C; Configure ACK -> `typec_mux_set`; Attention -> HPD. Details that mattered on hardware:
  - HPD is reported once stable for 1.5 s. The hub raises HPD, drops it ~150 ms later and raises it again; msm torn down mid-plug often showed nothing.
  - Every request times out after 3 s and the flow starts over (5 tries). A Discover Identity sent right after the contract can go unanswered, and PSRDY can lag the last interrupt of an attach.
  - A power role swap (the hub's charger taking over after the tablet powered it) resets DP; the Enter reply was lost in the swap.
  - Detach drops HPD at once, then sets the muxes safe.
  - Do not reload the module with DP in use: msm keeps the old HPD bridge, notifications go nowhere (and it risks a use-after-free).
- 0057 board DT: i2c14 + PS5169, `gpio-sbu-mux` (gpio10 enable active low, gpio47 side), L18A 912000 µV, QMP `mode-switch`, `mdss_dp` on 2 lanes.
- 0058 board DT: MultiMedia3 (third q6asm DAI) to `DISPLAY_PORT_RX`, codec `mdss_dp`.
- 0059 `drm_hdmi_audio_helper`: also prepare at hw_params. ASoC prepares CPU DAIs before codecs; the AFE DP port started before msm enabled its audio engine never started (AFE enable -110, then `ADSP_EALREADY` until reboot).
- Userspace: `audio-up` routes MultiMedia3 and offers a PulseAudio sink "HDMI" (hw:0,2, 100%) only while DP-1 is connected. Opening the DP device with nothing connected wedges the ADSP's DP port until reboot.

Open: tablet as source (firmware never discovers; sending Discover Identity there makes the mailbox stop answering), hub needing a power cycle after some replugs (a phone recovers on the same hub, so something on attach differs), USB 3 (dwc3 is built in and the QMP a module: deferred-probe timeout, dwc3 init fails, the display comes up late), pin C (AUX fails after the QMP switch to DP-only).

### For other MAX77705 devices

The Galaxy Tab S8+ port (aaronsb/sm-x800-linux), whose `max77705-usbc` driver this one started from, uses the same CCIC. Findings 3 to 6 should apply there unchanged: without the AP's Discover Identity as sink + DFP, alt mode never starts.
