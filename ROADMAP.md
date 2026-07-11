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
- ✅ P7: Automated regression test suite (3/3 smoke tests PASS on R1)
- ✅ P8: Route-table research (complete, no safe Back fix found)

### C Daemon Status (LIVE — shadow mode retired)
The C resume daemon is running as the primary daemon (shadow=0) on the R1.
- ✅ Path detection, track detection, position reads all working
- ✅ Save/restore active
- ✅ Auto-tap (Approach A: framebuffer-based) — **WORKING** (at_fired=2, at_skipped=1)
- ✅ Autostart activation after auto-tap
- ✅ Follow-up Now Playing tap after auto-tap
- ✅ Idle→audiobook autostart transition
- ✅ Music→audiobook restore (confirmed: restored to pt03 at pos=109428)
- ✅ test_resume smoke test PASS (122.9s)

### Seamless Autoplay (Phase 1 complete, Phase 2 research done)
- ✅ Framebuffer-based auto-tap (Approach A) deployed and working
- ✅ Replaces broken path-based detection (at_fired=0 → at_fired=2)
- ✅ Phase 2: Static analysis complete (see findings below)
- ⬜ Phase 3: Extended marker + direct-open pre-arm (deprioritized — see findings)

### Phase 2 Static Analysis Findings (2026-07-11)

**Bug fix:** The explorer marker code cave at `0x360E38` had a register encoding
bug — `lw/sw` used `t0` (register 8) instead of `s0` (register 16) as the base.
Fixed: `0x8D080000 → 0x8E080000`, `0xAD080000 → 0xAE080000`.

**Key finding:** `0x4EFE00` is the `.m3u` file-open callback (confirmed by string
check for `"m3u"` at `0x780E2C`). It fires when a `.m3u` file is tapped from the
explorer folder view — NOT when a title row is tapped from the native hub.

**Title row tap flow:**
1. User taps title row → folder view opens showing `.m3u` files
2. User (or daemon) taps `.m3u` → `0x4EFE00` fires → track list appears
3. Daemon auto-taps first track via framebuffer detection

**Assessment:** The extended marker at `0x4EFE00` fires too late in the chain
(step 2, not step 1) to provide the intended early signal. The existing auto-tap
(Approach A) already detects the track list via framebuffer at step 3. The
marker would save only ~200-500ms of track-list flash, which is marginal.

**Recommendation:** Deprioritize Phase 3 (extended marker + direct-open pre-arm).
The auto-tap (Approach A) is working well enough. Future improvement should
focus on reducing daemon poll interval or framebuffer detection speed rather
than adding another binary patch.

### Smoke Test Results (2026-07-11 10:38 CDT)
All 3 smoke tests PASS on R1 hardware:
- test_launcher: PASS (63.3s)
- test_playback: PASS (92.9s)
- test_resume: PASS (122.9s)

Fix: Added retry logic to `capture_screenshot` (3 retries with backoff) and
`invoke_control` (2 retries). Reduced per-command timeout from 120s to 30s.
Root cause: USB ADB on the R1 has intermittent hiccups.

## Next Steps

1. **Package v1.7.0 release** — the C daemon, auto-tap, smoke test fixes, and
   explorer marker bug fix represent a major upgrade over v1.6.1
2. **Run full regression test suite** — `python3 tests/test_suite.py --suite full`
   on the R1 to validate all tests pass before release
3. **Tune auto-tap timing** — evaluate whether track-list flash can be reduced
   further (poll interval, tap delay)
4. **Author/Series views** — Title view is stable. Author needs catalog isolation.
   Series has catalog data with Seanap/Plex folders.
5. **True direct resume** — The `0x49e200` shared media-open path supports
   off-screen row selection. The direct-open helper can force saved track index.

## Longer-Term Research

- **Custom Audiobooks list view**: P8 research confirmed no safe route-table
  fix exists. A custom list view in `hiby_player` would be needed for clean Back
  navigation. Target: `vg_listview_book` handler at `0x00490fa0`.
- **Audible-inspired features**: Sleep timers, bookmarks, narration speed — wait
  until resume/browse is production-stable.
- **QEMU system emulation**: Not ready for release validation. Host-native + ADB
  tests remain more useful.