HiBy R1 Audiobook Mod v2.0.22
=============================

A stability-focused update for the normal HiBy R1 on stock firmware 1.6.
Do not install it on the R1 MIDI or another HiBy model.

Install over any earlier v2.0.x release. Your library, resume positions,
bookmarks, and Bluetooth pairings are preserved. The About screen shows
"HiBy R1 2.0.22" after installation.

What is new
-----------

More reliable controls and volume
- Physical play/pause and volume controls now use an ordered player command
  queue, so quick presses are not lost or applied against stale player state.
- Holding Volume Up or Volume Down ramps continuously.
- A temporary percentage overlay confirms volume changes on screen.
- Wired and Bluetooth volume are tracked separately. Bluetooth mixer reads are
  retried to prevent startup and pause/resume volume jumps.
- Bluetooth pause/resume remains clear without the previously reported garbled
  restart.

Smoother playback and resume
- Added 2.0x playback speed alongside 1.0, 1.1, 1.25, and 1.5x.
- Player state, track, and position are published as one locked snapshot so
  progress, bookmarks, and UI state cannot disagree during rapid input.
- SD-card read failures no longer look like a completed book and cannot replace
  the last good resume position.
- Multipart chapter labels begin with "Part N" and can wrap, making repeated or
  long chapter titles easier to tell apart.

Responsive Refresh Library
- Refresh Library runs on a worker connection while playback and controls stay
  responsive.
- The screen shows that a refresh is running and confirms success or failure.
- Scans commit as one transaction. A failed scan rolls back instead of leaving
  a partial catalog or stale database journal.
- Resume writes remain authoritative on the SD card and do not block playback
  while the scanner briefly owns the database writer lock.
- Repeated refresh testing showed stable task and available-memory counts.

Better browsing and resilience
- Folders now follows the actual directory hierarchy below /Audiobooks.
- Catalog integrity is checked at open. A malformed database is quarantined so
  the app can rebuild cleanly instead of freezing the player.
- Input remains responsive after turning the screen off with the power button.
- Leaving the app during a refresh is guarded until the scan closes cleanly.

Compatibility note for v2.0.20
------------------------------

The public v2.0.20 tag was built from an experimental UTF-8/Cyrillic side
branch. That text-rendering experiment is not included in v2.0.22 because this
release follows the separately tested stability line. If you rely on Cyrillic
audiobook filenames or metadata, remain on v2.0.20 for now.

Install
-------

1. Download r1-audiobooks-2.0.22.upt.
2. Rename it to exactly r1.upt.
3. Copy r1.upt to the root of the R1 SD card.
4. On the R1, open System -> Firmware Update -> Local and install it.
5. Wait for the successful update and reboot.
6. Delete or rename r1.upt on the SD card after installation.
7. Open Audiobooks and tap Refresh Library if this is a new SD card or its
   /Audiobooks contents changed.

Recommended folders
-------------------

/Music
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/Book Title.m4b

Album = book title and Artist/Album Artist = author give the best results, but
folder and file names are used as fallbacks. Genre does not need to be tagged
as Audiobook; placement under /Audiobooks is what matters.

Verification
------------

- Package size: 42,213,376 bytes
- MD5: d5dfdf3e0977d9339ab0ae862f4b3bf5
- SHA256: 28dd05c76b203ea29298a7a59eafc036431e1c5b18760455913e751048f7f141
- Tested on the project R1 with wired and Bluetooth playback, pause/resume,
  rapid physical-button presses, held volume ramp, all playback speeds,
  multipart resume, Refresh Library during playback, repeated refreshes,
  exit/reopen, clean reboot, and persistent ADB.

Known limitations
-----------------

- Audiobook playback stops when leaving the audiobook app for the launcher.
- There is no audiobook search screen; use Titles, Authors, Series, or Folders.
- ADB and USB DAC share the USB controller. Select Device mode for ADB or DAC
  mode for USB audio.
- This unofficial firmware is tested on one normal R1. Keep the official stock
  1.6 firmware available for recovery.

Feedback and bug reports:
https://github.com/yetisoldier/Hiby-R1-Audiobook-Mod/issues
