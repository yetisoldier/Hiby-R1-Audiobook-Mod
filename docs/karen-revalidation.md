# HiBy R1 Audiobook App - Revalidation Report

**QA Agent:** Karen  
**Date:** 2026-07-11  
**Scope:** Static revalidation of the 13 fixes, phases 1-6 and 8. Phase 7 was intentionally skipped per explicit instruction.  
**Device Interaction:** Not performed in this pass.

## Executive Summary

**Overall Status:** FAIL

**Overall Risk:** High

**Confidence:** High for static/code findings, Medium overall because runtime smoke testing was explicitly skipped.

**Release Recommendation:** Not Ready for Release

The source tree shows that most of the reported fixes are real, but two issues still block a flash-ready signoff:

- The theme color claim does not match the actual RGB565 conversion of `0x1062F2`.
- The checked-in `r1-audiobooks-2.0.0-audiobook.upt` artifact still contains an unstripped app binary, so the package is stale relative to the current stripped build.

## Test Plan

- Phase 1: Build verification, executed.
- Phase 2: Source code static analysis, executed.
- Phase 3: Config and packaging, executed.
- Phase 4: Spec compliance, executed.
- Phase 5: M4B/AAC decoder, executed.
- Phase 6: UI verification, executed.
- Phase 7: Device smoke test, skipped by instruction.
- Phase 8: Music regression, executed as static/code review only.

## Detailed Results

### Phase 1: Build Verification

- 1.1 Clean compile: PASS. `rm -f build/r1_audiobook_app && sh app/build.sh` completed without compiler output or errors.
- 1.2 Binary type: PASS. `file build/r1_audiobook_app` reported `ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, stripped`.
- 1.3 Binary size: PASS. `ls -la build/r1_audiobook_app` showed `2569656` bytes, under the 5 MB ceiling.
- 1.4 Architecture: PASS. `readelf -h build/r1_audiobook_app | grep -E 'Machine|Class|Data'` showed `ELF32`, little-endian, `MIPS R3000`.
- 1.5 Static linkage: PASS. `readelf -d build/r1_audiobook_app 2>/dev/null | grep NEEDED` returned no `NEEDED` entries.

### Phase 2: Source Code Static Analysis

- 2.1 `strncpy` NUL termination: PASS. `rg -n "strncpy" app/src/*.c` returned no matches.
- 2.2 Buffer overflow patterns: PASS. `rg -n "sprintf|strcat|gets|strcpy\(" app/src/*.c` returned no matches.
- 2.3 Null dereference review: PASS. Guarded dereferences are visible in `player.c`, `ui.c`, and `db.c`, for example `player.c:14`, `player.c:63`, and `ui.c:35`.
- 2.4 File descriptor lifecycle: PASS. `alsa.c`, `fb.c`, `touch.c`, and `db.c` all show open/close symmetry.
- 2.5 Thread safety in player: PASS. Shared state is guarded by `pthread_mutex_lock` / `pthread_mutex_unlock` throughout `player.c:147-370`.
- 2.6 Use-after-free review: PASS. Cleanup order in `player.c` closes decoder and ALSA resources before `queue_free()`.

### Phase 3: Config and Packaging

- 3.1 Config field alignment: PASS. `config.c` maps the runtime paths and env overrides directly to the config fields, including `AUDIOBOOK_APP_ROOT`, `AUDIOBOOK_LIBRARY_ROOT`, `AUDIOBOOK_DB_PATH`, `AUDIOBOOK_FONT_PATH`, and others.
- 3.2 Defaults sane: PASS. `config.c` defaults are plausible and device-specific:

```c
ab_copy_str(cfg->app_root, sizeof(cfg->app_root), "/usr/data/audiobooks");
ab_copy_str(cfg->library_root, sizeof(cfg->library_root), "/Audiobooks");
ab_copy_str(cfg->db_path, sizeof(cfg->db_path), "/usr/data/audiobooks/library.db");
ab_copy_str(cfg->font_path, sizeof(cfg->font_path), "/usr/resource/fonts/msyh.ttf");
cfg->scan_interval_ms = 3000;
cfg->save_interval_ms = 5000;
cfg->back_skip_ms = 15000;
cfg->forward_skip_ms = 30000;
```

