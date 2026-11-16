# Android frame timing (uibench RenderingJitter, `dumpsys gfxinfo framestats`)

One line per change; times in ms (p50 / p99).

| Date | Change | UI start after vsync | Frame to frame | Vsync → frame done | Notes |
|---|---|---|---|---|---|
| 2026-11-16 | baseline, schedutil only | 6.95 / 11.8 | | 25.6 / 31.2 | dequeueBuffer 12.8, GPU 2.5 |
| 2026-11-16 | + uclamp touch boost (power HAL) | 3.6 / 6.2 | 16.76 / 19.2 | 21.4 / 23.8 | no touch; vsync interval sd 0.16 |
| 2026-11-16 | + GPU min_freq boost on touch | | | | 670 MHz floor while touching; `input motionevent` is too heavy to measure it |

Findings:
- TE works: `te_rd_ptr` IRQs at 60/s match `pp_done` (DPU core_irq in debugfs). Vsync source `mdp_vsync_p` (gpio66).
- SurfaceFlinger composes on DPU planes (usesDeviceComposition=true, no client composition).
- The app waits ~13 ms in dequeueBuffer: one frame of latency in the buffer queue, not jitter. 120 Hz would halve it (half-screen bug at 120 Hz first).
