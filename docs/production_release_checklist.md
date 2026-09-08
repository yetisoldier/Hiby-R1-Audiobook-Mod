# Production Release Checklist

This file records the exact current public release inputs and verification
evidence. For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.29`
- About-screen label: `HiBy R1 2.0.29`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `main`
- Source release commit: the commit tagged `v2.0.29`
- Package: `r1-audiobooks-2.0.29.upt` (42,250,240 bytes)
- Package MD5: `cb536fd0b3d2677a87369e9f57e9bdd3`
- Package SHA256:
  `c3eb881f853715cb4c464c56db6f39d02198dd07071a7e667dece47fea7537bb`
- Build flags: `-IncludeAudiobookNativeApp -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.29
  -CustomVersionLabel "HiBy R1 2.0.29"`

## Changes Since v2.0.28

- Interposes the stock player's exact `echo mem > /sys/power/state` suspend
  command while Audiobooks owns the display.
- Converts the automatic timeout into the existing lightweight backlight-only
  blank so playback continues and the display remains wakeable.
- Leaves every other `system()` command untouched and preserves normal HiBy
  timeout/suspend behavior outside Audiobooks.
- Playback, resume, catalog isolation, chapters, bookmarks, UI, USB handling,
  and audio unlocks are unchanged from v2.0.28.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.29 `
  -OutputUpt work\audiobook-firmware-2.0.29\r1-audiobooks-2.0.29.upt `
  -IncludeAudiobookNativeApp `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.29 `
  -CustomVersionLabel "HiBy R1 2.0.29"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.29 `
  --upt-name r1-audiobooks-2.0.29.upt `
  --expected-version 2.0.29 `
  --expected-label "HiBy R1 2.0.29" `
  --expect-native-app `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The builder recompiles the preload hook from current source before packaging.
The strict verifier passed all 5,488 stock rootfs paths and modes, 482 symlink
targets, root ownership, launcher integration, wrapper and hook modes, the
catalog-cleanup and suspend-block markers, hardened USB scripts, absence of `S90adb`, firmware
markers, audio unlocks, OTA hash, and known-bad package rejection.

## Regression Verification

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
- The built MIPS hook exports the `system` interposer and contains the exact
  stock suspend command plus the lightweight-blank diagnostic marker.

## Device Verification

The screen-timeout fix was validated statically and through the full local
suite. An R1 was not connected during final packaging, so installed-device
timeout testing remains a useful post-release follow-up:

1. Flash v2.0.29 and leave the normal backlight timer enabled.
2. Start an audiobook and let the timer turn the screen off.
3. Confirm playback continues and one power press or a double-tap wakes it.
4. Repeat after pause/resume and over wired and Bluetooth output.
5. With ADB enabled, confirm `/tmp/.audiobook_hook.log` contains
   `stock suspend blocked; using lightweight blank`.

## Publish

Release assets:

- `work\audiobook-firmware-2.0.29\r1-audiobooks-2.0.29.upt`
- `firmware\releases\v2.0.29\MD5SUMS.txt`
- `firmware\releases\v2.0.29\SHA256SUMS.txt`
- `firmware\releases\v2.0.29\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.29 `
  -Name "HiBy R1 Audiobook Mod v2.0.29" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.29\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.29\r1-audiobooks-2.0.29.upt,firmware\releases\v2.0.29\MD5SUMS.txt,firmware\releases\v2.0.29\SHA256SUMS.txt,firmware\releases\v2.0.29\RELEASE_NOTES.md"
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
