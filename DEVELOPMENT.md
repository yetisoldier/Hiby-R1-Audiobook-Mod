# Development

Developer-facing documentation for building, verifying, flashing, and testing
the HiBy R1 Audiobook Firmware Mod. For user-facing install and usage, see
[README.md](README.md).

For the end-to-end build/flash/verify/publish workflow, also see
[docs/build_flash_verify_runbook.md](docs/build_flash_verify_runbook.md).

For modder orientation, see [docs/modder_start_here.md](docs/modder_start_here.md).

## Prerequisites

On the development PC:

- Windows PowerShell
- Python
- Git
- ADB or repo-local `.tools/platform-tools/adb.exe`
- WSL Ubuntu 24.04 for shell syntax and QEMU/user-mode helper tests
- Stock HiBy R1 1.6 package saved as `stock/r1.upt`
- A known-good stock `r1.upt` kept separately for recovery

The build scripts download or use local dependencies under `.deps` where
possible. Do not commit `.deps`, extracted work trees, ADB captures, or device
state dumps.

## 1. Extract Stock Firmware

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

Resume helper binaries:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_memscan_helper.ps1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_direct_open_helper.ps1
```

## 3. Build The Audiobook Firmware

Current release-style build with native Audiobooks hub, generated views,
DB maintenance, resume runtime, custom icon, and audio unlocks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-1.6.18-audiobook `
  -OutputUpt work\audiobook-firmware-1.6.18-audiobook\r1-audiobooks-1.6.18-audiobook.upt `
  -IncludeAudiobookLauncherIcon `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -IncludeAudiobookResumeRuntime `
  -IncludeAudiobookDbMaintenance `
  -IncludeAudiobookNativeHubLauncher `
  -IncludeAudiobookNativeHubViewRows `
  -CustomVersionId 1.6.18-audiobook `
  -CustomVersionLabel "HiBy R1 Audiobook FW 1.6.18"
```

Build switches should stay explicit for release candidates.

### Conservative Offline Package (Labels Only)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1
```

This package only applies English Audiobooks labels and was kept as an early
development reference.

### Boot ADB For Development Builds

```powershell
tools\build_r1_audiobook_firmware.ps1 -EnableBootAdb
```

Installs `/etc/init.d/S90adb` as a wrapper around stock `/etc/init.d/T90adb`.
The wrapper only starts ADB when `System -> USB working mode` is set to
`Device`.

## 4. Run Local Sanity Checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_local_dev_sanity.ps1
```

This checks PowerShell parsing, shell syntax (WSL), Python compile, resume
daemon logic, DB watcher logic, Windows DB helper fixture, QEMU MIPS DB helper
fixture, and Git whitespace.

### Local Test Helpers

**DB helper fixture test:**

```powershell
python tools\test_r1_db_maint_local_fixture.py `
  --helper work\native-db-maint\r1_audiobook_db_maint_win_test.exe
```

**MIPS DB helper under WSL/QEMU:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\test_r1_db_maint_qemu_wsl.ps1 `
  -Helper work\native-db-maint\r1_audiobook_db_maint
```

**Resume daemon logic test:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\test_r1_resume_daemon_logic_wsl.ps1
```

## 5. Verify The Package

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

The verifier checks exact patch bytes, rootfs modes, hashes, scripts,
resources, feature markers, and known-bad package hashes.

## 6. Stage Firmware On The R1

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

The staging script runs local verification first, refuses known-bad packages,
pushes to `/usr/data/mnt/sd_0/r1.upt`, backs up existing files, and verifies
remote byte count and hashes.

## 7. Flash On The Device

Use the normal R1 firmware update UI. After the update finishes and the R1
reboots, manually re-enable ADB if further verification is needed.

## 8. Verify The Installed Firmware

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

The installed verifier checks version markers, audio unlock markers, resume
daemon and DB watcher processes, DB helper files, staged firmware hygiene,
`user.ini` cleanup, free space, media DB integrity, audiobook row counts, Music
leakage, generated catalogs, and framebuffer capture.

## 9. Live Smoke Testing

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_live_audiobook_smoke.ps1 `
  -ResetBacks 4
```

Lower-level ADB control:

