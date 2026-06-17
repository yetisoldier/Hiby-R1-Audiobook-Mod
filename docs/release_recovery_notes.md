# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Current Release

- GitHub release: `v1.5.0`
- Custom version marker: `1.6.16-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.16-audiobook\r1-audiobooks-1.6.16-audiobook.upt`
- Firmware MD5: `4938a5d3f74204995a1bb297175da463`
- Firmware SHA256: `ba3b16dc63e35abfc22cd0ac9e4324a5a2e3834ad894c42fd310f30f99c3f1e0`
- Rootfs MD5: `48abe53dc5e83e8eeb045dfd8f4a3d17`
- Rootfs SHA256: `adfdf99eefdeb2693aa8cf610780b05e60404f7d8e6dac33b0b5ef6b1c1d69ca`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`

Local verification and installed-device verification passed on 2026-06-17.
Installed artifacts are under
`work\installed-release-verification\20260617-151416`.

Installed verification confirmed:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.16-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- Play-mode guard is active.
- SD-root `r1.upt` is absent after post-flash cleanup.
- `/usr/data` has about 30 MB free.
- DB integrity is `ok`.
- Audiobooks contains 135 media rows across six books.
- Title, author, and series sidecar catalogs are present.
- Music search, album, and genre tables have no audiobook leakage.
- No known active development artifacts remain under `/usr/data/audiobooks`.

The installed package archive on the SD card is:

```text
/usr/data/mnt/sd_0/r1-audiobooks-1.6.16-audiobook-installed-20260617-1513.upt
```

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16-audiobook.upt` to the SD-card root.
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
  -ExpectedVersion 1.6.16-audiobook `
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

The previous public release was `v1.4.0`, firmware marker
`1.6.15-audiobook`.
