# Production Release Checklist

This file records the exact public release inputs and verification evidence.
For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.26`
- About-screen label: `HiBy R1 2.0.26`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `main`
- Source release commit: the commit tagged `v2.0.26`
- Package: `r1-audiobooks-2.0.26.upt` (42,242,048 bytes)
- Package MD5: `3d7d87aff3a098b70e68825e577c32c5`
- Package SHA256:
  `a9c9fec73e2be9fdc664088a47ae6834378e7e6c112fe29fdc153b404005d938`
- Hook MD5: `0fc5b293962fc6b6de7fecb604475cc2`
- Hook SHA256:
  `318d82b1f3d7a7b48b7414a3a4b8f698fbcd18c39bd9836657a35678670014c6`
- Build flags: `-IncludeAudiobookNativeApp -EnableBootAdb -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.26
  -CustomVersionLabel "HiBy R1 2.0.26"`

## Changes Since v2.0.25

- Intercepted stock hard framebuffer blanks while Audiobooks owns the display
  and converted them to the app's lightweight backlight-only blank.
- Added `FBIOPAN_DISPLAY = EBUSY` recovery for a missed hard blank, restoring
  reliable one-press power and double-tap wake.
- Made media and volume keys explicitly unblank the framebuffer before their
  normal actions.
- Changed NativeApp packaging to rebuild the hook from current source on every
  firmware build, preventing a valid package from silently carrying stale code.
- Added a reproducible MIPS framebuffer blank test helper and documentation.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.26 `
  -OutputUpt work\audiobook-firmware-2.0.26\r1-audiobooks-2.0.26.upt `
  -IncludeAudiobookNativeApp `
  -EnableBootAdb `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.26 `
  -CustomVersionLabel "HiBy R1 2.0.26"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.26 `
  --upt-name r1-audiobooks-2.0.26.upt `
  --expected-version 2.0.26 `
  --expected-label "HiBy R1 2.0.26" `
  --expect-native-app `
  --require-boot-adb `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The firmware builder recompiles the preload hook from current source before
packaging and fails if that build does not succeed.

The verifier passed all NativeApp checks, including all 5,488 stock rootfs
paths and modes, 482 symlink targets, root ownership, launcher hook/cave,
wrapper, app and hook modes, marker flags, stock theme-aware launcher resources,
OTA rootfs hash, and known-bad package rejection.

## Device Verification

Production `2.0.26` was flashed on 2026-07-27 through the data-preserving stock
recovery/update path. Verified after boot:

- `/etc/r1_audiobook_version` reports `version=2.0.26` and all expected flags.
- ADB was available after the update reboot with USB working mode set to
  Device.
- Installed hook MD5 and SHA256 match the production build.
- The NativeApp installed-release verifier passed, including wrapper/hook
  presence, running host process, framebuffer capture, and SD library integrity
  (`52` books and `298` tracks).
- Ten forced hard framebuffer blanks each converted to lightweight blank and
  woke with one power press.
- Double-tap, volume-key, and play/pause-key wake behavior passed.
- The player retained the same PID, 30 threads, and 37 open descriptors; RSS
  changed from 17,236 KB to 17,292 KB during the production test.
- Audiobook exit restored the launcher, and stock Music opened and returned
  normally.

## Publish

Release assets:

- `work\audiobook-firmware-2.0.26\r1-audiobooks-2.0.26.upt`
- `firmware\releases\v2.0.26\MD5SUMS.txt`
- `firmware\releases\v2.0.26\SHA256SUMS.txt`
- `firmware\releases\v2.0.26\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.26 `
  -Name "HiBy R1 Audiobook Mod v2.0.26" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.26\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.26\r1-audiobooks-2.0.26.upt,firmware\releases\v2.0.26\MD5SUMS.txt,firmware\releases\v2.0.26\SHA256SUMS.txt,firmware\releases\v2.0.26\RELEASE_NOTES.md"
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
