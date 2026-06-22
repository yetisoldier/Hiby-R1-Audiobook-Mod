# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Current Release

- GitHub release: `v1.6.1`
- Custom version marker: `1.6.18-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.18-audiobook\r1-audiobooks-1.6.18-audiobook.upt`
- Firmware MD5: `e3dba87c24ef84196ec1c91fe3c3e26a`
- Firmware SHA256: `e42d70d84bf3353391c16fa60f83f399d2624226d2792f3c7882d9a1bbe45253`
- Rootfs MD5: `dd47cf5f338d70ecab1f8be108529505`
- Rootfs SHA256: `bfac581b61ff87c133bb5eb085a5ce5bb56db10678bae84697fae04d8697f8e6`
- `hiby_player` MD5: `cd4d2812ab3425174b52925766424d2b`

Local package verification passed on 2026-06-22. The matching
`1.6.17.2-refresh-dev` build was installed and live-tested on the test R1 before
public relabeling. Installed artifacts are under
`work\installed-release-verification\20260622-144832`.

Installed verification confirmed:

- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- DB integrity is `ok`.
- Audiobooks contains 135 media rows across 6 books on the test SD card.
- Title, author, and series sidecar catalogs are present.
- Music search and album tables have no audiobook leakage.
- The Audiobooks hub contains `Refresh Library`, `Titles`, `Authors`, `Series`,
  and `Folders`.
- `Refresh Library` opens the Titles view as feedback, writes a manual refresh
  request, and logs refresh completion under `/usr/data/audiobooks/refresh.log`.
- Audiobooks rows are rebuilt from files under `/Audiobooks`, so genre tags do
  not need to be exactly `Audiobook`.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.18-audiobook.upt` to the SD-card root.
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
  -ExpectedVersion 1.6.18-audiobook `
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

The previous public release was `v1.6.0`, firmware marker
`1.6.17-audiobook`.