```powershell
python tools\r1_adb_control.py devices
python tools\r1_adb_control.py screenshot --classify --label before-test
python tools\r1_adb_control.py preset main-audiobooks --after-screenshot
```

## 10. Publish A Public Release

Prepare release assets under `firmware/releases/vX.Y.Z/`:

```text
firmware\releases\vX.Y.Z\
  README.md
  MD5SUMS.txt
  SHA256SUMS.txt
  r1-audiobooks-...upt
```

Update `CHANGELOG.md` and release docs. Commit, tag, and push:

```powershell
git tag -a vX.Y.Z -m "HiBy R1 Audiobook Mod vX.Y.Z"
git push origin HEAD:main
git push origin vX.Y.Z
```

Publish the GitHub Release:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag vX.Y.Z `
  -Name "HiBy R1 Audiobook Mod vX.Y.Z" `
  -BodyFile firmware\releases\vX.Y.Z\README.md `
  -Assets "firmware\releases\vX.Y.Z\r1-audiobooks-...upt,firmware\releases\vX.Y.Z\MD5SUMS.txt,firmware\releases\vX.Y.Z\SHA256SUMS.txt"
```

Verify the release:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag vX.Y.Z `
  -VerifyOnly `
  -Assets "firmware\releases\vX.Y.Z\r1-audiobooks-...upt,firmware\releases\vX.Y.Z\MD5SUMS.txt,firmware\releases\vX.Y.Z\SHA256SUMS.txt"
```

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
  binary/rootfs changes (mode/ownership drift, e.g. `/bin/busybox` lost setuid).

Those hashes are guarded in the staging/verifier scripts.

## Binary Patching

Default patcher behavior is safe (no patches applied):

```powershell
python tools\patch_hiby_player.py work\rootfs\usr\bin\hiby_player -o work\patched\hiby_player.default-safe
```

Audiobooks launcher genre patch (opt-in):

```powershell
python tools\patch_hiby_player.py work\rootfs\usr\bin\hiby_player `
  --audiobook-launcher-genre `
  -o work\patched\hiby_player.audiobook-launcher-genre
```

Historical experimental shim (for analysis only):

```powershell
python tools\patch_hiby_player.py work\rootfs\usr\bin\hiby_player --scan-skip --book-audio-shim -o work\patched\hiby_player.experimental-shim
```

RAM-only runtime patch helper (dry-run by default):

```powershell
python tools\adb_runtime_patch_hiby_player.py
```

## DB Tools

**Add audiobooks to a copied media DB:**

```powershell
python tools\add_audiobooks_to_media_db.py `
  work\device-db-20260609-093915\usrlocal_media.db `
  --adb-scan --adb-sizes `
  -o work\db-audiobooks-all-20260609\usrlocal_media.with-audiobooks.sized.db
```

**Split-catalog mode (audiobooks in media tables, out of Music catalog):**

```powershell
python tools\add_audiobooks_to_media_db.py `
  work\live-db-before-split-20260609-105855\usrlocal_media.live-before-split.db `
  --adb-scan --adb-sizes `
  --music-catalog-excludes-audiobooks `
  --id-base 1000 `
  -o work\live-db-before-split-20260609-105855\usrlocal_media.split-catalog.db
```

**Build release-clean DB/catalog:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_build_release_audiobook_db.ps1
```

**Install checked release DB/catalog:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_install_release_audiobook_db.ps1 `
  -Database work\release-db-candidate\...\usrlocal_media.release-candidate.db `
  -Catalog work\release-db-candidate\...\catalog.release-candidate.tsv `
  -RestartResumeDaemon `
  -MoveRemoteBackupsToSd `
  -IUnderstandThisModifiesDevice
```

**Generate resume catalog:**

```powershell
python tools\write_audiobook_resume_catalog.py `
  work\...\usrlocal_media.split-catalog.db `
  -o work\audiobook-resume-catalog.tsv
```

**Install resume runtime:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_install_audiobook_resume_runtime.ps1 `
  -CatalogSource work\audiobook-resume-catalog.tsv `
  -RestoreEnabled
```

## Debug And Monitoring Tools

**Collect resume debug bundle:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_collect_audiobook_resume_debug.ps1
```

