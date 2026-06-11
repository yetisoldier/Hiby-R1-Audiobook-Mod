# Firmware Improvement Plan

This plan keeps `1.6.4-audiobook` as the stable public baseline while preparing
incremental changes that can be tested off-device before a new firmware build.

## Current Stable Baseline

- Public release: `v1.0.0`, firmware marker `1.6.4-audiobook`.
- Based on stock HiBy R1 firmware 1.6 for the normal R1.
- Audiobooks are separated from Music albums/genres/search, have a launcher
  entry, use the stock Now Playing screen, and keep per-book resume state.
- The installed DB watcher/helper can rebuild catalogs from `/Music` and
  `/Audiobooks` after the user runs Music -> Update Database.

## Useful New Information From hiby-modding

- `hiby-mods` documents a fuller HiBy media database builder, including wider
  format-code coverage, sidecar cover names, LRC paths, and scan/collation
  behavior.
- `hiby-mods` documents that oversized cover art can make these low-memory
  players slower; 360x360 JPEG sidecar art is a good target to test on R1.
- `hiby-mods` and `hiby_os_crack` document the OTA format, MD5 chain, boot
  sequence, stock init script behavior, and rootfs layout. Most of that matches
  the safety checks we already added, but it gives us more pre-flash verifier
  ideas.
- `hiby_os_crack` confirms that full QEMU system emulation is still research
  work because the X1600 peripherals are not modeled. User-mode or host-native
  helper testing is the practical short-term path.

## Safe Near-Term Work

1. Harden the on-device DB helper while preserving the current UI/resume path.
2. Add repeatable local fixture tests for DB helper behavior.
3. Build a candidate helper binary and test it through ADB as a runtime-only
   replacement before building or flashing a new firmware package.
4. Only after runtime tests pass, build a new firmware candidate with a new
   version marker such as `1.6.5-audiobook`.

## Implemented For Next Test Build

- Multipart title-tap resume no longer relies on stock hardware Next/Previous
  fallback by default. Live testing showed that those buttons can move through
  tracks in a non-linear database order for at least one multipart audiobook.
  The daemon now prefers direct visible-row selection and stops safely if that
  cannot prove it reached the saved track.
- The native DB helper now recognizes `.iso` files during fallback scans,
  matching the broader HiBy format table used by the hiby-modding database
  updater.
- The native DB helper now fills empty `album_pic_path` fields from sidecar
  files in the same folder, using this priority:
  `cover.jpg`, `folder.jpg`, `front.jpg`, `albumart.jpg`, plus `.jpeg` and
  `.png` variants.
- The native DB helper now fills empty `lrc_path` when a same-stem `.lrc` file
  sits beside the audio file.
- The offline `add_audiobooks_to_media_db.py` experiment tool now mirrors those
  `.iso`, cover, and LRC behaviors.
- `tools/test_r1_db_maint_local_fixture.py` creates a disposable fake SD card
  and seed DB, runs a Windows build of the C helper, and verifies `.mp3`,
  `.m4b`, `.iso`, cover, LRC, catalog, and release-state invariants.
- The resume catalog now includes a backward-compatible `book_key` column based
  on author plus book title when available. New resume records include the same
  key so future builds can survive simple audiobook folder renames or rebuilds
  more gracefully while still reading existing root-path records.
- The catalog also carries optional `series` and `series_part` columns derived
  from Seanap/Plex-style folders. Books without a series folder keep those
  fields blank.
- The DB helper now writes `/usr/data/audiobooks/catalog-books.tsv`, a
  one-row-per-book sidecar with author, title, stable book key, optional series,
  optional series part, track count, and first media ID. This prepares the data
  layer for Author / Title / Series view experiments without changing the
  release UI yet.
- The resume daemon now uses a slower idle polling interval when playback is not
  on an audiobook path. Active audiobook resume still uses the tuned fast
  interval, but normal music playback does less background work.
- Multipart title-tap resume now has a pre-play direct-start path. When the
  daemon can identify the selected book from the stock track-list memory, it
  reads the saved resume record and taps the saved track row before playback
  starts. This should avoid briefly playing each earlier file; if the memory
  scan fails, the daemon falls back to the current first-track-plus-correction
  behavior.
- `tools/test_r1_resume_daemon_logic_wsl.ps1` runs shell-level logic tests for
  the resume daemon's direct-track geometry, disable switches, and saved-track
  title selection without launching the daemon loop.
- `tools/test_r1_db_maint_qemu_wsl.ps1` runs the same DB helper fixture through
  WSL and `qemu-mipsel-static`, executing the real MIPS helper binary instead
  of the Windows test executable.
- `tools/r1_adb_control.py macro edge-back` wraps the reliable left-edge swipe
  back gesture used during live testing.

## Audible-Inspired Feature Notes

Audible-style audiobook players commonly emphasize reliable resume, chapter
navigation, sleep timers, bookmarks/notes, narration speed, offline playback,
and simple driving/car controls. On the R1, reliable resume and chapter/file
navigation are the best immediate fit because they can ride the stock playback
engine. Bookmarks, notes, speed control, and a dedicated sleep-timer UI would
require deeper UI work or a new input convention, so they should wait until the
current resume and browsing path is production-stable.

