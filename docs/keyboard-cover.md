# Book Cover Keyboard

Samsung's Book Cover Keyboard (EF-DT970) connects through the pogo pins. It contains an STM32 microcontroller that talks to the tablet over I2C (bus 9, address 0x2a). The driver is `drivers/input/keyboard/samsung-stm32-pogo.c` (`linux-patches/0004`, `0030`, `0046`, `0048`).

**Never write to the keyboard's MCU** (firmware update or test commands). There is no recovery path if it breaks. Reading is fine.

## Input devices

| Device | What |
|---|---|
| `Samsung Book Cover Keyboard` | keys, from a row/column matrix in the DT |
| `Samsung Book Cover Touchpad` | multitouch buttonpad, reported as a libinput touchpad |
| `Samsung Book Cover Tablet Mode Switch` | `SW_TABLET_MODE`: 1 while the cover is detached |

## How attach and detach work

- `gpio43` is the connect line, high while the cover is attached. On a low level the driver cuts the cover's 3.3 V supply (`pogo_vdd_3v3`), as stock does. Once the line has been stable for 250 ms it powers the cover again. This is how the keyboard comes back by itself after the line drops.
- The tablet mode switch follows the same line, but only after 2 s without changes. Short dips during trackpad use (below) therefore don't flip the desktop into tablet mode.
- Desktops use the switch directly: KDE (touch mode "Automatic") and GNOME enter tablet mode when the cover comes off. KDE's default auto-rotate setting only rotates in tablet mode.
- Folding the keyboard back behind the tablet is not detected as tablet mode yet.

## Trackpad

- Axes: the controller's raw x/y already match the pointer directions (`0046`). An older rotation, copied from the stock driver, was wrong for this cover.
- **Known issue: stalls.** The pointer stops for 0.2 to 5 s at a time. On the stock kernel this happens too, except on One UI.

### Why it stalls (best current theory)

From the cover's MCU firmware (read-only analysis, details on the `trackpad-research` branch, `docs/trackpad-research.md`):

1. During touch, the MCU periodically reads the lid angle from the tablet's sensor DSP, over a separate link.
2. Each read waits up to 3 × 10 ms per transfer, so with no answer from the DSP the MCU's main loop blocks for about 90 ms per cycle, and its input queue backs up.
3. On One UI the DSP answers. On Linux it only does while the DSP runs (see [sensors.md](sensors.md)), and the lid-angle sensor (`bridge_driver_0`) still never probes.

Test C (60 s of finger movement each, gaps over 150 ms in the event stream):

| SLPI | Run 1 stalled | Run 2 (reversed order) stalled |
|---|---|---|
| running | 8 s | ~9 s (31 s including one long pause) |
| stopped | 47 s | 42 s |

A running DSP cuts the stalls about 3–5×, in both orders. The remaining fix is to get `bridge_driver_0` (DSP I2C bus 7, address 0x2a) to probe. It probably needs one more power rail, see the rails section of [sensors.md](sensors.md).

## Measuring stalls yourself

```sh
# event node of the touchpad
grep -l 'Book Cover Touchpad' /sys/class/input/event*/device/name
```
Read the node for 60 s while moving a finger without stopping. Count gaps between `SYN_REPORT` events longer than 150 ms; `evtest` timestamps work, as does a few lines of Python reading 24-byte `input_event` records.
