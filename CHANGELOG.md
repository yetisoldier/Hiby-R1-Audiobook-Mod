# Changelog

All public releases are for the normal HiBy R1 on stock firmware 1.6. Do not install these packages on the R1 MIDI.

## v2.0.20 - 2026-07-21

Firmware marker: `2.0.20` - About-screen label `HiBy R1 2.0.20`.

Adds UTF-8 / Cyrillic support for audiobook filenames and metadata tags,
and changes the Now Playing Prev/Next buttons from track-skip to relative
seek. Install over v2.0.4 (or any v2.0.x); library, resume positions,
bookmarks, and BT pairings are preserved.

### UTF-8 / Cyrillic support (filenames and tags)

The audiobook app now renders Cyrillic (and other non-Latin) text
correctly instead of `???????`. The device font (`msyh.ttf`, Microsoft
YaHei) already contained the glyphs; the limitation was entirely in the
app's own C code:

- A new pure-C helper (`utf8.{c,h}`) provides UTF-8 decode, UTF-8
  boundary-safe truncate/append, UTF-16 -> UTF-8 (BOM-aware, surrogates),
  and Windows-1251 -> UTF-8 (embedded WHATWG table).
- The font glyph cache is now a bounded LRU keyed by codepoint (cap 512)
  instead of a fixed 95-slot ASCII array, so any codepoint the font holds
  can render. The old `non-ASCII -> '?'` clamp is gone.
- Text drawing and measuring walk UTF-8 codepoints; `render_text_wrap`
  is pixel/codepoint-aware (wraps at spaces, never mid-codepoint).
- ID3 tag decoding handles encodings 0/1/2/3: UTF-16 -> UTF-8 (1/2), UTF-8
  copy (3), and encoding-0 frames with high bytes are treated as
  Windows-1251 -> UTF-8 (the common Russian-MP3 convention).
- All fixed `char[]` buffer copies in scan/library/ui are now
  UTF-8-boundary-safe, and UI list truncation appends a U+2026 ellipsis
  (`…`) cleanly without splitting a glyph.

Verified on-device with a Windows-1251 ID3v2.3 tag: author `Иван Петров`,
track `Первая глава`, album `Сборник` render correctly from both the tag
and a Cyrillic folder/filename. FTS5 search finds Cyrillic text; no `?`
anywhere; no data loss (52 -> 53 books). Scope is the audiobook app only;
the stock music app (`hiby_player`, closed binary) is unchanged.

### Now Playing skip buttons: relative seek

Prev / Next on the Now Playing screen now seek relative to the current
position instead of skipping tracks: Prev rewinds 30 s, Next advances
60 s (clamped to [0, total]). Useful for re-hearing a sentence or jumping
past an intro within a long chapter. Track navigation is still available
via the chapter list.

### Diagnostics

`setup_alsa` now logs a single diagnostic line naming which
`snd_pcm_hw_params_set_*` call failed (or that all succeeded and only the
constraint commit rejected the buffer/period combo) when `snd_pcm_hw_params`
errors. It fires only on failure - zero overhead on the success path. This
makes any future ALSA `hw_params` error self-describing in the hook log
instead of a bare `failed: -22`. (Audio playback is healthy on a clean
install; a prior `hw_params -22` seen during testing was stale DAC state
from rapid re-flashing, cleared by a normal reboot - not a code bug.)

Everything else is unchanged from 2.0.19: the multi-file chapter-list fix,
the three stock-feature unlocks (USB DAC, Native DSD, Bluetooth SBC XQ),
SD-primary bookmarks, PNG and progressive-JPEG covers, Bluetooth A2DP
output with AVRCP remote and wired fallback, SD-primary resume positions,
boot ADB, and the storage-full scan guard.

## v2.0.19 - 2026-07-21

Firmware marker: `2.0.19` - About-screen label `HiBy R1 2.0.19`.

Fixes the chapter list for audiobooks split into one file per chapter,
where every chapter was listed as "Chapter 1" with a duration of 0:00
and tapping a chapter did not jump to the right place. This was a scanner
bug (scan.c), not a playback bug. Install over v2.0.4 (or any v2.0.x);
library, resume positions, bookmarks, and BT pairings are preserved.
Existing books keep their resume positions; the chapter list is rebuilt
on the next library refresh.

### Fixed: multi-file books showed "Chapter 1" / 0:00 (scan.c)

Two related scanner bugs:

- For multi-file M4A / M4B books with no embedded chapter metadata, the
  `is_m4b` code path wrote a single "Chapter 1" placeholder for each file
  instead of one chapter per file named after the file, so every row
  literally read "Chapter 1".
- For multi-file MP3 books, each file's chapter row was stored with a
  start position of 0:00 instead of its cumulative position within the
  whole book, so every chapter showed 0:00 and chapter-tap seek always
  jumped back to the first file.

The scanner now gives every multi-file book one chapter per file, named
after the file (its title tag, or the file name if untitled), spanning
that file's real cumulative position within the whole book
(`[book_pos_ms, book_pos_ms + duration]`). `book_offset_ms` is threaded
through `scan_chapter_cb` so embedded chapters in a single-file M4B that
is part of a multi-file set are also offset to whole-book positions.
Single-file audiobooks with no embedded chapters now get a single
"Chapter 1" entry covering the whole file, so every book has a populated
Chapters list.

