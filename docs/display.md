# Native display research

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

## Test results

| Run | Result |
|---|---|
| 1, 2 | `PHY timing calculation failed`, `Power on failed: -22`; the DSI link never started, so nothing reached the panel. Removing or restoring sleep out and tear on made no difference |
| Cause | `protected-clocks` on `dispcc`, added for the firmware framebuffer, stops Linux registering the display clocks, so the escape clock rate reads 0 |
| 3 | Clocks fixed. Panel driver never loaded: the user saw the panel turning green within the wait before loading it, and restarted. No software fault seen for the green |

So sleep out and tear on are back in the driver only because earlier runs got further with them, not because these runs showed they are needed.

The native image needs no `protected-clocks`; the working framebuffer image needs them. The source tree currently holds the native variant.

The panel has gone green whenever Linux takes over the display path and no panel driver runs. Next test: load the panel driver straight after `msm` at boot, with the auto power-off running and logs in `/var/log/early`.

## Still unexplained

- `INTF_CONFIG` bit 23 and `INTF_CONFIG2` bit 8 are set by the bootloader. Neither mainline nor the stock driver code read so far names them.
- Stock sends display on after the first frame; this driver sends it in `enable`, before the first frame.
- PHY timings are computed by mainline; stock's table is `00 31 0D 0D 2B 27 0D 0D 09 02 04 00 27 1C`. Not compared yet.
- None of these changes has reached the panel yet.
