HiBy R1 Audiobook Mod v2.0.18
=============================

A focused fix for the HiBy R1 on stock firmware 1.6. It corrects the scanner
so long audiobooks report their real duration instead of being shown as only
a couple of hours. It installs the in-process audiobook app (the "Audiobooks"
tile) alongside the stock player. Do not install this on the R1 MIDI.

Install over v2.0.4 (or any v2.0.x). Your library, resume positions,
bookmarks, and Bluetooth pairings are preserved. Existing books keep their
resume positions; only the stored duration is corrected on the next library
refresh.

About-screen label after install: "HiBy R1 2.0.18".

What's new in 2.0.18
--------------------

Fixed: long audiobooks showed a far-too-short duration
- Some long audiobooks were listed as only a couple of hours even though they
  were far longer, so the seek bar, time remaining, and finish detection were
  all wrong. This was a scanner bug, not a playback bug - the audio itself
  played correctly end to end.
- Two independent causes, both in the scanner's tag reader (tags.c):
  1. MP3 variable-bitrate (VBR) files: the scanner estimated duration from
     the bitrate of the first audio frame only, and never read the Xing / Info
     / Fraunhofer VBRI header that stores the true frame count. For VBR books
     whose first frame is a low-bitrate silence frame, the estimate was a
     fraction of the real length. The scanner now parses the Xing / Info and
     VBRI headers (located past ID3v2 and the first frame's side info) and
     falls back to a full bitrate sweep only for true CBR files.
  2. M4B / AAC audiobooks with a large movie (moov) atom: the old parser walked
     the QuickTime atom tree using each atom's declared end, which on large
     moov atoms ran past the 256 KB read buffer and read garbage, corrupting
     the movie-header (mvhd) duration. The scanner now memory-maps the whole
     moov atom (the same helper that fixed the scan-hang on big-moov books in
     v2.0.4) and reads the mvhd duration directly, so moov-at-end and very
     large moov atoms are handled correctly.
- Also fixed: the MP3 tag reader used a 64 KB read for ID3v2 text tags, which
  was too small for a few books with very large cover-art-bearing ID3v2 tags
  (the first MPEG frame sat past 64 KB), so those books also got a wrong
  first-frame bitrate estimate. The VBR header parse now seeks to the first
  real MPEG frame regardless of ID3v2 size.
- Verified on-device against all 298 files on the test library. 44 books were
  corrected; no book regressed. Examples of the correction:
    Trilobyte:        5.1 h  -> 25.5 h
    Dad Is Fat:       2.7 h  ->  5.4 h
    Johnny:           8.9 h  -> 11.0 h
    Saint Odd:        1.9 h  ->  9.3 h
  (Books whose duration was already correct are unchanged.)

How the corrected durations show up
- The fix lives in the scanner. After install, open the Audiobooks app and
  tap "Refresh Library" on the Home screen once. The re-scan re-reads every
  book's duration and stores the corrected value. Resume positions, bookmarks,
  and the library itself are not touched; only the duration field is refreshed.
- Books you have already started keep their saved position - they do not
  restart from the beginning - and the seek bar / time-remaining now reflect
  the true length.

Everything else is unchanged from 2.0.17: the three stock-feature unlocks
(USB DAC, Native DSD, Bluetooth SBC XQ), SD-primary bookmarks, PNG and
progressive-JPEG covers, Bluetooth A2DP output with AVRCP remote and wired
fallback, SD-primary resume positions, boot ADB, and the storage-full scan
guard.

Install
-------
1. Copy r1-audiobooks-2.0.18.upt to the root of your SD card.
2. On the device, run the firmware update from the SD card and confirm.
3. The device reboots into recovery, applies the package, and reboots into
   2.0.18. The Audiobooks tile launches the audiobook app.
4. Open the Audiobooks app and tap Refresh Library once to correct the
   stored durations.

Verify the checksums with MD5SUMS.txt / SHA256SUMS.txt after copying.

Feedback and issues: please report on the GitHub repository.