# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Public Release

- GitHub release: `v1.5.3`
- Firmware marker: `1.6.16.4-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.16.4-audiobook\r1-audiobooks-1.6.16.4-audiobook.upt`
- Package MD5: `d6ebce37c653f3756b54a7b5c3725788`
- Package SHA256: `eefd1f060babf5930d7bae4be481d7f580edf225a128d17ab6130beced4dd404`
- Rootfs MD5: `8728cd7ad4734f3f36efdfe6d0c1093a`
- Rootfs SHA256: `394db7b39571f3cc95f04ceec1195f1fedb0abe3ac2a3dec3dbf5f7c3461c152`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since 1.6.16.2

- Hotfix for the case where Music -> Update Database creates a valid media DB
  with zero audiobook rows even though files exist under `/Audiobooks`.
- The DB helper's fast `--needs-maintenance` check now scans `/Audiobooks` and
  compares those file paths with DB audiobook rows.
- Folder location now wins over genre metadata; audiobook files do not need an
  exact `Audiobook` genre tag.
- The watcher repairs this state with `content-repair-mtime`, regenerates the
  audiobook catalogs, and mirrors the repaired DB to `/data` and the SD-root
  DB copy.

## Local Verification

Local package verification passed on 2026-06-22:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.16.4-audiobook `
  --upt-name r1-audiobooks-1.6.16.4-audiobook.upt `
  --expected-version 1.6.16.4-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.16.4" `
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

The public `1.6.16.4` hotfix package was flashed on the test R1 on 2026-06-22.
Installed verification passed under
`work\installed-release-verification\20260622-084707`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16.4-audiobook `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.4-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` is allowed during staged verification; remove or rename it
  after manual install.
- `/usr/data` free space is about 18 MB after cleanup.
- DB integrity is `ok`.
- Audiobooks contains 298 media rows across 52 books on the regression SD card.
- SD-root `/usr/data/mnt/sd_0/usrlocal_media.db` has integrity `ok` and 298
  audiobook rows.
- Forced live regression test: replacing the primary DB with a same-size copy
  containing zero audiobook rows while 298 audiobook files existed triggered
  `content-repair-mtime`, rebuilt 298 audiobook rows, and mirrored the fixed DB.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.
- Audiobook detection is based on `/Audiobooks` folder location, not an exact
  genre tag.

## Publish Commands

After release assets are staged under `firmware\releases\v1.5.3`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.3 `
  -Name "HiBy R1 Audiobook Mod v1.5.3" `
  -BodyFile firmware\releases\v1.5.3\README.md `
  -Assets "firmware\releases\v1.5.3\r1-audiobooks-1.6.16.4-audiobook.upt,firmware\releases\v1.5.3\MD5SUMS.txt,firmware\releases\v1.5.3\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.3 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.5.3\r1-audiobooks-1.6.16.4-audiobook.upt,firmware\releases\v1.5.3\MD5SUMS.txt,firmware\releases\v1.5.3\SHA256SUMS.txt"
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
