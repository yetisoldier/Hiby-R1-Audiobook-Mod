# HiBy R1 Audiobook Mod v1.0.0

First public release for the normal HiBy R1 on stock firmware 1.6. Do not install this package on the R1 MIDI.

## Firmware Package

- File: `r1-audiobooks-1.6.4-audiobook.upt`
- Firmware marker: `1.6.4-audiobook`
- MD5: `71c8d0d94bf50529a06aa9a31350f595`
- SHA256: `02b286676d93ec683307820e1ef40288f34ef21a42a24f5cbda361f2d3733b7b`
- Installed verification artifacts: `work/installed-release-verification/20260610-153805`

## Highlights

- Renames the stock Books launcher entry to Audiobooks.
- Opens an audiobook title list from the main launcher.
- Starts audiobook playback through the stock Now Playing screen.
- Saves independent per-book resume positions, including multipart books.
- Keeps `/Audiobooks` files out of normal Music Albums, Genres, and Search.
- Rebuilds the media database on-device from `/Music` and `/Audiobooks` when needed.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.4-audiobook.upt` to the SD-card root.
3. Rename the copied file to `r1.upt`.
4. Run the firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename SD-root `r1.upt`.

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

Run the normal on-device Music scan/update after changing SD cards or adding new files, then wait about a minute or reboot once.

## Known Quirks

- Back from the Audiobooks title list first lands on the stock Genres page; a second Back returns to the launcher.
- Multipart resume may briefly show the track list or advance through tracks while landing on the saved file.
- The About screen may truncate the visible suffix to something like `1.6.4-a`.
- ADB does not persist in practice on the test device; manually re-enable it after reboot/update when verifying.
