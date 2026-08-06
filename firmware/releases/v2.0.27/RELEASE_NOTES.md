# HiBy R1 Audiobook Mod v2.0.27

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.27` after installation.

## What changed since v2.0.26

### Restored normal SD-card USB storage

The public firmware no longer starts ADB automatically at boot. Connecting the
R1 to a computer in Device mode once again presents the SD card as normal USB
storage.

The old boot-ADB retry could take control of the USB gadget after HiBy had
already configured mass storage. A dormant mass-storage connection could then
keep the SD block device busy without mounting it on the computer or the R1.
This could also make a valid audiobook appear not to play because its file was
temporarily unavailable.

### Hardened development USB tools

- Manual ADB and mass-storage changes now run through one serialized helper.
- Entering ADB mode releases stale mass-storage state before mounting the SD
  locally.
- Returning to mass storage syncs and cleanly unmounts the SD first.
- A busy filesystem is refused rather than exposed live to the computer.
- ADB shutdown uses a detached worker so stopping ADB cannot interrupt the
  transition halfway through.
- If the stock ADB helper refuses an already-mounted empty configfs during
  rollback, the hardened helper reconstructs the same FunctionFS gadget
  directly instead of leaving USB unavailable.
- Failures are rolled back where possible and logged to
  `/tmp/r1-usb-mode.log`.
- Optional development boot ADB is now explicitly marker-gated and performs a
  single delayed transition without a retry loop.

These changes do not alter audiobook playback, resume, library scanning,
chapters, bookmarks, or the user interface from v2.0.26.

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

## Installation

1. Download `r1-audiobooks-2.0.27.upt`.
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
launcher integration, USB helper modes and content, feature markers, audio
unlocks, OTA metadata, and known-bad package checks passed.

On the test R1, an audiobook correctly prevented unsafe mass-storage export
while its file was open. After leaving Audiobooks, the hardened transition
stopped ADB and Windows mounted the `HibyR1` exFAT volume through the Linux
File-Stor Gadget. A reported non-playing 1.25 GiB M4B also played normally once
the SD mount was restored.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- ADB, USB mass storage, and USB DAC cannot be active simultaneously because
  they share one USB gadget controller.
- Public firmware does not keep ADB enabled after reboot. Developers must
  enable it manually when needed.
- The experimental UTF-8/Cyrillic changes from v2.0.20 are not included.

Keep a copy of official stock HiBy R1 1.6 firmware for recovery. This is an
unofficial community modification tested on one normal R1, so install at your
own risk.
