# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Current Release

- GitHub release: `v1.5.3`
- Custom version marker: `1.6.16.4-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.16.4-audiobook\r1-audiobooks-1.6.16.4-audiobook.upt`
- Firmware MD5: `d6ebce37c653f3756b54a7b5c3725788`
- Firmware SHA256: `eefd1f060babf5930d7bae4be481d7f580edf225a128d17ab6130beced4dd404`
- Rootfs MD5: `8728cd7ad4734f3f36efdfe6d0c1093a`
- Rootfs SHA256: `394db7b39571f3cc95f04ceec1195f1fedb0abe3ac2a3dec3dbf5f7c3461c152`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`

Local verification and installed-device verification passed on 2026-06-22.
Installed artifacts are under
`work\installed-release-verification\20260622-084707`.

Installed verification confirmed:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.4-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` may be present if the firmware was staged for testing; remove or rename it after a manual install.
- `/usr/data` has about 18 MB free after cleanup on the test device.
- DB integrity is `ok`.
- Audiobooks contains 298 media rows across 52 books on the regression SD card.
- SD-root `usrlocal_media.db` has integrity `ok` and 298 normalized audiobook
  rows.
- A forced live regression test replaced the primary DB with a same-size copy
  containing zero audiobook rows while 298 audiobook files were present; the
  watcher repaired it with `content-repair-mtime` and mirrored the fixed DB.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.
- Audiobooks rows are rebuilt from files under `/Audiobooks`, so genre tags do
  not need to be exactly `Audiobook`.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16.4-audiobook.upt` to the SD-card root.
3. Rename the copied file to exactly `r1.upt`.
4. Run the firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename SD-root
   `r1.upt`.
6. Go to Music and run `Update Database`, then wait for the scan to complete.
7. Open Audiobooks.

## Verify

After flashing, optional ADB verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16.4-audiobook `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -CaptureFramebuffer
```

## Recovery

If the player ever fails to boot normally, reinstall the official stock HiBy R1
1.6 firmware with the normal SD-card recovery/update process. This mod is based
on stock 1.6 and should be reversible by reinstalling stock firmware.

Keep these together for recovery and comparison:

- Official HiBy R1 1.6 stock `r1.upt`
- This release package
- `MD5SUMS.txt`
- `SHA256SUMS.txt`

## Previous Release

The previous public release was `v1.5.2`, firmware marker
`1.6.16.2-audiobook`.
