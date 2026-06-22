# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Public Release

- GitHub release: `v1.5.4`
- Firmware marker: `1.6.16.5-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.16.5-audiobook\r1-audiobooks-1.6.16.5-audiobook.upt`
- Package MD5: `f6a0e65af41c7990f03e342fef995bad`
- Package SHA256: `efd77a5a6f83879e76089ace072657891ff2e5475c4f0e82d812f728ad4e2816`
- Rootfs MD5: `1797f124a92177605e776615144f323a`
- Rootfs SHA256: `cf2076de6c700abd24d66dc587ac3109786829e5f589f4068e61988b0a481325`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since 1.6.16.4

- Hotfix for SD-card swaps where Music -> Update Database updates the SD-root
  media DB but the internal active DB still contains rows from the previous SD
  card.
- The DB watcher now tracks SD-root DB signature changes in addition to the
  internal DB.
- If the SD-root DB is clean/current and the internal DB needs repair, the
  watcher promotes the SD DB to primary, runs maintenance, and mirrors the
  repaired DB back to active DB locations.

## Local Verification

Local package verification passed on 2026-06-22:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.16.5-audiobook `
  --upt-name r1-audiobooks-1.6.16.5-audiobook.upt `
  --expected-version 1.6.16.5-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.16.5" `
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

The public `1.6.16.5` hotfix package was flashed on the test R1 on 2026-06-22.
Installed verification passed under
`work\installed-release-verification\20260622-093147`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16.5-audiobook `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.5-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` is allowed during staged verification; remove or rename it
  after manual install.
- `/usr/data` free space is about 31 MB after cleanup.
- DB integrity is `ok`.
- Audiobooks contains 135 media rows across 6 books on the swapped-card test SD.
- SD-root `/usr/data/mnt/sd_0/usrlocal_media.db` has integrity `ok` and 135
  audiobook rows.
- Forced live SD-swap regression test: replacing the internal DB with an old-card
  copy containing 298 audiobook rows while the SD-root DB correctly contained
  the current card's 135 audiobook rows triggered `primary-copy reason=boot`,
  rebuilt 135 audiobook rows, and mirrored the fixed DB.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.
- Audiobook detection is based on `/Audiobooks` folder location, not an exact
  genre tag.

## Publish Commands

After release assets are staged under `firmware\releases\v1.5.4`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.4 `
  -Name "HiBy R1 Audiobook Mod v1.5.4" `
  -BodyFile firmware\releases\v1.5.4\README.md `
  -Assets "firmware\releases\v1.5.4\r1-audiobooks-1.6.16.5-audiobook.upt,firmware\releases\v1.5.4\MD5SUMS.txt,firmware\releases\v1.5.4\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.5.4 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.5.4\r1-audiobooks-1.6.16.5-audiobook.upt,firmware\releases\v1.5.4\MD5SUMS.txt,firmware\releases\v1.5.4\SHA256SUMS.txt"
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
