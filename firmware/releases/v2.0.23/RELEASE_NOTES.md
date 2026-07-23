# HiBy R1 Audiobook Mod v2.0.23

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.23` after installation.

## What changed since v2.0.22

### Overnight SD-card freeze mitigation

An overnight freeze on the test R1 was traced to the stock Ingenic MMC driver,
not to the audiobook app running out of RAM. The SD worker faulted while the
card was resuming from runtime suspend, leaving the player blocked in SD
writeback.

While Audiobooks is open, v2.0.23 keeps the SD platform, host, and card
runtime-power controls active. Leaving the app restores the original values, so
the card still uses normal stock power saving in Music and on the launcher.
There is no global power-management change or always-running daemon.

### Less resume-write traffic

- Resume position sidecars now checkpoint every 15 seconds instead of every 5.
- The SQLite list-progress mirror is limited to once per minute.
- Pause, stop, completion, and app exit still save immediately.
- Identical back-to-back saves are skipped.

The SD sidecar remains the authoritative resume position. A book must play for
at least 15 seconds before its first periodic checkpoint.

## Included features

- Dedicated Audiobooks app with Continue, Titles, Authors, Series, Folders,
  Finished, and Refresh Library.
- Per-book and multipart resume with a 5-second smart rewind.
- MP3 and M4B/AAC playback.
- Cover art, chapters, bookmarks, sleep timer, and 1.0x through 2.0x
  pitch-preserving speed control.
- Physical play/pause, previous/next, and hold-to-ramp volume controls.
- Wired and Bluetooth playback with Bluetooth SBC XQ.
- USB DAC mode and Native DSD unlocks.
- Boot ADB while USB working mode is Device.

## Installation

1. Download `r1-audiobooks-2.0.23.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the R1 SD card.
4. On the R1, choose System -> Firmware update -> Via SD-card.
5. Confirm the update and wait for the reboot.
6. Delete or rename `r1.upt` after the update.
7. Open Audiobooks and tap Refresh Library.

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

The package passed strict rootfs and feature verification, was flashed through
the stock updater, and was tested on the project R1. ADB returned automatically;
installed hashes matched; the SD guard acquired and released correctly; resume
checkpoint and immediate-save behavior passed; Refresh Library retained all 52
test books; memory remained near 18 MB available; and the post-flash kernel log
was clean.

The original SD failure was intermittent and appeared after a long idle period.
This update directly addresses the observed kernel path, but reports from other
devices and SD cards are especially useful.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- ADB and USB DAC cannot be active simultaneously because they share the USB
  gadget controller. Select Device mode for ADB or DAC mode for USB audio.
- The experimental UTF-8/Cyrillic changes from v2.0.20 are not included.

Keep a copy of official stock HiBy R1 1.6 firmware for recovery. This is an
unofficial community modification tested on one normal R1, so install at your
own risk.
