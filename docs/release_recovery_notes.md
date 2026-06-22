# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Current Release

- GitHub release: `v1.5.4`
- Custom version marker: `1.6.16.5-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.16.5-audiobook\r1-audiobooks-1.6.16.5-audiobook.upt`
- Firmware MD5: `f6a0e65af41c7990f03e342fef995bad`
- Firmware SHA256: `efd77a5a6f83879e76089ace072657891ff2e5475c4f0e82d812f728ad4e2816`
- Rootfs MD5: `1797f124a92177605e776615144f323a`
- Rootfs SHA256: `cf2076de6c700abd24d66dc587ac3109786829e5f589f4068e61988b0a481325`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`

Local verification and installed-device verification passed on 2026-06-22.
Installed artifacts are under
`work\installed-release-verification\20260622-093147`.

Installed verification confirmed:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.5-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` may be present if the firmware was staged for testing; remove or rename it after a manual install.
- `/usr/data` has about 31 MB free after cleanup on the test device.
- DB integrity is `ok`.
- Audiobooks contains 135 media rows across 6 books on the swapped-card test SD.
- SD-root `usrlocal_media.db` has integrity `ok` and 135 normalized audiobook
  rows.
- A forced live SD-swap regression test replaced the internal DB with an old-card
  copy containing 298 audiobook rows while the SD-root DB correctly contained
  the current card's 135 audiobook rows; the watcher promoted the SD DB, repaired
  the internal DB, and mirrored the fixed DB.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.
- Audiobooks rows are rebuilt from files under `/Audiobooks`, so genre tags do
  not need to be exactly `Audiobook`.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16.5-audiobook.upt` to the SD-card root.
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
  -ExpectedVersion 1.6.16.5-audiobook `
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

The previous public release was `v1.5.3`, firmware marker
`1.6.16.4-audiobook`.
