# HiBy R1 Audiobook Mod v2.0.26

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.26` after installation.

## What changed since v2.0.25

### Fixed: audiobook plays behind a black screen that will not wake

Some users found that an audiobook could keep playing after the display timed
out, while the screen would no longer wake from touch or the power button. The
screen sometimes returned only after connecting a charger.

The problem was a mismatch between two screen states. HiBy's player could fully
power down the framebuffer while the audiobook app tracked only the backlight
brightness. Audio and hardware controls stayed alive, but the app did not know
that the display itself needed to be restored.

v2.0.26 now:

- Converts HiBy's full screen blank into the audiobook app's lightweight
  screen-off mode, so audio can continue while the display is dark.
- Detects and recovers if the framebuffer was already fully blanked.
- Explicitly wakes the framebuffer before handling media or volume buttons.
- Preserves one-press power wake and touchscreen double-tap wake.

The recovery runs inside the existing UI loop. It does not add a background
service, another thread, or recurring memory allocation.

### Safer firmware builds

The firmware builder now recompiles the audiobook hook from the current source
every time it creates a NativeApp package. This prevents a valid-looking update
from accidentally including an older hook library.

The installed-release checker now understands the NativeApp architecture and
validates the wrapper, preload hook, host process, and SD library database. A
repeatable developer tool for forcing and diagnosing framebuffer blanks is also
included in the source repository.

## Included features

- Dedicated Audiobooks app with Continue, Titles, Authors, Series, Folders,
  Finished, and Refresh Library.
- Per-book and multipart resume with a 5-second smart rewind.
- Authoritative SD-card position checkpoints every 15 seconds, plus immediate
  saves on pause, stop, completion, and app exit.
- MP3 and M4B/AAC playback.
- Cover art, publisher descriptions, embedded M4B and MP3 chapters, bookmarks,
  sleep timer, and 1.0x through 2.0x pitch-preserving speed control.
- Physical play/pause, previous/next, and hold-to-ramp volume controls.
- Wired and Bluetooth playback with Bluetooth SBC XQ.
- App-scoped SD runtime-power protection.
- USB DAC mode and Native DSD unlocks.
- Boot ADB while USB working mode is Device.

## Installation

1. Download `r1-audiobooks-2.0.26.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the R1 SD card.
4. On the R1, choose System -> Firmware update -> Via SD-card.
5. Confirm the update and wait for the reboot.
6. Delete or rename `r1.upt` after the update.
7. Open Audiobooks and tap **Refresh Library** if this is a new card or its
   audiobook contents changed.

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

The exact production package was flashed to the test R1. Ten forced hard-screen
blanks each recovered and woke with one power press. Double-tap, volume,
play/pause, app exit, and stock Music navigation also passed. The player kept
the same process, thread count, and open-file count during the test.

This failure was originally reported after the player sat idle, so longer-term
and overnight testing across more devices remains valuable.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- ADB and USB DAC cannot be active simultaneously because they share the USB
  gadget controller. Select Device mode for ADB or DAC mode for USB audio.
- The experimental UTF-8/Cyrillic changes from v2.0.20 are not included.

Keep a copy of official stock HiBy R1 1.6 firmware for recovery. This is an
unofficial community modification tested on one normal R1, so install at your
own risk.
