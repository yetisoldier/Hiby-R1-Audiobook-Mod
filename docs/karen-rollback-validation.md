# Karen Rollback Validation

Overall verdict: **ISSUES FOUND**

## Check Results

1. **Arm-window burst logic in `src/state.c`**: **PASS**
   - `state_arm_window_begin()` arms the window with a monotonic deadline and immediate first poll.
   - `state_arm_window_burst()` retries at the configured cadence, exits on success, and clears arm state on timeout or completion.
   - No obvious race conditions were introduced in the current single-threaded event loop.
   - Relevant lines: `src/state.c:384-433`, `src/state.c:438-527`

2. **New arm-window fields in `src/state.h`**: **PASS**
   - `book_title_arm_deadline_ms`, `book_title_arm_next_poll_ms`, and `book_title_arm_active` are present and typed appropriately.
   - Relevant lines: `src/state.h:57-64`

3. **Arm-window config in `src/config.h` and `src/config.c`**: **PASS**
   - `AUDIOBOOK_ARM_WINDOW_MS` and `AUDIOBOOK_ARM_POLL_MS` are defined in the config table.
   - Defaults are `1000` ms and `200` ms, which are sane for the intended bounded burst.
   - `AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED` defaults to `0`.
   - Relevant lines: `src/config.c:87-92`, `src/config.c:204-215`, `src/config.c:454-455`

4. **Direct-open patch removal in `tools/patch_hiby_player.py`**: **PASS**
   - Exact removed symbols are no longer present in the patcher.
   - No `AUDIOBOOK_DIRECT_OPEN_PATCHES`, no `audiobook_direct_open_cave()`, and no `--audiobook-direct-open` CLI flag were found.

5. **Build flag removal in `tools/build_firmware.py`**: **PASS**
   - `--include-audiobook-direct-open-patch` is not present.
   - The remaining `--audiobook-direct-open-helper` flag is helper-related and not the removed binary patch include.
   - Relevant lines: `tools/build_firmware.py:940-960`

6. **Wrapper script in `tools/r1_audiobook_resume_daemon_wrapper.sh`**: **PASS**
   - Starts with `#!/bin/sh`.
   - ASCII-only, no NUL bytes.
   - Uses `exec` to hand off to the compiled daemon or fallback shell script.
   - Relevant lines: `tools/r1_audiobook_resume_daemon_wrapper.sh:1-23`

7. **Shebang verifier in `tools/verify_r1_audiobook_build.py`**: **PASS**
   - `assert_ascii_shell_wrapper()` verifies non-ELF, no NUL bytes, ASCII decoding, and a `#!/bin/sh` prefix.
   - The build verifier applies this check to the daemon wrapper.
   - Relevant lines: `tools/verify_r1_audiobook_build.py:478-493`, `tools/verify_r1_audiobook_build.py:1219-1220`

8. **Regression tests in `tools/test_phase2_regression.py`**: **PASS**
   - The test now checks arm-window plumbing, monotonic timing, and the default-off direct-open config.
   - It also confirms the direct-open patch symbols are absent.
   - Relevant lines: `tools/test_phase2_regression.py:54-77`

9. **Compile check**: **PASS**
   - Command completed with exit code `0`.
   - No compiler errors or warnings were emitted.
   - Output binary size: `142836` bytes, which is under the `150000` byte limit.

10. **Regression test run**: **PASS**
    - `python3 tools/test_phase2_regression.py` completed successfully.
    - Result: `22 PASS, 0 FAIL`

11. **No direct-open patch references anywhere in the codebase**: **FAIL**
    - Stale references to the removed patch still exist in docs and the regression test.
    - `docs/task1-binary-patch-analysis.md:16` and `docs/task1-binary-patch-analysis.md:89` still mention the old `--audiobook-direct-open` patch flag.
    - `docs/karen-phase2-validation.md:68` and `docs/karen-phase2-validation.md:70` still mention `AUDIOBOOK_DIRECT_OPEN_PATCHES` and `--audiobook-direct-open`.
    - `tools/test_phase2_regression.py:2` and `tools/test_phase2_regression.py:59-60` still name the removed patch symbols.

12. **Daemon binary size reasonable**: **PASS**
    - `/tmp/karen_rollback_validate` is `142836` bytes.

13. **Wrapper ASCII and shebang**: **PASS**
    - Verified ASCII-only and `#!/bin/sh` prefix.

14. **`AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED` defaults to `0`**: **PASS**
    - Default is set in `src/config.c`.
    - The runtime/install scripts also propagate `0` by default.
    - Relevant lines: `src/config.c:215`, `tools/verify_installed.py:357`, `tools/adb_verify_installed_audiobook_release.ps1:203-210`

15. **Spec §18.2 safety check**: **PASS**
    - No framebuffer or touch-injection logic was reintroduced in the reviewed daemon path.
    - The only new timing is a bounded, marker-triggered retry window inside the daemon state machine.

16. **Spec §18.3 event-driven check**: **PASS**
    - The arm-window burst is triggered by the marker-driven autostart path, not by a continuous polling loop.
    - The retry loop is bounded and exits after the window expires.

## Findings

### 1. Arm-window burst is not reachable for all marker reasons

- **Severity:** Medium
- **File:** `src/state.c:515-524`
- **Issue:** `state_maybe_autostart()` only enters the new arm-window burst for `launcher`, `context`, and `relaxed` reasons. The `path` and `catalog` reasons are skipped entirely, even though the code computes `allow_ms` for those cases.
- **Why it matters:** This makes the new bounded retry path unreachable in some marker-triggered start scenarios, which weakens the rollback's intended event-driven pre-arm behavior.
- **Recommended fix:** Either include `path` and `catalog` in the arm-window branch, or remove the dead `allow_ms` logic and document the intended trigger set explicitly.

### 2. Stale direct-open patch references remain in docs and tests

- **Severity:** Low
- **Files:**
  - `docs/task1-binary-patch-analysis.md:16`
  - `docs/task1-binary-patch-analysis.md:89`
  - `docs/karen-phase2-validation.md:68`
  - `docs/karen-phase2-validation.md:70`
  - `tools/test_phase2_regression.py:2`
  - `tools/test_phase2_regression.py:59-60`
- **Issue:** The removed direct-open patch is still referenced by name in documentation and in the regression test's negative checks.
- **Why it matters:** This fails the strict "no references anywhere" safety check and keeps stale architecture naming in the tree.
- **Recommended fix:** Rewrite or remove the stale references, or relax the requirement to allow historical/documentation mentions while keeping implementation references removed.

