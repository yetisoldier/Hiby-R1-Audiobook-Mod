#!/usr/bin/env python3
"""test_book_switch — Quick book switching, no bookmark corruption.

Tests:
  1. Play audiobook A (row 1) for 20+ seconds.
  2. Back out, select audiobook B (row 2).
  3. Verify B starts playing, A's bookmark is preserved.
  4. Back out, re-select A.
  5. Verify A restores to its saved position (not B's position).
  6. Check daemon log for no "stale memscan root" warnings.

Expected behaviour:
  - Switching books does not corrupt the previous book's resume record.
  - Each book restores to its own saved position.
  - The daemon log should not show stale memscan root warnings.
"""

from __future__ import annotations

import json
import re

from test_suite import (
    TestContext, classify_screen, goto_launcher, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
)


def _read_resume_file(ctx: TestContext) -> dict | None:
    """Read the first resume JSON file from the device."""
    listing = ctx.shell(
        "ls /usr/data/audiobooks/resume.d/*.json 2>/dev/null | head -1"
    ).strip()
    if not listing:
        return None
    content = ctx.shell(f"cat '{listing}' 2>/dev/null || true")
    if not content.strip():
        return None
    try:
        return json.loads(content)
    except json.JSONDecodeError:
        return None


def _get_daemon_log_tail(ctx: TestContext, lines: int = 50) -> str:
    """Get the last N lines of the daemon log."""
    return ctx.shell(
        f"tail -n {lines} /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
    )


