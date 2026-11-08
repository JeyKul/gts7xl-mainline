# Optional patches

Not applied by the CI build (`build.yml` only takes `linux-patches/*.patch`).
Apply by hand on top of the full series with `git am`.

- `rtc-allow-set-time.patch`: lets Linux write the PM8150 RTC counter
  (`allow-set-time`). Untested on this tablet; see `TESTING-rtc.md`.
