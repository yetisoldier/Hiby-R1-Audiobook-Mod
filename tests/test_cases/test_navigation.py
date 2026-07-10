#!/usr/bin/env python3
"""test_navigation — Back navigation, launcher return.

Tests:
  1. Open Audiobooks from launcher.
  2. Navigate into a title, then back out.
  3. Count back presses needed to reach launcher.
  4. Log the count (don't fail — this is a known limitation, just document).
  5. Verify no crashes or reboots during navigation.

Expected behaviour:
  - Navigation should work without crashes.
  - The number of back presses to reach launcher is documented but not
    treated as a failure (known UX wart from genre-route approach).
  - The device should not reboot or crash during navigation.
"""

from __future__ import annotations

import re

from test_suite import (
    TestContext, classify_screen, goto_launcher, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
)


def _check_process_alive(ctx: TestContext, pattern: str) -> bool:
    """Check if a process matching the pattern is still running."""
    output = ctx.shell(f"ps | grep -E '{pattern}' | grep -v grep || true")
    return bool(output.strip())


def run(ctx: TestContext) -> None:
    """Run the navigation test."""

    MAX_BACKS = 10

    # ── Step 1: Open Audiobooks from launcher ──────────────────────────────
    print(color("  Step 1: Open Audiobooks from launcher", C_DIM))
    state = goto_launcher(ctx, max_backs=5)
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher (got '{state}')")

    ctx.tap("main-audiobooks")
    ctx.sleep(ctx.settle + 5)
    state = classify_screen(ctx, "audiobooks-open")
    if state != "list":
        raise RuntimeError(f"Expected list after opening Audiobooks, got '{state}'")
    print(color("  ✓ Audiobooks list open", C_GREEN))

    # ── Step 2: Navigate into a title ───────────────────────────────────────
    print(color("  Step 2: Navigate into a title", C_DIM))
    ctx.row(1)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "title-opened")
    if state != "now-playing":
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "title-retry")
    if state != "now-playing":
        raise RuntimeError(f"Could not open a title (state={state})")
    print(color("  ✓ Title opened — Now Playing", C_GREEN))

    # Verify key processes are alive before navigation
    print(color("  Step 3: Verify processes before navigation", C_DIM))
    hiby_alive = _check_process_alive(ctx, "hiby_player")
    daemon_alive = _check_process_alive(ctx, "r1_audiobook_resume_daemon")
    if not hiby_alive:
        raise RuntimeError("hiby_player process not found — may have crashed")
    print(color("  ✓ hiby_player alive", C_GREEN))
    if not daemon_alive:
        print(color("  ⚠ Resume daemon not found", C_YELLOW))
    else:
        print(color("  ✓ Resume daemon alive", C_GREEN))

    # ── Step 4: Count back presses to reach launcher ──────────────────────
    print(color("  Step 4: Count back presses to reach launcher", C_DIM))
    back_count = 0
    for i in range(MAX_BACKS):
        back_count += 1
        ctx.back()
        ctx.sleep(ctx.settle)
        state = classify_screen(ctx, f"nav-back-{i+1}")
        if state == "launcher":
            break

    if state == "launcher":
        print(color(f"  ✓ Reached launcher after {back_count} back press(es)", C_GREEN))
    else:
        print(color(
            f"  ⚠ Did not reach launcher after {MAX_BACKS} back presses "
            f"(state={state})", C_YELLOW
        ))

    # Log the count — this is a known limitation, not a failure
    print(color(f"  📝 Back presses needed: {back_count}", C_CYAN))
    if back_count > 2:
        print(color(
            f"  📝 Note: {back_count} back presses is a known UX wart "
            f"(genre-route navigation). Not a failure.", C_DIM
        ))

    # ── Step 5: Verify no crashes or reboots ──────────────────────────────
    print(color("  Step 5: Verify no crashes or reboots", C_DIM))
    hiby_alive_after = _check_process_alive(ctx, "hiby_player")
    if not hiby_alive_after:
        raise RuntimeError(
            "hiby_player process not found after navigation — may have crashed or rebooted"
        )
    print(color("  ✓ hiby_player still alive after navigation", C_GREEN))

    daemon_alive_after = _check_process_alive(ctx, "r1_audiobook_resume_daemon")
    if not daemon_alive_after:
        print(color("  ⚠ Resume daemon not found after navigation", C_YELLOW))
    else:
        print(color("  ✓ Resume daemon still alive", C_GREEN))

    # Check daemon log for crash indicators
    log = ctx.shell(
        "tail -n 20 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
    )
    if re.search(r"crash|segfault|killed|SIGSEGV", log, re.IGNORECASE):
        raise RuntimeError(
            "Daemon log shows crash indicators after navigation"
        )
    print(color("  ✓ No crash indicators in daemon log", C_GREEN))

    # Cleanup
    cleanup(ctx)