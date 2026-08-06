# Production Release Checklist

This file records the exact public release inputs and verification evidence.
For the full procedure, see
[`build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) and
[`github_release_process.md`](./github_release_process.md).

## Current Release

- GitHub release: `v2.0.27`
- About-screen label: `HiBy R1 2.0.27`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `main`
- Source release commit: the commit tagged `v2.0.27`
- Package: `r1-audiobooks-2.0.27.upt` (42,246,144 bytes)
- Package MD5: `8a208aeb24087aee59c0ac02581aef0a`
- Package SHA256:
  `4b04586414aff6bceaf4cd6d50f88291199c94180bf3dac2793e9fbdf0b7d368`
- Hook MD5: `0fc5b293962fc6b6de7fecb604475cc2`
- Hook SHA256:
  `318d82b1f3d7a7b48b7414a3a4b8f698fbcd18c39bd9836657a35678670014c6`
- Build flags: `-IncludeAudiobookNativeApp -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.27
  -CustomVersionLabel "HiBy R1 2.0.27"`

## Changes Since v2.0.26

- Persistent ADB is disabled in the public build; `/etc/init.d/S90adb` is
  omitted so Device mode restores normal SD-card USB storage.
- Replaced stock `adbon`/`adboff` wrappers with serialized, logged transitions.
- ADB mode removes stale mass-storage LUNs and mounts the SD locally.
- Mass-storage mode refuses to export a busy local filesystem and restores ADB
  if a safe transition cannot complete.
- ADB shutdown runs in a detached worker so stopping `adbd` cannot terminate
  the transition halfway through.
- A direct FunctionFS fallback restores ADB if the stock helper refuses an
  already-mounted empty configfs during rollback.
- Development-only boot ADB is marker-gated, Device-only, delayed, and no
  longer has the controller-stealing retry loop.
- Audiobook playback and UI binaries are unchanged from v2.0.26.

## Build And Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-2.0.27 `
  -OutputUpt work\audiobook-firmware-2.0.27\r1-audiobooks-2.0.27.upt `
  -IncludeAudiobookNativeApp `
  -UnlockNativeDsd `
  -EnableBluetoothSbcXq `
  -UnlockUsbDacMode `
  -CustomVersionId 2.0.27 `
  -CustomVersionLabel "HiBy R1 2.0.27"

py -3 tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.27 `
  --upt-name r1-audiobooks-2.0.27.upt `
  --expected-version 2.0.27 `
  --expected-label "HiBy R1 2.0.27" `
  --expect-native-app `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

The firmware builder recompiles the preload hook from current source before
packaging and fails if that build does not succeed.

The verifier passed all NativeApp checks, including all 5,488 stock rootfs
paths and modes, 482 symlink targets, root ownership, launcher hook/cave,
wrapper, app and hook modes, hardened USB scripts, absence of `S90adb`, marker
flags, OTA rootfs hash, and known-bad package rejection.

A separate non-release build with `-EnableBootAdb` also passed strict
verification. Its `S90adb` requires the explicit opt-in marker, accepts Device
mode only, waits for stock USB initialization, delegates to the hardened helper,
and contains no retry loop. That development artifact is not published.

## Device Verification

Pre-flash transition testing used the test R1 running v2.0.26:

- An active audiobook kept the SD busy; the hardened transition correctly
  refused mass-storage export and left ADB plus the local mount intact.
- After leaving Audiobooks, the detached transition shut down ADB cleanly.
- Windows detected `Linux File-Stor Gadget` and mounted the 231 GiB `HibyR1`
  exFAT volume as normal USB storage.
- A reported non-playing M4B decoded successfully in the standalone probe and
  played in the real app after the SD mount was restored, confirming the USB
  transition rather than media encoding as the cause.

Rollback development first used a RAM-only bind mount of the revised helper,
then repeated the same checks from the final installed v2.0.27 rootfs:

- A forced direct FunctionFS fallback stopped and recreated ADB in about two
  seconds with serial `ingenic_2233` and UDC `13500000.otg_new`.
- With `hiby_player` holding `.temp/most_played.db`, mass-storage export was
  safely refused. The stock ADB restart reproduced its configfs error, the
  direct fallback took over, and ADB returned automatically with the SD still
  mounted locally.

Final installed v2.0.27 checks on the test R1:

- The stock SD-card updater completed for the exact package and rebooted.
- `/etc/r1_audiobook_version` reported `2.0.27`, `boot_adb=disabled`, and
  `usb_gadget_scripts=hardened`; `/etc/init.d/S90adb` was absent.
- The three installed USB helpers matched the checked-in source byte-for-byte.
- NativeApp, Native DSD, SBC XQ, USB DAC, 52 books / 298 tracks, and SQLite
  integrity passed the installed-release verifier.
- The 23:34:05 Butcher M4B opened as AAC-LC 44.1 kHz stereo, reached Now
  Playing, and advanced normally.
- The installed direct FunctionFS fallback and busy-SD refusal/ADB recovery
  both passed.
- The used `r1.upt` trigger was removed.
- After the final reboot, ADB did not enumerate and Windows automatically
  mounted the healthy 248 GB `HibyR1` exFAT volume as drive `I:`.

Installed verification command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 2.0.27 `
  -ExpectNativeApp `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -ExpectHardenedUsbGadgetScripts `
  -ExpectBootAdbDisabled `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

## Publish

Release assets:

- `work\audiobook-firmware-2.0.27\r1-audiobooks-2.0.27.upt`
- `firmware\releases\v2.0.27\MD5SUMS.txt`
- `firmware\releases\v2.0.27\SHA256SUMS.txt`
- `firmware\releases\v2.0.27\RELEASE_NOTES.md`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\publish_github_release.ps1 `
  -Tag v2.0.27 `
  -Name "HiBy R1 Audiobook Mod v2.0.27" `
  -TargetCommitish main `
  -BodyFile firmware\releases\v2.0.27\RELEASE_NOTES.md `
  -Assets "work\audiobook-firmware-2.0.27\r1-audiobooks-2.0.27.upt,firmware\releases\v2.0.27\MD5SUMS.txt,firmware\releases\v2.0.27\SHA256SUMS.txt,firmware\releases\v2.0.27\RELEASE_NOTES.md"
```

Run the same command with `-VerifyOnly` after publication and confirm all four
asset names and sizes through the GitHub release API.

## Known Limitations

- Audiobook playback stops when leaving the app for the HiBy launcher.
- No audiobook search UI; browse Titles, Authors, Series, or Folders.
- ADB, USB mass storage, and USB DAC are mutually exclusive. Persistent ADB is
  not included in the public build.
- UTF-8/Cyrillic text support from v2.0.20 is not included in this stability
  line.
- The original SD freeze was intermittent; broad long-duration testing across
  different SD cards remains valuable.
- This is unofficial firmware tested on one normal R1. Keep stock 1.6 firmware
  available for recovery.
