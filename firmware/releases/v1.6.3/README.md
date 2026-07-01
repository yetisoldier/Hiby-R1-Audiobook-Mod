# HiBy R1 Audiobook Mod v1.6.3

Hotfix release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.16.6-audiobook.upt`
- Firmware marker: `1.6.16.6-audiobook`
- MD5: `f4b605a1edd8385a0d6ed5279dfa7add`
- SHA256: `75d9d3d822bba35a9eb4a508fb604f720b97276b61e71f6dfa09360eff359ebf`
- Local package verification: passed.

## What Changed Since v1.6.2

- Fixed a likely title-start resume bug after listening to Music.
- When Music is still the active playback path but the Audiobooks title list is visible, the resume daemon now refreshes Audiobooks title context and watches title selections at the faster Audiobooks cadence.
- This targets reports where opening the exact chapter resumed correctly, but `Audiobooks -> Titles -> Book Title` could fail to start/resume after switching from music.
- All `v1.6.2` bookmark, refresh-library, folder-based audiobook detection, multipart resume, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is retained.

## Install

1. Download `r1-audiobooks-1.6.16.6-audiobook.upt`.
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
- There is still no audiobook search UI.
- The generated `_views` folder may appear under Audiobooks -> Folders.
- From the Folders root, edge-back is more reliable than the left arrow.
- This replaces the old text Books launcher flow.
- Keep a stock HiBy R1 1.6 firmware file handy in case you want to revert.
