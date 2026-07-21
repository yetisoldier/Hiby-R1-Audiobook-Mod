HiBy R1 Audiobook Mod v2.0.20
=============================

A feature release for the HiBy R1 on stock firmware 1.6. It adds UTF-8 /
Cyrillic support for audiobook filenames and metadata tags, and changes the
Now Playing Prev / Next buttons from track-skip to a relative seek (rewind
30 s / forward 60 s). It installs the in-process audiobook app (the
"Audiobooks" tile) alongside the stock player. Do not install this on the R1
MIDI.

Install over v2.0.4 (or any v2.0.x). Your library, resume positions,
bookmarks, and Bluetooth pairings are preserved. Existing books keep their
resume positions; no re-scan is required for the new features (filenames and
tags are read live).

About-screen label after install: "HiBy R1 2.0.20".

What's new in 2.0.20
-------------------

UTF-8 / Cyrillic support for filenames and tags
- The audiobook app now renders Cyrillic (and other non-Latin) text correctly
  instead of `???????`. The device font (msyh.ttf, Microsoft YaHei) already
  contained the Cyrillic glyphs (U+0400-U+04FF); the limitation was entirely
  in the app's own C code, not the font.
- A new pure-C helper (utf8.c) provides UTF-8 decode, UTF-8 boundary-safe
  truncate/append, UTF-16 -> UTF-8 (BOM-aware, handles surrogate pairs), and
  Windows-1251 -> UTF-8 (embedded WHATWG table).
- The font glyph cache is now a bounded LRU keyed by codepoint (cap 512)
  instead of a fixed 95-slot ASCII array, so any codepoint the font holds can
  render. The old "non-ASCII -> '?'" clamp is gone.
- Text drawing and measuring walk UTF-8 codepoints; render_text_wrap is now
  pixel/codepoint-aware (wraps at spaces, never splits a multi-byte sequence
  mid-codepoint).
- ID3 tag decoding handles encodings 0/1/2/3: UTF-16 -> UTF-8 (1/2), UTF-8 copy
  (3), and encoding-0 frames with high bytes are treated as Windows-1251 ->
  UTF-8 (the common Russian-MP3 convention; a documented heuristic).
- All fixed char[] buffer copies in scan/library/ui are now UTF-8-boundary
  safe, and UI list truncation appends a U+2026 ellipsis (...) cleanly without
  splitting a glyph.
- Verified on-device with a Windows-1251 ID3v2.3 tag: author Ivan Petrov,
  track Pervaya glava, album Sbornik render correctly from both the tag and a
  Cyrillic folder/filename. FTS5 search finds Cyrillic text; no "?" anywhere;
  no data loss.
- Scope is the audiobook app only. The stock HiBy music app (hiby_player) is a
  closed binary and is unchanged; its non-Latin handling is a known limitation
  outside this mod's scope.

Now Playing skip buttons: relative seek
- Prev / Next on the Now Playing screen now seek relative to the current
  position instead of skipping tracks: Prev rewinds 30 s, Next advances 60 s
  (clamped to [0, total]). Useful for re-hearing a sentence or jumping past
  an intro within a long chapter. Track navigation is still available via the
  chapter list.

Diagnostics
- setup_alsa now logs a single diagnostic line naming which
  snd_pcm_hw_params_set_* call failed (or that all succeeded and only the
  constraint commit rejected the buffer/period combo) when
  snd_pcm_hw_params errors. It fires only on failure - zero overhead on the
  success path - so any future ALSA hw_params error is self-describing in the
  hook log instead of a bare "failed: -22". Audio playback is healthy on a
  clean install; a hw_params -22 seen during testing was stale DAC state from
  rapid re-flashing, cleared by a normal reboot - not a code bug.

Everything else is unchanged from 2.0.19: the multi-file chapter-list fix, the
long-audiobook duration fix, the three stock-feature unlocks (USB DAC,
Native DSD, Bluetooth SBC XQ), SD-primary bookmarks, PNG and progressive-JPEG
covers, Bluetooth A2DP output with AVRCP remote and wired fallback,
SD-primary resume positions, boot ADB, and the storage-full scan guard.

Install
-------
1. Copy r1-audiobooks-2.0.20.upt to the root of your SD card.
2. On the device, run the firmware update from the SD card and confirm.
3. The device reboots into recovery, applies the package, and reboots into
   2.0.20. The Audiobooks tile launches the audiobook app.
4. No re-scan is required. (If you want the v2.0.18 duration fix or the v2.0.19
   chapter-list fix applied to books added before those releases, tap Refresh
   Library once.)

Verify the checksums with MD5SUMS.txt / SHA256SUMS.txt after copying.

Feedback and issues: please report on the GitHub repository.