# Help wanted

Only things we are sure we can't work out ourselves: they need closed Samsung/Qualcomm knowledge or signed code.
Everything we can still investigate lives in `TODO.md` ("Missing internals"). Details and dates are in `NOTES.md`.

## Fingerprint (Goodix GW9558)

- The sensor is reached only through a Samsung trusted app in TrustZone. Its command protocol and its signed TA are not public.
- What Android does is in `docs/fingerprint.md`. We need someone who knows that TA's interface, or a documented way to load it from Linux (QSEECOM).

## TrustZone PAS error codes

- TZ returns raw status codes that mainline maps to -EINVAL. For example, `a0 = 0xffcfffbb` (−0x300045) on `qcom_scm_pas_init_image` came from wrongly signed IPA firmware.
- There is no public table of these codes. A table would make every remaining TZ refusal (NPU, PAS 23) much quicker to debug.
