# Build, Flash, Verify, And Release Runbook

This runbook documents the repeatable development workflow for the HiBy R1
audiobook firmware. It is written for modders who want to build or modify the
firmware, not for normal end users.

## Current Release Reference

- Public release: `v1.6.1`
- Firmware marker: `1.6.18-audiobook`
- Package: `r1-audiobooks-1.6.18-audiobook.upt`
- Base firmware: stock HiBy R1 1.6 for the normal R1
- Target device: normal HiBy R1 only, not R1 MIDI

## Prerequisites

On the development PC:

- Windows PowerShell
- Python
- Git
- ADB or repo-local `.tools\platform-tools\adb.exe`
- WSL Ubuntu 24.04 for shell syntax and QEMU/user-mode helper tests
- Stock HiBy R1 1.6 package saved as `stock\r1.upt`
- A known-good stock `r1.upt` kept separately for recovery

The build scripts download or use local dependencies under `.deps` where
possible. Do not commit `.deps`, extracted work trees, ADB captures, or device
state dumps.

## 1. Extract Stock Firmware

Start from the official stock R1 1.6 package:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\extract_r1_firmware.ps1
```

Expected reconstructed files:

```text
work\original\xImage
work\original\rootfs.squashfs
```

The R1 `.upt` is an ISO-style OTA image with `ota_config.in` and chunked files
under `ota_v0`. The extraction script reconstructs the kernel and rootfs.

## 2. Build Native Helpers

The DB helper is a static MIPS binary built from C plus SQLite:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_db_maint_helper.ps1
```

If resume helper binaries were changed, rebuild them too:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_memscan_helper.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_direct_open_helper.ps1
```

The release verifier checks that the expected helper strings/features are
present before a firmware package can be trusted.

## 3. Build The Audiobook Firmware

The current (v2.0.x) release-style build uses the NativeApp pivot
(`-IncludeAudiobookNativeApp`), boot ADB, and the three optional audio unlocks
that the pre-2.0 line carried and v2.0.17 restores:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.17 `
  -OutputUpt work\audiobook-firmware-2.0.17\r1-audiobooks-2.0.17.upt `
  -IncludeAudiobookNativeApp `
  -EnableBootAdb `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.17 `
  -CustomVersionLabel "HiBy R1 2.0.17"
```

The NativeApp pivot is mutually exclusive with the legacy resume-daemon switches
(`-IncludeAudiobookResumeRuntime`, `-IncludeAudiobookDbMaintenance`,
`-IncludeAudiobookNativeHubLauncher`, `-IncludeAudiobookNativeHubViewRows`, etc.)
- use `-IncludeAudiobookNativeApp` alone for those. The three audio unlocks
(`-UnlockNativeDsd`, `-EnableBluetoothSbcXq`, `-UnlockUsbDacMode`) are
independent of that guard and combine cleanly with the pivot.

Build switches should stay explicit for release candidates. That makes it clear
which risky features are included.

## 4. Run Local Sanity Checks

Run the broad local test suite:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_local_dev_sanity.ps1
```

This checks:

- PowerShell parser health.
- Shell script syntax through WSL.
- Python compile.
- Resume daemon logic.
- DB watcher logic.
- Windows DB helper fixture.
- QEMU/user-mode MIPS DB helper fixture.
- Git whitespace.

Run the release package verifier:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.18-audiobook `
  --upt-name r1-audiobooks-1.6.18-audiobook.upt `
  --expected-version 1.6.18-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.18" `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-native-hub-launcher `
  --expect-native-hub-view-rows `
  --expect-current-hashes
```

The verifier is intentionally strict. It checks exact patch bytes, rootfs modes,
hashes, scripts, resources, feature markers, and known-bad package hashes.

## 5. Stage Firmware On The R1

Enable ADB on the R1, connect it, then stage the verified package:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_stage_verified_firmware.ps1 `
  -Package firmware\releases\v1.6.1\r1-audiobooks-1.6.18-audiobook.upt `
  -BuildOutDir work\audiobook-firmware-1.6.18-audiobook `
  -ExpectCurrentHashes `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -ExpectNativeHubLauncher `
  -ExpectNativeHubViewRows `
  -IUnderstandThisStagesFirmware
