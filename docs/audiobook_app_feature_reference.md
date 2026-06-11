# Audiobook App Feature Reference

This note maps common audiobook-player features to practical R1 firmware work.
It is a planning document, not release behavior.

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
  This should be feasible in the resume daemon because it already owns seek
  restore. Keep it configurable, since exact resume is also valuable.
- Sleep timer by configuration file: feasible as a daemon feature, but awkward
  without a UI. A first version could read a small file from
  `/usr/data/audiobooks` or the SD card and stop/pause after a fixed number of
  minutes.
- Book-level progress report: feasible from `resume.d` plus
  `catalog-books.tsv`, useful for diagnostics and future UI work.
- Author/title/series views: metadata foundation exists in `catalog-books.tsv`,
  but stock route experiments so far do not provide a clean UI shortcut.

## Hard Or Risky

- Playback speed: likely requires deeper stock player/audio pipeline support.
  Do not assume the resume daemon can implement this cleanly.
- Notes/bookmarks UI: storing bookmarks is easy; creating a usable on-device UI
  to add/select them is the hard part.
- True chapter list for M4B metadata: the current practical chapter model is
  one file per chapter/part. Parsing embedded M4B chapters would need a deeper
  metadata path.
- Car Mode or lock-screen style UI: not a good fit for the R1 screen/UI stack.

## Suggested Order

1. Keep stabilizing resume and sequential playback.
2. Add an optional smart-rewind setting in the daemon.
3. Add a diagnostics/report command for book progress.
4. Continue RAM-only experiments for author/title/series route handling.
5. Consider sleep timer only after deciding how the user would turn it on/off.
