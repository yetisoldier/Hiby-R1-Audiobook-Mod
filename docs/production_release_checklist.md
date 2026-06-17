# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Public Release

- GitHub release: `v1.5.2`
- Firmware marker: `1.6.16.2-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.16.2-audiobook\r1-audiobooks-1.6.16.2-audiobook.upt`
- Package MD5: `80c0d7295c2d55575870c4d226e83be9`
- Package SHA256: `3109fea179b816dcdd4c1536b8973f527ef8f8b2d628942317f6b4ded62ca4c6`
- Rootfs MD5: `35ffdbb9b401c03f1742782da0104b55`
- Rootfs SHA256: `127b90ddfc92ecf2e668368e31422b5eb090c47e86011c391c30dd4b4ec4c475`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since 1.6.16.1

- Hotfix for the remaining new-SD-card scan case where the stock scanner could
  rewrite audiobook rows back to their original genre tags without changing the
  DB file size.
- The DB helper now supports a cheap `--needs-maintenance` check.
- The watcher uses that check before skipping same-size DB changes and repairs
  with `content-repair-mtime` when needed.
- The watcher repairs the primary DB once and copies it to mirror DB paths,
  reducing heavy post-scan DB work on large cards.
- Active audiobook resume polling is reduced from 1 second to 2 seconds while
  preserving the 15-second save cadence.

## Local Verification

Local package verification passed on 2026-06-17:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.16.2-audiobook `
  --upt-name r1-audiobooks-1.6.16.2-audiobook.upt `
  --expected-version 1.6.16.2-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.16.2" `
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

The public `1.6.16.2` hotfix package was flashed on the test R1 on 2026-06-17.
Installed verification passed under
`work\installed-release-verification\20260617-170615`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16.2-audiobook `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.2-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` was renamed after flashing.
- `/usr/data` free space is about 26 MB.
- DB integrity is `ok`.
- Audiobooks contains 298 media rows across 52 books on the regression SD card.
- SD-root `/usr/data/mnt/sd_0/usrlocal_media.db` has integrity `ok` and 298
  audiobook rows with normalized `Audiobook` genre values.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.
- A screenshot-assisted ADB check opened Audiobooks to the title list instead of
  `No music found`: `work\adb-control\screenshots\20260617-170213-preset-main-audiobooks.png`.

The installed package archive on the SD card is:

```text
/usr/data/mnt/sd_0/r1-audiobooks-1.6.16.2-audiobook-installed-20260617-1703.upt
```

## Publish Commands

After release assets are staged under `firmware\releases\v1.5.2`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.2 `
  -Name "HiBy R1 Audiobook Mod v1.5.2" `
  -BodyFile firmware\releases\v1.5.2\README.md `
  -Assets "firmware\releases\v1.5.2\r1-audiobooks-1.6.16.2-audiobook.upt,firmware\releases\v1.5.2\MD5SUMS.txt,firmware\releases\v1.5.2\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.2 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.5.2\r1-audiobooks-1.6.16.2-audiobook.upt,firmware\releases\v1.5.2\MD5SUMS.txt,firmware\releases\v1.5.2\SHA256SUMS.txt"
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