```

The staging script:

- Runs local firmware verification first.
- Refuses known bad package hashes.
- Pushes the package to `/usr/data/mnt/sd_0/r1.upt`.
- Backs up an existing different `r1.upt`.
- Verifies remote byte count and hashes.

## 6. Flash On The Device

Use the normal R1 firmware update UI. After the update finishes and the R1
reboots, manually re-enable ADB if further verification is needed.

For normal users, remove or rename SD-root `r1.upt` after a successful flash.
For test verification, the installed verifier can allow it to remain present.

## 7. Verify The Installed Firmware

After flashing and re-enabling ADB:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.18-audiobook `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

The installed verifier checks:

- `/etc/r1_audiobook_version`
- `/usr/resource/config.json`
- audio unlock markers
- resume daemon process
- DB watcher process
- DB helper files and feature strings
- staged firmware hygiene
- `user.ini` saved-last audiobook cleanup
- free space
- media DB integrity
- audiobook row counts
- Music search/album leakage
- generated catalogs
- framebuffer capture

The production `1.6.18-audiobook` package passed this installed verification on
2026-06-22 with artifacts under:

```text
work\installed-release-verification\20260622-150507
```

## 8. Live Smoke Testing

When the device is on the main launcher:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_live_audiobook_smoke.ps1 `
  -ResetBacks 4
```

Use `tools\r1_adb_control.py` for lower-level operations:

```powershell
python tools\r1_adb_control.py devices
python tools\r1_adb_control.py screenshot --classify --label before-test
python tools\r1_adb_control.py preset main-audiobooks --after-screenshot
```

RAM-only route experiments should use the `adb_test_*` helpers and should be
restored or rebooted after each experiment.

## 9. Publish A Public Release

Prepare release assets:

```text
firmware\releases\vX.Y.Z\
  README.md
  MD5SUMS.txt
  SHA256SUMS.txt
  r1-audiobooks-...upt
```

Update non-README release docs:

```text
CHANGELOG.md
docs\production_release_checklist.md
docs\release_recovery_notes.md
docs\github_release_process.md
```

Commit and push `main`, then tag:

```powershell
git tag -a vX.Y.Z -m "HiBy R1 Audiobook Mod vX.Y.Z"
git push origin HEAD:main
git push origin vX.Y.Z
```

Publish the GitHub Release through the checked-in REST helper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag vX.Y.Z `
  -Name "HiBy R1 Audiobook Mod vX.Y.Z" `
  -BodyFile firmware\releases\vX.Y.Z\README.md `
  -Assets "firmware\releases\vX.Y.Z\r1-audiobooks-...upt,firmware\releases\vX.Y.Z\MD5SUMS.txt,firmware\releases\vX.Y.Z\SHA256SUMS.txt"
```

Always verify the release API object and assets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag vX.Y.Z `
  -VerifyOnly `
  -Assets "firmware\releases\vX.Y.Z\r1-audiobooks-...upt,firmware\releases\vX.Y.Z\MD5SUMS.txt,firmware\releases\vX.Y.Z\SHA256SUMS.txt"
```

A pushed tag is not enough. The GitHub Release object must exist and list the
download assets.

## Recovery And Rollback

If a custom build fails:

1. Put official stock HiBy R1 1.6 firmware on the SD card as `r1.upt`.
2. Use the normal R1 update/recovery flow.
3. Re-enable ADB after boot if further inspection is needed.
4. Do not keep trying unverified packages. Inspect rootfs modes, `hiby_player`
   executable bit, and verifier failures first.

Known historical black-screen causes:

- Repacked rootfs left `/usr/bin/hiby_player` non-executable.
- A package passed a basic update but booted to black screen due to unsafe
  binary/rootfs changes.

Those hashes are guarded in the staging/verifier scripts.

## Development Cleanup

Before releasing or asking others to test:

- Remove active dev artifacts from `/usr/data/audiobooks`.
- Keep logs capped.
- Verify no personal seed catalog is embedded in rootfs.
- Verify package hashes match docs.
- Verify Music albums/search have no audiobook leakage.
- Verify a clean SD-card scan/update path works without PC-side DB tools.
- Verify a stock firmware recovery path remains available.
