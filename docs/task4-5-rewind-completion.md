# Task 4+5 Notes: Smart Rewind, Accidental-Start Protection, Completion

## What changed

- Added `last_played_at` and `completed_at` to `resume_record` while keeping schema version 3 compatible.
- Added smart rewind configuration knobs:
  - `AUDIOBOOK_SMART_REWIND_ENABLED`
  - `AUDIOBOOK_REWIND_SHORT_MS`
  - `AUDIOBOOK_REWIND_MEDIUM_MS`
  - `AUDIOBOOK_REWIND_LONG_MS`
- Updated restore target computation to use pause-duration tiers:
  - < 5 minutes: exact position
  - 5 to 60 minutes: short rewind
  - 1 to 24 hours: medium rewind
  - > 24 hours or unknown/reboot-like state: long rewind
- Added daemon runtime protection tracking with `position_protected_until_ms` and `last_paused_at`.
- Added resume-save protection so a book opened at track 1 / position 0 does not overwrite the prior bookmark until playback advances far enough.
- Tightened completion handling to mark finished only on the final track near natural EOF, and to persist `completed=1` with `completed_at`.
- Added completed-book restart handling so a finished book opens from the beginning and clears completed state after playback runs for a few seconds.

## Compatibility notes

- Existing schema v3 records still load. New fields are optional on read and written when saving.
- The current daemon/state branch already uses the refactored state machine names in `state.h`; I added compatibility fields and stubs so the legacy `state.c` implementation can still compile cleanly in this workspace.

## Verification

- `cc -fsyntax-only /home/yetisoldier/projects/hiby-r1-codex/src/*.c` passed.
- `cc -o /tmp/r1_resume_daemon_host /home/yetisoldier/projects/hiby-r1-codex/src/*.c` linked on the host compiler.
- `zig cc -target mipsel-linux-musl -Os -static -s -o r1_audiobook_resume_daemon_c src/*.c` could not be run here because `zig` is not installed in this environment.
