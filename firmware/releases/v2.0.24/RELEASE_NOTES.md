# HiBy R1 Audiobook Mod v2.0.24

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.24` after installation.

## What changed since v2.0.23

### Publisher descriptions

Book detail pages can now show the publisher summary stored in:

- MP3 Comment (`COMM`)
- M4B Description (`desc` or `ldes`)

The summary uses the full screen width between the cover and progress area.
Progress now sits immediately above the bottom-anchored Play, Chapters,
Bookmarks, and Menu buttons. Common HTML tags, line breaks, extra whitespace,
and Audible-style punctuation are cleaned up during the library scan.

Run **Audiobooks -> Refresh Library** once after upgrading so descriptions are
added to books already in the catalog. Books without description metadata
continue to work normally.

### Light and dark theme icon

The launcher now uses HiBy's stock theme-specific Books icon while retaining
the Audiobooks label and behavior. This avoids the black square/background that
the previous custom icon could show in light mode.

### Faster, reliable return to the launcher

Leaving Audiobooks no longer relies on HiBy eventually repainting two
framebuffer pages that the app had cleared. The app preserves the launcher
frame, restores it immediately on exit, and briefly watches HiBy's first
double-buffer redraws so a hidden navigation frame is panned onscreen.

On the test R1:

- Idle exit returned in about 590 ms.
- Exit while a book was playing returned in about 760 ms.
- The first launcher tap displayed immediately.
- No second tap or power-button screen cycle was needed.
- The temporary 750 KiB frame snapshot and handoff thread cleaned up normally.

## Included features

- Dedicated Audiobooks app with Continue, Titles, Authors, Series, Folders,
  Finished, and Refresh Library.
- Per-book and multipart resume with a 5-second smart rewind.
- Authoritative SD-card position checkpoints every 15 seconds, plus immediate
  saves on pause, stop, completion, and app exit.
- MP3 and M4B/AAC playback.
- Cover art, descriptions, chapters, bookmarks, sleep timer, and 1.0x through
  2.0x pitch-preserving speed control.
- Physical play/pause, previous/next, and hold-to-ramp volume controls.
- Wired and Bluetooth playback with Bluetooth SBC XQ.
- App-scoped SD runtime-power protection from v2.0.23.
- USB DAC mode and Native DSD unlocks.
- Boot ADB while USB working mode is Device.

## Installation

1. Download `r1-audiobooks-2.0.24.upt`.
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
verification. All 5,488 stock paths, modes, symlinks, root ownership, launcher
integration, feature markers, audio unlocks, OTA metadata, and known-bad
package checks passed.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- Descriptions require supported metadata and a Refresh Library scan.
- ADB and USB DAC cannot be active simultaneously because they share the USB
  gadget controller. Select Device mode for ADB or DAC mode for USB audio.
- The experimental UTF-8/Cyrillic changes from v2.0.20 are not included.

Keep a copy of official stock HiBy R1 1.6 firmware for recovery. This is an
unofficial community modification tested on one normal R1, so install at your
own risk.
