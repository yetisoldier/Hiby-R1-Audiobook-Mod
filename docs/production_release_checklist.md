# Production Release Checklist

This file records the exact public release inputs and verification evidence.
For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.24`
- About-screen label: `HiBy R1 2.0.24`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `codex/sd-runtime-stability`
- Source release commit: the commit tagged `v2.0.24`
- Package: `r1-audiobooks-2.0.24.upt` (42,237,952 bytes)
- Package MD5: `17b56c5ff1a3b0dbf59073d24a23dc7a`
- Package SHA256:
  `377217abbefbb073cfbf9d85847a8c90717a59145134100713959883385f51ce`
- Hook MD5: `75bc7449545458916acbc2f2fdc76678`
- Hook SHA256:
  `e8a1af61b6bd6b57d597bd4d88959ed5fcf982367482435bae0c203d4768a1b1`
- Build flags: `-IncludeAudiobookNativeApp -EnableBootAdb -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.24
  -CustomVersionLabel "HiBy R1 2.0.24"`

## Changes Since v2.0.23

- Added MP3 `COMM` and M4B `desc`/`ldes` publisher-summary extraction.
- Added a separate `book_metadata` table and full-width detail descriptions.
- Bottom-anchored detail progress and controls now share drawing/hitbox
  constants; corrected TrueType wrapping uses the full available width.
- Restored stock theme-aware Books icon assets while retaining the Audiobooks
  label and callback.
- Preserved/restored the launcher framebuffer and added a bounded redraw
  handoff monitor, fixing the 5-10 second black/frozen-looking app exit.
- Added an active-`yoffset` framebuffer capture helper for trustworthy ADB
  screenshots and refreshed README images from the installed test build.
- Documented Ingenic GCC/glibc and Rust/Slint/Nanowave research.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_hook.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.24 `
  -OutputUpt work\audiobook-firmware-2.0.24\r1-audiobooks-2.0.24.upt `
  -IncludeAudiobookNativeApp `
  -EnableBootAdb `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.24 `
  -CustomVersionLabel "HiBy R1 2.0.24"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.24 `
  --upt-name r1-audiobooks-2.0.24.upt `
  --expected-version 2.0.24 `
  --expected-label "HiBy R1 2.0.24" `
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

Production `2.0.24` was flashed on 2026-07-23 through the stock
System -> Firmware update -> Via SD-card flow. Verified after boot:

- `/etc/r1_audiobook_version` reports `version=2.0.24` and all expected flags.
- ADB was available after the update reboot with USB working mode set to
  Device.
- Installed hook MD5 and SHA256 match the production build.
- Refresh Library retained all 52 books and populated the new description
  metadata.
- Beyond Exile showed its full-width publisher description with progress and
  controls anchored correctly.
- Idle exit returned in about 590 ms; active-playback exit returned in about
  760 ms.
- The first post-exit launcher tap appeared immediately without a second touch
  or power-button cycle.
- The framebuffer handoff worker returned to the baseline 29 process threads
  and released its temporary snapshot.
- Available memory remained approximately 16-18 MB.

## Publish

Release assets:

- `work\audiobook-firmware-2.0.24\r1-audiobooks-2.0.24.upt`
- `firmware\releases\v2.0.24\MD5SUMS.txt`
- `firmware\releases\v2.0.24\SHA256SUMS.txt`
- `firmware\releases\v2.0.24\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.24 `
  -Name "HiBy R1 Audiobook Mod v2.0.24" `
  -TargetCommitish codex/sd-runtime-stability `
  -BodyFile firmware\releases\v2.0.24\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.24\r1-audiobooks-2.0.24.upt,firmware\releases\v2.0.24\MD5SUMS.txt,firmware\releases\v2.0.24\SHA256SUMS.txt,firmware\releases\v2.0.24\RELEASE_NOTES.md"
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
