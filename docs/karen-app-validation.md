# HiBy R1 Audiobook App Validation Report

Date: 2026-07-11  
Reviewer: Karen, QA Agent  
Target: `r1_audiobooks-1.9.0-audiobook.upt`

## Executive Summary

Overall verdict: **ISSUES FOUND**

The build and packaging checks pass, and the binary is the expected MIPS32 LE static ELF under the size limit. The main blockers are in resume correctness, completion handling, and scanner ordering. The app is not flash-ready yet because the multi-file resume path is functionally wrong.

## Check Results

| Check | Result | Notes |
|---|---|---|
| Compile check | PASS | `sh app/build.sh` completed with no emitted warnings or errors. |
| Binary type | PASS | `build/r1_audiobook_app` is a 32-bit MIPS ELF, statically linked, stripped. |
| Binary size | PASS | `build/r1_audiobook_app` is 2,045,192 bytes, under the 5 MB limit. |
| Source review | FAIL | Resume, completion, and scanner behavior do not fully meet the spec. |
| Memory safety | PASS with caveats | No high-confidence buffer overflow, UAF, double-free, or fd leak was found in the reviewed paths. There are robustness gaps that affect correctness more than memory safety. |
| Spec compliance | FAIL | Resume, completion, track ordering, and event-driven behavior are incomplete. |
| Firmware packaging | PASS | Overlay and build scripts stage the app and launcher correctly. |
| Test review | PASS with caveats | Test structure is reasonable, but the MIPS-on-x86 execution path needs cross-architecture handling. |

## Must Fix Before Flash

### 1. Resume seek is wrong for multi-file books

