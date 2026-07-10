# Roadmap

Forward-looking work items for the HiBy R1 Audiobook Firmware Mod.

## Current Status (2026-07-10)

All 8 architecture optimizations (P1-P8) are complete:
- ✅ P1: C resume daemon (11 modules, 131 tests, shadow mode deployed on R1)
- ✅ P2: PowerShell → Python tooling (24 scripts)
- ✅ P3: Overlay-based build system
- ✅ P4: Article normalization (already implemented)
- ✅ P5: Cover art guidance
- ✅ P6: Documentation consolidation
- ✅ P7: Automated regression test suite (3/3 smoke tests PASS on R1)
- ✅ P8: Route-table research (complete, no safe Back fix found)

### Shadow Mode Migration (in progress)
The C resume daemon is running in shadow mode on the R1 alongside the shell daemon.
- ✅ Path detection matching
- ✅ Track detection matching
- ✅ Position reads matching (pos=103289 = pos_ms=103289)
- ⬜ Loop count calibration (12 vs 25/min — polling frequency gap)
- ⬜ Save/restore shadow logging
- ⬜ 7-day comparison period
- ⬜ Controlled cutover (Week 4 of migration plan)
- ⬜ Shell daemon retirement (Week 5+)

## Next Steps

1. **Continue shadow mode comparison** — play audiobooks normally on the R1 for several days, pull logs, compare decisions
2. **Fix loop count gap** — adjust C daemon polling to match shell daemon's active-playback frequency
3. **Implement save/restore shadow logging** — C daemon should log what it *would* save/restore
4. **Run full regression test suite** — `python3 tests/test_suite.py --suite full` on the R1
5. **After 7 days of matching logs** — controlled cutover from shell to C daemon

## Longer-Term Research

- **Custom Audiobooks list view**: P8 research confirmed no safe route-table fix exists. A custom list view in `hiby_player` would be needed for clean Back navigation. Target: `vg_listview_book` handler at `0x00490fa0`.
- **Author/Series views**: Title view is stable. Author needs catalog isolation. Series has catalog data with Seanap/Plex folders.
- **True direct resume**: The `0x49e200` shared media-open path supports off-screen row selection. The direct-open helper can force saved track index.
- **Audible-inspired features**: Sleep timers, bookmarks, narration speed — wait until resume/browse is production-stable.
- **QEMU system emulation**: Not ready for release validation. Host-native + ADB tests remain more useful.