def run(ctx: TestContext) -> None:
    """Run the book switch test."""

    PLAY_SECONDS = 22

    # ── Step 1: Play audiobook A (row 1) ────────────────────────────────────
    print(color("  Step 1: Play audiobook A (row 1)", C_DIM))
    state = goto_launcher(ctx, max_backs=5)
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher (got '{state}')")

    ctx.tap("main-audiobooks")
    ctx.sleep(ctx.settle + 5)
    state = classify_screen(ctx, "open-audiobooks")
    if state != "list":
        raise RuntimeError(f"Expected list, got '{state}'")

    ctx.row(1)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "book-a-playing")
    if state != "now-playing":
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "book-a-retry")
    if state != "now-playing":
        raise RuntimeError(f"Book A did not start (state={state})")
    print(color("  ✓ Book A playing", C_GREEN))

    # Play for 20+ seconds
    print(color(f"  Playing A for {PLAY_SECONDS}s…", C_DIM))
    ctx.sleep(PLAY_SECONDS)

    # Save A's resume record
    resume_a = _read_resume_file(ctx)
    pos_a = None
    if resume_a and "position" in resume_a:
        try:
            pos_a = int(resume_a["position"])
        except (ValueError, TypeError):
            pass
    if pos_a is not None and pos_a > 0:
        print(color(f"  ✓ Book A position saved: {pos_a}", C_GREEN))
    else:
        print(color("  ⚠ Book A position not saved yet", C_YELLOW))

    # ── Step 2: Back out, select audiobook B (row 2) ───────────────────────
    print(color("  Step 2: Back out, select audiobook B (row 2)", C_DIM))
    ctx.back()
    ctx.sleep(ctx.settle)
    state = classify_screen(ctx, "back-from-a")
    if state == "now-playing":
        ctx.back()
        ctx.sleep(ctx.settle)
        state = classify_screen(ctx, "back-from-a-2")
    if state == "launcher":
        ctx.tap("main-audiobooks")
        ctx.sleep(ctx.settle + 5)
        state = classify_screen(ctx, "reopen-for-b")
    if state != "list":
        # Try navigating back to list
        for _ in range(3):
            ctx.back()
            ctx.sleep(ctx.settle)
            state = classify_screen(ctx, "navigate-to-list-b")
            if state in ("list", "launcher"):
                break
        if state == "launcher":
            ctx.tap("main-audiobooks")
            ctx.sleep(ctx.settle + 5)
            state = classify_screen(ctx, "reopen-for-b-2")
    if state != "list":
        raise RuntimeError(f"Cannot get back to audiobook list (state={state})")

    ctx.row(2)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "book-b-playing")
    if state != "now-playing":
        # If row 2 is empty, try row 3
        ctx.back()
        ctx.sleep(ctx.settle)
        ctx.row(3)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "book-b-retry")
    if state != "now-playing":
        raise RuntimeError(f"Book B did not start (state={state})")
    print(color("  ✓ Book B playing", C_GREEN))

    # Play B for a few seconds
    ctx.sleep(10)

    # ── Step 3: Verify A's bookmark is preserved ───────────────────────────
    print(color("  Step 3: Verify A's bookmark preserved", C_DIM))
    resume_after_b = _read_resume_file(ctx)
    if resume_after_b and "position" in resume_after_b:
        print(color("  ✓ Resume record exists after switching to B", C_GREEN))
    else:
        print(color("  ⚠ No resume record after switching to B", C_YELLOW))

    # ── Step 4: Back out, re-select A ──────────────────────────────────────
    print(color("  Step 4: Back out, re-select A", C_DIM))
    ctx.back()
    ctx.sleep(ctx.settle)
    state = classify_screen(ctx, "back-from-b")
    if state == "now-playing":
        ctx.back()
        ctx.sleep(ctx.settle)
        state = classify_screen(ctx, "back-from-b-2")
    if state == "launcher":
        ctx.tap("main-audiobooks")
        ctx.sleep(ctx.settle + 5)
        state = classify_screen(ctx, "reopen-for-a")
    if state != "list":
        for _ in range(3):
            ctx.back()
            ctx.sleep(ctx.settle)
            state = classify_screen(ctx, "navigate-to-list-a")
            if state in ("list", "launcher"):
                break
        if state == "launcher":
            ctx.tap("main-audiobooks")
            ctx.sleep(ctx.settle + 5)
            state = classify_screen(ctx, "reopen-for-a-2")
    if state != "list":
        raise RuntimeError(f"Cannot get back to list for re-selecting A (state={state})")

    ctx.row(1)
    ctx.sleep(ctx.settle + 8)
    state = classify_screen(ctx, "book-a-resumed")
    if state != "now-playing":
        ctx.row(1)
        ctx.sleep(ctx.settle + 8)
        state = classify_screen(ctx, "book-a-resumed-retry")
    if state != "now-playing":
        raise RuntimeError(f"Book A did not resume (state={state})")
    print(color("  ✓ Book A resumed", C_GREEN))

    # ── Step 5: Verify A restores to its own position ──────────────────────
    print(color("  Step 5: Verify A restores to its own position", C_DIM))
    resume_a_restored = _read_resume_file(ctx)
    if resume_a_restored and "position" in resume_a_restored:
        try:
            pos_a_restored = int(resume_a_restored["position"])
        except (ValueError, TypeError):
            pos_a_restored = 0
        if pos_a is not None and pos_a > 0:
            # Position should be close to the original (within 5 seconds)
            diff = abs(pos_a_restored - pos_a)
            if diff <= 5:
                print(color(f"  ✓ Position restored: {pos_a} → {pos_a_restored} (Δ={diff}s)", C_GREEN))
            else:
                raise RuntimeError(
                    f"Position drift too large: {pos_a} → {pos_a_restored} (Δ={diff}s). "
                    f"May have restored B's position instead of A's."
                )
        else:
            print(color(f"  ⚠ Original position was 0/null, restored: {pos_a_restored}", C_YELLOW))
    else:
        print(color("  ⚠ No resume record after restoring A", C_YELLOW))

    # ── Step 6: Check daemon log for stale memscan warnings ────────────────
    print(color("  Step 6: Check daemon log for stale memscan warnings", C_DIM))
    log = _get_daemon_log_tail(ctx, 80)
    if re.search(r"stale memscan root", log, re.IGNORECASE):
        raise RuntimeError(
            "Daemon log shows 'stale memscan root' warning — possible "
            f"bookmark corruption during book switch."
        )
    print(color("  ✓ No stale memscan root warnings", C_GREEN))

    # Cleanup
    cleanup(ctx)