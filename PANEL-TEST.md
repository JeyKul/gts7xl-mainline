# Testing the panel handoff

Goal: from the bootloader splash to the desktop with at worst a black screen, never green garbage.

Build with the patches in `linux-patches/` too, because the load order matters.

## Modes

Set the mode, then do a full reboot. The mode matters only at boot:

    echo 'options panel-samsung-s6tuum3 clean_handoff=1' | sudo tee /etc/modprobe.d/panel-handoff.conf

| Mode | What it does |
|---|---|
| 0 | Old behaviour, for comparison |
| 1 | Default. Display off and sleep in over DSI on the first prepare, then reset |
| 2 | Holds reset from probe until the first prepare |

Check the mode with `cat /sys/module/panel_samsung_s6tuum3/parameters/clean_handoff`.

## Test

Film each boot, from power on to the desktop. Run modes 2, 1 and 0, twice each, because results vary between identical runs.

## What to look for

- When the splash goes away, and whether the screen goes black or green.
- How long any green lasts, and whether it comes before or after a black period.
- Any flash when the picture appears.
- Whether the desktop comes up, and whether it stays correct at 60Hz.
- After suspend and resume: the picture comes back, about 200ms slower than before.

## What to send back

The video timings and, for each mode:

    dmesg | grep -iE "dsi|dpu|underrun|timeout|panel"

Useful lines:

- `handoff: holding reset until first prepare`: its time, against the time green appeared.
- `handoff: panel off sent: N`: mode 1 only, 0 means the commands were sent.
- `tcon ready after N ms: V`: `V` should be 1. In mode 0, `0 ms` would mean the old poll passed at once.

## Reading the result

- Green before the `holding reset` line: the gap between the MDSS reset and panel probe is visible.
- Green after the `holding reset` line: reset alone does not darken the panel.
- Green after the `tcon ready` line: the cause is in the first frames, not the handoff.

## Result, 2026-11-04

Five boots across modes 0, 1 and 2: every mode keeps one glitch line at the start, from msm stopping the link mid-frame. Mode 1 is the default. The line could only be removed by blanking the panel before msm loads.