### Hardening

- `SYNTH_CHAPTER_CAP = 1024` caps placeholder chapters a single file can
  synthesize.
- `MAX_CHAPTER_ROWS = 2048` caps chapter rows the chapter list renders or
  accepts taps for.

Both guard against a malformed file reporting a huge chapter count that
could exhaust the device's memory and freeze it. Defensive only; normal
behavior unchanged.

Verified on-device: cumulative chapter start_ms across multi-file books
(e.g. Food: A Love Story `[0, 32026, 73064, 177109, 414144, 634173, ...]`),
per-file titles, 993 -> 1012 chapters, 52 books / 298 tracks, no data loss.
A user-reported freeze while browsing chapters of an M4A multi-file book
could not be reproduced (test library's multi-file books are MP3); the
memory-exhaustion hardening addresses the most plausible cause.

Everything else is unchanged from 2.0.18: the long-audiobook duration fix,
the three stock-feature unlocks (USB DAC, Native DSD, Bluetooth SBC XQ),
SD-primary bookmarks, PNG and progressive-JPEG covers, Bluetooth A2DP
output with AVRCP remote and wired fallback, SD-primary resume positions,
boot ADB, and the storage-full scan guard.

## v2.0.18 - 2026-07-20

Firmware marker: `2.0.18` - About-screen label `HiBy R1 2.0.18`.

Fixes the scanner so long audiobooks report their real duration instead of
being shown as only a couple of hours. This was a scanner (tag-reader) bug,
not a playback bug - the audio itself played correctly end to end. Install
over v2.0.4 (or any v2.0.x); library, resume positions, bookmarks, and BT
pairings are preserved. After install, open the Audiobooks app and tap
Refresh Library once so the corrected durations are stored.

### Fixed: long-audiobook duration under-reporting (tags.c)

Two independent causes, both in the scanner's tag reader:

- **MP3 VBR duration**: the scanner estimated duration from the bitrate of the
  first audio frame only and never read the Xing / Info / Fraunhofer VBRI header
  that stores the true frame count. For VBR books whose first frame is a
  low-bitrate silence frame, the estimate was a fraction of the real length.
  The scanner now parses the Xing / Info header (at first-frame + 4 + side
  info: 32 bytes stereo / 17 mono for MPEG-1, 17 / 9 for MPEG-2 / 2.5) and the
  VBRI header (fixed at first-frame + 4 + 32), computing duration as
  `frames * samples_per_frame * 1000 / sample_rate`, and falls back to a full
  bitrate sweep only for true CBR files. The ID3v2 syncsafe size in the first
  10 bytes is read first so the first MPEG frame is found regardless of ID3v2
  size (some books had cover-art-bearing ID3v2 tags over 64 KB, which the old
  64 KB read could not pass).
- **M4B / AAC large-moov duration**: the old `parse_qt_atoms` walker used each
  atom's declared end to recurse, which on large `moov` atoms ran past the
  256 KB read buffer and read garbage, corrupting the `mvhd` duration. The
  scanner now memory-maps the whole `moov` atom via the existing `read_moov`
  helper (the same mmap that fixed the big-moov scan-hang in v2.0.4) and reads
  the `mvhd` duration directly via `qt_find_child`, so moov-at-end and very
  large moov atoms are handled correctly.

Verified on-device against all 298 files on the test library: 44 books
corrected, no regressions. Examples: Trilobyte 5.1 h -> 25.5 h; Dad Is Fat
2.7 h -> 5.4 h; Johnny 8.9 h -> 11.0 h; Saint Odd 1.9 h -> 9.3 h. Books whose
duration was already correct are unchanged.

The fix lives in the scanner; playback, resume, and bookmarks are untouched.
A re-scan (Refresh Library) re-reads every book's duration and stores the
corrected value; it does not restart in-progress books.

Everything else is unchanged from 2.0.17: the three stock-feature unlocks
(USB DAC, Native DSD, Bluetooth SBC XQ), SD-primary bookmarks, PNG and
progressive-JPEG covers, Bluetooth A2DP output with AVRCP remote and wired
fallback, SD-primary resume positions, boot ADB, and the storage-full scan
guard.

## v2.0.17 - 2026-07-20

Firmware marker: `2.0.17` - About-screen label `HiBy R1 2.0.17`.

Restores three general device/music feature unlocks that the NativeApp pivot
(v2.0.0) had dropped, even though the tooling for them was never removed. Every
pre-2.0 release (v1.5.0 through v1.6.3) shipped these; they were simply left out
of the v2.0.x build invocations. They are pure stock-resource / shell-config
tweaks (no binary, boot, PMIC, or mount changes), so they layer onto v2.0.16
without touching the audiobook app or its hook. Install over v2.0.4 (or any
v2.0.x); library, resume positions, bookmarks, and BT pairings are preserved.

### Restored stock-feature unlocks

- **USB DAC mode**: unlocks the USB-DAC working mode and related Settings flags
  (`usb_mode`, `dac_feedback`, `car_mode`, `standby`, `about`, `dac_to_store`) in
  `set_functions.json` / `midi_set_functions.json` / `config.json`. USB-DAC and
  boot-ADB share the one USB gadget controller and stay mutually exclusive by
  System -> USB working mode (Device = ADB on; DAC = USB audio out, ADB off that
  session). This is complementary to the boot-ADB shipped since v2.0.15, not a
  conflict.
- **Native DSD**: sets `AnalogDsdNative: native` on the analog output device in
  `ot_devices.json`, enabling native DSD on the analog output path for the stock
  Music player.
- **Bluetooth SBC XQ**: adds `--sbc-quality=xq` to the BlueALSA launch in
  `/usr/bin/bt_init`, raising SBC encoding quality when the receiving device
  supports it. Because the audiobook app drives `pcm.bluealsa` directly for BT
  output, this also applies to audiobook-over-BT; on-device testing confirmed
  no regression vs v2.0.16's BT path.

Everything else is unchanged from 2.0.16: SD-primary bookmarks, PNG and
progressive-JPEG covers, Bluetooth A2DP output with AVRCP remote and wired
fallback, SD-primary resume positions, boot ADB, and the storage-full scan
guard. The audiobook app and hook library are byte-identical to v2.0.16.

## v2.0.16 - 2026-07-19

Firmware marker: `2.0.16` - About-screen label `HiBy R1 2.0.16`.

Hardens bookmark storage so a full internal data partition can never lose or
refuse a bookmark. Install over v2.0.4 (or any v2.0.x); library, resume
positions, bookmarks, and BT pairings are preserved.

### Bookmarks are now SD-primary

- Bookmarks are saved to the SD card (`bookmark_sd.{c,h}`, one tiny
  `<book_id>.bm` file per book under `.audiobook_pos/`), not to `library.db` on
  the internal partition. Adding a bookmark now touches only the SD card, so a
  full `/usr/data` can no longer poison the sqlite connection or flip Now Playing
  to "Book not found" the way a failed DB write could. Mirrors the proven
  SD-primary position store (`posstore.h`).
- Each `.bm` is written atomically (temp file then rename within one directory),
  so a power cut cannot corrupt an existing set; a damaged line is skipped on
  read, so the worst case is losing one book's marks, never the whole library DB.
- `created_at` doubles as the bookmark id reported to the UI, so the existing
  jump/delete paths in `ui.c` are unchanged (`audiobook_delete_bookmark` gained
  a `book_id` param so the SD file can be located; the one call site was updated).
- Existing in-DB bookmarks migrate to SD automatically the first time a book's
  bookmark screen is opened (one-time, per-book; an empty marker file is left
  even when the DB has no rows so the DB is never re-queried). The `bookmarks`
  table stays in the schema but is now inert.
- Orphan-prune drops a removed book's `.bm` alongside its `.pos`
  (`bookmark_remove_book_sd` next to `pos_remove_sd` in `scan.c`).
- The library database itself stays on the UBIFS internal partition
  (power-cut-safe), unchanged for books/tracks/chapters/progress.

## v2.0.15 - 2026-07-19

Firmware marker: `2.0.15` - About-screen label `HiBy R1 2.0.15`.

First public release since v2.0.4. Bundles all accumulated dev-build
improvements (v2.0.5 through v2.0.15) into one update. Install over v2.0.4
(or any v2.0.x); library, resume positions, and BT pairings are preserved.

### Covers

- PNG cover art now renders (external `cover.png`/`folder.png` and embedded
  MP3 APIC / M4B `covr` PNG). A self-contained streaming PNG decoder
  (`pngdec.c`) decodes row-by-row over `dlopen`'d `libz`, downsampling on the
  fly (~150 KB peak), so large PNGs no longer OOM the device. No libpng
  dependency.
- Progressive-JPEG covers now decode instead of bailing. libjpeg's
  `max_memory_to_use` is capped to available RAM (minus a headroom margin),
  so huge progressives bail gracefully while normal/baseline covers render.
  Previously about half of a large library showed no cover.
- Cover dispatch is by file signature + extension; the `.r565` RGB565 cache
  is shared across JPEG and PNG.

### Bluetooth output

- Audiobook playback can route to a connected Bluetooth A2DP headphone/speaker
  via BlueALSA (`pcm.bluealsa` plug device, auto rate/format conversion).
  Auto-detect at track open; falls back to the wired DAC if no sink is
  connected or if the BT transport drops mid-playback (retry-then-fallback,
  no auto-switch-back until the next track open).
- Force-takes the A2DP slot from the stock player (same `hiby_player`
  process) when needed, since the stock player holds it exclusively.
- AVRCP remote support: a BT speaker's play/pause button controls the
  audiobook (the virtual AVRCP input device is detected and reopened lazily).
- On audiobook exit over BT, the stock player is handed back **paused**: the
  A2DP sink is briefly disconnected then reconnected, which leaves the stock
  music engine paused (and healthy - press play on the speaker/launcher to
  resume). Music no longer auto-plays over the speaker when you leave the
  audiobook app. Expect about a 2 s BT blip on exit. (Earlier builds injected
  a play/pause key event after the reconnect, which could start the music
  instead of pausing it; the injection is removed.)
- Fixed garbled/doubled audio after a **pause then restart** over Bluetooth.
  The resume re-seek reused the existing BlueALSA A2DP/LDAC stream with only
  a `drop`/`prepare`, which left the encoder reservoir in a stale state and
  played garbage on restart. The re-seek now re-opens the BlueALSA PCM fresh
  (the same path the first clean play used); the wired DAC keeps the cheap
  `drop`/`prepare` (it resumes clean).

### Resume and position saving

- Fixed a race that could **reset a book to the beginning** or double the
  audio: tapping play on a stopped book while an AVRCP/key event arrived in
  the player thread's idle window submitted a second "play" command for the
  same book (the BT speaker auto-sends an AVRCP PLAY the instant the A2DP
  slot is taken). Both plays then ran into the same output. A same-book
  resume while already playing that book is now dropped; a seek, a different
  book, or any play while not playing still proceeds.
- The exact final position is now saved on quit (previously only as fresh as
  the 5-second periodic save, so up to 5 s could be lost on exit).
- Positions remain SD-primary (`/usr/data/mnt/sd_0/.audiobook_pos`), surviving
  a full `/usr/data` partition; the SQLite DB is a best-effort mirror.
- The DB mirror write is now skipped when `/usr/data` is critically low (< 1
  MB free). The device's data partition is chronically ~95% full (the stock
  music DB rebuilds on every boot), and a sqlite write hitting `SQLITE_FULL`
  mid-WAL could poison the connection so the next read failed and the Now
  Playing screen flipped to "Book not found". The SD store is authoritative,
  so skipping the mirror just leaves the list-view "%" briefly stale.