Severity: **Critical**  
Files:
- [`app/src/main.c`](../app/src/main.c#L151-L163)
- [`app/src/player.c`](../app/src/player.c#L60-L67)
- [`app/src/player.c`](../app/src/player.c#L235-L254)

Issue:
- The app stores `snap.position_ms` as an absolute book position in `main.c`.
- `player_open_book()` stores that value in `pending_seek_ms`.
- `open_transport_locked()` then passes `pending_seek_ms` directly to `decoder_seek_ms()` as a track-local seek when `pending_seek` is set.
- That means a resume at chapter 2 or later seeks to the wrong offset within the selected track.

Impact:
- Resume on any multi-file audiobook can jump to the wrong position.
- This breaks the core “tap a title and continue from the correct sentence” behavior.

Recommended fix:
- Store both book-level position and track-local position explicitly, or compute the local offset from `track_ordinal` and total book elapsed time after the decoder is ready.
- Clamp the seek to the track duration before issuing the decoder seek.
- Add a regression test for resuming chapter 2+ in a multi-track book.

## Issues Found

### Major

#### 2. Scanner does not guarantee spec-defined track ordering

Files:
- [`app/src/scanner.c`](../app/src/scanner.c#L102-L126)
- [`app/src/scanner.c`](../app/src/scanner.c#L195-L197)

Issue:
- `collect_tracks()` consumes `readdir()` order and assigns ordinals in that order.
- There is no explicit sort by disc number, track number, embedded chapter start, natural numeric filename order, then alphabetical order.
- Disc-folder recursion exists, but file ordering inside a book is still filesystem-order dependent.

Impact:
- Multi-file books can play chapters in the wrong order.
- This is a direct spec mismatch for chapter ordering and can break playback consistency.

Recommended fix:
- Collect tracks first, then sort them using the precedence in §5.4 before persisting and before creating the playback queue.
- Add tests for `1.mp3`, `2.mp3`, `10.mp3` ordering and disc-folder merging.

#### 3. Natural EOF does not persist finished state

Files:
- [`app/src/main.c`](../app/src/main.c#L143-L149)
- [`app/src/main.c`](../app/src/main.c#L151-L173)
- [`app/src/resume.c`](../app/src/resume.c#L71-L75)

Issue:
- The worker emits `AB_EVT_EOF_REACHED`, and `resume_on_event()` marks the in-memory resume state completed.
- However, `main.c` never writes `progress.completed = 1` or updates the books table when EOF is reached.
- The Finished screen exists in UI code, but there is no persistence path that moves a finished book out of Continue Listening.

Impact:
- Completed books will not reliably appear in Finished.
- Continue Listening will keep showing books that have actually ended.

Recommended fix:
- On final-track EOF, write a completed progress record and update the book’s completed flag in the database atomically.
- Clear or preserve the resume point according to the restart rules in the spec.

#### 4. Resume protection and smart rewind are not wired into the actual playback path

Files:
- [`app/src/resume.c`](../app/src/resume.c#L57-L64)
- [`app/src/main.c`](../app/src/main.c#L151-L173)
- [`app/src/ui.c`](../app/src/ui.c#L108-L126)

Issue:
- `resume_smart_rewind_ms()` exists, but I did not find any call site in the app runtime path.
- `protected_until_ms` exists in the schema, but it is not used to protect a meaningful saved position from accidental restarts.
- Progress writes happen on a periodic timer and on exit, not on the lifecycle events required by §7.2 such as pause, seek, track change, and explicit navigation.

Impact:
- The app does not satisfy the resume reliability contract.
- A quick accidental start can overwrite a useful resume point.

Recommended fix:
- Integrate smart rewind into the book-open path.
- Add protection logic so a fresh restart does not immediately erase a good saved point.
- Save on pause, seek, track change, and exit, not only on the timer.

#### 5. Track ID upsert returns an unreliable value on updates

File:
- [`app/src/db.c`](../app/src/db.c#L106-L130)

Issue:
- `db_upsert_track()` returns `sqlite3_last_insert_rowid()` even when the statement resolves via `ON CONFLICT DO UPDATE`.
- On update, that value is not the updated row’s ID.

Impact:
- Rescans can assign the wrong track ID to in-memory rows.
- That can corrupt downstream references such as resume or bookmarks if the caller relies on the returned ID.

Recommended fix:
- Use `RETURNING track_id` if supported, or explicitly query the row ID after the upsert using the track path.

### Minor

#### 6. UI initialization ignores touch-open failure

File:
- [`app/src/ui.c`](../app/src/ui.c#L19-L29)

Issue:
- If `touch_open()` fails, `ui_init()` calls `touch_close()` and still returns success.

Impact:
- The app can start in a degraded state without usable touch input.

Recommended fix:
- Return failure when the configured touch device cannot be opened, unless there is an intentional non-touch fallback mode.

## Spec Compliance Notes

### §3.3 Direct title tap

PASS.

`ui_handle_touch()` opens the selected book directly and switches to Now Playing without an intermediate track list flash.

Relevant files:
- [`app/src/ui.c`](../app/src/ui.c#L145-L150)
- [`app/src/ui.c`](../app/src/ui.c#L108-L126)

### §7 Resume reliability

FAIL.

The code has the pieces, but the runtime path does not correctly apply smart rewind or protect against accidental overwrite, and the seek offset bug breaks multi-file resume.

### §8 Completion detection on natural EOF

FAIL.

EOF is detected, but finished state is not persisted.

### §9 Book-bounded private queue

PARTIAL PASS.

The queue is book-bounded and does not obviously escape into music playback, but track order is filesystem-dependent and repeat/shuffle behavior is not explicitly enforced in the runtime path.

Relevant files:
- [`app/src/queue.c`](../app/src/queue.c#L18-L46)
- [`app/src/player.c`](../app/src/player.c#L124-L189)

### §18.2 No framebuffer pixels for detection

PASS.

The framebuffer is used for rendering only; I did not find pixel-reading detection logic.

### §18.3 Event-driven, not polling

FAIL.

The main loop uses a 200 ms timeout around `touch_poll()` and then polls player state each iteration.

Relevant files:
- [`app/src/main.c`](../app/src/main.c#L96-L177)
- [`app/src/touch.c`](../app/src/touch.c#L31-L65)

## Memory Safety Review

No high-confidence memory corruption bug was identified in the reviewed modules.

What I checked:
- Buffer writes in `common.c`, `resume.c`, `ipc.c`, `ui.c`, `scanner.c`, and `db.c`
- Thread ownership and shutdown in `player.c`
- fd lifecycle in `fb.c`, `touch.c`, `ipc.c`, `alsa.c`, and `db.c`

Residual risk:
- `player.c` uses a worker thread with shared state, so any future change that touches decoder or ALSA state outside the lock needs careful review.
- `db_clear_library()` does not explicitly roll back on failure, which can leave SQLite in a transaction state if an error occurs mid-clear.

## Packaging Review

### `tools/firmware_overlay.json`

PASS.

The manifest includes both:
- `usr/bin/r1_audiobook_app`
- `usr/bin/r1_audiobook_launch.sh`

Relevant lines:
- [`tools/firmware_overlay.json`](../tools/firmware_overlay.json#L32-L41)

### `tools/build_firmware.py`

PASS.

The staging logic copies the built app and launcher wrapper into the rootfs and marks the launcher executable.

Relevant lines:
- [`tools/build_firmware.py`](../tools/build_firmware.py#L421-L487)
- [`tools/build_firmware.py`](../tools/build_firmware.py#L610-L611)

### `tools/r1_audiobook_launch.sh`

PASS.

The wrapper checks for the app binary, retries launches, and exits with a clear failure if the app keeps crashing.

Relevant lines:
- [`tools/r1_audiobook_launch.sh`](../tools/r1_audiobook_launch.sh#L1-L21)

### Firmware artifact

PASS.

The UPT file exists and is a reasonable size for the project:
- `r1-audiobooks-1.9.0-audiobook.upt`: 43,198,464 bytes

## Test Review

`app/tests/test_app.py` has a sensible shape:
- schema/scanner validation
- resume logic validation
- IPC protocol validation

Relevant lines:
- [`app/tests/test_app.py`](../app/tests/test_app.py#L42-L73)
- [`app/tests/test_app.py`](../app/tests/test_app.py#L91-L138)
- [`app/tests/test_app.py`](../app/tests/test_app.py#L141-L193)

Issue:
- The scanner test invokes the built MIPS binary directly, which will fail on x86 unless there is an emulator or device-targeted test runner.

Recommended cross-architecture fixes:
1. Split tests into host-native unit tests and device integration tests.
2. Run the audiobook app under `qemu-mipsel` for host CI if emulation is available.
3. Keep pure logic tests for scanner, resume, and IPC helpers in a host-compiled test binary.
4. Mark device-only tests explicitly so local x86 CI can skip them cleanly.

## Final Verdict

**ISSUES FOUND**

Not ready to flash yet. The critical resume-seek bug and the unfinished completion flow need to be fixed before this should go onto a physical R1.
