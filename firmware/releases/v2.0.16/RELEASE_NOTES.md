HiBy R1 Audiobook Mod v2.0.16
=============================

A small, focused update for the HiBy R1 on stock firmware 1.6. It installs the
in-process audiobook app (the "Audiobooks" tile) alongside the stock player.
Do not install this on the R1 MIDI.

Install over v2.0.4 (or any v2.0.x). Your library, resume positions,
bookmarks, and Bluetooth pairings are preserved.

About-screen label after install: "HiBy R1 2.0.16".

What's new in 2.0.16
--------------------

Bookmarks are now stored SD-primary
- Bookmarks are saved to the SD card, not to the internal data partition, so a
  full internal partition can never lose a bookmark or refuse to add one. The
  internal partition on this device is chronically near-full (permanent UBIFS
  overhead plus the stock music database the player rebuilds every boot), and
  a bookmark add used to write to the library database there. That write is now
  gone: adding a bookmark touches only the SD card, so it can no longer trip
  the "Book not found" screen the way a failed database write could.
- Each book's bookmarks live in one tiny file on the SD card, written
  atomically (temp file then rename) so a power cut cannot corrupt an existing
  set. A damaged line is skipped on read; the worst case is losing one book's
  marks, never the whole library.
- Existing bookmarks already in the library database are moved to the SD card
  automatically the first time you open that book's bookmark screen. After that
  the database table is no longer used for bookmarks (it stays in place but is
  inert). The library database itself stays on the internal partition, which is
  power-cut-safe, unlike the SD card.
- The same SD-sidecar cleanup that drops stale resume-position files for
  removed books now also drops their stale bookmark files, so orphan bookmark
  files do not accumulate.

Everything else is unchanged from 2.0.15: PNG and progressive-JPEG covers,
Bluetooth A2DP output with AVRCP remote and wired fallback, SD-primary resume
positions, boot ADB, and the storage-full scan guard.

Install
-------
1. Copy r1-audiobooks-2.0.16.upt to the root of your SD card.
2. On the device, run the firmware update from the SD card and confirm.
3. The device reboots into recovery, applies the package, and reboots into
   2.0.16. The Audiobooks tile launches the audiobook app.

Verify the checksums with MD5SUMS.txt / SHA256SUMS.txt after copying.

Feedback and issues: please report on the GitHub repository.
