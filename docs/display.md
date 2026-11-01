# Native display research

**Status: working at 60Hz; 120Hz mode is offered but not confirmed.** What it took: stock on sequence with no sleep out or tear on, stock 60Hz mode, one packet per line, non-continuous DSI clock, `HS_REQ_SEL_PHY` cleared, interleave 1, no `TE` or block DMA in `TRIG_CTRL`, no `protected-clocks` on `dispcc`, and the panel module loaded straight after `msm`. Which of these are strictly needed hasn't been tested one by one.

Sources: registers the bootloader left running (read from `/dev/mem`), the stock kernel's
`techpack/display/msm/samsung/S6TUUM3_AMSA24VU01`, and Linux v7.3 `drivers/gpu/drm/msm`.

## Confirmed matching

| Item | Value |
|---|---|
| Timing | 2800x1752 at 120Hz, h 64/64/48, v 48/64/48 |
| Link | 4 lanes, about 1528 Mbps computed, stock 1530 |
| DSC | 2 encoders on 1 interface, 2 slices of 1400x12, 8bpc, 8bpp |
| Encoder values | PPS and bootloader DSC registers agree with the driver's calculated values by hand check |
| Common mode | split panel and multiplex, register value 3 |
| Pingpong | DSC enabled, data out endian flip set |
| Initial lines | 2 in the bootloader, 2 by the mainline formula |
| Rate control | Bootloader encoder uses min QP 13 for the last range, like mainline; the PPS sends 12 |

## Differences fixed in the driver

| Item | Before | Evidence |
|---|---|---|
| Packets per line | 2 | Bootloader sends 1: word count 2801, compression control 0x3911. The stock dtsi says 1 slice per packet, so this is the bootloader's setup, not stock Android's |
| Sleep out, tear on | Sent | Not in the stock on sequence |
| Display on | In the init sequence | Stock sends it separately, then UPI sync `B0 00 78`, `CB 70 33` |
| First display on | Missing | Stock sends `F8 58 00 D0 1D`, `F9 00 00 00 00` 18ms after display on, first time only |
| Brightness | `51` high byte first | Stock: `68 19 20 00 14`, `51` low byte first, `53 20`, range to 0xDD8 |
| Width | 273mm | Stock 267mm |

## MDSS probe stops the firmware framebuffer

With `mdss` enabled in the device tree, loading `msm` wipes the bootloader's display setup: DSC registers read 0 and `CTL_TOP` changes from `0x20000` to `0x1f00`. The panel then keeps showing its last frame while GNOME runs unseen. So the firmware framebuffer and an enabled `mdss` can't be used together: the native driver must take over in the same boot, or the image must go back to `out/direct-simplefb-arch.img`.

## Controlling the bootloader's display

The bootloader leaves its pipeline running: auto refresh on, planes scanning out `0x9c000000`, and the DSI link enabled. `kmod/gts7xl-panel-ctl` sends panel commands over that link without any reset or takeover: command format as in the msm driver, `DMA_BASE`, `DMA_LEN`, `TRIG_DMA`. Brightness is confirmed by eye. This doesn't replace a native driver, but needs no `mdss` in the device tree.

Plan: swap plane addresses during the blank period using `INTF_TEAR_INT_COUNT_VAL` as the position, which should stop tearing.

## Test results

| Run | Result |
|---|---|
| 1, 2 | `PHY timing calculation failed`, `Power on failed: -22`; the DSI link never started, so nothing reached the panel. Removing or restoring sleep out and tear on made no difference |
| Cause | `protected-clocks` on `dispcc`, added for the firmware framebuffer, stops Linux registering the display clocks, so the escape clock rate reads 0 |
| 3 | Clocks fixed. Panel driver never loaded: the user saw the panel turning green within the wait before loading it, and restarted. No software fault seen for the green |

| 4 | Panel loaded right after `msm`. Link up, encoder registers match the bootloader, no errors logged. Fullscreen glitchy lines with GNOME |
| 5 | GNOME off: glitch, then screen off |
| 6 | Status reads in LP mode work: power mode `1c`, signal mode `80`, DSI errors `00`, `DISP_DET` and TCON ready normal. Reads in HS mode return nothing |
| 7 | Display on sent 200ms after enable, as stock does it after the first frame: black, no glitch at first |
| 8 | Colour pattern written to `/dev/fb0`: black. Clocks correct (bit 1528 MHz, pixel 254.7 MHz, byte 191 MHz), no underruns, tear signal about 51 per second instead of 120 |
| 9 | Brightness 800 instead of 100: small glitches, then off |
| 10 | GNOME back on, frames reach the plane (`AR30`, linear): same |
| 11 | No panel reset or commands at all: same small glitches, then off. Status reads came back as garbage and the tear signal almost stopped, so the panel was not healthy in this run and this result is weak |

| 12 | Stock 60Hz mode (porches 224/320/212, `60 00`): tear signal counter runs at exactly 60 per second. Green briefly, then black |
| 13 | Auto refresh poked by hand: invalid test, wrong start register, and redraws cancelled it |
| 14 | Frames rewritten nonstop, HS clock forced on (mainline default): glitches. So frames reach the panel |
| 15 | `MIPI_DSI_CLOCK_NON_CONTINUOUS`, as the bootloader leaves no force request: black, no glitching, lane status `1f1f` like the bootloader, about 30 frames per second |
| 16 | `HS_REQ_SEL_PHY` cleared and interleave 1, so `0xac` and `CMD_CFG0` match the bootloader: slightly bigger glitch, then black. Signal mode reads `00` instead of `80` |

| 17 | `CTL_LAYER` set to border only, mixers output solid colours, frames paced: with 0x11 and 0x35 still sent, rolling garbage, no colours. So planes aren't the cause |
| 18 | `TRIG_CTRL` matched to the bootloader (no TE or block DMA bits): short line at the bottom, then black |
| 19 | Exactly the stock panel on sequence, no sleep out or tear on: **red and blue seen, the first correct image from the native path** |

| 20 | Colour blocks through the real plane, then GNOME: works, smooth, no tearing reported by the user |

Results vary slightly between identical runs, so small differences on screen aren't reliable.

Plane setup still differs: mainline scans one 2800 wide `DMA0` plane split over two mixers; the bootloader uses `VIG0` and `VIG1`, 1400 wide each, from physical memory. Whether the picture reaches the DSC encoder at all is unproven.

Open leads: the 51Hz tear signal; the bootloader runs hardware auto refresh (`INTF` 0x2b4 = 0x80000002) and mainline doesn't; the `CMD_CFG0` interleave field (bootloader 1, mainline 0); `INTF_CONFIG2` bit 8 and `INTF_CONFIG` bit 23; the `vddlcd` rail, which isn't modelled in the device tree.

So sleep out and tear on are back in the driver only because earlier runs got further with them, not because these runs showed they are needed.

The native image needs no `protected-clocks`; the working framebuffer image needs them. The source tree currently holds the native variant.

The panel has gone green whenever Linux takes over the display path and no panel driver runs. Next test: load the panel driver straight after `msm` at boot, with the auto power-off running and logs in `/var/log/early`.

## Still unexplained

- `INTF_CONFIG` bit 23 and `INTF_CONFIG2` bit 8 are set by the bootloader. Neither mainline nor the stock driver code read so far names them.
- Stock sends display on after the first frame; this driver sends it in `enable`, before the first frame.
- PHY timings are computed by mainline; stock's table is `00 31 0D 0D 2B 27 0D 0D 09 02 04 00 27 1C`. Not compared yet.
- No run has shown a correct picture yet.