## Test Path Before Any Firmware Build

1. Build a Windows test helper:

   ```powershell
   $zig = (Resolve-Path .deps\zig\zig-x86_64-windows-0.16.0\zig.exe).Path
   $sqlite = (Resolve-Path .deps\sqlite\sqlite-amalgamation-3530200).Path
   & $zig cc -target x86_64-windows-gnu -O2 -I $sqlite `
     -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION `
     -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED `
     tools\r1_audiobook_db_maint.c (Join-Path $sqlite 'sqlite3.c') `
     -o work\native-db-maint\r1_audiobook_db_maint_win_test.exe
   ```

2. Run the local fixture:

   ```powershell
   python tools\test_r1_db_maint_local_fixture.py `
     --helper work\native-db-maint\r1_audiobook_db_maint_win_test.exe
   ```

3. Build the R1 helper:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\build_r1_db_maint_helper.ps1 `
     -OutFile work\native-db-maint\r1_audiobook_db_maint_enhanced
   ```

4. Run the MIPS helper under WSL/QEMU:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\test_r1_db_maint_qemu_wsl.ps1 `
     -Helper work\native-db-maint\r1_audiobook_db_maint_enhanced
   ```

5. Run the resume daemon logic test:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\test_r1_resume_daemon_logic_wsl.ps1
   ```

6. Runtime-only device test, no flashing:
   copy the enhanced helper to `/usr/data/audiobooks/bin/`, run the stock
   Music -> Update Database scan, wait for the watcher, then verify that any
   test book with `cover.jpg` and matching `.lrc` gets those paths in the DB.

7. If runtime-only testing passes, build a new development candidate and
   run the full pre-flash verifier before staging.

## Next Live Tests

When the R1 is available again, the most useful tests are:

1. Install the latest resume runtime without flashing:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_install_audiobook_resume_runtime.ps1 `
     -CatalogSource work\audiobook-resume-catalog.tsv `
     -RestoreEnabled
   ```

2. From the Audiobooks title list, tap a multipart book with a saved position
   on a later file. Expected result: the track list may appear briefly, but the
   daemon should tap the saved track directly instead of audibly playing each
   earlier track. The daemon should also force Now Playing play mode to
   sequential list-loop mode (`/usr/data/user.ini` offset `0x250`, value `3`) so
   shuffle or single-track repeat cannot make multipart books advance out of
   order.

3. If the result is odd, collect a debug bundle before rebooting:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_collect_audiobook_resume_debug.ps1
   ```

   The same collector is also useful after normal-music lag, freezes, or random
   reboots once ADB is available again.

4. For battery or normal-music responsiveness checks, run the read-only runtime
   monitor while music is playing:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_monitor_r1_runtime.ps1 `
     -DurationMinutes 120 `
     -IntervalSeconds 60
   ```

5. Before building or flashing another candidate, run the consolidated local
   sanity wrapper:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\run_local_dev_sanity.ps1
   ```

6. Use the unified ADB controller to capture screens and drive common UI steps
   when manual tapping is inconvenient:

   ```powershell
   python tools\r1_adb_control.py macro open-audiobooks
   python tools\r1_adb_control.py row 1 --after-screenshot
   python tools\r1_adb_control.py key playpause
   ```

   See `docs\adb_control_tools.md` for the full preset list.

7. Repeat once with pre-play direct start disabled to compare fallback behavior:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_install_audiobook_resume_runtime.ps1 `
     -CatalogSource work\audiobook-resume-catalog.tsv `
     -RestoreEnabled `
     -DisableBookTitleDirectTrackPreplay
   ```

## Longer-Term Research

- Real `hiby_player` UI work: a native Audiobooks page would be cleaner than the
  current genre-route and daemon workaround, but it requires deeper binary/UI
  reverse engineering.
- Author/Title/Series audiobook subviews are tracked in
  `docs\audiobook_views_research.md`. Title is the current stable route;
  Author may be testable through stock artist routes but needs catalog isolation;
  Series now has catalog data when Seanap/Plex-style folders are present, but
  still needs custom UI/query work.
- Better metadata parsing inside the on-device helper: possible for simple ID3
  tags, but higher risk and more code size. The current safer path is still to
  let the stock scanner provide metadata when possible and use folder/file
  fallback otherwise.
- QEMU system emulation: useful someday, but not ready for release validation.
  Local host-native helper tests and live ADB runtime tests remain more useful
  right now.
- Playback-mode handling: live R1 snapshots mapped `/usr/data/user.ini` offset
  `0x250` (`592` decimal) as the Now Playing play-mode byte. The observed
  normal-R1 tap cycle is `1 -> 2 -> 0 -> 3 -> 1`; framebuffer crops showed
  `1=single-track loop`, `2=random/shuffle`, `0=random/shuffle`, and
  `3=list loop`. Development builds now target value `3` for active
  audiobooks, using the Now Playing framebuffer guard before tapping the mode
  button.
