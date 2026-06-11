# HiBy R1 Audiobook Mod v1.1.0

Second public release for the normal HiBy R1 on stock firmware 1.6. Do not install this package on the R1 MIDI.

## Firmware Package

- File: `r1-audiobooks-1.6.7-audiobook.upt`
- Firmware marker: `1.6.7-audiobook`
- MD5: `7a5b0267811de7198039aa96144f3f8c`
- SHA256: `2ac14cdd858f91af99cff8365c5d0664ca3d01233a89bc82e8ba010c7dfcbd78`
- Installed verification artifacts: `work/installed-release-verification/20260611-094702`

## Highlights

- Keeps the Audiobooks launcher entry, separated audiobook catalog, normal Now Playing screen, and per-book resume from v1.0.0.
- Adds guarded audiobook play-mode correction so multipart books are less likely to be affected by shuffle or single-track repeat settings.
- Improves multipart resume when the stock title-list route lands near the saved file but not exactly on it.
- Treats files under `/Audiobooks` as audiobooks even when their genre tag is custom or blank, after Music -> Update Database runs.
- Keeps exact resume by default. Optional smart rewind exists internally for testing but is off in this release.
- Uses lower-power idle polling when playback is not on an audiobook path.
- Improves the audiobook catalog foundation for future Author, Title, and Series views.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.7-audiobook.upt` to the SD-card root.
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
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.7-a`.
- This replaces the old text Books feature with Audiobooks.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own risk.
