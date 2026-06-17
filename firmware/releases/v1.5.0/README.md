# HiBy R1 Audiobook Mod v1.5.0

Sixth public release for the normal HiBy R1 on stock firmware 1.6. Do not
install this package on the R1 MIDI.

## Firmware Package

- File: `r1-audiobooks-1.6.16-audiobook.upt`
- Firmware marker: `1.6.16-audiobook`
- MD5: `4938a5d3f74204995a1bb297175da463`
- SHA256: `ba3b16dc63e35abfc22cd0ac9e4324a5a2e3834ad894c42fd310f30f99c3f1e0`
- Installed verification artifacts: `work/installed-release-verification/20260617-151416`

## Highlights

- Keeps the Audiobooks launcher entry, separated audiobook catalog, normal Now
  Playing screen, per-book resume, and self-contained on-device database
  maintenance from previous releases.
- Adds a dedicated Audiobooks launcher icon.
- Improves Back from the Audiobooks title/list area so one Back usually returns
  to the main launcher instead of stopping on the stock Genres page.
- Improves multipart resume from the title list, including faster saved-file
  correction and a settle guard before seeking to the saved position.
- Hardens audiobook switching while another audiobook is already playing, so a
  previous book is less likely to affect the next title start.
- Reduces steady audiobook resume writes to a 15-second cadence while keeping
  the 15-second save guard.
- Improves fresh SD-card and late-mount behavior after Music -> Update Database.
- Adds internal title, author, and series sidecar catalogs for future UI work
  and easier debugging. The visible Audiobooks UI remains the title list.
- Enables Native DSD, Bluetooth SBC XQ, and USB DAC related flags.
- Preserves the rule that audiobooks under `/Audiobooks` stay out of normal
  Music Albums, Genres, and Search.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16-audiobook.upt` to the SD-card root.
3. Rename the copied file to exactly `r1.upt`.
4. Run the firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename SD-root
   `r1.upt`.
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

For best results, set the album tag to the book title and the artist/album
artist to the author. The genre tag does not need to be exactly `Audiobook`;
files under `/Audiobooks` are normalized for the Audiobooks section by the
on-device helper. The firmware can fall back to folder and file names when tags
are not perfect.

Optional series-friendly layout:

```text
/Audiobooks/Author/Series/2020 - Book Title [Series 02]/01 - Chapter.mp3
/Audiobooks/Author/2021 - Standalone Book/01 - Chapter.mp3
```

Standalone books do not need a fake series folder.

## Audio Unlock Notes

- Native DSD does not add a new Audiobooks control. Play DSD files normally.
- Bluetooth SBC XQ is automatic when Bluetooth audio uses SBC and the receiving
  device supports it.
- USB DAC related options are exposed through the stock Settings/System USB
  working mode area. On the test R1, setting USB working mode to Auto, rebooting
  cleanly, then connecting a phone as the USB source allowed the R1 to receive
  audio and play it out through its selected output.

## Verification

This build was flashed on the test R1 on 2026-06-17. Installed verification
passed with:

- Version marker and resource config reporting `1.6.16-audiobook`.
- Resume daemon and DB watcher running from init.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers present.
- Play-mode guard active.
- SD-root `r1.upt` absent after cleanup.
- `/usr/data` free space around 30 MB after cleanup.
- 135 audiobook media rows across six books.
- Title, author, and series sidecar catalogs present.
- No audiobook leakage into Music search, album, or genre tables.
- No known development artifacts remaining under `/usr/data/audiobooks`.

## Known Quirks

- Back from Audiobooks is improved, but still uses a guarded workaround on top
  of the stock Genres route.
- Multipart resume may briefly show a track list or nearby file while the
  daemon corrects to the saved file and position.
- A resume position is only saved after at least 15 seconds of audiobook
  playback.
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.16-`.
- This replaces the old text Books feature with Audiobooks.
- USB DAC and Bluetooth SBC XQ are lightly tested compared with the audiobook
  features.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own
  risk.
