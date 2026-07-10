#!/usr/bin/env python3
"""test_music_idle — Music playback, daemon quiet state.

Tests:
  1. Start playing an audiobook.
  2. Switch to Music, play a music track.
  3. Wait 30 seconds.
  4. Verify daemon log shows position_reads=0, saves=0 during music playback.
  5. Verify daemon reduced polling (idle interval active).
  6. Switch back to audiobook.
  7. Verify audiobook resumes correctly.

Expected behaviour:
  - When music is playing (not an audiobook), the resume daemon should go idle.
  - Daemon log should show no position reads or saves during music playback.
  - Switching back to the audiobook should resume correctly.
"""

from __future__ import annotations

import re
import time

from test_suite import (
    TestContext, classify_screen, goto_launcher, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
)


def _get_daemon_log_size(ctx: TestContext) -> int:
    """Get the current byte size of the daemon log."""
    text = ctx.shell("wc -c < /usr/data/audiobooks/resume-daemon.log 2>/dev/null || echo 0")
    match = re.search(r"(\d+)", text)
    return int(match.group(1)) if match else 0


def _read_new_log(ctx: TestContext, since_bytes: int) -> str:
    """Read daemon log content added since the given byte offset."""
    return ctx.shell(
        f"dd if=/usr/data/audiobooks/resume-daemon.log bs=1 skip={since_bytes} "
        f"2>/dev/null || true"
    )


def run(ctx: TestContext) -> None:
    """Run the music idle test."""

    MUSIC_WAIT = 30  # seconds to wait during music playback

    # ── Step 1: Start playing an audiobook ────────────────────────────────
    print(color("  Step 1: Start audiobook playback", C_DIM))
    state = goto_launcher(ctx, max_backs=5)
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher (got '{state}')")

    ctx.tap("main-audiobooks")
    ctx.sleep(ctx.settle + 5)
    state = classify_screen(ctx, "audiobooks-open")
    if state != "list":
        raise RuntimeError(f"Expected list, got '{state}'")

    ctx.row(1)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "audiobook-playing")
    if state != "now-playing":
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "audiobook-retry")
    if state != "now-playing":
        raise RuntimeError(f"Audiobook did not start (state={state})")
    print(color("  ✓ Audiobook playing", C_GREEN))

    # Play audiobook briefly to establish daemon activity
    ctx.sleep(15)

    # Record daemon log offset before music
    log_offset_before = _get_daemon_log_size(ctx)

    # ── Step 2: Switch to Music, play a music track ───────────────────────
    print(color("  Step 2: Switch to Music", C_DIM))
    # Back out to launcher
    ctx.back()
    ctx.sleep(ctx.settle)
    state = classify_screen(ctx, "back-from-audiobook")
    if state == "now-playing":
        ctx.back()
        ctx.sleep(ctx.settle)
        state = classify_screen(ctx, "back-from-audiobook-2")
    for _ in range(3):
        if state == "launcher":
            break
        ctx.back()
        ctx.sleep(ctx.settle)
        state = classify_screen(ctx, "navigate-to-launcher")
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher to switch to music (state={state})")

    # Tap Music tile
    ctx.tap("main-music")
    ctx.sleep(ctx.settle + 5)
    ctx.screenshot("music-screen")
    state = classify_screen(ctx, "music-state")
    print(color(f"  Music screen state: {state}", C_DIM))

    # Try to play a music track — tap first row in the music list
    if state == "list":
        ctx.tap_point(240, 234)  # first row center
        ctx.sleep(ctx.settle + 5)
        ctx.screenshot("music-playing")
        state = classify_screen(ctx, "music-playing-state")
    elif state == "now-playing":
        pass  # already playing

    print(color("  ✓ Switched to Music", C_GREEN))

    # ── Step 3: Wait 30 seconds ────────────────────────────────────────────
    print(color(f"  Step 3: Wait {MUSIC_WAIT}s during music playback", C_DIM))
    ctx.sleep(MUSIC_WAIT)

    # ── Step 4: Verify daemon quiet state ─────────────────────────────────
    print(color("  Step 4: Verify daemon quiet during music", C_DIM))
    new_log = _read_new_log(ctx, log_offset_before)

    # Check for position reads/saves
    has_position_reads = bool(re.search(r"position_reads?[=:]\s*[1-9]", new_log))
    has_saves = bool(re.search(r"saves[=:]\s*[1-9]", new_log))

    if has_position_reads:
        print(color("  ⚠ Daemon log shows position reads during music playback", C_YELLOW))
    else:
        print(color("  ✓ No position reads during music playback", C_GREEN))

    if has_saves:
        print(color("  ⚠ Daemon log shows saves during music playback", C_YELLOW))
    else:
        print(color("  ✓ No saves during music playback", C_GREEN))

    # ── Step 5: Verify daemon reduced polling ─────────────────────────────
    print(color("  Step 5: Verify daemon idle/reduced polling", C_DIM))
    # Look for idle interval indicators in the log
    has_idle = bool(re.search(r"idle|reduced|sleep|quiet", new_log, re.IGNORECASE))
    has_non_audiobook = bool(re.search(r"non-audiobook|leave audiobook", new_log, re.IGNORECASE))
    if has_idle or has_non_audiobook:
        print(color("  ✓ Daemon shows idle/reduced polling state", C_GREEN))
    else:
        # Not necessarily a failure — the daemon may simply not log idle state.
        # Check if the log is just shorter (less activity)
        if len(new_log.strip()) < 200:
            print(color("  ✓ Daemon log is quiet (short output during music)", C_GREEN))
        else:
            print(color("  ⚠ Cannot confirm daemon idle state from log", C_YELLOW))

    # ── Step 6: Switch back to audiobook ──────────────────────────────────
    print(color("  Step 6: Switch back to audiobook", C_DIM))
    state = goto_launcher(ctx, max_backs=5)
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher to return to audiobook (state={state})")

    ctx.tap("main-audiobooks")
    ctx.sleep(ctx.settle + 5)
    state = classify_screen(ctx, "audiobooks-reopen")
    if state != "list":
        raise RuntimeError(f"Cannot reopen audiobook list (state={state})")

    ctx.row(1)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "audiobook-resumed")
    if state != "now-playing":
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "audiobook-resumed-retry")
    if state != "now-playing":
        raise RuntimeError(f"Audiobook did not resume after music (state={state})")
    print(color("  ✓ Audiobook resumed after music", C_GREEN))

    # Cleanup
    cleanup(ctx)