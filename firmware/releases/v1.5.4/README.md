# HiBy R1 Audiobook Mod v1.5.4

Hotfix release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.16.5-audiobook.upt`
- Firmware marker: `1.6.16.5-audiobook`
- MD5: `f6a0e65af41c7990f03e342fef995bad`
- SHA256: `efd77a5a6f83879e76089ace072657891ff2e5475c4f0e82d812f728ad4e2816`
- Installed verification: passed on the test R1 on 2026-06-22 after a forced SD-swap regression test where the internal DB was stale and the SD-root DB was current.

## What Changed Since v1.5.3

- Fixed an SD-card swap case where Audiobooks could keep using old-card internal database rows even after Music -> Update Database updated the SD card database.
- The watcher now notices SD-root media DB changes, not just internal DB changes.
- If the SD-card database is clean/current and the internal DB is stale, the watcher promotes the SD database to internal, rebuilds audiobook catalogs, and mirrors the repaired database.
- Live testing forced the internal DB back to 298 old-card audiobook rows while the inserted SD card had 135 audiobook files. The installed firmware promoted the SD DB and repaired the active DB back to 135 rows.

All v1.5.3 features are otherwise retained: folder-based audiobook detection, Audiobooks launcher, title-list start, per-book resume, multipart resume, audiobook/music separation, Native DSD flag, Bluetooth SBC XQ, and unlocked USB DAC mode settings.

## Install

1. Download `r1-audiobooks-1.6.16.5-audiobook.upt`.
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
