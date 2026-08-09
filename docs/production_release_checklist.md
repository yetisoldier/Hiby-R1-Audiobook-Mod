# Production Release Checklist

This file records the exact current public release inputs and verification
evidence. For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.28`
- About-screen label: `HiBy R1 2.0.28`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `main`
- Source release commit: the commit tagged `v2.0.28`
- Package: `r1-audiobooks-2.0.28.upt` (42,250,240 bytes)
- Package MD5: `da13cf3a78823ce9e982bdc1a51f9cd3`
- Package SHA256:
  `fcce40b32fd1eddef5cd31412cae5565f434c13c2293dd3692648b8b39173431`
- Build flags: `-IncludeAudiobookNativeApp -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.28
  -CustomVersionLabel "HiBy R1 2.0.28"`

## Changes Since v2.0.27

- Added on-entry cleanup of `/Audiobooks` rows from HiBy's separate stock
  Music database copies.
- Reconciles Music search rows, named catalogs, format counts, total counts,
  and time indexes in a transaction.
- Cleanup is independent of whether Music Update Database or Audiobooks
  Refresh Library was run last.
- Uses a short-lived 256 KiB worker with bounded lock retries. No daemon,
  recurring allocation, idle polling, or persistent battery cost was added.
- Playback, resume, chapters, bookmarks, UI, display wake, USB handling, and
  audio unlocks are unchanged from v2.0.27.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.28 `
  -OutputUpt work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt `
  -IncludeAudiobookNativeApp `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.28 `
  -CustomVersionLabel "HiBy R1 2.0.28"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.28 `
  --upt-name r1-audiobooks-2.0.28.upt `
  --expected-version 2.0.28 `
  --expected-label "HiBy R1 2.0.28" `
  --expect-native-app `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The builder recompiles the preload hook from current source before packaging.
The strict verifier passed all 5,488 stock rootfs paths and modes, 482 symlink
targets, root ownership, launcher integration, wrapper and hook modes, the
catalog-cleanup marker, hardened USB scripts, absence of `S90adb`, firmware
markers, audio unlocks, OTA hash, and known-bad package rejection.

## Catalog Regression Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\test_music_catalog_cleanup.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_local_dev_sanity.ps1 -SkipDbFixtures
```

- Synthetic fixture: two audiobook rows removed, two Music rows unchanged.
- Captured fixture: 135 audiobook rows removed, 114 Music rows unchanged.
- Large captured fixture: 298 audiobook rows removed, 7,396 Music rows
  unchanged.
- Each run verified stock side-table counts, format/time indexes, SQLite
  integrity, shared music/audiobook metadata, and idempotence.
- The full local sanity suite passed.

## Device Verification

The catalog fix was validated against captured R1 databases and a strict
production image build. An R1 was not connected during final packaging, so
installed-device verification remains a useful post-release follow-up:

1. Flash v2.0.28 and run Music -> Update Database.
2. Open Audiobooks once and wait briefly for the background cleanup.
3. Confirm Titles still opens and books play normally.
4. Return to Music and confirm `/Audiobooks` files are absent from All Songs,
   Albums, Genres, and Search.
5. Check `/tmp/.audiobook_hook.log` for a successful `[catalog]` line.

## Publish

Release assets:

- `work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt`
- `firmware\releases\v2.0.28\MD5SUMS.txt`
- `firmware\releases\v2.0.28\SHA256SUMS.txt`
- `firmware\releases\v2.0.28\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.28 `
  -Name "HiBy R1 Audiobook Mod v2.0.28" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.28\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt,firmware\releases\v2.0.28\MD5SUMS.txt,firmware\releases\v2.0.28\SHA256SUMS.txt,firmware\releases\v2.0.28\RELEASE_NOTES.md"
```

Run the same command with `-VerifyOnly` after publication and confirm all four
assets through the GitHub release API.

## Known Limitations

- Audiobook playback stops when leaving the app for the HiBy launcher.
- No audiobook search UI; browse Titles, Authors, Series, or Folders.
- ADB, USB mass storage, and USB DAC are mutually exclusive. Persistent ADB is
  not included in the public build.
- UTF-8/Cyrillic text support from v2.0.20 is not included in this stability
  line.
- This is unofficial firmware tested on one normal R1. Keep stock 1.6 firmware
  available for recovery.
