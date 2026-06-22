# HiBy R1 Audiobook Mod v1.6.0

Feature release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.17-audiobook.upt`
- Firmware marker: `1.6.17-audiobook`
- MD5: `e8491f65ead4ef7a34163a67c7ee7007`
- SHA256: `47b6b2aa85f0f14d13d659f0f3f987808f7d389a7a32bf7e54676388e6f82523`
- Installed verification: passed on the test R1 after flashing this package.

## What Changed Since v1.5.4

- Audiobooks now opens to a native hub with `Scan`, `Titles`, `Authors`, `Series`, and `Folders`.
- `Titles`, `Authors`, and `Series` are generated on-device after Music -> Update Database.
- `Folders` opens the SD-card `/Audiobooks` folder and uses a friendly `Folders` header.
- The DB helper repairs the internal Audiobook route row/count needed by the new hub after scans and SD-card swaps.
- The DB helper avoids embedded-NUL text in audiobook media rows.
- The v1.5.4 SD-card swap repair, folder-based audiobook detection, resume behavior, Native DSD, Bluetooth SBC XQ, and USB DAC-related settings are retained.

## Install

1. Download `r1-audiobooks-1.6.17-audiobook.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the SD card.
4. Run the normal firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename `r1.upt` on the SD card.
6. Go to Music and run `Update Database`.
7. Wait for the scan to complete, then give the watcher about a minute or reboot once before opening Audiobooks.

Recommended SD card folders:

```text
/Music
/Audiobooks
```

The genre tag does not need to be exactly `Audiobook`. Files under `/Audiobooks` are treated as audiobooks by folder location.

## Known Quirks

- The About screen may shorten the visible version suffix.
- Audiobook positions are saved only after at least 15 seconds of playback.
- There is still no audiobook search UI.
- The generated `_views` folder may appear under Audiobooks -> Folders.
- From the Folders root, edge-back is more reliable than the left arrow.
- This replaces the old text Books launcher flow.
- Keep a stock HiBy R1 1.6 firmware file handy in case you want to revert.
