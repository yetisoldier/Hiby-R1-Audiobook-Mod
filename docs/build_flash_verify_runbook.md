# Build, Flash, Verify, And Release Runbook

This runbook documents the repeatable development workflow for the HiBy R1
audiobook firmware. It is written for modders who want to build or modify the
firmware, not for normal end users.

## Current Release Reference

- Public release: `v2.0.28`
- About-screen label: `HiBy R1 2.0.28`
- Package: `r1-audiobooks-2.0.28.upt`
- Base firmware: stock HiBy R1 1.6 for the normal R1
- Target device: normal HiBy R1 only, not R1 MIDI
- Source branch: `main`. The complete NativeApp source and release history are
  maintained on the repository's main branch.

> This runbook was rewritten for the v2.0.x "NativeApp" pivot (in-process
> `LD_PRELOAD` hook into `hiby_player`). The pre-2.0 v1.6.x resume-daemon /
> DB-watcher / generated-`_views` workflow it replaced is historical. For the
> deep architecture and the change categories that have bricked the device,
> see [`docs/modding/`](./modding/) — especially
> [`modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md)
> before building or flashing anything.

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

## 2. Build The Native App + Hook

The NativeApp pivot builds the hook shared library (`libaudiobook_hook.so`,
~1.6 MB) and the native smoke-test helper from `audiobook_app/` with
`zig cc` (target `mipsel-linux-gnueabihf.2.22`). This is handled by the
firmware build script in §3, which invokes `tools/build_r1_audiobook_hook.ps1`.
To build just the hook/app in isolation (for a quick check):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_hook.ps1
```

The Zig toolchain lives under `.deps\zig\` (gitignored by design); the build
script throws if it is missing. Install it via
`tools\build_r1_db_maint_helper.ps1` if needed. Static linking is NOT
supported for this glibc target — the hook is a shared lib. See
[`modding/hook_architecture.md`](./modding/hook_architecture.md) for the
full build flags and ABI pin rationale.

(The legacy `build_r1_db_maint_helper.ps1` / `build_r1_memscan_helper.ps1`
/ `build_r1_direct_open_helper.ps1` build the v1.6.x resume-daemon helpers
and are NOT used by the NativeApp pivot.)

## 3. Build The Audiobook Firmware

The current (v2.0.x) release-style build uses the NativeApp pivot
(`-IncludeAudiobookNativeApp`) and the three optional audio unlocks that the
pre-2.0 line carried and v2.0.17 restores. Public builds omit persistent ADB:

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

Run the release package verifier against the NativeApp build:

```powershell
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

The verifier is intentionally strict. NativeApp mode checks the launcher
callback and cave, stock Books hub preservation, wrapper, native app and hook
modes, version/feature markers, stock theme-aware launcher resources, rootfs
paths/modes/symlinks,
OTA rootfs hash, and known-bad package hashes. Do not add the legacy
`--require-db-maintenance`, `--expect-native-hub-launcher`, or
`--expect-native-hub-view-rows` flags; those contradict the NativeApp pivot.

## 5. Stage Firmware On The R1

Enable ADB manually on the R1 for development, connect it, then stage the
verified package. Public builds intentionally do not restart ADB after reboot.
The staging script runs local verification, refuses known-bad hashes, pushes
to `/usr/data/mnt/sd_0/r1.upt`, backs up an existing different `r1.upt`, and
verifies remote byte count + hashes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_stage_verified_firmware.ps1 `
  -Package work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt `
  -BuildOutDir work\audiobook-firmware-2.0.28 `
  -ExpectedVersion 2.0.28 `
  -ExpectedLabel "HiBy R1 2.0.28" `
  -ExpectNativeApp `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -IUnderstandThisStagesFirmware
```

> **Gotcha:** `adb_stage_verified_firmware.ps1` defaults to staging the stale
> 1.6.16.6 build — it does not know the NativeApp pivot output path. If it
> does not pick up your 2.0.x `.upt`, stage directly with `adb push` instead
> (gives no auto-backup, so back up the prior image first):
>
> ```bash
> adb shell cp /usr/data/mnt/sd_0/r1.upt /usr/data/mnt/sd_0/r1.upt.prev.bak
> MSYS_NO_PATHCONV=1 adb -s <serial> push work\audiobook-firmware-2.0.28\r1-audiobooks-2.0.28.upt /usr/data/mnt/sd_0/r1.upt
> adb shell md5sum /usr/data/mnt/sd_0/r1.upt
> ```
>
> The `MSYS_NO_PATHCONV=1` prefix is required on Windows git-bash so the
> `/usr/...` destination is not mangled to `C:/Program Files/Git/usr/...`.
> See [`modding/flash_and_recovery.md`](./modding/flash_and_recovery.md).

## 6. Flash On The Device

The data-preserving ADB flash path is byte-for-byte the menu's firmware-update
path, but driven from ADB so `/usr/data` (library.db, resume positions,
bookmarks, BT pairings) is preserved:

```bash
# 1. (Staged in §5 — .upt is at /usr/data/mnt/sd_0/r1.upt, md5 verified.)

# 2. Write the ota:kernel2 boot-MODE marker to mtd5.
MSYS_NO_PATHCONV=1 adb -s <serial> shell /usr/bin/bootmode.sh Recovery

# 3. Reboot into recovery — bootmode.sh does NOT reboot on its own.
adb -s <serial> reboot
```

`bootmode.sh Recovery` does `flash_erase /dev/mtd5 0 1` + `nandwrite -s 0 -p
/dev/mtd5 -` to write a 256-byte `ota:kernel2` marker. After `adb reboot`, the
bootloader sees the marker, boots the recovery kernel, which validates +
applies the staged `.upt`, and reboots into the new firmware (~90–100 s). ADB
returns on the new version.

**Critical gotchas** (all from [`modding/flash_and_recovery.md`](./modding/flash_and_recovery.md)):

- `bootmode.sh Recovery` does **NOT reboot** — you must run `adb reboot`
  separately. A flash that wrote the marker but never rebooted leaves the
  device on the old version with nothing applied.
- Use `Recovery` (writes `ota:kernel2`); `bootmode.sh` with no arg / `*`
  writes `ota:kernel` — a different boot mode, not the firmware-update path.
- **DO NOT use `/data/recovery_all` + `S39_recovery.recovery`** — that is a
  factory reset (`rm -rf /data/*`) that wipes `/usr/data`. Only
  `bootmode.sh Recovery` is data-preserving.
- Prefix `MSYS_NO_PATHCONV=1` on Windows git-bash for `adb shell <abs-path>`,
  or the path is mangled to `C:/Program Files/Git/usr/...` and the command is
  a silent no-op.

End users flashing from the SD card use the normal R1 firmware update UI
instead (System → Firmware Update → Local); the result is identical. Remove
or rename `r1.upt` on the SD card after a successful flash so the updater
stops offering it.

## 7. Verify The Installed Firmware

After flashing, manually enable ADB for development verification. Public builds
do not start it automatically:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 2.0.28 `
  -ExpectNativeApp `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

The installed verifier checks (NativeApp-relevant):

- `/etc/r1_audiobook_version` (version + enabled-feature markers)
- `/usr/resource/config.json` and audio unlock markers (`ot_devices.json`,
  `bt_init`, `set_functions.json`)
- NativeApp wrapper, executable, preload hook, and running `hiby_player` host
- SD-native `library.db` integrity plus nonempty `books` and `tracks` tables
- staged firmware hygiene
- free space
- framebuffer capture

(The legacy resume-daemon / DB-watcher / generated-catalog checks are
v1.6.x-era and do not apply when `-ExpectNativeApp` is selected.) See
[`modding/adb_automation_screenshots.md`](./modding/adb_automation_screenshots.md)
for driving the UI over ADB safely.

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
  RELEASE_NOTES.md
  MD5SUMS.txt
  SHA256SUMS.txt

work\audiobook-firmware-X.Y.Z\
  r1-audiobooks-X.Y.Z.upt
```

Update release docs:

```text
CHANGELOG.md
README.md
docs\production_release_checklist.md
```

Commit the release documentation, fast-forward `main` to the tested release
commit, and push `main`. Then create and push the annotated tag:

```powershell
git tag -a vX.Y.Z -m "HiBy R1 Audiobook Mod vX.Y.Z"
git push origin main
git push origin vX.Y.Z
```

Publish the GitHub Release through the checked-in REST helper. **The
release body file MUST be pure ASCII** — PowerShell 5.1 `Get-Content -Raw`
reads no-BOM UTF-8 as cp1252, so `ConvertTo-Json` fails with HTTP 400 on any
non-ASCII byte. Verify `0` non-ASCII bytes before publishing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag vX.Y.Z `
  -Name "HiBy R1 Audiobook Mod vX.Y.Z" `
  -TargetCommitish main `
  -BodyFile firmware\releases\vX.Y.Z\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-X.Y.Z\r1-audiobooks-X.Y.Z.upt,firmware\releases\vX.Y.Z\MD5SUMS.txt,firmware\releases\vX.Y.Z\SHA256SUMS.txt,firmware\releases\vX.Y.Z\RELEASE_NOTES.md"
```

> **Gotchas** (see [`docs/github_release_process.md`](./github_release_process.md)):
> a transient 503→422 "asset already exists" response cleans up on retry; on
> Windows git-bash, **unquoted backslashes in PowerShell
> args are stripped** (`work\release-v2.0.17\r1-audiobooks-2.0.17.upt` →
> `workrelease-v2.0.17r1-audiobooks-2.0.17.upt`) — quote paths or use forward
> slashes when invoking PowerShell from git-bash.

Always verify the release API object and assets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag vX.Y.Z `
  -VerifyOnly `
  -Assets "work\audiobook-firmware-X.Y.Z\r1-audiobooks-X.Y.Z.upt,firmware\releases\vX.Y.Z\MD5SUMS.txt,firmware\releases\vX.Y.Z\SHA256SUMS.txt,firmware\releases\vX.Y.Z\RELEASE_NOTES.md"
```

A pushed tag is not enough. The GitHub Release object must exist and list the
download assets.

## Recovery And Rollback

If a custom build fails (or bricks — two historical builds did, see
[`modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md)):

1. Put a known-good `r1.upt` (stock 1.6, or a prior good release like `2.0A`)
   on the SD card root.
2. Use the normal R1 SD-card firmware update / recovery flow. The bootloader's
   recovery path reads the SD root even when the main rootfs is broken.
3. Once it boots the known-good firmware, re-stage and re-flash normally.
4. Re-enable ADB manually after boot if further inspection is needed. A
   development build may use marker-gated `-EnableBootAdb`, but public builds
   intentionally omit it.
5. Do not keep trying unverified packages. Inspect rootfs modes,
   `hiby_player` executable bit, and verifier failures first.

See [`modding/flash_and_recovery.md`](./modding/flash_and_recovery.md) for the
full revert path and the MTD layout. Known historical black-screen / brick
causes (guarded in the staging/verifier scripts):

- Repacked rootfs left `/usr/bin/hiby_player` non-executable.
- A package passed a basic update but booted to black screen due to unsafe
  binary/rootfs changes.
- v2.0.1: bundled PMIC (AXP2101) + USB-DAC + brightness-binary-patch changes.
- v2.0.2: `mount_ubifs.sh` `sync`→`noatime` broke the `/usr/data` UBIFS mount
  → freeze/reset loop.

## Development Cleanup

Before releasing or asking others to test:

- Remove active dev artifacts from `/usr/data/audiobooks`.
- Keep logs capped.
- Verify no personal seed catalog is embedded in rootfs.
- Verify package hashes match docs.
- Verify Music albums/search have no audiobook leakage.
- Verify a clean SD-card scan/update path works without PC-side DB tools.
- Verify a stock firmware recovery path remains available.
