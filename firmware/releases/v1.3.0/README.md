# HiBy R1 Audiobook Mod v1.3.0

Fourth public release for the normal HiBy R1 on stock firmware 1.6. Do not install this package on the R1 MIDI.

## Firmware Package

- File: `r1-audiobooks-1.6.11-audiobook.upt`
- Firmware marker: `1.6.11-audiobook`
- MD5: `208b7312800e4c26af79d9af7cd5570d`
- SHA256: `bd353cd343d9968c532b2df8a9fd6ee74cb2e8dff66531bbb10a55bb5734abad`
- Installed verification artifacts: `work/installed-release-verification/20260611-180112`

## Highlights

- Keeps the Audiobooks launcher entry, separated audiobook catalog, normal Now Playing screen, and per-book resume from previous releases.
- Improves title-list resume for multipart books by verifying row taps and correcting near-misses more directly.
- Adds a small memory-scan helper used by the resume daemon to identify the selected audiobook title in some title-list flows.
- Caps resume and DB maintenance log growth on the device so internal storage does not slowly fill during testing or long-term use.
- Keeps audiobook files out of Music Albums, Genres, and Search after Music -> Update Database.
- Preserves the 15-second audiobook save guard: an audiobook must play for at least 15 seconds before its current position is remembered.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.11-audiobook.upt` to the SD-card root.
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

- Version marker and resource config reporting `1.6.11-audiobook`.
- Resume daemon and DB watcher running from init.
- Log rotation code present in the installed runtime.
- SD-root `r1.upt` absent after archiving.
- `/usr/data` free space around 27 MB after cleanup.
- 135 audiobook media rows across six books.
- No audiobook leakage into Music search, album, or genre tables.
- No known development artifacts remaining under `/usr/data/audiobooks`.

A short runtime monitor after flashing showed no reboot, one resume daemon, one DB watcher, stable internal free space, and small capped logs. An ADB smoke test opened Audiobooks, started `Ice Like Fire`, restored to the saved position around 17 minutes, and paused playback afterward.

## Known Quirks

- Back from the Audiobooks title list first lands on the stock Genres page; a second Back returns to the launcher.
- Multipart resume may briefly show or play nearby files while the daemon corrects to the saved file.
- A resume position is only saved after at least 15 seconds of audiobook playback.
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.11-a`.
- This replaces the old text Books feature with Audiobooks.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own risk.