### ADB and boot

- ADB is now always available at boot when System -> USB working mode is set
  to "Device" (mode 1), via a new `S90adb` init script baked into the
  rootfs. No in-app toggle needed. ADB and USB-DAC share the single USB
  gadget controller and are mutually exclusive by USB working mode, so this
  does not block USB-DAC (set the mode to DAC and ADB stays off that session).
- Removed the in-app "Developer" / "ADB (durable)" toggle and its hook-side
  poll (superseded by boot ADB).

### Library scan robustness

- Storage-full guards: the scan aborts cleanly (with a red on-screen error
  flash) if `/usr/data` has too little free space to write the library DB,
  instead of stalling. A `VACUUM` runs after orphan cleanup.

## v2.0.4 - 2026-07-19

Firmware marker: `2.0.4` - About-screen label `HiBy R1 2.0.4`.

Fixes a library-scan hang that affected users with large audiobook
libraries. M4B books with large "moov" metadata atoms (15 MB and up) caused
the scanner to run out of memory and freeze on the "Scanning library..."
screen. The chapter parser now memory-maps the moov atom instead of loading
it all into RAM, so only the few KB of chapter data it actually needs are
read. Scans that previously hung now complete.

## v2.0.0 - 2026-07-18

Firmware marker: `2.0A` - About-screen label `HiBy R1 2.0 A`.

