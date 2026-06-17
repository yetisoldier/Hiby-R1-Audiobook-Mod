# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Public Release

- GitHub release: `v1.5.1`
- Firmware marker: `1.6.16.1-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.16.1-audiobook\r1-audiobooks-1.6.16.1-audiobook.upt`
- Package MD5: `d30527750a071602a67f1eceb462f8cc`
- Package SHA256: `085495646039eafb496279d3ef2625671783552ad069150c3e959e5c219d7f3f`
- Rootfs MD5: `7a0b2a3d001ea53b079b79fbcf9c5933`
- Rootfs SHA256: `26c9b68e49a3761930dcae3c95b172905d8e88108c68f59be44ffe3c0a96d942`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since 1.6.16

- Hotfix for new SD cards where Audiobooks could show `No music found` after
  Music -> Update Database.
- The DB watcher now normalizes all active media DB copies:
  `/usr/data/usrlocal_media.db`, `/data/usrlocal_media.db`, and
  `/usr/data/mnt/sd_0/usrlocal_media.db`.
- Verified on the regression SD card that the SD-root database has integrity
  `ok`, contains 298 normalized audiobook rows, and still keeps Audiobooks out
  of Music Search, Albums, and Genres.
- Verified the Audiobooks launcher opens the title list instead of `No music
  found`.
- All `1.6.16-audiobook` UI, resume, audio unlock, and catalog features are
  retained.

## Local Verification

Local package verification passed on 2026-06-17:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.16.1-audiobook `
  --upt-name r1-audiobooks-1.6.16.1-audiobook.upt `
  --expected-version 1.6.16.1-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.16.1" `
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

The public `1.6.16.1` hotfix package was flashed on the test R1 on 2026-06-17.
Installed verification passed under
`work\installed-release-verification\20260617-160119`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16.1-audiobook `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.1-audiobook`.
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

The installed package archive on the SD card is:

```text
/usr/data/mnt/sd_0/r1-audiobooks-1.6.16.1-audiobook-installed-20260617-1602.upt
```

## Publish Commands

After release assets are staged under `firmware\releases\v1.5.1`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.1 `
  -Name "HiBy R1 Audiobook Mod v1.5.1" `
  -BodyFile firmware\releases\v1.5.1\README.md `
  -Assets "firmware\releases\v1.5.1\r1-audiobooks-1.6.16.1-audiobook.upt,firmware\releases\v1.5.1\MD5SUMS.txt,firmware\releases\v1.5.1\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.1 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.5.1\r1-audiobooks-1.6.16.1-audiobook.upt,firmware\releases\v1.5.1\MD5SUMS.txt,firmware\releases\v1.5.1\SHA256SUMS.txt"
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
