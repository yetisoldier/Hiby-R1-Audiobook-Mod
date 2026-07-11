# Phase 2 Validation Report - HiBy R1 Audiobook Mod

Date: 2026-07-11

## Verdict

**ISSUES FOUND**

The Phase 2 refactor compiles cleanly and the regression suite passes, but there are still important functional and safety gaps in the daemon logic. The largest problems are the incomplete state machine, completion detection that can still fire on a near-end seek, and several unbounded string-copy sites in the daemon state handling.

## Checks

1. `src/state.c` state machine completeness: **FAIL**
   - The new enum states are defined in [`src/state.h`](./src/state.h), but the daemon only transitions between `STATE_IDLE` and `STATE_AUDIOBOOK_TRACKING`.
   - Relevant lines:
     - [`src/state.h`](./src/state.h): 20-31
     - [`src/state.c`](./src/state.c): 529-530, 797-810
   - Issue:
     - `STATE_BOOK_OPENED`, `STATE_TRACK_LOADING`, `STATE_TRACK_READY`, and `STATE_BOOK_COMPLETED` are effectively unreachable in the current C daemon.
     - `state_poll_cycle()` still behaves like a polling implementation, not the event-driven state machine described in the Phase 2 task and spec.

2. `src/state.h` enum and runtime struct: **FAIL**
   - The enum is present, but the declared state progression is not implemented in `state.c`.
   - Relevant lines:
     - [`src/state.h`](./src/state.h): 20-31, 34-115
   - Issue:
     - The runtime struct contains many state-machine fields, but the poll cycle does not populate or consume the new phase fields (`state_entered_at`, `loading_deadline_at`, `active_*`, `last_position_ms`, etc.).
     - This makes the struct larger than the behavior currently needs, which is a RAM and maintenance concern on a 56 MB device.

3. `src/config.h` field coverage: **PASS with notes**
   - All `FIELD()` entries in `config.c` refer to real `daemon_config` members.
   - No invalid member names were found in the field table.
   - Note:
     - Several config fields appear unused outside config parsing / summary output, which suggests leftover knobs from earlier designs.

4. `src/config.c` loading and defaults: **PASS with notes**
   - Defaults are populated and `config_load_file()` / env overrides work as a standard 3-tier load order.
   - Relevant lines:
     - [`src/config.c`](./src/config.c): 148-253, 331-403
   - Note:
     - The config parser is permissive and does not reject unknown keys, which is acceptable for this firmware style.
     - Several fields are still unused by the daemon code path and likely need cleanup or explicit documentation as compatibility-only settings.

5. `src/resume.c` smart rewind and accidental-start protection: **PASS**
   - Smart rewind tiers match the spec table in practice:
     - under 5 minutes -> no rewind
     - 5 minutes to 1 hour -> short rewind
     - 1 hour to 24 hours -> medium rewind
     - beyond 24 hours / reboot-like case -> long rewind
   - Relevant lines:
     - [`src/resume.c`](./src/resume.c): 434-474
     - [`src/state.c`](./src/state.c): 586-595, 736-743
   - Accidental-start protection exists and prevents the old bookmark from being overwritten until playback moves past the protected threshold.

6. `src/ui.c` and `src/ui.h` stubs: **PASS**
   - Both files are effectively empty stubs with no framebuffer or touch behavior.
   - Relevant lines:
     - [`src/ui.c`](./src/ui.c): 1-6
     - [`src/ui.h`](./src/ui.h): 1-9
   - No remaining references to old UI functions were found in `src/`.

7. `src/shadow.c` play mode wrapper: **PASS**
   - `shadow_wrap_play_mode()` is a no-op stub and does not reintroduce UI behavior.
   - Relevant lines:
     - [`src/shadow.c`](./src/shadow.c): 114-119

8. `tools/patch_hiby_player.py` direct-open patch: **PASS**
   - `AUDIOBOOK_DIRECT_OPEN_PATCHES` is present and the cave blob is well formed.
   - The hook at `0x540A80` redirects execution into the cave with an 8-byte `j` + `nop` sequence.
   - The patch is gated behind the `--audiobook-direct-open` flag.
   - Relevant lines:
     - [`tools/patch_hiby_player.py`](./tools/patch_hiby_player.py): 264-356, 839-969
   - Cave validation:
     - cave size: 239 bytes
     - hook replacement size: 8 bytes
     - patch application is conditional on `audiobook_direct_open`

9. Compile check: **PASS**
   - Command run:
     - `/home/yetisoldier/tools/zig/zig cc -target mipsel-linux-musleabi -Os -static -s -o /tmp/karen_validate_daemon src/*.c`
   - Result:
     - zero errors
     - zero warnings
     - binary produced successfully

10. Regression test: **PASS**
    - Command run:
      - `python3 tools/test_phase2_regression.py`
    - Result:
      - 19 PASS, 0 FAIL