**Major release: the NativeApp pivot.** v2.0.0 replaces the v1.6.x
resume-daemon / stock-route approach with a self-contained in-process audiobook
app (`audiobook_app/`) that draws its own UI to the framebuffer and drives audio
through ALSA, launched from the launcher's Audiobooks tile via an `LD_PRELOAD`
hook into `hiby_player`.

- New dedicated Audiobooks app with Home, Titles/Authors/Series/Folders/Finished
 lists, book detail, Now Playing, Chapters, and Bookmarks screens - all drawn
 by the app, not repurposed stock music views.
- MP3 and M4B/AAC playback (AAC via `dlopen`'d `libfdk-aac`; self-contained
 `mp4_audio.c` demuxer).
- Per-book and multipart resume across reboots and book switching, with a
 5-second smart rewind on resume from a saved position.
- Now Playing: cover art, title/author/duration, and a draggable progress
 handle for scrub-seek (tapping the bar elsewhere does not jump).
- Playback speed 1.0 / 1.1 / 1.25 / 1.5x via WSOLA time-stretch (pitch
 preserved; 1.0x exact passthrough). Persists via `playback_speed`.
- Sleep timer Off / 15 / 30 / 60 min with live on-screen countdown; auto-pauses
 and saves position on expiry.
- M4B embedded chapters parsed from the QuickTime chapter track (stsc-aware, so
 multi-sample chunks resolve correctly) or Nero `chpl`; MP3 books get one
 synthesized chapter per file. Chapters cached at scan time (Home -> Refresh).
- Bookmarks: tap Mark on Now Playing to add; tap to jump; long-press to delete.
- Cover-art thumbnails in lists (libjpeg decode-on-demand, progressive-JPEG
 guard to avoid OOM freezes; pre-warm starvation fix so one bad cover doesn't
 block the rest).
- Hardware controls: power toggles backlight (audio plays dark, double-tap
 wakes), play/pause/prev/next/volume in-app, fine-stepped volume (~2-2.5 dB)
 with hold-to-ramp, Back always top-left.
- Swipe left from a list jumps to Now Playing.
- Library, progress, chapters, and bookmarks in an on-device SQLite DB; the
 stock music database is no longer involved.
- Clean exit to the HiBy launcher (no black screen, no power-button kick).

**Expected behavior:** open Audiobooks and tap Home -> Refresh on first run
with an SD card to scan `/Audiobooks`. **Audiobook playback stops when you
leave the app to the launcher** - audio is tied to the app being open.
Background playback on the launcher is planned for a later phase, not in this
release.

Build is `-IncludeAudiobookNativeApp` only (mutually exclusive with the legacy
resume-daemon switches). **No persistent boot-ADB** in the public release
(`-EnableBootAdb` omitted).

Built package `r1-audiobooks-2.0A.upt`.