- 3.3 Wrapper script is shell: PASS. `tools/r1_audiobook_resume_daemon_wrapper.sh` starts with `#!/bin/sh`.
- 3.4 Launcher script is shell: PASS. `tools/r1_audiobook_launch.sh` starts with `#!/bin/sh`.
- 3.5 Overlay includes app: PASS. `tools/firmware_overlay.json` contains `usr/bin/r1_audiobook_app`.
- 3.6 Overlay includes launcher: PASS. `tools/firmware_overlay.json` contains `usr/bin/r1_audiobook_launch.sh`.
- 3.7 UPT exists and is a real image: PASS. The checked-in `r1-audiobooks-2.0.0-audiobook.upt` is a valid `ISO 9660 CD-ROM filesystem data` image and is in the expected tens-of-megabytes range.
- 3.8 UPT contains the stripped app binary: FAIL. The extracted app from the current `r1-audiobooks-2.0.0-audiobook.upt` is still the old unstripped build:

```text
/tmp/upt_extract/upt_root/usr/bin/r1_audiobook_app: ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, with debug_info, not stripped
-rwxr-xr-x 1 root root 9972764 Jul 11 18:15 /tmp/upt_extract/upt_root/usr/bin/r1_audiobook_app
```

The current local build is stripped and much smaller:

```text
build/r1_audiobook_app: ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, stripped
-rwxrwxr-x 1 yetisoldier yetisoldier 2569656 Jul 11 19:08 build/r1_audiobook_app
```

This means the packaged `.upt` artifact is stale.

- 3.9 UPT contains launcher script: PASS. The extracted launcher starts with `#!/bin/sh`.
- 3.10 Old daemon/touch artifacts removed: PASS. `rg -n "r1_audiobook_direct_open|touch-event|BACK_GUARD|PLAY_MODE|UI_SEEK" tools/firmware_overlay.json` produced no matches.
- 3.11 Font and theme assets are in the overlay: PASS. `tools/firmware_overlay.json` includes `usr/share/audiobooks/fonts/msyh.ttf` and `usr/share/audiobooks/hiby-theme`.

### Phase 4: Spec Compliance

- 4.1 Direct title tap: PASS. `ui.c` opens a book directly and moves to `UI_SCREEN_NOW_PLAYING`.
- 4.2 Separate progress per book: PASS. `db.c` defines `progress(book_id INTEGER PRIMARY KEY ...)`.
- 4.3 Accidental-start protection: PASS. `ui.c` sets `protected_until_ms` and `main.c` respects it before saving.
- 4.4 Smart rewind tiers: PASS. `resume.c` now implements:
  - `< 300s` -> `0`
  - `300-3600s` -> `5000`
  - `3600-86400s` -> `10000`
  - `> 86400s` or reboot -> `20000`

Relevant evidence:

```c
uint32_t resume_smart_rewind_ms(uint64_t paused_seconds, bool rebooted, uint32_t saved_position_ms) {
    (void)saved_position_ms;
    if (!smart_rewind_enabled) return 0;
    if (rebooted || paused_seconds <= 0) return 20000;
    if (paused_seconds < 300) return 0;
    if (paused_seconds < 3600) return 5000;
    if (paused_seconds < 86400) return 10000;
    return 20000;
}
```

- 4.5 Completion on natural EOF only: PASS. `main.c` marks completion only when EOF is reached on the last track.
- 4.6 Completed books restart from beginning: PASS. `ui.c` resets completed books to track 1, position 0, completed 0.
- 4.7 Book-bounded queue: PASS. `queue_set_tracks()` stores the current book id and copies only the provided track list.
- 4.8 No shuffle/repeat in audiobook mode: PASS. `queue_next()` does contain a `repeat_book` branch, but nothing in the app sets `repeat_book` true, so audiobook playback is not shuffled or repeated by default.
- 4.9 Separate audiobook database: PASS. The app uses `/usr/data/audiobooks/library.db` and there are no `media.db` or `usrlocal` references in `app/src`.
- 4.10 No framebuffer pixel detection: PASS. `fb.c` is render-only; no readback-based screen detection is present.
- 4.11 Event-driven, not polling: PASS. `touch_poll()` uses `poll()` with a timeout.
- 4.12 No continuous polling when idle: PASS. The main loop uses a 200 ms wait path and does not spin.

### Phase 5: M4B/AAC Decoder

- 5.1 FAAD2 symbols present: PASS. The extracted unstripped app binary inside the package contains `NeAACDecOpen`, `NeAACDecDecode`, `NeAACDecInit2`, and related symbols.
- 5.2 M4B/M4A/AAC handled: PASS. `decoder.c` routes `.m4b`, `.m4a`, and `.aac` through the M4B/AAC decoder path.
- 5.3 MP4 parser exists: PASS. `m4b_decoder.c` includes `moov`, `udta`, `trak`, and `chpl` parsing plus `mp4read_open()`.
- 5.4 Large-file safety: PASS. `m4b_decoder.c` now uses `fseeko()` / `ftello()` and `m4b_decoder.h` stores offsets in `uint64_t`; `app/build.sh` also defines `_FILE_OFFSET_BITS=64`.
- 5.5 Chapters go to the chapters table, not tracks: PASS. `db_replace_track_chapters()` inserts into `chapters`, and `ui.c` uses `db_query_tracks()` for playback plus `db_query_chapters_display()` for the chapters screen.

