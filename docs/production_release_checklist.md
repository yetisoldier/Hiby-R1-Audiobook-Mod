# Production Release Checklist

This file records the exact current public release inputs and verification
evidence. For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.30`
- About-screen label: `HiBy R1 2.0.30`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `main`
- Source release commit: the commit tagged `v2.0.30`
- Package: `r1-audiobooks-2.0.30.upt` (42,237,952 bytes)
- Package MD5: `db30162a2caf961f24c79618706d8b6d`
- Package SHA256:
  `d7814c39f149592584e34b91b1681767fc30f02d25eaa984544691a2c11a89b8`
- Build flags: `-IncludeAudiobookNativeApp -IncludeAudiobookLauncherIcon
  -UnlockNativeDsd -EnableBluetoothSbcXq -UnlockUsbDacMode
  -CustomVersionId 2.0.30 -CustomVersionLabel "HiBy R1 2.0.30"`
- Boot ADB: disabled in the public image.

## Changes Since v2.0.29

- Avoids the R1 AIC driver's unstable one-channel DMA path by duplicating mono
  PCM to stereo only at the wired sink. Bluetooth retains source channels.
- Adds a bounded full-period wired PCM queue and quiesces its owner before any
  drop, prepare, or close operation.
- Reuses wired PCM across compatible MP3 parts, preloads the next decoder and
  first block, and applies a 250 ms settle only when a real DAC restart occurs.
- Makes MP3 byte seeks start after ID3/APIC metadata and reopens only the
  lightweight decoder for same-part seeks.
- Moves periodic progress writes to a low-priority latest-wins worker. The
  15-second checkpoint is an internal, SD-CID-keyed cache; pause, stop,
  completion, and exit flush the SD sidecar and DB mirror.
- Corrects physical Next and Previous to Linux codes 163 and 165, providing
  reliable +60-second and -30-second skips.
- Reconciles only Music catalog groups affected by removed `/Audiobooks` rows
  and runs cleanup at lower priority, preserving unrelated stock metadata and
  ordering.
- Hardens release staging and installed-file verification for public images
  without boot ADB and packaged LF-normalized shell scripts.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.30 `
  -OutputUpt work\audiobook-firmware-2.0.30\r1-audiobooks-2.0.30.upt `
  -IncludeAudiobookNativeApp `
  -IncludeAudiobookLauncherIcon `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.30 `
  -CustomVersionLabel "HiBy R1 2.0.30"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.30 `
  --upt-name r1-audiobooks-2.0.30.upt `
  --expected-version 2.0.30 `
  --expected-label "HiBy R1 2.0.30" `
  --expect-native-app `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The strict package verifier passed all 5,488 stock rootfs paths and modes, 482
symlink targets, root ownership, launcher integration, native-app files,
catalog behavior, hardened USB scripts, absence of `S90adb`, firmware markers,
audio unlocks, OTA hashes, and known-bad-package rejection.

## Regression Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\test_music_catalog_cleanup.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_local_dev_sanity.ps1 -SkipDbFixtures
```

- Small and 6,002-Music-row synthetic fixtures passed.
- Both captured R1 databases passed, including the 7,694-row database.
- On-device cleanup removed 298 audiobook rows in about 1.3 seconds and left
  all 7,396 Music rows unchanged. The old whole-table rebuild took more than
  six minutes on the same data.
- Tests verify stock Music rows, flags, ordering, counts, SQLite integrity,
  shared metadata, affected-group reconciliation, and idempotence.
- The complete local sanity suite and strict package verifier passed.

## Device Verification

RC21 was installed and passed the strict installed-release verifier before the
identical production source was packaged as v2.0.30.

- A mono multipart MP3 resumed near the end of part 1 and directly sought to
  the expected byte after ID3/APIC metadata.
- Natural playback crossed into part 2 through the preloaded decoder.
- Physical Next moved +60 seconds into part 2; Previous moved -30 seconds.
- Additional same-part Next/Previous actions reopened only the MP3 decoder and
  did not report false EOF or skip a part.
- Physical Play/Pause and volume up/down passed. The original volume was
  restored to 83 percent after testing.
- The final playback logs recorded zero AIC DMA-address faults.
- `hiby_player` was about 18.7 MB RSS with about 14.6 MB available, then exited
  cleanly to the launcher.
- The listener's original progress sidecar and library database were restored
  byte-for-byte after destructive boundary testing.

After the production package is installed, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 2.0.30 `
  -ExpectNativeApp `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -ExpectHardenedUsbGadgetScripts `
  -ExpectBootAdbDisabled `
  -AllowStagedFirmware
```

## Publish

Release assets:

- `work\audiobook-firmware-2.0.30\r1-audiobooks-2.0.30.upt`
- `firmware\releases\v2.0.30\MD5SUMS.txt`
- `firmware\releases\v2.0.30\SHA256SUMS.txt`
- `firmware\releases\v2.0.30\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.30 `
  -Name "HiBy R1 Audiobook Mod v2.0.30" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.30\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.30\r1-audiobooks-2.0.30.upt,firmware\releases\v2.0.30\MD5SUMS.txt,firmware\releases\v2.0.30\SHA256SUMS.txt,firmware\releases\v2.0.30\RELEASE_NOTES.md"
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