- UPT MD5: `5153a5a80e9a4acdc9d2748011b0c34d`
- UPT SHA256: `ab954621a02f7610563775d3e3770b69fff793ab9e13c324669dac77c1d5e1c8`
- Rootfs MD5: `6baf5dcaae7d00fdded6b8cac62f485a`
- Rootfs SHA256: `edae5ba040741e0a522f8192b969858232ce0f54af9ccb0cc9a042242a02eec4`
- `hiby_player.audiobooks` (supervisor shell) MD5 inside rootfs:
 `cbe2bc1001cbe6ad6ce6cd8e04889c59`
- `libaudiobook_hook.so` size: 1,623,492 bytes
- `r1_audiobook_app` size: 81,160 bytes

## v1.6.3 - 2026-07-01

Firmware marker: `1.6.16.6-audiobook`

Hotfix release after `v1.6.2`.

- Fixed a likely `Audiobooks -> Titles` resume/start failure after listening to Music.
- When Music remains the active playback path but the Audiobooks title list is visible, the resume daemon now refreshes Audiobooks title context and polls title selections at the faster Audiobooks cadence.
- Added daemon logic tests for title-context polling while Music is active and for the visible-title-list context refresh.
- Updated the ADB staging helper defaults to the `1.6.16.6-audiobook` package.
- Built package `r1-audiobooks-1.6.16.6-audiobook.upt`. MD5: `f4b605a1edd8385a0d6ed5279dfa7add`; SHA256: `75d9d3d822bba35a9eb4a508fb604f720b97276b61e71f6dfa09360eff359ebf`.
- Rootfs MD5: `74430b4fc06220419a0558a4a5b8b829`; Rootfs SHA256: `400acaf978ed8beb7255e46acbda3f3905f3e8b350b26908cfae7cca5fd0d0b1`.
- Player MD5 inside rootfs remains `09997a636c94112ff76c85a6d4a8d0ff`.
- All `v1.6.2` bookmark, refresh-library, folder-based audiobook detection, multipart resume, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is otherwise retained.

## v1.6.2 - 2026-06-24

Firmware marker: `1.6.16.5-audiobook`

Bookmark feature release after `v1.6.1`.

- Replaced the old `Series` hub row with `Bkmarks`.
- Added on-device manual bookmarking from the Now Playing screen via long-press Back.
- Added a native bookmark monitor helper that writes bookmark requests without changing stock playback controls.
- Added generated bookmark playlist views under `/Audiobooks/_views/Bookmarks` so saved bookmarks can be reopened from the Audiobooks hub.
- Added bookmark-aware restore selection so opening a bookmark can prefer the saved bookmark position even when the same book also has a newer ordinary resume record.
- Relaxed the late backward-restore guard for bookmark-backed restores only, which lets bookmark launches seek forward to the saved bookmark instead of being treated like a manual rewind attempt.
- Kept `Refresh Library`, `Titles`, `Authors`, and `Folders` in the native Audiobooks hub.
- Live installed-device validation on the test R1 confirmed the bookmark playlist opened `All the Pretty Horses` and restored near the saved `214649 ms` position at about `03:44`.
- Built package `r1-audiobooks-1.6.16.5-audiobook.upt`. MD5: `64fd718252935d0ebf220b43e1f86a0e`; SHA256: `1410a718778b269a49165ef6fd6f0a8c67466ae600333332c3e989ff66952def`.
- Rootfs MD5: `17622256b464b81026463c278dc93e5f`; Rootfs SHA256: `6784c7341b54ea8877520154dcda21d6e497e3d571903e95eef96810915d6b32`.
- Player MD5 inside rootfs remains `9ccf6668a82dab0f7f3535615e5108e1`.
- All `v1.6.1` refresh-library behavior, folder-based audiobook detection, multipart resume, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is otherwise retained.

## v1.6.1 - 2026-06-22

Firmware marker: `1.6.18-audiobook`

Hotfix for `v1.6.0`.

- Renamed the Audiobooks hub's misleading `Scan` row to `Refresh Library`.
- Replaced the stock text-book scan action behind that row with an audiobook-aware refresh action.
- Tapping `Refresh Library` now opens `Titles` as visible feedback, instead of appearing to do nothing.
- The refresh action writes a request marker and starts an immediate background audiobook catalog/view rebuild.
- Refresh logs are written to `/usr/data/audiobooks/refresh.log` with start, row-count, mirror-copy, and completion lines.
- Fixed a boot-timing edge case where a manual refresh request could be swallowed if tapped during the post-boot watcher window.
- Local verification passed for `r1-audiobooks-1.6.18-audiobook.upt`.
- Live installed-device verification passed on the test R1 with the matching `1.6.17.2-refresh-dev` build: `Refresh Library` rebuilt 135 audiobook tracks, opened the title list, DB integrity was `ok`, Music album/search leakage remained zero, and resume/DB watcher processes were running.
- Built package `r1-audiobooks-1.6.18-audiobook.upt`. MD5: `e3dba87c24ef84196ec1c91fe3c3e26a`; SHA256: `e42d70d84bf3353391c16fa60f83f399d2624226d2792f3c7882d9a1bbe45253`.
- Rootfs MD5: `dd47cf5f338d70ecab1f8be108529505`; Rootfs SHA256: `bfac581b61ff87c133bb5eb085a5ce5bb56db10678bae84697fae04d8697f8e6`.
- All `v1.6.0` native Audiobooks hub, generated title/author/series views, folder-based audiobook detection, resume behavior, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is otherwise retained.

