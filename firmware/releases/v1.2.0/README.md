# HiBy R1 Audiobook Mod v1.2.0

Third public release for the normal HiBy R1 on stock firmware 1.6. Do not install this package on the R1 MIDI.

## Firmware Package

- File: `r1-audiobooks-1.6.9-audiobook.upt`
- Firmware marker: `1.6.9-audiobook`
- MD5: `3c3b3f05724acc474fb349e6378fc351`
- SHA256: `f78e67089ff84021b18d69a4af2cb01be6f872bc59d187bf9cba256f8cd792aa`
- Installed verification artifacts: `work/installed-release-verification/20260611-152156`

## Highlights

- Keeps the Audiobooks launcher entry, separated audiobook catalog, normal Now Playing screen, and per-book resume from previous releases.
- Fixes a post-flash case where Music -> Update Database could finish with no visible music tracks when the stock DB only had placeholder Music rows.
- Reduces resume-daemon work during normal music playback and non-audiobook playback.
- Fixes the Audiobooks title-list side index so alphabet jumping is based on book title instead of chapter/file title.
- Keeps audiobook files out of Music Albums, Genres, and Search after Music -> Update Database.
- Preserves the 15-second audiobook save guard: an audiobook must play for at least 15 seconds before its current position is remembered.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.9-audiobook.upt` to the SD-card root.
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

## Known Quirks

- Back from the Audiobooks title list first lands on the stock Genres page; a second Back returns to the launcher.
- Multipart resume may briefly show or play nearby files while the daemon corrects to the saved file.
- A resume position is only saved after at least 15 seconds of audiobook playback.
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.9-a`.
- This replaces the old text Books feature with Audiobooks.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own risk.
