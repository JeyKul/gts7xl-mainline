# Testing: dark, green photos from the rear camera

Branch `camera-stills-fix`. Not tested on the tablet yet. Everything below runs as your
user with the Camera app closed. Nothing here touches focus, power or sensor registers;
the only writes are the V4L2 format, exposure and gain controls libcamera sets itself.

## What is wrong (most likely)

The preview and the photo use different sensor modes: the preview a binned 1008x756 mode,
the photo the full 4128x3096 mode. On every mode change the sensor driver resets the
exposure to its default (`s5k3m5`: 256 lines, about 2.2 ms; `s5k-tab`: VTS/4).
The settled preview typically sits near 3318 lines, so the photo starts about 13 times
darker (more if the binned mode collects more light per pixel).

libcamera 0.7.2's software ISP doesn't reset anything itself. It starts from whatever the
sensor holds, and climbs back slowly:
- When the frame is very dark it raises exposure by at most 6% per step (`agc.cpp`:
  error ≤ 1.5 × 0.04).
- It computes statistics only on every fourth frame (`swstats_cpu.h`, `kStatPerNumFrames = 4`).

The app kept the 10th frame, which allows only two or three AGC steps (+12-19%). So the
photo is badly underexposed.

The green cast follows from that. AWB gains are reset to 1.0 at every start (`awb.cpp`
`configure`). On a frame this dark the colour sums fall below the black-level offset, get
clamped to 1, and the gains come out as exactly 1.0. That leaves the sensor's raw colour:
green about 1.8x red and 2.4x blue, which gamma 2.2 turns into the measured ~1.3x and ~1.5x.

The photo is not saved raw. It goes through the same software ISP as the preview (black
level, AWB, gamma). It is simply exposed for the wrong mode.

Confidence: fairly high that this is the main cause, from the code. One thing doesn't fit
the numbers: a max of only ~21-30/255 after gamma means more than 13x underexposure. So
the binned preview mode may also be several times more sensitive than the full mode (see
step 4 of the diagnostic). Hypotheses, ranked:

1. Exposure reset on mode switch, with slow AGC and a fixed 10-frame wait (above).
2. On top of 1: the 1008x756 mode (vertical binning) is more sensitive than the full mode,
   so the same exposure and gain give a darker full-size frame. `--long` measures this.
3. AGC at full size not running, or running the wrong way. Step 2 of the diagnostic
   prints how many AGC updates ran and in which direction.

## What the branch changes

- `gts7xl-camera`:
  - Before each pipeline starts, it sets the sensor to the mode libcamera will pick, then
    restores the exposure and gain the previous stream ended with. libcamera's configure
    then sees the same mode and doesn't reset, and AGC starts from the right value.
  - Photos are kept once the last 6 frames agree within 4% (at least 10 frames; after
    8 s it saves anyway), instead of always the 10th frame.
  - JPEG is now written with GdkPixbuf.
  - Prints `seeded ...` and `photo after N frames: mean R G B, sensor (exposure, gain)` to
    the terminal.
  - `GTS7XL_CAMERA_MODE_GAIN=<x>` scales the exposure when moving from preview to photo,
    if step 4 finds the modes differ.
- `gts7xl-cam-diag`: the diagnostic below.
- `linux-patches/0067-media-cameras-keep-the-exposure-across-sensor-mode-c.patch`: the
  kernel side of the same fix (drop the exposure reset in `s5k3m5` and `s5k-tab`; all modes
  of each sensor share HTS, VTS and PLL, so lines mean the same time). With it, every
  libcamera app benefits, not just ours. It applies cleanly on top of 0066 (checked
  against the reconstructed files, blob hashes match), but it **hasn't been compiled**.
  The app fix doesn't need it.

## 1. Diagnose (about a minute, no install needed)

Copy the script over, or check out the branch on the tablet, then:

```sh
sudo install -m755 rootfs/overlay/usr/local/sbin/gts7xl-cam-diag /usr/local/sbin/
gts7xl-cam-diag rear
```