## v1.6.0 - 2026-06-22

Firmware marker: `1.6.17-audiobook`

Feature release after `v1.5.4`.

- Reworked the Audiobooks entry from the direct title-list flow into a native Audiobooks hub.
- The Audiobooks hub now contains `Scan`, `Titles`, `Authors`, `Series`, and `Folders`.
- `Titles` opens generated book-title playlist rows.
- `Authors` opens generated `Author - Title` playlist rows.
- `Series` opens generated series rows for books that have series metadata or series-like folders. Standalone books remain out of the Series view instead of being forced into a fake series.
- `Folders` opens the SD-card `/Audiobooks` folder and now displays a friendly `Folders` page header instead of the stock `Files` header when opened from the Audiobooks hub.
- Added a native sub-back helper for generated Audiobooks views so Titles, Authors, Series, and Folders use Audiobooks-specific page labels and return paths.
- The DB watcher/helper now generates the `/Audiobooks/_views` playlist folders used by the Titles, Authors, and Series hub views after Music -> Update Database.
- The DB helper now preserves and repairs one internal `Audiobook` route row with the correct audiobook count so the Audiobooks hub can keep finding books after scans and SD-card swaps.
- The `--needs-maintenance` check now detects missing or stale audiobook route rows and triggers repair instead of silently accepting a DB that would break the Audiobooks hub.
- Audiobook media rows now use normal SQLite text binding instead of embedded-NUL text binding, and release verification now checks for embedded-NUL audiobook text fields.
- Release-state verification now checks the internal Audiobook route row count, generated view catalogs, and route-row repair strings.
- Kept `v1.5.4` SD-card swap behavior: the watcher still tracks SD-root DB changes, can promote a clean/current SD database back to internal storage, and mirrors repaired databases.
- Kept folder-based audiobook detection: files under `/Audiobooks` do not need an exact `Audiobook` genre tag.
- Kept per-book resume, multipart resume, completed-book restart behavior, the 15-second save guard, Native DSD enablement, Bluetooth SBC XQ, and unlocked USB DAC-related settings.
- Known quirk: `Folders` may show the generated `_views` folder because that is where the on-device title/author/series playlist views live.
- Known quirk: from the Folders root, edge-back is more reliable than the left arrow for returning to the Audiobooks hub.
- Built package `r1-audiobooks-1.6.17-audiobook.upt`. MD5: `e8491f65ead4ef7a34163a67c7ee7007`; SHA256: `47b6b2aa85f0f14d13d659f0f3f987808f7d389a7a32bf7e54676388e6f82523`.
- Rootfs MD5: `d8c6a46cb4dc90624042f89224f611e6`; Rootfs SHA256: `687b83dff23319af917e19af9bb1bc1c95a7f6c915e852d175385b1c4e9d6b5f`.
- Local package verification passed with native hub view rows, DB maintenance, Audiobooks icon, Native DSD, Bluetooth SBC XQ, and USB DAC mode enabled. Installed-device verification passed on the test R1 after flashing this public-labeled package: version markers reported `1.6.17-audiobook`, DB integrity was `ok`, 135 audiobook rows across six books were present, title/author/series catalogs were pulled, one resume daemon and one DB watcher were running, and the title flow reached Now Playing with resume.

## v1.5.4 - 2026-06-22

Firmware marker: `1.6.16.5-audiobook`

Hotfix for `v1.5.3`.

- Fixed SD-card swap behavior where Music -> Update Database could update the SD-card media DB while the internal active media DB still contained rows from the previous SD card.
- The DB watcher now tracks SD-root media DB signature changes in addition to the internal DB.
- When the SD-root `usrlocal_media.db` is clean/current and the internal DB needs repair, the watcher promotes the SD DB back to the internal DB, runs the maintainer, and mirrors the repaired DB back to all active DB locations.
- Live installed-device regression test forced the internal DB to an old-card state with 298 audiobook rows while the current SD card had 135 audiobook files. The watcher logged `primary-copy reason=boot`, rebuilt 135 audiobook rows, regenerated catalogs, and left both primary and SD-root DBs with no maintenance needed.
- Installed-device verification passed afterward with DB integrity `ok`, 135 audiobook rows across 6 books, zero audiobook album/genre/search leakage, one resume daemon, one DB watcher, and about 31 MB free under `/usr/data`.
- Built package `r1-audiobooks-1.6.16.5-audiobook.upt`. MD5: `f6a0e65af41c7990f03e342fef995bad`; SHA256: `efd77a5a6f83879e76089ace072657891ff2e5475c4f0e82d812f728ad4e2816`.
- Rootfs MD5: `1797f124a92177605e776615144f323a`; Rootfs SHA256: `cf2076de6c700abd24d66dc587ac3109786829e5f589f4068e61988b0a481325`.
- All `v1.5.3` folder-based audiobook detection, resume, UI, Native DSD, Bluetooth SBC XQ, and USB DAC mode behavior is otherwise retained.

## v1.5.3 - 2026-06-22

Firmware marker: `1.6.16.4-audiobook`

Hotfix for `v1.5.2`.

