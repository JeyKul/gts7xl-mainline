# Pogo trackpad stalls: research

Desk research only, nothing run on hardware. The symptom used here is the owner's latest: on Linux the pointer moves, stalls every few seconds, then resumes. In TWRP there is no cursor and touches look like touchscreen input.

Sources:
- `S`: Samsung kernel [shezik/samsung-kernel-T970XXS7DXH1](https://github.com/shezik/samsung-kernel-T970XXS7DXH1), `drivers/input/keyboard/stm/stm32l0/`
- `FW`: `firmware/keyboard_stm/stm32_gts7l.bin` from the same tree, the file the stock gts7xlwifi DTS names. Its strings say `TabS7/S7+ Lite STM32G0 V27`; version bytes at 0x200 read `0027 0027 20210422`. Addresses are from my own disassembly. Register and pin roles are inferred from code and log strings.
- `DTS`: `arch/arm64/boot/dts/samsung/gts7xl/kona-sec-gts7xlwifi-eur-overlay-r06.dts`

Check first: the MCU on the tablet may run a different version from this file. Compare with the version the handshake logs.

## Corrections to the current notes
- The STM32G0 looks tablet-side, not in the cover (inference). The AP has its reset and SWCLK lines (`DTS:11391-11392`), and it has its own I2C bus to the SLPI. The cover seems to talk to it over a "1Wire" link (`FW:0x8004838`).
- The second I2C bus is a real SLPI link. On PA11 the firmware logs `SLPI I2C CLK Rlease` (`FW:0x800bf64`). It reads as I2C slave there: `FW:0x80055f4` calls a function taking buffer, length and timeout, with no device address (inferred from the signature).
- TWRP showing no cursor is expected and says nothing about the fault. Stock registers a plain MT device (`S/stm32_pogo_touchpad.c:634-656`). Only the gpio43 observation in TWRP counts as evidence.

## Likely causes, ranked

### 1. SLPI not answering blocks the MCU main loop (best fit, unproven)
Sequence in the firmware:
1. Cover frames of type 5 become message 8, at most one per 30 ms (`FW:0x8001c24-0x8001c68`, tick cleared at `0x800bd78-0x800bd82`).
2. Message 8 calls the lid-angle routine from the main loop (`FW:0x8003d84`, call at `0x8003dd6`).
3. That routine drives PA15 low, then does three slave receives, then drives PA15 high (`FW:0x80056b2`, `0x80056be`, `0x80056fe`, `0x800573a`, `0x800576e`).
4. Each receive has a 10 ms timeout and up to 3 tries (`FW:0x80055e4-0x8005642`), so with a silent SLPI one cycle blocks for about 90 ms.
5. The failure is only logged as `SLPI_COMM_FAIL`, and nothing disables the cycle (`FW:0x8003de2-0x8003de6`, `0x8003ec4`).

Why this would stall the trackpad:
- Calculated maximum: messages can arrive at 33/s, while lid-angle cycles drain only about 11/s.
- The queue has 100 slots and no full check, so a lap makes it look empty and drops the backlog (`FW:0x8003a40-0x8003a5e`, `0x8003a6c`).
- Touch frames share one payload buffer (`FW:0x8001ba2-0x8001bc0`), so a backlog also loses or repeats coordinates.
- At that rate, backlog plus lap takes roughly 1-4 s. That matches "stalls every few seconds", but the real type 5 rate is unknown.

Why it fits the other facts:
- One UI: the SLPI answers, so the cycle should be short.
- TWRP and Linux: no working SLPI sensor stack. In TWRP this is an assumption; check it.
- The old README line "gpio43 bouncing about 10 times a second" matches a cycle of about 90 ms.
- PA15 idles high and goes low for exactly this window. Whether it reaches gpio43 is a guess; no schematic was available.

Gaps:
- Why typing alone is fine. Guess: the cover sends type 5 frames only during touch. The cover firmware is not available.
- What physically pulls gpio43 low.

### 2. The power cut on gpio43 low turns each dip into a long stall (certain as code, amplifier only)
- Stock cuts power on any low level and checks again 250 ms later (`S/stm32_pogo_i2c.c:1232-1234`, `1153-1170`). Patch 0030 copies this.
- Repo stall per dip: 250 ms debounce, 50 ms after power on, MCU boot, then a handshake with `msleep(200)` inline in the IRQ thread. Stock runs that from delayed work (`S/stm32_pogo_i2c.c:880`).
- This does not explain why gpio43 dips. It does set how long each stall lasts.

### 3. Rail sag under touch load (guess, low)
- The 3.3 V rail is a fixed GPIO70 enable (`DTS:11243-11250`). Its upstream supply is unknown, and the owner has already ruled out BOB.
- It would not explain why One UI is fine unless Android holds a rail up. Needs a scope; try it last.

### 4. libinput discarding events (possible extra, cannot explain gpio43)
- Disable-while-typing, palm handling, or no axis resolution: the repo driver sets no `input_abs_set_res`.
- Experiment A separates this in one run.

### Ruled out by the source
- A userspace mode switch or keep-alive through the kernel. The touchpad driver only receives (`S/stm32_pogo_touchpad.c:703-705`). Sysfs offers only firmware, debug and `keyboard_connected`, plus raw `write_cmd`/`read_cmd` on non-ship builds (`S/stm32_pogo_i2c.c:1875-1891`). The stock tree has no report-mode command. A framework write through `write_cmd` or `keyboard_connected` is not excluded by source alone.
- The stock bus vote (`S/stm32_pogo_i2c.c:929-943`). It runs in TWRP too, so it cannot be the difference. The README also records TWRP showing the gpio43 drops.
- MCU filtering touch by lid state (`FW:0x8003c50-0x8003c9c`): state 1 blanks the touch payload and state 2 rewrites it as a hall event, which the repo driver treats as a finger release. Those states are set only after a successful SLPI read (`FW:0x8003de2-0x8003e78`), so this is not expected without an SLPI. One exception remains open: a non-zero `0x20000012` also blanks touch, and message 11 (`GO_TO_STOP`, `FW:0x8003bba`) resets it, with type 5 frames counting it up (`FW:0x8001c26-0x8001c30`). Experiment B would show either case as release frames.

## Experiments
Apply `docs/trackpad-conn-debounce.patch` for B and C; A works on the current kernel. Keep a finger moving the whole time, or gaps mean nothing. The defconfig has no ftrace or dynamic debug, so the patch adds a `conn_trace` parameter.

**A. Kernel or userspace?**
```sh
DEV=/dev/input/$(basename $(dirname $(dirname $(grep -l 'Book Cover Touchpad' /sys/class/input/event*/device/name))))
evtest "$DEV" > /tmp/ev.log &
libinput debug-events --device "$DEV" > /tmp/li.log &
sleep 60; kill %1 %2
awk '/SYN_REPORT/{t=$3+0; if(p&&t-p>0.15)printf "%.3f gap %.0f ms\n",t,(t-p)*1000; p=t}' /tmp/ev.log
```
- Gaps in `ev.log`: kernel or MCU side. Go to B.
- No gaps in `ev.log` but pointer stalls: userspace (cause 4). Check `/tmp/li.log`, and `journalctl -b | grep -i 'libinput\|lagging'`.

Stop other readers only if needed: `libinput debug-events` needs the device readable, not exclusive.

**B. Do the stalls line up with gpio43, and how wide are the dips?**
```sh
echo 1 > /sys/module/samsung_stm32_pogo/parameters/conn_trace
dmesg -w | grep --line-buffered 'conn=' > /tmp/conn.log &
evtest "$DEV" > /tmp/ev.log &
sleep 60; kill %1 %2
awk -F'conn=| t=' '{if($2==1&&p)printf "low %d ms\n",int(($3-p)/10000)*10; if($2==0)p=$3}' /tmp/conn.log | sort | uniq -c | sort -rn | head
```
`t=` is microseconds of real time and evtest uses real time too, so the two logs compare directly.
- Stalls gone or much shorter with the patch: cause 2 confirmed.
- Low pulses near 90 ms at about 10 Hz: supports cause 1 and a PA15 link.
- Very different widths: that link is wrong.
- Gaps with no conn edges: the MCU is stalling without dropping the line, which also fits cause 1's backlog. Log hall frames too, to rule out the `POGO_ID_HALL` release path.

**C. Does a live SLPI fix it? (the key test)**
Run A while `sensord` is up, inside the roughly 41 s before `sensor_process` crashes (NOTES, 2026-11-04). Repeat after the crash, and with the SLPI stopped. Stalls gone only while the SLPI serves would confirm cause 1. No change is inconclusive, since the lid-angle client on the DSP may not be running yet.

**D. The reverse test on One UI (needs root)**
```sh
getprop | grep -iE 'init.svc.*(sensor|sscrpcd)'   # find the service names
setprop ctl.stop <name>                           # stop them
ls /sys/class/remoteproc/; cat /sys/class/remoteproc/*/name
```
Stalls appearing in One UI would be strong proof. If the SLPI keeps serving on its own, no change proves nothing.

**E. Compare frames on the stock kernel**
Run on One UI and in TWRP:
```sh
echo 2 > /sys/class/sec/sec_keypad/debug
dmesg -w | grep -E 'pogo_notifier_notify|stm32_conn_isr|stm32_check_conn_work|sendconn'
```
Compare frame rate, hall frames (ID=4), and conn events during touch. `debug` bit 1 logs frames (`S/stm32_pogo_i2c.c:116`, `126`). The `con:`, `int:` and `rst:` line prints every 30 s (`S/stm32_pogo_i2c.c:48-61`).

**Optional.** The firmware prints these strings over a debug UART (`FW:0x8004658`, assuming it is not stubbed). If the pads are reachable, `SLPI_COMM_FAIL` would settle cause 1 at once. Pins and baud are unknown.

## Fix direction
- Short term: `docs/trackpad-conn-debounce.patch`. Written against 0001+0004+0030. It applies to the driver rebuilt from those, but has not been compiled or run. It is a mitigation for cause 2 and does not fix cause 1. Risk: the pogo rail stays on up to 250 ms after a real detach, where stock cuts it at once.
- Real fix if C or D confirm cause 1: get the SLPI lid-angle path answering through `sensord`. The AP has no known route onto that bus, so it can't stand in for the SLPI.
- Don't probe MCU commands such as `DATA_COMMAND_SET_TEST_MODE_FRAME` blindly. The README says unsolicited I2C upsets the MCU, and those strings look like cover-link or factory commands.
