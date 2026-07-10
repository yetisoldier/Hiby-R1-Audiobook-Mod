"""Test cases package for the HiBy R1 regression test suite.

Each module in this package exposes a ``run(ctx: TestContext) -> None`` function.
The runner imports modules dynamically and calls ``run``.

Available test modules:

- test_launcher      — Audiobooks launcher entry and back navigation
- test_playback      — Title selection, playback start, pause
- test_resume        — Position save/restore, bookmark integrity
- test_book_switch   — Quick book switching, no bookmark corruption
- test_music_idle    — Music playback, daemon quiet state
- test_db_maintenance — Catalog invariants, no leakage
- test_play_mode     — Play-mode enforcement for audiobooks
- test_navigation    — Back navigation, launcher return
"""