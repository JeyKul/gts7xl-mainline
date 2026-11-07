# DRM: Widevine for Tidal / Netflix on aarch64 Arch (Arch Linux ARM)

Verdict: **Widevine L3 works on aarch64 Linux** via Google's ChromeOS arm64 CDM, repackaged by Asahi's installer. Expect software-only decode, capped quality. Mainly needs recent glibc.

## Facts
- Tool: https://github.com/AsahiLinux/widevine-installer — downloads Google's LaCrOS (ChromeOS Chrome) arm64 squashfs from `commondatastorage.googleapis.com/chromeos-localmirror/distfiles`, extracts `WidevineCdm`, runs `widevine_fixup.py` on `libwidevinecdm.so` (only fixes ELF format for non-ChromeOS), installs under `/var/lib/widevine` with links for Chromium (`/usr/lib/chromium-browser/WidevineCdm`) and Firefox (`gmp-widevinecdm/system-installed`, `firefox/defaults/pref`). Needs root, `curl`, `squashfs-tools` (`unsquashfs`), `python3` (+ pyelftools? check script header).
- **Gate**: installer refuses glibc < 2.36 (`verchk`); README (current) says "Arch Linux ARM ships an ancient glibc… will not work". May be outdated → **check `ldd --version` on the tablet first**. If ALARM glibc ≥2.36 (ALARM tracks Arch snapshots with lag; likely 2.4x in late 2026, guess) it works.
- Widevine here = **L3** (software CDM, no TEE; Qualcomm hardware L1 exists on SM8250 but needs Android's TEE/OEMCrypto — not available to Linux).
- Service limits (typical, check live): **Netflix**: L3 → 540p (SD), no 1080p/HDR/4K without hardware DRM (Netflix 1080p on Linux needs L1; on Chrome/Firefox Linux max 720p in some regions via L3? **guess**: commonly 720p only with certain browsers; official: Linux browsers get up to 720p). **Tidal** (web player, tidal.com): HiFi/FLAC via Widevine is *not* L3-compatible for MQA/Max? Tidal web lossless uses DASH FLAC; Widevine L3 allows "HiFi (16-bit/44.1)" in Chrome on desktop (Max/Atmos needs L1 or the native app) – guess; test quality badge. Disney+/Prime: 720p; YouTube: no DRM needed (AV1/VP9 software).
- Browser support: Chromium (Arch ARM package `chromium`) reads `/usr/lib/chromium-browser/WidevineCdm`; Firefox (ALARM `firefox`) picks up system-installed GMP via `media.gmp-widevinecdm.*` prefs written by the installer. **Chrome Browser binaries are x86 only for Linux; aarch64 has only Chromium/Firefox**. Netflix additionally sniffs UA: Chromium arm64 on Linux sometimes gets 540p/"unsupported"; spoof Chrome UA (`--user-agent`) when it complains (guess).
- HW decode: not available in browsers (see video-codec.md) → **software decode** on 8 cores: fine to 1080p H.264 / VP9 for L3 streams; 4K not attempted (L3 doesn't give it anyway).

## Step by step (Arch ARM)
1. `ldd --version | head -1` ≥ 2.36 else `pacman -Syu` (ALARM glibc) and recheck; if still <2.36, wait or use a newer rootfs.
2. `pacman -S --needed git squashfs-tools python curl chromium firefox` (and `python-pyelftools` if the fixup script imports it: `head widevine_fixup.py`).
3. `git clone https://github.com/AsahiLinux/widevine-installer && cd widevine-installer`; read `widevine-installer` (it's a shell script); `sudo ./widevine-installer` and press Enter (accept license). Needs ~200–300 MB temp in /tmp (`mktemp -d /tmp/widevine-installer.*`, it `unsquashfs`es partial; ensure /tmp has space). Test: `ls /var/lib/widevine/WidevineCdm/manifest.json` and `libwidevinecdm.so` present.
4. Firefox: restart, `about:addons → Plugins`: "Widevine Content Decryption Module" present/enabled; `about:preferences#general → Digital Rights Management (DRM) Content` enabled; `about:config` `media.gmp-widevinecdm.visible=true`, `media.eme.enabled=true`. Test: <https://bitmovin.com/demos/drm> → Widevine playable; <https://shaka-player-demo.appspot.com/> "Angel One (Widevine)".
5. Chromium: restart, `chrome://components` → "Widevine Content Decryption Module" version listed (installed system-wide; may show "Component not updated" – fine). Test same demo pages. If DRM page missing, pass `--widevine-path`? (not needed when symlinked; check `chrome://version`).
6. Services: log in; Netflix → check quality via `Ctrl+Alt+Shift+D` stats overlay; Tidal web → Settings → Streaming quality; verify "HiFi" badge. If the service refuses (e.g. Netflix "M7701" or HDCP-level 0): spoof Chrome-on-Linux UA; for Netflix use Firefox (often 720p).
7. Update path: rerun installer when CDM version changes (it pins `WIDEVINE_VERSION`/`LACROS_VERSION`; `git pull`). Add a pacman hook or systemd timer if desired.

## Quality limits (L3, expect)
- Netflix: 540p (Chromium arm64 UA-dependent) up to 720p (some reports); no HDR/Dolby/4K. 
- Disney+/Prime/Max: ≤720p (Linux L3).
- Tidal: web player hi-res "Max" (MQA/FLAC 24-bit) and Dolby Atmos not available through L3 Widevine (use native client/Android); HiFi lossless 16/44.1 plausibly OK. Test.
- YouTube/Spotify web/Apple Music (FairPlay/Widevine?): Spotify web uses Widevine for premium → OK L3.

## Risks
- Legal/ToS: unofficial repackaging of Google's CDM; Google may revoke L3 certs for non-ChromeOS devices (has happened to some Linux ARM builds historically? **guess**).
- Security: runs a proprietary blob; keep it only in browser sandbox.
- Space: ~hundreds of MB in /tmp during install; clean `/tmp/widevine-installer.*`.
- glibc gate may block entirely on stale ALARM; avoid hacks (don't bypass verchk; the CDM needs new symbols).
- Hardware L1 / 1080p: not possible on Linux for this tablet.
