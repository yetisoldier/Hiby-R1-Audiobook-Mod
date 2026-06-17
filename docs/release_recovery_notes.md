# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Current Release

- GitHub release: `v1.5.1`
- Custom version marker: `1.6.16.1-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.16.1-audiobook\r1-audiobooks-1.6.16.1-audiobook.upt`
- Firmware MD5: `d30527750a071602a67f1eceb462f8cc`
- Firmware SHA256: `085495646039eafb496279d3ef2625671783552ad069150c3e959e5c219d7f3f`
- Rootfs MD5: `7a0b2a3d001ea53b079b79fbcf9c5933`
- Rootfs SHA256: `26c9b68e49a3761930dcae3c95b172905d8e88108c68f59be44ffe3c0a96d942`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`

Local verification and installed-device verification passed on 2026-06-17.
Installed artifacts are under
`work\installed-release-verification\20260617-160119`.

Installed verification confirmed:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16.1-audiobook`.
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

The installed package archive on the SD card is:

```text
/usr/data/mnt/sd_0/r1-audiobooks-1.6.16.1-audiobook-installed-20260617-1602.upt
```

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16.1-audiobook.upt` to the SD-card root.
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
  -ExpectedVersion 1.6.16.1-audiobook `
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

The previous public release was `v1.5.0`, firmware marker
`1.6.16-audiobook`.
