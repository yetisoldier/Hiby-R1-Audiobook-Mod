# HiBy R1 Audiobook Mod v2.0.25

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.25` after installation.

## What changed since v2.0.24

### Faster folder browsing

Opening and moving through **Audiobooks -> Folders** no longer performs several
full library scans on the UI thread. The app now uses the selected cached row
and one indexed folder query. Nested folders opened in 1-2 ms on the test R1,
and the normal folder back stack remained responsive.

### Embedded MP3 chapters

Single-file MP3 audiobooks can now expose embedded ID3v2.3/v2.4 `CHAP`/`CTOC`
chapters. Chapter titles and ordering are read during **Refresh Library**, then
chapter taps seek directly to the selected timestamp.

The parser streams metadata and skips large artwork instead of loading the
whole ID3 tag into RAM. It also has strict limits on tag size, frame count,
frame payload, and chapter count. Multipart MP3 books without embedded chapter
metadata continue to show one chapter per file.

### Larger Now Playing cover

Now Playing uses a larger 270-pixel cover. The title, author, duration, and
progress section has moved down into the newly available lower space while the
playback controls remain in their familiar positions. Long titles wrap without
overlapping progress or controls.

### Development tooling

ADB screenshots now capture the framebuffer page that is actually visible,
which avoids saving a stale boot screen when the display uses its second
buffer. Generated MP3 chapter fixtures are included in the complete local
sanity suite.

## Included features

- Dedicated Audiobooks app with Continue, Titles, Authors, Series, Folders,
  Finished, and Refresh Library.
- Per-book and multipart resume with a 5-second smart rewind.
- Authoritative SD-card position checkpoints every 15 seconds, plus immediate
  saves on pause, stop, completion, and app exit.
- MP3 and M4B/AAC playback.
- Cover art, descriptions, embedded chapters, bookmarks, sleep timer, and
  1.0x through 2.0x pitch-preserving speed control.
- Physical play/pause, previous/next, and hold-to-ramp volume controls.
- Wired and Bluetooth playback with Bluetooth SBC XQ.
- App-scoped SD runtime-power protection.
- USB DAC mode and Native DSD unlocks.
- Boot ADB while USB working mode is Device.

## Installation

1. Download `r1-audiobooks-2.0.25.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the R1 SD card.
4. On the R1, choose System -> Firmware update -> Via SD-card.
5. Confirm the update and wait for the reboot.
6. Delete or rename `r1.upt` after the update.
7. Open Audiobooks and tap **Refresh Library**.

Recommended layout:

```text
/Music
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/Book Title.m4b
```

Album = book title and Artist/Album Artist = author are recommended, but folder
and file names are used as fallbacks. The Genre tag does not need to be
`Audiobook`; location under `/Audiobooks` is authoritative.

## Verification

The production package passed the complete local sanity suite and strict rootfs
verification. All 5,488 stock paths and modes, 482 symlinks, root ownership,
launcher integration, feature markers, audio unlocks, OTA metadata, and
known-bad package checks passed.

On-device testing covered nested folder navigation, embedded MP3 chapter
discovery and direct seeking, long-title wrapping, progress scrubbing, physical
controls, repeated app entry/exit, memory plateau, reboot, boot ADB, and direct
resume.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- Run Refresh Library after upgrading to cache embedded MP3 chapters.
- ADB and USB DAC cannot be active simultaneously because they share the USB
  gadget controller. Select Device mode for ADB or DAC mode for USB audio.
- The experimental UTF-8/Cyrillic changes from v2.0.20 are not included.

Keep a copy of official stock HiBy R1 1.6 firmware for recovery. This is an
unofficial community modification tested on one normal R1, so install at your
own risk.
