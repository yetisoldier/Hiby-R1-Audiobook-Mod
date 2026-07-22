# Production Release Checklist

This file records the exact public release inputs and verification evidence.
For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.22`
- About-screen label: `HiBy R1 2.0.22`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `codex/r1-hiby-modding-integration`
- Package: `r1-audiobooks-2.0.22.upt` (42,213,376 bytes)
- Package MD5: `d5dfdf3e0977d9339ab0ae862f4b3bf5`
- Package SHA256:
  `28dd05c76b203ea29298a7a59eafc036431e1c5b18760455913e751048f7f141`
- Hook SHA256:
  `fc68198a25f58888ef7102aabcf3cdf41b6bf839c9d46326986a3091401054e8`
- Build flags: `-IncludeAudiobookNativeApp -IncludeAudiobookLauncherIcon
  -EnableBootAdb -UnlockNativeDsd -EnableBluetoothSbcXq -UnlockUsbDacMode
  -CustomVersionId 2.0.22 -CustomVersionLabel "HiBy R1 2.0.22"`

The hook hash is identical to the user-tested `2.0.22-stability-rc11` hook.
Only the public version marker and About-screen label changed for the production
package.

## Changes Since v2.0.20

- Ordered player command queue and coherent state snapshot.
- Reliable physical keys after screen blanking; play/pause toggle stays ordered.
- Fine volume steps, hold-to-ramp, and visible volume percentage feedback.
- Separate/retried wired and Bluetooth mixer state; no pause/resume volume jump.
- 2.0x WSOLA playback speed.
- Real folder-hierarchy drill-down.
- Nonblocking Refresh Library worker with progress/success/failure feedback.
- Serialized catalog writes and nonblocking SD-primary progress saves.
- Transactional scans and corrupt-database quarantine.
- SD media-loss guard preserves progress and avoids false completion.
- Multipart chapter labels lead with `Part N` and wrap long titles.
- NativeApp-aware firmware verifier and safer staging checks.

The experimental UTF-8/Cyrillic side branch used by public v2.0.20 is not
included. Users who rely on Cyrillic names or tags should remain on v2.0.20.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_hook.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.22 `
  -OutputUpt work\audiobook-firmware-2.0.22\r1-audiobooks-2.0.22.upt `
  -IncludeAudiobookNativeApp `
  -IncludeAudiobookLauncherIcon `
  -EnableBootAdb `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.22 `
  -CustomVersionLabel "HiBy R1 2.0.22"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.22 `
  --upt-name r1-audiobooks-2.0.22.upt `
  --expected-version 2.0.22 `
  --expected-label "HiBy R1 2.0.22" `
  --expect-native-app `
  --require-boot-adb `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The verifier passed all NativeApp checks: stock path/mode/symlink preservation,
root ownership, launcher hook/cave, wrapper, app and hook modes, marker flags,
launcher icons, OTA rootfs hash, and known-bad package hash rejection.

## Device Verification

Production `2.0.22` was flashed on 2026-07-22. Verified after boot:

- `/etc/r1_audiobook_version` reports `version=2.0.22` and all expected flags.
- On-device hook SHA256 matches the production build and tested RC11.
- Persistent ADB returned after recovery flash without manual re-enable.
- Audiobooks tile opened the app and displayed all 52 books in the test catalog.
- Available memory was about 18 MB with the audiobook app open.
- No restart loop, black screen, out-of-memory kill, or kernel fault appeared.

RC11, which has the identical hook binary, passed the hands-on listening matrix:

- Wired and Bluetooth audiobook playback.
- Bluetooth pause/resume with clear audio and stable volume.
- Rapid play/pause input and held Volume Up/Down ramp.
- All playback speeds through 2.0x.
- Direct multipart resume with the 5-second rewind.
- Refresh Library during playback with no audible or UI stall.
- Repeated refresh with stable task count and available memory.
- Clean exit/reopen and resume after reboot.

## Publish

Release assets:

- `firmware\releases\v2.0.22\r1-audiobooks-2.0.22.upt`
- `firmware\releases\v2.0.22\MD5SUMS.txt`
- `firmware\releases\v2.0.22\SHA256SUMS.txt`
- `firmware\releases\v2.0.22\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.22 `
  -Name "HiBy R1 Audiobook Mod v2.0.22" `
  -TargetCommitish codex/r1-hiby-modding-integration `
  -BodyFile firmware\releases\v2.0.22\RELEASE_NOTES.md `
  -Assets "firmware\releases\v2.0.22\r1-audiobooks-2.0.22.upt,firmware\releases\v2.0.22\MD5SUMS.txt,firmware\releases\v2.0.22\SHA256SUMS.txt,firmware\releases\v2.0.22\RELEASE_NOTES.md"
```

Run the same command with `-VerifyOnly` after publication and confirm all four
asset names and sizes through the GitHub release API.

## Known Limitations

- Audiobook playback stops when leaving the app for the HiBy launcher.
- No audiobook search UI; browse Titles, Authors, Series, or Folders.
- ADB and USB DAC are mutually exclusive by USB working mode.
- UTF-8/Cyrillic text support from v2.0.20 is not included in this stability
  line.
- This is unofficial firmware tested on one normal R1. Keep stock 1.6 firmware
  available for recovery.