### Phase 6: UI Verification

- 6.1 HiBy font loaded with fallback paths: PASS. `config.c` defaults to `/usr/resource/fonts/msyh.ttf` and `font.c` tries `/usr/resource/fonts/msyh.ttf`, `/usr/share/audiobooks/fonts/msyh.ttf`, and `/usr/resource/fonts/default.otf`.
- 6.2 HiBy button PNGs used: PASS. `ui.c` loads `btn_play.png`, `btn_pause.png`, `btn_prev.png`, and `btn_next.png` from the theme path and uses them in the mini-player and Now Playing UI.
- 6.3 Theme color is correct: FAIL. The source comment and constant claim `0x130F`, but the actual conversion of `0x1062F2` is `0x131E`.

Evidence:

```text
R5=2 G6=24 B5=30 RGB565=0x131E
```

And the code still says:

```c
/* HiBy brand blue: 0x1062F2 (RGB888) -> RGB565.
 * Packed: 0001_0011_0000_1111_0 = 0x130F.
 */
#define TH_FOCUS_BLUE       0x130Fu
```

This is a real mismatch. If the target color is exactly `0x1062F2`, the constant should be `0x131E`.

- 6.4 Cover art loading: PASS. `cover.c` and `ui.c` both use `stbi_load()` and fall back to `cover.jpg` / `folder.jpg`.
- 6.5 Swipe-right gesture: PASS. `TOUCH_SWIPE_RIGHT` is handled and navigates to Now Playing.
- 6.6 Mini-player: PASS. `draw_mini_player()` exists and is rendered on the home screen when a book is active.
- 6.7 Screen list includes AUTHORS, SERIES, FOLDERS: PASS. `ui.h` includes `UI_SCREEN_AUTHORS`, `UI_SCREEN_SERIES`, and `UI_SCREEN_FOLDERS`, and `ui.c` has render and touch handling for each.

### Phase 7: Device Smoke Test

- 7.1 Push app to device: SKIPPED.
- 7.2 Run app via ADB: SKIPPED.
- 7.3 Verify database created: SKIPPED.
- 7.4 Run app in foreground: SKIPPED.
- 7.5 Verify `hiby_player` still running: SKIPPED.
- 7.6 Kill app cleanly: SKIPPED.
- 7.7 M4B playback test: SKIPPED.

Reason: explicit instruction from the latest inter-session message said to stop device smoke testing and skip Phase 7 entirely.

### Phase 8: Music Regression

- 8.1 Music database intact: PASS as static review. The app uses `/usr/data/audiobooks/library.db`, and there are no code references to `usrlocal_media.db` or the stock music database path.
- 8.2 No audiobook leakage in music: PASS as static review. `db_delete_orphan_books()` only touches audiobook tables inside the audiobook database, and the scanner writes only to audiobook-owned tables.
- 8.3 Stock player still runs: SKIPPED in this pass. This requires a live device/process check, which was explicitly disabled together with Phase 7.

## New Issues Found

- Theme color math is wrong. The current code/comment says `0x130F`, but exact RGB565 conversion of `0x1062F2` is `0x131E`.
- The checked-in `r1-audiobooks-2.0.0-audiobook.upt` artifact is stale and still contains the 9.97 MB unstripped app binary instead of the current 2.57 MB stripped build.
- `book_search` is created and queried, but there is no visible insert path in `app/src` that populates it. Likewise, `authors` and `series` tables are queried by the UI but not populated by the scanner. This looks like a low-severity functional gap for search and the AUTHORS/SERIES views.

## Must Fix Before Flash

- Regenerate the `.upt` package from the current stripped `build/r1_audiobook_app`.
- Correct the HiBy blue constant to match the actual RGB565 conversion if `0x1062F2` is the intended source color.
- If search, AUTHORS, or SERIES are expected in this release, add the missing population path before flashing.

## Final Verdict

**ISSUES FOUND**

The bulk of the earlier fixes are present in source, but the flashable package is still stale and the UI color claim does not match the actual math. Because Phase 7 was explicitly skipped, runtime smoke validation remains unperformed in this pass.
