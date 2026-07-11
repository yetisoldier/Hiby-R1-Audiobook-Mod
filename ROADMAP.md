# Roadmap

Forward-looking work items for the HiBy R1 Audiobook Firmware Mod.

## Current Status (2026-07-11)

All 8 architecture optimizations (P1-P8) are complete:
- ✅ P1: C resume daemon (11 modules, 170 tests, LIVE mode on R1)
- ✅ P2: PowerShell → Python tooling (24 scripts)
- ✅ P3: Overlay-based build system
- ✅ P4: Article normalization (already implemented)
- ✅ P5: Cover art guidance
- ✅ P6: Documentation consolidation
- ✅ P7: Automated regression test suite (2/3 smoke tests PASS on R1)
- ✅ P8: Route-table research (complete, no safe Back fix found)

### C Daemon Status (LIVE — shadow mode retired)
The C resume daemon is running as the primary daemon (shadow=0) on the R1.
- ✅ Path detection, track detection, position reads all working
- ✅ Save/restore active
- ✅ Auto-tap (Approach A: framebuffer-based) — **WORKING** (at_fired=3, at_skipped=0)
- ✅ Autostart activation after auto-tap
- ✅ Follow-up Now Playing tap after auto-tap
- ✅ Idle→audiobook autostart transition
- ⬜ test_resume smoke test (timing issue, not a functionality issue)

### Seamless Autoplay (Phase 1 complete)
- ✅ Framebuffer-based auto-tap (Approach A) deployed and working
- ✅ Replaces broken path-based detection (at_fired=0 → at_fired=3)
- ⬜ Phase 2: Static analysis for extended autostart marker hook point
- ⬜ Phase 3: Extended marker + direct-open pre-arm

## Next Steps

1. **Fix test_resume timeout** — the third smoke test times out due to slow navigation; likely needs tap_frames or timeout tuning
2. **Run full regression test suite** — `python3 tests/test_suite.py --suite full` on the R1
3. **Phase 2: Static analysis** — find the explorer file-open callback address in `hiby_player` for the extended autostart marker
4. **Phase 3: Extended marker + direct-open** — add second marker hook, enable pre-arm with saved track index
5. **Tune auto-tap timing** — evaluate whether track-list flash can be reduced further

## Longer-Term Research

- **Custom Audiobooks list view**: P8 research confirmed no safe route-table fix exists. A custom list view in `hiby_player` would be needed for clean Back navigation. Target: `vg_listview_book` handler at `0x00490fa0`.
- **Author/Series views**: Title view is stable. Author needs catalog isolation. Series has catalog data with Seanap/Plex folders.
- **True direct resume**: The `0x49e200` shared media-open path supports off-screen row selection. The direct-open helper can force saved track index.
- **Audible-inspired features**: Sleep timers, bookmarks, narration speed — wait until resume/browse is production-stable.
- **QEMU system emulation**: Not ready for release validation. Host-native + ADB tests remain more useful.