- Fixed a second new-SD/new-scan case where Audiobooks could show `No music found` after Music -> Update Database even though audiobook files existed under `/Audiobooks`.
- Root cause: the stock scanner can produce a valid media database with zero audiobook rows when files under `/Audiobooks` do not have a genre tag that the stock app treats as Audiobook. The previous watcher only detected misnormalized existing audiobook rows, so a same-size DB with missing audiobook rows could be skipped.
- The DB helper now scans the actual `/Audiobooks` folder during its fast `--needs-maintenance` check and compares those paths with existing audiobook rows in the media DB.
- Folder location now wins over genre metadata: files under `/Audiobooks` are repaired into the Audiobooks section even when the genre is blank, custom, or not exactly `Audiobook`.
- Live installed-device regression test forced a same-size primary media DB with zero audiobook rows while 298 audiobook files were present on the SD card. The watcher detected `content-repair-mtime`, rebuilt 298 audiobook rows, regenerated the title/author/series catalogs, and mirrored the repaired DB to `/data/usrlocal_media.db` and the SD-root DB copy.
- Installed-device verification passed afterward with DB integrity `ok`, 298 audiobook rows across 52 books, zero audiobook album/genre/search leakage, one resume daemon, one DB watcher, and about 18 MB free under `/usr/data`.
- Built package `r1-audiobooks-1.6.16.4-audiobook.upt`. MD5: `d6ebce37c653f3756b54a7b5c3725788`; SHA256: `eefd1f060babf5930d7bae4be481d7f580edf225a128d17ab6130beced4dd404`.
- Rootfs MD5: `8728cd7ad4734f3f36efdfe6d0c1093a`; Rootfs SHA256: `394db7b39571f3cc95f04ceec1195f1fedb0abe3ac2a3dec3dbf5f7c3461c152`.
- All `v1.5.2` audiobook, resume, UI, Native DSD, Bluetooth SBC XQ, and USB DAC mode behavior is otherwise retained.

## v1.5.2 - 2026-06-17

Firmware marker: `1.6.16.2-audiobook`

Hotfix for `v1.5.1`.

- Fixed the remaining new-SD-card scan case where Audiobooks could still show `No music found` after Music -> Update Database.
- Root cause: after the DB helper normalized audiobook rows, the stock scanner could rewrite those rows back to their original genre tags without changing the media DB file size. The `v1.5.1` watcher skipped same-size DB changes as mtime-only churn, so it could miss that rewrite.
- The DB helper now has a cheap `--needs-maintenance` check that reports whether `/Audiobooks` rows are misnormalized or leaked into search.
- The DB watcher now uses that check before skipping same-size DB changes and runs a `content-repair-mtime` pass when the DB contents need repair.
- Reduced scan-time lag by repairing the primary media DB once, then copying the repaired DB to the `/data` and SD-root mirror locations instead of fully reprocessing each mirror DB.
- Reduced active audiobook resume polling from 1 second to 2 seconds while keeping the 15-second resume save cadence.
- Live runtime testing on the regression SD card confirmed all three DB copies report 298 audiobook rows, zero misnormalized audiobook rows, and zero audiobook rows in search after repair.
- Built package `r1-audiobooks-1.6.16.2-audiobook.upt`. MD5: `80c0d7295c2d55575870c4d226e83be9`; SHA256: `3109fea179b816dcdd4c1536b8973f527ef8f8b2d628942317f6b4ded62ca4c6`.
- Rootfs MD5: `35ffdbb9b401c03f1742782da0104b55`; Rootfs SHA256: `127b90ddfc92ecf2e668368e31422b5eb090c47e86011c391c30dd4b4ec4c475`.

## v1.5.1 - 2026-06-17

Firmware marker: `1.6.16.1-audiobook`

Hotfix for `v1.5.0`.

- Fixed a new-SD-card regression where Audiobooks could show `No music found` after running Music -> Update Database, even though files existed under `/Audiobooks`.
- Root cause: on some scans the stock UI reads the SD-root media database copy at `/usr/data/mnt/sd_0/usrlocal_media.db`; `1.6.16-audiobook` normalized `/usr/data/usrlocal_media.db` and `/data/usrlocal_media.db`, but did not always normalize the SD-root DB copy.
- The DB watcher now runs the audiobook maintainer against `/usr/data/usrlocal_media.db`, `/data/usrlocal_media.db`, and `/usr/data/mnt/sd_0/usrlocal_media.db` when those database files exist.
- Live verification on the regression SD card confirmed the SD-root database has integrity `ok`, contains 298 audiobook rows with normalized `Audiobook` genre values in `MEDIA_TABLE` and `MEDIA2_TABLE`, and keeps Audiobooks out of Music Search, Albums, and Genres.
- Live UI verification confirmed the Audiobooks launcher opens the title list instead of `No music found` after the hotfix.
- Built public package `r1-audiobooks-1.6.16.1-audiobook.upt`. MD5: `d30527750a071602a67f1eceb462f8cc`; SHA256: `085495646039eafb496279d3ef2625671783552ad069150c3e959e5c219d7f3f`.
- Rootfs MD5: `7a0b2a3d001ea53b079b79fbcf9c5933`; Rootfs SHA256: `26c9b68e49a3761930dcae3c95b172905d8e88108c68f59be44ffe3c0a96d942`.
- All `1.6.16-audiobook` UI, resume, audio unlock, and catalog features are otherwise retained.