11. Spec compliance check: **FAIL**
    - Relevant spec sections:
      - [`docs/audiobook-feature-spec.md`](./docs/audiobook-feature-spec.md): 7.3, 7.4, 8, 9.1, 18.2, 18.3
    - Passes:
      - No framebuffer pixels, no touch injection, and no timing-delay UI automation remain in `src/`.
      - Smart rewind tiers are aligned with the spec.
      - The direct-open patch does open the selected track path directly.
    - Failures:
      - **§8**: completion is still detected by `pos + 500 >= dur` on the final track in `state.c`, which can fire on a near-end seek rather than only on natural EOF.
      - **§18.2 / §18.3**: the daemon code has not fully moved to a stable event-driven selection model; the state machine is still poll-based and the new states are not actually used.

## Issues Found

### 1. State machine is incomplete and the new states are unreachable
- Severity: **Major**
- Files:
  - [`src/state.h`](./src/state.h): 20-31
  - [`src/state.c`](./src/state.c): 529-530, 797-810
- Why this matters:
  - The Phase 2 task calls for an event-driven progression through `IDLE -> BOOK_OPENED -> TRACK_LOADING -> TRACK_READY -> TRACKING -> BOOK_COMPLETED`.
  - The current daemon only records `STATE_IDLE` and `STATE_AUDIOBOOK_TRACKING`, so the new states are dead code.
  - This increases the risk of hidden transition bugs because the implementation is not actually exercising the new model.
- Recommended fix:
  - Add explicit state transitions for book open, track loading, track ready, active tracking, and completion.
  - Use `state_entered_at` / `loading_deadline_at` or remove them if they are not needed.
  - Add a regression test that verifies each transition is reachable.

### 2. Completion detection can still trigger on a near-end seek
- Severity: **Major**
- Files:
  - [`src/state.c`](./src/state.c): 695-727
- Why this matters:
  - The code marks a book complete when the final track is within 500 ms of the end, regardless of how that position was reached.
  - That still violates the spec requirement that completion be based on natural EOF, not simply seeking near the end.
- Recommended fix:
  - Gate completion on a real playback-end event or on a transition that proves the decoder reached EOF naturally.
  - Do not finalize completion solely from a position threshold check.

### 3. Several string copies in state handling do not guarantee a terminating NUL
- Severity: **Major**
- Files:
  - [`src/state.c`](./src/state.c): 611-613, 641-643, 668-669, 678-682, 793-794, 800-810
  - [`src/resume.c`](./src/resume.c): 218-229, 311-323, 397-399
- Why this matters:
  - These `strncpy(..., size - 1)` calls do not explicitly write the final `'\0'`.
  - If a source string reaches the buffer limit, later `strcmp`, `log_msg`, or record-update logic can read past the end of the array.
  - On a firmware image, that is a real memory-safety risk.
- Recommended fix:
  - Replace these copies with a small helper that always writes the terminator, or follow every bounded copy with `dest[sizeof(dest) - 1] = '\0';`.
  - Audit the same pattern across `resume.c` and `state.c` because the issue repeats in multiple places.

### 4. Dead or compatibility-only config knobs remain in the config table
- Severity: **Minor**
- Files:
  - [`src/config.h`](./src/config.h): 15-106
  - [`src/config.c`](./src/config.c): 41-142
- Why this matters:
  - Several fields are present in the struct and config table but do not appear to be used by the daemon code path anymore.
  - That adds maintenance overhead and makes the runtime footprint harder to reason about on the R1.
- Recommended fix:
  - Either wire the fields into the state machine or mark them explicitly as compatibility/future-use fields.
  - If they are intentionally kept, document that in `config.h` so they are not mistaken for live behavior.

## Final Assessment

The Phase 2 refactor is **close**, but not release-ready yet.

The code builds and the regression suite passes, but the daemon still needs:
- a real state-machine implementation,
- stricter EOF-based completion detection,
- and cleanup of the unsafe copy patterns.

Until those are fixed, I would not flash this to hardware.

## Re-validation After Fixes

I re-read the current `src/state.c`, `src/state.h`, and `src/resume.c` directly and reran the build and regression checks. I did not rely on the earlier report.

1. State machine states reachable: **FAIL**
   - The new transitions are present for `STATE_IDLE -> STATE_BOOK_OPENED -> STATE_TRACK_LOADING -> STATE_TRACK_READY -> STATE_TRACKING`.
   - However, `STATE_BOOK_COMPLETED` is still not practically reachable because the completion branch depends on `rt->last_position_ms >= pos`, and `last_position_ms` is never assigned anywhere in `src/`.
   - Result:
     - The transition chain is incomplete.
     - `STATE_BOOK_COMPLETED` remains effectively dead in normal execution.

2. Completion detection only on natural EOF: **FAIL**
   - The new guard does require both `pos >= dur` and `rt->last_position_ms >= pos`.
   - That is the right shape for avoiding near-end seeks, but the fix is not complete because `last_position_ms` is never updated in the source tree.
   - Result:
     - A seek near the end will not incorrectly complete the book.
     - But neither will natural EOF, because the second half of the guard cannot become true.

