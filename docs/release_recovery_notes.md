# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Current Release

- GitHub release: `v1.5.2`
- Custom version marker: `1.6.16.2-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.16.2-audiobook\r1-audiobooks-1.6.16.2-audiobook.upt`
- Firmware MD5: `80c0d7295c2d55575870c4d226e83be9`
- Firmware SHA256: `3109fea179b816dcdd4c1536b8973f527ef8f8b2d628942317f6b4ded62ca4c6`
- Rootfs MD5: `35ffdbb9b401c03f1742782da0104b55`
- Rootfs SHA256: `127b90ddfc92ecf2e668368e31422b5eb090c47e86011c391c30dd4b4ec4c475`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`

Local verification and installed-device verification passed on 2026-06-17.
Installed artifacts are under
`work\installed-release-verification\20260617-170615`.

Installed verification confirmed:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.2-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` is absent after post-flash cleanup.
- `/usr/data` has about 26 MB free.
- DB integrity is `ok`.
- Audiobooks contains 298 media rows across 52 books on the regression SD card.
- SD-root `usrlocal_media.db` has integrity `ok` and 298 normalized audiobook
  rows, fixing the new-card `No music found` regression.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.
- A screenshot-assisted ADB check opened Audiobooks to the title list instead of
  `No music found`: `work\adb-control\screenshots\20260617-170213-preset-main-audiobooks.png`.

The installed package archive on the SD card is:

```text
/usr/data/mnt/sd_0/r1-audiobooks-1.6.16.2-audiobook-installed-20260617-1703.upt
```

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16.2-audiobook.upt` to the SD-card root.
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
  -ExpectedVersion 1.6.16.2-audiobook `
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

The previous public release was `v1.5.1`, firmware marker
`1.6.16.1-audiobook`.
