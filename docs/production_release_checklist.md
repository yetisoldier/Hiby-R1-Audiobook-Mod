# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Public Release

- GitHub release: `v1.5.0`
- Firmware marker: `1.6.16-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.16-audiobook\r1-audiobooks-1.6.16-audiobook.upt`
- Package MD5: `4938a5d3f74204995a1bb297175da463`
- Package SHA256: `ba3b16dc63e35abfc22cd0ac9e4324a5a2e3834ad894c42fd310f30f99c3f1e0`
- Rootfs MD5: `48abe53dc5e83e8eeb045dfd8f4a3d17`
- Rootfs SHA256: `adfdf99eefdeb2693aa8cf610780b05e60404f7d8e6dac33b0b5ef6b1c1d69ca`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since 1.6.15

- Audiobooks launcher icon added.
- Guarded Back cleanup reduces the old double-Back quirk from the Audiobooks
  title/list area.
- Multipart title-start resume is faster and avoids stale previous-book memory
  roots.
- Direct-open and restore-settle guards improve landing on the saved file and
  position for multipart books.
- Steady-state audiobook save cadence is 15 seconds.
- DB watcher boot/restart handling is hardened, including stale lock recovery,
  boot-stability waiting, late `/Audiobooks` retry, and clean stop/restart.
- Missing/empty media DB recovery can seed a valid schema and scan `/Music` and
  `/Audiobooks`.
- Title/author/series sidecar catalogs are generated for future UI work.
- Native DSD, Bluetooth SBC XQ, and USB DAC related flags are enabled.
- Boot ADB remains disabled in public builds.

## Local Verification

Local package verification passed on 2026-06-17:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.16-audiobook `
  --upt-name r1-audiobooks-1.6.16-audiobook.upt `
  --expected-version 1.6.16-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.16" `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The default verifier also supports:

```powershell
python tools\verify_r1_audiobook_build.py `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-current-hashes
```

## Device Verification

The public `.16` package was flashed on the test R1 on 2026-06-17. Installed
verification passed under `work\installed-release-verification\20260617-151416`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16-audiobook `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` was renamed after flashing; the verifier reported
  `no-r1.upt`.
- `/usr/data` free space is about 30 MB.
- DB integrity is `ok`.
- Audiobooks contains 135 media rows across six books.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.

The installed package archive on the SD card is:

```text
/usr/data/mnt/sd_0/r1-audiobooks-1.6.16-audiobook-installed-20260617-1513.upt
```

## Publish Commands

After release assets are staged under `firmware\releases\v1.5.0`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.0 `
  -Name "HiBy R1 Audiobook Mod v1.5.0" `
  -BodyFile firmware\releases\v1.5.0\README.md `
  -Assets "firmware\releases\v1.5.0\r1-audiobooks-1.6.16-audiobook.upt,firmware\releases\v1.5.0\MD5SUMS.txt,firmware\releases\v1.5.0\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.0 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.5.0\r1-audiobooks-1.6.16-audiobook.upt,firmware\releases\v1.5.0\MD5SUMS.txt,firmware\releases\v1.5.0\SHA256SUMS.txt"
```

## Known Limitations

- ADB is not persistent in practice on the test device. It must be manually
  enabled after reboot/update for verification.
- Back navigation from Audiobooks is improved by a guarded cleanup, but it is
  still built on top of the stock Genres route.
- There is no audiobook search UI; browse by scrolling through titles.
- The DB helper provides practical fallback metadata, not a full audiobook tag
  parser. Clean folder structure and numbered multipart files still matter.
- USB DAC and Bluetooth SBC XQ are lightly tested compared with the audiobook
  features.
