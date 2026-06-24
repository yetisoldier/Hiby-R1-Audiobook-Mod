# HiBy R1 Audiobook Mod v1.6.2

Bookmark release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.16.5-audiobook.upt`
- Firmware marker: `1.6.16.5-audiobook`
- MD5: `64fd718252935d0ebf220b43e1f86a0e`
- SHA256: `1410a718778b269a49165ef6fd6f0a8c67466ae600333332c3e989ff66952def`
- Local package verification: passed.
- Installed-device verification: passed on the test R1 after flashing the public `1.6.16.5-audiobook` package.

## What Changed Since v1.6.1

- Replaced the old `Series` hub row with `Bkmarks`.
- Added manual bookmark save from the Now Playing screen via long-press Back.
- Added a native bookmark monitor helper that writes bookmark requests without changing stock playback controls.
- Added generated bookmark playlist views under `/Audiobooks/_views/Bookmarks`.
- Opening a bookmark from `Bkmarks` now restores near the saved bookmark position, even when the same book also has a deeper ordinary resume point.
- `Refresh Library`, `Titles`, `Authors`, and `Folders` remain in the native Audiobooks hub.
- All `v1.6.1` refresh-library behavior, folder-based audiobook detection, multipart resume, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is retained.

## Install

1. Download `r1-audiobooks-1.6.16.5-audiobook.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the SD card.
4. Run the normal firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename `r1.upt` on the SD card.
6. Go to Music and run `Update Database`.
7. Wait for the scan to complete, then open Audiobooks.

Recommended SD card folders:

```text
/Music
/Audiobooks
```

The genre tag does not need to be exactly `Audiobook`. Files under `/Audiobooks` are treated as audiobooks by folder location.

## Known Quirks

- The About screen may shorten the visible version suffix.
- Audiobook positions are saved only after at least 15 seconds of playback.
- Bookmark restores should land near the saved time, but it is still worth living with the feature for a while and watching for odd library-specific edge cases.
- There is still no audiobook search UI.
- The generated `_views` folder may appear under Audiobooks -> Folders.
- From the Folders root, edge-back is more reliable than the left arrow.
- This replaces the old text Books launcher flow.
- Keep a stock HiBy R1 1.6 firmware file handy in case you want to revert.
