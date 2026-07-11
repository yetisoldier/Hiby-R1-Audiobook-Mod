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
