# Production Release Checklist

This file records the exact public release inputs and verification evidence.
For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.25`
- About-screen label: `HiBy R1 2.0.25`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `main`
- Source release commit: the commit tagged `v2.0.25`
- Package: `r1-audiobooks-2.0.25.upt` (42,242,048 bytes)
- Package MD5: `58c186e02f7f68167cdc2e86cb6f4333`
- Package SHA256:
  `844bbe648832ac48031ad908815c11259042308b115369650bf3e3638e3d27b2`
- Hook MD5: `f78d1110924c78a3898959a418179418`
- Hook SHA256:
  `e81d6d29548b76163f9b8f766a07d420aa01b6656ad118c3e6340553e3041231`
- Build flags: `-IncludeAudiobookNativeApp -EnableBootAdb -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.25
  -CustomVersionLabel "HiBy R1 2.0.25"`

## Changes Since v2.0.24

- Removed repeated full-catalog work from folder taps and added an indexed
  subtree lookup. Nested folder screens rebuild in 1-2 ms on the test R1.
- Added bounded streaming ID3v2.3/v2.4 `CHAP`/`CTOC` parsing for embedded MP3
  chapters, including ordered CTOC children and nested TIT2 titles.
- Preserved the existing one-file-per-chapter fallback for multipart MP3 books.
- Increased the Now Playing cover from 220 to 270 pixels and moved the title,
  author, duration, and progress section down without moving playback controls.
- Added generated MP3 chapter fixtures to the full local sanity suite.
- Updated ADB screenshots to compile/deploy the native helper automatically and
  capture the framebuffer's active `yoffset` page.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_hook.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.25 `
  -OutputUpt work\audiobook-firmware-2.0.25\r1-audiobooks-2.0.25.upt `
  -IncludeAudiobookNativeApp `
  -EnableBootAdb `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.25 `
  -CustomVersionLabel "HiBy R1 2.0.25"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.25 `
  --upt-name r1-audiobooks-2.0.25.upt `
  --expected-version 2.0.25 `
  --expected-label "HiBy R1 2.0.25" `
  --expect-native-app `
  --require-boot-adb `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The verifier passed all NativeApp checks, including all 5,488 stock rootfs
paths and modes, 482 symlink targets, root ownership, launcher hook/cave,
wrapper, app and hook modes, marker flags, stock theme-aware launcher resources,
OTA rootfs hash, and known-bad package rejection.

## Device Verification

Production `2.0.25` was flashed on 2026-07-24 through the stock
System -> Firmware update -> Via SD-card flow. Verified after boot:

- `/etc/r1_audiobook_version` reports `version=2.0.25` and all expected flags.
- ADB was available after the update reboot with USB working mode set to
  Device.
- Installed hook MD5 and SHA256 match the production build.
- Refresh Library retained 52 books, 298 tracks, and cached 1,150 chapters.
- Six single-file MP3 books exposed embedded chapters; a chapter tap sought
  directly to its saved timestamp.
- All tested nested folder pages rebuilt in 1-2 ms with normal back behavior.
- The longest title in the test library wrapped to two lines without overlap;
  progress scrubbing and control buttons remained aligned.
- Repeated enter/exit cycles plateaued at about 17 MB RSS with no per-entry
  memory or file-descriptor growth.
- A normal reboot restored boot ADB and direct audiobook resume.

## Publish

Release assets:

- `work\audiobook-firmware-2.0.25\r1-audiobooks-2.0.25.upt`
- `firmware\releases\v2.0.25\MD5SUMS.txt`
- `firmware\releases\v2.0.25\SHA256SUMS.txt`
- `firmware\releases\v2.0.25\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.25 `
  -Name "HiBy R1 Audiobook Mod v2.0.25" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.25\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.25\r1-audiobooks-2.0.25.upt,firmware\releases\v2.0.25\MD5SUMS.txt,firmware\releases\v2.0.25\SHA256SUMS.txt,firmware\releases\v2.0.25\RELEASE_NOTES.md"
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