3. `strncpy` NUL termination: **FAIL**
   - Several copies are now correctly terminated, and some copies into zero-initialized buffers are safe by construction.
   - But three runtime string fields in `state.c` still lack an explicit terminating write after `strncpy(..., size - 1)`:
     - `book_title_restore_wait_log_key`
     - `book_title_pre_restore_log_key`
     - `book_title_autostart_reset_key`
   - Result:
     - The fix reduced the problem, but it did not fully eliminate the unsafe pattern.

4. New issues introduced by the fixes: **YES**
   - `last_position_ms` is read by the completion logic but never written, which makes the new EOF gate non-functional.
   - The string-copy audit still shows missing terminators on runtime fields in `state.c`.
   - The `strncpy` updates in `resume.c` are mostly fine because the destination structs/buffers are zeroed first, but that does not compensate for the remaining `state.c` misses.

5. Build and regression validation: **PASS**
   - Compile command:
     - `/home/yetisoldier/tools/zig/zig cc -target mipsel-linux-musleabi -Os -static -s -Wall -Wextra -o /tmp/karen_revalidate src/*.c`
   - Result:
     - compiled cleanly
   - Regression command:
     - `python3 tools/test_phase2_regression.py`
   - Result:
     - 19 PASS, 0 FAIL
   - Binary size:
     - `/tmp/karen_revalidate` = `141492` bytes
     - Under the `150 KB` limit

## Re-validation Verdict

**ISSUES REMAIN**

My independent assessment is that the fixes are not complete enough for release:
- the state machine still has a dead completion path,
- the EOF completion guard is not actually live,
- and the string-copy cleanup is incomplete in `state.c`.

## Final Re-validation

1. `last_position_ms` tracking and EOF guard: **PASS**
   - `pos_stopped` is computed before the assignment:
     - [`src/state.c`](./src/state.c): 592-596
   - `rt->last_position_ms` is assigned immediately after the comparison:
     - [`src/state.c`](./src/state.c): 595-596
   - The natural-EOF completion gate uses `pos_stopped`:
     - [`src/state.c`](./src/state.c): 755-759
   - Result:
     - The guard now matches the intended logic: natural EOF is detected by observing that playback position stopped advancing.

2. `strncpy` NUL termination audit: **FAIL**
   - Verified terminated sites in `src/state.c`:
     - `book_title_restore_wait_log_key`: [`src/state.c`](./src/state.c): 151-153
     - `book_title_pre_restore_log_key`: [`src/state.c`](./src/state.c): 165-167
     - `book_title_autostart_reset_key`: [`src/state.c`](./src/state.c): 620-622
     - `last_path`, `restored_path`, `completed_saved_path`, `deferred_overwrite_path`, and the other bounded copies are also explicitly terminated in their immediate follow-up lines.
   - Verified terminated sites in `src/resume.c`:
     - `rec->book_id`, `rec->book_key`, `rec->root_hiby_path`, `rec->current_path`, `rec->chapter_title`, `rec->updated_at`: [`src/resume.c`](./src/resume.c): 219-240
     - local `book_id`: [`src/resume.c`](./src/resume.c): 323-324
     - `dir`: [`src/resume.c`](./src/resume.c): 410-411
     - `failure_path`, `failure_kind`, `failure_key`: [`src/resume.c`](./src/resume.c): 565-582
     - `failure_path` and `failure_kind` in the track-failure path: [`src/resume.c`](./src/resume.c): 592-598
   - Remaining issue:
     - `tmpl.book_key` and `tmpl.chapter_title` in the natural-EOF completion block still lack an immediate `dest[sizeof(dest) - 1] = 0;` after `strncpy`:
       - [`src/state.c`](./src/state.c): 774-780
   - Result:
     - The termination audit is not fully clean yet. The completion-path template copies still need the explicit NUL write.

3. Compile and regression: **PASS**
   - Compile command:
     - `/home/yetisoldier/tools/zig/zig cc -target mipsel-linux-musleabi -Os -static -s -Wall -Wextra -o /tmp/karen_final /home/yetisoldier/projects/hiby-r1-codex/src/*.c`
   - Result:
     - zero errors
     - zero warnings
   - Regression command:
     - `python3 tools/test_phase2_regression.py`
   - Result:
     - 19 PASS, 0 FAIL
   - Binary size:
     - `/tmp/karen_final` = `141540` bytes
     - under the `150 KB` limit

4. Overall verdict: **ISSUES REMAIN**
   - The `last_position_ms` EOF guard is now implemented correctly.
   - The build, regression, and size checks all pass.
   - The final blocker is the remaining missing NUL termination on `tmpl.book_key` and `tmpl.chapter_title` in `src/state.c`.

## Karen Sign-Off

PASS: `src/state.c` now has explicit NUL termination after both `strncpy` calls for `tmpl.book_key` and `tmpl.chapter_title`.
PASS: `/home/yetisoldier/tools/zig/zig cc -target mipsel-linux-musleabi -Os -static -s -Wall -Wextra -o /tmp/karen_signoff src/*.c`
PASS: `python3 tools/test_phase2_regression.py` = 19/19

Final verdict: READY TO FLASH
