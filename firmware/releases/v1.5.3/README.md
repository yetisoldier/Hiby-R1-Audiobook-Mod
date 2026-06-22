# HiBy R1 Audiobook Mod v1.5.3

Hotfix release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.16.4-audiobook.upt`
- Firmware marker: `1.6.16.4-audiobook`
- MD5: `d6ebce37c653f3756b54a7b5c3725788`
- SHA256: `eefd1f060babf5930d7bae4be481d7f580edf225a128d17ab6130beced4dd404`
- Installed verification: passed on the test R1 on 2026-06-22 after a forced same-size media DB rewrite with zero audiobook rows. The watcher rebuilt the audiobook catalog from `/Audiobooks` and Audiobooks opened from the repaired DB.

## What Changed Since v1.5.2

- Fixed another `No music found` case after Music -> Update Database on a new or changed SD card.
- Files under `/Audiobooks` no longer need the genre tag to be exactly `Audiobook` to appear in the Audiobooks section.
- The DB watcher now detects when audiobook files exist on the SD card but the media database is missing those audiobook rows.
- The DB helper repairs missing audiobook rows by folder location, keeps audiobooks out of Music Albums/Genres/Search, and rebuilds the title/author/series sidecar catalogs.
- Live testing forced a DB with zero audiobook rows while 298 audiobook files were present; the installed firmware repaired it back to 298 audiobook rows and mirrored the fixed DB.

All v1.5.2 features are otherwise retained: Audiobooks launcher, title-list start, per-book resume, multipart resume, audiobook/music separation, Native DSD flag, Bluetooth SBC XQ, and unlocked USB DAC mode settings.

## Install

1. Download `r1-audiobooks-1.6.16.4-audiobook.upt`.
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
- This replaces the old text Books launcher flow.
- Keep a stock HiBy R1 1.6 firmware file handy in case you want to revert.
