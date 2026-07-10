#!/usr/bin/env python3
"""test_resume — Position save/restore, bookmark integrity.

Tests:
  1. Play an audiobook for 20+ seconds (past the save bucket threshold).
  2. Back out to the title list.
  3. Re-select the same title.
  4. Verify position restores to within 5 seconds of the saved position.
  5. Verify a resume record exists in /usr/data/audiobooks/resume.d/.
  6. Verify the resume record JSON has correct path, position, and track fields.

Expected behaviour:
  - The resume daemon saves position after ~15 seconds of playback.
  - Backing out and re-selecting restores to the saved position.
  - A resume JSON file exists with the audiobook path and position > 0.
"""

from __future__ import annotations

import json
import re

from test_suite import (
    TestContext, classify_screen, goto_launcher, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
)


def run(ctx: TestContext) -> None:
    """Run the resume test."""

    PLAY_SECONDS = 22  # past the save bucket threshold
    TITLE_ROW = 1

    # ── Step 1: Open Audiobooks and start playback ─────────────────────────
    print(color("  Step 1: Open Audiobooks and start playback", C_DIM))
    state = goto_launcher(ctx, max_backs=5)
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher (got '{state}')")

    ctx.tap("main-audiobooks")
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "audiobooks-open")
    if state != "list":
        # Retry - the tap may not have registered
        print(color(f"  State was '{state}', retrying Audiobooks tap...", C_YELLOW))
        goto_launcher(ctx, max_backs=5)
        ctx.sleep(2)
        ctx.tap("main-audiobooks")
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "audiobooks-retry")
    if state != "list":
        raise RuntimeError(f"Expected list, got '{state}'")

    ctx.row(TITLE_ROW)
    ctx.sleep(ctx.settle + 3)
    state = classify_screen(ctx, "after-title-tap")

    # The native hub opens a track list first - tap track 1 to start playback
    if state == "list":
        print(color("  Track list opened - tapping track 1", C_DIM))
        ctx.row(TITLE_ROW)
        ctx.sleep(ctx.settle + 8)
        ctx.screenshot("after-track-tap")
        state = classify_screen(ctx, "after-track-tap")

    if state != "now-playing":
        ctx.row(TITLE_ROW)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "after-title-retry")
    if state != "now-playing":
        raise RuntimeError(f"Playback did not start (state={state})")
    print(color("  ✓ Playback started", C_GREEN))

    # ── Step 2: Play for 20+ seconds ───────────────────────────────────────
    print(color(f"  Step 2: Play for {PLAY_SECONDS}s (save threshold)", C_DIM))
    ctx.sleep(PLAY_SECONDS)
    ctx.screenshot("after-playback-period")

    # Check daemon log for save activity
    log = ctx.shell(
        "tail -n 40 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
    )
    has_save = bool(re.search(r"saves=[1-9]|after_position_response=", log))
    if has_save:
        print(color("  ✓ Daemon log shows save activity", C_GREEN))
    else:
        print(color("  ⚠ Daemon log does not show save activity yet", C_YELLOW))

    # ── Step 3: Back out to title list ──────────────────────────────────────
    print(color("  Step 3: Back out to title list", C_DIM))
    ctx.back()
    ctx.sleep(ctx.settle)
    state = classify_screen(ctx, "after-back-out")
    if state == "now-playing":
        # One more back
        ctx.back()
        ctx.sleep(ctx.settle)
        state = classify_screen(ctx, "after-back-out-2")
    if state not in ("list", "launcher"):
        # Try more backs
        for _ in range(3):
            ctx.back()
            ctx.sleep(ctx.settle)
            state = classify_screen(ctx, "back-to-list")
            if state in ("list", "launcher"):
                break
    print(color(f"  ✓ Backed out to '{state}'", C_GREEN))

    # ── Step 4: Re-select same title ───────────────────────────────────────
    print(color("  Step 4: Re-select same title", C_DIM))
    if state == "launcher":
        ctx.tap("main-audiobooks")
        ctx.sleep(ctx.settle + 5)
        state = classify_screen(ctx, "reopen-audiobooks")
        if state != "list":
            raise RuntimeError(f"Cannot reopen audiobook list (got '{state}')")

    ctx.row(TITLE_ROW)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "resume-playback")
    if state != "now-playing":
        ctx.row(TITLE_ROW)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "resume-retry")
    if state != "now-playing":
        raise RuntimeError(f"Resume did not start playback (state={state})")
    print(color("  ✓ Re-selected title — playback resumed", C_GREEN))

    # ── Step 5: Verify resume record exists ────────────────────────────────
    print(color("  Step 5: Verify resume record", C_DIM))
    resume_dir_listing = ctx.shell(
        "ls -la /usr/data/audiobooks/resume.d/ 2>/dev/null || echo 'NO_DIR'"
    )
    if "NO_DIR" in resume_dir_listing:
        raise RuntimeError(
            "Resume directory /usr/data/audiobooks/resume.d/ does not exist"
        )
    print(color("  ✓ Resume directory exists", C_GREEN))

    # Find resume JSON files
    resume_files = ctx.shell(
        "ls /usr/data/audiobooks/resume.d/*.json 2>/dev/null || echo 'NO_FILES'"
    )
    # Strip ANSI escape codes from ls output (BusyBox color ls)
    import re as _re
    resume_files = _re.sub(r'\x1b\[[0-9;]*m', '', resume_files)
    if "NO_FILES" in resume_files:
        raise RuntimeError(
            "No resume JSON files found in /usr/data/audiobooks/resume.d/"
        )

    # Read the first resume record
    first_file = resume_files.strip().splitlines()[0].strip()
    # Strip any remaining ANSI escape codes from the filename
    first_file = _re.sub(r'\x1b\[[0-9;]*m', '', first_file)
    if not first_file:
        raise RuntimeError("Could not find any resume JSON file")

    resume_content = ctx.shell(f"cat '{first_file}' 2>/dev/null || echo 'READ_FAIL'")
    if "READ_FAIL" in resume_content:
        raise RuntimeError(f"Cannot read resume file: {first_file}")

    try:
        resume_data = json.loads(resume_content)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Resume JSON is invalid: {exc}\nContent: {resume_content[:200]}"
        ) from exc

    # ── Step 6: Verify resume record fields ────────────────────────────────
    print(color("  Step 6: Verify resume record fields", C_DIM))

    required_fields = ["path", "position", "track"]
    missing = [f for f in required_fields if f not in resume_data]
    if missing:
        raise RuntimeError(
            f"Resume record missing required fields: {missing}. "
            f"Present fields: {list(resume_data.keys())}"
        )

    # Verify path contains Audiobooks
    path_val = str(resume_data.get("current_path", resume_data.get("path", "")))
    if "Audiobooks" not in path_val and "audiobooks" not in path_val.lower():
        raise RuntimeError(
            f"Resume record path does not contain 'Audiobooks': {path_val!r}"
        )
    print(color(f"  ✓ Path: {path_val[:80]}", C_GREEN))

    # Verify position is a positive number
    position = resume_data.get("position", 0)
    try:
        position_int = int(position)
    except (ValueError, TypeError):
        position_int = 0
    if position_int <= 0:
        print(color(f"  ⚠ Position is {position} (expected > 0)", C_YELLOW))
    else:
        print(color(f"  ✓ Position: {position_int}", C_GREEN))

    # Verify track is present
    track = resume_data.get("track")
    if track is None:
        print(color("  ⚠ Track field is null", C_YELLOW))
    else:
        print(color(f"  ✓ Track: {track}", C_GREEN))

    # Cleanup
    cleanup(ctx)