**Runtime monitor (battery, memory, processes):**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_monitor_r1_runtime.ps1 `
  -DurationMinutes 120 `
  -IntervalSeconds 60
```

**Guarded seek test:**

```powershell
python tools\adb_test_audiobook_seek_restore.py --sample-seconds 4 --back-seconds 20
```

**Test helper build (Windows):**

```powershell
$zig = (Resolve-Path .deps\zig\zig-x86_64-windows-0.16.0\zig.exe).Path
$sqlite = (Resolve-Path .deps\sqlite\sqlite-amalgamation-3530200).Path
& $zig cc -target x86_64-windows-gnu -O2 -I $sqlite `
  -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION `
  -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED `
  tools\r1_audiobook_db_maint.c (Join-Path $sqlite 'sqlite3.c') `
  -o work\native-db-maint\r1_audiobook_db_maint_win_test.exe
```

## Development Cleanup

Before releasing or asking others to test:

- Remove active dev artifacts from `/usr/data/audiobooks`.
- Keep logs capped.
- Verify no personal seed catalog is embedded in rootfs.
- Verify package hashes match docs.
- Verify Music albums/search have no audiobook leakage.
- Verify a clean SD-card scan/update path works without PC-side DB tools.
- Verify a stock firmware recovery path remains available.

## Repository Map

- `tools/extract_r1_firmware.ps1` — extracts `stock/r1.upt`
- `tools/filter_music_db.py` — removes `/Audiobooks/` from copied media DB
- `tools/add_audiobooks_to_media_db.py` — adds `/Audiobooks/` audio files to media DB
- `tools/check_audiobook_release_state.py` — verifies release DB/catalog invariants
- `tools/adb_build_release_audiobook_db.ps1` — builds release-clean DB/catalog
- `tools/adb_install_release_audiobook_db.ps1` — installs checked DB/catalog
- `tools/adb_verify_installed_audiobook_release.ps1` — post-reboot installed verifier
- `tools/adb_collect_r1_state.ps1` — read-only ADB state collection
- `tools/r1_adb_control.py` — unified non-persistent R1 control console
- `tools/patch_hiby_player.py` — guarded binary patcher
- `tools/patch_r1_resource_text.py` — resource/localization text patcher
- `tools/build_r1_upt.py` — rebuilds R1-style OTA `.upt`
- `tools/build_r1_audiobook_firmware.ps1` — offline build wrapper
- `tools/verify_r1_audiobook_build.py` — local pre-flash verifier
- `tools/r1_audiobook_resume_daemon.sh` — on-device resume daemon
- `tools/r1_audiobook_db_maint.c` — on-device native DB helper
- `tools/r1_audiobook_db_watch.sh` — on-device DB watcher
- `tools/r1_audiobook_refresh.sh` — manual refresh helper
- `tools/r1_audiobook_memscan.c` — title/track selection helper
- `tools/r1_audiobook_direct_open.c` — direct-open helper
- `firmware/seed/usrlocal_media.seed.db` — empty seed DB schema
- `firmware/releases/` — public release packages and checksums

## Developer Documentation Index

- [Modder Start Here](docs/modder_start_here.md) — orientation for new contributors
- [Audiobook Firmware Architecture](docs/audiobook_firmware_architecture.md) — how the mod is wired together
- [Build, Flash, Verify Runbook](docs/build_flash_verify_runbook.md) — detailed end-to-end workflow
- [Investigation](docs/investigation.md) — original stock firmware findings
- [Audiobook Views Research](docs/audiobook_views_research.md) — route experiments and hub design
- [ADB Control Tools](docs/adb_control_tools.md) — live device control reference
- [Production Release Checklist](docs/production_release_checklist.md) — pre-release verification
- [GitHub Release Process](docs/github_release_process.md) — publishing runbook
- [Release Recovery Notes](docs/release_recovery_notes.md) — compact recovery reference
- [Safe Prototype](docs/safe_prototype.md) — older non-flash ADB prototype workflow
- [Network OTA Research](docs/network_ota_research.md) — OTA format and hosting notes
- [Architecture Review](docs/architecture-review-2026-07-10.md) — architecture review notes
- [Dev History Archive](docs/dev-history/firmware-improvement-plan-archive.md) — historical development tracking