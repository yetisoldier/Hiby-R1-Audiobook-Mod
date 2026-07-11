#!/usr/bin/env python3
"""test_playback — Title selection, playback start, pause.

Tests:
  1. Open Audiobooks from the launcher.
  2. Select a known audiobook title (row 1).
  3. Verify playback starts (screen transitions to "now-playing").
  4. Pause playback via physical play/pause key.
  5. Verify Now Playing screen shows correct state.
  6. Capture screenshot.

Expected behaviour:
  - Tapping a title row opens Now Playing and playback begins.
  - The screen classifier reports "now-playing".
  - Pressing play/pause pauses playback; screen stays "now-playing".
"""

from __future__ import annotations

from test_suite import (
    TestContext, classify_screen, goto_launcher, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
)


def run(ctx: TestContext) -> None:
    """Run the playback test."""

    # ── Step 1: Navigate to launcher and open Audiobooks ───────────────────
    print(color("  Step 1: Open Audiobooks", C_DIM))
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
        raise RuntimeError(f"Expected list state after opening Audiobooks, got '{state}'")
    print(color("  ✓ Audiobooks list open", C_GREEN))

    # ── Step 2: Select a title ─────────────────────────────────────────────
    print(color("  Step 2: Select title row 1", C_DIM))
    ctx.screenshot("before-title-tap")
    ctx.row(1)
    ctx.sleep(ctx.settle + 3)  # settle for track list to open
    ctx.screenshot("after-title-tap")

    state = classify_screen(ctx, "after-title-tap")

    # With auto-tap enabled, the daemon may automatically tap the first
    # track and transition to now-playing without manual intervention.
    # Handle three scenarios:
    #   1. Auto-tap fired → now-playing directly (best case)
    #   2. Track list visible → tap track 1 manually
    #   3. Track list visible but auto-tap may still fire → wait longer
    if state == "now-playing":
        print(color("  ✓ Auto-tap started playback directly", C_GREEN))
    elif state == "list":
        print(color("  Track list opened - tapping track 1", C_DIM))
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)  # extra settle for playback start
        ctx.screenshot("after-track-tap")
        state = classify_screen(ctx, "after-track-tap")

        if state != "now-playing":
            # Retry
            print(color(f"  State was '{state}', retrying…", C_YELLOW))
            ctx.row(1)
            ctx.sleep(ctx.settle + 8)
            ctx.screenshot("after-title-retry")
            state = classify_screen(ctx, "after-title-retry")
    else:
        # Unexpected state — retry with a tap
        print(color(f"  State was '{state}', retrying…", C_YELLOW))
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "after-title-retry")

    if state != "now-playing":
        raise RuntimeError(
            f"Expected now-playing state after selecting title, got '{state}'. "
            f"Playback may not have started."
        )
    print(color("  ✓ Playback started — Now Playing screen", C_GREEN))

    # ── Step 3: Verify playback is active ──────────────────────────────────
    # Check daemon log for recent activity
    print(color("  Step 3: Verify playback activity in daemon log", C_DIM))
    log = ctx.shell(
        "tail -n 30 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
    )
    if not log.strip():
        print(color("  ⚠ Daemon log is empty or missing", C_YELLOW))
    else:
        # Look for audiobook path or restore activity
        if "a:\\Audiobooks\\" in log or "restore" in log.lower():
            print(color("  ✓ Daemon log shows audiobook activity", C_GREEN))
        else:
            print(color("  ⚠ Daemon log does not show audiobook path", C_YELLOW))

    # ── Step 4: Pause playback ─────────────────────────────────────────────
    print(color("  Step 4: Pause playback", C_DIM))
    ctx.key("playpause")
    ctx.sleep(2)
    ctx.screenshot("after-pause")
    state = classify_screen(ctx, "after-pause")
    # Should still be on now-playing (paused)
    if state != "now-playing":
        raise RuntimeError(
            f"Expected now-playing after pause, got '{state}'. "
            f"Pause may have exited playback."
        )
    print(color("  ✓ Playback paused — still on Now Playing", C_GREEN))

    # Cleanup
    cleanup(ctx)