# HiBy R1 Audiobook Mod v1.4.0

Fifth public release for the normal HiBy R1 on stock firmware 1.6. Do not install this package on the R1 MIDI.

## Firmware Package

- File: `r1-audiobooks-1.6.15-audiobook.upt`
- Firmware marker: `1.6.15-audiobook`
- MD5: `8f3ecb1f377493b84dbe80d947c89ecd`
- SHA256: `fa637ed2e4d6f21bf77014f6fc9bbcb9aed10aa6b3b58b8c52dd387465f639dc`
- Installed verification artifacts: `work/installed-release-verification/20260611-200902`

## Highlights

- Keeps the Audiobooks launcher entry, separated audiobook catalog, normal Now Playing screen, and per-book resume from previous releases.
- Prevents duplicate DB watcher instances from running at the same time.
- Skips same-size media DB timestamp churn during music and audiobook playback instead of running extra DB maintenance.
- Reduces the chance that normal music playback inherits audiobook resume/database lag.
- Hardens the ADB staging helper so failed uploads clean up temporary firmware files.
- Extends installed-release verification to check the DB watcher lock and mtime-only skip logic.
- Preserves the 15-second audiobook save guard: an audiobook must play for at least 15 seconds before its current position is remembered.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.15-audiobook.upt` to the SD-card root.
3. Rename the copied file to exactly `r1.upt`.
4. Run the firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename SD-root `r1.upt`.
6. Go to Music and run `Update Database`, then wait for the scan to complete.
7. Open Audiobooks.

## SD Card Layout

Use these folders:

```text
/Music
/Audiobooks
```

Recommended audiobook layout:

```text
/Audiobooks/Author/Year - Book Title/01 - Chapter.mp3
/Audiobooks/Author/Year - Book Title/02 - Chapter.mp3
```

Single-file `.m4b` books should also work.

For best results, set the album tag to the book title and the artist/album artist to the author. The genre tag does not need to be exactly `Audiobook`; files under `/Audiobooks` are normalized for the Audiobooks section by the on-device helper. The firmware can fall back to folder and file names when tags are not perfect.

## Verification

This build was flashed on the test R1 on 2026-06-11. Installed verification passed with:

- Version marker and resource config reporting `1.6.15-audiobook`.
- Resume daemon and DB watcher running from init.
- DB watcher duplicate-process lock present in the installed runtime.
- One DB watcher process active, with the lock PID matching that watcher.
- SD-root `r1.upt` absent after cleanup.
- `/usr/data` free space around 27 MB after cleanup.
- 135 audiobook media rows across six books.
- No audiobook leakage into Music search, album, or genre tables.
- No known development artifacts remaining under `/usr/data/audiobooks`.

ADB smoke testing after flashing showed normal music playback with zero audiobook position reads/saves, then Audiobooks playback restored `Holidays on Ice` to its saved point. The DB watcher skipped music and audiobook mtime-only DB churn instead of running extra maintenance.

## Known Quirks

- Back from the Audiobooks title list first lands on the stock Genres page; a second Back returns to the launcher.
- Multipart resume may briefly show or play nearby files while the daemon corrects to the saved file.
- A resume position is only saved after at least 15 seconds of audiobook playback.
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.15-`.
- This replaces the old text Books feature with Audiobooks.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own risk.