Keep the tablet still and pointed at one indoor scene. Send `~/cam-diag-*/summary.txt`.

The three steps:
1. Preview mode, 90 frames.
2. Full size straight after, 12 frames: what the app does today.
3. Preview again, then the preview's exposure and gain set by hand at full size, 12 frames.

**Hypothesis 1 confirmed if:**
- In step 2 the first AGC line shows `exp 256`, and only 2-3 AGC updates ran.
- Step 2's JPEGs are dark and green (mean far below step 1; G well above R and B; last
  AWB gain near 1/1).
- Step 3's JPEGs come out about as bright as step 1's, with normal colour.

**Hypothesis 2 is likely if** step 3 is clearly brighter than step 2 but still darker than
step 1. Then run the long test (a few minutes at full size):

```sh
gts7xl-cam-diag rear --long
```

It prints `preview/full sensitivity (MODE_GAIN)`. Valid only if step 4's last AGC line
has `error` within ±0.2.

## 2. Try the fixed app

```sh
sudo install -m755 rootfs/overlay/usr/local/bin/gts7xl-camera /usr/local/bin/
gts7xl-camera            # from a terminal, to see its output
```

Let the preview settle and take a few photos. If step 4 gave a ratio, try
`GTS7XL_CAMERA_MODE_GAIN=<ratio> gts7xl-camera`.

**Success looks like:**
- The terminal shows `seeded 4120x3096: exposure <n> gain <g>`, with `<n>` the preview's
  value (not 256).
- `photo after 10-20 frames: mean R .. G .. B ..`, with the three means close to each
  other and well above 3.
- The saved photo is about as bright as the preview, without the green cast. Mean
  roughly 60-150/255 indoors, highlights reaching 200+.

It takes a moment longer than before (the 12.7 MP software ISP runs at a few frames per
second). If it says `(timed out)`, exposure was still moving after 8 s. Please report that.

The front and ultrawide cameras use the same logic and should benefit too.

## 3. Optional: kernel patch 0067

Build the kernel with `linux-patches/0067-*.patch` as usual (CLAUDE.md), and flash it.

Then step 2 of `gts7xl-cam-diag` alone should no longer start at `exp 256`: the sensor
keeps the preview's exposure. The old app's photos should also look right, apart from
AWB, which still needs a few frames.

If it doesn't compile, it is a 2-line change in each `set_fmt`: just drop the
`__v4l2_ctrl_s_ctrl(... exposure ...)` call.

## Workaround without the new app

After using the preview (so the sensor holds a good exposure), with the app closed:

```sh
CAM=/base/soc@0/cci@ac4f000/i2c-bus@0/camera@2d
SUB=$(for d in /sys/class/video4linux/v4l-subdev*; do [ "$(readlink -f $d/device/of_node)" = /sys/firmware/devicetree$CAM ] && echo /dev/${d##*/}; done)
E=$(v4l2-ctl -d $SUB --get-ctrl=exposure | awk '{print $2}')     # read BEFORE the mode changes
v4l2-ctl -d $SUB --set-subdev-fmt pad=0,width=4128,height=3096
v4l2-ctl -d $SUB --set-ctrl=exposure=$E
LIBCAMERA_SOFTISP_MODE=cpu gst-launch-1.0 -q libcamerasrc camera-name=$CAM ! video/x-raw,width=4120,height=3096 ! \
  identity eos-after=20 ! videoconvert ! jpegenc ! multifilesink location=$HOME/Pictures/shot.jpg
```

The last of the 20 frames is kept. Without the seeding, running full size for a minute or
more (`eos-after=300`) also ends bright, because AGC gets there eventually.

Raw (Bayer) photos are possible with `cam -c1 -s role=raw -C1 --file=...`. That gives
unprocessed 10-bit data that needs a raw developer. The soft ISP can't give raw and RGB
from one stream, so the app doesn't offer it yet.
