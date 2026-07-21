HiBy R1 Audiobook Mod v2.0.19
=============================

A focused fix for the HiBy R1 on stock firmware 1.6. It corrects the chapter
list for audiobooks that are split into one file per chapter (a common way to
buy audiobooks), where every chapter was listed as "Chapter 1" with a duration
of 0:00 and tapping a chapter did not jump to the right place. It installs the
in-process audiobook app (the "Audiobooks" tile) alongside the stock player.
Do not install this on the R1 MIDI.

Install over v2.0.4 (or any v2.0.x). Your library, resume positions,
bookmarks, and Bluetooth pairings are preserved. Existing books keep their
resume positions; the chapter list is rebuilt on the next library refresh.

About-screen label after install: "HiBy R1 2.0.19".

What's new in 2.0.19
-------------------

Fixed: multi-file audiobooks showed "Chapter 1" / 0:00 for every chapter
- For an audiobook split into one audio file per chapter (for example a book
  bought as a folder of per-chapter files), the Chapters list showed every
  entry as "Chapter 1" with a duration of 0:00, and tapping a chapter did not
  seek to the right spot in the book.
- Two related scanner bugs caused this, both in the chapter-indexing code
  (scan.c):
  1. For multi-file M4A / M4B books with no embedded chapter metadata, the
     scanner took a code path that wrote a single "Chapter 1" placeholder for
     each file instead of one chapter per file named after the file, so every
     row literally read "Chapter 1".
  2. For multi-file MP3 books, each file's chapter row was stored with a
     start position of 0:00 instead of its real position within the whole
     book, so every chapter showed 0:00 and chapter-tap seek always jumped
     back to the first file.
- The scanner now gives every multi-file book one chapter per file, named
  after the file (its title tag, or the file name if untitled), spanning that
  file's real position within the whole book. Chapter positions are now
  cumulative across the files, so the list shows real timestamps and tapping
  a chapter seeks to the correct file and position. Embedded chapters in a
  single-file M4B are also offset to whole-book positions when the book is part
  of a multi-file set.
- Single-file audiobooks with no embedded chapters now get a single
  "Chapter 1" entry covering the whole file, so every book has a populated
  Chapters list. (Before, some single-file books had no chapters listed at
  all.)

Hardening
- Added a cap on the number of placeholder chapters a single file can
  synthesize (1024), and a cap on the number of chapter rows the chapter list
  will render or accept taps for (2048). These guard against a malformed file
  reporting a huge chapter count, which could exhaust the device's memory and
  freeze it. This is defensive hardening only; it does not change normal
  behavior.

How the fixed chapter list shows up
- The fix lives in the scanner. After install, open the Audiobooks app and
  tap "Refresh Library" on the Home screen once. The re-scan rebuilds the
  chapter index for every book. Resume positions, bookmarks, and the library
  itself are not touched.
- Books you have already started keep their saved position. They do not
  restart from the beginning.

Note on a reported freeze
- A user also reported that, after browsing chapters for one such multi-file
  book for about a minute, the screen went black and the device became
  unresponsive. That symptom could not be reproduced on the test device (its
  multi-file books are MP3, while the report was on an M4A book), so the
  exact cause is not yet confirmed. The memory-exhaustion hardening above
  addresses the most plausible cause. If you still hit a freeze, please note
  the book format (MP3 / M4A / M4B) and report it on the GitHub repository so
  it can be tracked down.

Everything else is unchanged from 2.0.18: the long-audiobook duration fix, the
three stock-feature unlocks (USB DAC, Native DSD, Bluetooth SBC XQ),
SD-primary bookmarks, PNG and progressive-JPEG covers, Bluetooth A2DP output
with AVRCP remote and wired fallback, SD-primary resume positions, boot ADB,
and the storage-full scan guard.

Install
-------
1. Copy r1-audiobooks-2.0.19.upt to the root of your SD card.
2. On the device, run the firmware update from the SD card and confirm.
3. The device reboots into recovery, applies the package, and reboots into
   2.0.19. The Audiobooks tile launches the audiobook app.
4. Open the Audiobooks app and tap Refresh Library once to rebuild the
   chapter lists.

Verify the checksums with MD5SUMS.txt / SHA256SUMS.txt after copying.

Feedback and issues: please report on the GitHub repository.