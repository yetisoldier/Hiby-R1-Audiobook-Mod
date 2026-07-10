#!/usr/bin/env python3
"""test_play_mode — Play-mode enforcement for audiobooks.

Tests:
  1. Start audiobook playback.
  2. Read /usr/data/user.ini offset 0x250 (decimal 592).
  3. Verify the value is 3 (sequential/list-loop) for audiobooks.
  4. Stop audiobook, start music playback.
  5. Verify the play mode is NOT forced to 3 during music playback.

Expected behaviour:
  - The firmware mod sets the play-mode byte at user.ini offset 0x250 to 3
    (list-loop) when an audiobook is playing, to ensure sequential track
    progression.
  - When music is playing, this byte should not be forced to 3 — it should
    reflect the user's chosen play mode.
"""

from __future__ import annotations

import re

from test_suite import (
    TestContext, classify_screen, goto_launcher, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
)

USER_INI_REMOTE = "/usr/data/user.ini"
PLAY_MODE_OFFSET = 592  # decimal offset 0x250
EXPECTED_AUDIOBOOK_MODE = 3  # sequential / list-loop


def _read_play_mode(ctx: TestContext) -> int | None:
    """Read the play-mode byte at offset 0x250 from user.ini on the device."""
    # Use dd to read one byte at the offset
    output = ctx.shell(
        f"dd if={USER_INI_REMOTE} bs=1 skip={PLAY_MODE_OFFSET} count=1 "
        f"2>/dev/null | od -An -tu1 | tr -d ' \\n'"
    )
    output = output.strip()
    if not output:
        return None
    try:
        return int(output)
    except ValueError:
        return None


def run(ctx: TestContext) -> None:
    """Run the play-mode test."""

    # ── Step 1: Start audiobook playback ──────────────────────────────────
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

    # Wait a moment for the daemon to set the play mode
    ctx.sleep(5)

    # ── Step 2: Read play-mode byte ───────────────────────────────────────
    print(color(f"  Step 2: Read user.ini offset 0x250 ({PLAY_MODE_OFFSET})", C_DIM))
    mode_audiobook = _read_play_mode(ctx)
    if mode_audiobook is None:
        raise RuntimeError(
            f"Cannot read play-mode byte at {USER_INI_REMOTE}:{PLAY_MODE_OFFSET}"
        )
    print(color(f"  Play-mode byte: {mode_audiobook}", C_DIM))

    # ── Step 3: Verify audiobook play mode is 3 ────────────────────────────
    print(color("  Step 3: Verify audiobook play mode is 3 (list-loop)", C_DIM))
    if mode_audiobook == EXPECTED_AUDIOBOOK_MODE:
        print(color(f"  ✓ Play mode = {EXPECTED_AUDIOBOOK_MODE} (sequential/list-loop)", C_GREEN))
    else:
        raise RuntimeError(
            f"Audiobook play mode should be {EXPECTED_AUDIOBOOK_MODE}, "
            f"got {mode_audiobook}. The firmware may not be enforcing "
            f"sequential play mode for audiobooks."
        )

    # ── Step 4: Stop audiobook, start music ───────────────────────────────
    print(color("  Step 4: Stop audiobook, start music", C_DIM))
    ctx.key("playpause")  # pause audiobook
    ctx.sleep(2)

    # Navigate to launcher and open Music
    state = goto_launcher(ctx, max_backs=5)
    if state != "launcher":
        raise RuntimeError(f"Cannot reach launcher for music (state={state})")

    ctx.tap("main-music")
    ctx.sleep(ctx.settle + 5)
    ctx.screenshot("music-opened")
    state = classify_screen(ctx, "music-open")
    print(color(f"  Music screen state: {state}", C_DIM))

    # Try to start music playback
    if state == "list":
        ctx.tap_point(240, 234)  # first row center
        ctx.sleep(ctx.settle + 5)
        state = classify_screen(ctx, "music-playing")
    elif state == "now-playing":
        pass  # already playing

    # Wait for play mode to potentially change
    ctx.sleep(5)

    # ── Step 5: Verify music play mode is NOT forced to 3 ──────────────────
    print(color("  Step 5: Verify music play mode is NOT 3", C_DIM))
    mode_music = _read_play_mode(ctx)
    if mode_music is None:
        raise RuntimeError(
            f"Cannot read play-mode byte at {USER_INI_REMOTE}:{PLAY_MODE_OFFSET}"
        )
    print(color(f"  Play-mode byte (music): {mode_music}", C_DIM))

    if mode_music == EXPECTED_AUDIOBOOK_MODE:
        raise RuntimeError(
            f"Music play mode is {EXPECTED_AUDIOBOOK_MODE} (list-loop), same as audiobook. "
            f"The firmware may be incorrectly forcing play mode for music."
        )
    else:
        print(color(f"  ✓ Music play mode = {mode_music} (not forced to 3)", C_GREEN))

    # Cleanup
    cleanup(ctx)