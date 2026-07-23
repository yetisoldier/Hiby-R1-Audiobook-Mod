# Production Release Checklist

This file records the exact public release inputs and verification evidence.
For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.23`
- About-screen label: `HiBy R1 2.0.23`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `codex/sd-runtime-stability`
- Source fix commit: `692e0ab`
- Package: `r1-audiobooks-2.0.23.upt` (42,217,472 bytes)
- Package MD5: `11ddcf7e8d93eefc1038662d4d324830`
- Package SHA256:
  `c366a2b5a78a7943b20fab619a8e20d26c61d17f374dab66e34436f99f40f653`
- Hook MD5: `df2f44e082d2fdf6784404c58f9d23c7`
- Hook SHA256:
  `c9f5a517494c88ca4bb203f28b4a8c9e4a411a43b86f13c4b51d2cc2712d2a28`
- Build flags: `-IncludeAudiobookNativeApp -IncludeAudiobookLauncherIcon
  -EnableBootAdb -UnlockNativeDsd -EnableBluetoothSbcXq -UnlockUsbDacMode
  -CustomVersionId 2.0.23 -CustomVersionLabel "HiBy R1 2.0.23"`

## Changes Since v2.0.22

- Added an app-scoped runtime-power hold for the removable SD platform, host,
  and card while Audiobooks is open.
- Restores the exact prior power-control values on app exit, preserving stock
  suspend behavior outside Audiobooks.
- Added diagnostic-only checks for a missing `mmcqd/1` worker or repeated
  non-active SD runtime state.
- Changed authoritative position checkpoints from 5 to 15 seconds.
- Limited SQLite progress mirrors to once per 60 seconds.
- Kept immediate exact saves on pause, stop, completion, and app exit.
- Coalesced identical saves to reduce exFAT metadata churn.
- Replaced README screenshots with images captured from installed v2.0.23.

The change targets an observed stock-kernel MMC runtime-resume Oops that killed
`mmcqd/1` and left `hiby_player` blocked in SD writeback. Available RAM remained
near 18 MB and no OOM event occurred.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_hook.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.23 `
  -OutputUpt work\audiobook-firmware-2.0.23\r1-audiobooks-2.0.23.upt `
  -IncludeAudiobookNativeApp `
  -IncludeAudiobookLauncherIcon `
  -EnableBootAdb `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.23 `
  -CustomVersionLabel "HiBy R1 2.0.23"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.23 `
  --upt-name r1-audiobooks-2.0.23.upt `
  --expected-version 2.0.23 `
  --expected-label "HiBy R1 2.0.23" `
  --expect-native-app `
  --require-boot-adb `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The verifier passed all NativeApp checks, including all 5,488 stock rootfs
paths and modes, 482 symlink targets, root ownership, launcher hook/cave,
wrapper, app and hook modes, marker flags, launcher icons, OTA rootfs hash, and
known-bad package rejection.

## Device Verification

Production `2.0.23` was flashed on 2026-07-23 through the stock
System -> Firmware update -> Via SD-card flow. Verified after boot:

- `/etc/r1_audiobook_version` reports `version=2.0.23` and all expected flags.
- Persistent ADB returned automatically after the update reboot.
- Installed hook MD5 and SHA256 match the production build.
- Outside Audiobooks, all three SD controls are `auto` and the card suspends.
- Inside Audiobooks, all three controls are `on`, card state is `active`, and
  `mmcqd/1` remains present.
- Resume sidecars advanced on the 15-second cadence; the DB mirror advanced on
  the 60-second cadence.
- Pause and app exit updated both stores immediately.
- Beyond Exile reopened at the exact saved `3:21:29` position and played.
- Refresh Library completed with all 52 books visible.
- Available memory remained approximately 18 MB.
- Post-flash `dmesg` contained no Oops, allocation failure, I/O error, or
  blocked-task warning.

## Publish

Release assets:

- `work\audiobook-firmware-2.0.23\r1-audiobooks-2.0.23.upt`
- `firmware\releases\v2.0.23\MD5SUMS.txt`
- `firmware\releases\v2.0.23\SHA256SUMS.txt`
- `firmware\releases\v2.0.23\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.23 `
  -Name "HiBy R1 Audiobook Mod v2.0.23" `
  -TargetCommitish codex/sd-runtime-stability `
  -BodyFile firmware\releases\v2.0.23\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.23\r1-audiobooks-2.0.23.upt,firmware\releases\v2.0.23\MD5SUMS.txt,firmware\releases\v2.0.23\SHA256SUMS.txt,firmware\releases\v2.0.23\RELEASE_NOTES.md"
```

Run the same command with `-VerifyOnly` after publication and confirm all four
asset names and sizes through the GitHub release API.

## Known Limitations

- Audiobook playback stops when leaving the app for the HiBy launcher.
- No audiobook search UI; browse Titles, Authors, Series, or Folders.
- ADB and USB DAC are mutually exclusive by USB working mode.
- UTF-8/Cyrillic text support from v2.0.20 is not included in this stability
  line.
- The original SD freeze was intermittent; broad long-duration testing across
  different SD cards remains valuable.
- This is unofficial firmware tested on one normal R1. Keep stock 1.6 firmware
  available for recovery.