## v1.5.0 - 2026-06-17

Firmware marker: `1.6.16-audiobook`

This release consolidates all development work since `1.6.15-audiobook`.

- Added the new Audiobooks launcher icon while keeping the launcher label as `Audiobooks`.
- Reduced the Audiobooks double-Back quirk. After the firmware sees the Audiobooks title/list screen, one Back from that area now triggers a guarded cleanup that returns to the main launcher instead of leaving the user on the stock Genres page.
- Improved multipart title-start resume. The runtime now favors the faster first-track plus direct-open correction path for launcher/context starts, so it can jump to the saved file and position more quickly.
- Added a direct-open helper for title-list starts. This improves the path from tapping a book title to landing on the saved multipart file.
- Added a restore-settle guard after direct-open or track correction so the stock player and Now Playing screen settle before position restore/UI seek runs. This fixes the live title-switch race found after `1.6.15`.
- Hardened audiobook title switching while another audiobook is already playing. The daemon avoids stale memory roots from the previously playing book and protects deeper saved bookmarks from accidental overwrite.
- Fixed title-list resume when the player opens a selected book at `00:00`; guarded title-start restores can now seek back to the saved bookmark instead of waiting for normal playback to advance.
- Kept the 15-second new-track commit guard and changed steady-state audiobook position saves to a 15-second cadence, reducing unnecessary internal writes while preserving resume behavior.
- Added lower-overhead path checks during normal music/non-audiobook playback so music use stays away from the heavier audiobook resume logic.
- Improved DB watcher startup and restart behavior. The watcher now waits for boot scan stability, recovers stale locks, force-clears stuck old watcher processes when needed, and exits cleanly on stop/restart.
- Improved fresh SD-card and late-mount behavior. If the first boot/update pass sees no audiobook files because the SD card is not ready yet, the watcher retries when `/Audiobooks` appears.
- Added missing/empty media DB recovery: the watcher can seed a valid empty media database, then the helper can scan `/Music` and `/Audiobooks` itself.
- Extended audiobook catalogs written under `/usr/data/audiobooks/` to include title, author, and series sidecar views for future UI work and easier debugging. The visible Audiobooks UI is still the title list.
- Kept audiobook isolation from normal Music search, album, and genre tables.
- Added Native DSD enablement for the analog output path.
- Added Bluetooth SBC XQ launch configuration for Bluetooth audio quality when SBC is used and the receiving device supports it.
- Unlocked USB DAC related settings/flags. Light real-device testing confirmed USB audio input could play through the R1 and out to a Bluetooth speaker after a clean reboot.
- Kept boot ADB out of public builds by default.
- Added local and installed verification coverage for the new audio unlocks, DB watcher boot stability, context-start guard, play-mode guard, sidecar catalogs, and framebuffer capture.
- Built public package `r1-audiobooks-1.6.16-audiobook.upt`. MD5: `4938a5d3f74204995a1bb297175da463`; SHA256: `ba3b16dc63e35abfc22cd0ac9e4324a5a2e3834ad894c42fd310f30f99c3f1e0`.
- Rootfs MD5: `48abe53dc5e83e8eeb045dfd8f4a3d17`; Rootfs SHA256: `adfdf99eefdeb2693aa8cf610780b05e60404f7d8e6dac33b0b5ef6b1c1d69ca`.
- Local package verification passed with DB maintenance, Audiobooks launcher icon, Native DSD, Bluetooth SBC XQ, USB DAC mode, and the Back/title-start runtime checks.

## v1.4.0 - 2026-06-11

Firmware marker: `1.6.15-audiobook`

- Added a DB watcher lock so duplicate watcher processes exit cleanly instead of running multiple maintenance loops.
- Improved same-size DB signature checks so music and audiobook playback timestamp churn is skipped instead of triggering maintenance.
- Verified normal music playback stays on the low-overhead path with zero audiobook position reads/saves.
- Verified audiobook title playback still restores through the Now Playing screen and resumes to the saved point.
- Hardened the ADB firmware staging helper so failed uploads remove temporary files.
- Extended local and installed-release verification to assert the DB watcher lock and mtime-only skip behavior.

## v1.3.0 - 2026-06-11

Firmware marker: `1.6.11-audiobook`

- Improved title-list resume for multipart books with row-tap verification and near-miss correction.
- Added a selected-title memory-scan helper for some title-list flows.
- Capped resume and DB maintenance log growth on the device.
- Kept audiobook rows out of Music Albums, Genres, and Search.

## v1.2.0 - 2026-06-11

Firmware marker: `1.6.9-audiobook`

- Reduced resume-daemon work during normal music playback and non-audiobook playback.
- Improved runtime stability after early public feedback about lag during music selection and shuffle.

## v1.1.0 - 2026-06-11

Firmware marker: `1.6.7-audiobook`

- Improved self-contained on-device DB maintenance.
- Continued the transition away from PC/ADB-only database setup.

## v1.0.0 - 2026-06-10

Firmware marker: `1.6.4-audiobook`

- First public audiobook firmware release.
- Renamed Books to Audiobooks.
- Added audiobook-only title browsing and stock Now Playing playback.
- Added per-book resume, including multipart books.
- Kept `/Audiobooks` content out of normal Music Albums and Genres.
- Added on-device DB maintenance after Music -> Update Database.
