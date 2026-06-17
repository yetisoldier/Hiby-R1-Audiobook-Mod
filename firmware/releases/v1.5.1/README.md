# HiBy R1 Audiobook Mod v1.5.1

Seventh public release for the normal HiBy R1 on stock firmware 1.6. Do not
install this package on the R1 MIDI.

This is a hotfix for `v1.5.0`.

## Firmware Package

- File: `r1-audiobooks-1.6.16.1-audiobook.upt`
- Firmware marker: `1.6.16.1-audiobook`
- MD5: `d30527750a071602a67f1eceb462f8cc`
- SHA256: `085495646039eafb496279d3ef2625671783552ad069150c3e959e5c219d7f3f`
- Installed verification artifacts: `work/installed-release-verification/20260617-160119`

## What Changed Since v1.5.0

- Fixed a new-SD-card regression where Audiobooks could show `No music found`
  after Music -> Update Database.
- The issue happened because the stock UI could read the SD-root media database
  copy at `/usr/data/mnt/sd_0/usrlocal_media.db`, while `v1.5.0` only reliably
  normalized the internal media database copies.
- The DB watcher now normalizes all active media DB copies when present:
  `/usr/data/usrlocal_media.db`, `/data/usrlocal_media.db`, and
  `/usr/data/mnt/sd_0/usrlocal_media.db`.
- All `v1.5.0` features are otherwise retained: Audiobooks icon, separated
  audiobook catalog, normal Now Playing playback, per-book resume, multipart
  resume improvements, Back cleanup, title/author/series sidecar catalogs,
  Native DSD, Bluetooth SBC XQ, and USB DAC related flags.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.16.1-audiobook.upt` to the SD-card root.
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
on-device helper.

## Verification

This build was flashed on the test R1 on 2026-06-17. Installed verification
passed with:

- Version marker and resource config reporting `1.6.16.1-audiobook`.
- Resume daemon and DB watcher running from init.
- DB watcher mirror logic present for `/data/usrlocal_media.db` and
  `/usr/data/mnt/sd_0/usrlocal_media.db`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers present.
- Play-mode guard active.
- `/usr/data` free space around 26 MB after cleanup.
- 298 audiobook media rows across 52 books on the regression SD card.
- SD-root media database integrity `ok`, with 298 normalized audiobook rows.
- Title, author, and series sidecar catalogs present.
- No audiobook leakage into Music search, album, or genre tables.
- Live UI check confirmed Audiobooks opens the title list instead of `No music
  found`.

## Known Quirks

- Back from Audiobooks is improved, but still uses a guarded workaround on top
  of the stock Genres route.
- Multipart resume may briefly show a track list or nearby file while the
  daemon corrects to the saved file and position.
- A resume position is only saved after at least 15 seconds of audiobook
  playback.
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.16.1-`.
- This replaces the old text Books feature with Audiobooks.
- USB DAC and Bluetooth SBC XQ are lightly tested compared with the audiobook
  features.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own
  risk.
