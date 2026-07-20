# Audiobook App Feature Reference

This note maps common audiobook-player features to practical R1 firmware work.
It is a planning document, not release behavior.

> **⚠️ SUPERSEDED — historical record (predates the NativeApp pivot,
> 2026-07-17).** This document was written 2026-06-11 about the
> *resume-daemon / stock-route* approach. The device now runs the **NativeApp
> pivot** (`-IncludeAudiobookNativeApp`), a self-contained in-process audiobook
> app (`audiobook_app/`, LD_PRELOAD hook into `hiby_player`). Many items listed
> below as "Hard Or Risky" or "Good Next Candidates" are now **implemented** —
> see the "Already Covered (NativeApp)" section below. For the current,
> authoritative per-feature reference, see the [`docs/modding/`](./modding/)
> knowledge base (e.g. [`modding/wsola_seek_resume.md`](./modding/wsola_seek_resume.md),
> [`modding/library_scan_storage.md`](./modding/library_scan_storage.md),
> [`modding/cover_art.md`](./modding/cover_art.md)). Treat the resume-daemon
> sections below as historical context only.

## Already Covered (NativeApp pivot — current)

These are implemented and confirmed on-device in the NativeApp build, on top of
the original resume/sequential-playback foundation:

- Offline local playback; per-book + multipart resume across reboot/switch.
- Now Playing screen with cover art (IJG libjpeg, scale-on-decode, progressive
  skip), title/author/duration, draggable progress handle (scrub seek).
- Hardware buttons: power (backlight toggle), play/pause, prev/next, vol±
  (fine-stepped + hold-to-ramp).
- **Playback speed 1.0/1.1/1.25/1.5× via WSOLA time-stretch** (pitch
  preserved; 1.0× exact passthrough). Persists via `playback_speed` setting.
- **Sleep timer** (Off/15/30/60 min, live countdown, auto-pause + save).
- **Bookmarks**: add from Now Playing ("Mark"), tap to jump, long-press to
  delete.
- **M4B/AAC playback** (dlopen'd `libfdk-aac`, self-contained `mp4_audio.c`).
- **M4B embedded chapters**: parsed from Nero `chpl` or QuickTime chapter track
  (stsc-based sample resolution — handles multi-sample chunks); re-read at
  scan time (Home → Refresh). MP3 books get one synthesized chapter per track.
- Library lists (Titles/Authors/Series/Folders/Finished) with on-demand
  thumbnails (progressive-JPEG-guarded pre-warm).
- Swipe left → Now Playing.

Sources checked on 2026-06-11:

- Audible Google Play listing:
  <https://play.google.com/store/apps/details?id=com.audible.application>
- Audible Help player topic, when reachable:
  <https://help.audible.com/s/topic/0TO4z000000SoJKGA0/player?language=en_US>

Audible's public Play Store listing calls out adjustable narration speed, sleep
timer, bookmarks and notes, offline listening, Car Mode, playlists, and library
organization. On the R1, the realistic split is:

## Already Covered

- Offline local playback.
- Per-book resume across music, reboot, and switching books.
- Multipart resume to the saved file and saved position.
- Completed-book handling.
- Now Playing screen and normal hardware playback controls.
- Separate audiobook catalog, with Music album/genre/search leakage blocked.

## Good Next Candidates

- Smart rewind on resume: optionally seek a few seconds before the saved point.
  Development builds expose this as `AUDIOBOOK_RESTORE_REWIND_MS`, defaulting to
  `0` so exact resume remains the release behavior until a value is chosen.
- Sleep timer by configuration file: feasible as a daemon feature, but awkward
  without a UI. A first version could read a small file from
  `/usr/data/audiobooks` or the SD card and stop/pause after a fixed number of
  minutes.
- Book-level progress report: feasible from `resume.d` plus
  `catalog-books.tsv`, useful for diagnostics and future UI work.
- Author/title/series views: metadata foundation exists in `catalog-books.tsv`,
  but stock route experiments so far do not provide a clean UI shortcut.

## Hard Or Risky

> Most of the items below were "hard" under the old resume-daemon/stock-route
> model. Under the NativeApp pivot they are now DONE — annotations marked
> **[DONE]**. Kept for history.

- **[DONE]** Playback speed: WSOLA time-stretch in the NativeApp (pitch
  preserved). See `Hiby-R1-wsola-speed-findings.md`.
- **[DONE]** Notes/bookmarks UI: add/jump/hold-to-delete in the NativeApp.
  See `Hiby-R1-...` bookmarks memory.
- **[DONE]** True chapter list for M4B metadata: embedded M4B chapters parsed
  (Nero `chpl` + QuickTime chapter track via stsc). See
  `Hiby-R1-m4b-chapters-fix.md`.
- **[DONE]** Sleep timer: Off/15/30/60 min in the NativeApp.
- Car Mode or lock-screen style UI: not a good fit for the R1 screen/UI stack.

## Suggested Order

1. Keep stabilizing resume and sequential playback.
2. Add an optional smart-rewind setting in the daemon.
3. Add a diagnostics/report command for book progress.
4. Continue RAM-only experiments for author/title/series route handling.
5. Consider sleep timer only after deciding how the user would turn it on/off.
