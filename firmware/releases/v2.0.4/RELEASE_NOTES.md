HiBy R1 Audiobook Mod v2.0.4
============================

Fixes a library-scan hang that affected users with large audiobook
libraries. This is a firmware update for the HiBy R1; it installs the
in-process audiobook app (the "Audiobooks" tile) alongside the stock
player.

What's fixed
------------

- Large-library scan no longer hangs. When refreshing the library, M4B
  books with large "moov" metadata atoms (15 MB and up, common in long
  single-file audiobooks) caused the scanner to run out of memory and
  freeze on the "Scanning library..." screen. The chapter parser now
  memory-maps the moov atom instead of loading it all into RAM, so only
  the few KB of chapter data it actually needs are read. Scans that
  previously froze now complete in a couple of minutes with full chapter
  data intact.

- Storage-full protection. Before scanning, the app checks that the
  internal data partition has at least 1 MB free (the partition is
  chronically near-full because of the stock music database). If it is
  genuinely full, the scan aborts cleanly with a red "Scan failed:
  storage full" message instead of silently writing a half-finished
  library. The library database is also compacted (VACUUM) after each
  scan prunes removed books.

Installing
----------

1. Copy r1-audiobooks-2.0.4.upt to the root of your SD card.
2. On the R1: System -> Firmware Update -> Local, and select the file.
3. After the reboot, open Settings -> About to confirm it reads
   "HiBy R1 2.0.4".
4. Open the Audiobooks tile, then Home -> Refresh to scan your library.

Verify the download before flashing:
- MD5:    d6249a87a03ce57499158bd25d512061
- SHA256: 52b2a66789893e71926a335b3ba158b701e7997895fc81c255b13d82ede581c5

Requirements and notes
---------------------

- This is the NativeApp pivot build: the audiobook app runs as an
  LD_PRELOAD hook inside the stock player. It does not modify boot,
  power, or mount settings.
- Compatible with libraries mixing M4B (with chapters) and MP3.
- If upgrading from v2.0.0 (2.0 A), your existing library, position, and
  bookmarks are preserved; just run Home -> Refresh after flashing.
- A recovery image (the previous stable release) can be flashed from the
  SD card if ever needed.

See the checksum files (MD5SUMS.txt, SHA256SUMS.txt) alongside this
release for verifying the firmware image.